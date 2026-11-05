from pathlib import Path
import os
import sys
from support import api_fixture, check, mounted

work = Path(sys.argv[1])
disk = api_fixture(work / 'api.img')
with mounted(disk, work) as mount:
    assert (mount / 'usr').is_dir()
    assert os.readlink(mount / 'etc') == 'private/etc'
    assert (mount / 'etc/passwd').read_bytes().startswith(b'#\n# You probably')
    assert (mount / 'mach_kernel').read_bytes() == bytes(range(256)) * 1024
check(disk)
