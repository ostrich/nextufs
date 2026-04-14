# nextufs

NeXT/OpenStep-compatible UFS library plus thin probe, FUSE, and offline
mutation frontends.

Current scope:

- open a raw image with `pread`
- report image size
- inspect the first sector as an MBR
- scan for and decode a NeXT `dlV3` disk label
- validate the label block number and v3 checksum field when present
- enumerate decoded NeXT partitions and select a filesystem slice from the label
- scan the early part of the image for candidate UFS/FFS magic values
- decode a candidate superblock
- decode enough filesystem geometry to map inodes across cylinder groups
- list the top-level directory
- resolve a slash-separated path from `/`
- follow symlinks during path lookup
- print the resolved inode metadata
- list resolved directories
- print a preview of resolved regular-file contents
- read regular files through direct and indirect block pointers
- mount the filesystem through FUSE with read/write support for the current
  mutation primitives

Build:

```sh
make -f Makefile.linux
```

This builds:

- `libnextufs.a`: shared NeXT UFS library
- `nextufs_probe`: raw-image inspector/probe
- `nextufs_fuse`: FUSE mount frontend backed by the shared library
- `nextufs_test`: library regression test binary
- `nextufs_mkfile`: offline scratch-image mutation tool for regression tests
- `nextufs.h`: public library API
- `nextufs_internal.h`: internal shared subsystem interfaces

Library layout:

- `nextufs_image.c`: image open/close, disklabel/superblock parsing, inode/data reads
- `nextufs_directory.c`: directory iteration and name lookup
- `nextufs_path.c`: pathname resolution and symlink following
- `nextufs_node.c`: higher-level node/path wrappers used by the frontends
- `nextufs_layout.c`: low-level metadata I/O, on-disk encoding, summary updates
- `nextufs_alloc.c`: inode and data allocation/free helpers
- `nextufs_dir_mutate.c`: directory entry mutation and directory-structure helpers
- `nextufs_mutate.c`: public mutation operations shared by FUSE and `nextufs_mkfile`

Public API highlights in `nextufs.h`:

- `nextufs_probe_info_get`
- `nextufs_image_open` / `nextufs_image_close`
- `nextufs_node_get_root`
- `nextufs_node_get_by_inode`
- `nextufs_node_lookup`
- `nextufs_node_stat`
- `nextufs_node_is_dir` / `nextufs_node_is_reg` / `nextufs_node_is_lnk`
- `nextufs_node_check_access`
- `nextufs_fs_statvfs`
- `nextufs_path_read`
- `nextufs_path_readlink`
- `nextufs_directory_iterate_path`
- `nextufs_directory_iterate_nodes_path`
- `nextufs_path_create_file`, `nextufs_path_rename`, `nextufs_path_truncate`

Regression suite:

```sh
make -f Makefile.linux test
```

This runs:

- `nextufs_test`: library/API checks against the bundled OpenStep 4.2 raw image
- `test_fuse.sh`: read-only FUSE smoke test against the same image

Small-file mutation regression:

```sh
make -f Makefile.linux test-write
```

This creates a scratch copy of `openstep42-base.raw`, writes a small regular
file under `/private/tmp`, verifies readback with `nextufs_probe`, and runs
`fsck_next -n` against the extracted filesystem slice.

Larger mutation regression:

```sh
make -f Makefile.linux test-write-big
```

This creates a fresh scratch image, writes a 3000-byte file under
`/private/tmp`, verifies its size with `nextufs_probe`, reads the full file
back through the FUSE mount with `cmp`, and then runs `fsck_next -n` against
the extracted filesystem slice.

Directory growth mutation regression:

```sh
make -f Makefile.linux test-write-grow
```

This creates many empty files under `/private/tmp` on a fresh scratch image to
force directory growth, verifies through the probe that `/private/tmp` grows to
multiple directory blocks and contains the final file, verifies the final file
through the FUSE mount, and then runs `fsck_next -n` against the extracted
filesystem slice.

Unlink regression:

```sh
make -f Makefile.linux test-unlink
```

This creates a file on a fresh scratch image, unlinks it through the offline
writer, verifies that path lookup now fails in both the probe and FUSE mount,
and then runs `fsck_next -n` against the extracted filesystem slice.

Mkdir regression:

```sh
make -f Makefile.linux test-mkdir
```

This creates a new directory on a fresh scratch image, creates a regular file
inside it, verifies both through the probe and the FUSE mount, and then runs
`fsck_next -n` against the extracted filesystem slice.

Overwrite and append regression:

```sh
make -f Makefile.linux test-rewrite
```

