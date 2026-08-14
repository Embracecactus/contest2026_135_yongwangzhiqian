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
