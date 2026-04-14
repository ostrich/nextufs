# nextufs

Early raw-image probe tooling for a NeXT/OpenStep-compatible UFS reader.

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
- mount the filesystem read-only through FUSE

Build:

```sh
make -f Makefile.linux
```

This builds:

- `libnextufs.a`: shared NeXT UFS reader library
- `nextufs_probe`: raw-image inspector/probe
- `nextufs_fuse`: read-only FUSE mount frontend
- `nextufs_test`: library regression test binary
- `nextufs_mkfile`: offline scratch-image mutation tool for write regression tests
- `nextufs_read.h`: public reader API
- `nextufs_read_internal.h`: internal read-side interface used across the reader modules
- `nextufs_write.h`: offline write API
- `nextufs_write_internal.h`: internal write-side interface used across the write modules

Library layout:

- `nextufs_read_io.c`: image open/close, disklabel/superblock parsing, inode/data reads
- `nextufs_read_dir.c`: directory iteration and name lookup
- `nextufs_read_path.c`: pathname resolution and symlink following
- `nextufs_read_api.c`: higher-level node/path wrappers used by the frontends

Write-side layout:

- `nextufs_write_io.c`: low-level metadata I/O, on-disk encoding, summary updates
- `nextufs_write_alloc.c`: inode and data allocation/free helpers
- `nextufs_write_dir.c`: directory entry mutation and directory-structure helpers
- `nextufs_write_ops.c`: public offline write operations used by `nextufs_mkfile`

Public API highlights in `nextufs_read.h`:

- `nextufs_get_probe_info`
- `nextufs_open_image` / `nextufs_close_image`
- `nextufs_get_root`
- `nextufs_get_node_by_inode`
- `nextufs_lookup`
- `nextufs_node_stat`
- `nextufs_node_is_dir` / `nextufs_node_is_reg` / `nextufs_node_is_lnk`
- `nextufs_check_access`
- `nextufs_statvfs`
- `nextufs_read_path`
- `nextufs_readlink_path`
- `nextufs_iterate_directory_path`
- `nextufs_iterate_directory_nodes_path`

Regression suite:

```sh
make -f Makefile.linux test
```

This runs:

- `nextufs_test`: library/API checks against the bundled OpenStep 4.2 raw image
- `test_fuse.sh`: read-only FUSE smoke test against the same image

Early offline write test:

```sh
make -f Makefile.linux test-write
```

This creates a scratch copy of `openstep42-base.raw`, writes a small regular
file under `/private/tmp`, verifies readback with `nextufs_probe`, and runs
`fsck_next -n` against the extracted filesystem slice.

Larger offline write regression:

```sh
make -f Makefile.linux test-write-big
```

This creates a fresh scratch image, writes a 3000-byte file under
`/private/tmp`, verifies its size with `nextufs_probe`, reads the full file
back through the FUSE mount with `cmp`, and then runs `fsck_next -n` against
the extracted filesystem slice.

Directory growth write regression:

```sh
make -f Makefile.linux test-write-grow
```

This creates many empty files under `/private/tmp` on a fresh scratch image to
force directory growth, verifies through the probe that `/private/tmp` grows to
multiple directory blocks and contains the final file, verifies the final file
through the FUSE mount, and then runs `fsck_next -n` against the extracted
filesystem slice.

Unlink write regression:

```sh
make -f Makefile.linux test-unlink
```

This creates a file on a fresh scratch image, unlinks it through the offline
writer, verifies that path lookup now fails in both the probe and FUSE mount,
and then runs `fsck_next -n` against the extracted filesystem slice.

Mkdir write regression:

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

Mount the image read-only through FUSE:

```sh
mkdir -p /tmp/nextufs-mnt
./nextufs_fuse openstep42-base.raw /tmp/nextufs-mnt -f -s
```

In another shell, you can then inspect the mounted tree:

```sh
ls /tmp/nextufs-mnt
readlink /tmp/nextufs-mnt/etc
head -n 3 /tmp/nextufs-mnt/etc/passwd
```

Current FUSE support is intentionally minimal:

- `access`
- `getattr`
- `readdir`
- `open`
- `read`
- `readlink`
- `statfs`

The mounted FUSE frontend is still read-only. Write support currently exists
only through the offline `nextufs_mkfile` path, which is exercised by the
write regression targets above. The probe and FUSE frontend both use the shared
reader library rather than carrying separate filesystem parsers. The FUSE
frontend now reports stable inode numbers and filesystem statistics from the
shared API.
