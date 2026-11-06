"""VDI discovery helper parsing, graph handling and CLI regressions."""

import json
from unittest.mock import patch
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import vdi_chain as helper


def header(path, identity=1, parent=0):
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray(512)
    for offset, value in [
        (0x40, 0xBEDA107F),
        (0x44, 0x10001),
        (0x48, 0x190),
        (0x4C, 4 if parent else 1),
        (0x178, 1048576),
        (0x180, 8),
    ]:
        struct.pack_into('<I', data, offset, value)
    struct.pack_into('<Q', data, 0x170, 8 * 1048576)
    data[0x188:0x198] = identity.to_bytes(16, 'little')
    data[0x1A8:0x1B8] = parent.to_bytes(16, 'little')
    path.write_bytes(data)
    return path


class VDIChainTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def cli(self, *args, status=0):
        result = subprocess.run(
            [sys.executable, ROOT / 'tools/vdi_chain.py', *map(str, args)],
            capture_output=True,
            timeout=5,
        )
        self.assertEqual(result.returncode, status, result.stderr)
        self.assertNotIn(b'Traceback', result.stderr)
        return result.stdout

    def test_uuid_byte_order(self):
        self.assertEqual(
            helper.format_uuid(bytes(range(16))), '03020100-0504-0706-0809-0a0b0c0d0e0f'
        )
        with self.assertRaises(ValueError):
            helper.format_uuid(b'x')

    def test_invalid_headers_and_missing_files(self):
        self.assertIsNone(helper.parse_vdi(self.root / 'absent'))
        disk = header(self.root / 'image.vdi')
        original = disk.read_bytes()
        for content in [original[:100], b'\0' * 512]:
            disk.write_bytes(content)
            self.assertIsNone(helper.parse_vdi(disk))
        for offset, value in [(0x44, 2), (0x48, 1)]:
            content = bytearray(original)
            struct.pack_into('<I', content, offset, value)
            disk.write_bytes(content)
            self.assertIsNone(helper.parse_vdi(disk))

    def test_roles_and_snapshot_trace(self):
        base = header(self.root / 'base.vdi')
        middle = header(self.root / 'Snapshots/middle.vdi', 2, 1)
        head = header(self.root / 'Snapshots/head.vdi', 3, 2)
        header(self.root / 'orphan.vdi', 4, 99)
        header(self.root / 'standalone.VDI', 5)
        before = {p: p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        rows = json.loads(self.cli('scan', '--json', self.root))
        self.assertEqual(
            {Path(r['path']).stem: r['role'] for r in rows},
            {
                'base': 'base',
                'middle': 'intermediate',
                'head': 'head',
                'orphan': 'orphan',
                'standalone': 'standalone',
            },
        )
        chain = json.loads(self.cli('trace', '--json', head))
        self.assertEqual([Path(r['path']) for r in chain], [base, middle, head])
        self.assertEqual([r['traced'] for r in chain], [False, False, True])
        self.assertTrue(all(p.read_bytes() == data for p, data in before.items()))
        self.assertIn(b'Orphan:', self.cli('scan', self.root))

    def test_duplicate_uuid_rejected(self):
        base = header(self.root / 'base.vdi')
        shutil.copyfile(base, self.root / 'duplicate.vdi')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            helper.build_uuid_index(helper.scan_tree(self.root))
        self.cli('scan', '--json', self.root, status=1)

    def test_cycle_is_bounded_and_reported(self):
        a = header(self.root / 'a.vdi', 1, 2)
        header(self.root / 'b.vdi', 2, 1)
        self.assertIn(b'Cycle:', self.cli('scan', self.root))
        self.cli('trace', a, status=1)

    def test_long_cycle_does_not_depend_on_python_recursion_limit(self):
        count = 1100
        infos = [
            helper.VDIInfo(
                self.root / f'{i}.vdi',
                str(i),
                str((i - 1) % count),
                4,
                8192,
                4096,
                2,
                0,
                512,
            )
            for i in range(count)
        ]
        with patch('builtins.print') as output:
            helper.print_scan(infos)
        self.assertTrue(any('Cycle:' in str(call) for call in output.call_args_list))

    def test_invalid_cli_and_scan_guard(self):
        self.cli(status=2)
        self.cli('scan', self.root, status=1)
        self.assertEqual(
            json.loads(self.cli('scan', '--force', '--json', self.root)), []
        )
        self.cli('trace', self.root / 'missing.vdi', status=1)


if __name__ == '__main__':
    unittest.main()
