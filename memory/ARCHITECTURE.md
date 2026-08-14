# Architecture

Last reviewed: 2026-08-14

## System context

The physical target is a three-core Arm Cortex-M33 BK7258. CPU0 is the CP boot
master. Physical CPU1 and CPU2 form one AP NuttX SMP cluster. The source
Tier-1 bootloader hands the CP image control; CP initializes shared hardware
owners and releases the AP image.

Canonical overview: [BK7258 porting report](../docs/bk7258-t5ai/porting-report.md).

## Components and ownership

| Component | Owner and responsibility |
|---|---|
| BK7258 platform and physical boards | `board/bk7258` owns shared SoC, CP/AP, boot-chain, wrapper, partition and build code. `chip/` owns controller mechanics and NuttX lower halves; the selected `boards/t5ai_core` or `boards/t5_board` variant owns fixed electrical facts and attached-device registration through early/device hooks. Runtime I2C/SPI transaction settings remain upper-half inputs. T5AI-Core remains the compatibility default. See [ADR-023](decisions/ADR-023-bk7258-platform-board-variants.md). |
| BK7258 build profiles | `board/bk7258/configs/<profile>/defconfig` selects one reusable product or bounded validation feature set for one physical board and one CP/AP role. Adjacent `profile.conf` is the packaging contract: board, role, boot mode, class and CP/AP compatibility ID. The build wrapper consumes metadata rather than profile-name whitelists and serializes physical builds because openvela configures the shared `nuttx/` and `apps/` trees in place. See [ADR-024](decisions/ADR-024-bk7258-physical-board-build-profiles.md). |
| BK7258 initialization layers | `src/bk7258_platform.c` owns mandatory SDK/IPC/PM/AP lifetime initialization, `src/bk7258_bringup.c` owns application-facing procfs/MTD/filesystem registration, and the selected physical-board hook owns attached LCD/touch/camera validation and registration. `board_late_initialize()` and `board_app_initialize()` are thin NuttX entry points. |
| Tier-1 bootloader | Board-owned source reconstructed for this port; it is built as a project artifact rather than patched into a vendor binary. It normalizes boot/cache/MPU/watchdog state. Direct profiles validate and transfer straight to CP; signed profiles transfer to Manifest/MCUboot BL2. |
| BK7258 integrated Flash | 8 MiB on the current T5-AI; interface reports `0xc86517`, compatible with the GD25WQ64E command identity but not evidence of a separate board-level chip |
| CP NuttX on CPU0 | Flash/LittleFS owner, AP lifecycle supervisor, RPMsg peer, Beken Bluetooth Controller owner, Wi-Fi RF/PHY/MAC/WPA/controller owner, PSRAM hardware/PM owner. Bluetooth desired state is published only after real SDK init/deinit success; Wi-Fi remains whole-chip lifetime. See [ADR-025](decisions/ADR-025-bk7258-radio-lifecycle-boundary.md). |
| AP NuttX SMP on CPU1+CPU2 | Stock NuttX scheduling/Host/services; logical CPU0 owns RPMsg/Bluetooth/Wi-Fi gateways, logical CPU1 is a business and socket producer |
| Beken SDK v3.1.1.9 | Immutable BK7258 CP/AP archives reached through minimal board ABI wrappers; the sole runtime SDK |
| Beken `bk_idk release/v2.0.1` | Read-only official reference: its `docs/bk7258/**` pages and generic security tools provide BK7258 Secure Boot semantics/packaging evidence; its buildable `projects/security/**` examples are BK7236-only single-core samples. Never a runtime archive or source replacement; see [ADR-017](decisions/ADR-017-bk7258-official-secureboot-source-crosswalk.md) |
| Windows/WSL2 tools | Build, sparse/factory download, UART/J-Link evidence, and no-GUI BLE client |
| Historical N15 OTA evidence | The former custom inactive-slot writer, dual-bank journal and trial/rollback lifecycle were physically exercised, then retired from active source. Their ADRs and verification records are historical evidence, not current firmware architecture. |
| N16 Wi-Fi (accepted architecture, complete for STA scope) | Official v3.1.1.9 radio/controller and DHCP client remain on CP; AP uses the official vnet proxy plus a repository-owned lease/netdev adapter to native NuttX `wlan0`/IPv4/sockets; vendor AP lwIP is excluded |
| BL1/BL2/MCUboot chain (recoverable baseline) | BK7236 `bk_idk` is a read-only semantic/source reference; its single-core addresses/ABI/TFM mapping are not copied. The executable BK7258 chain uses a board-owned BL1, candidate Manifest verifier and pinned NuttX MCUboot BL2 with CP/AP same-slot gating. BL1 publishes fixed Primary→Secondary order and links no retired N15/N17 lifecycle or Flash-write module. The chain is board-verified but remains software-rooted and unarmed; BootROM Manifest acceptance, OTP/eFuse binding and hardware rollback remain open. |
| CP debug and console transport | SWD route, target core and console transport are independent configuration axes with paired-image pin-conflict gates. SWD supports P0/P1 or P20/P21; console supports NONE, RTT or UART0/1/2 with explicit frame/baud and route settings. Direct profiles stop APB/AON watchdogs and hold in BL1 after final cleanup; release magic immediately precedes the CP branch. MCUboot profiles hold at the equivalent BL2-to-CP boundary. The board-verified T5-Board profile uses CP SWD/RTT on P0/P1, omits UART1/COM4, suppresses only the SDK all-pin default-map pass that would overwrite the route, and downloads through COM3. P20/P21 and alternate UART routes are compiled but not board-verified. |
| PM and timer policy | NuttX remains the PM owner and the SDK is a leaf hardware service. Ordinary idle uses clear-SLEEPDEEP then DSB/WFI/ISB. Coordinated standby is board-owned but follows the v3.1.1.9 protocol: CP owns the request, PWC mailbox exchange and both-AP vote barrier; each AP checks its vote and pending IRQ/DMA state, saves/stops SysTick, publishes AON WFI state, preserves the mailbox wake path, enters SLEEPDEEP and restores in official wake order. CP uses the bounded RTC wake and compensates NuttX ticks. Missing votes, pending work, stale generations, mailbox errors or restore errors fail closed; no core may independently claim low-voltage standby. Diagnostics are observational and do not authorize entry. |

