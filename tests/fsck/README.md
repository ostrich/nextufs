# Repair Lab

This directory contains the repair-development lab for `nextufs fsck`.

Goals:
- keep corruption fixtures pristine
- make every corruption deterministic and reproducible
- use disposable working copies for any mutating `fsck` run
- support optional comparison against a second checker build

Run `make test-fsck` (also included in `make test`). The image tests cover raw,
labeled, CD-sector and fragment layouts. `test_corpus.py` generates every case
fresh, requires non-mutating detection with status 8, repair with status 0, and a
clean recheck with status 0. No cached output or diagnostic blacklist is used to
decide whether a test passed. See [the test guide](../README.md) for case selection
and artifact handling.

Layout:
- `tools/`: source for the corruption generator; its binary lives under `build/tests/`.
- `test_images.sh`, `test_names.py`: image layout and filename regressions.
- `test_corpus.py`: detection/repair/recheck assertions.
- `cases/`: committed descriptions of each corruption family.
- `lab/`: optional tools for comparison with a second checker build.

Current seed:
- one small raw UFS image created with `nextufs mkimg --raw`
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

## Optional reference-comparison lab

```sh
make test-tools
tests/fsck/lab/run_case.sh shipped -n bad-block-count
FSCK_REFERENCE_BIN=/path/to/reference-fsck \
  tests/fsck/lab/compare_case.sh -n bad-block-count
FSCK_REFERENCE_BIN=/path/to/reference-fsck \
  tests/fsck/lab/compare_all_cases.sh -n
```

`run_case.sh` returns the checker's actual status, so detecting corruption with
`-n` normally returns 8. Comparison checks both status and normalized output.
Each invocation creates a fresh `.scratch/fsck-lab/run-*/` directory and generates
its fixtures there. Set `FSCK_LAB_DIR` only to intentionally share one investigation's
artifacts. The scripts require already-built programs and never invoke Make.
`CORRUPT` can select another built corruption generator.

The `lostfound-*` fixtures exercise repair successfully but also trigger surrounding
fallout; they do not isolate every `lost+found` create/reallocate path. Reference
comparison remains an investigation aid, separate from regression assertions.
