#!/bin/sh
set -eu
NEXTUFS=${NEXTUFS:-./nextufs}
status=0
output=$("$NEXTUFS" mount ignored-image ignored-mountpoint 2>&1) || status=$?
test "$status" -eq 2
printf '%s\n' "$output" | grep -F 'mount support is unavailable in this offline build' >/dev/null
echo 'offline mount contract passed'
