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
REFERENCE_BIN="${FSCK_REFERENCE_BIN:-}"

if [[ -z "$REFERENCE_BIN" ]]; then
  echo "FSCK_REFERENCE_BIN is required for compare_case.sh" >&2
  exit 2
fi

mkdir -p "$TMP_DIR"

REFERENCE_OUT="$TMP_DIR/$CASE_NAME-reference-${MODE#-}.out"
SHIPPED_OUT="$TMP_DIR/$CASE_NAME-shipped-${MODE#-}.out"
REFERENCE_NORM="$TMP_DIR/$CASE_NAME-reference-${MODE#-}.norm"
SHIPPED_NORM="$TMP_DIR/$CASE_NAME-shipped-${MODE#-}.norm"

bash "$TEST_ROOT/scripts/run_case.sh" reference "$MODE" "$CASE_NAME" >"$REFERENCE_OUT"
bash "$TEST_ROOT/scripts/run_case.sh" shipped "$MODE" "$CASE_NAME" >"$SHIPPED_OUT"
tail -n +2 "$REFERENCE_OUT" >"$REFERENCE_NORM"
tail -n +2 "$SHIPPED_OUT" >"$SHIPPED_NORM"
diff -u "$REFERENCE_NORM" "$SHIPPED_NORM"
