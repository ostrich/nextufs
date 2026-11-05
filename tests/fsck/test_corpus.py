"""Regenerate each corrupt fixture and require detection, repair, then cleanliness."""
import os
from pathlib import Path
import shutil
import sys
from support import ROOT, check, image, run, tool

work = Path(sys.argv[1])
seed = image(work / 'seed.raw', '32M', raw=True)
tool('mkfile', seed, '/alpha', 'alpha')
tool('mkfile', '--mkdir', seed, '/dir')
tool('mkfile', seed, '/dir/file', 'nested')
tool('mkfile', seed, '/big', 'x')
tool('mkfile', '--truncate', seed, '/big', '120000')
check(seed)
for case in sorted((ROOT / 'tests/fsck/cases').glob('*.txt')):
    disk = work / (case.stem + '.raw')
    shutil.copyfile(seed, disk)
    run(os.environ['CORRUPT'], case.stem, disk)
    before = disk.read_bytes()
    tool('fsck', '-n', disk, status=8)
    assert disk.read_bytes() == before, case.stem
    tool('fsck', '-y', disk)
    check(disk)
    print('PASS', case.stem, flush=True)
