# Project Rules

Last reviewed: 2026-08-21

## Domain invariants

- Official NuttX/apps/SDK source and SDK static libraries are read-only deliverable inputs. Use team-owned wrappers; see [ADR-001](decisions/ADR-001-wrapper-only-official-source-boundary.md).
- BK7259 and Beken `release/v4.0.1` are retired for this project. Do not use
  them as source, build input, test input, validation target or architecture
  precedent. The only active BK7258 runtime SDK is official v3.1.1.9. The
  official BK7236/BK7258 `bk_idk release/v2.0.1` secureboot material may be
  inspected as a read-only BL1/BL2/TF-M reference; it is not a replacement
  runtime SDK or static-library bundle.
- CP is the sole owner of on-chip Flash, the Beken Bluetooth Controller, AP
  lifecycle and PSRAM hardware initialization. Persistent storage topology is
  a system choice; no application owns its medium or partition.
- AP is the sole stock NuttX Bluetooth Host/GAP/GATT owner and a PSRAM consumer only.
- AP remains one native SMP cluster and one RPTUN peer; physical CPU2 is not a second peer.
- N14 upper 8 MiB PSRAM is boot-tested/reserved, not a general heap. The only
  exception is the N15-F validation profile's fixed volatile transfer window
  `0x60800000..0x60a76200`; it creates no allocator or persistence contract.
  See [ADR-002](decisions/ADR-002-n14-psram-ownership-and-layout.md).
- N15 uses the accepted official-style contiguous A/B layout in
  [ADR-004](decisions/ADR-004-n15-official-contiguous-ab-layout.md). The
  ADR-003 sector-swap addresses, metadata ABI, and scratch path are retired
  and must never be enabled or flashed.
- Every maintained CSV explicitly declares Manifest A/B and BL2 A/B. No code
  may infer the B address from a gap. Normal application firmware cannot write
  those ranges; a future updater needs a separately reviewed inactive-slot
  lifecycle and exact-range authority.

## Permissions and ownership

- After a change is merged, all new contest development starts from the main
  contest repository's latest `dev-ai-contest-2026`.  Do not continue
  development on an already merged fork branch or auxiliary worktree; a fork
  is publication transport only when the contest workflow requires it.
- Active development, review and verification use only the primary contest
  repository checkout.  Do not create or use `.worktrees/`, auxiliary
  worktrees, or workspace clones as implementation or evidence sources unless
  the owner explicitly reactivates that workflow.
- Temporary diagnostic edits to official trees are allowed only for debugging and must be removed before a checkpoint.
- The standing Git publication authority is defined below. Do not mutate any
  other external repository state, flash a destructive factory image, or take
  an irreversible action unless the owner grants that separate authority.
- The owner grants the primary agent standing authority for bounded,
  recoverable hardware validation: accepted-layout sparse firmware updates,
  UART capture/commands, read-only J-Link inspection, and normal hardware
  reset may run without asking again.  This does not authorize chip/factory
  erase, layout migration, calibration/persistent-data destruction, OTP/eFuse or
  security-lifecycle writes, debug locking, or any other irreversible action.
- Preserve unrelated dirty or untracked work. Resolve exact targets before any destructive operation.
- The owner-authorized one-time N15 layout migration and LittleFS reset was
  consumed successfully on 2026-08-03. It did not authorize chip erase or
  recurring factory writes; any later destructive Flash action needs fresh
  authority. Project tools must preserve raw `0x7fa000..0x800000`.
- The former N15/N17 custom OTA selector, writer, journal, validation profile
  and campaign are retired. Do not restore or reconstruct them as a second
  update framework. Any future field updater must start from the pinned NuttX
  MCUboot semantics and obtain fresh, range-specific Flash-write authority.
- Secure Boot development must keep the board recoverable while later drivers are
  adapted. OTP/eFuse writes, secure-boot enable, lifecycle-state changes,
  public-key-hash/security-counter provisioning and JTAG/debug locking are
  forbidden unless the owner later grants a new field-specific irreversible
  operation scope after the N17-H evidence gates in
  [ADR-008](decisions/ADR-008-n17-phased-ota-authentication.md) pass. Read-only
  capability/source inspection is allowed. No normal build, script or test
  may perform these writes implicitly.
- `trust.py` must reject a private key that does not derive to the compiled
  build-local BL1/BL2 public root. Any merged
  secure-boot image produced for pipeline comparison remains a
  host-reference-only artifact until BK7258 BootROM acceptance and the exact
  AES/CRC consumer are independently proven; it must not trigger OTP/eFuse
  writes or be described as production-bootable.
- A generated layout and public vector are host artifacts, not board-write
  authority. A layout-identity change requires explicit migration or full
  reflash and cannot be presented as an ordinary OTA update.
