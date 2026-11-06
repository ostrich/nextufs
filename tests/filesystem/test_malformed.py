"""Bounded malformed-geometry cases must reject safely and preserve image bytes."""

from pathlib import Path
import shutil
import struct
import subprocess
import sys
from support import TOOL, image

work = Path(sys.argv[1])
base = image(work / 'base.raw', '8M', raw=True)
for field, value in [
    (0xA0, 1),
    (0xA0, 0xFFFFFFFF),
    (0x38, 16),
    (0x60, 64),
    (0xB8, 0),
    (0xB8, 0xFFFFFFFF),
    (0xBC, 0),
    (0x30, 3),
    (0x34, 1),
    (0x78, 1),
    (0x2C, 0xFFFFFFFF),
    (0x9C, 0xFFFFFFFF),
]:
    disk = work / f'bad-{field:x}-{value:x}.raw'
    shutil.copyfile(base, disk)
    with disk.open('r+b') as stream:
        stream.seek(0x2000 + field)
        stream.write(struct.pack('>I', value))
    before = disk.read_bytes()
    for args in [('browse', disk), ('mkfile', disk, '/new', 'x'), ('fsck', '-n', disk)]:
        result = subprocess.run(
            [TOOL, *map(str, args)], capture_output=True, timeout=10
        )
        assert result.returncode > 0, (args, result.returncode, result.stderr)
        assert (
            b'AddressSanitizer' not in result.stderr
            and b'LeakSanitizer' not in result.stderr
            and b'runtime error:' not in result.stderr
        )
        assert disk.read_bytes() == before
