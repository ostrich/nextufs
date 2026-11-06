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
with mounted(disk, work, 'rw,mode=user,uid=123,gid=124') as mount:
    try:
        (mount / 'locked/sub/no').write_bytes(b'no')
    except PermissionError:
        pass
    else:
        raise AssertionError('ancestor search permission bypassed')
    if groups:
        assert (mount / 'file').read_bytes() == b'group access'
check(disk)
