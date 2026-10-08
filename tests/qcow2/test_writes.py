#!/usr/bin/env python3
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys

TOOL = str(Path(os.environ.get('NEXTUFS', './nextufs')).resolve())
PROBE = str(Path(os.environ.get('QCOW2_PROBE', 'build/qcow2_probe')).resolve())
WORK = Path(sys.argv[1] if len(sys.argv) > 1 else '.scratch/qcow2-writes').resolve()
WORK.mkdir(parents=True, exist_ok=True)


def run(*args, **kwargs):
    result = subprocess.run([str(a) for a in args], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, **kwargs)
    if result.returncode:
        raise RuntimeError(f'{args}: {result.returncode}: {result.stderr.decode(errors="replace")}')
    return result.stdout


def digest(path):
    with open(path, 'rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def check(image):
    run('qemu-img', 'check', image)
    run(TOOL, 'fsck', '-n', image)


for layout in ['raw', 'labeled']:
    seed = WORK / f'{layout}.img'
    run(TOOL, 'mkimg', *(['--raw'] if layout == 'raw' else []),
        '--force-overwrite', seed, '64M')
    run(TOOL, 'mkfile', seed, '/seed', 'inherited contents')
    for compat, cluster in [('0.10', 512), ('1.1', 4096), ('1.1', 65536)]:
        base = WORK / f'base-{layout}-{compat}-{cluster}.qcow2'
        head = WORK / f'head-{layout}-{compat}-{cluster}.qcow2'
        run('qemu-img', 'convert', '-f', 'raw', '-O', 'qcow2', '-o',
            f'compat={compat},cluster_size={cluster}', seed, base)
        run('qemu-img', 'create', '-f', 'qcow2', '-F', 'qcow2', '-b', base.name,
            '-o', f'compat={compat},cluster_size={cluster}', head)
        before = digest(base)
        run(TOOL, 'mkfile', head, '/new', 'new contents')
        run(TOOL, 'mkfile', '--overwrite', head, '/seed', 'changed head')
        run(TOOL, 'mkfile', '--truncate', head, '/new', '150000')
        run(TOOL, 'mkfile', '--mkdir', head, '/directory')
        for n in range(12):
            run(TOOL, 'mkfile', head, '/directory/' + str(n) + '-' + 'x' * 200, str(n))
        run(TOOL, 'mkfile', '--unlink', head, '/directory/0-' + 'x' * 200)
        assert run(TOOL, 'browse', '--raw', head, '/seed') == b'changed head'
        expected = b'new contents' + b'\0' * (150000 - len(b'new contents'))
        assert run(TOOL, 'browse', '--raw', head, '/new') == expected
        assert digest(base) == before
        check(head)
        converted = WORK / 'converted.raw'
        run('qemu-img', 'convert', '-f', 'qcow2', '-O', 'raw', head, converted)
        assert run(TOOL, 'browse', '--raw', converted, '/new') == expected
        run(TOOL, 'fsck', '-n', converted)

# Tiny clusters force L2 allocation, new refcount blocks, and table relocation.
growth = WORK / 'growth.qcow2'
run('qemu-img', 'create', '-f', 'qcow2', '-o', 'cluster_size=512', growth, '12M')
before = struct.unpack('>QI', growth.read_bytes()[48:60])
run(PROBE, 'write', growth, 0, 9 * 1024 * 1024)
after = struct.unpack('>QI', growth.read_bytes()[48:60])
assert after[1] > before[1], (before, after)
run('qemu-img', 'check', growth)
expected = bytes((i * 17 + 31) % 251 for i in range(9 * 1024 * 1024))
assert run(PROBE, 'read', growth, 0, len(expected)) == expected
run('qemu-img', 'convert', '-f', 'qcow2', '-O', 'raw', growth, WORK / 'growth.raw')
assert (WORK / 'growth.raw').read_bytes()[:len(expected)] == expected
print('QCOW2 write regressions passed')
