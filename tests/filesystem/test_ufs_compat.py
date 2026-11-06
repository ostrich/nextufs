"""Regressions for the NEXTSTEP kernel compatibility review."""
from pathlib import Path
import os
import re
import shutil
import struct
import sys
from support import check, image, node, read, run, tool

work = Path(sys.argv[1])
base = image(work / 'base.raw', '16M', raw=True)

def patch(disk, offset, fmt, value):
    with disk.open('r+b') as stream:
        stream.seek(offset)
        stream.write(struct.pack(fmt, value))

def inode_offset(disk, path):
    return int(re.findall(rb'inode \d+ at 0x([0-9a-f]+)', tool('browse', disk, path))[-1], 16)

def copy(name):
    disk = work / name
    shutil.copyfile(base, disk)
    return disk

# Validate the selected backup, independently of the primary.
disk = copy('backup.raw')
patch(disk, 8192 + 0xA0, '>I', 1)
tool('fsck', '-n', '-b16', disk)
tool('fsck', '-y', '-b16', disk)
check(disk)
disk = copy('bad-backup.raw')
patch(disk, 16384 + 0xA0, '>I', 16384)
before = disk.read_bytes()
tool('fsck', '-y', '-b16', disk, status=8)
assert disk.read_bytes() == before

# A name must fit its record; high-bit bytes remain legal.
for namelen, reclen in [(255, 12), (1, 9), (1, 1028)]:
    disk = copy(f'dir-{namelen}-{reclen}.raw')
    root = inode_offset(disk, '/')
    frag = struct.unpack_from('>I', disk.read_bytes(), root + 0x28)[0]
    patch(disk, frag * 1024 + 4, '>H', reclen)
    patch(disk, frag * 1024 + 6, '>H', namelen)
    before = disk.read_bytes()
    tool('mkfile', disk, '/new', 'x', status=1)
    assert disk.read_bytes() == before

# Invalid geometry must be rejected before truncating an existing output.
for geometry in [('32','4','4096','512'), ('1','1','8192','1024','3')]:
    target = work / 'preserved'
    target.write_bytes(b'preserve this existing output')
    result = __import__('subprocess').run([os.environ['NEXTUFS'], 'mkimg', '--raw',
        '--force-overwrite', str(target), '8M', *geometry], capture_output=True)
    assert result.returncode != 0
    assert target.read_bytes() == b'preserve this existing output'

# Ownership and set-ID behavior use the original UFS rules.
disk = copy('ownership.raw')
tool('mkfile', '--chmod', disk, '/', '0777')
tool('mkfile', '--chown', disk, '/', '0', '55')
user = ['--policy', 'user', '--uid', '123', '--gid', '124']
tool('mkfile', *user, disk, '/child', 'x')
assert node(disk, '/child')['gid'] == 55
tool('mkfile', '--chmod', disk, '/child', '06777')
tool('mkfile', *user, '--pwrite', disk, '/child', '0', 'y')
off = inode_offset(disk, '/child')
assert struct.unpack_from('>H', disk.read_bytes(), off)[0] & 0o6000 == 0
tool('mkfile', *user, '--mknod', disk, '/device', '020666', '1', status=1)
patch(disk, off + 0x6c, '>I', 1234)
tool('mkfile', '--unlink', disk, '/child')
assert struct.unpack_from('>I', disk.read_bytes(), off + 0x6c)[0] == 1235
tool('mkfile', disk, '/reused', 'x')
assert inode_offset(disk, '/reused') == off
assert struct.unpack_from('>I', disk.read_bytes(), off + 0x6c)[0] == 1235
patch(disk, off + 2, '>H', 32767)
before = disk.read_bytes()
tool('mkfile', '--link', disk, '/reused', '/extra', status=1)
assert disk.read_bytes() == before
patch(disk, off + 2, '>H', 1)
root_off = inode_offset(disk, '/')
patch(disk, root_off + 2, '>H', 32767)
before = disk.read_bytes()
tool('mkfile', '--mkdir', disk, '/overflow', status=1)
assert disk.read_bytes() == before
patch(disk, root_off + 2, '>H', 3)
for path in ('/a', '/b', '/a/child'):
    tool('mkfile', '--mkdir', disk, path)
b_off = inode_offset(disk, '/b')
patch(disk, b_off + 2, '>H', 32767)
before = disk.read_bytes()
tool('mkfile', '--rename', disk, '/a/child', '/b/child', status=1)
assert disk.read_bytes() == before
patch(disk, b_off + 2, '>H', 2)
check(disk)

# A sparse endpoint beyond the image consumes only a few physical blocks.
disk = copy('sparse.raw')
tool('mkfile', disk, '/file', 'abc')
tool('mkfile', '--truncate', disk, '/file', str(32 * 1024 * 1024))
run(os.environ['FS_PROBE'], 'read', disk, '/file', '1000000', '64')
tool('mkfile', '--pwrite', disk, '/file', str(24 * 1024 * 1024), 'tail')
check(disk)
tool('mkfile', '--truncate', disk, '/file', str((1 << 31) - 1))
check(disk)
before = disk.read_bytes()
tool('mkfile', '--truncate', disk, '/file', str(1 << 31), status=1)
assert disk.read_bytes() == before

# One session crosses both the direct-directory and parent-group inode limits.
disk = copy('capacity.raw')
run(os.environ['FS_PROBE'], 'capacity', disk)
ipg = struct.unpack_from('>I', disk.read_bytes(), 8192 + 0xb8)[0]
last_name = '/' + str(ipg + 7).zfill(10) + 'x' * 245
tool('mkfile', '--rename', disk, last_name, '/moved')
tool('mkfile', '--unlink', disk, '/moved')
check(disk)

# Reserved space is available to privileged writers only.
disk = copy('reserved.raw')
for sb in (8192, 16384):
    patch(disk, sb + 0x3c, '>I', 99)
tool('mkfile', '--chmod', disk, '/', '0777')
payload = work / 'reserved-payload'
payload.write_bytes(b'R' * (512 * 1024))
before = disk.read_bytes()
tool('mkfile', *user, '--from-file', disk, '/user', payload, status=1)
assert disk.read_bytes() == before
tool('mkfile', '--from-file', disk, '/root', payload)
check(disk)

# A terminated writer cannot leave the filesystem marked clean.
disk = copy('crashed.raw')
run(os.environ['FS_PROBE'], 'crash', disk)
assert disk.read_bytes()[8192 + 0xd1] == 2
output = tool('fsck', '-p', disk)
assert b'skipping check' not in output
assert disk.read_bytes()[8192 + 0xd1] == 1
# A successful ordinary write must not bless preexisting corruption.
patch(disk, 8192 + 0xd1, '>B', 3)
tool('mkfile', disk, '/new', 'x')
assert disk.read_bytes()[8192 + 0xd1] == 3
