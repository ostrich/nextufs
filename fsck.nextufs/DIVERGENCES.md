# Known Divergences

This file records any known or suspected divergence between the legacy checker
and the modern rewrite effort.

Current status:
- two intentional divergences are present

Intentional divergence:
- `fsck.nextufs` is currently raw-source only
- it does not carry the later `fsck_prepare_source()` / extracted-slice path
  found in the current `fsck.legacy/` tree
- this is intentional because the rewrite is using the historical checker logic
  as its specification first

- `fsck.nextufs` now preserves writable access on byte-swapped
  filesystems and swaps metadata at the buffer boundary on read/write
- the older source tree in `fsck.legacy/` disables writeback by closing `dfile.wfdes`
  as soon as it detects a swapped superblock in `setup()`
- OpenStep 4.2 x86 `/usr/etc/fsck` does not follow that model; its `setup`,
  `getblk`, `flush`, and `bwrite` path keeps writable access and performs
  per-buffer byte swapping instead
- the modern checker follows the OpenStep x86 model because that is the
  historically relevant shipped behavior for the target images we are testing
  and repairing

Legacy-tree note:
- the current `fsck.legacy/` tree already contains non-original source-preparation
  logic for non-raw inputs
- that logic is not currently treated as part of the historical specification
  for the modern rewrite

Rules:
- every intentional behavioral divergence must be logged here before it lands
- every unresolved equivalence question should also be logged here
