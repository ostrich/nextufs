import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tests/fsck')]
from fixtures import seed
seed(Path(sys.argv[1]))
