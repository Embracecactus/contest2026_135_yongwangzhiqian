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
- A debug-artifact handoff is not a final project handoff.  Defer broad host
  regression, unrelated-board clean builds, documentation and provenance
  audits, official-checkout audits, ZIP/release assembly and other final
  acceptance work until the owner reports that the hardware path passes.  Do
  not introduce speculative tests merely to delay the next hardware attempt.
- Mandatory device-data and trust protections still apply.  If an iteration
  requires a whole-device BIN, perform only the minimum required accepted-base,
  fresh-key, signature, rollback and exact-Flash-size checks, then hand off the
  BIN without unrelated gates or a ZIP.  Prefer the installed apps-only path
  when its existing trust contract permits the affected CP/AP update.
- Exit hardware-fast mode only when the owner explicitly requests final
  acceptance/release work or reports that the relevant hardware behavior has
  passed.  Then run the deferred regression, multi-board, provenance and final
  delivery checks before claiming completion.

## BK7258 architecture

- Before using an old implementation as design input, define the target public commands, internal domain boundaries, authoritative source for each mutable fact, and deletion set. Stop at architecture analysis if any is unknown.
- Historical scripts, schemas, tests, and documents are evidence, not requirements. Preserve behavior only when a current build, package, verification, or hardware path consumes it; do not create one-file compatibility moves.
- `tools/bk7258/bk7258.py` is the only tracked public entry.  Its top-level commands are `build`, `toolchain`, `sdk`, `package`, `release`, `deploy`, and `verify`; nested commands are `toolchain install|verify`, `sdk list|verify|install|rebuild`, `package accept-base|create|delivery|extract|flash-contract|materialize`, `release full|ota|product`, and `verify layout|image|build-manifest|package|delivery|trust`.  Domain implementation belongs under `_lib`.
- The team manifest owns SDK/toolchain identity, CP/AP profiles own board/role compatibility, `--boot` is explicit input, and the selected partition CSV owns geometry, topology, roles, and build/write policy.  The board-selected release-policy CSV owns only product update semantics (`replace`, `preserve`, `device-unique`, `transactional`, `factory-init`, `immutable`) and must cover every partition by name without repeating offsets or sizes.  Consumers must not duplicate these facts.
- Descriptor-only board extension applies to the current BK7258 dual-core BL1/BL2 and MCUboot A/B product model.  A different boot/update model requires a separately reviewed product-mode contract; never implement it as a physical-board-name branch or silently reinterpret the current `boot`/`cp`/`ap`/BL2/manifest/`persistent_data` contract.
- Accept cleanup only after reporting deleted layers, confirming tracked top-level file count did not grow, and checking for duplicate version, profile, path, layout, or build-policy truths.

## Upstream-oriented peripheral drivers

- This rule applies to every external peripheral class, including displays,
  sensors, storage, NFC, audio codecs, chargers, cameras and future devices.
- Before implementing a peripheral driver, search the fully synchronized
  NuttX/OpenVela trees first.  Reuse an existing standard driver and ABI when
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
- Keep Linux-native mock and sanitizer regression under `tests/host/`; run it
  directly from the contest checkout and never map its host Makefile into the
  official OpenVela `tests/` application tree.
- Keep target CMocka applications under `app/testing/`, mirroring their
  official `apps/testing/` destination and structured with the
  official Kconfig, Make.defs and Application.mk contracts.  Expose the whole
  application directory through one manifest linkfile below the official
  `apps/testing/` auto-discovery point; do not route a test application through
  `external/` when `apps/testing/` already provides Kconfig, Make and CMake
  child discovery.  This keeps the directory ready to move upstream without a
  product-only build wrapper.
- Add board serial automation as a linked child below the official pytest
  `tests/scripts/script/` tree.  Reuse its parent fixtures and UART0 control
  channel; do not fork `conftest.py`, `utils/common.py` or `pytest.ini`.

## Workspace and manifest ownership

- This contest repository is the only writable submission source.  Keep the
  checked-out official NuttX, OpenVela apps, tests, packages and documentation
  projects free of team-owned tracked edits; expose team-owned trees with
  manifest `linkfile` entries instead.
- A `linkfile` source is relative to this project root and its destination is
  the official workspace discovery location.  Link one coherent directory,
  not individual implementation files, and verify both the manifest source
  and the materialized destination symlink after changing it.
- Preserve the generated contest examples in `app/hello_app` and
  `quickapp/hello_quickapp` as template examples.  Product commands and
  features belong in a separately named directory such as `app/bk7258` with
  its own manifest mapping; never turn the hello example into a product-app
  compatibility container.
- Map a NuttX built-in product command below an official application discovery
  boundary that actually recurses through CMake, Kconfig and Make, such as
  `apps/system/bk7258`.  Do not place such a command below `packages/demos`
  merely because the generated hello template uses that package location.
  After changing an application mapping or symbol, verify the resolved
  `.config` and the ELF/builtin table; a symbol present only in the seed
  defconfig is not integrated.
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

- Treat the manifest as the repository inventory.  Before syncing, inspect the
  materialized projects, remotes and pinned revisions.  Do not re-download an
  already complete project merely because it also exists upstream.
- If a manifest project is missing, synchronize the complete manifest with
  normal, non-forced `repo sync`; do not keep a hand-picked partial workspace
  and do not use `--force-sync`.  A China-hosted mirror may be used when the
  original host is slow only when it serves the same repository and object
  identity; the manifest revision and post-sync commit verification remain
  mandatory.
