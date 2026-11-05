#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_ROOT="$ROOT/tests/fsck"
CASES_DIR="$TEST_ROOT/cases"
CORRUPT_DIR="$TEST_ROOT/cache/corrupt"
WORK_DIR="$TEST_ROOT/work/repair-all"
BIN="$ROOT/nextufs"

"$TEST_ROOT/scripts/build_corpus.sh" >/dev/null
mkdir -p "$WORK_DIR"

check_clean() {
  local image="$1"
  "$BIN" fsck -n "$image"
}

status=0

for case_file in "$CASES_DIR"/*.txt; do
  case_name="$(basename "$case_file" .txt)"
  fixture="$CORRUPT_DIR/$case_name.raw"
  work="$WORK_DIR/$case_name.raw"

  cp "$fixture" "$work"
  if "$BIN" fsck -y "$work" && check_clean "$work"; then
    printf 'OK   %s\n' "$case_name"
  else
    printf 'FAIL %s\n' "$case_name" >&2
    status=1
  fi
done

exit "$status"
