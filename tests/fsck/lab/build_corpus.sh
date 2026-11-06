#!/usr/bin/env bash
set -euo pipefail

. "$(dirname "$0")/common.sh"
TEST_ROOT="$ROOT/tests/fsck"
CACHE_DIR="$FSCK_LAB_DIR/cache"
CORRUPT_DIR="$CACHE_DIR/corrupt"
SEED="$("$TEST_ROOT/lab/build_seed.sh")"
TOOL="${CORRUPT:-$ROOT/build/tests/fuse-1/corrupt}"


mkdir -p "$CORRUPT_DIR"

test -x "$TOOL" || { echo "build test tools first: make test-tools" >&2; exit 2; }

for case_file in "$TEST_ROOT/cases/"*.txt; do
  case_name="$(basename "$case_file" .txt)"
  case_path="$CORRUPT_DIR/$case_name.raw"
  cp "$SEED" "$case_path"
  "$TOOL" "$case_name" "$case_path"
  echo "$case_name $case_path"
done