- A sync conflict in an official checkout is evidence that a team adaptation
  escaped its owner.  Move the adaptation into this repository and expose it
  through the supported manifest/build extension, then clean only the verified
  accidental team-owned checkout change and retry.  Never discard unrelated
  user work, explicitly excluded OpenAMP experiments or unknown untracked
  files as part of bulk cleanup.
- The manifest-pinned Beken SDK identity is authoritative and may point to the
  owner's fork.  Do not silently replace that fork with the vendor upstream or
  rebuild an installed bundle from an arbitrary local SDK tree.  Verify remote,
  commit, profile and provenance first; synchronize or rebuild only when the
  pinned project or required object is actually absent or invalid.

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
- A change under shared chip/common/test/build code affects all three BK7258
  boards and requires clean-build coverage for each supported board/profile.
  A board-only wiring change requires that board's clean build plus the shared
  host regression; never claim multi-board support from one successful image.

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
- `docs/verification/bk7258/` contains dated, immutable acceptance evidence;
  `docs/learning/bk7258/` contains version-labelled teaching material; neither
  is a live task tracker.  Preserve unique hardware/reverse-engineering
  evidence, but delete copied upstream manuals, completed prompts, worklogs,
  superseded plans, duplicate reviews and documents whose only purpose was to
  describe a retired intermediate layout.
- Follow the official Markdown convention for links inside one documentation
  tree.  For a cross-tree source/config reference that would require three or
  more parent traversals, prefer a repository-root logical path in code font,
  such as `boards/bk7258/CONFIGS.md`, instead of an absolute checkout path or
  repository-name-bound URL.  Never create placeholder files only to keep an
  obsolete link alive.
- After moving or deleting documentation, remove empty directories, repair
  every retained local Markdown link, scan for stale `current`, `progress`,
  prompt and old-board claims, and report the deleted layers and the evidence
  categories deliberately retained.

## Source provenance and acceptance

- Every new source file must state its license.  Record the exact upstream or
  SDK repository/version/path and license for copied or source-derived tables,
  protocols and initialization sequences in `SOURCE_PROVENANCE.md`; preserving
  an original contest template unchanged is an explicit, documented exception.
- Do not claim complete SPDX coverage, an official driver reuse, a clean
  official repository or a board build without checking it in the current
  worktree.  Source counts include intended untracked additions and exclude
  deleted paths, ignored SDK/toolchain payloads and AI logs.
- Before handoff, run `git diff --check`, the host BK7258 regression and header
  audit, validate all manifest links and local documentation links, confirm
  official repositories have no team-owned tracked edits, and clean-build
  every affected board/profile.  For a downloadable artifact, also verify the
  build manifest, required ELF symbols and the resulting package with the sole
  BK7258 CLI.
- Build and package are separate acceptance gates.  Always hand off the exact
  package path, board/profile/boot identity, manifest identity and verification
  result; do not call a loose pair of binaries a verified full package.  A
  direct or unsigned diagnostic artifact must be labelled as such and must not
  be presented as a fresh-key full-trust download.
- Every whole-device download handoff must include one dense, single-input
  operator `.bin` for Flash offset zero whose size equals the complete Flash
  capacity declared by the selected partition CSV.  A `.bkpack`/`.bkpkg`,
  `pair.bin`, sparse fragments or a build directory is supporting evidence,
  never the sole downloadable artifact.  Do not concatenate, pad or archive
  release bytes outside the maintained CLI.
- A normal wired recovery is device-bound: materialize it from one exact
  complete readback of that same unit, replace firmware partitions, reset only
  transactional state, and preserve `preserve`, `factory-init`,
  `device-unique`, `immutable` and unmapped bytes.  Its ZIP must record the
  canonical `package accept-base` evidence binding the hash/size to the board,
  layout, stable device ID and capture method, and warn that it cannot be
  copied to another unit.  A caller-supplied raw hash alone is not acceptance
  evidence; the operator or controlled fixture remains responsible for proving
  which unit produced the readback.
- A universal factory image is a different product.  Generate one only when a
  reviewed manufacturing provisioner assigns per-device MAC/RF/Bluetooth and
  calibration state.  Until then, the product manifest must say
  `requires-provisioning`; never turn a board readback or an all-`0xff` tail
  into a purported universal factory image.
- Direct diagnostics use `package delivery` with an explicit version, complete
  base and accepted-base evidence.  Signed full/OTA releases are combined with
  `release product`; the resulting verified ZIP contains `release.json`,
  `SHA256SUMS`, `FLASHING.md`, accepted-base/build/release-policy evidence, the complete
  device-bound recovery BIN and any compatible OTA package.  OTA updates only
  CP/AP and state their accepted source version plus the root required on the
  source device.  Wired recovery separately states the new root it installs;
  these roots may differ during intentional key rotation.  OTA never claims to
  be a whole-device download.  A root/BL2/layout transition remains a wired
  full recovery rather than an apps-only OTA.
- Publish release directories and product ZIPs atomically with no-replace
  semantics; an existing target, including an empty directory, is an error.
  A copied `evidence/build-manifest.json` must validate against its packaged
  target/layout/security facts without depending on the original `out/` path.
  Only initial release construction may consume the path-bound build handoff.


<claude-mem-context>
# Memory Context

# [contest2026_135_yongwangzhiqian] recent context, 2026-08-31 11:30am GMT+8

No previous sessions found.
</claude-mem-context>
