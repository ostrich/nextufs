"""One-shot raw/VDI I/O faults roll back guest contents; persistent failures poison sessions."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
from support import check, image, read, run, tool, vdi

work = Path(sys.argv[1])
raw = image(work / 'raw.img', '8M', raw=True)
tool('mkfile', raw, '/file', 'original')
base = vdi(raw, work / 'base.vdi')
for source in [raw, base]:
    for operation in [
        ('mkfile', '--pwrite', '/file', '0', 'changed'),
        ('mkfile', '--rename', '/file', '/renamed'),
        ('mkfile', '--truncate', '/file', '24000'),
    ]:
        for fault in ['PWRITE', 'PREAD', 'FSYNC']:
            for point in [1, 2, 5, 12]:
                disk = work / 'fault.img'
                shutil.copyfile(source, disk)
                args = [operation[0], operation[1], str(disk), *operation[2:]]
                result = subprocess.run(
                    [os.environ['QCOW2_FAULT_BIN'], *args],
                    capture_output=True,
                    env=dict(os.environ, **{'QCOW_FAIL_' + fault: str(point)}),
                    timeout=20,
                )
                assert result.returncode in (0, 1), (args, fault, point, result.stderr)
                assert not any(
                    marker in result.stderr
                    for marker in (
                        b'AddressSanitizer',
                        b'LeakSanitizer',
                        b'runtime error:',
                    )
                ), result.stderr
                if result.returncode:
                    assert read(disk, '/file') == b'original', (source, fault, point)
                    if source == raw:
                        assert disk.read_bytes() == raw.read_bytes()
                check(disk)
# Stateful writer holds ownership and survives successive operations.
disk = work / 'session.raw'
shutil.copyfile(raw, disk)
run(os.environ['FS_PROBE'], 'session', disk)
assert read(disk, '/file') == b'XYiginal'
disk = work / 'poison.raw'
shutil.copyfile(raw, disk)
run(
    os.environ['FS_PROBE'],
    'poison',
    disk,
    env=dict(os.environ, NEXTUFS_FAIL_PWRITE_FROM='1'),
)
# Resize must restore the original bytes/length on a returned error.
for fault in ['PWRITE', 'FSYNC', 'FTRUNCATE']:
    for point in [1, 3]:
        disk = work / 'resize.raw'
        shutil.copyfile(raw, disk)
        result = subprocess.run(
            [os.environ['QCOW2_FAULT_BIN'], 'resize', 'grow', str(disk), '16M'],
            capture_output=True,
            env=dict(os.environ, **{'QCOW_FAIL_' + fault: str(point)}),
            timeout=20,
        )
        assert result.returncode in (0, 1)
        assert not any(
            marker in result.stderr
            for marker in (b'AddressSanitizer', b'LeakSanitizer', b'runtime error:')
        ), result.stderr
        if result.returncode:
            assert disk.read_bytes() == raw.read_bytes(), (fault, point)
        check(disk)
