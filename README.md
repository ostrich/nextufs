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

For QCOW2 chains, use `tools/qcow2_chain.py` to inspect backing relationships:

```sh
tools/qcow2_chain.py trace /path/to/head.qcow2
```

Point normal commands at the current chain head; writes leave parents unchanged.
See the [QCOW2 guide](docs/qcow2.md) for supported features and limitations.

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
commands, including fsck, but `mount` returns status 2. Switching modes selects
the corresponding libraries and command; no clean step is required. Build artifacts are
separated by mode, and the root `nextufs` symlink selects the last built mode. FUSE-specific test
targets require `WITH_FUSE=1`.

The compiler records header dependencies automatically. Changing compiler or
linker flags invalidates the corresponding build configuration; no clean step is
required. `nextufs --version` includes the source version and Git revision (with
`dirty` for tracked local edits); exported source builds use `+source`.

Run the generated-fixture regression suite:

```sh
make test
```

It does not require an OPENSTEP installation image, mounting, or QEMU. Use
`make test-all` for the full generated-fixture suite including QEMU and FUSE.
See [tests/README.md](tests/README.md) for groups, individual cases, prerequisites,
retained failure artifacts, sanitizer/coverage commands and supplied-image checks.

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

JSON names are valid UTF-8 display strings. `name_bytes_hex` preserves the exact
filename bytes, including names that are not UTF-8.

Mount through FUSE. Mounts are read-only by default:

```sh
mkdir -p /tmp/nextufs-mnt
nextufs mount /path/to/source /tmp/nextufs-mnt
```

Mount operations are serialized. Writable mounts retain an exclusive advisory
lock on the image; conflicting nextufs commands fail while it is mounted.

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

See [image ownership and write failures](docs/writes.md) for operation rollback,
session ownership and the limits of crash recovery.

## Testing

The test suite covers the unified CLI, image creation, resize command contracts,
FUSE reads, offline mutations, VDI-aware source handling, and a reproducible
fsck corruption corpus. The fsck repair corpus exercises many detected metadata
corruption families and verifies repaired images with a follow-up check.

Useful entry points:

```sh
make test
make test-fsck
make test-qcow2
make test-vdi
make test-fuse
make test-stress
make test-all
make test-sanitize
```

The [test guide](tests/README.md) defines these groups and their prerequisites.
The [QCOW2](tests/qcow2/README.md) and [VDI](tests/vdi/README.md) coverage
guides map tests to supported behavior and record the remaining limits. The optional fsck reference-comparison
workflow is documented in [tests/fsck/README.md](tests/fsck/README.md).

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
