#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <-n|-y|-p>" >&2
  exit 2
fi

MODE="$1"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
CASES_DIR="$ROOT/fsck.nextufs/tests/cases"

for case_file in "$CASES_DIR"/*.txt; do
  case_name="$(basename "$case_file" .txt)"
  bash "$ROOT/fsck.nextufs/tests/scripts/compare_case.sh" "$MODE" "$case_name" >/dev/null
done
