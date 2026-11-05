## Project entry points

- Start with `README.md` for the product and source layout,
  `docs/platforms/bk7258/README.md` for architecture and ownership,
  `boards/bk7258/CONFIGS.md` for board/profile acceptance, and
  `docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md` for operations.
- Keep mutable facts in those maintained sources. Do not recreate a parallel
  project-memory tree, current-progress file or root task-plan document.
  Dated evidence belongs in `docs/verification/bk7258/`; retired plans and
  architecture decisions remain available in Git history.
- Historical documents and task instructions do not authorize a new hardware,
  trust, publication or destructive operation. Use the current owner's scope.
- Preserve the remaining organizer-provided scaffold during repository cleanup:
  `app/hello_app`, `quickapp/hello_quickapp`, their manifest mappings, CLA workflow,
  `.gitignore.example`, and the AI-log submission structure. A sample not used by
  the product is not automatically a cleanup target.

## Git publication ownership

- For an authorized commit or push, the primary agent reviews the exact diff,
  stages only intended paths, and verifies the configured fork branch SHA
  against local `HEAD` after pushing. Preserve unrelated work and the index.
- Force-push, remote branch deletion, direct upstream/default-branch push and
  publication to another repository each require action-specific authority.
- PR creation, editing, closing and merging remain with the owner unless the
  owner explicitly requests that PR action. Read-only branch/PR/check inspection
  does not grant permission to mutate remote state.
- Provide a Chinese PR title and body when a publication handoff is requested.
  Do not add PR prose to the source repository.
- Before publication, fetch the authoritative upstream base. Start from that
  base, verify it is an ancestor with zero commits behind, and verify the exact
  cross-fork comparison. A stale fork branch is not a publication base.
- Compare patch or tree identity before replaying work already merged under
  another commit ID. Keep rebase-only submissions linear; do not add an `ours`
  merge or duplicate changes to hide divergent ancestry. Use a new clean feature
  branch when published ancestry needs repair unless force-push is authorized.
- Verify the repository's permitted merge method and checks for the exact head
  before an authorized merge.

## BK7258 trust safety

- During active BK7258 work, do not use N17 or another historical trust domain as a source, baseline, key candidate, or fallback unless the owner explicitly reactivates it.
- A maintained signing identity is referenced explicitly by its public fingerprint and an approved secure local path, secret-manager, or HSM reference. Do not generate, rotate, destroy, print, or place private key material in the repository, logs, or temporary automation unless the owner explicitly authorizes that identity operation.
- A missing private signing key blocks only the signing/release step. It does not block source review, configuration, ordinary incremental builds, host regression, or unsigned diagnostic artifacts; state the blocked signing input precisely.
- Reuse unchanged boot components and sealed signed artifacts when content,
  configuration, toolchain, trust identity, protected metadata and dependencies
  still match. Use the existing CP/AP OTA signing path without re-signing BL1/BL2.
  A changed version, counter, TLV or pair dependency invalidates the affected
  signed artifact; a matching raw payload alone does not establish reuse.
- Before any full download, validate the selected signed package's board identity, signer reference, rollback counter, declared Flash size and protected tail against the retained accepted image input. Do not require or perform a new device Flash readback/backup as a preflight gate. The apps-only path remains bound to its installed public trust contract and exact target fingerprint.
- The AIDK 8-MiB full-image path reuses the owner's retained factory image only when its size, hash, same-board binding and protected tail match. This is image-input integrity, not a demand to reacquire a backup. Do not ask again for configuration-rollback approval; never copy target-bound image or device-unique data to another unit.

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
  rollback and boot evidence path. This is an explicit specialist
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
  release-policy CSV.  Partition layout CSVs are maintained in
  `boards/bk7258/common/partitions/bk7258/`; a board directory selects one and
  does not keep a private copy.  Adding a board may add or reuse those declarations but
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

## Evidence, artifacts and deletion

- Where to look: layer and workflow rules here; current support in
  `boards/bk7258/CONFIGS.md`; platform and SOP entry in
  `docs/platforms/bk7258/README.md`; SoC contracts in `docs/chips/bk7258/`;
  dated hardware/host verification in `docs/verification/bk7258/`; host test
  records in `tests/host/bk7258/acceptance/`; historical designs in
  `docs/platforms/bk7258/nuttx-port/`; raw serial captures of early bring-up
  in `logs/bk7258-*`; AI coding logs in `logs/<github_login>/`.
- A tracked verification record must not cite the workspace `out/`, an
  ignored directory or `/tmp` as the only copy of evidence it depends on.  Copy
  the reviewed raw hardware evidence (serial, session, HIL result) into
  `docs/verification/<soc>/<date>-<topic>/` or
  `tests/host/bk7258/acceptance/<id>-evidence-<date>/` with its SHA-256 when
  the record is written; generated host results may instead cite the source
  revision and command that regenerates them.  State unrecoverable evidence
  explicitly; never recreate it.
- Build trees, per-run contract outputs and variant `build-*` trees are local
  regenerable output: keep them ignored, never cite them, and remove them when
  their run is finished.  Retain the default incremental build directory, the
  locked toolchain, SDK bundles and private datasets.
- An experiment-only helper may stay untracked while its experiment runs.
  Before a committed record depends on it, put reusable workflows in a `_lib`
  domain behind `bk7258.py` or the owning Windows tool group. Record-specific
  host reproduction harnesses belong in `tests/host/<soc>/`, with their source
  revision and command documented. Remove obsolete entry points in the same
  change.
- Before deleting a tracked or user-owned file, check references (docs, code,
  build files, CI, manifest `linkfile`, acceptance JSON), build and test
  consumers, uncommitted edits and historical value.  Delete only with that
  evidence; otherwise report the uncertainty and leave the file in place.

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
