# Module Graph

This document defines the intended architecture of the rewritten checker.

Goal:
- preserve legacy semantics aggressively
- refactor architecture aggressively
- make module boundaries reflect responsibility rather than historical source
  files

## Design Principles

- pass structure remains explicit because it is semantic, not historical
- checker state moves into one explicit context object
- traversal and repair primitives are shared services used by passes
- directory logic, inode logic, allocation-map logic, and operator I/O are
  separated by responsibility
- no file exists solely to mirror an old translation unit

## Implemented Modules

## Public Shell

- `main.c`
  - process startup
  - option parsing
  - signal policy
  - per-filesystem orchestration
  - fstab iteration and preen forking

- `session.c`
  - one-filesystem check session orchestration
  - phase banner printing
  - summary printing
  - end-of-run superblock state handling

Rationale:
- separates process-level iteration from one-filesystem semantic execution

## Shared State and Types

- `fsck.h`
  - `struct fsck_ctx`
  - state enums
  - shared legacy-compatible helper types

- `state.c`
  - context initialization
  - per-run state reset
  - allocation/free of maps, dup lists, zero-link lists

Rationale:
- historical globals are semantically important but architecturally accidental
- the rewrite should make them explicit and scoped

## Operator and Device I/O

- `operator.c`
  - `reply`
  - `pfatal`, `pwarn`, `pinfo`
  - `errexit`
  - signal-facing shutdown helpers

- `device.c`
  - low-level open/close
  - `bread`, `bwrite`, `rwerr`
  - mounted-device checks

- `buffer.c`
  - `getblk`
  - `flush`
  - `ckfini`-style buffer teardown sequencing

Rationale:
- operator interaction and block I/O are distinct responsibilities
- they were historically mixed in `utilities.c` only by accumulation

## Filesystem Bootstrap

- `setup.c`
  - top-level per-source setup policy
  - superblock load and validation
  - alternate-super comparison
  - summary-info loading
  - device type interpretation
  - mounted/root/read-only status initialization

Rationale:
- this logic is still concentrated in one module because the legacy setup path
  interleaves source opening and superblock validation semantically

## Inode and Block Traversal

- `inode_scan.c`
  - `ginode`
  - `ckinode`
  - `iblock`
  - `outrange`
  - low-level traversal callbacks and block-walk sequencing

- `inode_ops.c`
  - `ftypeok`
  - `pinode`
  - `blkerr`
  - `allocino`
  - `freeino`
  - `clri`

Rationale:
- traversal semantics and inode mutation/report helpers should not be fused

## Directory Traversal and Namespace Repair

- `dir_scan.c`
  - `descend`
  - `dirscan`
  - `fsck_readdir`
  - `dircheck`
  - `direrr`
  - `findino`
  - `findname`
  - `getpathname`

- `dir_repair.c`
  - `adjust`
  - `linkup`
  - `makeentry`
  - `expanddir`
  - `allocdir`
  - `freedir`
  - `lftempname`
  - `chgino`
  - `mkentry`

Rationale:
- directory reading/validation and namespace repair are separate responsibilities
- both remain explicit because the original semantics depend on their control
  flow

## Allocation and Bitmap Helpers

- `alloc_map.c`
  - block bitmap accessors
  - `allocblk`
  - `freeblk`
  - `fragacct` glue and related free-space helpers

Rationale:
- the old checker scattered allocation bookkeeping across utilities and passes
- this logic belongs together because it owns free-space accounting state

## Passes

- `pass1.c`
- `pass1b.c`
- `pass2.c`
- `pass3.c`
- `pass4.c`
- `pass5.c`

Rationale:
- pass boundaries are semantic and externally visible in messages
- collapsing passes would be a semantic rewrite

## Support

- `byteorder.c`
  - byte-swapping helpers

- `ufs_tables.c`
- `ufs_subr_user.c`
  - imported UFS support routines that are not checker logic

Rationale:
- low-level format support is neither pass logic nor checker policy

## Dependency Direction

High-level direction:

`main/session`
-> `setup`, `state`, `operator`
-> `super`, `device`, `buffer`
-> `passN`
-> `inode_scan`, `inode_ops`, `dir_scan`, `dir_repair`, `alloc_map`
-> `byteorder`, `compat_ufs`

Constraints:
- passes may depend on traversal and repair modules
- traversal modules may depend on buffer/device state
- low-level modules must not depend on pass modules
- operator prompting must not leak into low-level byteorder helpers

## Legacy Structure Intentionally Retained

- explicit `pass1` through `pass5` modules
  - retained because phase boundaries, ordering, and banners are semantic
- callback-driven inode descriptor traversal
  - retained because the original decision structure depends on it
- explicit state-machine enums such as `USTATE`, `DSTATE`, `DFOUND`
  - retained because pass invariants are defined in those terms

These are retained because they encode semantics, not because they are old.
