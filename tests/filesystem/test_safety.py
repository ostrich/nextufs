"""Regressions from the utility review: arguments, rename, permissions and writes."""

from pathlib import Path
import json
import os
import sys
from support import check, image, read, tool

work = Path(sys.argv[1])
disk = image(work / 'image.raw', '8M', raw=True)
tool('mkfile', disk, '/file', 'preserve me')
for option, values in [
    ('--truncate', ['nonsense']),
    ('--truncate', ['-1']),
    ('--truncate', ['18446744073709551616']),
    ('--pwrite', ['1junk', 'bad']),
    ('--chmod', ['888']),
    ('--mknod', ['010644', '4294967296']),
    ('--utimes', ['0', '-1']),
]:
    before = disk.read_bytes()
    tool('mkfile', option, disk, '/file', *values, status=2)
    assert disk.read_bytes() == before
for value in ['4294967296', '-1', '65536', '12junk']:
    before = disk.read_bytes()
    tool('mkfile', '--policy', 'user', '--uid', value, disk, '/bad', 'bad', status=2)
    assert disk.read_bytes() == before
for path in ['/a', '/b', '/locked', '/locked/sub']:
    tool('mkfile', '--mkdir', disk, path)
tool('mkfile', '--rename', disk, '/a', '/b')
check(disk)
tool('mkfile', '--chmod', disk, '/locked', '0700')
before = disk.read_bytes()
tool(
    'mkfile',
    '--policy',
    'user',
    '--uid',
    '123',
    '--gid',
    '123',
    disk,
    '/locked/sub/no',
    'no',
    status=1,
)
assert disk.read_bytes() == before
tool('mkfile', '--symlink', disk, '/locked/sub', '/shortcut')
tool(
    'mkfile',
    '--policy',
    'user',
    '--uid',
    '123',
    '--gid',
    '123',
    disk,
    '/shortcut/no',
    'no',
    status=1,
)
# Raw bytes round-trip separately from the human-readable display name.
name = os.fsdecode(b'/bad-\xff\xc0\xaf')
tool('mkfile', disk, name, 'byte name')
entries = json.loads(tool('browse', '--json', disk))
assert any(bytes.fromhex(e['name_bytes_hex']) == os.fsencode(name)[1:] for e in entries)
# A small overwrite must not need a second copy of the file.
payload = work / 'payload'
payload.write_bytes(b'A' * (5 * 1024 * 1024))
tool('mkfile', '--from-file', disk, '/large', payload)
tool('mkfile', '--pwrite', disk, '/large', '12345', 'Z')
assert read(disk, '/large') == b'A' * 12345 + b'Z' + b'A' * (5 * 1024 * 1024 - 12346)
tool('mkfile', '--truncate', disk, '/large', '9000')
tool('mkfile', '--truncate', disk, '/large', '20000')
assert read(disk, '/large') == b'A' * 9000 + b'\0' * 11000
# Returned ENOSPC must restore all original bytes, including allocation metadata.
before = disk.read_bytes()
payload.write_bytes(b'B' * (12 * 1024 * 1024))
tool('mkfile', '--from-file', disk, '/too-large', payload, status=1)
assert disk.read_bytes() == before
check(disk)
