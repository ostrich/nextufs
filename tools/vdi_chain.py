#!/usr/bin/env python3
"""Inspect VirtualBox VDI images and report parent/child chains.

This utility reads VDI headers directly. It does not modify any files.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional


VDI_SIGNATURE = 0xBEDA107F
VDI_VERSION = 0x00010001
VDI_TYPE_DYNAMIC = 1
VDI_TYPE_DIFF = 4

VDI_SIG_OFF = 0x40
VDI_VERSION_OFF = 0x44
VDI_HEADER_SIZE_OFF = 0x48
VDI_TYPE_OFF = 0x4C
VDI_BLOCKS_OFFSET_OFF = 0x154
VDI_DATA_OFFSET_OFF = 0x158
VDI_SECTOR_SIZE_OFF = 0x168
VDI_DISK_SIZE_OFF = 0x170
VDI_BLOCK_SIZE_OFF = 0x178
VDI_BLOCK_EXTRA_OFF = 0x17C
VDI_BLOCK_COUNT_OFF = 0x180
VDI_BLOCKS_ALLOC_OFF = 0x184
VDI_UUID_IMAGE_OFF = 0x188
VDI_UUID_PARENT_OFF = 0x1A8
VDI_MIN_HEADER_READ = VDI_UUID_PARENT_OFF + 16


def read_le32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def read_le64(buf: bytes, off: int) -> int:
    return struct.unpack_from("<Q", buf, off)[0]


def format_uuid(raw: bytes) -> str:
    if len(raw) != 16:
        raise ValueError("UUID must be 16 bytes")
    d1, d2, d3 = struct.unpack_from("<IHH", raw, 0)
    d4 = raw[8:]
    return (
        f"{d1:08x}-{d2:04x}-{d3:04x}-"
        f"{d4[0]:02x}{d4[1]:02x}-"
        f"{d4[2]:02x}{d4[3]:02x}{d4[4]:02x}{d4[5]:02x}{d4[6]:02x}{d4[7]:02x}"
    )


def is_zero_uuid(raw: bytes) -> bool:
    return raw == b"\x00" * 16


@dataclass
class VDIInfo:
    path: Path
    image_uuid: str
    parent_uuid: Optional[str]
    image_type: int
    disk_size: int
    block_size: int
    block_count: int
    blocks_allocated: int
    source_size: int

    @property
    def type_name(self) -> str:
        if self.image_type == VDI_TYPE_DYNAMIC:
            return "dynamic"
        if self.image_type == VDI_TYPE_DIFF:
            return "differencing"
        return f"type-{self.image_type}"


def parse_vdi(path: Path) -> Optional[VDIInfo]:
    try:
        with path.open("rb") as f:
            buf = f.read(VDI_MIN_HEADER_READ)
    except OSError:
        return None

    if len(buf) < VDI_MIN_HEADER_READ:
        return None
    if read_le32(buf, VDI_SIG_OFF) != VDI_SIGNATURE:
        return None
    if read_le32(buf, VDI_VERSION_OFF) != VDI_VERSION:
        return None
    header_size = read_le32(buf, VDI_HEADER_SIZE_OFF)
    if VDI_SIG_OFF + header_size < VDI_MIN_HEADER_READ:
        return None

    image_uuid_raw = buf[VDI_UUID_IMAGE_OFF:VDI_UUID_IMAGE_OFF + 16]
    parent_uuid_raw = buf[VDI_UUID_PARENT_OFF:VDI_UUID_PARENT_OFF + 16]

    try:
        stat = path.stat()
    except OSError:
        return None

    return VDIInfo(
        path=path,
        image_uuid=format_uuid(image_uuid_raw),
        parent_uuid=None if is_zero_uuid(parent_uuid_raw) else format_uuid(parent_uuid_raw),
        image_type=read_le32(buf, VDI_TYPE_OFF),
        disk_size=read_le64(buf, VDI_DISK_SIZE_OFF),
        block_size=read_le32(buf, VDI_BLOCK_SIZE_OFF),
        block_count=read_le32(buf, VDI_BLOCK_COUNT_OFF),
        blocks_allocated=read_le32(buf, VDI_BLOCKS_ALLOC_OFF),
        source_size=stat.st_size,
    )


def scan_tree(root: Path) -> List[VDIInfo]:
    infos: List[VDIInfo] = []
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            if not name.lower().endswith(".vdi"):
                continue
            path = Path(dirpath) / name
            info = parse_vdi(path)
            if info is not None:
                infos.append(info)
    infos.sort(key=lambda item: str(item.path))
    return infos


def looks_like_vm_dir(root: Path) -> bool:
    try:
        entries = list(root.iterdir())
    except OSError:
        return False

    for entry in entries:
        if entry.is_file() and entry.name.lower().endswith(".vdi"):
            return True
    return any(entry.is_dir() and entry.name == "Snapshots" for entry in entries)


def build_uuid_index(infos: Iterable[VDIInfo]) -> Dict[str, VDIInfo]:
    index: Dict[str, VDIInfo] = {}
    for info in infos:
        index[info.image_uuid] = info
    return index


def build_children(infos: Iterable[VDIInfo]) -> Dict[str, List[VDIInfo]]:
    children: Dict[str, List[VDIInfo]] = {}
    for info in infos:
        if info.parent_uuid is None:
            continue
        children.setdefault(info.parent_uuid, []).append(info)
    for items in children.values():
        items.sort(key=lambda item: str(item.path))
    return children


def classify(info: VDIInfo, uuid_index: Dict[str, VDIInfo], children: Dict[str, List[VDIInfo]]) -> str:
    has_parent = info.parent_uuid is not None
    parent_present = has_parent and info.parent_uuid in uuid_index
    has_children = bool(children.get(info.image_uuid))
    if has_parent and not parent_present:
        return "orphan"
    if not has_parent and has_children:
        return "base"
    if not has_parent and not has_children:
        return "standalone"
    if has_parent and has_children:
        return "intermediate"
    return "head"


def chain_for(info: VDIInfo, uuid_index: Dict[str, VDIInfo]) -> List[VDIInfo]:
    chain = [info]
    seen = {info.image_uuid}
    cur = info
    while cur.parent_uuid is not None and cur.parent_uuid in uuid_index:
        cur = uuid_index[cur.parent_uuid]
        if cur.image_uuid in seen:
            break
        seen.add(cur.image_uuid)
        chain.append(cur)
    chain.reverse()
    return chain


def display_path(path: Path, root: Optional[Path]) -> str:
    if root is not None:
        try:
            rel = path.resolve().relative_to(root.resolve())
            return "." if not rel.parts else str(rel)
        except ValueError:
            pass
    return str(path)


def trace_from_path(path: Path) -> List[VDIInfo]:
    target = parse_vdi(path)
    if target is None:
        raise SystemExit(f"not a supported VDI file: {path}")
    infos = scan_tree(path.parent.parent if path.parent.name == "Snapshots" else path.parent)
    if not any(info.path == path for info in infos):
        infos.append(target)
    uuid_index = build_uuid_index(infos)
    return chain_for(target, uuid_index)


def summarize_scan(infos: List[VDIInfo]) -> List[dict]:
    uuid_index = build_uuid_index(infos)
    children = build_children(infos)
    out = []
    for info in infos:
        out.append(
            {
                "path": str(info.path),
                "uuid": info.image_uuid,
                "parent_uuid": info.parent_uuid,
                "type": info.type_name,
                "role": classify(info, uuid_index, children),
                "disk_size": info.disk_size,
                "block_size": info.block_size,
                "block_count": info.block_count,
                "blocks_allocated": info.blocks_allocated,
                "source_size": info.source_size,
                "children": [child.image_uuid for child in children.get(info.image_uuid, [])],
            }
        )
    return out


def print_chain(
    chain: List[VDIInfo],
    target: Optional[Path] = None,
    display_root: Optional[Path] = None,
) -> None:
    target = target.resolve() if target is not None else None
    for idx, info in enumerate(chain):
        prefix = "  " * idx
        label = "Base" if idx == 0 else ("Head" if idx == len(chain) - 1 else "Child")
        marker = ""
        if target is not None and info.path.resolve() == target:
            marker = "  [traced file]"
        print(f"{prefix}{label}: {info.path.name}{marker}")
        print(f"{prefix}  Path: {display_path(info.path, display_root)}")
        print(f"{prefix}  UUID: {info.image_uuid}")
        if info.parent_uuid is not None:
            print(f"{prefix}  Parent UUID: {info.parent_uuid}")
        print(f"{prefix}  Type: {info.type_name}")
        print(f"{prefix}  Disk Size: {info.disk_size}")


def print_scan(infos: List[VDIInfo], display_root: Optional[Path] = None) -> None:
    uuid_index = build_uuid_index(infos)
    children = build_children(infos)

    roots: List[VDIInfo] = []
    orphans: List[VDIInfo] = []
    for info in infos:
        role = classify(info, uuid_index, children)
        if role in {"base", "standalone"}:
            roots.append(info)
        elif role == "orphan":
            orphans.append(info)

    seen: set[str] = set()

    def walk(info: VDIInfo, depth: int) -> None:
        seen.add(info.image_uuid)
        role = classify(info, uuid_index, children)
        prefix = "  " * depth
        print(f"{prefix}{role.title()}: {info.path.name}")
        print(f"{prefix}  Path: {display_path(info.path, display_root)}")
        print(f"{prefix}  UUID: {info.image_uuid}")
        if info.parent_uuid is not None:
            print(f"{prefix}  Parent UUID: {info.parent_uuid}")
        print(f"{prefix}  Type: {info.type_name}")
        for child in children.get(info.image_uuid, []):
            walk(child, depth + 1)

    for info in sorted(roots, key=lambda item: str(item.path)):
        walk(info, 0)
        print()

    for info in sorted(orphans, key=lambda item: str(item.path)):
        if info.image_uuid in seen:
            continue
        walk(info, 0)
        print()

    leftovers = [info for info in infos if info.image_uuid not in seen]
    for info in leftovers:
        walk(info, 0)
        print()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Inspect VirtualBox VDI images and report parent/child chains.",
        epilog=(
            "Examples:\n"
            "  vdi_chain.py scan \"/path/to/VirtualBox/VMs/OPENSTEP 4.2\"\n"
            "  vdi_chain.py trace \"/path/to/VM/Snapshots/{uuid}.vdi\"\n"
            "  vdi_chain.py scan --json /path/to/vm-dir\n\n"
            "Use 'scan' to map all base and snapshot relationships under a VM directory.\n"
            "Use 'trace' to print one VDI's full parent chain from base to the selected file."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    scan = sub.add_parser(
        "scan",
        help="scan a directory tree for VDI chains",
        description=(
            "Scan a directory tree for .vdi files, group them into parent/child chains, "
            "and identify bases, heads, intermediates, standalones, and orphans."
        ),
    )
    scan.add_argument("path", type=Path, help="directory tree to scan")
    scan.add_argument("--json", action="store_true", help="emit JSON")
    scan.add_argument(
        "--force",
        action="store_true",
        help="scan even if the target does not look like a VM directory",
    )

    trace = sub.add_parser(
        "trace",
        help="trace the chain for one VDI file",
        description=(
            "Trace one VDI back through its parents and print the full chain from base "
            "to the selected file."
        ),
    )
    trace.add_argument("path", type=Path, help="VDI file to trace")
    trace.add_argument("--json", action="store_true", help="emit JSON")
    return parser


def main(argv: List[str]) -> int:
    parser = build_parser()
    if not argv:
        parser.print_help(sys.stderr)
        return 2

    args = parser.parse_args(argv)
    if args.cmd == "scan":
        if not args.path.is_dir():
            raise SystemExit(f"not a directory: {args.path}")
        if not args.force and not looks_like_vm_dir(args.path):
            raise SystemExit(
                "scan target does not look like a VM directory "
                "(expected a top-level .vdi or a Snapshots/ subdirectory); "
                "use --force to scan anyway"
            )
        infos = scan_tree(args.path)
        if args.json:
            json.dump(summarize_scan(infos), sys.stdout, indent=2)
            sys.stdout.write("\n")
        else:
            print_scan(infos, display_root=args.path)
        return 0

    chain = trace_from_path(args.path)
    display_root = args.path.parent.parent if args.path.parent.name == "Snapshots" else args.path.parent
    if args.json:
        json.dump(
            [
                {
                    "path": str(info.path),
                    "display_path": display_path(info.path, display_root),
                    "uuid": info.image_uuid,
                    "parent_uuid": info.parent_uuid,
                    "type": info.type_name,
                    "disk_size": info.disk_size,
                    "block_size": info.block_size,
                    "block_count": info.block_count,
                    "blocks_allocated": info.blocks_allocated,
                    "source_size": info.source_size,
                    "traced": info.path.resolve() == args.path.resolve(),
                }
                for info in chain
            ],
            sys.stdout,
            indent=2,
        )
        sys.stdout.write("\n")
    else:
        print_chain(chain, target=args.path, display_root=display_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
