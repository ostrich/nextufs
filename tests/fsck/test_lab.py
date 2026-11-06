"""Negative controls for the repair oracle and comparison harness."""
import os
from pathlib import Path
import subprocess
import sys
from fixtures import seed, preserved
from model import UFS
from support import ROOT, TOOL, tool

work = Path(sys.argv[1])
before = seed(work / 'seed.raw')
tool('mkimg', '--raw', work / 'empty.raw', '33M')
try:
    preserved(before, UFS(work / 'empty.raw').snapshot(), 'bad-block-count')
except AssertionError:
    pass
else:
    raise AssertionError('empty replacement passed preservation oracle')
# Also reject a payload change even when names and inode metadata are preserved.
after = {p: dict(obj) for p, obj in before.items()}
after['/tail']['sha256'] = 'incorrect'
try:
    preserved(before, after, 'bad-block-count')
except AssertionError:
    pass
else:
    raise AssertionError('changed payload passed oracle')
lab = ROOT / 'tests/fsck/lab/compare_case.sh'
env = dict(os.environ, FSCK_LAB_DIR=str(work / 'lab'))
def compare(reference, status, shipped=TOOL, mode='-n'):
    result = subprocess.run(['bash', str(lab), mode, 'bad-block-count'],
        env=dict(env, FSCK_REFERENCE_BIN=str(reference), NEXTUFS=shipped),
        capture_output=True, timeout=120)
    assert result.returncode == status, (result.returncode, result.stdout, result.stderr)
    return result
compare('/nonexistent/fsck', 2, shipped='/nonexistent/nextufs')
reference = work / 'reference'
reference.write_text('#!/bin/sh\nexec "$NEXTUFS" fsck "$@"\n')
reference.chmod(0o755)
compare(reference, 0)
compare(reference, 0, mode='-y')
compare(reference, 0, mode='-p')
for body in ('exit 8', 'kill -TERM $$', 'echo fake-checker; exit 0',
             'echo unexpected-first-line; exec "$NEXTUFS" fsck "$@"'):
    reference.write_text('#!/bin/sh\n' + body + '\n')
    compare(reference, 1)