- Every MCUboot package embeds public-only evidence bound to actual BL1/BL2
  roots and image bytes; there is no separate trust contract. Before any
  normal MCUboot download,
  non-halting J-Link reads must match the target's existing BL1 Manifest and
  BL2 MCUboot public fingerprints.  J-Link failure or failure to match every
  identity at one permitted address fails closed before `bk_loader`; normal
  download has no bypass and may not rotate trust roots.  The contract, logs
  and project memory must never contain private-key material or private-key
  paths.  This preflight does not grant authority to write boot-chain,
  OTP/eFuse or lifecycle ranges.
- Trust identities live in linker-owned BL1/BL2 sections. Package verification
  locates and validates their bytes; source code does not carry developer or
  legacy probe addresses. A match authorizes only the requested normal
  download, never an implicit boot-chain rewrite.

### Git publication ownership

- For authorized project development, the primary agent owns the complete
  local commit and remote feature-branch publication workflow: review the
  exact diff, stage only intended paths, commit it, push the current feature
  branch to the configured `fork`, and verify that the remote branch SHA
  exactly matches local `HEAD` before handoff.
- This standing authority does not allow force-push, remote branch deletion,
  direct push to the upstream/default branch, or publication to another
  repository. Those actions require fresh, action-specific owner authority.
- The primary agent provides a copy-ready Chinese PR title and body directly
  in the conversation. Do not store PR prose in the repository or a temporary
  file unless the owner explicitly requests a file.
- The project owner exclusively creates, edits, reviews and merges PRs. The
  agent must not create, reopen, edit, close or merge a PR unless the owner
  explicitly requests that specific PR action in the current turn.
- Read-only remote inspection is allowed when needed to confirm branch
  publication or report PR/check state; it must not mutate the PR.

## Failure and recovery behavior

- Initialization, transport, allocator, or AP startup failures fail closed and must not be converted into READY with a warning.
- Every wait in board diagnostics and lifecycle control is bounded.
- Stale AP/RPMsg state is generation-scoped; rollback uses a verified prior profile/artifact rather than reusing stale state.
- After a future authorized N17 migration, any non-`0xff` byte anywhere in the
  policy sector means armed. Armed boot accepts only format 3 and a matching
  signed Manifest plus verified CP/AP payload; it must never fall back to
  format 2 or header-only slot A.
- BKFIL Flash read-back used as acceptance evidence must run at 115200 and be
  repeated until two captures are byte-identical. A 6 Mbps read may contain
  injected 128-byte zero blocks and is never a bit-exact recovery image.

## Security and privacy rules

- Never record passwords, tokens, private keys, session cookies, or personal data in repository memory.
- Proprietary SDK archives remain ignored and must not be redistributed; only
  SDK profiles and accepted deterministic tree hashes are versioned.
- Local BK7258 SDK bundles live only under the ignored canonical
  `board/bk7258/bk_idk/armino_as_lib/versions` store as real directories.
  Active tooling must reject bundle symlinks and the retired
  `board/bk7258_t5ai` source root. The canonical SDK source and base version
  are derived from the single team-manifest project carrying the
  `bk7258-sdk` group; `bk7258.py sdk rebuild --source` is the only
  developer-supplied migration override.
- Active SDK roles and variants are derived from the maintained profile files
  for the manifest-selected base version. There is no legacy bundle fallback
  or recovery selection, and memory must not duplicate the selected version,
  checkout path or profile filenames.
- Do not claim secure BLE, power-loss safety, cache coherency, or production SLA without a dedicated accepted stage and evidence.
- N17-S may claim signed OTA publisher authentication and software downgrade
  prevention only after its implementation gates pass. Until BootROM secure
  boot, OTP trust anchoring and a trusted monotonic counter are separately
  provisioned and verified, do not claim hardware-rooted secure boot,
  complete-Flash attack resistance or hardware-backed anti-rollback.

## User-experience conventions

- Do not launch `BLEDebug.EXE`; the project owner reports that it makes the Windows host unusably slow. Use repository no-GUI BLE tooling.
- UART and test tools emit stable machine-readable PASS/FAIL lines with bounded timeouts.
- For long firmware builds, flashing and hardware captures, poll in short
  bounded intervals and report only stage transitions, the current blocker,
  root-cause evidence and the final result. Do not replay large compiler,
  packer or downloader logs unless a specific excerpt is needed to explain a
  failure.

## Engineering conventions

- BK7258 refactors are target-architecture-first. Old framework, registry,
  catalog, schema, test or wrapper files are not requirements and must not be
  migrated one-for-one. Preserve only behavior consumed by a current real
  build/package/verification/hardware path.
