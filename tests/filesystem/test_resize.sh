#!/bin/sh
set -eu

NEXTUFS=${NEXTUFS:-./nextufs}
WORK="${1:-.scratch}/resize"
mkdir -p "$WORK"

grow_and_check()
{
	image=$1
	sectors=$2
	"$NEXTUFS" resize grow "$image" "$sectors"
	test "$(wc -c < "$image")" -eq "$((sectors * 1024))"
	"$NEXTUFS" fsck -n "$image"
}

"$NEXTUFS" resize --help >/dev/null
"$NEXTUFS" resize grow --help >/dev/null

# The primary superblock stays at byte 8192 with 2 KiB fragments too.
# fs_sblkno * 512 incorrectly addresses byte 4096 for this geometry.
"$NEXTUFS" mkimg --raw --force-overwrite "$WORK/2k-fragments.raw" 64M 32 4 8192 2048 >/dev/null
grow_and_check "$WORK/2k-fragments.raw" 73728
"$NEXTUFS" info "$WORK/2k-fragments.raw" > "$WORK/2k-fragments.info"
grep -F 'filesystem size                75497472 bytes' "$WORK/2k-fragments.info" >/dev/null

# This formatter gives these raw images 8 MiB groups and 8 x 1 KiB fragments/block.
# Exercise every tail length, including converting seven free fragments to a block.
"$NEXTUFS" mkimg --raw --force-overwrite "$WORK/partial.raw" 65M >/dev/null
printf 'resize payload\n' > "$WORK/payload"
"$NEXTUFS" mkfile --from-file "$WORK/partial.raw" /payload "$WORK/payload" >/dev/null
for size in 66561 66562 66563 66564 66565 66566 66567 66568 66569; do
	grow_and_check "$WORK/partial.raw" "$size"
done
"$NEXTUFS" info "$WORK/partial.raw" > "$WORK/partial.info"
grep -F 'filesystem size                68166656 bytes (66569 1K sectors)' "$WORK/partial.info" >/dev/null
"$NEXTUFS" browse --raw "$WORK/partial.raw" /payload > "$WORK/payload.after"
cmp "$WORK/payload" "$WORK/payload.after"

"$NEXTUFS" mkimg --raw --force-overwrite "$WORK/base.raw" 64M >/dev/null

# A newly created full final group uses cg_ncyl == 0. On subsequent growth
# it becomes an interior group and must use fs_cpg instead.
cp "$WORK/base.raw" "$WORK/exact-new.raw"
grow_and_check "$WORK/exact-new.raw" 73728
grow_and_check "$WORK/exact-new.raw" 74752

# The same convention applies when extending an existing partial group,
# even just before the boundary when fs_ncyl has already rounded upward.
cp "$WORK/base.raw" "$WORK/exact-existing.raw"
grow_and_check "$WORK/exact-existing.raw" 66561
grow_and_check "$WORK/exact-existing.raw" 73727
grow_and_check "$WORK/exact-existing.raw" 73728
grow_and_check "$WORK/exact-existing.raw" 74752

# Labeled targets include the front porch; verify the image length and UFS.
"$NEXTUFS" mkimg --force-overwrite "$WORK/labeled.img" 64M >/dev/null
grow_and_check "$WORK/labeled.img" 66561
grow_and_check "$WORK/labeled.img" 66568

# A new group must have room for its metadata. Reject before changing the image.
cp "$WORK/base.raw" "$WORK/too-small.raw"
if "$NEXTUFS" resize grow "$WORK/too-small.raw" 65537 > "$WORK/too-small.log" 2>&1; then
	echo 'resize unexpectedly accepted a group too small for its metadata' >&2
	exit 1
fi
grep -F 'target ends before metadata for cylinder group' "$WORK/too-small.log" >/dev/null
cmp "$WORK/base.raw" "$WORK/too-small.raw"

echo 'resize regressions passed'
