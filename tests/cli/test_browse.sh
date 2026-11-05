#!/bin/sh
set -eu
NEXTUFS=${NEXTUFS:-./nextufs}
export NEXTUFS
WORK="${1:-.scratch}/browse"
mkdir -p "$WORK"
export WORK
"$NEXTUFS" mkimg --raw --force-overwrite "$WORK/image.raw" 64M >/dev/null
python3 - <<'PYTEST'
import json, os, pathlib, resource, subprocess
work = pathlib.Path(os.environ['WORK'])
tool = os.environ['NEXTUFS']
image = str(work / 'image.raw')
def run(*args, **kw):
    return subprocess.run([tool, *args], check=True, **kw)
payload = bytes(range(256)) * (224 * 1024 + 1)
(work / 'payload').write_bytes(payload)
name = '/quote"back\\slash'
run('mkfile', '--from-file', image, name, str(work / 'payload'))
run('mkfile', image, '/empty', '')
run('mkfile', '--symlink', image, name, '/link')
# Constrain address space to show extraction is independent of file size.
def limit_memory():
    resource.setrlimit(resource.RLIMIT_AS, (48 * 1024 * 1024,) * 2)
assert run('browse', '--raw', image, name, capture_output=True,
           preexec_fn=limit_memory).stdout == payload
assert run('browse', '--raw', image, '/empty', capture_output=True).stdout == b''
assert run('browse', '--raw', image, '/link', capture_output=True).stdout == payload
records = json.loads(run('browse', '--json', image, '/', capture_output=True).stdout)
assert records[0]['name'] == '.'
entry = next(r for r in records if r['name'] == name[1:])
assert entry['size'] == len(payload)
assert all(k in entry for k in ('inode', 'mode', 'uid', 'gid', 'atime', 'mtime'))
link = json.loads(run('browse', '--json', image, '/link', capture_output=True).stdout)
assert link[0]['mode'] & 0o170000 == 0o120000
for args in [('browse', image, '/missing'), ('browse', '--raw', image),
             ('browse', '--raw', image, '/'), ('browse', '--json', image, '/missing')]:
    assert subprocess.run([tool, *args], capture_output=True).returncode != 0
if pathlib.Path('/dev/full').exists():
    for args in [('browse', '--raw', image, name), ('browse', '--json', image, '/')]:
        with open('/dev/full', 'wb') as out:
            assert subprocess.run([tool, *args], stdout=out).returncode != 0
print('browse regressions passed')
PYTEST
