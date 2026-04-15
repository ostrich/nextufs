# fsck.nextufs

This directory contains the architecturally refactored implementation of
`fsck.nextufs` in modern C.

This is not a semantic redesign. The legacy `fsck.legacy/` source tree is the
specification for behavior only:
- pass structure
- invariants
- checks
- repair decisions
- state transitions
- mutation ordering
- error paths
- user-visible behavior

Current status:
- semantic analysis complete
- architecture redesign plan complete
- responsibility-based module layout implemented
- explicit per-run checker context implemented
- raw-source checker implementation in place
- swapped-filesystem writeback implemented with buffer-boundary byte swapping
- repair lab and corruption corpus in place

Current scope:
- raw-source only
- no source-preparation or VDI path
- semantic fidelity takes priority over elegance
- architectural refactoring is expected and deliberate

Process constraints:
- preserve legacy phase structure and control flow
- preserve repair decisions, prompts, exit paths, and mutation ordering
- treat any logical deviation as a bug unless explicitly justified
- do not preserve old file layout or globals by default
- retain legacy structure only when semantically necessary and justified

Documents:
- [ANALYSIS.md](ANALYSIS.md): legacy program behavior, phases, state, and
  repair points
- [MODULE_GRAPH.md](MODULE_GRAPH.md): implemented responsibility-based module
  graph
- [EQUIVALENCE_PLAN.md](EQUIVALENCE_PLAN.md): mapping from legacy logic blocks
  to the new modules and functions
- [DIVERGENCES.md](DIVERGENCES.md): running list of known divergences between
  the legacy tree and the modern implementation
- [tests/README.md](tests/README.md): corruption corpus, repair workflow, and
  parity/repair test harness
