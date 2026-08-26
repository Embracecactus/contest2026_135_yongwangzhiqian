<!-- PROJECT_MEMORY_START -->
## Optional project memory

- Project memory is opt-in. Do not read or update it, or checkpoint task events, unless the owner invokes `$maintain-project-memory`; follow separately stated repository instructions.
<!-- PROJECT_MEMORY_END -->

## Model routing and token discipline

- Keep the coordinating Sol agent focused on requirements, architecture,
  ambiguous root causes, tradeoffs, integration review, risky hardware
  actions, and final acceptance.
- Do not use Terra for this repository.
- Use one agent by default. Delegate only an independent, bounded task whose
  mechanical exploration would otherwise pollute the coordinating context.
- When delegation is justified, use only `gpt-5.6-luna` at `max`. Suitable
  work is narrow inventory, filtered long-log classification, exact-symbol
  review, repetitive verification, or a small edit with objective acceptance
  criteria. Sol retains architecture, ambiguous diagnosis, integration and
  final acceptance.
- Run at most one Luna subagent at a time. Keep it read-only when the main
  agent is editing the same area, and reuse its thread for follow-up work.
- In an indexed checkout, query CodeGraph before text search. Filter build and
  hardware output to the first actionable root cause and final PASS/FAIL;
  never relay raw logs when a short evidence summary is sufficient.
- Require subagents to return concise conclusions, exact file or symbol
  evidence, risks, and verification status instead of raw logs or long source
  excerpts.

## Git publication ownership

- Before any commit, push or PR action, follow the canonical
  [Git publication ownership rules](memory/RULES.md#git-publication-ownership).

## BK7258 resume guard

- During active BK7258 driver/application work, do not use or inspect N17 or
  another historical trust domain as a source, key candidate, baseline, or
  fallback unless the owner explicitly reactivates that phase.
- A missing `/tmp` filename is not evidence that a trust identity is lost.
  Before declaring a blocker or asking the owner to restore an agent-created
  artifact, trace its current-project provenance and compare only explicitly
  approved same-domain candidates by public fingerprint.  Never run a broad
  key search or infer identity from a filename.
- Do not invent a second key resolver, trust gate or download policy.  Reuse
  the merged BK7258 flow: build a public-only trust contract from the actual
  BL1/BL2 ELF and binaries, then require the non-halting J-Link fingerprint
  preflight before the existing apps-only loader path.  Do not record private
  material or private-key paths in repository memory.

## BK7258 architecture guard

- Before reading an old implementation as a design input, freeze four things:
  the final public command tree, the internal domain boundaries, the sole
  source of truth for every mutable fact, and the deletion set. If any of
  these is unknown, stop at architecture analysis instead of adding a
  compatibility layer.
- Design from the accepted target architecture, not from the historical file
  tree. Before a refactor, state the target public commands, internal domains
  and deletion set. Never perform a one-file-to-one-file compatibility move.
- Historical scripts, schemas, tests and documentation are evidence, not
  requirements. Preserve a behavior only when a current build, package,
  verification or hardware path actually consumes it.
- `tools/bk7258/` has exactly one tracked public entry: `bk7258.py`, exposing
  only `build`, `sdk`, `package` and `verify`. Implementation belongs under
  `_lib` by domain; do not add public wrappers or compatibility CLIs unless the
  owner explicitly requests one.
- Team manifest owns SDK/toolchain identity. CP/AP profile metadata owns
  board/role compatibility, while `--boot` is an explicit command input. The
  selected partition CSV owns geometry, storage topology, semantic roles and
  build/write policy. Python, shell, Make and CMake may consume these facts
  but must not duplicate them.
- Before accepting a BK7258 cleanup, report which old layers were deleted,
  confirm tracked top-level file count did not grow, and search for duplicate
  version/profile/layout truths. If a new file does not replace a real domain
  capability, delete it rather than documenting it.
- Reject a refactor that introduces a version, SDK checkout path, profile
  filename, partition address or build-policy conditional outside its owning
  manifest/profile/CSV. A changed historical test or document is never, by
  itself, evidence that the compatibility surface must survive.
