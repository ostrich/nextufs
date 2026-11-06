# Image ownership and write failures

Writable image opens hold a nonblocking exclusive `flock` on the head file.
Read-only opens and backing files hold shared locks. A conflicting cooperating
process gets an error instead of observing or changing an in-progress operation.
These are advisory locks: software that does not use the same locking convention,
including a VM, must still be stopped before modifying its images. Locks follow
file identity, so opening a hardlink does not bypass ownership.

A writable FUSE mount keeps one `nextufs_write_session` open until unmount and
serializes callbacks, including when clients issue concurrent requests. Offline
mutation calls use a one-operation session unless the caller supplies
`nextufs_write_ctx.session`. Initialize the entire context to zero before setting
credentials. The public session API is serialized, not thread-safe: its owner
must serialize calls. Replacing the source path with a different file while the
session is open causes mutations to fail with `ESTALE`.

## Returned failures

Each offline or mounted file mutation records the previous bytes before writing
them. The operation-local undo log uses an anonymous temporary file, keeping RAM
usage independent of file size. It requires temporary disk space proportional to
the bytes written, including repeated metadata writes. A failed operation restores
records in reverse order and syncs the image. This covers allocation maps, inode
and directory metadata, and overwritten contents, including ENOSPC during growth.
A successful operation syncs the image before discarding its undo log.

If restoration or its sync fails, the operation returns `EUCLEAN` (reported by
Linux as "Structure needs cleaning") and a persistent writer session refuses
further operations. Close/unmount it and check a copy of the image. A QCOW2 backend
that has already latched a write failure may reject undo writes; the QCOW2 dirty
state is preserved rather than claiming that recovery succeeded. Undo restores
virtual filesystem bytes; it need not restore the exact container allocation
layout or reclaim newly appended host clusters. VDI block allocation publishes
its allocation count before the map reference and restores both on returned
metadata-write failures. If that restoration fails, the backend rejects further
writes. Ambiguous parent UUIDs are rejected rather than selecting a directory entry.

Resize uses the same undo mechanism for growth metadata and saves the disk-label
region before modifying it. On recoverable failure it restores metadata and the
previous host file length. Cylinder-summary evacuation commits a consistent move
before restarting growth; a later growth failure may therefore leave those valid
moves in place. Formatter creation is a separate operation: it holds the target
lock through label creation, formatting and final sync, but a failed creation can
leave an incomplete output file.

## Write ordering audit

| Operation | Ordering and remaining crash exposure |
| --- | --- |
| Create | Allocate storage and initialize the inode before inserting its directory entry. An interruption can leave unreferenced allocations. |
| Rename | Replace the destination, insert the new name, remove the old name, then update parent/link metadata. One shared parent inode state is used for sibling replacement. An interruption can leave intermediate namespace/link counts. |
| Write/grow | Preserve existing blocks; allocate or extend only needed runs. Zero newly exposed bytes, update pointers and accounting, then sync. Existing-data writes are in place. |
| Shrink/unlink | Remove storage and indirect metadata incrementally and update inode/link accounting. An interruption can leave stale pointers or allocation counts. |
| Resize | Evacuate obstructing summary storage, extend the host file, update label/groups/superblocks, then sync. Returned failures attempt rollback; interruption can leave partial growth. |

**This is not a durable journal or a power-loss guarantee.** The undo file is
unlinked and disappears when the process exits. Process death, torn writes and
storage reordering can interrupt the sequences above. Keep a copy for repair or
modification of valuable images. QCOW2 metadata ordering/dirty marking does not
make the UFS operation atomic, and v2 has no v3 dirty bit.

Tests cover returned syscall errors, malformed geometry, competing writers,
failed growth, directory replacement, sparse reads, near-full overwrites and
concurrent mounted clients. They do not establish crash recovery or guest-boot
compatibility. See [the test guide](../tests/README.md).
