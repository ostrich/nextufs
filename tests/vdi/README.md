# VDI coverage

Run `make test-vdi` for all eight cases, or `make test-sanitize TEST_ARGS=vdi`
for whole-utility fatal ASan/UBSan coverage. FUSE and QEMU prerequisites are
checked before execution. The ordinary `make test` suite includes generated
backend cases and helper unit tests without these external prerequisites.

| Case | What it establishes |
| --- | --- |
| `vdi/backend` | Head-only writes, immutable parent, malformed maps, cycles and the 32-image depth limit |
| `vdi/matrix` | Exact guest-byte comparisons for cross-block writes, multilevel inheritance, explicit zero and unallocated blocks, partial final blocks, shorter parents, and per-block extra bytes |
| `vdi/parents` | Snapshot-directory discovery and rejection of missing, mismatched or duplicate parent UUIDs, including ambiguity across both searched directories |
| `vdi/failures` | One-shot read/write/sync errors during fresh allocation in a three-level chain; failed operations restore guest bytes, reopen cleanly and pass fsck; persistent write errors poison the writer session |
| `vdi/interop` | QEMU-generated dynamic VDI, native mutations, conversion back through QEMU, full guest-byte equality and fsck |
| `vdi/helper` | UUID byte order, header rejection, graph roles, snapshot tracing, duplicate identities, bounded cycle output and CLI errors |
| `filesystem/io-failures` | Shared raw/VDI mutation failures and writer-session checks |
| `fuse/native-vdi` | Concurrent clients, writer exclusion, seeded writes/truncates, directory replacement, links and attributes through a real mount |

QEMU is an independent oracle for standalone dynamic VDI. Differencing-chain
fixtures are generated locally and checked against an explicit raw byte oracle;
these tests do not claim VirtualBox snapshot/guest-boot interoperability. The
backend supports dynamic and differencing images, not fixed VDI. Fault injection
covers returned syscall errors, not process termination, torn writes or power
loss. Container allocation layout may change after recovery even when all guest
bytes are restored. See [write recovery limits](../../docs/writes.md).
