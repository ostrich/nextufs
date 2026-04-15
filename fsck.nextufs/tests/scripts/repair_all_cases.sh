#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_ROOT="$ROOT/fsck.nextufs/tests"
CASES_DIR="$TEST_ROOT/cases"
CORRUPT_DIR="$TEST_ROOT/cache/corrupt"
WORK_DIR="$TEST_ROOT/work/repair-all"
BIN="$ROOT/fsck.nextufs/fsck.nextufs"

"$ROOT/fsck.nextufs/tests/scripts/build_corpus.sh" >/dev/null
mkdir -p "$WORK_DIR"

check_clean() {
  local image="$1"
  local out

  out="$("$BIN" -n "$image" 2>&1 || true)"
  case "$out" in
    *"DIRECTORY CORRUPTED"*|*"MISSING "*|*"UNREF "*|*"DUP/BAD"*|\
    *"UNALLOCATED"*|*"I OUT OF RANGE"*|*"EXTRA "*|\
    *"INCORRECT BLOCK COUNT"*|*"UNKNOWN FILE TYPE"*|\
    *"PARTIALLY ALLOCATED INODE"*|*"BLK(S) MISSING IN BIT MAPS"*|\
    *"SUMMARY INFORMATION BAD"*|*"FREE BLK COUNT(S) WRONG IN SUPERBLK"*|\
    *"IMPOSSIBLE MINFREE"*|*"UNDEFINED OPTIMIZATION"*|\
    *"ROOT INODE UNALLOCATED"*|*"ROOT INODE NOT DIRECTORY"*|\
    *"NO lost+found"*|*"SORRY."*)
      printf '%s\n' "$out"
      return 1
      ;;
  esac
  return 0
}

status=0

for case_file in "$CASES_DIR"/*.txt; do
  case_name="$(basename "$case_file" .txt)"
  fixture="$CORRUPT_DIR/$case_name.raw"
  work="$WORK_DIR/$case_name.raw"

  cp "$fixture" "$work"
  "$BIN" -y "$work" >/dev/null 2>&1 || true
  if check_clean "$work"; then
    printf 'OK   %s\n' "$case_name"
  else
    printf 'FAIL %s\n' "$case_name" >&2
    status=1
  fi
done

exit "$status"
