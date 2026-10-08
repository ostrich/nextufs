#!/usr/bin/env python3
import os
from pathlib import Path
import struct
import subprocess
import sys

PROBE = str(Path(os.environ.get('QCOW2_PROBE', 'build/qcow2_probe')).resolve())
WORK = Path(sys.argv[1] if len(sys.argv) > 1 else '.scratch/qcow2-cow').resolve()
WORK.mkdir(parents=True, exist_ok=True)
MASK = 0x00fffffffffffe00
COPIED = 1 << 63


def run(*args):
    p = subprocess.run([str(a) for a in args], capture_output=True)
    if p.returncode:
        raise RuntimeError(f'{args}: {p.stderr.decode(errors="replace")}')
    return p.stdout


def ref_offset(data, host):
    cs = 1 << struct.unpack_from('>I', data, 20)[0]
    table = struct.unpack_from('>Q', data, 48)[0]
    block = struct.unpack_from('>Q', data, table + (host // cs // (cs // 2)) * 8)[0]
    return block + (host // cs % (cs // 2)) * 2


# Construct valid shared data and shared L2 mappings, verified independently by QEMU.
for shared_l2 in [False, True]:
    image = WORK / f'shared-{shared_l2}.qcow2'
    run('qemu-img', 'create', '-f', 'qcow2', '-o', 'cluster_size=4096', image, '8M')
    run('qemu-io', '-f', 'qcow2', '-c', 'write -P 0x5a 0 4096', image)
    b = bytearray(image.read_bytes())
    l1 = struct.unpack_from('>Q', b, 40)[0]
    l2 = struct.unpack_from('>Q', b, l1)[0] & MASK
    data = struct.unpack_from('>Q', b, l2)[0] & MASK
    if shared_l2:
        struct.pack_into('>Q', b, l1, l2)
        struct.pack_into('>Q', b, l1 + 8, l2)
        struct.pack_into('>H', b, ref_offset(b, l2), 2)
        alias = 2 * 1024 * 1024
    else:
        struct.pack_into('>Q', b, l2 + 8, data)
        alias = 4096
    struct.pack_into('>Q', b, l2, data)
    struct.pack_into('>H', b, ref_offset(b, data), 2)
    image.write_bytes(b)
    run('qemu-img', 'check', image)
    run(PROBE, 'write', image, 17, 777)
    expected = b'\x5a' * 17 + bytes((i * 17 + 31) % 251 for i in range(777)) + b'\x5a' * (4096 - 794)
    assert run(PROBE, 'read', image, 0, 4096) == expected
    assert run(PROBE, 'read', image, alias, 4096) == b'\x5a' * 4096
    run('qemu-img', 'check', image)

# An allocated or unallocated zero cluster must override backing contents.
raw = WORK / 'short.raw'
raw.write_bytes(b'\x5a' * 8192)
for allocated in [False, True]:
    image = WORK / f'zero-{allocated}.qcow2'
    run('qemu-img', 'create', '-f', 'qcow2', '-F', 'raw', '-b', raw.name,
        '-o', 'cluster_size=4096', image, '64K')
    if allocated:
        run('qemu-io', '-f', 'qcow2', '-c', 'write -P 0x66 0 4096', image)
    run('qemu-io', '-f', 'qcow2', '-c', 'write -z 0 4096', image)
    assert run(PROBE, 'read', image, 0, 12288) == b'\0' * 4096 + b'\x5a' * 4096 + b'\0' * 4096
    run(PROBE, 'write', image, 31, 700)
    expected = b'\0' * 31 + bytes((i * 17 + 31) % 251 for i in range(700)) + b'\0' * (4096 - 731)
    assert run(PROBE, 'read', image, 0, 4096) == expected
    assert raw.read_bytes() == b'\x5a' * 8192
    run('qemu-img', 'check', image)
# Normal preallocated images remain writable and interoperable.
for mode in ['metadata', 'full']:
    image = WORK / f'preallocated-{mode}.qcow2'
    run('qemu-img', 'create', '-f', 'qcow2', '-o', f'cluster_size=4096,preallocation={mode}', image, '8M')
    run(PROBE, 'write', image, 4091, 8200)
    assert run(PROBE, 'read', image, 4091, 8200) == bytes((i * 17 + 31) % 251 for i in range(8200))
    run('qemu-img', 'check', image)
# Explicit raw formats override magic probing; absent format extensions probe content.
raw_magic = WORK / 'magic.raw'
raw_magic.write_bytes(b'QFI\xfb' + b'\0' * (8192 - 4))
image = WORK / 'explicit-raw.qcow2'
run('qemu-img', 'create', '-f', 'qcow2', '-F', 'raw', '-b', raw_magic.name, image)
assert run(PROBE, 'read', image, 0, 8192) == raw_magic.read_bytes()
image = WORK / 'implicit-format.qcow2'
run('qemu-img', 'create', '-f', 'qcow2', '-F', 'raw', '-b', raw.name, image)
b = bytearray(image.read_bytes())
pos = struct.unpack_from('>I', b, 100)[0]
while struct.unpack_from('>I', b, pos)[0]:
    kind, length = struct.unpack_from('>II', b, pos)
    if kind == 0xe2792aca:
        struct.pack_into('>I', b, pos, 0x1234abcd)
        break
    pos += 8 + ((length + 7) & ~7)
image.write_bytes(b)
assert run(PROBE, 'read', image, 0, 8192) == raw.read_bytes()
print('QCOW2 COW and zero-cluster regressions passed')
