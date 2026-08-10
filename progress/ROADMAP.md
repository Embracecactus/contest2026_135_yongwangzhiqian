# Roadmap

Last reviewed: 2026-08-10

## Latest completed baseline: N16 Wi-Fi STA data plane

- CP owns official v3.1.1.9 RF/PHY/MAC/WPA/controller and DHCP. AP logical CPU0
  owns the official proxy plus a repository adapter into native NuttX `wlan0`
  and sockets; vendor AP lwIP/socket and SDK FreeRTOS remain excluded.
- Runtime STA, wrong-password recovery, local TCP/UDP, retained-service
  coexistence, AP-restart refusal and 3/3 RTS recovery are board-verified.
- Official SDK/NuttX/apps source remains unmodified. Read the N16 worklog and
  phase verification before changing the accepted lifecycle boundaries.

## Prior completed baseline: N15

This section is historical verification. The custom selector, metadata
journal, staging, trial/confirmation and rollback implementation has since
been removed from active source; it must not be treated as the current update
architecture.

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

## Active MAIN stage: BL1/BL2/MCUboot recoverable Secure Boot chain

The official BK7258 v3.1.1.9 SDK has no buildable Secure Boot adaptation.
Technical support confirmed that BK7236 and BK7258 share the security
architecture, while BK7236 is single-core. Therefore BK7236 `bk_idk`
documentation/source is the semantic reference; BK7258 addresses, Manifest
bytes, OTP/eFuse ABI and CP/AP mapping must still be source- or board-verified.

The active objective is a complete recoverable board-owned BL1 -> NuttX
MCUboot BL2 -> CP/AP SMP chain. No NuttX/SDK source changes, OTP/eFuse writes,
secure-boot enable, lifecycle transition or debug lock are allowed.

The board proof (`B1PAGE -> BL2RAM -> MCUboot -> NSH`) is evidence for the
repository-owned chain, not proof of BK7258 BootROM Manifest acceptance.
The old N17 custom OTA journal/policy implementation is retired; N17-SB below
names the historical Secure Boot workstream, not an active OTA state machine.

## N17-SB phases and gates

1. **SB0 — scope/evidence freeze**: separate BK7236 reusable semantics from
   unproven BK7258 facts; freeze the recoverable development boundary.
2. **SB1 — BK7236 reference extraction**: record the reference BL1 sequence,
   MCUboot 1.9 behavior, Manifest semantics, partition/security tables and
   AES/CRC/padding/merge/signing order.
3. **SB2 — BK7258 read-only boundary**: confirm raw Flash reads, CRC-XIP
   conversion, Manifest sectors, TrustEngine/OTP shadow state and CP/AP
   placement using source review and minimal J-Link/UART evidence.
4. **SB3 — BL1**: finish Manifest A/B parsing, root-hash policy, version floor,
   BL2 digest/signature verification, fallback and SRAM handoff.
5. **SB4 — BL2**: finish the pinned NuttX MCUboot integration, CP/AP same-slot
   validation, image/TLV signature checks, security-counter policy and vector
   handoff.
6. **SB5 — packaging**: implement the BK7258-specific CRC and optional AES
   stages and represent the documented padding/merge/signing order without
   forcing BK7236 single-core layout onto the CP/AP pair.
7. **SB6 — A/B recovery**: verify normal boot, invalid Manifest/BL2, slot
   fallback, mismatched CP/AP pair and software rollback floor with only the
   existing download/UART/J-Link tools. Do not create a campaign framework
   before a recurring test need exists.
8. **SB-H — hardware root (deferred)**: only after new authority and real
   BK7258 material are available, bind the implementation to OTP/eFuse and
   BootROM Secure Boot. This is not part of the current board work.

### Gate status at 2026-08-08

- **SB0-SB2: complete for the reversible evidence boundary.**  BK7236
  semantics, BK7258 read-only TrustEngine/OTP shadow state, the 128 KiB BL2
  capacity and the two-stage source crosswalk are recorded.  BK7236 addresses
  and undocumented ABI values remain explicitly excluded.
- **SB3-SB4: implementation and board regression complete.**  The board-owned
  BL1 candidate page, primary/secondary BL2 fallback, pinned NuttX MCUboot
  validation, CP/AP same-slot/version/counter gate and vector handoff all pass
  the normal and negative captures.  This is not proof of the BootROM ABI.
- **SB5: host-reference complete, hardware exactness open.**  The v3.1.1.9
  source order (logical merge -> MCUboot sign/pad -> optional AES -> 32+2
  CRC -> physical tail/status placement) is emitted as a separately labelled
  artifact.  The exact BK7258 AES key/consumer and BootROM CRC read view are
  not available, so that artifact is not flashed.
- **SB6: recoverable development matrix complete.**  Manifest/key rejection,
  BL2 fallback, A/B selection, cross-slot/version/vector rejection and the
  compile-time security-counter floor are covered.  Persistent OTP/NV
  rollback is intentionally not claimed.
- **SB-H: blocked by missing BK7258 BootROM Manifest/OTP ABI and by the
  explicit no-irreversible-write rule.**

Network OTA transport remains paused. A future updater must start from NuttX
MCUboot semantics and the CP/AP same-slot contract, with new Flash-write and
recovery authorization; it must not restore the retired N15/N17 journal.

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
