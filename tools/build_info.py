#!/usr/bin/env python3
"""Write build metadata only when its contents change."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

mode, output = sys.argv[1:3]
path = Path(output)
if mode == 'config':
    names = (
        'CC',
        'CPPFLAGS',
        'CFLAGS',
        'FORMAT_CFLAGS',
        'FORMAT_CPPFLAGS',
        'FSCK_CFLAGS',
        'FSCK_CPPFLAGS',
        'FUSE_CFLAGS',
        'FUSE_LIBS',
        'LDFLAGS',
        'LDLIBS',
    )
    content = (
        json.dumps({name: os.environ.get(name, '') for name in names}, sort_keys=True)
        + '\n'
    )
else:
    version = os.environ.get('VERSION', '0.1.0-dev')
    root = Path(__file__).resolve().parents[1]
    revision = 'source'
    if shutil.which('git'):
        top = subprocess.run(
            ['git', 'rev-parse', '--show-toplevel'],
            cwd=root,
            capture_output=True,
            text=True,
        )
        result = subprocess.run(
            ['git', 'describe', '--always', '--dirty'],
            cwd=root,
            capture_output=True,
            text=True,
        )
        if top.returncode or Path(top.stdout.strip()) != root:
            result.returncode = 1
        revision = result.stdout.strip() if result.returncode == 0 else 'source'
    content = '#define NEXTUFS_VERSION ' + json.dumps(version + '+' + revision) + '\n'
path.parent.mkdir(parents=True, exist_ok=True)
if not path.exists() or path.read_text() != content:
    path.write_text(content)
