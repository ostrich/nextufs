#!/usr/bin/env python3
"""Disposable QCOW2 interoperability fixtures; QEMU is only a test oracle."""
import json
import os
from pathlib import Path
import subprocess
import sys

TOOL = str(Path(os.environ.get('NEXTUFS', './nextufs')).resolve())
WORK = Path(sys.argv[1] if len(sys.argv) > 1 else '.scratch/qcow2').resolve()
WORK.mkdir(parents=True, exist_ok=True)


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs).stdout


raw = WORK / 'seed.raw'
run(TOOL, 'mkimg', '--raw', '--force-overwrite', raw, '64M')
payload = bytes(range(256)) * 513
(WORK / 'payload').write_bytes(payload)
run(TOOL, 'mkfile', '--from-file', raw, '/payload', WORK / 'payload')
for compat in ['0.10', '1.1']:
    for cluster in [512, 4096, 65536, 2097152]:
        image = WORK / f'read-{compat}-{cluster}.qcow2'
        run('qemu-img', 'convert', '-f', 'raw', '-O', 'qcow2', '-o',
            f'compat={compat},cluster_size={cluster}', raw, image)
        info = json.loads(run(TOOL, 'info', '--json', image))
        assert info['source_format'] == 'qcow2'
        assert info['source_kind'] == 'container image'
        assert info['image_bytes'] == raw.stat().st_size
        assert info['backing_bytes'] == image.stat().st_size
        assert run(TOOL, 'browse', '--raw', image, '/payload') == payload
        before = image.read_bytes()
        run(TOOL, 'fsck', '-n', image)
        assert image.read_bytes() == before

base = WORK / 'base.qcow2'
run('qemu-img', 'convert', '-f', 'raw', '-O', 'qcow2', raw, base)
parents = [raw, base]
for i in range(3):
    image = WORK / f'layer{i}.qcow2'
    run('qemu-img', 'create', '-f', 'qcow2', '-F', 'qcow2' if i else 'raw',
        '-b', parents[-1].name if i else raw.name, image)
    parents.append(image)
    assert run(TOOL, 'browse', '--raw', image, '/payload') == payload
# Runtime commands use no QEMU programs, including writes to a backing chain.
head = parents[-1]
run(TOOL, 'info', head, env=dict(os.environ, PATH='/nonexistent'))
assert run(TOOL, 'browse', '--raw', head, '/payload',
           env=dict(os.environ, PATH='/nonexistent')) == payload
run(TOOL, 'mkfile', head, '/native', 'no QEMU runtime', env=dict(os.environ, PATH='/nonexistent'))
run(TOOL, 'fsck', '-n', head, env=dict(os.environ, PATH='/nonexistent'))
run('qemu-img', 'check', head)
before = head.read_bytes()
result = subprocess.run([TOOL, 'resize', 'grow', str(head), '73728'], capture_output=True)
assert result.returncode != 0
assert head.read_bytes() == before
# Existing VDI detection, size reporting, and writes remain compatible.
vdi = WORK / 'regression.vdi'
run('qemu-img', 'convert', '-f', 'raw', '-O', 'vdi', raw, vdi)
info = json.loads(run(TOOL, 'info', '--json', vdi))
assert info['source_format'] == 'vdi'
assert info['source_kind'] == 'container image'
assert info['backing_bytes'] == vdi.stat().st_size
assert run(TOOL, 'browse', '--raw', vdi, '/payload') == payload
run(TOOL, 'mkfile', vdi, '/vdi', 'VDI regression')
run(TOOL, 'fsck', '-n', vdi)
run('qemu-img', 'convert', '-f', 'vdi', '-O', 'raw', vdi, WORK / 'vdi-converted.raw')
assert run(TOOL, 'browse', '--raw', WORK / 'vdi-converted.raw', '/vdi') == b'VDI regression'
print('QCOW2 read regressions passed')
