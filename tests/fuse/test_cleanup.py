"""An assertion failure must unmount and reap the process before propagating."""
import os
from pathlib import Path
import sys
from support import image, mounted

work = Path(sys.argv[1])
disk = image(work / 'cleanup.img')
try:
    with mounted(disk, work):
        raise AssertionError('deliberate assertion failure')
except AssertionError as error:
    assert str(error) == 'deliberate assertion failure'
else:
    raise AssertionError('mount helper swallowed the assertion')
assert not os.path.ismount(work / 'mount')
