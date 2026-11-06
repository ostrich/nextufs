"""Generated VDI chains, bounds, cycles and head-only writes without QEMU."""

from pathlib import Path
import struct
import sys
from support import check, image, read, tool, vdi

work = Path(sys.argv[1])
raw = image(work / 'raw.img', '8M', raw=True)
tool('mkfile', raw, '/file', 'original')
base = vdi(raw, work / 'base.vdi')
head = vdi(raw, work / 'head.vdi', base)
before = base.read_bytes()
assert read(head, '/file') == b'original'
tool('mkfile', '--pwrite', head, '/file', '0', 'X')
assert read(head, '/file') == b'Xriginal'
assert base.read_bytes() == before
check(head)
for name, off, value in [
    ('huge-map', 0x180, 0xFFFFFFFF),
    ('alias', 516, 0),
    ('outside', 512, 0x1000),
    ('overlap', 0x158, 512),
]:
    disk = work / (name + '.vdi')
    disk.write_bytes(before)
    with disk.open('r+b') as stream:
        stream.seek(off)
        stream.write(struct.pack('<I', value))
    original = disk.read_bytes()
    tool('info', disk, status=1, timeout=10)
    assert disk.read_bytes() == original
# Put cycles/depth chains in a separate directory: UUID lookup must be unambiguous.
cycle = work / 'cycle'
cycle.mkdir()
a = vdi(raw, cycle / 'a.vdi')
b = vdi(raw, cycle / 'b.vdi', a)
with a.open('r+b') as stream:
    stream.seek(0x4C)
    stream.write(struct.pack('<I', 4))
    stream.seek(0x1A8)
    stream.write(b.read_bytes()[0x188:0x198])
tool('info', a, status=1, timeout=10)
deep = work / 'deep'
deep.mkdir()
parent = vdi(raw, deep / '0.vdi')
for i in range(1, 33):
    parent = vdi(raw, deep / f'{i}.vdi', parent)
    if i == 31:
        assert read(parent, '/file') == b'original'
tool('info', parent, status=1, timeout=10)
