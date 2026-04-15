# Legacy fsck Analysis

This document reverse-engineers the existing `fsck/` tree and treats it as the
behavioral specification for the modern rewrite.

## Scope

Primary legacy files reviewed:
- `fsck/main.c`
- `fsck/setup.c`
- `fsck/utilities.c`
- `fsck/pass1.c`
- `fsck/pass1b.c`
- `fsck/pass2.c`
- `fsck/pass3.c`
- `fsck/pass4.c`
- `fsck/pass5.c`
- `fsck/dir.c`
- `fsck/inode.c`
- `fsck/fsck.h`

## Top-Level Control Flow

The top-level program structure in `main.c` is:

1. Parse options.
2. Install signal handlers.
3. If filesystem arguments are present, run `checkfilesys()` on each.
4. Otherwise iterate `fstab` by pass number, optionally forking in preen mode.
5. Aggregate child exit statuses and decide final exit code.

### Command-Line State

The legacy flags directly affect behavior:
- `-p`: preen mode
- `-P`: NeXT-specific flag
- `-b`: alternate superblock location
- `-d`: debug output
- `-n` / `-N`: answer no
- `-y` / `-Y`: answer yes

### `checkfilesys()` Pass Order

The legacy checker executes phases in this order:

1. `setup(filesys)`
2. `pass1()`
3. optional `pass1b()` if duplicates were found
4. `pass2()`
5. `pass3()`
6. `pass4()`
7. `pass5()`
8. final summary and superblock state handling
9. `ckfini()`

This ordering is part of the specification.

## Global and Mutable State

The legacy checker is strongly stateful. The modern rewrite must preserve
semantic ownership of these global concepts even if the storage moves into a
single state struct.

### Global Buffers and File Control

- `struct filecntl dfile`
  - `rfdes`, `wfdes`, `mod`
- `BUFAREA inoblk`
- `BUFAREA fileblk`
- `BUFAREA sblk`
- `BUFAREA cgblk`

### Filesystem-Wide State

- `struct fs sblock`
- `struct csum *fsck_fs_csp[MAXCSBUFS]`
- `devname`
- `rootfs`
- `readonlyfs`
- `mountedfs`
- `usingblkdev`
- `needswap`

### Phase State and Working Sets

- `blockmap`
- `statemap`
- `lncntp`
- `duplist`
- `muldup`
- `zlnhead`
- `pathname`, `pathp`, `endpathname`
- `lfdir`
- `lfname`
- `n_blks`
- `n_files`
- `imax`
- `fmax`
- `lastino`
- `bmapsz`

### Mode / Interaction Flags

- `preen`
- `nflag`
- `yflag`
- `bflag`
- `debug`
- `Pflag`
- `exitstat`

## Core Data Structures

These are semantic, not just representational.

- `struct inodesc`
  - carries current inode walk state
  - current callback
  - logical parent/name context
  - fix mode and walk counters
- `struct dups`
  - duplicate-block tracking list
- `struct zlncnt`
  - zero-link inode list
- inode state encoding in `statemap`
  - `USTATE`
  - `FSTATE`
  - `DSTATE`
  - `DFOUND`
  - `FCLEAR`
  - `DCLEAR`

These states are central to pass invariants and must remain explicit in the
rewrite.

## Phase Responsibilities and Invariants

## Pass 1

Legacy entrypoint: `pass1()`

What it does:
- marks reserved filesystem blocks in `blockmap`
- scans all inodes
- classifies each inode into `USTATE`, `FSTATE`, `DSTATE`, `FCLEAR`, or
  `DCLEAR`
- records link counts into `lncntp`
- records zero-link inodes into `zlnhead`
- validates blocks via `ckinode(..., pass1check)`
- checks `di_blocks` against observed block usage

Important special cases:
- partially allocated but otherwise unallocated inode may be cleared
- unknown file type is not silently normalized
- fast symlink handling short-circuits normal data-block checks once validated
- garbage pointers past logical EOF trigger repair logic in indirect blocks

Pass 1 invariant established:
- every inode has a state in `statemap`
- `blockmap` contains all blocks believed to be referenced
- duplicate references are queued in `duplist`
- residual link counts are initialized in `lncntp`

### Pass 1 Repair Decision Points

- clear partially allocated unallocated inode
- continue after too many bad blocks
- continue after too many duplicate blocks
- fix partially truncated indirect block entries
- fix `di_blocks` mismatch

## Pass 1b

Legacy entrypoint: `pass1b()`

What it does:
- rescans non-`USTATE` inodes
- traces duplicate block references reported in pass 1

Invariant:
- duplicate block reports are fully cross-referenced between `duplist` and
  `muldup`

## Pass 2

