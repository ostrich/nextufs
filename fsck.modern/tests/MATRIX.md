# Corruption Matrix

This matrix tracks deterministic repair-lab coverage against the legacy
checker’s major detection and repair areas.

## Implemented Cases

Pass 1:
- `bad-block-count`
  - incorrect `di_blocks`
- `bad-file-type`
  - unknown allocated file type
- `cg-bitmap-bad`
  - used fragment falsely marked free in the cylinder-group bitmap
- `cg-summary-bad`
  - cylinder-group summary counts drift from rebuilt counts
- `dir-entry-fclear`
  - directory entry names an `FCLEAR` inode
- `dir-entry-unallocated`
  - directory entry names a fully unallocated inode
- `dup-block`
  - duplicate data block
- `out-of-range-block`
  - bad direct block
- `partial-indirect`
  - partially truncated indirect block entries
- `partially-allocated-inode`
  - unallocated inode with residual allocation

Pass 2:
- `missing-dot`
  - missing `.`
- `bad-dot-inode`
  - bad inode number for `.`
- `missing-dotdot`
  - missing `..`
- `bad-dotdot-inode`
  - bad inode number for `..`
- `extra-dot`
  - extra `.`
- `extra-dotdot`
  - extra `..`
- `extraneous-dir-link`
  - extraneous hard link to a directory
- `invalid-dir-inode`
  - inode out of range in directory entry
- `short-dir`
  - directory too short
- `misaligned-dir-size`
  - directory size not multiple of `DIRBLKSIZ`
- `zero-length-dir`
  - zero-length directory
  - note: this is a multi-effect case, not a pure isolated pass-2 case

Pass 3 / 4:
- `unreferenced-alpha`
  - unreferenced regular file
- `unreferenced-dir`
  - disconnected directory with downstream link-count fallout

Setup / root / pass 5:
- `root-unallocated`
  - root inode unallocated
- `root-not-dir`
  - root inode not a directory
- `super-cstotal-bad`
  - superblock `fs_cstotal` counts drift from rebuilt counts
- `super-minfree`
  - impossible `fs_minfree`
- `super-optim`
  - undefined `fs_optim`
- `super-free-counts`
  - wrong superblock free counts

## Staged But Not Yet Clean

These fixtures exist, but they still trigger too much surrounding fallout to
count as clean repair-oracle cases for the specific `lost+found` paths they are
meant to target:
- `lostfound-missing`
  - intended to drive reconnect with no `lost+found` directory
- `lostfound-not-dir`
  - intended to drive reconnect with `lost+found IS NOT A DIRECTORY`

## Not Yet Implemented

These remain good targets for additional deterministic fixtures:
- `DCLEAR` entry handling in directories
- bad/duplicate root inode cases
- excessive duplicate blocks / excessive bad blocks
- pass-5 summary / bitmap mismatches:
  - free-inode summary wrong
- lost+found-specific flows:
  - missing `lost+found` with a clean reconnect trigger
  - `lost+found` not a directory with a clean reconnect trigger
  - no space in `lost+found`

## Policy

- prefer single-effect fixtures first
- keep multi-effect fixtures only when the legacy checker’s semantics make the
  interaction itself important
- always preserve a pristine corrupted fixture and run repairs on a copy
