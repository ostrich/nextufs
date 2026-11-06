"""Supported UFS holes return zeros; oversized legacy inodes are rejected."""

from pathlib import Path
import re
import struct
import sys
from support import check, image, tool, run
import os

work = Path(sys.argv[1])
disk = image(work / 'holes.raw', '8M', raw=True)
tool('mkfile', disk, '/hole', '')
output = tool('browse', disk, '/hole')
offset = int(re.search(rb'lookup.*?inode \d+ at 0x([0-9a-f]+)', output, re.S)[1], 16)
n = 8192 // 4
# Keep the CLI extraction bounded; API probe reads windows in very large sparse files.
for blocks in [13, 12 + n + 1]:
    with disk.open('r+b') as stream:
        stream.seek(offset + 8)
        stream.write(struct.pack('>Q', blocks * 8192))
    run(os.environ['FS_PROBE'], 'read', disk, '/hole', str((blocks - 1) * 8192), '64')
    if blocks == 13:
        assert tool('browse', '--raw', disk, '/hole') == b'\0' * (blocks * 8192)
        check(disk)

with disk.open('r+b') as stream:
    stream.seek(offset + 8)
    stream.write(struct.pack('>Q', 1 << 32))
run(os.environ['FS_PROBE'], 'read', disk, '/hole', '0', '64', status=1)
tool('fsck', '-n', disk, status=8)
