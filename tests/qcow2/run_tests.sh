#!/bin/sh
set -eu
WORK=${1:-.scratch/qcow2-tests}
mkdir -p "$WORK"
python3 -B tests/qcow2/test_reads.py "$WORK/reads"
python3 -B tests/qcow2/test_cow.py "$WORK/cow"
python3 -B tests/qcow2/test_writes.py "$WORK/writes"
python3 -B tests/qcow2/test_failures.py "$WORK/failures"
if test "${WITH_FUSE:-1}" = 1; then
    "$NEXTUFS" mkimg --raw --force-overwrite "$WORK/fuse.raw" 64M >/dev/null
    qemu-img convert -f raw -O qcow2 "$WORK/fuse.raw" "$WORK/fuse.qcow2"
    ./nextufs_stress --backend fuse --seed 0x13579bdf --ops 120 --quiet \
        "$WORK/fuse.qcow2" > "$WORK/fuse-stress.log"
    grep -F 'nextufs_stress: ok' "$WORK/fuse-stress.log"
    qemu-img check "$WORK/fuse.qcow2" > "$WORK/fuse-check.log"
    "$NEXTUFS" fsck -n "$WORK/fuse.qcow2" > "$WORK/fuse-fsck.log"
fi
echo 'QCOW2 interoperability suite passed'
