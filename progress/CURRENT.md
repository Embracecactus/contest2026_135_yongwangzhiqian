# Current Progress

Last updated: 2026-08-06T01:14:35+08:00
Updated by: Codex (`maintain-project-memory` checkpoint)

## Snapshot

- Branch: `feat/bk7258-n16-wifi`; base checkpoint `0af5efe`. N16 completion
  changes are local and not yet committed or published.
- Sole active SDK: checksum-pinned official Beken v3.1.1.9. Official SDK,
  NuttX and apps source remain unmodified.
- N15's physical A-to-B-to-A OTA baseline remains complete and merged.
- N16 is complete for the accepted STA scope. CP owns official RF/PHY/MAC/WPA,
  vnet control and DHCP; AP synchronizes the lease into native NuttX `wlan0`
  and owns IPv4/TCP/UDP/sockets. Vendor AP lwIP/socket and SDK FreeRTOS remain
  excluded.
- Runtime-only association, bounded wrong-password recovery, gateway ICMP,
  local TCP/UDP, active-Wi-Fi RPMsg/RPMsgFS/Bluetooth coexistence, AP-restart
  rejection and 3/3 controlled RTS recovery are board-verified.
- Dynamic partitions, factory boundaries and Tier-1 Boot OTA gates-zero passed;
  post-reset RPMsgFS/LittleFS write/read passed. The separate N14 PSRAM profile
  was not rerun and was not modified by N16.

Canonical evidence:

- [N16 completion worklog](../docs/bk7258-t5ai/nuttx-port/prompts/16-n16-wifi-data-plane.md)
- [N16 board verification](verification/2026-08-06-n16-wifi-sta-coexistence.md)
- [N16 malloc compatibility evidence](verification/2026-08-05-n16-wifi-malloc-compatibility.md)
- [ADR-007 Wi-Fi ownership](../memory/decisions/ADR-007-n16-cp-radio-ap-nuttx-network.md)

## Durable implementation boundaries

- CP Wi-Fi initialization uses a PID-scoped zeroing malloc compatibility
  window because the immutable archive assumes selected fresh-heap objects
  are zero. Other CP threads keep normal malloc semantics.
- AP `wlan0` persists across runtime STA stop/start attempts. The control
  worker waits for CP stop completion, withdraws stale carrier/lease state,
  then applies new runtime credentials.
- `ap_smp_wifi` uses NuttX's independent 16 KiB FS heap for VFS/RPMsgFS/socket
  metadata, fixing the observed live-file overwrite without modifying NuttX.
- Official Wi-Fi teardown is incomplete. AP-only restart fails closed while
  Wi-Fi is active; whole-chip reset remains the recovery boundary.

## Next actions

1. Review and commit only the N16-owned implementation, documentation and
   memory changes; publish them when the owner requests it.
2. Proposed next MAIN Stage: N17 authenticated update policy. Start with a
   separate architecture review covering signatures, key provisioning,
   anti-rollback and recovery keys before implementation.

## Open constraints

- Credentials remain runtime-only and must never enter source, config, logs or
  project memory.
- SoftAP, warm Wi-Fi recovery, network OTA and legacy SDK validation remain
  outside the completed N16 scope.
- Existing unrelated dirty debug tools, QEMU trees, logs and N15 scratch files
  are owner work and must not be staged with N16.
