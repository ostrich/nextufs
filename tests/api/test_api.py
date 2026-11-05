import os
from pathlib import Path
import sys
from support import api_fixture, check, run

work = Path(sys.argv[1])
fixture = api_fixture(work / 'api.img')
run(os.environ['NEXTUFS_TEST'], fixture)
check(fixture)
