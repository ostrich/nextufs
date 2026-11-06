"""Parent discovery must be deterministic and reject missing/ambiguous identities."""

from pathlib import Path
import shutil
import sys
from support import image, read, tool, vdi

work = Path(sys.argv[1])
raw = image(work / 'seed.raw', '8M', raw=True)
tool('mkfile', raw, '/file', 'original')
for scenario in ('missing', 'uuid-mismatch', 'duplicate', 'cross-directory-duplicate'):
    folder = work / scenario
    folder.mkdir()
    snapshots = folder / 'Snapshots'
    snapshots.mkdir()
    base = vdi(raw, folder / 'base.vdi')
    head = vdi(raw, snapshots / 'head.vdi', base)
    assert read(head, '/file') == b'original'
    if scenario == 'missing':
        base.unlink()
    elif scenario == 'uuid-mismatch':
        with base.open('r+b') as stream:
            stream.seek(0x188)
            stream.write(b'Z' * 16)
    else:
        destination = folder if scenario == 'duplicate' else snapshots
        shutil.copyfile(base, destination / 'duplicate.vdi')
    before = head.read_bytes()
    tool('info', head, status=1, timeout=10)
    tool('mkfile', '--pwrite', head, '/file', '0', 'bad', status=1, timeout=10)
    assert head.read_bytes() == before
