# Equivalence Plan

This document maps legacy `fsck/` logic into a behavior-preserving but
architecturally refactored implementation plan.

The original source defines semantics, not module boundaries.

## Design Rules

- one explicit checker context instead of ambient globals
- preserve original pass ordering and decision ordering
- preserve callback-driven inode and directory traversal
- redesign module boundaries around responsibility
- retain legacy structure only when semantically necessary
- comments should document phase invariants and equivalence, not redesign

## Legacy-to-New Mapping

## Program Shell

Legacy:
- `main()`
- `checkfilesys()`

Planned:
- `main()` in `main.c`
- `fsck_run_source()` in `session.c`

Equivalence note:
- command-line behavior, signal behavior, per-filesystem iteration, preen
  forking policy, and exit-code aggregation remain in the same order
- split retained only to separate process-level iteration from one-source check
  orchestration

## Setup and Teardown

Legacy:
- `setup()`
- `badsb()`
- `ckfini()`

Planned:
- `fsck_setup_source()` in `setup.c`
- `fsck_load_super()` / `fsck_bad_superblock()` in `super.c`
- `fsck_finish_run()` plus buffer shutdown in `buffer.c`

Equivalence note:
- superblock loading, byte-swap detection, alternate-super comparison,
  summary-info loading, and map allocation remain in the same order
- the old co-location of setup and superblock logic is not semantically
  required, so it will not be preserved

## Messaging and Prompting

Legacy:
- `reply()`
- `pfatal()`, `pwarn()`, `pinfo()`
- `rwerr()`

Planned:
- operator prompting in `operator.c`
- low-level read/write failure handling split between `device.c` and
  `operator.c`

Equivalence note:
- prompts, printed text, preen-specific fatality, and `exitstat` updates are
  preserved
- legacy co-location in `utilities.c` is treated as accidental

## Buffer I/O

Legacy:
- `getblk()`
- `flush()`
- `bread()`
- `bwrite()`

Planned:
- `device.c` owns raw device reads/writes
- `buffer.c` owns cached buffer lifecycle and flush ordering

Equivalence note:
- cache/buffer behavior remains explicit
- no hidden abstraction over legacy block I/O sequencing

## Inode Traversal

Legacy:
- `ckinode()`
- `iblock()`
- `outrange()`
- `ginode()`
- `findino()`
- `findname()`
- `blkerr()`
- `pinode()`

Planned:
- `inode_scan.c` owns block traversal and inode reads
- `inode_ops.c` owns inode reporting and inode mutation helpers
- `findino()` / `findname()` move to `dir_scan.c` because they are namespace
  directory-entry callbacks, not inode services

Equivalence note:
- direct then indirect traversal order is preserved
- callback invocation order is preserved
- pass1 partial-truncation repair remains confined to the same callback path

## Directory Traversal and Validation

Legacy:
- `descend()`
- `dirscan()`
- `fsck_readdir()`
- `dircheck()`
- `direrr()`

Planned:
- `dir_scan.c` owns directory walking, validation, and pathname reconstruction

Equivalence note:
- state transitions `DSTATE -> DFOUND`
- zero-length / short / misaligned directory handling
- entry-coalescing corruption repair behavior
- directory-entry callback order
  all remain unchanged

## Repair Helpers

Legacy:
- `adjust()`
- `linkup()`
- `makeentry()`
- `expanddir()`
- `allocdir()`
- `freedir()`
- `allocino()`
- `freeino()`
- `clri()`

Planned:
- namespace repair in `dir_repair.c`
- inode allocation/clear helpers in `inode_ops.c`
- block allocation/free-space helpers in `alloc_map.c`

Equivalence note:
- legacy helper placement is not preserved unless it matches actual
  responsibility
- each helper still maps directly to one or a small number of legacy logic
  blocks

## Pass Mapping

### Pass 1

Legacy:
- `pass1()`
- `pass1check()`

Planned:
- `pass1.c` remains the semantic owner of pass 1
- block/bitmap primitives it uses move under `alloc_map.c`

Equivalence note:
- inode classification and block-accounting behavior remain structurally close
- fast-symlink and partial-allocation special cases remain explicit

### Pass 1b

Legacy:
- `pass1b()`
- `pass1bcheck()`

Planned:
- same semantic split in `pass1b.c`

### Pass 2

Legacy:
- `pass2()`
- `pass2check()`

Planned:
- same semantic split in `pass2.c`
- root handling remains local to pass 2 because it is pass-2 semantics, not a
  generic directory helper

Equivalence note:
- root repair logic stays as a separate explicit case ladder
- directory entry repair ordering remains unchanged

### Pass 3

Legacy:
- `pass3()`

Planned:
- `pass3.c` remains a dedicated pass module
- lost+found repair helpers stay in `dir_repair.c`

Equivalence note:
- orphan-parent tracing via repeated `..` lookup remains structurally visible

### Pass 4

Legacy:
- `pass4()`
- `pass4check()`

Planned:
- same semantic split in `pass4.c`
- block release logic delegates to `alloc_map.c` and inode clear helpers

### Pass 5

Legacy:
- `pass5()`

Planned:
- `pass5.c` remains dedicated to cylinder-group recomputation and repair
- supporting summary/bitmap primitives move beneath it into `alloc_map.c` and
  `super.c` as appropriate

Equivalence note:
- cylinder-group recomputation is preserved as a rebuild-and-compare pass, not
  replaced with a higher-level summary helper

## Legacy Structure Retained On Purpose

- explicit pass modules
  - retained because phase boundaries are semantic and user-visible
- callback-based traversal via `struct inodesc`
  - retained because branch structure and mutation timing depend on it
- explicit checker state machine values like `USTATE` and `DFOUND`
  - retained because every pass invariant is expressed in those terms

## Reimplementation Order

1. replace mirrored globals/header with explicit context and state modules
2. split setup/operator/device/buffer responsibilities
3. split inode and directory services by responsibility
4. keep pass modules but retarget them onto the new services
5. delete temporary file-for-file carryover structure
6. re-run parity checks and log any divergence

Each step should land only after its equivalence note is written and its local
control flow is checked against the legacy code.

## Verification Standard

For each significant function or phase:

1. describe what the legacy code does
2. describe what the new code does
3. state why they are equivalent
4. record any divergence in `DIVERGENCES.md`

If equivalence is uncertain, the implementation should move closer to the
legacy structure rather than becoming cleaner.
