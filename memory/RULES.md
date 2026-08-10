# Project Rules

Last reviewed: 2026-08-10

## Domain invariants

- Official NuttX/apps/SDK source and SDK static libraries are read-only deliverable inputs. Use team-owned wrappers; see [ADR-001](decisions/ADR-001-wrapper-only-official-source-boundary.md).
- BK7259 and Beken `release/v4.0.1` are retired for this project. Do not use
  them as source, build input, test input, validation target or architecture
  precedent. The only active BK7258 runtime SDK is official v3.1.1.9. The
  official BK7236/BK7258 `bk_idk release/v2.0.1` secureboot material may be
  inspected as a read-only BL1/BL2/TF-M reference; it is not a replacement
  runtime SDK or static-library bundle.
- CP is the sole owner of flash/LittleFS, the Beken Bluetooth Controller, AP lifecycle, and PSRAM hardware initialization.
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
- N17 reserves Manifest A at `0x50b000..0x50c000`, Manifest B at
  `0x50c000..0x50d000`, and the one-way authentication policy at
  `0x50d000..0x50e000`. The generic partition wrapper must reject writes and
  erases to all three. Only future dedicated lifecycle code may update the
  inactive slot's Manifest under ADR-009; normal firmware must never mutate
  the policy sector.

## Permissions and ownership

- Temporary diagnostic edits to official trees are allowed only for debugging and must be removed before a checkpoint.
- Do not commit, push, open a PR, flash a destructive factory image, or mutate external repositories unless the user grants the corresponding authority.
- The owner grants the primary agent standing authority for bounded,
  recoverable hardware validation: accepted-layout sparse firmware updates,
  UART capture/commands, read-only J-Link inspection, and normal hardware
  reset may run without asking again.  This does not authorize chip/factory
  erase, layout migration, calibration/LittleFS destruction, OTP/eFuse or
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
- The BL1 development manifest packer must reject a private key that does not
  derive to the compiled board-owned development root. Any merged
  secure-boot image produced for pipeline comparison remains a
  host-reference-only artifact until BK7258 BootROM acceptance and the exact
  AES/CRC consumer are independently proven; it must not trigger OTP/eFuse
  writes or be described as production-bootable.
- The accepted N17 CSV/generated layout and public vector are host artifacts,
  not firmware or board-write authority. The current board remains unarmed and
  runs N15 format 2 until a separately reviewed implementation and migration
  are explicitly authorized.

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
- Proprietary SDK archives remain ignored and must not be redistributed; only manifests/provenance metadata are versioned.
- Do not claim secure BLE, power-loss safety, cache coherency, or production SLA without a dedicated accepted stage and evidence.
- N17-S may claim signed OTA publisher authentication and software downgrade
  prevention only after its implementation gates pass. Until BootROM secure
  boot, OTP trust anchoring and a trusted monotonic counter are separately
  provisioned and verified, do not claim hardware-rooted secure boot,
  complete-Flash attack resistance or hardware-backed anti-rollback.

## User-experience conventions

- Do not launch `BLEDebug.EXE`; the project owner reports that it makes the Windows host unusably slow. Use repository no-GUI BLE tooling.
- UART and test tools emit stable machine-readable PASS/FAIL lines with bounded timeouts.

## Engineering conventions

- The default, owner-revocable model division is: `gpt-5.6-sol` at `xhigh`
  owns project planning, architecture, task decomposition, review,
  integration decisions and final acceptance. Boundary-clear implementation
  and focused tests may be delegated to `gpt-5.6-luna` at `max`, or to
  CodeBuddy `hy3` with effort `max`. Every delegation must state exact file
  and behavior scope, forbidden actions, acceptance criteria and expected
  evidence; the primary agent reviews the returned diff and evidence before
  accepting or integrating it. Delegation does not expand any permission in
  this file, especially official-source, hardware, Flash, OTP/eFuse, commit,
  push or architecture authority. The owner may cancel or replace this model
  division at any time; on cancellation, stop new delegation and safely halt
  active delegated work at the next non-destructive boundary.
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
  source changes, commits, pushes, PRs, and final acceptance remain with the
  primary agent unless the owner explicitly expands that authority.
- At a workspace root containing `.codegraph/`, use CodeGraph before grep/file reading when locating or understanding code.
- Use `rg`/`rg --files` for text and file discovery when CodeGraph does not cover the team repository.
- Use `apply_patch` for manual file edits and preserve user-authored changes.
- Update stage docs and `progress/CURRENT.md` after material implementation, verification, commit, deployment, rollback, or blocker changes.
- Keep `memory/INDEX.md` and `progress/CURRENT.md` concise; route completed detail to milestones and verification records.
