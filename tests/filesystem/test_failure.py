"""Failed creates and growth preserve existing data and leave a clean UFS."""
from pathlib import Path
import sys
from support import check, image, read, tool

work = Path(sys.argv[1])
disk = image(work / 'full.raw', '4M', raw=True)
source = work / 'too-big'
source.write_bytes(b'\0' * 6000000)
tool('mkfile', disk, '/seed', 'ok')
tool('mkfile', '--from-file', disk, '/too-big', source, status=1)
tool('browse', disk, '/too-big', status=1)
assert read(disk, '/seed') == b'ok'
tool('mkfile', '--truncate', disk, '/seed', str(1 << 31), status=1)
assert read(disk, '/seed') == b'ok'
check(disk)