- `tools/bk7258/bk7258.py build|sdk|package|verify` is the only tracked public
  BK7258 tool surface. SDK and toolchain identities come from the team
  manifest, board/role compatibility from selected CP/AP profiles, boot mode
  from an explicit command input, and all Flash/storage geometry plus policy
  from the selected CSV. No consumer may
  repeat those facts as version/address/profile conditionals.

- Before the first source, build or hardware action after a resumed session,
  read `memory/INDEX.md`, then `progress/CURRENT.md`, then only the active
  verification record linked there.  Historical N15/N17 records must not be
  used to fill a current-context gap.  If Git or an owner-reported merge makes
  `CURRENT.md` stale, reconcile that snapshot before exploring an unrelated
  historical phase.
- Trust identity is established by the current phase's reviewed public
  fingerprints, never by a filename.  A missing temporary filename must not
  be converted directly into an owner blocker: first trace the agent-created
  artifact's current-project provenance and compare only explicitly approved
  same-domain candidates by public fingerprint.  Never inspect an unrelated
  trust domain, generate a replacement or rotate roots as a fallback.  A
  public contract or signed artifact proves identity but cannot sign a new
  image.
- The merged public-contract/J-Link preflight is the only normal BK7258
  download trust gate.  Do not add a parallel key resolver or infer trust from
  a path: the build binds public fingerprints to actual BL1/BL2 ELF and image
  bytes, and every MCUboot download must match those identities on the target
  before `bk_loader`.  Repository memory may record public fingerprints and
  verification conclusions, but not private material or private-key paths.
- Hardware-facing changes must be validated on the real board as soon as a
  recoverable test image is available.  Do not grow one-off verification
  scripts or a parallel validation framework merely to avoid direct board
  testing; reuse the existing build/flash/debug path and add only the smallest
  unit test or observable diagnostic needed to isolate a failure.
- The default, owner-revocable model division uses one agent unless a task is
  independent and bounded. `gpt-5.6-sol` at `high` owns requirements,
  architecture, ambiguous root causes, integration decisions and final
  acceptance. `gpt-5.6-terra` at `medium` may handle read-heavy exploration,
  logs, ordinary review and evidence gathering. `gpt-5.6-luna` at `low`,
  `medium` or `max`, or CodeBuddy `hy3` at a proportionate effort, may handle
  narrow, repeatable and mechanically verifiable checks or small implementation
  work. Prefer Luna `max` for bounded but non-trivial edit/check combinations
  and lower effort for simple inventories.
  Run no more than two subagents concurrently, do not parallelize dependent
  tasks or overlapping edits, and reuse an existing thread for follow-ups.
  Every delegation must state exact file and behavior scope, forbidden
  actions, acceptance criteria and expected concise evidence; the primary
  agent reviews the returned diff and evidence before accepting or integrating
  it. Delegation does not expand any permission in this file, especially
  official-source, hardware, Flash, OTP/eFuse, commit, push or architecture
  authority. The owner may cancel or replace this model division at any time;
  on cancellation, stop new delegation and safely halt active delegated work
  at the next non-destructive boundary.
- UART capture, reset-synchronized UART capture, COM-port enumeration and
  guarded J-Link diagnostics must use the repository-owned
  `tools/windows-hardware-debug` toolkit by default. Delegate these bounded
  evidence-collection runs to `ask_hy3` when available. The primary agent must
  first freeze the exact ports, baud, allowed control action, duration,
  positive/negative evidence expressions and output directory, then review
  `serial.raw` and `session.json` itself before accepting the result.
  Delegation never grants Flash/erase/PSRAM-memory writes, extra resets,
  arbitrary J-Link command files, source edits, commits or pushes. Those need
  their own explicit scope and remain primary-agent responsibilities unless
  the owner says otherwise.
- Simple, boundary-clear implementation code may be delegated to `ask_hy3`
  with explicit file scope, acceptance criteria, and mutation authority. The
  primary agent reviews and integrates the result and performs final
  verification; architecture decisions, hardware/Flash operations, official
  source changes, commits, feature-branch pushes and final acceptance remain
  with the primary agent. PR actions remain owner-owned under the publication
  rule unless explicitly requested in the current turn.
- At a workspace root containing `.codegraph/`, use CodeGraph before grep/file reading when locating or understanding code.
- Use `rg`/`rg --files` for text and file discovery when CodeGraph does not cover the team repository.
- Use `apply_patch` for manual file edits and preserve user-authored changes.
- Update stage docs and `progress/CURRENT.md` after material implementation, verification, commit, deployment, rollback, or blocker changes.
- Keep `memory/INDEX.md` and `progress/CURRENT.md` concise; route completed detail to milestones and verification records.
