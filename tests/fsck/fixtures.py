"""Shared repair fixtures and explicit data-preservation expectations."""
import hashlib
import json
import os
import shutil
from support import ROOT, tool, run, check
from model import UFS

CASES = tuple(p.stem for p in sorted((ROOT / 'tests/fsck/cases').glob('*.txt')))
LOSS = {
    'bad-file-type': ('/alpha',), 'out-of-range-block': ('/alpha',),
    'partially-allocated-inode': ('/alpha',),
    'dir-entry-fclear': ('/dir/file',), 'dir-entry-unallocated': ('/dir/file',),
    'dup-block': ('/alpha', '/dir/file'), 'zero-length-dir': ('/dir',),
}
RELOCATE = {
    'extra-dot': ('/dir/file',), 'extra-dotdot': ('/dir/file',),
    'invalid-dir-inode': ('/dir/file',), 'unreferenced-alpha': ('/alpha',),
    'unreferenced-dir': ('/dir',), 'lostfound-missing': ('/alpha',),
    'lostfound-not-dir': ('/alpha',), 'zero-length-dir': ('/dir/file',),
}


def seed(path, layout='raw'):
    args = [] if layout == 'labeled' else ['--raw']
    geometry = ['32', '4', '8192', '2048'] if layout in ('large-fragments', 'cd') else []
    tool('mkimg', *args, path, '33M', *geometry)
    if layout == 'cd':
        run(os.environ['CORRUPT'], 'cd-sectors', path)
    tool('mkfile', path, '/alpha', 'alpha')
    tool('mkfile', '--mkdir', path, '/dir')
    tool('mkfile', path, '/dir/file', 'nested')
    tool('mkfile', path, '/big', 'x')
    tool('mkfile', '--truncate', path, '/big', '120000')
    # Later entries and a deeper tree must survive unrelated repairs.
    tool('mkfile', '--mkdir', path, '/canary')
    tool('mkfile', '--mkdir', path, '/canary/deep')
    tool('mkfile', path, '/canary/deep/payload', 'untouched payload 0123456789')
    tool('mkfile', path, '/tail', 'last root entry')
    check(path)
    return UFS(path).snapshot()


def damage(source, target, case):
    shutil.copyfile(source, target)
    run(os.environ['CORRUPT'], case, target)
    assert source.read_bytes() != target.read_bytes(), ('ineffective corruption', case)


def preserved(before, after, case):
    """Every surviving object is accounted for; loss is explicitly case-specific."""
    def recovered(inode):
        matches = [p for p, obj in after.items() if p.startswith('/lost+found/#')
                   and '/' not in p[len('/lost+found/'):] and obj['inode'] == inode]
        assert len(matches) == 1, ('missing recovery entry', inode)
        assert int(matches[0].split('#')[-1]) == inode
        return matches[0]
    expected = {}
    root_rebuilt = case in ('root-not-dir', 'root-unallocated')
    for path, old in before.items():
        if path in LOSS.get(case, ()):
            assert old['inode'] not in {v['inode'] for v in after.values()}
            continue
        if path == '/':
            if not root_rebuilt:
                for key in ('inode', 'mode', 'uid', 'gid'):
                    assert after[path][key] == old[key]
            continue
        if path == '/lost+found' and (root_rebuilt or case.startswith('lostfound-')):
            continue
        dest = path
        if root_rebuilt:
            matches = [p for p, v in after.items() if v['inode'] == old['inode']]
            assert len(matches) == 1, ('missing recovered inode', path)
            dest = matches[0]
        elif case == 'extraneous-dir-link':
            if path == '/alpha':
                dest = recovered(old['inode'])
            elif path == '/dir' or path.startswith('/dir/'):
                dest = '/dirlink' + path[4:]
        else:
            for prefix in RELOCATE.get(case, ()):
                if path == prefix or path.startswith(prefix + '/'):
                    dest = recovered(before[prefix]['inode']) + path[len(prefix):]
                    break
        assert dest in after, ('missing path', case, path, dest)
        new = after[dest]
        keys = ['inode', 'mode', 'uid', 'gid', 'gen', 'flags']
        if 'sha256' in old:
            keys += ['size', 'sha256', 'atime', 'mtime']
        if case == 'partial-indirect' and path == '/big':
            keys.remove('size')
            keys.remove('sha256')
            assert new['size'] == 98305
            assert new['sha256'] == hashlib.sha256(b'x' + bytes(98304)).hexdigest()
        for key in keys:
            assert new[key] == old[key], (case, path, key, old[key], new[key])
        expected[dest] = new
    # Newly allocated recovery directories are permitted, invented payloads are not.
    for path, obj in after.items():
        if 'sha256' in obj and path not in expected:
            assert case == 'lostfound-not-dir' and path.startswith('/lost+found/#'), ('unexpected file', path)
            assert obj['size'] == 15 and obj['sha256'] == hashlib.sha256(b'not a directory').hexdigest()


def repair(disk, before, case):
    disk.with_suffix('.before.json').write_text(json.dumps(before, indent=2))
    damaged = disk.read_bytes()
    output = tool('fsck', '-n', disk, status=8)
    assert disk.read_bytes() == damaged, 'read-only checker modified image'
    if case == 'dir-entry-fclear':
        assert b'DUP/BAD' in output, output
    tool('fsck', '-y', disk)
    check(disk)
    after = UFS(disk).snapshot()
    disk.with_suffix('.after.json').write_text(json.dumps(after, indent=2))
    preserved(before, after, case)
    # A successful repair can retain the corrupted flag until the next check.
    tool('fsck', '-y', disk)
    settled = disk.read_bytes()
    tool('fsck', '-y', disk)
    assert disk.read_bytes() == settled, 'repair did not converge'
    check(disk)
    assert disk.read_bytes() == settled
