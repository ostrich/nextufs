#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <-n|-y|-p>" >&2
  exit 2
fi

MODE="$1"
. "$(dirname "$0")/common.sh"
CASES_DIR="$ROOT/tests/fsck/cases"

for case_file in "$CASES_DIR"/*.txt; do
  case_name="$(basename "$case_file" .txt)"
  bash "$ROOT/tests/fsck/lab/compare_case.sh" "$MODE" "$case_name" >/dev/null
done
