# BK7258 Runtime Contract Closure

- Date and time zone: 2026-08-14 GMT+8
- Verifier: Codex
- Baseline: `d81d32e`
- Implementation commit: `616fadb`
- Branch: `fix/bk7258-runtime-contracts`
- Remote: `fork/fix/bk7258-runtime-contracts`
- Hardware: T5-Board, COM3 download, P0/P1 SWD; COM4 was not opened

## Scope

This checkpoint addresses the actionable findings in the 66/100 independent
review of `board/bk7258`: PM transaction consistency, RPMsg construction
rollback, SDK OS-adapter semantics and retryable driver teardown.  Official
Beken v3.1.1.9 and NuttX sources were used as read-only contract references.

## Results

- PM IPC version 4 uses generation plus sequence identity.  AP retries the same
  transaction; CP commits once and replays the cached reply.  Stale and
  conflicting requests fail without applying a second resource transition.
- RPMsg health/test partial initialization now unwinds callbacks, endpoints,
  semaphores and joinable workers in reverse order.
- Queue-front, queue-reset, recursive mutex, local IRQ-state and semaphore
  maximum-count behavior now match their SDK-facing contracts.
- I2C, timer, WDT, USB-host and scale/rotate state changes are conditional on
  vendor success and preserve ownership needed for cleanup retry.  MIC rejects
  a null `AUDIOIOC_GETBUFFERINFO` argument in release builds.
- WDT and timer have default-off, validation-only SDK-boundary fault injection.
  The timer default/range is now channel 3 through 5 because the pinned AP SDK
  exports `CONFIG_TIMER_SUPPORT_ID_BITS=0x38`; the former channel-1 default was
  not supported by this binary bundle.
- `rtos_delete_thread()` was intentionally not changed: official v3.1.1.9 also
  treats the task handle as caller-owned and ignores the delete return.  The
  CH34x/USB `HPWORK` enqueue warning was not treated as a demonstrated High
  because the current kernel work-queue contract accepts those valid arguments;
  the independent USB ownership/teardown finding was fixed.

## Build and host verification

- The final full signed CP/AP build passed for
  `t5_board_cp_app_mcuboot + t5_board_ap_camera_validation_mcuboot`, version
  `18.6.42`, security counter `96`, using the board's existing external
  development trust key.
- Final physical artifacts:
  - CP `app_crc_flash.bin`:
    `6a7f586b4176e5bc738656ea807108348b054a30cfe7f1b6c66f07e0e4fbe242`
  - AP `app1_crc_flash.bin`:
    `c72897e6d083b6618feac102be2f0d68eb3c8aeffb2eeb948c736a69d6bd410a`
- Production config has PM, WDT and timer fault injection disabled; the final
  CP/AP ELFs contain none of their validation symbols.  All ten supported
  profile pairs passed metadata gates; missing AP supervisor/coordinated PM
  and insufficient PM domains are rejected.
- `board/bk7258/tests/run_tests.sh` passed mailbox 31/31, PM activity and BL1
  policy tests.  `git diff --check` and shell syntax validation passed.

## Physical-board verification

BKFIL through COM3 sparsely erased and wrote only:

- CP: `app_crc_flash.bin@0x11000-0x3d000`
- AP: `app1_crc_flash.bin@0x165000-0x30000`

It reported `WriteFlash ->pass` for both ranges, `Writing Flash OK` and
`All Finished Successfully`.  No bootloader, B slot, filesystem, configuration,
calibration or OTP/eFuse range was written.

The board's installed BL2 public key matched the final signed pair.  BL2 reached
its post-authentication, post-vector-check SWD hold.  This clone probe's old
firmware could not access the release word at `0x2809f7f0`, so the verifier
advanced only the final compare instruction after confirming PC, registers,
image headers and public key.  No validation or boot-policy instruction was
skipped.

The final production image reached:

- CP PC `0x02010718`, Thread mode, interrupts enabled;
- RPTUN state 4 (`CONNECTED`), flags `0x7f3f`, error 0;
- AP supervisor state 2 (`HEALTHY`), reason 0, zero fault/recovery state;
- CPU2 state 8 (`SCHEDULER_ONLINE`), AP online mask `0x3`;
- camera state 2 (`PASSED`), result 0, JPEG size 7307 bytes;
- WDT active with its production 8000 ms timeout;
- CP and AP PM sequence 22, with resource refs, votes and pending transaction
  state returned to zero.

Earlier validation images with one and three deliberately dropped PM replies
showed a single CP-side commit plus cached replay/pending recovery.  They did
not multiply the CP clock reference or frequency vote.

Two later validation-only images exercised the WDT/timer error paths.  Version
`18.6.40`/counter 94 proved WDT recovery and exposed that the old timer-channel
default was outside the SDK's `0x38` capability mask.  After fixing the range,
version `18.6.41`/counter 95 proved on hardware that:

- WDT initialization rejected both feeder-timer-stop and AON-stop failures;
  WDT stop and PM-restore failures returned `-EIO` without advertising a false
  inactive/active state, and the normal retries restored an active watchdog.
- injected timer stop failure returned `-EIO` while NuttX active and SDK
  hardware-enable state both remained 1; the normal retry changed both to 0.
- the same run still reached camera `PASSED` (7395-byte JPEG), RPTUN
  `CONNECTED`, both AP CPUs online, and balanced PM references/votes.

The validation-only defconfig lines were removed before the final production
build and sparse reflash.

## Residual risks

- This closes the named defects but does not independently rescore the tree.
- Remaining vendor-deinit paths, Wi-Fi/BT worker churn, MIC pause/resume and
  high-rate USB insertion still need targeted hardware fault/stress injection.
- Checkpatch debt remains outside this correctness batch.
- Development signing does not establish an OTP-rooted production chain.  The
  read-only anti-rollback floor is unchanged and no irreversible security state
  was touched.
