# nextufs

`nextufs` is a Linux userspace tool for working with NEXTSTEP and OPENSTEP UFS
filesystems. It provides one command, `nextufs`, with subcommands for inspecting,
browsing, mounting, checking, repairing, creating, resizing, and modifying disk
images.

## Capabilities

- Inspect source layout and filesystem metadata.
- Browse files and directories without mounting.
- Mount images through FUSE.
- Check and repair UFS metadata.
- Create raw UFS filesystems or labeled NeXT disk images.
- Grow supported raw and labeled images offline.
- Apply offline file and directory mutations for testing or recovery work.
- Read and write raw images, standalone VirtualBox VDIs, and VDI chains through
  the shared source backend.
- Read and write standard QCOW2 images and raw/QCOW2 backing chains natively.

## Supported Sources

- Raw UFS filesystem images. The filesystem starts at byte zero.
- Labeled NeXT disk images. These contain a NeXT disk label and a UFS slice.
- Standalone VirtualBox VDI images.
- VirtualBox VDI chains. Point commands at the current chain head, not the
  base image.
- QCOW2 v2/v3 images and external raw/QCOW2 backing chains.

For VDI chains, use `tools/vdi_chain.py` to identify base, intermediate, and
head images:

```sh
tools/vdi_chain.py scan "/path/to/VirtualBox/VMs/OPENSTEP 4.2"
tools/vdi_chain.py trace "/path/to/VM/Snapshots/{uuid}.vdi"
```

## QCOW2 Sources

Use existing image commands directly on a QCOW2 file or its current chain head:

```sh
nextufs info --json /path/to/head.qcow2
nextufs browse --raw /path/to/head.qcow2 /etc/passwd > passwd
nextufs fsck -n /path/to/head.qcow2
nextufs mkfile /path/to/head.qcow2 /example "hello"
```

Use the read-only chain helper to locate heads and inspect backing relationships:

```sh
tools/qcow2_chain.py scan /path/to/images
tools/qcow2_chain.py trace /path/to/head.qcow2
tools/qcow2_chain.py scan --json /path/to/images
```

The helper reads headers directly without QEMU. `scan` finds QCOW2 files by magic
and reports malformed files with QCOW extensions; referenced parents outside the
scanned directory are included. `trace` prints the chain from base to the selected
file. Reports show bases, intermediates, heads, branches, sizes, backing formats,
and header features rejected by nextufs. Roles describe discovered relationships;
files outside the scan may contain additional children.

JSON output contains `images`, `errors`, `scope`, and `validation`. Sizes are
bytes; `physical_size` is the host file length, rather than allocated disk space.
Missing parents,
cycles, malformed headers, and chains exceeding 32 files produce status 1 with
partial results. Unsupported header features are informational notes. This helper
does not check mapping tables, compressed clusters, refcounts, or UFS integrity.

QCOW2 support is native and works in both FUSE and offline builds. QEMU is needed
only to run the interoperability tests. Supported images use QCOW2 v2/v3,
standard L2 tables, 512-byte through 2 MiB clusters, and 16-bit refcounts.
Sparse, preallocated, zero-cluster, and external backing-chain images are
supported. Backing files may be raw or QCOW2; relative names resolve from the
child image's directory. Declared backing formats are honored, and otherwise
the format is detected from contents. Chain depth is limited to 32 files.

Writes affect only the selected head; parents remain read-only. Use images that
are not in use by a running VM or another writer. Work on a copy when repairing
or mutating an image that matters. New allocations append to the container;
freed host clusters are not reused or compacted.

Compressed clusters, encryption, internal snapshots, extended L2 tables,
external data files, lazy refcounts, dirty/corrupt images, and unsupported
feature flags are rejected. QCOW2 metadata repair is outside nextufs: UFS fsck
repairs the guest filesystem only. Interrupted v3 updates remain marked dirty;
container recovery requires a QCOW2-aware tool. Version 2 has no dirty flag, so
check its container after interrupted writes. Mutations are not transactional.

Metadata processing is bounded: L1 tables may occupy at most 32 MiB, refcount
tables at most 8 MiB, and the physical file may cover at most 16,777,216 clusters.
Unsupported or inconsistent images fail before mutation. `info` reports
`source_format` (`raw`, `vdi`, or `qcow2`) separately from `source_kind`, and
separates the head's physical file size from its virtual disk size.

QCOW2 creation and resize are not supported. Use QEMU to create/convert a
container; `mkimg` continues to create raw or labeled images, and `resize grow`
rejects container sources.

## Build And Install

Build the command:

```sh
make
```

Build without FUSE for offline image operations:

```sh
make WITH_FUSE=0
make WITH_FUSE=0 test
```

