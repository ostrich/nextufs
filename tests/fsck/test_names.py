"""Exercise fsck name validation on real directory entries, without repairs."""
import os
from pathlib import Path
import struct
import subprocess
import sys

tool = os.fsencode(os.environ.get('NEXTUFS', './nextufs'))
work = Path(sys.argv[1])
work.mkdir(parents=True, exist_ok=True)
image = work / 'valid.raw'


def run(*args, status=0):
    result = subprocess.run([tool, *map(os.fsencode, args)], capture_output=True)
    assert result.returncode == status, (args, result.returncode, result.stdout,
                                         result.stderr)
    return result.stdout


run('mkimg', '--raw', '--force-overwrite', image, '8M')
# Include every high-bit byte: filenames are byte strings, not necessarily UTF-8.
name = b'name-' + bytes(range(128, 256))
run('mkfile', image, b'/' + name, 'accent')
run('mkfile', image, '/after', 'later entry')
before = image.read_bytes()
run('fsck', '-n', image)
assert image.read_bytes() == before
assert run('browse', '--raw', image, b'/' + name) == b'accent'
assert run('browse', '--raw', image, '/after') == b'later entry'

# Replace an interior name byte, preserving length, terminator and record layout.
offset = before.index(name)
assert before.count(name) == 1
assert struct.unpack_from('>H', before, offset - 2)[0] == len(name)
for label, byte in [('slash', ord('/')), ('nul', 0)]:
    malformed = bytearray(before)
    malformed[offset + 6] = byte
    target = work / (label + '.raw')
    target.write_bytes(malformed)
    output = run('fsck', '-n', target, status=8)
    assert b'DIRECTORY CORRUPTED' in output, output
    assert target.read_bytes() == malformed

print('fsck filename regressions passed')
