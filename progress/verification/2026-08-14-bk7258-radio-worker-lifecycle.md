# Verification: BK7258 radio worker lifecycle boundary

- Date and time zone: 2026-08-14, Asia/Shanghai
- Verifier: Codex operating the owner's T5-Board
- Source baseline: `f717e652df0f`
- Working branch: `fix/bk7258-radio-worker-lifecycle`, uncommitted
- SDK: official Beken v3.1.1.9 CP/AP bundle and matching read-only source

## Scope

Close the Wi-Fi/Bluetooth worker-lifecycle item left by the runtime-contract
review without inventing a vendor teardown API.  Bluetooth must retain
symmetric Controller ownership across AP/CP and survive repeated close/reopen;
Wi-Fi must keep its existing whole-chip lifetime and reject AP-only restart.
Generic SDK thread-delete semantics, BL1/BL2, secondary images, filesystem,
configuration, calibration and OTP/eFuse are outside this change.

## Contract audit

- Official CP `components/bk_bluetooth/api/bt_main.c` guards duplicate init,
  returns deinit failures before clearing `bluetooth_already_init`, and clears
  it only after Controller teardown succeeds.
- Official CP `components/bk_bluetooth/ipc/src/bt_ipc_core.c` ignores the two
  real return values and emits `BT_EVENT_STATUS_NOERROR` for both operations.
- Official FreeRTOS sources and many callers use `rtos_delete_thread(NULL)` as
  self-delete.  NuttX `task_delete(0)` has the same meaning, so the earlier
  request to clear a caller-owned handle was not applied.
- Tuya TAL defers deletion for TAL-owned handles, but this is an upper-layer
  ownership service and does not replace the Beken low-adapter contract.
- The pinned Wi-Fi implementation has no complete controller/proxy teardown;
  AP-only restart therefore remains fail-closed with `-EBUSY`.

## Implementation evidence

- RPTUN control ABI gained `CP_BT_ACTIVE` at bit 17.
- CP wrappers publish active/inactive only after the real SDK operation returns
  success and retain an independent 64-byte lifecycle diagnostic record.
- AP uses explicit CLOSED/OPEN/UNKNOWN states, checks the CP-owned flag after
  every request, reconciles a lost reply only when CP already reached the
  desired state, and retains UNKNOWN state on mismatch/failure.
- A default-off Kconfig test performs ten Controller `deinit -> init` cycles
  before NuttX Host initialization.  Production profiles leave it disabled.

## Build and host results

- Validation pair: `t5_board_cp_wifi_mcuboot +
  t5_board_ap_wifi_mcuboot`, MCUboot `18.6.44`, security counter 98, ten test
  cycles.  Full CP/AP build, signing, partition verification and RPTUN ELF
  layout gate passed.
- Final production pair: the same profiles, MCUboot `18.6.45`, security
  counter 99.  Generated AP config contains
  `# CONFIG_BK7258_BT_LIFECYCLE_TEST is not set`.
- Final sparse artifacts:
  - CP `app_crc_flash.bin`, 1105920 bytes,
    SHA-256 `19c52fb021146ecda15b6a80b10eb77e85a24f24e822039f7920b8b11ee34085`;
  - AP `app1_crc_flash.bin`, 266240 bytes,
    SHA-256 `0f07a8abe9c701ad0d6d7c982d138be590ef5f3ef6375f545725ab1616a2449d`.
- Existing mailbox tests passed 31/31; PM activity and BL1 policy tests passed;
  `git diff --check` passed.  Official `nuttx/` and `apps/` have no tracked
  source changes.

## Physical-board results

COM3/BKFIL sparsely wrote only CP `0x011000 + 0x10e000` and AP
`0x165000 + 0x41000`.  Both writes reported `WriteFlash ->pass`, followed by
`Writing Flash OK` and `All Finished Successfully`.  COM4 was never opened;
P0/P1 remained SWD/RTT.  No bootloader, BL2, secondary slot, data or security
range was written.

The validation image was cold-started twice, with the second reset generated
by the established COM3 RTS 150 ms pulse.  Each reset independently reached
the post-authentication BL2 hold at `PC=0x280205a2`, `VTOR=0x28020000`, then
continued from the adjacent compare after supplying the documented `JLNK`
value.  On both runs:

- CP record at `0x28011748`: OPEN, init 11/11, deinit 10/10, last error 0;
- AP record at `0x28050940`: OPEN, init 11/11, deinit 10/10,
  requested/completed 10/10, validation status 0;
- AP unknown transitions, status mismatches and reconciled timeouts were all 0;
- RPTUN was CONNECTED with flags `0x0003ffff`, including CP_BT_ACTIVE.

This is two independent cold boots and 20 real SDK Controller close/reopen
cycles.  RTT0 then showed `bkbttest info` PASS with a valid non-fallback
address, ACL MTU/buffers 70/20.  HCI stats showed command/event 19/19,
invalid RX 0 and receive errors 0.  `apctl status` retained AP READY,
supervisor HEALTHY and both AP CPUs online.

After the production image was restored, both AP and CP records showed OPEN,
init 1/1, deinit 0/0, no errors and zero validation cycles.  `bkwifi status`
returned status 0 with the station currently disconnected; `bkbttest info`
again passed.  `apctl restart 5000` left generation 1, RPTUN CONNECTED,
CP_BT_ACTIVE set and the Bluetooth counts unchanged, matching the intentional
Wi-Fi-active `-EBUSY` boundary.

## Failures and investigation

- An external `/tmp` config mirror lacked the NuttX board `Make.defs` context
  and was rejected before compilation.  Validation was rebuilt from the real
  profile with a temporary Kconfig line, then the line and build-normalized
  defconfig changes were removed.
- J-Link `RSetType 2` and explicit reset-pin commands did not produce a
  trustworthy reset on this clone and were not counted.  COM3 RTS produced the
  expected BL2 cold-start signature and is the accepted reset evidence.
- `apctl` prints its long status before its error line; RTT no-block trim filled
  the 1 KiB channel before the final text.  The unchanged generation and full
  shared state prove that restart was not executed; source inspection anchors
  the rejecting return to `-EBUSY`.

## Residual risks

- Full NuttX Bluetooth Host unregister/re-register remains unsupported and is
  not claimed.  The verified loop is the official Controller lifecycle before
  Host ownership.
- Vendor init/deinit failure injection and intentional reply-drop reconciliation
  were source-reviewed but not forced on hardware in this run.
- Wi-Fi remains a whole-chip lifetime service until the official SDK supplies
  complete deinit and AP proxy-channel teardown.
- MIC pause/resume and high-rate USB attach pressure remain separate work.
- Development signing is not OTP-rooted production secure boot.

## Evidence locations

- Architecture decision:
  `memory/decisions/ADR-025-bk7258-radio-lifecycle-boundary.md`
- Validation RTT captures were retained during the run under
  `/tmp/bk7258-bt-lifecycle-validation.rA5t4C/`; this record contains the
  durable hashes, addresses and decoded acceptance values.
