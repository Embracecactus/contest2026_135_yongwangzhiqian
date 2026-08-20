<!-- PROJECT_MEMORY_START -->
## Project memory protocol

- Treat context compaction, a new session, an explicit continue/resume request, or conflicting visible state as a resume.
- Before the first code search, edit, build, deployment, or hardware action after resume: read repository instructions; inspect the active Git root, branch, HEAD, and dirty state; read `memory/INDEX.md`; read `progress/CURRENT.md`; then read only the active records linked there.
- Send the user a concise resume checksum with the objective, branch/HEAD, material work already complete, exact next action or blocker, and currently forbidden actions. If it conflicts with the latest request or repository/runtime evidence, reconcile it before acting.
- Do not browse milestones, old branches, sibling projects, archived verification, or unrelated trust domains to fill a context gap unless `CURRENT.md` links them for the active task or the user explicitly requests them.
- Verify important memory claims against Git, code, tests, configuration, or deployed state before acting on them.
- Keep each fact in one canonical location: durable rules and architecture in `memory/`, the current snapshot in `progress/CURRENT.md`, and phase-level evidence in `progress/verification/`.
- Keep `memory/INDEX.md` link-only and `progress/CURRENT.md` near 100 lines or fewer; link instead of copying status across documents.
- Treat the primary/coordinating agent as the single writer for `progress/CURRENT.md` unless ownership is explicitly delegated.
- At a material build, hardware result, merge, deployment, rollback, or newly confirmed blocker, replace the current snapshot in the same work turn before starting a different phase.
- Checkpoint only accepted durable decisions, meaningful phase or handoff points, materially changed verification conclusions, and major risks or blockers. Never append a chronological log to `CURRENT.md` or leave completed work stated as its next action.
- Do not checkpoint routine edits, builds, small tests, retries, or unchanged verification. Project memory must not create extra SOPs, campaigns, or test requirements.
- Use the project-memory checker only as a standalone structure check; never make product build or test success depend on documentation wording.
- Never store secrets, credentials, tokens, private keys, personal data, or raw production records in project memory.
- Do not commit, push, deploy, delete, or overwrite user work merely because the memory protocol exists; normal authorization rules still apply.
<!-- PROJECT_MEMORY_END -->

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