Legacy entrypoint: `pass2()`

What it does:
- validates or reconstructs root inode state
- recursively descends reachable directories using `descend()`
- validates directory entry structure and namespace consistency with
  `pass2check()`

Important directory rules enforced here:
- `.` must be first and self-referential
- `..` must be second and point to parent
- invalid inode references may be removed
- directory hard links beyond `.` and `..` are treated as errors
- `DCLEAR` / `FCLEAR` children may be removed or reclassified

Pass 2 invariant established:
- every reachable directory becomes `DFOUND`
- namespace-visible link counts are decremented from `lncntp`
- invalid directory entries may be removed in place

## Pass 3

Legacy entrypoint: `pass3()`

What it does:
- finds directories still in `DSTATE`
- walks upward via `..` to determine whether each is disconnected
- reconnects disconnected directories through `lost+found` with `linkup()`

Pass 3 invariant established:
- disconnected directories are either reconnected or left for later clearing

## Pass 4

Legacy entrypoint: `pass4()`

What it does:
- resolves residual reference-count problems
- clears unreferenced or bad/duplicate inodes
- frees blocks through `pass4check()`

Important cases:
- `FSTATE` / `DFOUND` with nonzero residual count go through `adjust()`
- `DSTATE` is cleared as unreferenced
- `DCLEAR` / `FCLEAR` are cleared as `BAD/DUP`

Pass 4 invariant established:
- live inodes have reconciled link counts
- blocks from cleared inodes are removed from `blockmap`

## Pass 5

Legacy entrypoint: `pass5()`

What it does:
- recomputes cylinder-group maps and summary counts from scratch
- compares recomputed state to on-disk cylinder-group data
- repairs free-space and summary accounting when accepted

Pass 5 invariant established:
- on-disk allocation summaries are aligned with the checker’s recomputed view

## Directory and Inode Helper Semantics

These helpers are semantic core logic and must not be rewritten loosely.

### `descend()`

- requires target inode to be `DSTATE`
- transitions it to `DFOUND`
- validates length and alignment of directory size
- runs directory scan using parent callback

### `fsck_readdir()` and `dircheck()`

- define the exact directory-entry validity model
- include the in-place corruption coalescing behavior used by pass 2

### `linkup()`

- handles orphan reconnect flow
- finds or creates `lost+found`
- may reallocate `lost+found` if it exists but is not a directory
- creates a temporary `#<ino>` entry name
- updates `..` of reconnected orphan directories
- updates link counts and path output side effects

### `makeentry()` / `expanddir()`

- encode exact entry insertion and directory growth behavior
- `expanddir()` only grows within direct blocks

### `allocino()` / `allocdir()` / `freeino()` / `freedir()`

- allocate and initialize inodes/blocks with legacy ordering
- update `statemap`, `lncntp`, `n_files`, and parent link counts in specific
  places

### `ckinode()` / `iblock()`

- define the exact block traversal order
- `iblock()` repairs partial truncation only in the pass-1 callback case

## Repair Decision Points

The modern rewrite must preserve each category of repair decision:

- superblock field normalization in `setup()`
- unallocated-but-dirty inode clearing
- bad block and duplicate block handling
- root inode reallocation / mode correction
- directory entry removal and `.` / `..` repairs
- orphan reconnect into `lost+found`
- link-count adjustment
- inode clearing
- cylinder-group summary repair

## User-Interaction Points

Legacy user interaction is part of the specification:

- `reply()`
- `pfatal()`
- `pwarn()`
- `pinfo()`
- pass-specific prompts such as:
  - `CLEAR`
  - `CONTINUE`
  - `ALLOCATE`
  - `REALLOCATE`
  - `FIX`
  - `REMOVE`
  - `ADJUST`
  - `RECONNECT`
  - `CREATE`
  - `EXPAND`
  - `SET TO DEFAULT`

Preen mode changes both prompting behavior and fatality decisions. That must be
preserved.

## Exit and Error Paths

Major exit/error mechanisms:

- `errexit(...)`
- `reply("CONTINUE") == 0` leading to termination in several low-level paths
- `setup()` failure returning `0`
- final exit code aggregation in `main()`
- `exitstat` updates on negative answers in non-write/no mode

The rewrite must preserve both local failure behavior and final process exit
behavior.

## Legacy Tree Divergence Already Present

The current `fsck/` tree is not identical to the historical checker source.
It already contains later source-preparation support:

- `preparedsource` global
- `fsck_prepare_source()`
- `fsck_cleanup_prepared_source()`
- additional source-opening logic in `setup()` / `utilities.c`
- untracked `fsck/source.c`

These are not treated as part of the original algorithm specification for the
modern rewrite unless explicitly carried forward later.
