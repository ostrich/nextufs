# nextufs

NeXT/OpenStep UFS tooling and filesystem driver work extracted from a larger
NeXT source tree.

Current components:

- `nextufs/`: core read/write library work, probe tool, writable FUSE mount,
  and offline mutation tool used for write regression tests
- `fsck/`: Linux port of the historical NeXT/BSD `fsck`
- `mkfs/`: Linux port of the historical NeXT/BSD `mkfs`

Current status:

- image probing and FUSE mount are working
- offline write operations are working and validated against `fsck_next`
- writable FUSE now covers the current write primitives
- writable FUSE defaults to permission-checked operations using the calling
  process credentials
- implemented write-side operations include `rename`, long symlinks,
  truncate/grow/shrink, indirect-block allocation, and special files

Continuation:

- read [CONTINUATION.md](CONTINUATION.md) before resuming development in a new session

Build:

```sh
make
```

That builds all three components.

Run the `nextufs` read-side regression suite:

```sh
make test
```

Run the current offline write regressions:

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

Project structure is still transitional. The code is currently split into
`nextufs_read_*` and `nextufs_write_*` modules; the intended longer-term
refactor is a subsystem-oriented split.

Provenance:

- the `fsck/` and `mkfs/` ports were derived from historical NeXT/BSD sources
- some UFS compatibility headers and implementation details were derived from
  NeXTMach-era sources and copied locally where needed

Large sample images are kept locally for development and are ignored by git.
