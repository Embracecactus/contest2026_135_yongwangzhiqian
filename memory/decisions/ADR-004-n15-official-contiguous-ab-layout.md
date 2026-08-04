# ADR-004: Adopt the official-style contiguous CP/AP A/B layout

- Status: Accepted
- Date: 2026-08-03
- Decision owner: Project owner

## Context

N15 requires CP and AP to update and roll back as one generation. Exact Beken
SDK v3.1.1.9 source and binary analysis proved that the official AB
bootloader uses one Flash-controller offset to remap one contiguous primary
CP/AP span to an equal-sized contiguous `s_app` span. The N14 layout placed
LittleFS between CP and AP, so it could not use that mechanism without a
layout migration.

ADR-003 proposed preserving the N14 layout with a journaled physical-sector
swap. Its read-only R2 evidence passed, but the design is substantially more
complex and slower than hardware remap. It also concentrated one erase per
pair sector on a single scratch sector: a 783-sector pair would erase that
scratch about 783 times per complete swap. Before ADR-003 was accepted or
used on hardware, the owner chose a long-term solution, explicitly allowed a
one-time full reflash, and confirmed that existing LittleFS data may be
discarded.

Official NuttX, apps, Beken SDK source, and SDK libraries remain immutable.
Permanent adaptation stays in team-owned bootloader, linker, packer, board
constants, wrappers, and verification tools.

## Requirements and drivers

- CP and AP are one generation; mixed-generation boot is forbidden.
- Prefer the exact official v3.1.1.9 partition and remap contracts over a
  custom full-image copy engine.
- Preserve the board calibration/configuration tail at
  `0x7fa000..0x800000` through migration and every later update.
- Keep a 1 MiB CP-owned LittleFS partition, but its existing contents need
  not survive the one-time migration.
- Keep the source bootloader and all permanent integration team-owned.
- Fail closed on a layout ID, address-domain, size, digest, or generation
  mismatch.

## Considered options

1. Preserve the N14 layout and add rotating scratch/journaled sector swap.
2. Migrate once to the official contiguous primary CP/AP plus `s_app` layout
   and use the official single-offset remap semantics.
3. Overwrite the primary pair directly without a retained rollback pair.

## Decision

Use option 2. Freeze this raw physical layout:

| Region | Raw physical range | Size | Policy |
|---|---:|---:|---|
| primary bootloader | `0x000000..0x011000` | `0x011000` | team source, official-compatible envelope |
| primary CP A | `0x011000..0x165000` | `0x154000` | official v3.1.1.9 boundary |
| primary AP A | `0x165000..0x286000` | `0x121000` | official v3.1.1.9 boundary |
| paired B / `s_app` | `0x286000..0x4fb000` | `0x275000` | contiguous CP+AP candidate |
| trial metadata | `0x4fb000..0x4fc000` | `0x001000` | official `ota_fina_executive` envelope |
| vendor user config | `0x4fc000..0x50a000` | `0x00e000` | preserved official envelope |
| reserved | `0x50a000..0x600000` | `0x0f6000` | unallocated |
| CP LittleFS | `0x600000..0x700000` | `0x100000` | raw `bk_flash_*` address domain |
| reserved | `0x700000..0x7fa000` | `0x0fa000` | unallocated |
| official calibration tail | `0x7fa000..0x800000` | `0x006000` | immutable to project flashing/OTA |

The corresponding primary XIP windows are:

- CP: `0x02010000..0x02150000` (`0x140000` logical bytes);
- AP: `0x02150000..0x02260000` (`0x110000` logical bytes).

The initial migration artifact must put the same verified CP/AP generation in
both A and B, leave metadata erased/unarmed while the current A-only Tier-1
does not consume it, erase the new LittleFS range, and leave the official tail untouched. “Full reflash” means
rewriting all project-owned layout regions with explicit bounds; it never
means a chip erase. Old-layout sparse images and the pre-migration factory
image become recovery inputs only and must be rejected by new-layout tooling.

The team bootloader will clean-room reproduce the required official AB remap,
trial, confirm, and rollback behavior. The ADR-003 sector-copy ABI, logs,
scratch region, and copy commands are retired. Their mutation gate remains
zero; R2 source/model evidence stays archived as a rejected-option record.

