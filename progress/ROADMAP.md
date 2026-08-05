# Roadmap

Last reviewed: 2026-08-04

## Now: N16 Wi-Fi STA data plane

- ADR-007 is accepted: CP owns official v3.1.1.9 RF/PHY/MAC/WPA/controller;
  AP logical CPU0 owns the official Wi-Fi proxy plus a repository-owned
  adapter to native NuttX `wlan0`, DHCP and sockets.
- N16-R is active and board-read-only. It must close the exact archive/object
  dependency graph, vendor pbuf/cpdu/headroom ABI, callback context, pointer
  ownership, credential-log audit and forbidden-symbol link gates.
- N16-A will add an idempotent CP shared-radio/controller wrapper without
  double-initializing the N12 Bluetooth PHY/RF/calibration path.
- N16-B/C will add the AP CPU0 command worker, minimal pbuf compatibility and
  NuttX Ethernet netdev seam. The vendor AP lwIP/socket and SDK FreeRTOS
  implementations must remain absent.
- N16-D will add dedicated validation profiles and verify STA association,
  NuttX DHCP, gateway ICMP and local TCP/UDP exchange.
- N16-E/V will verify AP SMP socket producers and Wi-Fi coexistence with
  RPTUN/RPMsg, RPMsgFS and Bluetooth, then close the finite board matrix.

## Latest completed baseline: N15

- The official-style contiguous CP/AP A/B layout and ADR-006 symmetric
  dual-bank selector are merged and board-verified for the approved scope.
- Generation 314 completed A-to-confirmed-B through bank 0; generation 315
  completed B-to-confirmed-A through bank 1. Both inactive pairs passed full
  Flash read-back/SHA and retained N14 services.
- Confirmed A survived RTS and complete removal of USB and J-Link power. The
  board was restored with a bounded normal gates-zero sparse image; AP SMP,
  RPTUN, LittleFS and PSRAM passed and `bkota` is absent.
- Physical rollback and analog mid-program brownout were not part of the
  approved minimal N15 board run. Future Flash writes require fresh authority.

## Next after N16

1. N17: authenticated update policy—signature format, key provisioning,
   anti-rollback and recovery-key design—after a separate architecture review.
2. N18: network OTA transport over the N16 native socket path, reusing N15's
   transport-neutral staging/publication API and N17 authentication policy.

These numbers are planning anchors, not authorization to start either stage.

## Later candidates

- Bluetooth/Wi-Fi warm restart with pointer quiesce and mailbox teardown.
- General upper-8 MiB PSRAM allocator ownership and cache/DMA policy.
- Product hardening: uncontrolled power cuts, voltage/temperature stress,
  long-duration networking and Flash wear characterization.
- Legacy SDK validation only after the v3.1.1.9 product path is fully complete
  and the owner requests it explicitly.

## Explicitly deferred

- Linking the vendor AP lwIP/socket stack into NuttX.
- CP-hosted remote sockets or a new RPMsg Ethernet protocol.
- SoftAP, bridge/repeater, monitor mode, raw injection and Wi-Fi power-save in
  the first N16 release.
- Repeating the N15 migration, chip erase, future OTA mutation or destructive
  board workflow without explicit owner authority.
- ADR-003 sector-swap, CPU0 direct 480 MHz, QEMU and unrelated debug-SOP work.
