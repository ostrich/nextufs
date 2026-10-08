#!/usr/bin/env python3
"""Unsupported/malformed inputs are immutable; failed writes remain detectable."""
import os
from pathlib import Path
import struct
import subprocess
import sys

PROBE = str(Path(os.environ.get('QCOW2_PROBE', 'build/qcow2_probe')).resolve())
FAULT_TOOL = str(Path(os.environ.get('QCOW2_FAULT_BIN', 'build/nextufs_qcow_fault')).resolve())
TOOL = str(Path(os.environ.get('NEXTUFS', './nextufs')).resolve())
WORK = Path(sys.argv[1] if len(sys.argv) > 1 else '.scratch/qcow2-failures').resolve()
WORK.mkdir(parents=True, exist_ok=True)


def run(*args):
    p = subprocess.run([str(a) for a in args], capture_output=True)
    if p.returncode:
        raise RuntimeError(f'{args}: {p.stderr.decode(errors="replace")}')
    return p.stdout


def rejected(image):
    before = image.read_bytes()
    for mode in ['read', 'write']:
        p = subprocess.run([PROBE, mode, str(image), '0', '32'], capture_output=True)
        assert p.returncode > 0, (mode, image)
        assert b"AddressSanitizer" not in p.stderr and b"runtime error:" not in p.stderr
        assert image.read_bytes() == before


base = WORK / 'base.qcow2'
run('qemu-img', 'create', '-f', 'qcow2', '-o', 'cluster_size=4096', base, '8M')
original = base.read_bytes()
patches = {
    'version': (4, '>I', 1), 'small-cluster': (20, '>I', 8),
    'large-cluster': (20, '>I', 22), 'encryption': (32, '>I', 1),
    'snapshots': (60, '>I', 1), 'dirty': (72, '>Q', 1),
    'corrupt': (72, '>Q', 2), 'external': (72, '>Q', 4),
    'compression-type': (72, '>Q', 8), 'extended-l2': (72, '>Q', 16),
    'unknown-feature': (72, '>Q', 1 << 63), 'lazy-refcounts': (80, '>Q', 1),
    'autoclear': (88, '>Q', 1), 'refcount-width': (96, '>I', 5),
    'short-header': (100, '>I', 100), 'huge-header': (100, '>I', 4097),
    'uncovered-size': (24, '>Q', 1 << 40), 'huge-l1': (36, '>I', 1 << 30),
    'l1-outside': (40, '>Q', 1 << 63), 'l1-overlap-header': (40, '>Q', 0),
    'l1-misaligned': (40, '>Q', 4097), 'ref-outside': (48, '>Q', 1 << 63),
    'huge-reftable': (56, '>I', 1 << 30), 'bad-backing-offset': (8, '>Q', 1),
}
for name, (offset, fmt, value) in patches.items():
    image = WORK / (name + '.qcow2')
    b = bytearray(original)
    struct.pack_into(fmt, b, offset, value)
    image.write_bytes(b)
    rejected(image)
for length in [0, 4, 71, 103, len(original) - 1]:
    image = WORK / f'truncated-{length}.qcow2'
    image.write_bytes(original[:length])
    rejected(image)

# Reject compressed clusters even when the header uses the standard feature set.
raw = WORK / 'compress.raw'
raw.write_bytes(b'A' * 65536)
compressed = WORK / 'compressed.qcow2'
run('qemu-img', 'convert', '-c', '-f', 'raw', '-O', 'qcow2', raw, compressed)
rejected(compressed)

# Parent resolution, declared format enforcement, inode-based cycles, and depth bound.
for parent in ['missing.raw', 'loop.qcow2']:
    image = WORK / ('missing.qcow2' if parent == 'missing.raw' else 'loop.qcow2')
    run('qemu-img', 'create', '-u', '-f', 'qcow2', '-F', 'qcow2', '-b', parent, image, '8M')
    rejected(image)
invalid_format = WORK / 'declared-wrong.qcow2'
run('qemu-img', 'create', '-u', '-f', 'qcow2', '-F', 'qcow2', '-b', raw.name, invalid_format, '8M')
rejected(invalid_format)
parent = base
for n in range(32):
    image = WORK / f'deep-{n}.qcow2'
    run('qemu-img', 'create', '-u', '-f', 'qcow2', '-F', 'qcow2', '-b', parent.name, image, '8M')
    parent = image
rejected(parent)

# Unknown extensions survive successful modifications, without header rewriting.
unknown = WORK / 'unknown-extension.qcow2'
b = bytearray(original)
header_length = struct.unpack_from('>I', b, 100)[0]
pos = header_length
while struct.unpack_from('>I', b, pos)[0]:
    length = struct.unpack_from('>I', b, pos + 4)[0]
    pos += 8 + ((length + 7) & ~7)
struct.pack_into('>II8sII', b, pos, 0x1234abcd, 8, b'preserve', 0, 0)
unknown.write_bytes(b)
run(PROBE, 'write', unknown, 0, 700)
assert unknown.read_bytes()[pos:pos + 24] == b[pos:pos + 24]
run('qemu-img', 'check', unknown)

