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

## Supported Sources

| Source type | Read | Write | Notes |
| --- | --- | --- | --- |
| Raw UFS filesystem image | yes | yes | Filesystem starts at byte zero. |
| Labeled NeXT disk image | yes | yes | Contains a NeXT disk label and a UFS slice. |
| Standalone VirtualBox VDI | yes | yes | Dynamic VDIs are handled through the VDI backend. |
| VirtualBox VDI chain | yes | yes | Point commands at the current chain head, not the base image. |

For VDI chains, use `tools/vdi_chain.py` to identify base, intermediate, and
head images:

```sh
tools/vdi_chain.py scan "/path/to/VirtualBox/VMs/OPENSTEP 4.2"
tools/vdi_chain.py trace "/path/to/VM/Snapshots/{uuid}.vdi"
```

## Build And Install

Build the command:

```sh
make
```

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

Grow a supported image offline. Bare numbers are interpreted as 1 KiB sectors:

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
make repair-repair-all
```

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
