# Current Progress

Last updated: 2026-08-05T21:55:00+08:00
Updated by: Codex (`maintain-project-memory` checkpoint)

## Snapshot

- Branch: `feat/bk7258-n16-wifi`, based on merged upstream N15 commit
  `6cee7839cac5b4e1f688a86b1496d67b1bc4608f`.
- Sole active SDK: official Beken v3.1.1.9. Older SDKs remain preserved and
  unused.
- N15's approved physical A-to-B-to-A scope remains complete and merged.
- N16 is the current MAIN Stage. The owner accepted ADR-007: CP keeps official
  RF/PHY/MAC/WPA/controller ownership; AP connects the official Wi-Fi proxy to
  native NuttX `wlan0`, DHCP and sockets through a repository-owned adapter.
- Dedicated `cp_nsh_wifi + ap_smp_wifi` images now build from checksum-pinned
  official v3.1.1.9 archives and boot on hardware. The current board runs the
  Wi-Fi-owner-scoped malloc compatibility image after sparse flash and COM7
  RTS verification.
- This checkpoint verifies controller initialization and retained AP/RPTUN
  health, not STA association, DHCP or socket data-plane closure. `bkwifi
  status` currently reports `status=0`, `link=0`.

## N16 verified facts

- Official vnet roles and mailbox channels remain as recorded in ADR-007 and
  the N16 worklog. Vendor AP lwIP/socket and SDK FreeRTOS remain excluded;
  NuttX is the sole intended IP/socket owner.
- The immutable CP archive assumes selected `malloc()` blocks are zero.
  Observation-only A/B firmware proved dirty station-table allocations and an
  RTS-path HardFault with stacked PC `0xaaaaaaaa` and LR in
  `sta_mgmt_entry_init()` when clearing was disabled.
- The permanent workaround is now limited to the PID executing
  `bk_wifi_init()`. Other concurrent CP threads retain normal malloc
  semantics. The full dual build, loader reboot, COM7 RTS, AP/RPTUN status and
  a 20-message two-CPU RPMsg run passed after this narrowing.
- Official CP `wifi_deinit()` is unsupported and AP mailbox deinit is
  incomplete. AP-only restart while Wi-Fi is active must initially fail closed;
  whole-chip reset is the recovery boundary.

Canonical documents:

- [N16 worklog and plan](../docs/bk7258-t5ai/nuttx-port/prompts/16-n16-wifi-data-plane.md)
- [N16 malloc compatibility evidence](verification/2026-08-05-n16-wifi-malloc-compatibility.md)
- [ADR-007 Wi-Fi ownership](../memory/decisions/ADR-007-n16-cp-radio-ap-nuttx-network.md)
- [N15 physical symmetric lifecycle](verification/2026-08-04-n15-physical-symmetric-lifecycle.md)
- [N15 format-2 symmetric host closure](verification/2026-08-04-n15-format2-symmetric-host.md)

## Next actions

1. Resume the N16 STA path from the verified controller baseline: associate
   using runtime-only credentials and prove AP-side native NuttX link state.
2. Close DHCP and native socket traffic without importing vendor AP lwIP.
3. Run the retained RPMsg/RPMsgFS/Bluetooth coexistence gates under Wi-Fi
   traffic, then decide the next N16 substage.

## Open boundaries

- Official NuttX/apps/SDK source and static libraries remain read-only;
  permanent changes belong to project wrappers. Temporary debug edits must be
  restored.
- Wi-Fi-disabled normal profiles must remain unchanged in behavior and link
  ownership.
- N16 is STA data-plane work only. Network OTA, publisher authentication,
  signatures, key provisioning and anti-rollback are outside this stage.
- SoftAP, Wi-Fi power-save, warm restart, legacy SDK validation, upper-8 PSRAM
  allocation and QEMU remain deferred.
- Existing unrelated dirty tools, QEMU, logs and N15 scratch files are owner
  work and must not be staged with N16.
