#!/bin/sh
set -eu

NEXTUFS=${NEXTUFS:-./nextufs}
NEXTUFS_TEST=${NEXTUFS_TEST:-./nextufs_test}
export NEXTUFS
WORK="${1:-.scratch}/fragments"
mkdir -p "$WORK"

for scenario in pack exhaust runs; do
	image="$WORK/$scenario.raw"
	"$NEXTUFS" mkimg --raw --force-overwrite "$image" 1M >/dev/null
	"$NEXTUFS_TEST" --fragments "$scenario" "$image"
done

for fragment in 1024 2048 4096; do
	image="$WORK/geometry-$fragment.raw"
	"$NEXTUFS" mkimg --raw --force-overwrite "$image" 1M 32 4 8192 "$fragment" >/dev/null
	"$NEXTUFS" resize grow "$image" "$((1024 + fragment / 1024))" >/dev/null
	"$NEXTUFS_TEST" --fragments geometry "$image"
done

echo 'fragment regressions passed'
