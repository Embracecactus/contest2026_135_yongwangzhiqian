<!-- PROJECT_MEMORY_START -->
## Project memory and Git publication

- Project memory is opt-in. Do not read, update, or checkpoint it unless the owner invokes `$maintain-project-memory`.
- Exception: when the owner explicitly requests a commit, push, or PR action, read only the [Git publication ownership rules](memory/RULES.md#git-publication-ownership) before acting.
<!-- PROJECT_MEMORY_END -->

## BK7258 trust safety

- During active BK7258 work, do not use N17 or another historical trust domain as a source, baseline, key candidate, or fallback unless the owner explicitly reactivates it.
- Every owner-authorized full download starts a fresh trust generation: create new ephemeral P-256 key pairs independently for BL1 and MCUboot, embed their public keys in a clean build, and use their private keys to sign the complete BL1/BL2/CP/AP chain. Never reuse a previous generation's private keys for another full download.
- Keep fresh private keys in a mode-0600 temporary directory only, never print or record their contents or paths in tracked files, and remove them after package verification and hardware acceptance. The signed package retains only public trust evidence.
- Before a fresh-key full download, the non-halting target preflight must match the latest accepted base generation, while the new package must independently pass its complete internal trust verification and use strictly increasing rollback counters. After download, fresh boot/readback evidence must identify the new generation. Do not require the not-yet-installed public key to match the pre-download target.
- The apps-only loader path remains bound to the already-installed public-only trust contract and its exact target fingerprint. Do not mix it with the fresh-key full-download path or add a parallel key resolver, trust gate, or download policy.

## BK7258 architecture

- Before using an old implementation as design input, define the target public commands, internal domain boundaries, authoritative source for each mutable fact, and deletion set. Stop at architecture analysis if any is unknown.
- Historical scripts, schemas, tests, and documents are evidence, not requirements. Preserve behavior only when a current build, package, verification, or hardware path consumes it; do not create one-file compatibility moves.
- `tools/bk7258/bk7258.py` is the only tracked public entry and exposes only `build`, `sdk`, `package`, and `verify`; domain implementation belongs under `_lib`.
- The team manifest owns SDK/toolchain identity, CP/AP profiles own board/role compatibility, `--boot` is explicit input, and the selected partition CSV owns geometry, topology, roles, and build/write policy. Consumers must not duplicate these facts.
- Accept cleanup only after reporting deleted layers, confirming tracked top-level file count did not grow, and checking for duplicate version, profile, path, layout, or build-policy truths.
