# BK7258 Driver Review Closure

- Date and time zone: 2026-08-13 13:02 GMT+8
- Verifier: Codex
- Review session: `019ff8ae-5436-72c3-8a6f-7194fbea2fca`
- Baseline: `3b49d8ccf678ea8c67c3e8fb4e92d2e83aae20a1`
- Worktree: uncommitted changes on `feat/bk7258-sdk-abi-boundary`
- Hardware: T5-Board, COM3 download, P0/P1 SWD and RTT; COM4 was not opened

## Scope

This checkpoint closes the actionable findings from the external driver
review against the current main contest repository.  Official Beken SDK
v3.1.1.9 and Tuya sources were read-only behavioral references.  No official
NuttX or SDK source was edited.

The implementation fixes:

- the SDK-compatible `delay()` loop instead of an empty stub;
- real event, queue-reset and thread-join semantics in the OS adaptation
  layer;
- CP standby accounting when the vendor low-voltage leaf rejects entry, and
  the redundant successful-path CP AON clear that could race AP0;
- process-lifetime DMA/YUV/JPEG/H264 SDK roots shared by camera, codec and MIC
  wrappers;
- AUD/MIC stop completion and worker/hardware teardown;
- BLE GATT initialization cleanup and one-time static attribute registration;
- I2S mono/stereo wire-slot handling without holding the state mutex through
  FIFO polling;
- watchdog re-arm error propagation and initialization retry;
- GT1151 IRQ preflight before the upstream GT9xx driver creates `/dev/input0`;
- semantic CP/AP RPTUN and Wi-Fi profile pairing, including MCUboot variants;
- RPTUN exclusive-state verification across compiler-local symbol suffixes and
  profiles where an AP SMP symbol is intentionally absent.

## Review decisions that did not become code changes

- Classic Make remains the authoritative build path.  The new media-root
  source was added to both source lists, but full CMake/Make parity is not
  claimed: the current CMake path also has a pre-existing `arm_vectors.c`
  collision and needs a separate build-system task.
- Shared wire structures were not made `packed`; their current static size and
  offset assertions preserve the aligned CP/AP ABI, while packing would add
  unaligned-access risk without fixing a demonstrated mismatch.
- No unlocked cleanup was added after an AP-supervisor `nxmutex_lock()` hard
  failure.  NuttX already retries interrupt/cancellation cases internally;
  the remaining errors imply invalid state or corruption and cannot be safely
  repaired without the lock.
- Broad checkpatch cleanup was not mixed into the behavioral fix batch.

## Host build and verification

The following completed successfully:

1. `cp_nsh_drivercheck + ap_smp_drivercheck` full dual-image build.
2. SDK provenance, partition/factory-layout and CP/AP RPTUN layout checks.
3. RPTUN mailbox host suite: 31/31 passed.
4. PM activity host test and BL1 policy test.
5. `git diff --check`.
6. Signed retained-service builds
   `cp_nsh_wifi_rtt_mcuboot + ap_smp_wifi_mcuboot`: extended-test version
   `18.6.9`, security counter `63`, followed by exact-final-source version
   `18.6.10`, security counter `64`.

Signing used temporary development software keys outside the repository.  It
does not establish OTP/eFuse-rooted production secure boot.

## Physical-board verification

The signed retained-service images were sparsely written through COM3.  BKFIL
reported success for BL1, CP, AP, primary BL2 and secondary BL2 in both runs.
LittleFS, `usr_config` and calibration were preserved.  Extended-test and
exact-final-source download evidence is under:

- `/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-124335`
- `/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-125919`

J-Link identified the STAR core over P0/P1 SWD at 1 MHz.  The existing BL2
`JLNK` hold was released without halting or resetting the target.  RTT0 was
attached at the current ELF's `_SEGGER_RTT` address `0x2802b9a0`; the agent
operated the NSH session directly and then shut down all host J-Link/RTT
processes.

On the extended 18.6.9 run, initial `apctl status` reported:

- AP `READY`, RPTUN `CONNECTED`, all readiness flags set and no pending
  doorbells;
- AP supervisor `HEALTHY`, reason `NONE`, zero faults/recoveries;
- CPU2 `SCHEDULER_ONLINE` and both AP CPUs online;
- SMP, affinity, semaphore-wake, semaphore-loop and lifecycle tests `PASSED`.

`bkrpmsgtest all 20 30000` passed all six idle/load and 1/64/464-byte
combinations.  Both AP CPUs sent and received 20 messages in every run, for
240 successful request/reply operations with zero errors.  The start, spawn
and report heap snapshots were identical in all runs, and the command ended
`BRPT SUITE PASS runs=6 count=20`.

`bkbttest all 1000 15000` passed controller information and a one-second BLE
scan.  The controller address was valid and non-fallback, ACL MTU/buffer count
was 70/20, and seven advertisers were observed.  The final `apctl health`
snapshot remained `HEALTHY` with zero faults, recoveries, consecutive failures
or last error.

The only source adjustment after that extended run made cross-task SDK thread
join retry a normal `-EINTR` from its 10 ms poll instead of returning failure.
After the full driver-check rebuild passed, the exact final source was rebuilt
and signed as `18.6.10`/counter `64`, then sparsely flashed again.  On that
image `apctl status` again reported AP `READY`, RPTUN `CONNECTED`, both AP CPUs
online, all built-in SMP/wake/lifecycle checks `PASSED`, and supervisor
`HEALTHY` with zero faults/recoveries.  `bkbttest info 15000` passed with a
valid non-fallback address and ACL MTU/buffer count 70/20.  A final
`apctl health` retained zero fault, recovery, consecutive-failure and last
error values.  This closes the exact-source-to-board evidence gap without
claiming a second long suite.

## Residual risks and evidence boundary

- The retained RTT profile validates boot, SMP/RPTUN, OS adaptation and
  retained BT service behavior.  Its system wakelock means this run is not
  evidence that the newly corrected vendor-leaf rejection branch actually
  entered or rejected low-voltage standby.
- Media-root, AUD/MIC, I2S, watchdog and GT1151 changes passed the full
  driver-check build but were not all exercised by this retained-service
  runtime image.  Existing camera and coordinated-PM board records remain
  separate evidence; a future peripheral matrix or soak is still useful.
- QEMU was not used and no QEMU BK7258-model coverage is claimed.  Cache,
  interrupt timing and physical pin behavior remain real-board concerns.
- Full CMake parity is still open as a separate task.  Classic Make is the
  validated path for this checkpoint.
- The changes are intentionally uncommitted and unpushed pending owner review.
