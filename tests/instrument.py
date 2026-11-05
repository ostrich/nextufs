#!/usr/bin/env python3
"""Build an isolated instrumented byte-I/O probe, then run the QCOW2 group."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WRAPS = '-Wl,--wrap=pwrite,--wrap=pread,--wrap=fsync,--wrap=ftruncate'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['sanitize', 'coverage'])
    parser.add_argument('--bin-dir', required=True)
    parser.add_argument('--nextufs', required=True)
    args = parser.parse_args()
    parent = ROOT / '.scratch/instrumentation'
    parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=args.mode + '-', dir=parent))
    compiler = shlex.split(os.environ.get('CC', 'cc'))
    common = [*compiler, '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'src'),
              '-g', '-std=gnu99']
    sources = [ROOT / 'tests/qcow2/backend_probe.c', ROOT / 'tests/qcow2/io_faults.c']
    if args.mode == 'sanitize':
        command = [*common, '-O1', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                   '-o', work / 'probe', *sources, *sorted((ROOT / 'src/core').glob('*.c')), WRAPS]
    else:
        subprocess.run([*common, '-O0', '--coverage', '-c', ROOT / 'src/core/qcow2.c',
                        '-o', work / 'qcow2.o'], check=True)
        # Link the instrumented object first; the archive supplies the other core objects.
        lib = Path(args.bin_dir).resolve().parents[1] / 'obj' / Path(args.bin_dir).name / 'libnextufs.a'
        command = [*common, '-O0', '--coverage', '-o', work / 'probe', *sources,
                   work / 'qcow2.o', lib, WRAPS]
    subprocess.run(command, check=True)
    print(f'Instrumentation: {work}', flush=True)
    result = subprocess.run([sys.executable, '-B', ROOT / 'tests/run.py', 'qcow2',
                             '--bin-dir', args.bin_dir, '--nextufs', args.nextufs, '--probe', work / 'probe',
                             '--keep', '--work-root', work / 'tests'], cwd=ROOT)
    if args.mode == 'coverage':
        report = subprocess.run(['gcov', '-b', '-c', '-o', work, ROOT / 'src/core/qcow2.c'],
                                cwd=work, text=True, capture_output=True)
        (work / 'coverage.txt').write_text(report.stdout + report.stderr)
        print(report.stdout + report.stderr)
        if report.returncode:
            return report.returncode
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
