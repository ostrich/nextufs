#!/bin/bash
set -euo pipefail
NEXTUFS=${NEXTUFS:-./nextufs}
WORK=$1
mkdir -p "$WORK"
rm -f "$WORK"/mkimg-raw.img "$WORK"/mkimg-labeled.img \
	"$WORK"/mkimg-raw-full.img \
	"$WORK"/mkimg-raw-a.img "$WORK"/mkimg-raw-b.img \
	"$WORK"/mkimg-labeled-a.img "$WORK"/mkimg-labeled-b.img \
	"$WORK"/mkimg-overwrite.img "$WORK"/mkimg-dry-run.img \
	"$WORK"/mkimg-limit.raw "$WORK"/mkimg-too-large.raw \
	"$WORK"/mkimg-invalid.raw \
	"$WORK"/mkimg-labeled.inspect "$WORK"/mkimg-overwrite.err \
	"$WORK"/mkimg-raw-label.err "$WORK"/mkimg-invalid.err \
	"$WORK"/mkimg-too-large.err
"$NEXTUFS" mkimg --raw "$WORK"/mkimg-raw.img 64M >/dev/null
"$NEXTUFS" fsck -n "$WORK"/mkimg-raw.img >/dev/null
"$NEXTUFS" mkimg "$WORK"/mkimg-labeled.img 64M >/dev/null
"$NEXTUFS" fsck -n "$WORK"/mkimg-labeled.img >/dev/null
"$NEXTUFS" info "$WORK"/mkimg-labeled.img >"$WORK"/mkimg-labeled.inspect
grep -F 'Disk Label' "$WORK"/mkimg-labeled.inspect >/dev/null
grep -F 'slice base                     0x28000 (163840)' "$WORK"/mkimg-labeled.inspect >/dev/null
grep -F 'superblock base                0x2a000 (172032)' "$WORK"/mkimg-labeled.inspect >/dev/null
"$NEXTUFS" mkimg --raw "$WORK"/mkimg-raw-full.img 65536 63 16 8192 1024 16 10 60 2048 t >/dev/null
"$NEXTUFS" fsck -n "$WORK"/mkimg-raw-full.img >/dev/null
"$NEXTUFS" mkimg --raw "$WORK"/mkimg-raw-a.img 64M >/dev/null
"$NEXTUFS" mkimg --raw "$WORK"/mkimg-raw-b.img 64M >/dev/null
cmp "$WORK"/mkimg-raw-a.img "$WORK"/mkimg-raw-b.img
"$NEXTUFS" mkimg --label repro "$WORK"/mkimg-labeled-a.img 64M >/dev/null
"$NEXTUFS" mkimg --label repro "$WORK"/mkimg-labeled-b.img 64M >/dev/null
cmp "$WORK"/mkimg-labeled-a.img "$WORK"/mkimg-labeled-b.img
"$NEXTUFS" mkimg --raw "$WORK"/mkimg-overwrite.img 64M >/dev/null
! "$NEXTUFS" mkimg --raw "$WORK"/mkimg-overwrite.img 64M >/dev/null 2>"$WORK"/mkimg-overwrite.err
grep -F 'cannot create' "$WORK"/mkimg-overwrite.err >/dev/null
"$NEXTUFS" mkimg --force-overwrite --raw "$WORK"/mkimg-overwrite.img 64M >/dev/null
"$NEXTUFS" mkimg --dry-run "$WORK"/mkimg-dry-run.img 64M >/dev/null
test ! -e "$WORK"/mkimg-dry-run.img
! "$NEXTUFS" mkimg --raw --label bad "$WORK"/mkimg-invalid.raw 64M >/dev/null 2>"$WORK"/mkimg-raw-label.err
grep -F -- '--label is not valid with --raw' "$WORK"/mkimg-raw-label.err >/dev/null
! "$NEXTUFS" mkimg --raw "$WORK"/mkimg-invalid.raw 64M 0 16 8192 1024 16 10 60 2048 t >/dev/null 2>"$WORK"/mkimg-invalid.err
grep -F 'preposterous nsect' "$WORK"/mkimg-invalid.err >/dev/null
"$NEXTUFS" mkimg --raw "$WORK"/mkimg-limit.raw 4194176 >/dev/null
"$NEXTUFS" fsck -n "$WORK"/mkimg-limit.raw >/dev/null
! "$NEXTUFS" mkimg --raw "$WORK"/mkimg-too-large.raw 4194177 >/dev/null 2>"$WORK"/mkimg-too-large.err
grep -F 'compatibility limit' "$WORK"/mkimg-too-large.err >/dev/null
"$NEXTUFS" info --json "$WORK"/mkimg-labeled.img >/dev/null
