import os
from pathlib import Path
import stat
import sys
from support import check, image, mounted, read

work = Path(sys.argv[1])
disk = image(work / 'image.img')
with mounted(disk, work) as mount:
    source = mount / 'file'
    source.write_bytes(b'hello')
    with source.open('ab') as stream:
        stream.write(b'+world')
    renamed = mount / 'renamed'
    source.rename(renamed)
    os.link(renamed, mount / 'hard')
    (mount / 'soft').symlink_to('renamed')
    (mount / 'directory').mkdir()
    with renamed.open('r+b') as stream:
        stream.truncate(14000)
        stream.seek(12288)
        stream.write(b'tail')
    expected = b'hello+world' + b'\0' * (12288 - 11) + b'tail' + b'\0' * (14000 - 12292)
    assert renamed.read_bytes() == expected
    assert os.readlink(mount / 'soft') == 'renamed'
    os.mkfifo(mount / 'fifo')
    assert stat.S_ISFIFO((mount / 'fifo').stat().st_mode)
    assert (mount / 'directory').is_dir()
    (mount / 'fifo').unlink()
    (mount / 'directory').rmdir()
for path in ['/renamed', '/hard', '/soft']:
    assert read(disk, path) == expected
check(disk)
