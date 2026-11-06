"""QEMU-generated dynamic VDI images are an independent read/write oracle."""

import os
from pathlib import Path
import sys
from support import check, image, read, run, tool

work = Path(sys.argv[1])
raw = image(work / 'seed.raw', '16M', raw=True)
tool('mkfile', raw, '/file', 'original')
disk = work / 'qemu.vdi'
run('qemu-img', 'convert', '-f', 'raw', '-O', 'vdi', raw, disk)
assert read(disk, '/file') == b'original'
tool('mkfile', '--pwrite', disk, '/file', '2', 'CHANGE')
tool('mkfile', '--truncate', disk, '/file', '20000')
tool('mkfile', '--mkdir', disk, '/directory')
check(disk)
flat = work / 'flat.raw'
run('qemu-img', 'convert', '-f', 'vdi', '-O', 'raw', disk, flat)
assert read(flat, '/file') == b'orCHANGE' + b'\0' * 19992
assert (
    run(os.environ['FS_PROBE'], 'bytes-read', disk, '0', str(flat.stat().st_size))
    == flat.read_bytes()
)
check(flat)
