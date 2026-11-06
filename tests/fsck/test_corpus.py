"""Detection, independent preservation checks, and repair convergence."""
from pathlib import Path
import sys
from fixtures import CASES, seed, damage, repair

work = Path(sys.argv[1])
source = work / 'seed.raw'
before = seed(source)
for case in (sys.argv[2:] or CASES):
    disk = work / (case + '.raw')
    damage(source, disk, case)
    repair(disk, before, case)
    print('PASS', case, flush=True)
