#!/usr/bin/env python3
"""Select and run executable regression cases. Make owns all compilation."""
import argparse
from dataclasses import dataclass
import fnmatch
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
from filesystem.test_operations import CASES as OPERATIONS


@dataclass(frozen=True)
class Case:
    name: str
    command: tuple
    needs: tuple = ()


def cases():
    result = []

    def add(name, script, *args, needs=()):
        interpreter = 'bash' if script.endswith('.sh') else sys.executable
        result.append(Case(name, (interpreter, str(ROOT / 'tests' / script), '{work}', *args), needs))

    add('build/incremental', 'build/test_incremental.py')
    add('api/read', 'api/test_api.py', needs=('api',))
    add('cli/contract', 'cli/test_contract.sh')
    add('cli/browse', 'cli/test_browse.sh')
    add('cli/mkimg', 'cli/test_mkimg.sh')
    add('cli/mount-contract', 'cli/test_mount.py')
    add('filesystem/resize', 'filesystem/test_resize.sh')
    add('filesystem/fragments', 'filesystem/test_fragments.sh', needs=('api',))
    for name in OPERATIONS:
        add('filesystem/' + name, 'filesystem/test_operations.py', name)
    add('filesystem/failure', 'filesystem/test_failure.py')
    add('filesystem/safety', 'filesystem/test_safety.py')
    add('filesystem/malformed', 'filesystem/test_malformed.py')
    add('filesystem/ufs-compat', 'filesystem/test_ufs_compat.py', needs=('fs-probe',))
    add('filesystem/sparse', 'filesystem/test_sparse.py', needs=('fs-probe',))
    add('filesystem/io-failures', 'filesystem/test_io_failures.py', needs=('fault','fs-probe'))
    add('vdi/backend', 'vdi/test_backend.py')
    add('vdi/matrix', 'vdi/test_matrix.py', needs=('fs-probe',))
    add('vdi/parents', 'vdi/test_parents.py')
    add('vdi/failures', 'vdi/test_failures.py', needs=('fault', 'fs-probe'))
    add('vdi/interop', 'vdi/test_interop.py', needs=('qemu', 'fs-probe'))
    result.append(Case('vdi/helper', (sys.executable, '-B', '-m', 'unittest', 'discover',
        '-s', 'tests/tools', '-p', 'test_vdi_chain.py', '-v')))
    add('fsck/images', 'fsck/test_images.sh', needs=('corrupt',))
    for description in sorted((ROOT / 'tests/fsck/cases').glob('*.txt')):
        add('fsck/corpus/' + description.stem, 'fsck/test_corpus.py', description.stem, needs=('corrupt',))
    add('fsck/failures', 'fsck/test_failures.py', needs=('corrupt', 'fault'))
    add('fsck/matrix', 'fsck/test_matrix.py', needs=('corrupt',))
    add('vdi/repair', 'fsck/test_containers.py', 'vdi', needs=('corrupt', 'fs-probe'))
    add('qcow2/repair', 'fsck/test_containers.py', 'qcow2', needs=('corrupt', 'qemu'))
    add('fsck/lab', 'fsck/test_lab.py', needs=('corrupt',))
    result.append(Case('tools/unit', (sys.executable, '-B', '-m', 'unittest', 'discover',
                                      '-s', 'tests/tools', '-p', 'test_*.py', '-v')))
    for name in ['reads', 'cow', 'matrix', 'writes', 'failures']:
        add('qcow2/' + name, f'qcow2/test_{name}.py', needs=('qemu', 'probe', 'fault', 'corrupt'))
    add('fuse/read', 'fuse/test_read.py', needs=('fuse',))
    add('fuse/cleanup', 'fuse/test_cleanup.py', needs=('fuse',))
    add('fuse/write', 'fuse/test_write.py', needs=('fuse',))
    add('fuse/permissions', 'fuse/test_permissions.py', needs=('fuse',))
    for kind in ('raw', 'vdi', 'qcow2'):
        add('fuse/native-' + kind, 'fuse/test_native.py', kind,
            needs=('fuse', 'qemu') if kind == 'qcow2' else ('fuse',))
    for name in OPERATIONS:
        add('fuse/readback-' + name, 'filesystem/test_operations.py', name, '--fuse', needs=('fuse',))
    add('fuse/stress', 'stress/test_stress.py', 'fuse', needs=('fuse', 'stress'))
    add('fuse/qcow2', 'stress/test_stress.py', 'qcow2', needs=('fuse', 'stress', 'qemu'))
    for name in ['offline', 'batch']:
        add('stress/' + name, 'stress/test_stress.py', name, needs=('stress',))
    add('image/read', 'compatibility/test_image.py', needs=('image', 'api'))
    add('image/stress', 'stress/test_stress.py', 'image', needs=('image', 'stress'))
    return result