## Positive consequences

- Uses the hardware mechanism and partition geometry already exercised by the
  pinned official SDK instead of copying every CP/AP sector at boot.
- Removes the scratch hot spot and most custom power-loss recovery states.
- Gives CP/AP one shared remap decision, which naturally prevents a mixed
  pair when metadata and bundle validation fail closed.
- Aligns the AP link address with the official v3.1.1.9 AP build
  (`0x02150000`) while leaving the CP entry at `0x02010000`.
- Retains almost 2 MiB of unallocated raw Flash around LittleFS.

## Negative consequences and risks

- Requires a one-time destructive LittleFS migration and a new full factory
  artifact.
- Changes the AP XIP address and therefore requires a complete N14 CP/AP SMP,
  RPMsg, Bluetooth, PSRAM, storage, reset, and recovery regression.
- Pre- and post-migration sparse artifacts are not interchangeable.
- The official RBL CRC32/FNV fields provide integrity, not publisher
  authentication or hardware-backed anti-rollback.
- A bug in factory bounds could damage the calibration tail; independent
  packer, loader-preflight, and read-back guards are mandatory.

## Validation plan

1. Source-verify the exact v3.1.1.9 partitions, logical-address conversion,
   AB remap registers, RBL placement, and trial-state semantics.
2. Add one canonical team layout definition and make linker, boot partition
   table, AP release address, MTD wrapper, packer, debug SOP, and verifiers
   consume or cross-check it.
3. Require deterministic factory and sparse manifests with a layout ID,
   per-segment ranges/hashes, and a hard end boundary below `0x7fa000`.
4. Clean-build `cp_nsh_psram + ap_smp_psram`; require all existing SDK,
   RPTUN, BLE GATT, and PSRAM gates plus new AP-vector/layout gates.
5. Before flashing, retain the N14 rollback artifact and prove a bounded
   recovery command. Then perform the explicitly approved migration, verify
   tail read-back, allow LittleFS autoformat, and rerun the full N14 matrix.
6. Only after the migrated baseline is board-verified may N15 add candidate
   staging, trial boot, confirmation, rollback, and fault injection.

## Outcome: N15-M migration

N15-M completed on 2026-08-03. Host gates passed against exact official
v3.1.1.9, then the owner-authorized board operation wrote only
`0x000000..0x4fc000` and `0x600000..0x700000`. LittleFS was intentionally
cleared and recreated. NSH, AP SMP, RPTUN, RPMsgFS, Bluetooth, PSRAM, SDK
timer and physical reset 3/3 passed. Full evidence is in the
[N15-M verification record](../../progress/verification/2026-08-03-n15-migration-board-verification.md).

The initial B content is a same-pair seed only. It has no RBL header, is
marked `boot_selectable=false`, and cannot be selected by the current
bootloader. Runtime OTA writes remain disabled until later N15 gates.

N15-C later froze the team metadata and read-only boot-decision contract in
[ADR-005](ADR-005-n15-boot-selector-metadata-v1.md). It is
host/source/ELF-verified but has not replaced the deployed A-only bootloader.

The pre-migration full-Flash read was taken at 6 Mbps and later shown to
contain occasional inserted 128-byte zero blocks. It is forensic-only, not a
bit-exact recovery image. Post-migration critical-region acceptance reads use
115200 and require two byte-identical captures.

## Reversal signals

- Exact source/binary evidence shows the team cannot reproduce the official
  remap safely without modifying immutable vendor inputs.
- The new AP link address breaks a required closed binary that cannot be
  corrected through team-owned configuration/wrappers.
- Hardware testing shows the remap affects raw data access or the preserved
  tail contrary to the verified contract.

## Open issues

- Publisher signatures, key provisioning, and hardware-backed anti-rollback
  remain a separate security decision; no current RBL field may be described
  as authentication.
- The metadata ABI is frozen by ADR-005. N15-D/E/F trial mutation,
  publication/reclamation, health confirmation and validation transport are
  host/source/ELF-verified, but no B-slot or metadata write has been authorized
  on the board. N15-V remains the physical gate.
- Confirmed B is currently terminal for metadata v1. Repeated confirmed
  generations require a future symmetric inactive-A staging design.
