"""Independent, bounded reader for the generated UFS repair fixtures.

This deliberately uses struct and on-disk offsets rather than libnextufs or fsck
when checking the namespace, inode identities and recovered payloads.
"""
import hashlib
import struct


class UFS:
    def __init__(self, path):
        self.raw = path.read_bytes()
        # Fixture layouts have either no label or the standard front porch.
        self.base = next((b for b in (0, 160 * 1024) if
            len(self.raw) >= b + 8192 + 1376 and
            self.u32(b + 8192 + 1372) == 0x11954), None)
        if self.base is None:
            raise AssertionError('fixture superblock not found')
        self.sb = self.base + 8192
        self.block = self.field(0x30)
        self.frag = self.field(0x34)
        self.ipg = self.field(0xb8)
        self.fpg = self.field(0xbc)
        assert self.block in (4096, 8192) and self.frag in (1024, 2048)

    def u32(self, off):
        return struct.unpack_from('>I', self.raw, off)[0]

    def field(self, off):
        return self.u32(self.sb + off)

    def inode_offset(self, number):
        cg, slot = divmod(number, self.ipg)
        start = cg * self.fpg + self.field(0x18) * (cg & (~self.field(0x1c) & 0xffffffff))
        return self.base + (start + self.field(0x10)) * self.frag + slot * 128

    def inode(self, number):
        off = self.inode_offset(number)
        mode, nlink, uid, gid = struct.unpack_from('>4H', self.raw, off)
        size = struct.unpack_from('>Q', self.raw, off + 8)[0]
        assert size <= 0x7fffffff
        return dict(inode=number, mode=mode, nlink=nlink, uid=uid, gid=gid,
                    size=size, blocks=self.u32(off + 104), gen=self.u32(off + 108),
                    flags=self.u32(off + 100), atime=self.u32(off + 16),
                    mtime=self.u32(off + 24))

    def data_frag(self, number, logical):
        off = self.inode_offset(number)
        if logical < 12:
            return self.u32(off + 40 + logical * 4)
        logical -= 12
        n = self.block // 4
        for level in (1, 2, 3):
            if logical < n ** level:
                ptr = self.u32(off + 88 + (level - 1) * 4)
                for depth in range(level - 1, -1, -1):
                    if not ptr:
                        return 0
                    index, logical = divmod(logical, n ** depth)
                    ptr = self.u32(self.base + ptr * self.frag + index * 4)
                return ptr
            logical -= n ** level
        raise AssertionError('logical block outside UFS addressing')

    def data(self, inode):
        size, number = inode['size'], inode['inode']
        assert size <= 64 * 1024 * 1024, 'fixture payload exceeds reader bound'
        if inode['mode'] & 0xf000 == 0xa000 and inode['flags'] & 1:
            off = self.inode_offset(number)
            return self.raw[off + 40:off + 40 + size]
        result = bytearray()
        for logical in range((size + self.block - 1) // self.block):
            frag = self.data_frag(number, logical)
            length = min(self.block, size - len(result))
            off = self.base + frag * self.frag
            chunk = self.raw[off:off + length] if frag else bytes(length)
            assert len(chunk) == length
            result.extend(chunk)
        return bytes(result)

    def entries(self, number):
        data = self.data(self.inode(number))
        entries = {}
        offset = 0
        while offset < len(data):
            ino, reclen, namlen = struct.unpack_from('>IHH', data, offset)
            assert reclen >= 8 and reclen % 4 == 0
            assert offset + reclen <= len(data) and reclen <= 1024 - offset % 1024
            if ino:
                assert 0 < namlen <= 255 and 8 + namlen < reclen
                name = data[offset + 8:offset + 8 + namlen]
                assert b'\0' not in name and b'/' not in name
                assert data[offset + 8 + namlen] == 0 and name not in entries
                entries[name] = (ino, self.base + self.data_frag(number, offset // self.block)
                                 * self.frag + offset % self.block)
            offset += reclen
        return entries

    def cg_offset(self, group):
        start = group * self.fpg + self.field(0x18) * (group & (~self.field(0x1c) & 0xffffffff))
        return self.base + (start + self.field(0x0c)) * self.frag

    def allocation(self, objects):
        owners = {}
        checked = set()
        for obj in objects.values():
            number = obj['inode']
            if number in checked:
                continue
            checked.add(number)
            group, slot = divmod(number, self.ipg)
            cg = self.cg_offset(group)
            assert self.raw[cg + 724 + slot // 8] & (1 << (slot % 8)), ('inode marked free', number)
            if obj['mode'] & 0xf000 not in (0x4000, 0x8000, 0xa000):
                continue
            if obj['mode'] & 0xf000 == 0xa000 and obj['flags'] & 1:
                continue
            fragments = 0
            def claim(first, count):
                nonlocal fragments
                assert first > 0 and first + count <= self.field(0x24)
                for frag in range(first, first + count):
                    assert frag not in owners, ('duplicate fragment', frag, number)
                    owners[frag] = number
                    group, index = divmod(frag, self.fpg)
                    bitmap = self.cg_offset(group) + 984
                    assert not self.raw[bitmap + index // 8] & (1 << (index % 8)), ('used fragment marked free', frag)
                fragments += count
            for logical in range((obj['size'] + self.block - 1) // self.block):
                frag = self.data_frag(number, logical)
                if frag:
                    length = self.block if logical >= 12 else min(self.block, obj['size'] - logical * self.block)
                    claim(frag, (length + self.frag - 1) // self.frag)
            def indirect(ptr, depth):
                if not ptr:
                    return
                claim(ptr, self.block // self.frag)
                if depth > 1:
                    for index in range(self.block // 4):
                        indirect(self.u32(self.base + ptr * self.frag + index * 4), depth - 1)
            for level in (1, 2, 3):
                indirect(self.u32(self.inode_offset(number) + 88 + 4 * (level - 1)), level)
            assert obj['blocks'] == fragments * self.field(0x7c), ('block count', obj, fragments)

    def snapshot(self):
        result, seen = {}, set()
        def visit(number, path, parent):
            inode = self.inode(number)
            assert inode['mode'] & 0xf000
            result[path] = inode
            if inode['mode'] & 0xf000 == 0x4000:
                assert number not in seen, 'directory cycle or extra directory link'
                seen.add(number)
                entries = self.entries(number)
                assert entries[b'.'][0] == number and entries[b'..'][0] == parent
                for name, (child, _) in entries.items():
                    if name not in (b'.', b'..'):
                        visit(child, path.rstrip('/') + '/' + name.decode('ascii'), number)
            elif inode['mode'] & 0xf000 in (0x8000, 0xa000):
                inode['sha256'] = hashlib.sha256(self.data(inode)).hexdigest()
        visit(2, '/', 2)
        # Count directory references independently, including . and .. .
        references = {}
        for inode in result.values():
            if inode['mode'] & 0xf000 == 0x4000:
                for number, _ in self.entries(inode['inode']).values():
                    references[number] = references.get(number, 0) + 1
        for inode in result.values():
            assert inode['nlink'] == references[inode['inode']], ('link count', inode)
        self.allocation(result)
        return result
