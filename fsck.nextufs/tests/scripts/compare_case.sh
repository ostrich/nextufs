#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 <-n|-y|-p> <case>" >&2
  exit 2
fi

MODE="$1"
CASE_NAME="$2"

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_ROOT="$ROOT/fsck.nextufs/tests"
TMP_DIR="$TEST_ROOT/work/compare"

mkdir -p "$TMP_DIR"

LEGACY_OUT="$TMP_DIR/$CASE_NAME-legacy-${MODE#-}.out"
MODERN_OUT="$TMP_DIR/$CASE_NAME-modern-${MODE#-}.out"
LEGACY_NORM="$TMP_DIR/$CASE_NAME-legacy-${MODE#-}.norm"
MODERN_NORM="$TMP_DIR/$CASE_NAME-modern-${MODE#-}.norm"

bash "$TEST_ROOT/scripts/run_case.sh" legacy "$MODE" "$CASE_NAME" >"$LEGACY_OUT"
bash "$TEST_ROOT/scripts/run_case.sh" modern "$MODE" "$CASE_NAME" >"$MODERN_OUT"
tail -n +2 "$LEGACY_OUT" >"$LEGACY_NORM"
tail -n +2 "$MODERN_OUT" >"$MODERN_NORM"
diff -u "$LEGACY_NORM" "$MODERN_NORM"