# Inject failures at every successive write/flush/allocation call until success.
for variable in ['QCOW_FAIL_PWRITE', 'QCOW_FAIL_FSYNC', 'QCOW_FAIL_FTRUNCATE']:
    failures = 0
    for n in range(1, 100):
        image = WORK / f'{variable}-{n}.qcow2'
        image.write_bytes(original)
        env = dict(os.environ, **{variable: str(n)})
        p = subprocess.run([PROBE, 'write', str(image), '0', '8192'],
                           env=env, capture_output=True)
        if p.returncode == 0:
            run('qemu-img', 'check', image)
            break
        failures += 1
        after = image.read_bytes()
        if after != original:
            assert struct.unpack_from('>Q', after, 72)[0] & 1, (variable, n)
            rejected(image)
    else:
        raise AssertionError(f'no successful terminus: {variable}')
    assert failures >= (3 if variable == 'QCOW_FAIL_FTRUNCATE' else 8), (variable, failures)
for n in [1, 3, 10]:
    image = WORK / f'read-failure-{n}.qcow2'
    image.write_bytes(original)
    p = subprocess.run([PROBE, 'read', str(image), '0', '32'], capture_output=True,
                       env=dict(os.environ, QCOW_FAIL_PREAD=str(n)))
    assert p.returncode != 0
    assert image.read_bytes() == original

# Bounds failures precede any modification.
for off, length in [(-1, 32), (8 * 1024 * 1024 - 1, 32)]:
    image = WORK / 'bounds.qcow2'
    image.write_bytes(original)
    p = subprocess.run([PROBE, 'write', str(image), str(off), str(length)], capture_output=True)
    assert p.returncode != 0
    assert image.read_bytes() == original
# Corrupt active mappings and refcounts, including metadata/data overlap.
mapped = WORK / 'mapped.qcow2'
run('qemu-img', 'create', '-f', 'qcow2', '-o', 'cluster_size=4096', mapped, '8M')
run('qemu-io', '-f', 'qcow2', '-c', 'write -P 0x5a 0 4096', mapped)
mapped_bytes = mapped.read_bytes()
l1 = struct.unpack_from('>Q', mapped_bytes, 40)[0]
ref_table = struct.unpack_from('>Q', mapped_bytes, 48)[0]
l2 = struct.unpack_from('>Q', mapped_bytes, l1)[0] & 0x00fffffffffffe00
data = struct.unpack_from('>Q', mapped_bytes, l2)[0] & 0x00fffffffffffe00
ref_block = struct.unpack_from('>Q', mapped_bytes, ref_table)[0]
for name, offset, value in [
    ('l1-reserved', l1, l2 | 2), ('l1-copied', l1, l2),
    ('l2-reserved', l2, data | (1 << 63) | 2),
    ('l2-outside', l2, (1 << 63) | (1 << 55)),
    ('l2-overlap-ref', l2, (1 << 63) | ref_table),
    ('l2-overlap-self', l2, (1 << 63) | l2),
    ('l2-zero-offset', l2, 1 << 63),
    ('refblock-overlap-data', ref_table, data),
    ('extension-overrun', struct.unpack_from('>I', mapped_bytes, 100)[0],
     (0x1234abcd << 32) | 0xffffffff),
]:
    image = WORK / (name + '.qcow2')
    b = bytearray(mapped_bytes)
    struct.pack_into('>Q', b, offset, value)
    image.write_bytes(b)
    rejected(image)
image = WORK / 'bad-refcount.qcow2'
b = bytearray(mapped_bytes)
struct.pack_into('>H', b, ref_block + (data // 4096) * 2, 2)
image.write_bytes(b)
rejected(image)

# Failed guest reads are propagated after a successful metadata open.
allocated = WORK / 'allocated.qcow2'
run('qemu-img', 'create', '-f', 'qcow2', '-o', 'cluster_size=4096', allocated, '8M')
run('qemu-io', '-f', 'qcow2', '-c', 'write -P 0x5a 0 4096', allocated)
allocated_before = allocated.read_bytes()
p = subprocess.run([PROBE, 'read', str(allocated), '0', '32'], capture_output=True,
                   env=dict(os.environ, QCOW_FAIL_PREAD_AFTER_OPEN='1'))
assert p.returncode != 0
assert allocated.read_bytes() == allocated_before

# fsck must report a failed final flush instead of a successful repair.
seed = WORK / 'repair.raw'
run(TOOL, 'mkimg', '--raw', '--force-overwrite', seed, '8M')
run(TOOL, 'mkfile', seed, '/alpha', 'alpha')
run(str(Path('tests/fsck/tools/corrupt_raw_case').resolve()), 'bad-block-count', seed)
for variable in ['QCOW_FAIL_FSYNC', 'QCOW_FAIL_PWRITE']:
    image = WORK / (variable + '-repair.qcow2')
    run('qemu-img', 'convert', '-f', 'raw', '-O', 'qcow2', seed, image)
    p = subprocess.run([FAULT_TOOL, 'fsck', '-y', str(image)], capture_output=True,
                       env=dict(os.environ, **{variable: '2'}))
    assert p.returncode == 8, (variable, p.returncode, p.stdout, p.stderr)
    assert struct.unpack_from('>Q', image.read_bytes(), 72)[0] & 1

# A successful native repair leaves both the QCOW2 metadata and UFS clean.
image = WORK / 'repair-success.qcow2'
run('qemu-img', 'convert', '-f', 'raw', '-O', 'qcow2', seed, image)
run(TOOL, 'fsck', '-y', image)
run(TOOL, 'fsck', '-n', image)
run('qemu-img', 'check', image)
print('QCOW2 rejection and I/O failure regressions passed')