GROUPS = {
    'default': ('build/*', 'api/*', 'cli/*', 'filesystem/*', 'fsck/*', 'tools/*', 'vdi/backend', 'vdi/matrix', 'vdi/parents', 'vdi/failures', 'vdi/repair'),
    'build': ('build/*',),
    'api': ('api/*',),
    'cli': ('cli/*',),
    'filesystem': ('filesystem/*',),
    'tools': ('tools/*',),
    'fsck': ('fsck/*',),
    'fsck/corpus': ('fsck/corpus/*',),
    'vdi': ('vdi/*', 'filesystem/io-failures', 'fuse/native-vdi'),
    'qcow2': ('qcow2/*',),
    'fuse': ('fuse/*',),
    'stress': ('stress/*',),
    'image': ('image/*',),
    'all': ('build/*', 'api/*', 'cli/*', 'filesystem/*', 'fsck/*', 'tools/*', 'vdi/*', 'qcow2/*', 'fuse/*', 'stress/*'),
}


def select(selectors):
    patterns = [pattern for selector in selectors for pattern in GROUPS.get(selector, (selector,))]
    available = cases()
    for pattern in patterns:
        if not any(fnmatch.fnmatchcase(case.name, pattern) for case in available):
            raise ValueError(f'unknown group or case: {pattern}')
    return [case for case in available if any(fnmatch.fnmatchcase(case.name, p) for p in patterns)]


def environment(args):
    env = dict(os.environ)
    directory = Path(args.bin_dir).resolve()
    env.update(NEXTUFS=str(Path(args.nextufs).resolve()),
               NEXTUFS_TEST=str(directory / 'api'), NEXTUFS_STRESS=str(directory / 'stress'),
               CORRUPT=str(directory / 'corrupt'), FS_PROBE=str(directory / 'fs-probe'),
               QCOW2_PROBE=str(Path(args.probe).resolve()) if args.probe else str(directory / 'qcow2-probe'),
               QCOW2_FAULT_BIN=str(directory / 'qcow2-fault'),
               PYTHONPATH=str(ROOT / 'tests'), PYTHONDONTWRITEBYTECODE='1')
    env.pop('PYTHONOPTIMIZE', None)
    env.pop('PYTHONPYCACHEPREFIX', None)
    if args.image:
        env['TEST_IMAGE'] = str(Path(args.image).resolve())
    return env


