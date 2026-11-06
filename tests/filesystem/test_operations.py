"""Deterministic mutation regressions on generated images, also checked via FUSE."""
import os
from pathlib import Path
import stat
import sys
from support import check, image, mounted, node, read, tool

CASES = ('write', 'file-import', 'directory-growth', 'unlink', 'mkdir', 'rewrite',
         'links', 'rmdir', 'metadata', 'rename', 'truncate', 'special', 'permissions')


def scenario(work, case, fuse=False):
    disk = image(work / 'image.img')
    for path in ['/private', '/private/tmp']:
        tool('mkfile', '--mkdir', disk, path)
    root = '/private/tmp/'
    expected, absent = {}, []

    def put(name, data):
        tool('mkfile', disk, root + name, data)

    def mutate(option, *args, **kwargs):
        return tool('mkfile', option, disk, *args, **kwargs)

    if case == 'write':
        put('file', 'hello from linux nextufs')
        expected['file'] = b'hello from linux nextufs'
    elif case == 'file-import':
        payload = bytes(65 + i % 26 for i in range(3000))
        source = work / 'payload'
        source.write_bytes(payload)
        mutate('--from-file', root + 'file', source)
        expected['file'] = payload
    elif case == 'directory-growth':
        for i in range(120):
            put(f'nextufs-grow-{i}', '')
        entries = tool('browse', '--json', disk, root)
        import json
        names = {entry['name'] for entry in json.loads(entries)}
        assert all(f'nextufs-grow-{i}' in names for i in range(120))
        assert node(disk, root)['size'] >= 3072
        expected['nextufs-grow-119'] = b''
    elif case == 'unlink':
        put('file', 'unlink me')
        mutate('--unlink', root + 'file')
        absent.append('file')
    elif case == 'mkdir':
        mutate('--mkdir', root + 'directory')
        put('directory/child', 'inside dir')
        expected['directory/child'] = b'inside dir'
    elif case == 'rewrite':
        put('file', 'start')
        mutate('--overwrite', root + 'file', 'middle')
        mutate('--append', root + 'file', '+end')
        expected['file'] = b'middle+end'
    elif case == 'links':
        put('source', 'linked data')
        mutate('--link', root + 'source', root + 'hard')
        mutate('--symlink', 'hard', root + 'soft')
        expected.update(source=b'linked data', hard=b'linked data', soft=b'linked data')
        assert node(disk, root + 'source')['inode'] == node(disk, root + 'hard')['inode']
    elif case == 'rmdir':
        mutate('--mkdir', root + 'directory')
        mutate('--rmdir', root + 'directory')
        absent.append('directory')
    elif case == 'metadata':
        put('file', 'meta')
        mutate('--chmod', root + 'file', '0600')
        mutate('--chown', root + 'file', '123', '456')
        mutate('--utimes', root + 'file', '111111111', '222222222')
        entry = node(disk, root + 'file')
        assert entry['mode'] == 0o100600 and entry['uid'] == 123 and entry['gid'] == 456
        assert entry['atime'] == 111111111 and entry['mtime'] == 222222222
        expected['file'] = b'meta'
    elif case == 'rename':
        for directory in ['a', 'b', 'a/subdir']:
            mutate('--mkdir', root + directory)
        put('a/file', 'rename payload')
        put('b/existing', 'replace me')
        mutate('--rename', root + 'a/file', root + 'b/file')
        mutate('--rename', root + 'b/file', root + 'b/existing')
        mutate('--rename', root + 'a/subdir', root + 'b/subdir')
        expected['b/existing'] = b'rename payload'
        absent.extend(['a/file', 'a/subdir', 'b/file'])
        assert stat.S_ISDIR(node(disk, root + 'b/subdir')['mode'])
    elif case == 'truncate':
        payload = bytes(65 + i % 26 for i in range(120000))
        source = work / 'payload'
        source.write_bytes(payload)
        mutate('--from-file', root + 'file', source)
        assert read(disk, root + 'file') == payload
        mutate('--truncate', root + 'file', '4096')
        assert read(disk, root + 'file') == payload[:4096]
        mutate('--truncate', root + 'file', '16384')
        mutate('--pwrite', root + 'file', '12288', 'tail')
        expected['file'] = payload[:4096] + b'\0' * 8192 + b'tail' + b'\0' * 4092
    elif case == 'special':
        mutate('--mknod', root + 'fifo', '010644', '0')
        mutate('--mknod', root + 'char', '020600', '259')
        assert stat.S_ISFIFO(node(disk, root + 'fifo')['mode'])
        assert node(disk, root + 'char')['mode'] == 0o20600
        assert b'rdev:         259' in tool('browse', disk, root + 'char')
    elif case == 'permissions':
        put('file', 'perm data')
        mutate('--chown', root + 'file', '1000', '200')
        mutate('--chmod', root + 'file', '0644')
        mutate('--mkdir', root + 'sticky')
        mutate('--chmod', root + 'sticky', '01777')
        put('sticky/victim', 'victim')
        mutate('--chown', root + 'sticky/victim', '1000', '200')

        def policy(uid, gid, option, *args, status=0):
            tool('mkfile', '--policy', 'user', '--uid', str(uid), '--gid', str(gid),
                 option, disk, *args, status=status)

        policy(1001, 200, '--chmod', root + 'file', '0600', status=1)
        policy(1000, 201, '--chmod', root + 'file', '03755')
        policy(1000, 333, '--chown', root + 'file', 'keep', '333')
        policy(1000, 333, '--chown', root + 'file', '1002', '333', status=1)
        policy(1001, 333, '--utimes', root + 'file', '111111111', '222222222', status=1)
        policy(1001, 1001, '--unlink', root + 'sticky/victim', status=1)
        policy(1001, 1001, '--rename', root + 'sticky/victim', root + 'sticky/victim2', status=1)
        tool('mkfile', '--policy', 'user', '--uid', '40000', '--gid', '0', disk,
             root + 'overflow', 'x', status=2)
        policy(0, 0, '--chown', root + 'file', '40000', '0', status=1)
        entry = node(disk, root + 'file')
        assert entry['mode'] == 0o100755 and entry['uid'] == 1000 and entry['gid'] == 333
        expected.update(file=b'perm data', **{'sticky/victim': b'victim'})
        absent.append('overflow')
    else:
        raise ValueError(case)

    for name, contents in expected.items():
        assert read(disk, root + name) == contents, name
    for name in absent:
        tool('browse', disk, root + name, status=1)
    check(disk)
    if fuse:
        with mounted(disk, work) as mount:
            base = mount / 'private/tmp'
            for name, contents in expected.items():
                assert (base / name).read_bytes() == contents, name
            for name in absent:
                assert not (base / name).exists(), name
            if case == 'links':
                assert os.readlink(base / 'soft') == 'hard'
                assert (base / 'source').stat().st_ino == (base / 'hard').stat().st_ino
            if case == 'rename':
                assert (base / 'b/subdir').is_dir()
            if case == 'special':
                assert stat.S_ISFIFO((base / 'fifo').stat().st_mode)
                info = (base / 'char').stat()
                assert stat.S_ISCHR(info.st_mode) and info.st_rdev == 259
        check(disk)


if __name__ == '__main__':
    scenario(Path(sys.argv[1]), sys.argv[2], '--fuse' in sys.argv[3:])