This creates a file on a fresh scratch image, overwrites it in place, appends
more data to the same inode, verifies the final contents through the probe and
the FUSE mount, and then runs `fsck_next -n` against the extracted filesystem
slice.

Link and symlink regression:

```sh
make -f Makefile.linux test-link-symlink
```

This creates a regular file, adds a hard link and a short inline symlink to it,
verifies both through the probe and FUSE mount, and then runs `fsck_next -n`
against the extracted filesystem slice.

Rmdir regression:

```sh
make -f Makefile.linux test-rmdir
```

This creates an empty directory, removes it, verifies that lookup fails through
 both the probe and FUSE mount, and then runs `fsck_next -n`.

Metadata regression:

```sh
make -f Makefile.linux test-meta
```

This creates a file, applies `chmod`, `chown`, and timestamp updates through
the offline writer, verifies the changed metadata through the probe, and then
runs `fsck_next -n`.

Rename regression:

```sh
make -f Makefile.linux test-rename
```

This exercises file and directory rename cases, including replacement of an
existing file and a cross-directory directory move, then validates the result
through both the probe and the FUSE mount before running `fsck_next -n`.

Truncate and indirect-allocation regression:

```sh
make -f Makefile.linux test-truncate
```

This creates a file large enough to require indirect blocks, verifies the
indirect mapping through the probe, shrinks and regrows the file, checks
zero-filled growth and later writes through the FUSE mount, and then runs
`fsck_next -n`.

Special-file regression:

```sh
make -f Makefile.linux test-special
```

This creates FIFO and character-device inodes through the offline writer,
verifies their metadata through the probe and the FUSE mount, and then runs
`fsck_next -n`.

Writable FUSE regression:

```sh
make -f Makefile.linux test-fuse-write
```

This exercises create, append, rename, hard link, symlink, mkdir, truncate,
range write, FIFO creation, unlink, and rmdir through the mounted FUSE
frontend, then validates the resulting image with the probe and `fsck_next -n`.

Permission and ownership regression:

```sh
make -f Makefile.linux test-permissions
```

This exercises permission-mode failures and successes for `chmod`, `chown`,
`utimes`, sticky-directory `unlink`/`rename`, and oversized `uid`/`gid`
rejection, then validates the resulting image with `fsck_next -n`.

Low-space failure-discipline regression:

```sh
make -f Makefile.linux test-failure
```

This creates a small scratch filesystem with `mkfs_next`, forces `ENOSPC`
during file creation and truncate growth, verifies that pre-existing data is
left intact, and then confirms that `fsck_next -n` still reports a clean
filesystem.

Probe the OpenStep sample image:

```sh
./nextufs_probe openstep42-base.raw
```

The probe prefers an explicitly decoded NeXT `dlV3` label when present and
falls back to superblock scanning only if label-based slice detection does not
validate. The current label handling uses NeXTMach-compatible top-level
`disk_label` fields and checksum rules, with the compact big-endian partition
serialization observed in the OpenStep 4.2 sample image.

Resolve a path from `/`:

```sh
./nextufs_probe openstep42-base.raw /etc
./nextufs_probe openstep42-base.raw /usr
./nextufs_probe openstep42-base.raw /etc/passwd
./nextufs_probe openstep42-base.raw mach_kernel
```

Mount the image through FUSE:

```sh
mkdir -p /tmp/nextufs-mnt
./nextufs_fuse openstep42-base.raw /tmp/nextufs-mnt -f -s
```

By default the mounted frontend uses permission-checked semantics based on the
calling process credentials. For image-editor semantics that allow unrestricted
metadata edits by the mounting user, pass:

```sh
./nextufs_fuse openstep42-base.raw /tmp/nextufs-mnt -o nextufs_mode=editor -f -s
```

For offline write testing, `nextufs_mkfile` also accepts leading global
credential options before the operation:

```sh
./nextufs_mkfile --policy permissions --uid 1000 --gid 100 \
  --chmod openstep42-base.raw /private/tmp/example 0600
```

In another shell, you can then inspect the mounted tree:

```sh
ls /tmp/nextufs-mnt
readlink /tmp/nextufs-mnt/etc
head -n 3 /tmp/nextufs-mnt/etc/passwd
```

Current FUSE support includes:

- `access`
- `getattr`
- `readdir`
- `open`
- `read`
- `write`
- `readlink`
- `statfs`
- `create`
- `mknod`
- `unlink`
- `mkdir`
- `rmdir`
- `rename`
- `link`
- `symlink`
- `chmod`
- `chown`
- `truncate`
- `utimens`
- `fsync`

The probe and FUSE frontend both use the shared library rather than carrying
separate filesystem parsers. The FUSE frontend now reports stable inode
numbers, filesystem statistics, and special-file metadata from the shared API.
