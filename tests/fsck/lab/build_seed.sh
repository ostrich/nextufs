#!/usr/bin/env bash
set -euo pipefail

. "$(dirname "$0")/common.sh"
TEST_ROOT="$ROOT/tests/fsck"
CACHE_DIR="$FSCK_LAB_DIR/cache"
SEED_DIR="$CACHE_DIR/seeds"
SEED="$SEED_DIR/base.raw"
NEXTUFS="${NEXTUFS:-$ROOT/nextufs}"

mkdir -p "$SEED_DIR"

# Regenerate instead of trusting an existence-only cache.
rm -f "$SEED"
NEXTUFS="$NEXTUFS" python3 -B "$TEST_ROOT/lab/seed.py" "$SEED" >/dev/null
echo "$SEED"
