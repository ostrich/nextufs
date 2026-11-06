"""Returned write/flush failures must be visible and repairs must be resumable."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
from fixtures import seed, damage, preserved
from model import UFS
from support import tool, check

work = Path(sys.argv[1])
source = work / 'seed.raw'
before = seed(source)
corrupt = work / 'corrupt.raw'
damage(source, corrupt, 'cg-bitmap-bad')
for fault in ('PWRITE', 'FSYNC'):
    failures = 0
    for point in (1, 2, 3, 5, 8):
        disk = work / (fault + '-' + str(point) + '.raw')
        shutil.copyfile(corrupt, disk)
        result = subprocess.run([os.environ['QCOW2_FAULT_BIN'], 'fsck', '-y', str(disk)],
            capture_output=True, timeout=30,
            env=dict(os.environ, **{'QCOW_FAIL_' + fault: str(point)}))
        (work / (disk.stem + '.log')).write_bytes(result.stdout + result.stderr)
        assert not any(marker in result.stderr for marker in
                       (b'AddressSanitizer', b'LeakSanitizer', b'runtime error:'))
        assert result.returncode in (0, 8), (fault, point, result)
        failures += result.returncode == 8
        tool('fsck', '-y', disk)
        check(disk)
        preserved(before, UFS(disk).snapshot(), 'cg-bitmap-bad')
    assert failures, ('fault injection never reached repair', fault)
