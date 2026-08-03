# Architecture

Last reviewed: 2026-08-03

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
| CP NuttX on CPU0 | Flash/LittleFS owner, AP lifecycle supervisor, RPMsg peer, Beken Bluetooth Controller owner, PSRAM hardware/PM owner |
| AP NuttX SMP on CPU1+CPU2 | Stock NuttX scheduling/Host/services; logical CPU0 owns transport gateway, logical CPU1 is a business producer |
| Beken SDK v3.1.1.9 | Immutable CP/AP archives reached through minimal board ABI wrappers |
| Windows/WSL2 tools | Build, sparse/factory download, UART/J-Link evidence, and no-GUI BLE client |

## Primary data flows

- Boot: BootROM → Tier-1 bootloader → CP NuttX → bounded AP power/reset release → AP SMP READY.
- IPC: one CP↔AP RPTUN/OpenAMP/RPMsg link; AP logical CPU0 is the mailbox/OpenAMP gateway.
- Storage: CP exclusively owns raw flash/MTD/LittleFS; AP reaches it through RPMsgFS.
- Bluetooth: CP owns the official Controller; AP owns the stock NuttX Host/GAP/GATT through official pointer IPC and a board lower-half.
- PSRAM: CP takes the official PM vote and performs the one-shot capacity gate; CP and AP use disjoint role-local heaps.

## Persistence and data lifecycle

- `/data` is CP LittleFS. A factory image overwrites the data/calibration area; normal sparse flashing preserves it.
- Bluetooth base MAC/calibration records are created through the official first-calibration path and persist in flash.
- RPMsg endpoints and AP-local state are generation-scoped; AP restart invalidates stale transport state.
- The N14 upper PSRAM half is tested at boot but has no runtime allocator or persistence semantics.

## External dependencies

- openvela/NuttX sibling checkouts in the workspace, treated as official read-only inputs.
- Beken SDK v3.1.1.9 source for verification and the checksum-pinned local archive bundle for linking.
- Windows BKFIL/Beken loader, COM serial devices, and SEGGER J-Link for physical-board operations.
- Product capability reference: [Beken BK7258](https://www.bekencorp.com/index/goods/detail/cid/60.html).

## Security and privacy boundaries

- Never store credentials, device-unique private material, or unredacted private production records in project memory or logs.
- The Bluetooth first release has no pairing/bonding/security claim.
- Generic pointers never cross CP/AP through RPMsg; only explicitly reviewed vendor pointer-IPC contracts may do so.

## Known constraints and technical debt

- N14 exposes only 128 KiB CP and 640 KiB AP role-local PSRAM heaps. The remaining regions are reserved by policy.
- PSRAM is non-cacheable; DMA/cache-coherency and performance tuning are deferred.
- AP automatic recovery is disabled by default; the verified baseline is detection plus bounded manual recovery.
- CPU0 480 MHz is not supported by the verified SDK policy; the product path uses the SDK-aligned 320 tier with CPU0 effectively 160 MHz and AP at 320 MHz.
