<!-- PROJECT_MEMORY_START -->
## Project memory and Git publication

- Project memory is opt-in. Do not read, update, or checkpoint it unless the owner invokes `$maintain-project-memory`.
- Exception: when the owner explicitly requests a commit, push, or PR action, read only the [Git publication ownership rules](memory/RULES.md#git-publication-ownership) before acting.
<!-- PROJECT_MEMORY_END -->

## BK7258 trust safety

- During active BK7258 work, do not use N17 or another historical trust domain as a source, baseline, key candidate, or fallback unless the owner explicitly reactivates it.
- Resolve trust identity from current-project provenance and explicitly approved same-domain public fingerprints. Never search broadly for keys, infer identity from a filename, or record private material or private-key paths.
- Reuse the existing public-only BL1/BL2 trust contract and non-halting J-Link fingerprint preflight before the apps-only loader path. Do not add a parallel key resolver, trust gate, or download policy.

## BK7258 architecture

- Before using an old implementation as design input, define the target public commands, internal domain boundaries, authoritative source for each mutable fact, and deletion set. Stop at architecture analysis if any is unknown.
- Historical scripts, schemas, tests, and documents are evidence, not requirements. Preserve behavior only when a current build, package, verification, or hardware path consumes it; do not create one-file compatibility moves.
- `tools/bk7258/bk7258.py` is the only tracked public entry and exposes only `build`, `sdk`, `package`, and `verify`; domain implementation belongs under `_lib`.
- The team manifest owns SDK/toolchain identity, CP/AP profiles own board/role compatibility, `--boot` is explicit input, and the selected partition CSV owns geometry, topology, roles, and build/write policy. Consumers must not duplicate these facts.
- Accept cleanup only after reporting deleted layers, confirming tracked top-level file count did not grow, and checking for duplicate version, profile, path, layout, or build-policy truths.
