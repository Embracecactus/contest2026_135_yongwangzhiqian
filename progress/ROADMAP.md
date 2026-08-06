# Roadmap

Last reviewed: 2026-08-06

## Latest completed baseline: N16 Wi-Fi STA data plane

- CP owns official v3.1.1.9 RF/PHY/MAC/WPA/controller and DHCP. AP logical CPU0
  owns the official proxy plus a repository adapter into native NuttX `wlan0`
  and sockets; vendor AP lwIP/socket and SDK FreeRTOS remain excluded.
- Runtime STA, wrong-password recovery, local TCP/UDP, retained-service
  coexistence, AP-restart refusal and 3/3 RTS recovery are board-verified.
- Official SDK/NuttX/apps source remains unmodified. Read the N16 worklog and
  phase verification before changing the accepted lifecycle boundaries.

## Prior completed baseline: N15

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

## Proposed next MAIN stages

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
