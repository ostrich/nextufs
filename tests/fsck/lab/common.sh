# Sourced by the optional reference-comparison tools; tests/run.py does not use a cache.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
mkdir -p "$ROOT/.scratch/fsck-lab"
if [[ -z "${FSCK_LAB_DIR:-}" ]]; then
  FSCK_LAB_DIR="$(mktemp -d "$ROOT/.scratch/fsck-lab/run-XXXXXX")"
fi
export FSCK_LAB_DIR
