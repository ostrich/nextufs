#!/usr/bin/env python3
"""Compare checker evidence; setup failures can never establish equivalence."""
import os
from pathlib import Path
import subprocess
import sys
import shutil
import tempfile
import json
import hashlib

ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tests/fsck')]
from fixtures import CASES, seed, damage, preserved
from model import UFS


def executable(value):
    resolved = shutil.which(value)
    if not resolved:
        raise ValueError('executable not found: ' + value)
    return resolved


def compare(mode, case, directory, shipped, reference):
    source = directory / 'seed.raw'
    before = seed(source)
    corrupt = directory / 'corrupt.raw'
    damage(source, corrupt, case)
    if mode == '-p':
        data = bytearray(corrupt.read_bytes())
        data[UFS(corrupt).sb + 0xd1] = 2
        corrupt.write_bytes(data)
    reports = []
    for engine, command in [('shipped', [shipped, 'fsck']), ('reference', [reference])]:
        disk = directory / (engine + '.raw')
        shutil.copyfile(corrupt, disk)
        run = subprocess.run([*command, mode, str(disk)], capture_output=True, timeout=120)
        output = run.stdout + run.stderr
        (directory / (engine + '.log')).write_bytes(output)
        assert not any(marker in output for marker in (b'AddressSanitizer', b'LeakSanitizer', b'runtime error:')), output
        if run.returncode not in (0, 8):
            raise AssertionError((engine, 'checker failed', run.returncode, output))
        assert b'skipping check' not in output
        assert (b'Phase 1' in output or b'PHASE 1' in output or
                (mode == '-p' and (b' files, ' in output or b'UNEXPECTED INCONSISTENCY' in output))), (engine, 'no check performed', output)
        if mode == '-n':
            assert run.returncode == 8
            assert disk.read_bytes() == corrupt.read_bytes()
        snapshot = None
        if mode != '-n' and run.returncode == 0:
            snapshot = UFS(disk).snapshot()
            preserved(before, snapshot, case)
            # fsck timestamps may differ across runs; compare structural evidence.
            snapshot = {p: {k: v for k, v in obj.items() if k not in ('atime', 'mtime')}
                        for p, obj in snapshot.items()}
        normalized = output.replace(str(disk).encode(), b'<image>')
        report = dict(status=run.returncode, output=normalized.decode(errors='replace'), snapshot=snapshot)
        (directory / (engine + '.json')).write_text(json.dumps(report, indent=2))
        reports.append(report)
    assert reports[0] == reports[1], 'checker evidence differs; inspect ' + str(directory)


def main():
    mode, selector = sys.argv[1:]
    assert mode in ('-n', '-y', '-p')
    shipped = executable(os.environ.get('NEXTUFS', str(ROOT / 'nextufs')))
    reference = executable(os.environ.get('FSCK_REFERENCE_BIN', ''))
    executable(os.environ.get('CORRUPT', str(ROOT / 'build/tests/fuse-1/corrupt')))
    os.environ['NEXTUFS'] = shipped
    os.environ.setdefault('CORRUPT', str(ROOT / 'build/tests/fuse-1/corrupt'))
    parent = Path(os.environ.get('FSCK_LAB_DIR', ROOT / '.scratch/fsck-lab'))
    parent.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='compare-', dir=parent))
    identities = {name: {'path': path, 'sha256': hashlib.sha256(Path(path).read_bytes()).hexdigest()}
                  for name, path in [('shipped', shipped), ('reference', reference),
                                     ('corrupt', executable(os.environ['CORRUPT']))]}
    (root / 'provenance.json').write_text(json.dumps(identities, indent=2))
    failures = []
    for case in CASES if selector == 'all' else (selector,):
        assert case in CASES, case
        directory = root / case
        directory.mkdir()
        try:
            compare(mode, case, directory, shipped, reference)
            print('PASS', case)
        except (AssertionError, OSError, subprocess.TimeoutExpired) as error:
            print('FAIL', case, error, file=sys.stderr)
            failures.append(case)
    print('Artifacts:', root)
    return bool(failures)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (ValueError, OSError) as error:
        print(error, file=sys.stderr)
        sys.exit(2)