def preflight(selected, env):
    needs = {need for case in selected for need in case.needs}
    for executable in ['NEXTUFS', *({'api': 'NEXTUFS_TEST', 'stress': 'NEXTUFS_STRESS',
                                    'corrupt': 'CORRUPT', 'probe': 'QCOW2_PROBE',
                                    'fault': 'QCOW2_FAULT_BIN', 'fs-probe': 'FS_PROBE'}[n]
                                   for n in needs & {'api', 'stress', 'corrupt', 'probe', 'fault', 'fs-probe'})]:
        if not os.access(env[executable], os.X_OK):
            raise ValueError(f'{executable} is not executable: {env[executable]}; build with make test-tools')
    if 'qemu' in needs:
        for command in ['qemu-img', 'qemu-io']:
            if not shutil.which(command):
                raise ValueError(f'requested tests require {command}')
    if 'fuse' in needs:
        if not shutil.which('fusermount3') or not os.access('/dev/fuse', os.R_OK | os.W_OK):
            raise ValueError('requested tests require fusermount3 and readable/writable /dev/fuse')
        help_text = subprocess.run([env['NEXTUFS'], 'mount'], capture_output=True)
        if b'mount support is unavailable' in help_text.stderr:
            raise ValueError('requested FUSE tests require a WITH_FUSE=1 build')
    if 'image' in needs and not Path(env.get('TEST_IMAGE', '')).is_file():
        raise ValueError('image tests require --image /path/to/OPENSTEP.img (Make: TEST_IMAGE=...)')


def execute(case, work, env, timeout):
    command = [str(work) if arg == '{work}' else arg for arg in case.command]
    env = dict(env, TMPDIR=str(work))
    with (work / 'output.log').open('wb') as output:
        output.write((json.dumps(command) + '\n').encode())
        output.flush()
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            return process.wait(timeout=timeout)
        except (subprocess.TimeoutExpired, KeyboardInterrupt):
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('selectors', nargs='*', default=['default'], help='groups, case names or glob patterns')
    parser.add_argument('--list', action='store_true', help='list selected cases without building or running')
    parser.add_argument('--keep', action='store_true', help='retain successful artifacts too')
    parser.add_argument('--work-root', default=str(ROOT / '.scratch/tests'))
    parser.add_argument('--bin-dir', default=str(ROOT / 'build/tests/fuse-1'))
    parser.add_argument('--nextufs', default=os.environ.get('NEXTUFS', str(ROOT / 'nextufs')))
    parser.add_argument('--probe', default=os.environ.get('QCOW2_PROBE'))
    parser.add_argument('--image', default=os.environ.get('TEST_IMAGE'))
    parser.add_argument('--timeout', type=int, default=1800, help='maximum seconds per case')
    args = parser.parse_args(argv)
    try:
        selected = select(args.selectors)
        if args.list:
            for case in selected:
                print(case.name)
            return 0
        env = environment(args)
        preflight(selected, env)
    except ValueError as error:
        parser.error(str(error))
    root = Path(args.work_root).resolve()
    root.mkdir(parents=True, exist_ok=True)
    session = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    results = []
    interrupted = False
    for case in selected:
        work = session / case.name.replace('/', '-')
        work.mkdir()
        print(f'RUN  {case.name}', flush=True)
        start = time.monotonic()
        try:
            status = execute(case, work, env, args.timeout)
        except OSError as error:
            status = f'launch error: {error}'
            with (work / 'output.log').open('a') as log:
                log.write(str(error) + '\n')
        except subprocess.TimeoutExpired:
            status = 'timeout'
        except KeyboardInterrupt:
            status = 'interrupted'
            interrupted = True
        passed = status == 0
        results.append(dict(name=case.name, status=status, seconds=round(time.monotonic() - start, 3)))
        print(f'{"PASS" if passed else "FAIL"} {case.name} ({results[-1]["seconds"]:.1f}s)', flush=True)
        if passed and not args.keep:
            shutil.rmtree(work)
        elif not passed:
            print(f'     artifacts: {work}')
            print((work / 'output.log').read_text(errors='replace')[-6000:])
        if interrupted:
            break
    failed = sum(r['status'] != 0 for r in results)
    print(f'{len(results) - failed} passed, {failed} failed, 0 skipped', flush=True)
    if failed or args.keep:
        (session / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(f'Results: {session}')
    else:
        session.rmdir()
    return 130 if interrupted else int(bool(failed))


if __name__ == '__main__':
    def stop(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop)
    sys.exit(main())
