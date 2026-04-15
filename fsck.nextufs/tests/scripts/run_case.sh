#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <legacy|modern> <-n|-y|-p> <case>" >&2
  exit 2
fi

ENGINE="$1"
MODE="$2"
CASE_NAME="$3"

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_ROOT="$ROOT/fsck.nextufs/tests"
CACHE_DIR="$TEST_ROOT/cache"
WORK_DIR="$TEST_ROOT/work"
EXPECT_DIR="$CACHE_DIR/expected"
CORRUPT_DIR="$CACHE_DIR/corrupt"

mkdir -p "$WORK_DIR" "$EXPECT_DIR"
"$TEST_ROOT/scripts/build_corpus.sh" >/dev/null

FIXTURE="$CORRUPT_DIR/$CASE_NAME.raw"
WORK="$WORK_DIR/$CASE_NAME-${ENGINE}-${MODE#-}.raw"
OUT="$EXPECT_DIR/$CASE_NAME-${ENGINE}-${MODE#-}.out"

cp "$FIXTURE" "$WORK"

case "$ENGINE" in
  legacy)
    BIN="$ROOT/fsck.legacy/fsck.nextufs"
    ;;
  modern)
    BIN="$ROOT/fsck.nextufs/fsck.nextufs"
    ;;
  *)
    echo "unknown engine: $ENGINE" >&2
    exit 2
    ;;
esac

"$BIN" "$MODE" "$WORK" >"$OUT" 2>&1 || true
cat "$OUT"
