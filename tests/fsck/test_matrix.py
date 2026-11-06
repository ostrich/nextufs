"""Selected repairs across geometry, orphan trees, and preen policy."""
from pathlib import Path
import shutil
import struct
import sys
from fixtures import seed, damage, repair, preserved, RELOCATE
from model import UFS
from support import tool, check

work = Path(sys.argv[1])
for layout in ('labeled', 'large-fragments', 'cd'):
    source = work / (layout + '.raw')
    before = seed(source, layout)
    for case in ('cg-bitmap-bad', 'unreferenced-dir', 'partial-indirect'):
        disk = work / (layout + '-' + case + '.raw')
        damage(source, disk, case)
        repair(disk, before, case)
source = work / 'raw.raw'
before = seed(source)
for case, paths in [('orphan-tree', ('/canary',)),
                    ('multiple-orphans', ('/alpha', '/dir', '/canary'))]:
    disk = work / (case + '.raw')
    shutil.copyfile(source, disk)
    fs = UFS(disk)
    data = bytearray(fs.raw)
    for path in paths:
        _, offset = fs.entries(2)[path[1:].encode()]
        struct.pack_into('>I', data, offset, 0)
    disk.write_bytes(data)
    RELOCATE[case] = paths
    repair(disk, before, case)
# A clean preen skip is intentional and separate from damaged dirty fixtures.
clean = source.read_bytes()
assert b'skipping check' in tool('fsck', '-p', source)
assert source.read_bytes() == clean
for case, status in [('bad-block-count', 0), ('dup-block', 8), ('unreferenced-dir', 0)]:
    disk = work / ('preen-' + case + '.raw')
    damage(source, disk, case)
    data = bytearray(disk.read_bytes())
    data[UFS(disk).sb + 0xd1] = 2
    disk.write_bytes(data)
    output = tool('fsck', '-p', disk, status=status)
    assert b'skipping check' not in output
    if status == 0:
        check(disk)
        preserved(before, UFS(disk).snapshot(), case)
    else:
        tool('fsck', '-y', disk)
        check(disk)
        preserved(before, UFS(disk).snapshot(), case)
# Move a live file into a staggered nonzero group. Normalize accounting before
# damaging it, so the bitmap test cannot accidentally keep exercising CG0.
cross = work / 'cross-group.raw'
shutil.copyfile(source, cross)
fs = UFS(cross)
assert fs.field(0x2c) > 1
assert fs.field(0x24) % fs.fpg, 'fixture must include a partial final group'
data = bytearray(fs.raw)
old = before['/alpha']['inode']
new = fs.ipg + 1
old_offset, new_offset = fs.inode_offset(old), fs.inode_offset(new)
assert fs.inode(new)['mode'] == 0
cg = fs.cg_offset(1)
index = next(i for i in range(fs.fpg) if data[cg + 984 + i // 8] & (1 << (i % 8)))
frag = fs.fpg + index
old_frag = fs.data_frag(old, 0)
data[frag * fs.frag:(frag + 1) * fs.frag] = data[old_frag * fs.frag:(old_frag + 1) * fs.frag]
data[new_offset:new_offset + 128] = data[old_offset:old_offset + 128]
struct.pack_into('>I', data, new_offset + 40, frag)
data[old_offset:old_offset + 128] = bytes(128)
struct.pack_into('>I', data, fs.entries(2)[b'alpha'][1], new)
cross.write_bytes(data)
tool('fsck', '-y', cross)
check(cross)
cross_before = UFS(cross).snapshot()
assert cross_before['/alpha']['sha256'] == before['/alpha']['sha256']
disk = work / 'cross-group-bitmap.raw'
damage(cross, disk, 'cg-bitmap-bad')
repair(disk, cross_before, 'cg-bitmap-bad')
disk = work / 'cross-group-inode.raw'
shutil.copyfile(cross, disk)
data = bytearray(disk.read_bytes())
data[cg + 724] &= ~(1 << 1)
disk.write_bytes(data)
repair(disk, cross_before, 'inode-bitmap')
# Fill every existing lost+found slot with valid hard links. Reconnection must
# expand the directory and preserve all existing names and their shared inode.
full = work / 'full-lostfound.raw'
shutil.copyfile(source, full)
fs = UFS(full)
lf = before['/lost+found']['inode']
size = before['/lost+found']['size']
content = bytearray(fs.data(fs.inode(lf)))
struct.pack_into('>H', content, 16, 12)  # shrink .. to its minimum record
serial = 0
for block in range(0, size, 1024):
    offset = block + (24 if block == 0 else 0)
    while offset < block + 1024:
        length = 16 if block + 1024 - offset >= 32 else block + 1024 - offset
        name = ('f%04d' % serial).encode()
        record = struct.pack('>IHH', before['/tail']['inode'], length, len(name)) + name + b'\0'
        content[offset:offset + length] = record.ljust(length, b'\0')
        offset += length
        serial += 1
data = bytearray(fs.raw)
for logical in range((size + fs.block - 1) // fs.block):
    offset = fs.data_frag(lf, logical) * fs.frag
    chunk = content[logical * fs.block:(logical + 1) * fs.block]
    data[offset:offset + len(chunk)] = chunk
full.write_bytes(data)
tool('fsck', '-y', full)
check(full)
full_before = UFS(full).snapshot()
disk = work / 'full-lostfound-orphan.raw'
damage(full, disk, 'unreferenced-alpha')
repair(disk, full_before, 'unreferenced-alpha')
assert UFS(disk).inode(lf)['size'] > size
