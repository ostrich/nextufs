"""Header-only fixtures require no QEMU; one optional interoperability test does."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/qcow2_chain.py'


def qcow(path, parent=None, fmt=None, version=3, features=0, **kwargs):
    """Minimal first-cluster fixture, deliberately not a valid mapped disk."""
    data = bytearray(4096)
    struct.pack_into('>II', data, 0, 0x514649fb, version)
    struct.pack_into('>I', data, 20, 12)
    struct.pack_into('>Q', data, 24, 8 * 1024 * 1024)
    header_length = 72 if version == 2 else 104
    if version == 3:
        struct.pack_into('>QQQII', data, 72, features, kwargs.get('compatible', 0),
                         kwargs.get('autoclear', 0), kwargs.get('order', 4), header_length)
    offset = header_length
    if fmt is not None:
        value = fmt.encode('ascii')
        struct.pack_into('>II', data, offset, 0xe2792aca, len(value))
        data[offset + 8:offset + 8 + len(value)] = value
        offset += 8 + ((len(value) + 7) & ~7)
    if parent is not None:
        value = os.fsencode(parent)
        backing_offset = offset + 8
        struct.pack_into('>QI', data, 8, backing_offset, len(value))
        data[backing_offset:backing_offset + len(value)] = value
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


class ChainTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def invoke(self, command, path, status=0, text=False):
        args = [sys.executable, '-B', str(SCRIPT), command, str(path)]
        if not text:
            args += ['--json']
        p = subprocess.run(args, capture_output=True, text=True, timeout=10,
                           env=dict(os.environ, PATH='/nonexistent'))
        self.assertEqual(p.returncode, status, p.stderr)
        return p.stdout if text else json.loads(p.stdout)

    def test_branch_and_external_raw_parent(self):
        raw = self.root / 'outside.raw'
        raw.write_bytes(b'raw bytes')
        directory = self.root / 'vm'
        base = qcow(directory / 'base.disk', '../outside.raw', 'raw', version=2)
        middle = qcow(directory / 'middle.qcow2', 'base.disk', 'qcow2')
        qcow(directory / 'a.qcow2', 'middle.qcow2', 'qcow2')
        qcow(directory / 'b.qcow2', str(middle), 'qcow2')
        standalone = qcow(directory / 'alone.qcow2')
        report = self.invoke('scan', directory)
        images = {Path(i['path']).name: i for i in report['images']}
        self.assertEqual(images['outside.raw']['role'], 'base')
        self.assertEqual(images['base.disk']['version'], 2)
        self.assertEqual(images['middle.qcow2']['role'], 'intermediate')
        self.assertTrue(images['middle.qcow2']['branch'])
        self.assertEqual(images['a.qcow2']['role'], 'head')
        self.assertEqual(images['alone.qcow2']['role'], 'standalone')
        chain = self.invoke('trace', directory / 'a.qcow2')['images']
        self.assertEqual([Path(i['path']).name for i in chain],
                         ['outside.raw', 'base.disk', 'middle.qcow2', 'a.qcow2'])
        self.assertTrue(chain[-1]['selected'])
        self.assertFalse(chain[2]['branch'])  # sibling is outside trace scope
        text = self.invoke('scan', directory, text=True)
        self.assertIn('[branch]', text)
        self.assertIn('Header inspection only', text)
        self.assertEqual(images['alone.qcow2']['virtual_size'], 8 * 1024 * 1024)
        self.assertEqual(images['alone.qcow2']['physical_size'], 4096)

    def test_missing_and_cycles(self):
        missing = qcow(self.root / 'missing.qcow2', 'absent.raw', 'raw')
        report = self.invoke('trace', missing, status=1)
        self.assertTrue(report['errors'])
        self.assertEqual(report['images'][-1]['role'], 'orphan')
        a = qcow(self.root / 'a.qcow2', 'b.qcow2', 'qcow2')
        qcow(self.root / 'b.qcow2', 'a.qcow2', 'qcow2')
        self.assertIn('cycle', ' '.join(self.invoke('trace', a, status=1)['errors']))
        alias = self.root / 'alias.qcow2'
        os.link(a, alias)
        qcow(a, 'alias.qcow2', 'qcow2')
        self.assertIn('cycle', ' '.join(self.invoke('trace', a, status=1)['errors']))

    def test_depth_bound(self):
        parent = self.root / 'raw'
        parent.write_bytes(b'raw')
        for n in range(32):
            parent = qcow(self.root / f'{n}.qcow2', parent.name)
        report = self.invoke('trace', parent, status=1)
        self.assertEqual(len(report['images']), 32)
        self.assertIn('exceeds 32', ' '.join(report['errors']))

    def test_features_are_notes_not_integrity_claims(self):
        image = qcow(self.root / 'features.qcow2', features=31, compatible=1,
                     autoclear=1, order=5)
        report = self.invoke('trace', image)
        issues = report['images'][0]['nextufs_header_issues']
        for name in ['dirty image', 'corrupt image', 'external data file',
                     'non-default compression', 'extended L2 tables', 'lazy refcounts']:
            self.assertIn(name, issues)
        self.assertIn('refcounts are not checked', report['validation'])
        old = qcow(self.root / 'old.qcow2', version=1)
        self.assertIn('unsupported QCOW version: 1',
                      self.invoke('trace', old)['images'][0]['nextufs_header_issues'])

    def test_malformed_headers_and_explicit_formats(self):
        for data in [b'QFI\xfb', b'not QCOW2']:
            bad = self.root / 'bad.qcow2'
            bad.write_bytes(data)
            self.assertTrue(self.invoke('trace', bad, status=1)['errors'])
        raw = self.root / 'looks-qcow.raw'
        raw.write_bytes(b'QFI\xfb' + b'\0' * 100)
        child = qcow(self.root / 'child.qcow2', raw.name, 'raw')
        self.assertEqual(self.invoke('trace', child)['images'][0]['format'], 'raw')
        raw.write_bytes(b'ordinary raw contents')
        child = qcow(child, raw.name, 'qcow2')
        self.assertTrue(self.invoke('trace', child, status=1)['errors'])
        qcow(child, 'file', 'vdi')
        self.assertTrue(self.invoke('trace', child, status=1)['errors'])
        qcow(child)
        b = bytearray(child.read_bytes())
        struct.pack_into('>II', b, 104, 0x1234abcd, 0xffffffff)
        child.write_bytes(b)
        self.assertIn('extension', ' '.join(self.invoke('trace', child, status=1)['errors']))

    def test_header_length_alignment(self):
        for length in [104, 105, 111, 112]:
            with self.subTest(header_length=length):
                image = qcow(self.root / 'alignment.qcow2')
                data = bytearray(image.read_bytes())
                struct.pack_into('>I', data, 100, length)
                image.write_bytes(data)
                valid = length % 8 == 0
                for command in ['trace', 'scan']:
                    report = self.invoke(command, image if command == 'trace' else self.root,
                                         status=0 if valid else 1)
                    self.assertEqual(bool(report['errors']), not valid)
                    if not valid:
                        self.assertIn('invalid header length', ' '.join(report['errors']))
                self.assertEqual(image.read_bytes(), data)

    def test_regular_files_only_and_no_writes(self):
        image = qcow(self.root / 'image.qcow2')
        fifo = self.root / 'pipe.qcow2'
        os.mkfifo(fifo)
        before = hashlib.sha256(image.read_bytes()).digest()
        self.invoke('scan', self.root)
        self.invoke('trace', fifo, status=1)
        self.assertEqual(before, hashlib.sha256(image.read_bytes()).digest())
        empty = self.root / 'empty'
        empty.mkdir()
        self.assertEqual(self.invoke('scan', empty)['images'], [])

    @unittest.skipUnless(shutil.which('qemu-img'), 'qemu-img unavailable')
    def test_qemu_generated_chain(self):
        base = self.root / 'base.qcow2'
        head = self.root / 'head.qcow2'
        subprocess.run(['qemu-img', 'create', '-f', 'qcow2', str(base), '8M'],
                       check=True, capture_output=True)
        subprocess.run(['qemu-img', 'create', '-f', 'qcow2', '-F', 'qcow2',
                        '-b', base.name, str(head)], check=True, capture_output=True)
        before = [hashlib.sha256(p.read_bytes()).digest() for p in (base, head)]
        expected = json.loads(subprocess.run(['qemu-img', 'info', '--backing-chain',
                              '--output=json', str(head)], check=True,
                              capture_output=True, text=True).stdout)
        actual = self.invoke('trace', head)['images']
        self.assertEqual([i['virtual_size'] for i in actual],
                         [i['virtual-size'] for i in reversed(expected)])
        self.assertEqual([i['path'] for i in actual], [str(base), str(head)])
        self.invoke('scan', self.root)
        self.assertEqual(before, [hashlib.sha256(p.read_bytes()).digest() for p in (base, head)])


if __name__ == '__main__':
    unittest.main()
