# nextufs

NeXT/OpenStep UFS tooling and filesystem driver work extracted from a larger
NeXT source tree.

Components:

- `nextufs/`: shared filesystem library, probe tool, writable FUSE mount, and
  offline mutation tool used for regression tests
- `fsck.nextufs/`: primary `fsck.nextufs` implementation
- `fsck.legacy/`: preserved historical NeXT/BSD-derived `fsck` source tree
- `mkfs/`: Linux port of the historical NeXT/BSD `mkfs`

Status:

- source probing and FUSE mount are working for raw disk images and VirtualBox
  VDI containers
- source probing supports raw disk images plus standalone/chained VDI
- offline mutation operations are working and validated against `fsck.nextufs`
- writable mutation and FUSE paths now cover raw disk images plus writable VDI
  containers
- writable FUSE defaults to permission-checked operations using the calling
  process credentials
- implemented mutation operations include `rename`, long symlinks,
  truncate/grow/shrink, indirect-block allocation, and special files

Build:

```sh
make
```

That builds all three components.

Run the `nextufs` library regression suite:

```sh
make test
```

Run the current mutation regressions:

```sh
make test-write
make test-write-big
make test-write-grow
make test-unlink
make test-mkdir
make test-rewrite
make test-link-symlink
make test-rmdir
make test-meta
make test-rename
make test-truncate
make test-special
make test-fuse-write
make test-permissions
make test-failure
```

Project structure is subsystem-oriented. Shared library code is split by image
I/O, directory handling, pathname resolution, node APIs, allocation, on-disk
layout updates, directory mutation, and higher-level mutation operations.

Provenance:

- the `fsck.legacy/` and `mkfs/` ports were derived from historical NeXT/BSD sources
- some UFS compatibility headers and implementation details were derived from
  NeXTMach-era sources and copied locally where needed

Large sample images are kept locally for development and are ignored by git.
