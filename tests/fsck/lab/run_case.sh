#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <reference|shipped> <-n|-y|-p> <case>" >&2
  exit 2
fi

ENGINE="$1"
MODE="$2"
CASE_NAME="$3"

. "$(dirname "$0")/common.sh"
TEST_ROOT="$ROOT/tests/fsck"
CACHE_DIR="$FSCK_LAB_DIR/cache"
WORK_DIR="$FSCK_LAB_DIR/work"
EXPECT_DIR="$CACHE_DIR/expected"
CORRUPT_DIR="$CACHE_DIR/corrupt"

mkdir -p "$WORK_DIR" "$EXPECT_DIR"
case "$ENGINE" in
  shipped) test -x "${NEXTUFS:-$ROOT/nextufs}" ;;
  reference) test -x "${FSCK_REFERENCE_BIN:-}" ;;
  *) echo "unknown engine: $ENGINE" >&2; exit 2 ;;
esac
[[ "$MODE" == -n || "$MODE" == -y || "$MODE" == -p ]] || exit 2
"$TEST_ROOT/lab/build_corpus.sh" >/dev/null

FIXTURE="$CORRUPT_DIR/$CASE_NAME.raw"
WORK="$WORK_DIR/$CASE_NAME-${ENGINE}-${MODE#-}.raw"
OUT="$EXPECT_DIR/$CASE_NAME-${ENGINE}-${MODE#-}.out"

cp "$FIXTURE" "$WORK"
if [[ "$MODE" == -p ]]; then
  # A corruption fixture must be checked, not skipped because its seed was clean.
  python3 - "$WORK" <<'PYTHON'
import sys
with open(sys.argv[1], 'r+b') as disk:
    disk.seek(8192 + 0xd1)
    disk.write(bytes([2]))
PYTHON
fi

case "$ENGINE" in
  reference)
    BIN="${FSCK_REFERENCE_BIN:-}"
    if [[ -z "$BIN" ]]; then
      echo "FSCK_REFERENCE_BIN is required for reference runs" >&2
      exit 2
    fi
    ;;
  shipped)
    BIN="${NEXTUFS:-$ROOT/nextufs}"
    ;;
  *)
    echo "unknown engine: $ENGINE" >&2
    exit 2
    ;;
esac

status=0
if [[ "$ENGINE" == "shipped" ]]; then
  "$BIN" fsck "$MODE" "$WORK" >"$OUT" 2>&1 || status=$?
else
  "$BIN" "$MODE" "$WORK" >"$OUT" 2>&1 || status=$?
fi
cat "$OUT"
exit "$status"
