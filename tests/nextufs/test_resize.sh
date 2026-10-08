#!/bin/sh
set -eu
NEXTUFS=${NEXTUFS:-./nextufs}
WORK="${1:-.scratch}/resize"
mkdir -p "$WORK"
"$NEXTUFS" mkimg --raw --force-overwrite "$WORK/2k-fragments.raw" 64M 32 4 8192 2048 >/dev/null
"$NEXTUFS" resize grow "$WORK/2k-fragments.raw" 73728
"$NEXTUFS" fsck -n "$WORK/2k-fragments.raw"
"$NEXTUFS" info "$WORK/2k-fragments.raw" > "$WORK/2k-fragments.info"
grep -F 'filesystem size                75497472 bytes' "$WORK/2k-fragments.info" >/dev/null
