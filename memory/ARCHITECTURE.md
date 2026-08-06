# Architecture

Last reviewed: 2026-08-06

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
| Beken SDK v3.1.1.9 | Immutable CP/AP archives reached through minimal board ABI wrappers |
| Windows/WSL2 tools | Build, sparse/factory download, UART/J-Link evidence, and no-GUI BLE client |
| N15 OTA (accepted architecture) | Official-style contiguous CP/AP A/B geometry is deployed; ADR-006 dual-bank inactive-slot A/B rotation completed the approved physical A-to-B-to-A lifecycle |
| N16 Wi-Fi (accepted architecture, complete for STA scope) | Official v3.1.1.9 radio/controller and DHCP client remain on CP; AP uses the official vnet proxy plus a repository-owned lease/netdev adapter to native NuttX `wlan0`/IPv4/sockets; vendor AP lwIP is excluded |

## Primary data flows

- Boot: BootROM → Tier-1 bootloader → CP NuttX → bounded AP power/reset release → AP SMP READY.
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
- OTA: CP/AP are one generation. Primary CP/AP and `s_app` are equal-length
  contiguous pairs selected by one official-style Flash remap decision;
  LittleFS and the calibration tail are outside both executable spans.

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

## External dependencies

- openvela/NuttX sibling checkouts in the workspace, treated as official read-only inputs.
- Beken SDK v3.1.1.9 source for verification and the checksum-pinned local archive bundle for linking.
- Windows BKFIL/Beken loader, COM serial devices, and SEGGER J-Link for physical-board operations.
- Product capability reference: [Beken BK7258](https://www.bekencorp.com/index/goods/detail/cid/60.html).

## Security and privacy boundaries

- Never store credentials, device-unique private material, or unredacted private production records in project memory or logs.
- The Bluetooth first release has no pairing/bonding/security claim.
- Wi-Fi credentials are runtime-only secrets: never place them in defconfig,
  repository logs or project memory, and never print them unredacted.
- Generic pointers never cross CP/AP through RPMsg; only explicitly reviewed vendor pointer-IPC contracts may do so.

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
- N15 R1/R2 sector-swap evidence is historical. N15-A through N15-F and the
  format-2 host campaign are verified; the approved physical scope completed
  generation 314 A-to-confirmed-B and generation 315 B-to-confirmed-A through
  both metadata banks. This does not claim an analog mid-Flash-pulse brownout
  or authorize future Flash writes.
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
- CPU0 480 MHz is not supported by the verified SDK policy; the product path uses the SDK-aligned 320 tier with CPU0 effectively 160 MHz and AP at 320 MHz.
