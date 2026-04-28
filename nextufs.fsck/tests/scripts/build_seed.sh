#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
TEST_ROOT="$ROOT/nextufs.fsck/tests"
CACHE_DIR="$TEST_ROOT/cache"
SEED_DIR="$CACHE_DIR/seeds"
SEED="$SEED_DIR/base.raw"

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
  make -C nextufs -f Makefile.linux nextufs >/dev/null
  ./nextufs/nextufs mkimg --raw "$SEED" 32768 63 16 8192 1024 16 10 60 2048 t >/dev/null
  ./nextufs/nextufs mkfile "$SEED" /alpha "alpha"
  ./nextufs/nextufs mkfile --mkdir "$SEED" /dir
  ./nextufs/nextufs mkfile "$SEED" /dir/file "nested"
  ./nextufs/nextufs mkfile "$SEED" /big "x"
  ./nextufs/nextufs mkfile --truncate "$SEED" /big 120000
)

echo "$SEED"
