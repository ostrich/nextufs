# Repair Lab

This directory contains the repair-development lab for `nextufs fsck`.

Goals:
- keep corruption fixtures pristine
- make every corruption deterministic and reproducible
- use disposable working copies for any mutating `fsck` run
- support optional comparison against a second checker build

Run `make test-fsck` (also included in `make test`). Each corpus case is
individually selectable, for example `python3 tests/run.py fsck/corpus/dup-block`;
`fsck/corpus` selects all 31. Use `--keep` to retain images and logs.
See [the test guide](../README.md) for prerequisites and artifact handling.

`fixtures.py` supplies the same seed and preservation rules to the corpus and lab.
`model.py` independently reads raw UFS bytes using documented on-disk offsets,
without calling the filesystem library. Repair checks include:

- non-mutating detection with status 8, repair with status 0, clean recheck;
- exact contents and inode identity for survivors, explicit permitted losses,
  and named recovery in `lost+found`;
- valid directory records, parent links, link counts, unique block ownership,
  allocated inode/fragment bits and sector-based block counts;
- convergence after the clean-state transition, followed by a byte-identical repair.

The seed includes a sparse indirect file, a nested canary tree and a trailing
root entry. Before/after semantic snapshots are saved as JSON. `test_lab.py`
proves the oracle rejects an empty replacement filesystem and changed contents,
and the comparison lab rejects missing/crashing checkers, fake success, and a
changed first diagnostic line.

`fsck/matrix` covers selected repairs on labeled, 2 KB-fragment and CD-sector
images; deep and multiple orphan trees; full `lost+found` expansion while
preserving hundreds of hard links; dirty preen repair/refusal versus intentional
clean skipping; and fragment/inode bitmap repair in a nonzero cylinder group
with a partial final group. `fsck/failures` injects returned write and flush
failures and verifies successful recovery on a subsequent invocation.

`vdi/repair` and `qcow2/repair` repair inherited corruption in differencing
children and require byte-identical parents. QCOW2 also uses QEMU's checker and
raw export; VDI exports through the backend probe. Both exports receive the
independent UFS checks. QEMU remains an explicit optional prerequisite.

Layout:
- `tools/`: corruption generator source; binaries live under `build/tests/`.
- `cases/`: descriptions of the original corruption families.
- `test_*.py`, `test_images.sh`: executable regression cases.
- `lab/`: optional comparison tools for a second checker build.

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
`-n` normally returns 8. Comparison checks status, all diagnostic lines (normalizing only image paths),
and independent repaired snapshots. Missing executables, timeouts, signals and
checks skipped in preen mode fail the comparison. All-case comparison reports
every failure and retains individual logs instead of stopping at the first one.
Comparison records executable paths and hashes in `provenance.json`, alongside
pristine/corrupt/repaired images, logs and JSON reports. Each invocation creates a fresh `.scratch/fsck-lab/run-*/` directory and generates
its fixtures there. Set `FSCK_LAB_DIR` only to intentionally share one investigation's
artifacts; fixtures are regenerated rather than trusting an existence-only cache.
Preen runs mark damaged fixtures dirty. The scripts require already-built programs and never invoke Make.
`CORRUPT` can select another built corruption generator.

The `lostfound-*` fixtures exercise repair successfully but also trigger surrounding
fallout; they do not isolate every `lost+found` create/reallocate path. Reference
comparison remains an investigation aid, separate from regression assertions.

Coverage is deliberately finite: these are deterministic synthetic repairs, not
a power-loss or exhaustive corruption campaign. Returned I/O failures do not
simulate process termination. No-space reconnection, arbitrary directory cycles,
large indirect directories and combined multi-layer corruption still need
dedicated fixtures before claiming coverage.
