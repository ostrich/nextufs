# Known Divergences

This file records any known or suspected divergence between the legacy checker
and the modern rewrite effort.

Current status:
- one intentional scope divergence is present

Intentional divergence:
- `fsck.nextufs.modern` is currently raw-source only
- it does not carry the later `fsck_prepare_source()` / extracted-slice path
  found in the current `fsck/` tree
- this is intentional because the rewrite is using the historical checker logic
  as its specification first

Legacy-tree note:
- the current `fsck/` tree already contains non-original source-preparation
  logic for non-raw inputs
- that logic is not currently treated as part of the historical specification
  for the modern rewrite

Rules:
- every intentional behavioral divergence must be logged here before it lands
- every unresolved equivalence question should also be logged here
