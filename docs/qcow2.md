# QCOW2 sources

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
