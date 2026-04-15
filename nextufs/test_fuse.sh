#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <source>" >&2
  exit 2
fi

image=$1
mountpoint=/tmp/nextufs-mnt
pid=

cleanup() {
  if [[ -n "${pid}" ]]; then
    kill "${pid}" >/dev/null 2>&1 || true
    wait "${pid}" >/dev/null 2>&1 || true
  fi
  fusermount3 -u "${mountpoint}" >/dev/null 2>&1 || true
}

trap cleanup EXIT

fusermount3 -u "${mountpoint}" >/dev/null 2>&1 || true
mkdir -p "${mountpoint}"

./nextufs_fuse "${image}" "${mountpoint}" -f -s >/tmp/nextufs_fuse_test.log 2>&1 &
pid=$!
sleep 1

test -d "${mountpoint}/usr"
test "$(readlink "${mountpoint}/etc")" = "private/etc"
head -n 1 "${mountpoint}/etc/passwd" | grep -Fx '#'
ls "${mountpoint}" | grep -Fx 'mach_kernel'

echo "test_fuse.sh: ok"
