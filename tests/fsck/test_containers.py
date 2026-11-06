"""Repair inherited corruption without modifying either backing image."""
import os
from pathlib import Path
import sys
from fixtures import seed, damage, preserved
from model import UFS
from support import run, tool, check, vdi

work = Path(sys.argv[1])
kind = sys.argv[2]
source = work / 'seed.raw'
before = seed(source)
for case in ('cg-bitmap-bad', 'unreferenced-dir', 'dup-block'):
    directory = work / case
    directory.mkdir()
    raw = directory / 'damaged.raw'
    damage(source, raw, case)
    base = directory / ('base.' + kind)
    head = directory / ('head.' + kind)
    if kind == 'vdi':
        vdi(raw, base)
        vdi(raw, head, base)
    else:
        run('qemu-img', 'convert', '-f', 'raw', '-O', 'qcow2', raw, base)
        run('qemu-img', 'create', '-f', 'qcow2', '-F', 'qcow2', '-b', str(base), head)
    original = base.read_bytes()
    frozen = head.read_bytes()
    tool('fsck', '-n', head, status=8)
    assert head.read_bytes() == frozen
    tool('fsck', '-y', head)
    check(head)
    exported = directory / 'repaired.raw'
    if kind == 'qcow2':
        run('qemu-img', 'check', head)
        run('qemu-img', 'convert', '-f', 'qcow2', '-O', 'raw', head, exported)
    else:
        # Read the logical view through the backend; the UFS oracle stays independent.
        exported.write_bytes(run(os.environ['FS_PROBE'], 'bytes-read', head, '0', str(raw.stat().st_size)))
    preserved(before, UFS(exported).snapshot(), case)
    assert base.read_bytes() == original
    tool('fsck', '-y', head)
    settled = head.read_bytes()
    tool('fsck', '-y', head)
    assert head.read_bytes() == settled
    assert base.read_bytes() == original
