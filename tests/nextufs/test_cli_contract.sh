#!/bin/sh
set -eu

NEXTUFS=${NEXTUFS:-./nextufs}
SCRATCH=${1:-.scratch}
WORK="$SCRATCH/cli-contract"

mkdir -p "$WORK"
rm -f "$WORK"/*

raw="$WORK/raw.img"
labeled="$WORK/labeled.img"
overwrite_err="$WORK/overwrite.err"
limit_err="$WORK/limit.err"
global_err="$WORK/global.err"
missing_err="$WORK/missing.err"
raw_text="$WORK/raw.info"
raw_json="$WORK/raw.json"
labeled_text="$WORK/labeled.info"
labeled_json="$WORK/labeled.json"
browse_out="$WORK/browse.out"

"$NEXTUFS" --help >"$WORK/help.out"
grep -F 'usage: nextufs [global-options] <command> [args...]' "$WORK/help.out" >/dev/null
grep -F "Run 'nextufs <command> --help'" "$WORK/help.out" >/dev/null

"$NEXTUFS" --version >"$WORK/version.out"
grep -E '^nextufs [0-9]+\.[0-9]+\.[0-9]+-' "$WORK/version.out" >/dev/null

"$NEXTUFS" info --help >"$WORK/info-help.out"
grep -F 'usage: nextufs info [--json] <source>' "$WORK/info-help.out" >/dev/null
"$NEXTUFS" browse --help >"$WORK/browse-help.out"
grep -F 'usage: nextufs browse <source> [path]' "$WORK/browse-help.out" >/dev/null
"$NEXTUFS" fsck --help >"$WORK/fsck-help.out"
grep -F 'usage: nextufs fsck [-n|-y] <source> [...]' "$WORK/fsck-help.out" >/dev/null
"$NEXTUFS" mkimg --help >"$WORK/mkimg-help.out"
grep -F 'usage: nextufs mkimg [options] <target> <size>' "$WORK/mkimg-help.out" >/dev/null
"$NEXTUFS" resize --help >"$WORK/resize-help.out"
grep -F 'usage: nextufs resize grow [--force-size] <source> <size-1k-sectors>' "$WORK/resize-help.out" >/dev/null
"$NEXTUFS" resize grow --help >"$WORK/resize-grow-help.out"
grep -F 'usage: nextufs resize grow [--force-size] <source> <size-1k-sectors>' "$WORK/resize-grow-help.out" >/dev/null

if "$NEXTUFS" --json info "$raw" >"$WORK/bad-global.out" 2>"$global_err"; then
	echo "nextufs --json info unexpectedly succeeded" >&2
	exit 1
fi
grep -F "unknown global option '--json'" "$global_err" >/dev/null

"$NEXTUFS" mkimg --raw "$raw" 64M >"$WORK/raw.mkimg"
"$NEXTUFS" mkimg --label contract "$labeled" 64M >"$WORK/labeled.mkimg"

"$NEXTUFS" info "$raw" >"$raw_text"
"$NEXTUFS" info --json "$raw" >"$raw_json"
"$NEXTUFS" info "$labeled" >"$labeled_text"
"$NEXTUFS" info --json "$labeled" >"$labeled_json"

python3 - "$raw_text" "$raw_json" "$labeled_text" "$labeled_json" <<'PY'
import json
import re
import sys

raw_text, raw_json_path, labeled_text, labeled_json_path = sys.argv[1:]

def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)

def text_line(path, label):
    pattern = re.compile(rf"^\s*{re.escape(label)}:?\s+(.+)$")
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = pattern.match(line)
            if m:
                return m.group(1)
    raise SystemExit(f"missing text label {label!r} in {path}")

def text_value(path, label):
    numeric = re.match(r"([0-9]+)", text_line(path, label))
    if numeric:
        return int(numeric.group(1))
    raise SystemExit(f"non-numeric text label {label!r} in {path}")

def text_offset(path, label):
    parenthesized = re.search(r"\(([0-9]+)\)", text_line(path, label))
    if parenthesized:
        return int(parenthesized.group(1))
    return text_value(path, label)

raw = load(raw_json_path)
labeled = load(labeled_json_path)

assert raw["source_kind"] == "raw filesystem image"
assert raw["used_disk_label"] is False
assert raw["filesystem_bytes"] == text_value(raw_text, "filesystem size")
assert raw["compatibility_ceiling_bytes"] == text_value(raw_text, "compatibility ceiling")
assert raw["cylinder_summary_capacity_groups"] == text_value(raw_text, "cylinder-summary capacity")
assert raw["filesystem"]["block_size"] == text_value(raw_text, "block size")
assert raw["filesystem"]["fragment_size"] == text_value(raw_text, "fragment size")

assert labeled["source_kind"] == "labeled disk image"
assert labeled["used_disk_label"] is True
assert labeled["slice_base"] == 163840
assert labeled["slice_base"] == text_offset(labeled_text, "slice base")
assert labeled["slice_bytes"] == text_value(labeled_text, "slice size")
assert labeled["filesystem_bytes"] == text_value(labeled_text, "filesystem size")
assert labeled["label"]["root_partition"] == "a"
PY

before="$(cksum "$raw")"
"$NEXTUFS" info "$raw" >/dev/null
"$NEXTUFS" info --json "$raw" >/dev/null
"$NEXTUFS" browse "$raw" / >/dev/null
"$NEXTUFS" fsck -n "$raw" >/dev/null
after="$(cksum "$raw")"
if [ "$before" != "$after" ]; then
	echo "read-only command contract modified raw image" >&2
	exit 1
fi

"$NEXTUFS" browse "$labeled" / >"$browse_out"
grep -F "directory listing for inode 2:" "$browse_out" >/dev/null

if "$NEXTUFS" info "$WORK/missing.img" >"$WORK/missing.out" 2>"$missing_err"; then
	echo "nextufs info unexpectedly opened missing image" >&2
	exit 1
fi
grep -F 'nextufs info: open source' "$missing_err" >/dev/null

if "$NEXTUFS" mkimg --raw "$raw" 64M >"$WORK/overwrite.out" 2>"$overwrite_err"; then
	echo "nextufs mkimg unexpectedly overwrote existing target" >&2
	exit 1
fi
grep -F 'nextufs mkimg: cannot create' "$overwrite_err" >/dev/null

if "$NEXTUFS" mkimg --raw "$WORK/too-large.img" 4194177 >"$WORK/limit.out" 2>"$limit_err"; then
	echo "nextufs mkimg unexpectedly accepted oversize image" >&2
	exit 1
fi
grep -F 'use --force-size to override' "$limit_err" >/dev/null

"$NEXTUFS" mkimg --dry-run --force-size --raw "$WORK/too-large.img" 4194177 >"$WORK/force-size.out"
test ! -e "$WORK/too-large.img"

"$NEXTUFS" mkimg --force-overwrite --raw "$raw" 64M >"$WORK/overwrite-ok.out"
"$NEXTUFS" fsck -n "$raw" >/dev/null

echo "test_cli_contract.sh: ok"
