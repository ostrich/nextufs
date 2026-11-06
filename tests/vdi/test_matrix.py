"""Byte-oracle coverage for VDI allocation, inheritance and boundary writes."""

import os
from pathlib import Path
import struct
import sys
from support import image, run, vdi

work = Path(sys.argv[1])
block = 1024 * 1024
raw = image(work / 'seed.raw', '8M', raw=True)
# Leave UFS metadata intact; use the latter half for backend byte operations.
with raw.open('r+b') as stream:
    stream.seek(4 * block)
    stream.write(bytes(range(256)) * (4 * block // 256))
base = vdi(raw, work / 'base.vdi')
child = vdi(raw, work / 'child.vdi', base)
head = vdi(raw, work / 'head.vdi', child)
expected = bytearray(raw.read_bytes())
probe = os.environ['FS_PROBE']


def read(disk, offset, size):
    return run(probe, 'bytes-read', disk, str(offset), str(size))


def write(disk, offset, size, oracle):
    run(probe, 'bytes-write', disk, str(offset), str(size))
    oracle[offset : offset + size] = bytes((i * 17 + 31) % 251 for i in range(size))
    assert read(disk, 0, len(oracle)) == oracle


write(child, 5 * block - 31, 93, expected)
frozen = {p: p.read_bytes() for p in (base, child)}
# Explicit zero masks inherited nonzero data.
with head.open('r+b') as stream:
    stream.seek(512 + 6 * 4)
    stream.write(struct.pack('<I', 0xFFFFFFFE))
expected[6 * block : 7 * block] = b'\0' * block
assert read(head, 0, len(expected)) == expected
for offset, size in [
    (4 * block - 7, 29),
    (6 * block + 111, 19),
    (7 * block - 13, 37),
    (8 * block - 9, 9),
]:
    write(head, offset, size, expected)
    assert all(p.read_bytes() == data for p, data in frozen.items())
run(probe, 'bytes-read', head, str(8 * block - 1), '2', status=1)
run(probe, 'bytes-write', head, str(8 * block), '1', status=1)
# Partial last block and a shorter parent: absent inherited bytes are zero.
partial = work / 'partial.raw'
partial.write_bytes(raw.read_bytes()[: 7 * block + 512])
parent = vdi(partial, work / 'partial.vdi')
large = vdi(raw, work / 'large.vdi', parent)
expected = bytearray(partial.read_bytes() + b'\0' * (block - 512))
assert read(large, 7 * block, block) == expected[7 * block :]
write(large, 7 * block + 500, 40, expected)
assert parent.read_bytes()[0x170:0x178] == struct.pack('<Q', len(partial.read_bytes()))
# Dynamic unallocated blocks are zero, with no parent fallback.
empty = vdi(raw, work / 'empty.vdi')
with empty.open('r+b') as stream:
    stream.seek(512 + 7 * 4)
    stream.write(struct.pack('<I', 0xFFFFFFFF))
    stream.seek(0x184)
    stream.write(struct.pack('<I', 7))
    stream.truncate(4096 + 7 * block)
expected = bytearray(raw.read_bytes())
expected[7 * block :] = b'\0' * block
write(empty, 7 * block + 3, 17, expected)
# Per-block prefixes are skipped for reads and initialized during allocation.
prefixed = vdi(raw, work / 'extra.vdi')
data = prefixed.read_bytes()
header = bytearray(data[:4096])
struct.pack_into('<I', header, 0x17C, 512)
with prefixed.open('wb') as stream:
    stream.write(header)
    for i in range(8):
        stream.write(b'X' * 512)
        stream.write(data[4096 + i * block : 4096 + (i + 1) * block])
assert read(prefixed, 0, len(raw.read_bytes())) == raw.read_bytes()
write(prefixed, 5 * block - 5, 20, bytearray(raw.read_bytes()))
# A partial final guest block is readable/writable up to the exact virtual size.
tailraw = work / 'tail.raw'
tailraw.write_bytes(raw.read_bytes() + b'T' * 512)
taildisk = vdi(tailraw, work / 'tail.vdi')
tailoracle = bytearray(tailraw.read_bytes())
write(taildisk, 8 * block + 499, 13, tailoracle)
run(probe, 'bytes-read', taildisk, str(8 * block + 511), '2', status=1)
# Entire child blocks beyond a shorter parent must materialize from zeroes.
beyond = vdi(tailraw, work / 'beyond.vdi', base)
write(beyond, 8 * block + 11, 23, bytearray(raw.read_bytes() + b'\0' * 512))
# Allocation with an extra block prefix must not shift guest data.
extrahead = vdi(raw, work / 'extra-head.vdi', base)
with extrahead.open('r+b') as stream:
    stream.seek(0x17C)
    stream.write(struct.pack('<I', 512))
write(extrahead, 6 * block - 9, 19, bytearray(raw.read_bytes()))

# Repeat inherited boundary writes with distinct container block sizes.
for block_size in (4096, 65536, 2 * block):
    parent = vdi(raw, work / f'block-{block_size}.vdi', block=block_size)
    child = vdi(raw, work / f'block-{block_size}-head.vdi', parent, block=block_size)
    before = parent.read_bytes()
    write(child, 6 * block - 17, 53, bytearray(raw.read_bytes()))
    assert parent.read_bytes() == before
