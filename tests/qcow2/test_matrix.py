#!/usr/bin/env python3
"""Version/cluster matrix and deterministic byte-level differential testing."""
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys

PROBE = str(Path(os.environ.get('QCOW2_PROBE', 'build/tests/fuse-1/qcow2-probe')).resolve())
WORK = Path(sys.argv[1]).resolve()
WORK.mkdir(parents=True, exist_ok=True)


def run(*args, status=0):
    p = subprocess.run([str(a) for a in args], capture_output=True, timeout=120)
    assert p.returncode == status, (args, p.returncode, p.stdout, p.stderr)
    return p.stdout


def pattern(length):
    return bytes((i * 17 + 31) % 251 for i in range(length))


def verify(image, expected):
    assert run(PROBE, 'read', image, 0, len(expected)) == expected
    run('qemu-img', 'check', image)
    converted = WORK / 'oracle.raw'
    run('qemu-img', 'convert', '-f', 'qcow2', '-O', 'raw', image, converted)
    assert converted.read_bytes() == expected
    converted.unlink()


for compat in ['0.10', '1.1']:
    for bits in range(9, 22):
        cs = 1 << bits
        l2_span = cs * (cs // 8)
        size = max(5 * cs + 512, l2_span + 512 if bits <= 12 else 0)
        image = WORK / f'matrix-{compat}-{bits}.qcow2'
        run('qemu-img', 'create', '-f', 'qcow2', '-o',
            f'compat={compat},cluster_size={cs}', image, size)
        expected = bytearray(size)
        # Empty reads/writes at EOF must succeed without changing the image.
        original = image.read_bytes()
        run(PROBE, 'read', image, size, 0)
        run(PROBE, 'write', image, size, 0)
        assert image.read_bytes() == original
        run(PROBE, 'readonly-write', image, 0, 1, status=1)
        assert image.read_bytes() == original
        for mode in ['read', 'write']:
            for offset, length in [(-1, 1), (size, 1), (size + 1, 0),
                                   (size - 1, 2)]:
                run(PROBE, mode, image, offset, length, status=1)
                assert image.read_bytes() == original
        writes = [(0, 1), (cs - 3, 11), (2 * cs - 1, cs + 2),
                  (size - 17, 17), (cs + 1, 31)]
        if bits <= 12:
            writes.append((l2_span - 7, 23))
        rng = random.Random(0x514649 + bits)
        writes += [(rng.randrange(size - 257), rng.randrange(1, 258))
                   for _ in range(8)]
        for offset, length in writes:
            run(PROBE, 'write', image, offset, length)
            expected[offset:offset + length] = pattern(length)
            assert run(PROBE, 'read', image, offset, length) == pattern(length)
        # The close path also flushes a successful write without explicit fsync.
        run(PROBE, 'write-close', image, 7, 29)
        expected[7:36] = pattern(29)
        verify(image, expected)

# Large-cluster L2 boundaries require huge virtual addresses, not huge data.
# QEMU extracts only a 1 KiB window from these sparse images.
for compat in ['0.10', '1.1']:
    for bits in range(13, 22):
        cs = 1 << bits
        boundary = cs * (cs // 8)
        image = WORK / f'l2-boundary-{compat}-{bits}.qcow2'
        run('qemu-img', 'create', '-f', 'qcow2', '-o',
            f'compat={compat},cluster_size={cs}', image, boundary + cs)
        run(PROBE, 'write', image, boundary - 13, 31)
        expected = b'\0' * 499 + pattern(31) + b'\0' * 494
        assert run(PROBE, 'read', image, boundary - 512, 1024) == expected
        run('qemu-img', 'check', image)
        window = WORK / 'window.raw'
        source = {'driver': 'raw', 'offset': boundary - 512, 'size': 1024,
                  'file': {'driver': 'qcow2',
                           'file': {'driver': 'file', 'filename': str(image)}}}
        run('qemu-img', 'convert', '-O', 'raw',
            'json:' + json.dumps(source), window)
        assert window.read_bytes() == expected
        window.unlink()

# A raw parent ending inside a cluster: preserve inherited bytes and zero-fill
# beyond its EOF. Different cluster sizes and absolute/relative parents coexist.
raw = WORK / 'short.raw'
raw.write_bytes(pattern(5003))
base = WORK / 'base.qcow2'
head = WORK / 'head.qcow2'
run('qemu-img', 'create', '-f', 'qcow2', '-F', 'raw', '-b', raw.name,
    '-o', 'compat=0.10,cluster_size=512', base, 32768)
run('qemu-img', 'create', '-f', 'qcow2', '-F', 'qcow2', '-b', base,
    '-o', 'cluster_size=4096', head, 65536)
parents = {p: hashlib.sha256(p.read_bytes()).digest() for p in [raw, base]}
expected = bytearray(raw.read_bytes() + b'\0' * (65536 - 5003))
verify(head, expected)
for offset, length in [(4999, 13), (4093, 9), (32765, 17), (65535, 1)]:
    run(PROBE, 'write', head, offset, length)
    expected[offset:offset + length] = pattern(length)
    verify(head, expected)
for parent, digest in parents.items():
    assert hashlib.sha256(parent.read_bytes()).digest() == digest

print('QCOW2 version/cluster/boundary matrix passed (26 combinations)')
