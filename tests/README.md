# Tests

Make builds the programs; `tests/run.py` selects and runs tests. The runner uses
Python's standard library. Existing focused shell tests and C probes remain
executable tests, not a second build system.

## Commands

| Command | Scope | Extra requirements |
| --- | --- | --- |
| `make test` | API, CLI, filesystem mutations, full fsck corpus, helper and runner tests | Python 3, Bash, ordinary build tools |
| `make test-fsck` | Image layouts, filename validation, detection and repair corpus | Same as `test` |
| `make test-qcow2` | QCOW2 reads, writes, COW, version/cluster matrix, rejection and injected failures | `qemu-img`, `qemu-io` |
| `make test-fuse` | Mount lifecycle, reads, writes, offline-operation readback, raw/QCOW2 stress | FUSE build, usable `/dev/fuse`, `fusermount3`, QEMU tools |
| `make test-stress` | Seeded offline and batch campaigns | Ordinary build tools |
| `make test-image TEST_IMAGE=/path/to/image` | OPENSTEP content/API compatibility and mutation stress on a copy | Supplied OPENSTEP installation image |
| `make test-all` | All generated-fixture groups above; excludes supplied-image tests | FUSE and QEMU requirements |
| `make test-qcow2-sanitize` | QCOW2 group with an ASan/UBSan byte-I/O probe | GCC/Clang sanitizer support and QEMU |
| `make test-qcow2-coverage` | QCOW2 group with gcov instrumentation of `qcow2.c` | GCC, gcov and QEMU |
| `make test-list` | List all generated-fixture cases | Python 3 only |

Explicitly requested groups fail preflight if prerequisites are missing. There
is no successful whole-group skip for unavailable QEMU or FUSE. The separate
chain-helper unit tests include one optional QEMU check, which unittest may skip
when running the ordinary suite without QEMU; its header-only tests still run.

`make WITH_FUSE=0 test` exercises the offline build, including the unavailable
mount command contract. Ordinary tests do not require mounting, QEMU or an OS
image, even when the program is built with FUSE support.

For individual cases, build once and invoke the runner directly:

```sh
make test-tools
python3 -B tests/run.py --list
python3 -B tests/run.py filesystem/truncate
python3 -B tests/run.py 'filesystem/*' fsck
python3 -B tests/run.py qcow2 --probe /absolute/path/to/instrumented-probe
python3 -B tests/run.py default --bin-dir build/tests/fuse-0 --nextufs build/bin/fuse-0/nextufs
```

Groups and glob selections are deduplicated. `--list all` lists every generated
case; `--list image` lists supplied-image cases. `TEST_ARGS='--keep'` passes runner
options through a Make target. `NEXTUFS` or `--nextufs` selects a different built
command; `--bin-dir` selects the corresponding test programs. Programs and libraries
are separated by build mode; the root `nextufs` symlink selects the last built mode. Build mode changes
must finish before running tests; do not rebuild a shared executable while
another test run is using it.

## Artifacts and failure handling

Each invocation creates `.scratch/tests/run-*/` and gives each case its own
working directory and `TMPDIR`. Successful case directories are removed by
default. Failures retain images, inputs, command/output logs and `results.json`;
`--keep` retains successes too. The runner reports pass/fail counts for executable
cases; nested unittest counts are in that case's log. A case has a bounded timeout
(`--timeout`, seconds). Assertion failures, crashes and timeouts fail the run.

FUSE helpers create a fresh mountpoint, wait for an observable mount, and unmount
and reap their own process even when an assertion fails. No test unmounts a fixed
shared `/tmp` location. The C stress program also uses a unique mountpoint under
the case's `TMPDIR`. `fuse/cleanup` deliberately fails an assertion to exercise
cleanup, and the runner unit tests check failed comparisons and crashed commands.

The fsck corpus is regenerated for every run from the committed case list and
corruption tool. Each case must be detected with status 8 without modifying the
image, repaired with status 0, and rechecked clean with status 0. Diagnostic text
alone never establishes success. Optional reference-comparison tools live under
[fsck/lab](fsck/README.md); they are not prerequisites of the regression suite.

Instrumentation runs keep their probe and reports under
`.scratch/instrumentation/`. Sanitizers cover the probe and all `src/core/*.c`;
the CLI/fsck subprocesses use the normal binary. UBSan findings are fatal. Before
running the suite, the sanitizer command verifies that an intentional overflow
fails and that the command helper propagates that failure; its diagnostic is
retained as `sanitizer-gate.log`. Coverage instruments
`src/core/qcow2.c` in the probe, and writes `coverage.txt` and gcov output. Neither
command replaces the normal binary. See the [QCOW2 coverage review](qcow2/README.md)
for what these measurements establish and the remaining coverage gaps.

## Layout and migration

- `api/`: C API tests and generated API fixture.
- `cli/`: command contracts, browsing/extraction and formatting.
- `filesystem/`: mutation scenarios, allocation geometry, growth and failure handling.
- `fsck/`: corruption cases, generator, image tests and optional comparison lab.
- `qcow2/`: container probes and interoperability tests.
- `fuse/`: mount lifecycle and native filesystem operations; mutation readback shares the filesystem scenarios.
- `stress/`: C model-based stress program and bounded campaigns.
- `compatibility/`: tests that intentionally depend on a supplied OS installation.
- `tools/`: unit tests for the chain helper and runner.
- `support.py`: shared command, fixture, integrity and mount helpers.

The former `tests/nextufs/` directory and inline Make recipes are replaced by
these groups. Scenario mapping:

| Previous target | Replacement case/group |
| --- | --- |
| `test-nextufs` | `default`; original OS-content assumptions are in `image/read` |
| `test-cli-contract`, `test-browse`, `test-mkimg` | `cli/contract`, `cli/browse`, `cli/mkimg` |
| `test-resize`, `test-fragments` | `filesystem/resize`, `filesystem/fragments` |
| `test-write`, `test-write-big`, `test-write-grow` | `filesystem/write`, `filesystem/file-import`, `filesystem/directory-growth` |
| `test-unlink`, `test-mkdir`, `test-rewrite`, `test-rmdir` | Corresponding `filesystem/` cases |
| `test-link-symlink`, `test-meta` | `filesystem/links`, `filesystem/metadata` |
| `test-rename`, `test-truncate`, `test-special`, `test-permissions`, `test-failure` | Corresponding `filesystem/` cases |
| Mounted checks inside mutation recipes | `fuse/readback-*` on generated images |
| `test-fuse-write`, old `test_fuse.sh` | `fuse/write`, `fuse/read` |
| `test-stress`, `test-stress-batch` | `stress/offline`, `stress/batch` |
| `test-stress-base`, `test-stress-fuse` | `image/stress`, `fuse/stress` |
| `test-fsck-images`, `repair-smoke`, `repair-repair-all` | `fsck/images`, `fsck/corpus` (also in the default suite) |
| `test-qcow2-chain` | `tools/unit` |
| `repair-tools`, `repair-corpus`, `repair-lab` | `make test-tools`; optional `tests/fsck/lab/` scripts |

Tests of general mutations now use generated images and compare actual bytes,
metadata and filesystem integrity. Installed-image directory sizes and hardcoded
slice offsets are no longer used as proxies for correctness. API fixtures include
a synthetic passwd file, symlink and large kernel-named payload to preserve the
original path traversal and indirect-read scenarios without distributing OS data.
