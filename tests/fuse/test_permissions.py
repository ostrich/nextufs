"""FUSE credentials include supplementary groups and enforce directory traversal."""

from pathlib import Path
import os
import sys
from support import check, image, mounted, tool

work = Path(sys.argv[1])
disk = image(work / 'permissions.raw', '8M', raw=True)
for directory in ['/locked', '/locked/sub']:
    tool('mkfile', '--mkdir', disk, directory)
tool('mkfile', '--chmod', disk, '/locked', '0700')
tool('mkfile', disk, '/file', 'group access')
groups = [g for g in os.getgroups() if g <= 32767]
if groups:
    tool('mkfile', '--chown', disk, '/file', '0', str(groups[0]))
    tool('mkfile', '--chmod', disk, '/file', '0640')
tool('mkfile', disk, '/opened', 'retained rights')
tool('mkfile', '--chown', disk, '/opened', '123', '124')
with mounted(disk, work, 'rw,mode=user,uid=123,gid=124') as mount:
    try:
        (mount / 'locked/sub/no').write_bytes(b'no')
    except PermissionError:
        pass
    else:
        raise AssertionError('ancestor search permission bypassed')
    assert disk.read_bytes()[8192 + 0xd1] == 2
    fd = os.open(mount / 'opened', os.O_RDWR)
    try:
        os.chmod(mount / 'opened', 0)
        assert os.pread(fd, 15, 0) == b'retained rights'
        assert os.pwrite(fd, b'X', 0) == 1
        os.ftruncate(fd, 8192)
        assert os.fstat(fd).st_blocks == 16
    finally:
        os.close(fd)
    if groups:
        assert (mount / 'file').read_bytes() == b'group access'
assert disk.read_bytes()[8192 + 0xd1] == 1
check(disk)
