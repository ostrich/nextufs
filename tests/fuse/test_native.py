"""Native mounted mutations, concurrent clients, ownership and backend parity."""

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import random
import sys
from support import check, image, mounted, read, run, tool, vdi

work = Path(sys.argv[1])
kind = sys.argv[2]
raw = image(work / 'seed.raw', '16M', raw=True)
tool('mkfile', raw, '/file', 'original')
if kind == 'raw':
    disk = raw
elif kind == 'vdi':
    disk = vdi(raw, work / 'image.vdi')
else:
    disk = work / 'image.qcow2'
    run('qemu-img', 'convert', '-f', 'raw', '-O', 'qcow2', raw, disk)
with mounted(disk, work) as mount:
    # A second cooperating writer must fail while this session owns the image.
    tool('mkfile', disk, '/outside', 'no', status=1)
    tool('resize', 'grow', disk, '32M', status=1)

    def writer(i):
        for j in range(8):
            (mount / f't{i}').write_bytes(bytes([65 + i]) * 4096)
        assert (mount / f't{i}').read_bytes() == bytes([65 + i]) * 4096

    with ThreadPoolExecutor(max_workers=8) as pool:
        list(pool.map(writer, range(8)))
    a = mount / 'a'
    b = mount / 'b'
    a.mkdir()
    b.mkdir()
    a.rename(b)
    assert b.is_dir() and not a.exists()
    f = mount / 'file'
    os.link(f, mount / 'alias')
    rng = random.Random(59)
    expected = bytearray(b'original')
    with f.open('r+b', buffering=0) as stream:
        for _ in range(40):
            if rng.randrange(3) == 0:
                size = rng.randrange(64000)
                stream.truncate(size)
                expected = expected[:size] + b'\0' * max(0, size - len(expected))
            else:
                offset = rng.randrange(64000)
                payload = rng.randbytes(rng.randrange(1, 3000))
                stream.seek(offset)
                stream.write(payload)
                if offset + len(payload) > len(expected):
                    expected.extend(b'\0' * (offset + len(payload) - len(expected)))
                expected[offset : offset + len(payload)] = payload
            assert f.read_bytes() == expected
    assert (mount / 'alias').read_bytes() == expected
    (mount / 'sym').symlink_to('file')
    assert (mount / 'sym').read_bytes() == expected
    os.chmod(f, 0o600)
    os.utime(f, (1234567890, 1234567890))
    assert f.stat().st_mtime == 1234567890
    (mount / 'alias').unlink()
    (mount / 'sym').unlink()
    b.rmdir()
assert read(disk, '/file') == expected
check(disk)
if kind == 'qcow2':
    run('qemu-img', 'check', disk)
