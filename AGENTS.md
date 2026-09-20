<!-- PROJECT_MEMORY_START -->
## Project memory and Git publication

- Project memory is opt-in. Do not read, update, or checkpoint it unless the owner invokes `$maintain-project-memory`.
- Exception: when the owner explicitly requests a commit, push, or PR action, read only the [Git publication ownership rules](memory/RULES.md#git-publication-ownership) before acting.
<!-- PROJECT_MEMORY_END -->

## BK7258 trust safety

- During active BK7258 work, do not use N17 or another historical trust domain as a source, baseline, key candidate, or fallback unless the owner explicitly reactivates it.
- A maintained signing identity is referenced explicitly by its public fingerprint and an approved secure local path, secret-manager, or HSM reference. Do not generate, rotate, destroy, print, or place private key material in the repository, logs, or temporary automation unless the owner explicitly authorizes that identity operation.
- A missing private signing key blocks only the signing/release step. It does not block source review, configuration, ordinary incremental builds, host regression, or unsigned diagnostic artifacts; state the blocked signing input precisely.
- Reuse unchanged boot components and sealed signed artifacts when content,
  configuration, toolchain, trust identity, protected metadata and dependencies
  still match. Use the existing CP/AP OTA signing path without re-signing BL1/BL2.
  A changed version, counter, TLV or pair dependency invalidates the affected
  signed artifact; a matching raw payload alone does not establish reuse.
- Before any full download, the target preflight must match its accepted base generation and the signed package must pass its own trust and rollback checks. Record the signer reference and public identity in release evidence, never a private path or value. The apps-only path remains bound to its installed public trust contract and exact target fingerprint.
- Backup scope follows the actual erase/write or migration scope. The current AIDK 8-MiB full-image path needs trusted same-device base material, but may reuse an accepted historical full-device readback when its hash and device identity match. The CLI requiring a complete base file is an interface condition, not a demand to reacquire it for every write. A configuration rollback needs explicit owner authorization; if non-reconstructible data has no trustworthy same-device material, refuse the operation. Never copy a target-bound image or device-unique data to another unit.

## BK7258 hardware-first debugging

- Active BK7258 bring-up and adaptation defaults to hardware-fast mode.  State
  `current mode: hardware-fast iteration` once at the start, then keep updates
  short and action-oriented.  Context compaction or a new session must not
  silently return the task to final-acceptance mode.
- In hardware-fast mode, follow the shortest useful loop: use the current board
  log to select one evidence-based hypothesis, make the smallest correctly
  layered change, incrementally build only the affected target, perform only
  the minimum image-integrity check, and hand off the directly flashable
  artifact immediately.  Real-board results are the acceptance signal for the
  iteration.
- Stop a short loop when the stated observation either confirms or rejects its
  single hypothesis, then report the next bounded question. Do not keep adding
  probes, broad regression, clean builds, signing, or key work after that stop
  condition without a new reason.
- Use mechanical tools for repeatable file, config, build and artifact facts.
  Give a model only the compact hypothesis, relevant evidence, changed owner,
  stop condition, and the affected tests; do not make it reconstruct unrelated
  logs or run a generic full-workspace review.
- A debug-artifact handoff is not a final project handoff.  Defer broad host
  regression, unrelated-board clean builds, documentation and provenance
  audits, official-checkout audits, ZIP/release assembly and other final
  acceptance work until the task includes that final delivery and the relevant
  hardware path passes. Do not introduce speculative tests merely to delay the
  next hardware attempt.
- Mandatory device-data and trust protections still apply.  If an iteration
  requires a whole-device BIN, perform only the minimum required accepted-base,
  signer-reference, signature, rollback and exact-Flash-size checks, then hand
  off the BIN without unrelated gates or a ZIP.  Prefer the installed apps-only path
  when its existing trust contract permits the affected CP/AP update.
- Passing the requested hardware observation completes that iteration; it does
  not add final acceptance or release work to the task. Continue any remaining
  authorized iterations. Enter final-acceptance mode only when the current task
  includes final acceptance or release, and apply only its matching handoff gates.

## BK7258 validation tiers

- Ordinary code/config increment: build only the affected role/target and run its directly
  relevant host or target regression. Reuse the established build tree and
  signing identity; neither `--clean` nor signing is a default validation step.
- Impacted regression: run the tests and builds selected by changed ownership
  (chip/common changes include affected board profiles; board wiring stays on
  that board). State why each selected test is relevant and what remains out of
  scope.
- Before adding a test or probe, state the current behavior or hypothesis it
  checks, why existing coverage is insufficient, and how its result changes the
  next action. Reuse the existing harness; stop expansion at the acceptance point.
- Boot/trust/layout change: run the dedicated clean, package, signature,
  rollback, readback and boot evidence path. This is an explicit specialist
  workflow, not a gate for ordinary app or driver increments.
- Documentation-only changes need format and affected-link review, not firmware
  builds or application tests. Reuse passing checks while their inputs remain
  unchanged.

## BK7258 delegation boundaries

- Follow the user-level T0–T3 routing policy. Hardware/Flash/trust, release,
  chip/board and AP/CP decisions remain owned by the current root agent.
- UART/debug/download access, a board build tree and a GPU training run are
  exclusive resources. Child agents may collect bounded logs, hashes,
  manifests, symbols and configuration evidence; the root chooses the
  hypothesis and owns each hardware-fast iteration and acceptance decision.

## BK7258 architecture

- Before redesigning legacy public entry points or domain ownership, define the target commands, ownership, authoritative mutable facts and deletion set. Resolve material architecture gaps before implementing that redesign; a narrow fix need not invent a complete redesign plan.
- Historical scripts, schemas, tests, and documents are evidence, not requirements. Preserve behavior only when a current build, package, verification, or hardware path consumes it; do not create one-file compatibility moves.
- `tools/bk7258/bk7258.py` is the only tracked public entry; domain implementation belongs under `_lib`. Consult its help for command syntax and the maintained build/package SOP for the requested workflow.
- The team manifest owns SDK/toolchain identity, CP/AP profiles own board/role compatibility, `--boot` is explicit input, and the selected partition CSV owns geometry, topology, roles, and build/write policy.  The board-selected release-policy CSV owns only product update semantics (`replace`, `preserve`, `device-unique`, `transactional`, `factory-init`, `immutable`) and must cover every partition by name without repeating offsets or sizes.  Consumers must not duplicate these facts.
- Descriptor-only board extension applies to the current BK7258 dual-core BL1/BL2 and MCUboot A/B product model.  A different boot/update model requires a separately reviewed product-mode contract; never implement it as a physical-board-name branch or silently reinterpret the current `boot`/`cp`/`ap`/BL2/manifest/`persistent_data` contract.
- For an authorized cleanup, report retired layers and ensure no duplicate public entry, version, profile, path, layout or build-policy owner was introduced. File count alone is not a quality gate.

## Upstream-oriented peripheral drivers

- This rule applies to every external peripheral class, including displays,
  sensors, storage, NFC, audio codecs, chargers, cameras and future devices.
- Before implementing a peripheral driver, search the relevant manifest-pinned
  NuttX/OpenVela trees first. Reuse an existing standard driver and ABI when
  one exists; provide only the missing SoC lower half and board binding.
- A vendor SDK private device object, component driver or example is reference
  material and, when unavoidable, a SoC transport backend.  It must not be
  used directly as the product peripheral driver or exposed as the product
  API.
- When NuttX/OpenVela has no suitable driver, implement the missing driver in
  an upstream-oriented NuttX form: public header under `include/nuttx/`,
  implementation under the matching `drivers/` class, Kconfig plus CMake and
  Make integration, a standard NuttX upper-half interface, and hardware-
  independent transport callbacks.  Keep vendor headers, SoC controller
  details, GPIO numbers, power votes and board wiring out of that generic
  driver.
- Keep SoC transport adaptation in `chips/` and physical instance policy in
  `boards/`.  Preserve the source license and provenance of any protocol or
  initialization sequence derived from a vendor SDK, and structure the generic
  driver so it can be submitted upstream without carrying BK7258/AIDK code.
- In this multi-repository product workspace, keep every new upstream-oriented
  NuttX/OpenVela driver as canonical source under the matching mirrored path in
  this team repository (for example `nuttx/drivers/...` and
  `nuttx/include/...`).  Expose the coherent overlay with one
  directory-level manifest `linkfile`, following the existing `chips/` and
  `boards/` mapping pattern; do not grow per-file mappings or hard-code the
  team checkout path in Kconfig, CMake or Make files.  Consume the mapped
  overlay through NuttX's external/custom integration points and do not edit
  the checked-out official NuttX/OpenVela repositories directly.  Only
  materialize the same overlay as direct upstream-repository changes when the
  owner explicitly requests an upstream patch, commit or PR.

## Test ownership and upstream integration

- The contest repository is the canonical source for every BK7258 test.  Do
  not edit or copy an official OpenVela test runner merely to add this board.
- When adding or changing test integration, use the matching host, CMocka or
  pytest rules in the [test integration SOP](docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md#test-integration).

## Workspace and manifest ownership

- This contest repository is the only writable submission source.  Keep the
  checked-out official NuttX, OpenVela apps, tests, packages and documentation
  projects free of team-owned tracked edits; expose team-owned trees with
  manifest `linkfile` entries instead.
- When changing manifest mappings or application registration, read the
  [workspace integration SOP](docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md#workspace-integration)
  for discovery boundaries and verification of the materialized mapping.
- Keep the repository layers literal: reusable SoC mechanisms in `chips/`,
  physical wiring and instance policy in `boards/`, upstream-shaped overlays
  in `nuttx/`, product apps in `app/`, host tests in `tests/host/`, target
  tests in `app/testing/`, official-runner pytest children in `tests/pytest/`,
  and the sole public BK7258 CLI in `tools/bk7258/`.
- Do not recreate generic `integration/`, `progress/`, top-level
  `vendorsetup.sh`, copied official source/docs, transient review dossiers,
  generated build trees or compatibility wrappers.  A required vendor build
  hook belongs under `boards/<chip>/build/`; generated artifacts belong under
  the workspace output tree and stay untracked.

## Repository synchronization and checkout hygiene

- The manifest pins repository and SDK identities. Synchronize only missing
  task dependencies; use full-manifest sync for full initialization or an
  explicit full-sync request. Before syncing or resolving checkout conflicts,
  read the [synchronization SOP](docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md#repository-synchronization-and-checkout-hygiene).
  Never force-sync, replace the pinned SDK fork, or discard unrelated user work.

## Change economy and maintainability

- Do not add a script for a one-off inspection, a short documented command or
  an alias to an existing tool.  Prefer a direct command for manual work and
  extend `tools/bk7258/bk7258.py` for a durable public workflow.
- A new script is justified only by a real build-system hook, board automation
  boundary or test-runner contract with a named consumer.  Document that
  consumer, keep policy in the existing Python domain modules, and add a
  mechanically verifiable check.  Delete superseded entry points in the same
  change so two scripts never own one workflow.
- A board `scripts/` directory may retain the NuttX-required `Make.defs` build
  hook.  Do not place product build/sign/package/key-broker/deploy workflows
  beside it; those remain commands/domains of the sole BK7258 CLI, while
  unavoidable button/cable operations are documented as manual fixture steps.
- Optimize for human maintenance: use explicit layer names and small cohesive
  modules, avoid generated-looking wrappers and speculative frameworks, and
  do not preserve an obsolete abstraction merely to reduce the apparent diff.

## Chip, board and shared-resource contracts

- BK7258 is one SoC adaptation serving multiple boards.  Put controller,
  interrupt, DMA, clock and cross-core mechanisms in `chips/bk7258/`; put each
  board's pin map, polarity, reset timing, rail hookup and populated-device
  policy in its own `boards/bk7258/<board>/` directory.  `boards/bk7258/common/`
  may contain only behavior genuinely shared by every consuming board; it is
  not a home for panel, sensor or other generic peripheral drivers.
- Keep one stable machine identifier for each physical board across directory,
  Kconfig, manifest, CLI, test and package paths.  Record marketing, schematic
  or colloquial names as documented aliases; never create a second board tree
  or compatibility script merely because the same board has another name.
- Each board `openvela.conf` selects its CP/AP profiles, partition CSV and one
  release-policy CSV.  Adding a board may add or reuse those declarations but
  must not add a board-name branch to the packaging code.  Flash capacity and
  operator length always come from the selected partition CSV; the release
  policy cannot override geometry.
- Define a chip-wide Kconfig/build requirement once, while every board/profile
  keeps its own explicit defconfig choices.  A configuration fix must be made
  at the narrowest correct owner: shared chip/test contract for all boards,
  board profile for one board, never a copied workaround in three defconfigs.
- Model a physically shared rail, clock or bus as a shared resource.  The chip
  layer may expose a generic module-identity vote/refcount mechanism; the board
  layer maps that resource to its physical GPIO or regulator.  Peripheral
  consumers acquire and release their own votes and must not directly unmap,
  power down or reconfigure a peer's pins or rail.
- Bus protocol belongs to the actual device contract.  Do not retain SPI
  helpers for a UART peripheral, touch optional data pins in one-bit SDIO mode,
  or infer wiring from another board.  Record the schematic-derived mapping in
  the board documentation/config and verify the selected pinmux in the image.
- Apply the handoff policy's board/profile coverage only for requested final
  acceptance. Never claim multi-board support from one successful image.

## Portable paths and build integration

- Follow the official build variables at each layer: prefer `TOPDIR`,
  `APPDIR`, `NUTTX_DIR`, `NUTTX_BOARD_DIR`, `CMAKE_CURRENT_LIST_DIR` and named
  repository/board roots.  Resolve a boundary once and derive all cross-tree
  inputs from that named root; do not scatter repeated `../../..` assumptions.
- Never embed `/home/...`, a team checkout directory name, a Windows user
  path or a remote repository URL as a build dependency.  A single
  script-location-relative root discovery is acceptable at a standalone
  script/test boundary when the official environment provides no root
  variable; downstream paths must still use the resolved root.
- Use compiler include directories and public headers instead of parent-path
  includes in C/C++.  Use the manifest-mapped overlay path derived from
  `NUTTX_DIR`/`TOPDIR`, not the physical contest checkout, when consuming
  team-owned NuttX overlays.
- Do not mechanically replace valid relative paths.  After changing path
  handling, test the supported invocation from outside the source directory
  when applicable and run the affected clean build; portability is an
  observed property, not a spelling rule.

## Documentation structure and lifecycle

- Keep one current truth for mutable state: board/profile support in
  `boards/bk7258/CONFIGS.md`, SoC contracts in `docs/chips/bk7258/`, and the
  platform entry in `docs/platforms/bk7258/README.md`.  Other documents link
  to these sources and must not maintain a second current-status table,
  roadmap or next-stage pointer.
- For documentation edits or cleanup, use the [maintenance rules](docs/README.md#文档维护).
  Moving or deleting a document requires checking its affected incoming links
  and claims, not a whole-repository link or status audit.

## Code style and comment conventions

- Governing style per directory (do not apply one formatter to the whole
  repository):
  NuttX-shaped team modules (`chips/`, `boards/`, `nuttx/`, `app/` sources that
  the NuttX build compiles) follow the pinned NuttX C coding standard; other
  OpenVela C/C++ follows the repository's `clang-format` 14 configuration;
  Android follows the existing Kotlin/Gradle setup; Python follows PEP 8,
  Shell/CMake/Make/Kconfig follow their own conventions.
- Check entry points and the versions used for the current pass:
  `nuttx/tools/nxstyle` and `nuttx/tools/checkpatch.sh` from the pinned NuttX
  checkout (record its commit), `clang-format` 14.0.6 read-only
  (`--dry-run -Werror`), `black` 24.10.0 for Python, plus
  `git diff --check`.  No `shellcheck`, `ktlint` or `cmake-format` exists in
  this environment; state that limit instead of claiming a pass.
- Check tool versions are recorded separately from the compiler toolchain and
  compile-time selection.  Do not upgrade the toolchain, SDK or a dependency to
  make a style check pass.
- Team explanatory comments are written in English, state why the code is
  written this way, and keep hardware semantics (pin, level, unit, register
  field, errata) plus interrupt, locking, ownership, lifecycle, cancellation,
  cache-coherence and cross-core constraints.  A comment that no longer matches
  the code is corrected, not translated.
- Content that stays in Chinese even inside team source: user-visible UI and
  localized resources, product wake/reply text, log strings that existing tools
  parse, training labels and test samples, runtime Skill text and prompts,
  Chinese reports, `README` prose and the original `logs/` trees.
- Never reformat third-party originals, vendor SDK copies, generated files
  (model arrays, generated headers), binary assets, private corpora or the
  rest of the OpenVela workspace reached through a `linkfile`; edit only the
  team-owned file in this repository and never the official checkout.
- Separate a real interface change from style work.  Renaming a public
  function, ABI, Kconfig symbol or protocol field is a functional change: it
  carries its own review and verification and never rides inside a style
  commit.  Do not silently change locks, release order, error contracts,
  scopes or lifetimes while "tidying" formatting.
- Keep Makefile recipes tab-indented and preprocessor continuations intact;
  `.editorconfig` exists so editors honour the per-file-type rules.

## Source provenance and acceptance

- Every new source file must state its license.  Record the exact upstream or
  SDK repository/version/path and license for copied or source-derived tables,
  protocols and initialization sequences in `SOURCE_PROVENANCE.md`; preserving
  an original contest template unchanged is an explicit, documented exception.
- Do not claim complete SPDX coverage, an official driver reuse, a clean
  official repository or a board build without checking it in the current
  worktree.  Source counts include intended untracked additions and exclude
  deleted paths, ignored SDK/toolchain payloads and AI logs.
- Debug-artifact and final-acceptance handoffs have different gates. Use the
  hardware-fast loop above for debug iteration. For final source acceptance or
  any downloadable artifact, read only the matching stage in the maintained
  [handoff policy](docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md#handoff-gates-by-stage).
- Always preserve the exact target's data, trust and artifact-identity gates
  for a download. Build, package, transport, boot and functional acceptance
  are separate claims; a diagnostic BIN is not a verified signed release.
