# Architecture

Last reviewed: 2026-08-08

## System context

The physical target is a three-core Arm Cortex-M33 BK7258. CPU0 is the CP boot
master. Physical CPU1 and CPU2 form one AP NuttX SMP cluster. The source
Tier-1 bootloader hands the CP image control; CP initializes shared hardware
owners and releases the AP image.

Canonical overview: [BK7258 porting report](../docs/bk7258-t5ai/porting-report.md).

## Components and ownership

| Component | Owner and responsibility |
|---|---|
| Tier-1 bootloader | Team source; normalizes boot/cache/MPU/watchdog state and transfers to CP |
| BK7258 integrated Flash | 8 MiB on the current T5-AI; interface reports `0xc86517`, compatible with the GD25WQ64E command identity but not evidence of a separate board-level chip |
| CP NuttX on CPU0 | Flash/LittleFS owner, AP lifecycle supervisor, RPMsg peer, Beken Bluetooth Controller owner, Wi-Fi RF/PHY/MAC/WPA/controller owner, PSRAM hardware/PM owner |
| AP NuttX SMP on CPU1+CPU2 | Stock NuttX scheduling/Host/services; logical CPU0 owns RPMsg/Bluetooth/Wi-Fi gateways, logical CPU1 is a business and socket producer |
| Beken SDK v3.1.1.9 | Immutable BK7258 CP/AP archives reached through minimal board ABI wrappers; the sole runtime SDK |
| Beken `bk_idk release/v2.0.1` | Read-only official reference: its `docs/bk7258/**` pages and generic security tools provide BK7258 Secure Boot semantics/packaging evidence; its buildable `projects/security/**` examples are BK7236-only single-core samples. Never a runtime archive or source replacement; see [ADR-017](decisions/ADR-017-bk7258-official-secureboot-source-crosswalk.md) |
| Windows/WSL2 tools | Build, sparse/factory download, UART/J-Link evidence, and no-GUI BLE client |
| N15 OTA (accepted architecture) | Official-style contiguous CP/AP A/B geometry is deployed; ADR-006 dual-bank inactive-slot A/B rotation completed the approved physical A-to-B-to-A lifecycle |
| N16 Wi-Fi (accepted architecture, complete for STA scope) | Official v3.1.1.9 radio/controller and DHCP client remain on CP; AP uses the official vnet proxy plus a repository-owned lease/netdev adapter to native NuttX `wlan0`/IPv4/sockets; vendor AP lwIP is excluded |
| N17-SB Secure Boot port (recoverable baseline) | BK7236 `bk_idk` is a read-only semantic/source reference; its single-core addresses/ABI/TFM mapping are not copied. The executable BK7258 chain uses a board-owned minimal BL1, candidate Manifest verifier and pinned NuttX MCUboot BL2 with CP/AP same-slot gating. Minimal BL1 publishes fixed Primary→Secondary order and links no N15/N17 lifecycle or Flash-write module. The chain is board-verified but remains software-rooted and unarmed; BootROM Manifest acceptance, OTP/eFuse binding and hardware rollback remain open. |

## Primary data flows

- Boot: legacy BootROM → board-owned minimal BL1 → signed Manifest → pinned
  NuttX MCUboot BL2 → signed CP/AP pair → bounded AP release → AP SMP READY.
- IPC: one CP↔AP RPTUN/OpenAMP/RPMsg link; AP logical CPU0 is the mailbox/OpenAMP gateway.
- Storage: CP exclusively owns raw flash/MTD/LittleFS; AP reaches it through RPMsgFS.
- Bluetooth: CP owns the official Controller; AP owns the stock NuttX Host/GAP/GATT through official pointer IPC and a board lower-half.
- Wi-Fi: CP owns official RF/PHY/MAC/WPA, the vnet controller and its DHCP
  client; AP logical CPU0 owns the official command/data proxy and a
  repository lease/netdev seam into native NuttX networking. AP does not run
  a second DHCP client. Runtime STA association, lease synchronization,
  gateway ICMP, local TCP/UDP sockets, bounded retained-service coexistence,
  AP-restart rejection and 3/3 controlled RTS recovery are board-verified.
