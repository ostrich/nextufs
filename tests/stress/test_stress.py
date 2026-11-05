"""Bounded, seeded campaigns; the C harness retains detailed failure traces."""
import os
from pathlib import Path
import shutil
import sys
from support import check, image, run

work = Path(sys.argv[1])
mode = sys.argv[2]
if mode == 'image':
    disk = work / 'supplied.img'
    shutil.copyfile(os.environ['TEST_IMAGE'], disk)
    args = ['--seed', '0x2468ace0', '--ops', '220', '--root', '/private/tmp/nextufs-stress']
else:
    disk = image(work / 'stress.raw', '8M', raw=True)
    args = ['--seed', '0x13579bdf', '--ops', '250' if mode == 'offline' else '120']
if mode == 'batch':
    args += ['--batch', '4']
if mode == 'fuse':
    args += ['--backend', 'fuse']
if mode == 'qcow2':
    converted = work / 'stress.qcow2'
    run('qemu-img', 'convert', '-f', 'raw', '-O', 'qcow2', disk, converted)
    disk = converted
    args += ['--backend', 'fuse']
output = run(os.environ['NEXTUFS_STRESS'], *args, '--save-fail-dir', work / 'failure',
             '--quiet', disk, timeout=600)
assert (b'nextufs_stress: batch ok' if mode == 'batch' else b'nextufs_stress: ok') in output
check(disk)
if mode == 'qcow2':
    run('qemu-img', 'check', disk)
