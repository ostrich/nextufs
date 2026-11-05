#!/usr/bin/env python3
"""Read-only QCOW2 header and backing-chain discovery; no QEMU dependency.

This is not a QCOW2 integrity checker. In particular, standard compressed
clusters and corrupt mapping/refcount tables cannot be detected from headers.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import json
import os
from pathlib import Path
import stat
import struct
import sys

MAGIC = b"QFI\xfb"
MAX_DEPTH = 32
BACKING_FORMAT = 0xE2792ACA
LIMITATION = "Header inspection only; cluster mappings, compression, and refcounts are not checked."


@dataclass
class Image:
    path: str
    format: str = "unknown"
    physical_size: int | None = None
    virtual_size: int | None = None
    version: int | None = None
    cluster_size: int | None = None
    backing_file: str | None = None
    backing_path: str | None = None
    backing_format: str | None = None
    incompatible_features: int = 0
    compatible_features: int = 0
    autoclear_features: int = 0
    nextufs_header_issues: list[str] = field(default_factory=list)
    errors: list[str] = field(default_factory=list)
    identity: tuple[int, int] | None = None
    parent: tuple[str, str] | None = None

    @property
    def key(self):
        return self.path, self.format


def absolute(path: Path) -> str:
    # Preserve the filename's directory for relative backing paths, including
    # symlink spellings. Inode identity is used separately to detect cycles.
    return os.path.abspath(path)


def inspect(path: Path, declared: str | None = None) -> Image:
    image = Image(absolute(path))
    try:
        with os.fdopen(os.open(image.path, os.O_RDONLY | os.O_NONBLOCK), "rb") as stream:
            st = os.fstat(stream.fileno())
            if not stat.S_ISREG(st.st_mode):
                raise ValueError("not a regular file")
            image.identity = st.st_dev, st.st_ino
            image.physical_size = st.st_size
            if declared == "raw":
                image.format = "raw"
                image.virtual_size = st.st_size
                return image
            prefix = stream.read(104)
            if declared not in (None, "qcow2"):
                image.format = declared
                image.errors.append(f"unsupported backing format: {declared}")
                return image
            if prefix[:4] != MAGIC:
                if declared == "qcow2":
                    raise ValueError("declared QCOW2 file has no QCOW2 magic")
                if prefix[64:68] == b"\x7f\x10\xda\xbe":
                    image.format = "vdi"
                    image.errors.append("VDI backing files are not supported in QCOW2 chains")
                    return image
                image.format = "raw"
                image.virtual_size = st.st_size
                return image
            image.format = "qcow2"
            if len(prefix) < 8:
                raise ValueError("truncated QCOW2 header")
            image.version = struct.unpack_from(">I", prefix, 4)[0]
            if image.version not in (2, 3):
                image.nextufs_header_issues.append(f"unsupported QCOW version: {image.version}")
                return image
            minimum = 72 if image.version == 2 else 104
            if len(prefix) < minimum:
                raise ValueError("truncated QCOW2 header")
            backing_offset, backing_length, bits = struct.unpack_from(">QII", prefix, 8)
            image.virtual_size = struct.unpack_from(">Q", prefix, 24)[0]
            encryption = struct.unpack_from(">I", prefix, 32)[0]
            snapshots = struct.unpack_from(">I", prefix, 60)[0]
            if encryption:
                image.nextufs_header_issues.append(f"encryption method {encryption}")
            if snapshots:
                image.nextufs_header_issues.append(f"{snapshots} internal snapshot(s)")
            header_length = minimum
            if image.version == 3:
                incompatible, compatible, autoclear, order, header_length = struct.unpack_from(">QQQII", prefix, 72)
                image.incompatible_features = incompatible
                image.compatible_features = compatible
                image.autoclear_features = autoclear
                for bit, name in enumerate(("dirty image", "corrupt image", "external data file",
                                            "non-default compression", "extended L2 tables")):
                    if incompatible & (1 << bit):
                        image.nextufs_header_issues.append(name)
                if incompatible & ~31:
                    image.nextufs_header_issues.append(f"unknown incompatible features: 0x{incompatible & ~31:x}")
                if compatible & 1:
                    image.nextufs_header_issues.append("lazy refcounts")
                if compatible & ~1:
                    image.nextufs_header_issues.append(f"unsupported compatible features: 0x{compatible & ~1:x}")
                if autoclear:
                    image.nextufs_header_issues.append(f"unsupported autoclear features: 0x{autoclear:x}")
                if order != 4:
                    image.nextufs_header_issues.append(f"unsupported refcount order: {order} (expected 4)")
            if not 9 <= bits <= 21:
                raise ValueError(f"unsupported cluster size exponent: {bits}")
            image.cluster_size = 1 << bits
            if not minimum <= header_length <= image.cluster_size:
                raise ValueError("invalid header length")
            if backing_offset and (not 1 <= backing_length <= 1023 or
                                   backing_offset < header_length or
                                   backing_offset + backing_length > image.cluster_size):
                raise ValueError("invalid backing filename bounds")
            stream.seek(0)
            header = stream.read(image.cluster_size)
            if len(header) < image.cluster_size:
                raise ValueError("truncated first cluster")
            end = backing_offset or image.cluster_size
            offset = header_length
            while offset + 8 <= end:
                kind, length = struct.unpack_from(">II", header, offset)
                if kind == 0:
                    break
                next_offset = offset + 8 + ((length + 7) & ~7)
                if next_offset > end:
                    raise ValueError("header extension exceeds its bounds")
                value = header[offset + 8:offset + 8 + length]
                if kind == BACKING_FORMAT:
                    if image.backing_format is not None:
                        raise ValueError("duplicate backing-format extension")
                    if not value or b"\0" in value:
                        raise ValueError("invalid backing format")
                    image.backing_format = value.decode("ascii")
                    if image.backing_format not in ("raw", "qcow2"):
                        image.nextufs_header_issues.append(f"unsupported backing format: {image.backing_format}")
                if kind in (0x23852875, 0x0537BE77, 0x44415441):
                    image.nextufs_header_issues.append({
                        0x23852875: "persistent bitmaps", 0x0537BE77: "encryption header",
                        0x44415441: "external data file header",
                    }[kind])
                offset = next_offset
            if backing_offset:
                name = header[backing_offset:backing_offset + backing_length]
                if b"\0" in name:
                    raise ValueError("backing filename contains a NUL byte")
                image.backing_file = os.fsdecode(name)
                image.backing_path = absolute(Path(image.path).parent / image.backing_file)
    except (OSError, ValueError, struct.error) as error:
        image.errors.append(str(error))
    return image


def trace(path: Path) -> list[Image]:
    chain = []
    seen = {}
    start = absolute(path)
    declared = "qcow2"
    while True:
        image = inspect(path, declared)
        if chain:
            chain[-1].parent = image.key
        if image.identity is not None and image.identity in seen:
            chain[-1].errors.append(f"backing cycle reaches {image.path}")
            chain[-1].parent = seen[image.identity]
            break
        chain.append(image)
        if image.identity is not None:
            seen[image.identity] = image.key
        if image.errors or not image.backing_path:
            break
        if len(chain) == MAX_DEPTH:
            image.errors.append(f"backing chain from {start} exceeds {MAX_DEPTH} files")
            break
        path, declared = Path(image.backing_path), image.backing_format
    return list(reversed(chain))


def discover(root: Path) -> tuple[list[Image], list[str]]:
    images = {}
    errors = []

    def walk_error(error):
        errors.append(str(error))

    for directory, dirs, names in os.walk(root, onerror=walk_error, followlinks=False):
        dirs.sort()
        for name in sorted(names):
            path = Path(directory) / name
            try:
                # Ignore devices/FIFOs before opening and recognize images by
                # magic even when their filenames use another extension.
                if not stat.S_ISREG(path.stat().st_mode):
                    continue
                with os.fdopen(os.open(path, os.O_RDONLY | os.O_NONBLOCK), "rb") as stream:
                    if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
                        continue
                    magic = stream.read(4)
            except OSError as error:
                errors.append(f"{path}: {error}")
                continue
            if magic != MAGIC and path.suffix.lower() not in (".qcow2", ".qcow", ".qc2"):
                continue
            for image in trace(path):
                previous = images.get(image.key)
                if previous:
                    previous.errors = sorted(set(previous.errors + image.errors))
                    if image.parent is not None:
                        previous.parent = image.parent
                else:
                    images[image.key] = image
    return sorted(images.values(), key=lambda item: item.key), errors


def records(images: list[Image], selected: str | None) -> list[dict]:
    children = {image.key: set() for image in images}
    by_key = {image.key: image for image in images}
    for image in images:
        if image.parent in children:
            children[image.parent].add(image.path)
    result = []
    for image in images:
        kids = sorted(children[image.key])
        parent = by_key.get(image.parent)
        if image.backing_path and parent is not None and parent.errors and parent.identity is None:
            role = "orphan"
        elif image.backing_path:
            role = "intermediate" if kids else "head"
        else:
            role = "base" if kids else "standalone"
        result.append({
            "path": image.path, "format": image.format, "role": role,
            "children": kids, "branch": len(kids) > 1,
            "selected": image.path == selected,
            "version": image.version, "virtual_size": image.virtual_size,
            "physical_size": image.physical_size, "cluster_size": image.cluster_size,
            "backing_file": image.backing_file, "backing_path": image.backing_path,
            "backing_format": image.backing_format,
            "incompatible_features": image.incompatible_features,
            "compatible_features": image.compatible_features,
            "autoclear_features": image.autoclear_features,
            "nextufs_header_issues": image.nextufs_header_issues,
            "errors": image.errors,
        })
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("scan", "trace"):
        sub = commands.add_parser(command, help=("discover QCOW2 chains in a directory" if command == "scan"
                                                else "follow backing files from base to selected QCOW2"))
        sub.add_argument("path", type=Path)
        sub.add_argument("--json", action="store_true", help="emit structured JSON, including diagnostics")
    args = parser.parse_args(argv)
    errors = []
    if args.command == "scan":
        if not args.path.is_dir():
            parser.error(f"not a directory: {args.path}")
        images, errors = discover(args.path)
    else:
        images = trace(args.path)
    output = records(images, absolute(args.path) if args.command == "trace" else None)
    for image in images:
        errors.extend(f"{image.path}: {message}" for message in image.errors)
    report = {"images": output, "errors": sorted(set(errors)),
              "scope": "Roles and branches reflect only relationships discovered by this command.",
              "validation": LIMITATION}
    if args.json:
        print(json.dumps(report, indent=2, ensure_ascii=True))
    else:
        for image in output:
            marker = " [selected]" if image["selected"] else ""
            branch = " [branch]" if image["branch"] else ""
            print(f'{image["role"]}: {image["path"]} ({image["format"]}){marker}{branch}')
            sizes = f'  virtual_bytes={image["virtual_size"]} file_bytes={image["physical_size"]}'
            if image["version"] is not None:
                sizes += f' version={image["version"]} cluster_bytes={image["cluster_size"]}'
            print(sizes)
            if image["backing_path"]:
                print(f'  backing: {image["backing_file"]} -> {image["backing_path"]} '
                      f'(declared format: {image["backing_format"] or "auto"})')
            for note in image["nextufs_header_issues"]:
                print(f'  nextufs header limitation: {note}')
        for error in report["errors"]:
            print(f"error: {error}", file=sys.stderr)
        print(report["scope"])
        print(LIMITATION)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