- PSRAM: CP takes the official PM vote and performs the one-shot capacity gate; CP and AP use disjoint role-local heaps.
- OTA: CP/AP are one launchable pair. The board-owned MCUboot BL2 exposes only
  one physical slot to each `boot_go()` attempt and requires the CP result and
  AP vector to come from that same slot; a cross-slot-only state fails closed.
  Primary CP/AP and `s_app` remain equal-length contiguous pairs selected by
  one official-style Flash remap decision; LittleFS and the calibration tail
  are outside both executable spans.
- Authenticated OTA (N17 design): the team bootloader authenticates a
  canonical ECDSA-P256/SHA-256 release manifest and then verifies the actual
  CP/AP pair. Manifest A is `0x50b000..0x50c000`, Manifest B is
  `0x50c000..0x50d000`, and the normal-write-forbidden policy sector is
  `0x50d000..0x50e000`. The 256-byte format-3 lifecycle journal references
  immutable Manifest signed-region digests and remains responsible only for
  pending, trial, confirmation, rollback and the software counter floor.
  N17-S pins a public key in the team bootloader and makes no hardware-root
  claim. N17-H may later bind the same format to BootROM/OTP under
  [ADR-008](decisions/ADR-008-n17-phased-ota-authentication.md).

## Persistence and data lifecycle

- `/data` is CP LittleFS at raw `0x600000..0x700000`. N15-M intentionally
  cleared and autoformatted it during the one-time layout migration; its
  persistence probe passed three physical resets.
- Bluetooth base MAC/calibration records are created through the official first-calibration path and persist in flash.
- RPMsg endpoints and AP-local state are generation-scoped; AP restart invalidates stale transport state.
- The N14 upper PSRAM half is tested at boot but has no general runtime
  allocator or persistence semantics. The N15-F validation profile alone may
  use fixed volatile range `0x60800000..0x60a76200` to transfer one candidate,
  descriptor and pending record after a generation-bound operator gate.
- ADR-003's append-only logs, scratch sector, metadata ABI, and SRAM copy
  closure are retired research artifacts. Their 32,915-case model remains
  evidence for the rejected alternative; the mutation gate is zero and no
  board consumed that ABI.
- ADR-004 freezes primary CP/AP at raw `0x011000..0x286000`, paired B at
  `0x286000..0x4fb000`, metadata bank 0/user config through `0x50a000`, LittleFS at
  `0x600000..0x700000`, and the immutable official tail at
  `0x7fa000..0x800000`.
- ADR-005 freezes metadata format 1 as historical one-direction evidence.
  ADR-006 is current: eight append-only 512-byte records per bank at
  `0x4fb000..0x4fc000` and `0x50a000..0x50b000`, with slot-neutral
  `PENDING_*/TRIAL_*/CONFIRMED_*/ROLLBACK_*` states. The selected bank remains
  durable until an inactive-bank record is completely read back; pending alone
  is never permission to remap.
- N17's canonical host layout reserves the two Manifest sectors and policy
  sector above and leaves `0x50e000..0x600000` unallocated. The accepted
  format-3 design reuses the two metadata sectors as sixteen 256-byte records
  per bank and commits confirmation plus the accepted counter floor together.
  Until the future migration is implemented, explicitly authorized and run,
  the deployed board remains on N15 format 2 and its policy sector is unarmed.

## External dependencies

