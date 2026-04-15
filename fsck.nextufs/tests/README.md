# Repair Lab

This directory contains the repair-development lab for `fsck.nextufs`.

Goals:
- keep corruption fixtures pristine
- make every corruption deterministic and reproducible
- use disposable working copies for any mutating `fsck` run
- use legacy `fsck.nextufs` as the initial repair oracle

Layout:
- `tools/`: small helper binaries used to generate corruption cases
- `scripts/`: seed/corpus/workflow scripts
- `cases/`: per-case notes
- `MATRIX.md`: coverage matrix of implemented and missing corruption families
- `cache/`: ignored generated assets
  - `seeds/`: pristine clean seed images
  - `corrupt/`: pristine corrupted fixtures
  - `expected/`: captured oracle output
- `work/`: ignored disposable working copies

Current seed:
- one small raw UFS image created with `mkfs.nextufs`
- populated deterministically with:
  - `/alpha`
  - `/dir`
  - `/dir/file`
  - `/big`
    - grown via `truncate` so indirect-block cases do not depend on host-file import

Current corruption cases:
- `bad-block-count`
  - increments `di_blocks` on `/alpha`
- `bad-dot-inode`
  - points `.` in `/dir` at the root inode
- `bad-dotdot-inode`
  - points `..` in `/dir` at `/dir` itself
- `bad-file-type`
  - sets `/alpha` to an unknown allocated file type
- `cg-bitmap-bad`
  - marks `/alpha`'s used fragment free in the cylinder-group bitmap
- `cg-summary-bad`
  - perturbs cylinder-group summary counts without changing actual allocation
- `dir-entry-fclear`
  - leaves `/dir/file` in `FCLEAR` state while the directory entry still points to it
- `dir-entry-unallocated`
  - fully clears `/dir/file`'s inode while leaving the directory entry intact
- `dup-block`
  - points `/dir/file` at `/alpha`'s data block
- `extra-dot`
  - renames `/dir/file` to `.`
- `extra-dotdot`
  - renames `/dir/file` to `..`
- `extraneous-dir-link`
  - turns `/alpha` into an extra hard link to `/dir`
- `invalid-dir-inode`
  - makes `/dir/file` point past `imax`
- `lostfound-missing`
  - clears both `lost+found` and `/alpha` from `/` to force reconnect without `lost+found`
- `lostfound-not-dir`
  - converts `lost+found` to a regular file and unrefs `/alpha`
- `missing-dot`
  - clears the inode number in `.` for `/dir`
- `missing-dotdot`
  - clears the inode number in `..` for `/dir`
- `misaligned-dir-size`
  - sets `/dir` size to a non-`DIRBLKSIZ` multiple
- `out-of-range-block`
  - points `/alpha` at a fragment beyond `fmax`
- `partial-indirect`
  - shrinks `/big` so stale indirect entries remain beyond logical EOF
- `partially-allocated-inode`
  - clears `/alpha` mode while leaving allocation behind
- `root-not-dir`
  - changes the root inode type to regular file
- `root-unallocated`
  - clears root inode allocation state
- `short-dir`
  - sets `/dir` size below `MINDIRSIZE`
- `super-cstotal-bad`
  - perturbs superblock aggregate free-block totals
- `super-free-counts`
  - perturbs free-space summary counts in the superblock
- `super-minfree`
  - sets `fs_minfree` to an impossible value
- `super-optim`
  - sets `fs_optim` to an undefined value
- `unreferenced-alpha`
  - clears the root directory entry for `/alpha`
- `unreferenced-dir`
  - clears the root directory entry for `/dir`
- `zero-length-dir`
  - sets `/dir` size to zero

Typical workflow:

```sh
make -C fsck.nextufs -f Makefile.linux repair-lab
fsck.nextufs/tests/scripts/run_case.sh legacy -n bad-block-count
fsck.nextufs/tests/scripts/run_case.sh modern -n bad-block-count
fsck.nextufs/tests/scripts/compare_case.sh -n bad-block-count
fsck.nextufs/tests/scripts/compare_all_cases.sh -n
```

The scripts always:
1. ensure the clean seed exists
2. ensure the pristine corrupted fixture exists
3. copy the fixture into `work/`
4. run the selected checker on the working copy

This keeps the corpus immutable while still allowing repair-mode runs.

Current limitations:
- the `lostfound-*` fixtures are not yet clean oracle cases
  - they still trigger enough surrounding fallout that they do not yet isolate
    the exact `lost+found` create/reallocate repair paths cleanly
- legacy `fsck.legacy/fsck.nextufs` remains a detection oracle for the cached swapped-image
  corpus under `-n`
  - the older source model still drops write access on swapped filesystems in
    `setup()`, so legacy `-y` runs do not provide a useful repair oracle on
    these images
- `fsck.nextufs` now supports swapped-image writeback and can be used for
  real repair runs on the cached corpus
  - representative `-y` cases repair successfully
  - full-cache `-n` parity remains the main semantic check against the legacy
    checker