The default `WITH_FUSE=1` build includes mounting. Offline builds keep all image
commands, including fsck, but `mount` returns status 2. Switching modes rebuilds
the selected libraries and command; no clean step is required. FUSE-specific test
targets require `WITH_FUSE=1`.

Run the main test suite:

```sh
make test
```

Install the unified `nextufs` command:

```sh
make install
```

Override the install prefix or stage into a package root:

```sh
make prefix="$HOME/.local" install
DESTDIR=/tmp/pkgroot make install
```

## Usage

The full command reference is in [man/nextufs.1](man/nextufs.1).

Show source and filesystem information:

```sh
nextufs info /path/to/source
nextufs info --json /path/to/source
```

Browse the filesystem without mounting:

```sh
nextufs browse /path/to/source
nextufs browse /path/to/source /etc/passwd
nextufs browse --json /path/to/source /etc
nextufs browse --raw /path/to/source /etc/passwd > passwd
```

Mount through FUSE. Mounts are read-only by default:

```sh
mkdir -p /tmp/nextufs-mnt
nextufs mount /path/to/source /tmp/nextufs-mnt
```

Request a writable mount explicitly:

```sh
nextufs mount /path/to/source /tmp/nextufs-mnt -o rw,mode=su
```

Check or repair a filesystem:

```sh
nextufs fsck -n /path/to/source
nextufs fsck -y /path/to/source
```

Create a labeled NeXT disk image:

```sh
nextufs mkimg /tmp/nextufs.img 256M
```

Create a raw UFS filesystem image:

```sh
nextufs mkimg --raw /tmp/nextufs.raw 256M
```

For `mkimg`, bare numeric sizes are interpreted as 1 KiB sectors in both
labeled and raw modes. Suffixes such as `K`, `M`, and `G` specify byte-based
KiB, MiB, and GiB quantities.

Refuse-to-overwrite is the default. Use `--force-overwrite` only when replacing
an existing target is intentional:

```sh
nextufs mkimg --force-overwrite /tmp/nextufs.img 256M
```

Grow a supported image offline, including partial cylinder groups. Targets must
leave room for any new cylinder group metadata. Bare numbers are interpreted as
1 KiB sectors:

```sh
nextufs resize grow /path/to/source 2097152
```

By default, `mkimg` and `resize grow` enforce the NEXTSTEP/OPENSTEP
compatibility ceiling of `4294836224` bytes, or `4194176` 1 KiB sectors.
Use `--force-size` only when intentionally creating or growing beyond that
limit.

Apply an offline mutation:

```sh
nextufs mkfile /path/to/source /private/tmp/example "hello from Linux"
nextufs mkfile --policy user --uid 1000 --gid 100 \
  --chmod /path/to/source /private/tmp/example 0600
```

## Safety Notes

- `info`, `browse`, and `fsck -n` are intended to be non-mutating.
- `fsck -y`, writable FUSE mounts, `mkfile`, and `resize grow` modify the
  target source.
- Work on a copy when repairing, resizing, or mutating an image that matters.
- For VDI chains, writes go through the chain head. Passing an older snapshot or
  base image is not equivalent to modifying the current VM state.
- `mkimg` refuses to overwrite existing files unless `--force-overwrite` is
  supplied.
- `mkimg` and `resize grow` enforce the NEXTSTEP/OPENSTEP compatibility ceiling
  of `4294836224` bytes, or `4194176` 1 KiB sectors, unless `--force-size` is
  supplied.

## Testing

The test suite covers the unified CLI, image creation, resize command contracts,
FUSE reads, offline mutations, VDI-aware source handling, and a reproducible
fsck corruption corpus. The fsck repair corpus exercises many detected metadata
corruption families and verifies repaired images with a follow-up check.

Useful entry points:

```sh
make test
make test-nextufs
make test-fsck
make test-qcow2
make test-qcow2-chain
make repair-repair-all
```

`test-qcow2` compares native reads/writes with `qemu-img` and `qemu-io`, checks
container refcounts, exercises backing chains and failure injection, and runs
FUSE stress in the default build. It explicitly skips when QEMU test tools are
unavailable; no QEMU programs are required at runtime.

Additional fsck corpus notes are in [tests/fsck/README.md](tests/fsck/README.md).

## Layout

- `src/commands/`: unified CLI subcommands.
- `src/core/`: shared image, label, source, size, inspection, and reporting code.
- `src/mutate/`: offline mutation primitives.
- `src/fsck/`: checker and repair implementation.
- `src/mkimg_format/`: raw UFS formatter implementation.
- `include/`: public and internal C headers.
- `tools/`: support utilities such as `vdi_chain.py`.
- `tests/`: command tests and fsck corruption fixtures.

## License

nextufs is released under the MIT License. See [LICENSE](LICENSE).