- openvela/NuttX sibling checkouts in the workspace, treated as official read-only inputs.
- Beken SDK v3.1.1.9 for all runtime linking, adaptation and board verification.
- Beken `bk_idk release/v2.0.1` only for read-only BK7236/BK7258 secureboot source review. BK7259 and `release/v4.0.1` are retired and prohibited by [the project rules](RULES.md).
- Windows BKFIL/Beken loader, COM serial devices, and SEGGER J-Link for physical-board operations.
- Product capability reference: [Beken BK7258](https://www.bekencorp.com/index/goods/detail/cid/60.html).

## Security and privacy boundaries

- Never store credentials, device-unique private material, or unredacted private production records in project memory or logs.
- The Bluetooth first release has no pairing/bonding/security claim.
- Wi-Fi credentials are runtime-only secrets: never place them in defconfig,
  repository logs or project memory, and never print them unredacted.
- Generic pointers never cross CP/AP through RPMsg; only explicitly reviewed vendor pointer-IPC contracts may do so.
- The active development board must remain recoverable for later driver work.
  OTP/eFuse, secure-boot enable, lifecycle/JTAG locks and hardware rollback
  counters are outside normal firmware and validation authority.
- Normal firmware may not write or erase the N17 policy sector. After a future
  authorized migration programs any non-`0xff` byte in that sector, boot must
  fail closed to format 3 plus a valid signed Manifest/payload; format 2 and
  header-only slot-A fallback are permanently ineligible. The current board
  has not been armed.

## Known constraints and technical debt

- N14 exposes only 128 KiB CP and 640 KiB AP role-local PSRAM heaps. The remaining regions are reserved by policy.
- PSRAM is non-cacheable; DMA/cache-coherency and performance tuning are deferred.
- AP automatic recovery is disabled by default; the verified baseline is detection plus bounded manual recovery.
- The official v3.1.1.9 single-offset AB remap was incompatible with N14's
  old layout. N15-M completed the owner-authorized ADR-004 migration and full
  retained-service board regression.
- Executable images use 32+2 CRC-expanded physical coordinates, while
  `bk_flash_*` data APIs use raw offsets. The canonical layout/verifier must
  cross-check every conversion and reject old/new layout mixing.
- BL2 XIP reads must never extend past the valid CRC-expanded payload. A test
  that copied 128 KiB from an 8 KiB BL2 package falsely appeared to show a
  64 KiB SRAM-bank limit; a complete 128 KiB logical/136 KiB physical CRC
  package crossed `0x28030000` and booted successfully. Copy length and
  package span are therefore one contract.
- Exact v3.1.1.9 public normal and A/B reset paths both start with vector MSP
  `0x28030000` and program `MSPLIM = 0x2802f800`. These are boot-stack bounds,
  not a partition of all SRAM or a limit on later BL2/AP allocations.
- The BK7236 v2.0.1 security documentation/source is the active same-Armino
  semantic reference: immutable BL1 authorizes Manifest/BL2, MCUboot
  authorizes later signed images, and TrustEngine/OTP/EFUSE surround the
  chain. It is single-core evidence; BK7258 Manifest bytes, secure registers,
  version-counter ABI and CP/AP mapping remain unproven. See
  [ADR-022](decisions/ADR-022-bk7258-secureboot-bk7236-semantic-port.md).
- MCUboot verification runs on the 34/32-decoded XIP stream and can exceed the
  BL1 watchdog's short recovery window. The board-owned BL2 flash-read wrapper,
  and BL1 only while it executes its one P-256 Manifest verification, use a
  60-second watchdog period; BL1 restores its ordinary period before acting on
  the result. This is a timing boundary, not a waiver for an infinite boot hang.
- N15 R1/R2 sector-swap evidence is historical. N15-A through N15-F and the
  format-2 host campaign are verified; the approved physical scope completed
  generation 314 A-to-confirmed-B and generation 315 B-to-confirmed-A through
  both metadata banks. This does not claim an analog mid-Flash-pulse brownout
  or authorize future Flash writes.
- N15 format 2 authenticates neither publisher nor rollback age. N17-S's
  layout, Manifest and format-3 architecture are frozen, but its Bootloader
  verifier/journal and migration are not implemented. Complete Flash
  replacement remains outside its threat coverage until the separately gated
  N17-H hardware trust chain is provisioned.
- Official v3.1.1.9 Wi-Fi teardown is incomplete: CP `wifi_deinit()` is
  unsupported and the AP proxy does not close its mailbox channels. Until a
  separate lifecycle design is verified, AP-only restart must fail closed
  while Wi-Fi is active and whole-chip reset is the recovery boundary.
- The immutable CP Wi-Fi archive consumes selected `malloc()` blocks as
  zero-initialized state. The board compatibility layer therefore zeroes
  allocations only for the PID executing `bk_wifi_init()`; concurrent CP
  threads retain normal NuttX allocation semantics. This boundary must not be
  broadened for another component without separate allocation evidence.
- The AP Wi-Fi profile uses NuttX's independent 16 KiB FS heap for
  VFS/RPMsgFS/socket metadata. This prevents nested filesystem allocations
  from sharing the vendor Wi-Fi/general AP heap; it is profile-scoped and
  requires no NuttX source modification.
- CPU0 480 MHz is not supported by the verified SDK policy.  BL1 hands off at
  120 MHz and the normal v3.1.1.9 startup gives `PM_DEV_ID_DEFAULT` a 120 MHz
  vote; 320 MHz is an explicit bring-up/module-request tier, not the product
  default.  All later module voting or NuttX PM governance must reuse the
  SDK-ordered BK7258 DVFS lower half instead of writing clock registers
  independently.