## Primary data flows

- Direct boot: legacy BootROM → board-owned BL1 direct-vector validation and
  optional final debug gate → CP → bounded AP release → AP SMP READY.
- Signed boot: legacy BootROM → board-owned BL1 → signed Manifest → pinned
  NuttX MCUboot BL2 → signed CP/AP pair → bounded AP release → AP SMP READY.
- IPC: one CP↔AP RPTUN/OpenAMP/RPMsg link; AP logical CPU0 is the mailbox/OpenAMP gateway.
- Power management: NuttX requests standby on CP → CP publishes a generation-scoped
  PWC request → both AP cores pass local IRQ/DMA/vote gates and publish AON WFI
  state → CP admits the bounded low-voltage interval → mailbox/RTC wake drives
  ordered AP and CP restore → CP compensates scheduler ticks. Any incomplete
  edge aborts to shallow idle.
- Storage: CP exclusively owns raw flash/MTD/LittleFS; AP reaches it through RPMsgFS.
- Bluetooth: CP owns the official Controller; AP owns the stock NuttX
  Host/GAP/GATT through official pointer IPC and a board lower-half.  CP's
  versioned active flag is authoritative across init/deinit; AP reconciles a
  lost reply only when that desired state already committed, otherwise retains
  UNKNOWN ownership and fails closed.  Repeated Controller validation occurs
  before Host ownership because this NuttX configuration cannot unregister and
  re-register the Host device.
