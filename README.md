# nextufs

NeXT/OpenStep UFS tooling and filesystem driver work extracted from a larger
NeXT source tree.

Current components:

- `nextufs/`: shared filesystem library, probe tool, writable FUSE mount, and
  offline mutation tool used for regression tests
- `fsck/`: Linux port of the historical NeXT/BSD `fsck`
- `mkfs/`: Linux port of the historical NeXT/BSD `mkfs`

Current status:

- image probing and FUSE mount are working
- offline mutation operations are working and validated against `fsck_next`
- writable FUSE now covers the current mutation primitives
- writable FUSE defaults to permission-checked operations using the calling
  process credentials
- implemented mutation operations include `rename`, long symlinks,
  truncate/grow/shrink, indirect-block allocation, and special files

Continuation:

- read [CONTINUATION.md](CONTINUATION.md) before resuming development in a new session

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

- the `fsck/` and `mkfs/` ports were derived from historical NeXT/BSD sources
- some UFS compatibility headers and implementation details were derived from
  NeXTMach-era sources and copied locally where needed

Large sample images are kept locally for development and are ignored by git.
