# QCOW2 coverage review

Reviewed 2026-11-05 against `src/core/qcow2.c` and the
[QEMU format specification](https://www.qemu.org/docs/master/interop/qcow2.html).
This covers nextufs's supported standard-L2 subset, not every QCOW2 feature.

Run `make test-qcow2`. QEMU tools are independent test oracles, not runtime
dependencies. The default build includes a real FUSE mount/stress/unmount test;
`WITH_FUSE=0` omits that part. `make test-qcow2-chain` tests the separate discovery
helper. Fixtures live under `.scratch/`; no installed binaries are changed.

| Area | Executable evidence |
| --- | --- |
| v2/v3 reads, raw UFS and disk labels | `test_reads.py`, `test_writes.py` |
| All 13 cluster sizes, 512 bytes through 2 MiB, in both versions | `test_matrix.py`: 26 combinations |
| Partial/overlapping writes, holes, cluster boundaries, last sector, zero-length and invalid I/O | `test_matrix.py`; full native read and QEMU raw conversion compared with a bytearray reference |
| L2 boundaries | `test_matrix.py` for every supported cluster size; sparse large-cluster fixtures compared with QEMU's extracted 1 KiB windows |
| Reopen persistence, read-only writes, close without explicit fsync | `test_matrix.py` and `backend_probe.c` |
| Shared data and shared L2 COW; remaining aliases preserved | `test_cow.py`; QEMU checks before and after |
| Allocated/unallocated zero clusters and preallocation | `test_cow.py` |
| Relative/absolute backing paths, mixed sizes/versions/clusters, short parent EOF | `test_reads.py`, `test_cow.py`, `test_matrix.py`; parent contents preserved |
| Missing/wrong-format parents, self/hardlink cycles, depth 32 accepted and 33 rejected | `test_failures.py` |
| Refcount block allocation and refcount table relocation | `test_writes.py`: 9 MiB write with 512-byte clusters; reopen, QEMU check and converted-byte validation |
| Reserved flags, out-of-range/overlapping metadata, inconsistent refcounts, truncation | `test_failures.py`; rejected opens preserve original bytes |
| Unsupported features and malformed extensions | `test_failures.py`: compression, encryption, snapshots, feature masks, refcount width, extension payloads |
| Unknown extension preservation | `test_failures.py` |
| Failed reads/writes/flushes/truncation and fsck repairs | `test_failures.py`; write failures checked for detectable v3 dirty state |
| Filesystem operations over mounted QCOW2 | `run_tests.sh`: FUSE stress, then QEMU and UFS checks |

The review found that unaligned v3 header lengths were accepted. A fixture with
header length 105 reproduced this; the backend now enforces eight-byte alignment.

## Verification on 2026-11-05

Host tools: GCC 16.2.1, QEMU 11.1.2. The expanded probe tests were also run
with AddressSanitizer and UndefinedBehaviorSanitizer on all `src/core/*.c`.
The CLI/fsck executable in that run was the normal build, not sanitizer-built.

A separate `-O0 --coverage` build of `src/core/qcow2.c`, linked into the byte-I/O
probe, measured 90.56% of 625 executable lines, 98.84% of 518 branches reached,
82.05% of branch outcomes taken, and all 111 call sites reached. These figures
cover the probe-driven COW, matrix, growth and failure tests, not CLI/FUSE paths.
They are control-flow measurements, not proof of complete format coverage.

To reproduce backend instrumentation, compile `src/core/qcow2.c` with
`--coverage`, then link that object before `build/libnextufs.a` together with
`backend_probe.c` and `io_faults.c`, retaining the same linker wrappers as
`build/qcow2_probe`. Set `QCOW2_PROBE` to the resulting absolute path when running
`tests/qcow2/run_tests.sh` directly; also set `NEXTUFS`, `QCOW2_FAULT_BIN` and
`WITH_FUSE=0`. Use `gcov -b -c` on the instrumented object. `make test-qcow2`
sets its own probe path, so an environment-only probe override does not apply.

## Remaining limits

- This is a deterministic interoperability suite, not exhaustive verification or
  a proof of power-loss safety. Injected system-call errors do not model torn
  sectors, reordered storage writes, sudden process death or real ENOSPC.
- Failure injection currently covers allocating writes on an empty v3 image and
  filesystem repair. It does not exhaust every failure point during shared-L2
  COW, refcount-table relocation or v2 writes. V2 has no v3 dirty feature bit.
- Allocation failures (`malloc`/`realloc`), exact resource-limit boundaries and
  multi-terabyte images are not exhaustively exercised. Large-cluster L2
  tests compare small windows rather than an entire virtual disk.
- Concurrent external writers, backing-file replacement while open, and guest
  OS boot/use of modified images are not tested. The suite uses disposable
  images and a single writer.
- FUSE stress is one deterministic 120-operation seed, not a long soak or a
  complete filesystem-operation matrix on every cluster size.
- The backend's close operation cannot report a flush error to its caller;
  successful close persistence is tested, but error-sensitive callers must use
  explicit fsync and check its return value.
- Unsupported compressed clusters, internal snapshots, encryption, extended L2,
  external data, bitmaps, lazy refcounts and non-16-bit refcounts are rejection
  cases, not supported-feature coverage.
