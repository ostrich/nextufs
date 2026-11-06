"""One-shot faults during new VDI block allocation and multi-level chain writes."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
from support import check, image, read, run, tool, vdi

work = Path(sys.argv[1])
raw = image(work / 'seed.raw', '8M', raw=True)
tool('mkfile', raw, '/file', 'original')
base = vdi(raw, work / 'base.vdi')
child = vdi(raw, work / 'child.vdi', base)
seed = vdi(raw, work / 'seed.vdi', child)
frozen = {p: p.read_bytes() for p in (base, child)}
for fault, points in [
    ('PWRITE', range(1, 25)),
    ('PREAD', range(1, 13)),
    ('FSYNC', range(1, 4)),
]:
    failures = 0
    for point in [*points, 10000]:
        disk = work / 'head.vdi'
        shutil.copyfile(seed, disk)
        result = subprocess.run(
            [
                os.environ['QCOW2_FAULT_BIN'],
                'mkfile',
                '--pwrite',
                str(disk),
                '/file',
                '0',
                'changed',
            ],
            capture_output=True,
            timeout=20,
            env=dict(os.environ, **{'QCOW_FAIL_' + fault: str(point)}),
        )
        assert result.returncode in (0, 1), (fault, point, result.stderr)
        assert not any(
            s in result.stderr
            for s in (b'AddressSanitizer', b'LeakSanitizer', b'runtime error:')
        ), result.stderr
        failures += result.returncode
        assert read(disk, '/file') == (
            b'original' if result.returncode else b'changedl'
        ), (fault, point, result.stderr)
        if result.returncode:
            assert (
                run(
                    os.environ['FS_PROBE'],
                    'bytes-read',
                    disk,
                    '0',
                    str(raw.stat().st_size),
                )
                == raw.read_bytes()
            ), (fault, point)
        assert all(p.read_bytes() == data for p, data in frozen.items())
        check(disk)
    assert failures, fault

# A persistent host write failure prevents safe undo and poisons the session.
for point in (1, 2, 3):
    disk = work / 'poison.vdi'
    shutil.copyfile(seed, disk)
    run(
        os.environ['FS_PROBE'],
        'poison',
        disk,
        env=dict(os.environ, NEXTUFS_FAIL_PWRITE_FROM=str(point)),
    )
    assert all(p.read_bytes() == data for p, data in frozen.items())
