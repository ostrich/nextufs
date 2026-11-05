#!/usr/bin/env bash
set -euo pipefail

. "$(dirname "$0")/common.sh"
TEST_ROOT="$ROOT/tests/fsck"
CACHE_DIR="$FSCK_LAB_DIR/cache"
SEED_DIR="$CACHE_DIR/seeds"
SEED="$SEED_DIR/base.raw"
NEXTUFS="${NEXTUFS:-$ROOT/nextufs}"

mkdir -p "$SEED_DIR"

if [[ "${1:-}" == "--force" ]]; then
  rm -f "$SEED"
fi

if [[ -f "$SEED" ]]; then
  echo "$SEED"
  exit 0
fi

(
  cd "$ROOT"
  "$NEXTUFS" mkimg --raw "$SEED" 32768 63 16 8192 1024 16 10 60 2048 t >/dev/null
  "$NEXTUFS" mkfile "$SEED" /alpha "alpha"
  "$NEXTUFS" mkfile --mkdir "$SEED" /dir
  "$NEXTUFS" mkfile "$SEED" /dir/file "nested"
  "$NEXTUFS" mkfile "$SEED" /big "x"
  "$NEXTUFS" mkfile --truncate "$SEED" /big 120000
)

echo "$SEED"
