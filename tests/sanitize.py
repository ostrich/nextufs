#!/usr/bin/env python3
"""Build the complete utility and helpers with fatal ASan/UBSan in an isolated tree."""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from instrument import check_sanitizer_gate, SANITIZER_FLAGS
from support import ROOT
from run import select

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('groups', nargs='*', default=['all'])
parser.add_argument('--without-fuse', action='store_true')
args = parser.parse_args()
selectors = args.groups
if args.without_fuse:
    selectors = [case.name for case in select(selectors) if 'fuse' not in case.needs]
    if not selectors:
        parser.error('the selected cases all require FUSE')
parent = ROOT / '.scratch/instrumentation'
parent.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='utility-', dir=parent))
env = dict(os.environ, NEXTUFS_SANITIZED="1")
for key in (
    'CPPFLAGS',
    'CFLAGS',
    'FORMAT_CFLAGS',
    'FSCK_CFLAGS',
    'LDFLAGS',
    'MAKEFLAGS',
    'MFLAGS',
):
    env.pop(key, None)
common = [*shlex.split(env.get('CC', 'cc')), '-g']
check_sanitizer_gate(common, work)
flags = '-g -Wall -Wextra ' + ' '.join(SANITIZER_FLAGS)
fuse = '0' if args.without_fuse else '1'
build = work / 'build'
subprocess.run(
    [
        'make',
        '-j4',
        f'BUILD_DIR={build}',
        f'WITH_FUSE={fuse}',
        f'CFLAGS={flags} -std=gnu99',
        f'FORMAT_CFLAGS={flags} -std=gnu89',
        f'FSCK_CFLAGS={flags} -std=gnu99 -fcommon',
        'LDFLAGS=-fsanitize=address,undefined',
        'test-binaries',
    ],
    cwd=ROOT,
    env=env,
    check=True,
)
print(f'Instrumentation: {work}', flush=True)
raise SystemExit(
    subprocess.call(
        [
            sys.executable,
            '-B',
            ROOT / 'tests/run.py',
            *selectors,
            '--nextufs',
            build / f'bin/fuse-{fuse}/nextufs',
            '--bin-dir',
            build / f'tests/fuse-{fuse}',
            '--work-root',
            work / 'tests',
            '--keep',
        ],
        cwd=ROOT,
        env=env,
    )
)