- Wi-Fi: CP owns official RF/PHY/MAC/WPA, the vnet controller and its DHCP
  client; AP logical CPU0 owns the official command/data proxy and a
  repository lease/netdev seam into native NuttX networking. AP does not run
  a second DHCP client. Runtime STA association, lease synchronization,
  gateway ICMP, local TCP/UDP sockets, bounded retained-service coexistence,
  AP-restart rejection and 3/3 controlled RTS recovery are board-verified.
- PSRAM: CP takes the official PM vote and performs the one-shot capacity gate; CP and AP use disjoint role-local heaps.
- Signed-image selection: CP/AP are one launchable pair. The board-owned MCUboot BL2 exposes only
  one physical slot to each `boot_go()` attempt and requires the CP result and
  AP vector to come from that same slot; a cross-slot-only state fails closed.
  Primary CP/AP and `s_app` remain equal-length contiguous pairs selected by
  one official-style Flash remap decision; LittleFS and the calibration tail
  are outside both executable spans. The active firmware does not contain an
  inactive-slot writer, trial journal, confirmation service or field-update
  transport.

## Persistence and data lifecycle

- `/data` is CP LittleFS at raw `0x600000..0x700000`. N15-M intentionally
  cleared and autoformatted it during the one-time layout migration; its
  persistence probe passed three physical resets.
- Bluetooth base MAC/calibration records are created through the official first-calibration path and persist in flash.
- RPMsg endpoints and AP-local state are generation-scoped; AP restart invalidates stale transport state.
- The N14 upper PSRAM half is tested at boot but has no general runtime
  allocator or persistence semantics. The retired N15 validation transfer
  window has no active consumer.
- ADR-003's append-only logs, scratch sector, metadata ABI, and SRAM copy
  closure are retired research artifacts. Their 32,915-case model remains
  evidence for the rejected alternative; the mutation gate is zero and no
  board consumed that ABI.
- The active CSV freezes primary CP/AP at raw `0x011000..0x286000`, paired B
  at `0x286000..0x4fb000`, `usr_config` at `0x4fc000..0x50a000`, read-only BL1
  Manifest pages at `0x50b000..0x50d000`, two read-only BL2 copies beginning
  at `0x51d000`, LittleFS at `0x600000..0x700000`, and the immutable official
  tail at `0x7fa000..0x800000`. The gaps are explicitly unallocated.
- ADR-004 records the one-time physical layout migration. ADR-005, ADR-006
  and the N17 format-3 journal remain historical design/evidence only; their
  runtime readers, writers and policy sector have been removed.

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
- Normal firmware may not write or erase BL1 Manifest or BL2 partitions.
  There is no active N17 policy sector or field-update mutation path. Any
  future updater must define a new authorization and recovery boundary before
  gaining Flash-write authority.

## Known constraints and technical debt

- T5AI-Core V1.0.1 remains the compatibility default. T5-Board is physically
  verified for COM3 sparse download, P0/P1 CP SWD, BL1 final hold/release,
  CP/AP startup, CPU2 SMP and bidirectional RPTUN. P20/P21, alternate UARTs,
  TF, RGB LCD and DVP remain compile-only until tested in compatible profiles.
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
- N15 R1/R2, format-2 and N17 format-3 lifecycle evidence is historical. The
  former A-to-B-to-A run does not authorize future Flash writes, and the
  corresponding runtime code has been retired. The preserved BL1/MCUboot
  chain authenticates boot images but is not itself a field-update service.
- Official v3.1.1.9 Wi-Fi teardown is incomplete: CP `wifi_deinit()` is
  unsupported and the AP proxy does not close its mailbox channels. Until a
  separate lifecycle design is verified, AP-only restart must fail closed
  while Wi-Fi is active and whole-chip reset is the recovery boundary.
- Official Bluetooth init/deinit is symmetric, but its CP IPC worker always
  reports success without checking the real return.  The board wrapper must
  therefore use CP-owned committed state rather than the vendor event as its
  ownership proof.  Full NuttX Host unregister/re-register remains outside the
  supported lifecycle; do not replace it with forced worker deletion.
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
