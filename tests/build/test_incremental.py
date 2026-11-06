"""Verify real compiler dependency discovery, no-op builds and flag changes."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
from support import ROOT

work = Path(sys.argv[1]) / 'source'
work.mkdir()
for name in ['src', 'include', 'tools']:
    shutil.copytree(
        ROOT / name, work / name, ignore=shutil.ignore_patterns('__pycache__')
    )
for name in ['Makefile', 'VERSION']:
    shutil.copyfile(ROOT / name, work / name)
env = dict(os.environ)
for name in [
    'MAKEFLAGS',
    'MFLAGS',
    'CPPFLAGS',
    'CFLAGS',
    'LDFLAGS',
    'LDLIBS',
    'FSCK_CFLAGS',
    'FORMAT_CFLAGS',
]:
    env.pop(name, None)


def build(*args):
    subprocess.run(['make', '-j4', 'WITH_FUSE=0', *args], cwd=work, env=env, check=True)


def stamps():
    return {
        p.relative_to(work): p.stat().st_mtime_ns for p in work.glob('build/obj/**/*.o')
    }


build()
before = stamps()
assert before
build()
assert stamps() == before
header = work / 'include/nextufs_ufs_types.h'
header.write_text(header.read_text() + '\n/* transitive dependency test */\n')
build()
after = stamps()
assert (
    after[Path('build/obj/fuse-0/src/fsck/byteorder.o')]
    != before[Path('build/obj/fuse-0/src/fsck/byteorder.o')]
)
assert (
    after[Path('build/obj/fuse-0/src/core/size.o')]
    == before[Path('build/obj/fuse-0/src/core/size.o')]
)
build('CFLAGS=-O1 -g -std=gnu99 -Wall -Wextra')
changed = stamps()
assert (
    changed[Path('build/obj/fuse-0/src/core/size.o')]
    != after[Path('build/obj/fuse-0/src/core/size.o')]
)
build('CFLAGS=-O1 -g -std=gnu99 -Wall -Wextra')
assert stamps() == changed
assert b'+source' in subprocess.check_output([work / 'nextufs', '--version'])
