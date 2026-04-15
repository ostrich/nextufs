#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_ROOT="$ROOT/fsck.nextufs/tests"
CACHE_DIR="$TEST_ROOT/cache"
CORRUPT_DIR="$CACHE_DIR/corrupt"
SEED="$("$TEST_ROOT/scripts/build_seed.sh")"
TOOL="$ROOT/fsck.nextufs/tests/tools/corrupt_raw_case"
CASES=(
  bad-block-count
  bad-dot-inode
  bad-dotdot-inode
  bad-file-type
  cg-bitmap-bad
  cg-summary-bad
  dir-entry-fclear
  dir-entry-unallocated
  dup-block
  extra-dot
  extra-dotdot
  extraneous-dir-link
  invalid-dir-inode
  lostfound-missing
  lostfound-not-dir
  missing-dot
  missing-dotdot
  misaligned-dir-size
  out-of-range-block
  partial-indirect
  partially-allocated-inode
  root-not-dir
  root-unallocated
  short-dir
  super-cstotal-bad
  super-free-counts
  super-minfree
  super-optim
  unreferenced-alpha
  unreferenced-dir
  zero-length-dir
)

mkdir -p "$CORRUPT_DIR"

(
  cd "$ROOT"
  make -C nextufs -f Makefile.linux libnextufs.a >/dev/null
  make -C fsck.nextufs -f Makefile.linux repair-tools >/dev/null
)

for case_name in "${CASES[@]}"; do
  case_path="$CORRUPT_DIR/$case_name.raw"
  if [[ ! -f "$case_path" || "${1:-}" == "--force" ]]; then
    cp "$SEED" "$case_path"
    "$TOOL" "$case_name" "$case_path"
  fi
  echo "$case_name $case_path"
done
