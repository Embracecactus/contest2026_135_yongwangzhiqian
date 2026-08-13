# BK7258 Wrapper Lifecycle and Teardown Symmetry

- Date: 2026-08-13
- Baseline: `22c477a1d05d376057c4846d0fce45e9824db68f`
- Branch: `fix/bk7258-wrapper-lifecycle`
- State: ready for PR review

## Conclusion

This phase closes the source-level ownership gaps identified for DVP and the
standalone YUV/H.264 helper.  A wrapper can no longer overwrite or forget the
only token for a live SDK resource, and failed initialization uses the same
cleanup path as normal close.

The normal V4L2 camera path also completed two consecutive real-board
open/capture/close cycles.  The second open, frame and close provide direct
runtime evidence that the first close released enough SDK, PM and V4L2 state
for reuse; this is not inferred from the earlier one-frame camera bring-up.

## Read-only reference audit

Official Beken v3.1.1.9 was treated as the ownership authority:

- `components/bk_dvp/src/bk_dvp.c`: failed `bk_dvp_open()` cleans its private
  allocation and publishes `*handle` only after complete success;
  `bk_dvp_close()` deinitializes and frees a published handle.
- `middleware/driver/h264/h264_driver.c`: H.264 deinit disables encode,
  interrupt, clock and power and unregisters all H.264 callbacks.
- `middleware/driver/yuv_buf/yuv_buf_driver.c`: YUV deinit stops/resets the
  engine, disables clock/interrupt/power and unregisters callbacks.
- `middleware/driver/general_dma/dma_driver.c`: DMA deinit clears channel and
  callback state; DMA free separately returns the allocation to the pool.
- `components/multimedia/pipeline/h264_encode_pipeline.c`: normal release
  stops/reset H.264/YUV and then deinitializes/frees DMA.

Tuya T5AI was used only as a call-order cross-check.  Its H.264 pipeline and
`tkl_dvp.c` use the same per-instance H.264/YUV teardown followed by DMA
deinit/free.  Neither official nor Tuya source was edited.

## DVP changes

- Removed the parallel `sdk_open` boolean.
- The non-NULL `camera_handle_t` is the sole SDK ownership token for stop,
  uninitialize, buffer, capture, suspend and resume.
- Reject `BK_OK` from open if no handle was published.
- Preserve the existing normal-close rollback for every failure after handle
  publication.
- Retry retained PM clock/MCLK references when a repeated imgdata uninitialize
  sees a NULL handle after an earlier PM put failure.

The last point closes a real retry hole: SDK close can be complete while PM
cleanup reports an error.  NULL handle then means “SDK resource gone,” not
“every wrapper-owned resource gone.”

## Standalone YUV/H.264 changes

- Added one locked teardown path for initialize failure and normal close.
- Stop DMA consumption first; stop/disable and reset YUV/H.264; unregister
  callbacks and CPU1 IRQ route; deinitialize H.264/YUV; deinitialize and free
  DMA only after channel state is gone.
- Keep per-resource flags and DMA id on cleanup error and retry cleanup before
  accepting a later initialize.
- Reset the complete owner object only after no resource tokens remain.
- Keep shared media driver roots initialized because they are board-lifetime
  resources shared with DVP and other media clients.

H.264 CPU2 routing is not restored during teardown.  The helper removes its
CPU1 route before H.264 deinit; the closed instance has no live interrupt, and
the next SDK `bk_h264_init()` establishes the normal CPU2 route itself.

## Host verification

The following full signed builds passed with SDK checksum, partition, CP/AP
pair binding, factory-layout and RPTUN-layout checks:

- `cp_nsh_drivercheck_mcuboot + ap_smp_drivercheck_mcuboot`, version
  `18.6.20`, counter `74`, covering the standalone YUV/H.264 helper on the
  final focused tree.
- `cp_nsh_drivercheck_rtt_mcuboot + ap_smp_camera_h264_mcuboot`, version
  `18.6.25`, counter `79`, covering the corrected retained DVP/H.264 debug
  profile.
- A temporary `cp_nsh_drivercheck_uart0_mcuboot +
  ap_smp_camera_h264_mcuboot` validation pair, version `18.6.22`, counter `76`,
  covering the exact AP lifecycle implementation used on hardware.
- `git diff --check` passed; generated defconfig changes were restored.

Temporary signing keys stayed under `/tmp` with mode `0600`.  These builds do
not establish OTP/eFuse-rooted secure boot.

## Physical-board verification

The signed `18.6.22` pair was sparsely written through COM3.  BKFIL reported
successful writes for BL1, primary and secondary BL2, CP and AP.  LittleFS,
`usr_config` and the calibration tail were preserved.  The download evidence
is `/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-173628`.
COM4 was never opened.

The validation pair used the already supported UART0/COM3 console at 115200
baud because the attached clone J-Link did not enumerate SW-DP in this run.
Its repeated RESET pin-15 warning is a known clone-probe artifact and is not
treated as the root cause.  The same connection failure occurred with RTT
absent and BL2 hold disabled, so it does not implicate RTT or camera teardown.
The validation build still retained P0/P1 SWD routing.  Its temporary config
and build-script allow-list entry were removed after capture.

After the UART camera run, restoring signed retained-profile `18.6.23`, counter
`77`, exposed a configuration regression.  It built successfully, but
`CONFIG_BK7258_CONSOLE_RTT` supplied only early polled output: RTT0 was not
registered as `/dev/console`, NSH exited, and CP remained in idle.  The profile
had also drifted from current `cp_nsh_drivercheck_mcuboot`: it lacked the T5
board selection, AP supervisor and coordinated PM, so the stale SWD selection
did not survive Kconfig dependency resolution.  This was not a camera,
`up_idle()` or physical RTT failure.

The retained profile now explicitly registers RTT0 and RTT1 serial devices,
uses RTT0 for NSH and RTT1 for syslog, and includes the current T5-board, SWD,
AP-supervisor and coordinated-PM baseline.  Version `18.6.24`, counter `78`,
was an intermediate RTT-console diagnostic only.  Full alignment was rebuilt
as signed version `18.6.25`, counter `79`; all checksum, partition, CP/AP pair,
factory-layout and RPTUN-layout gates passed.

Final `18.6.25` was sparsely written through COM3.  BL1, primary/secondary
BL2, CP and AP all passed, while LittleFS, `usr_config` and calibration were
preserved.  The download evidence is
`/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260813-182454`.
COM4 was never opened.

After the BL2 `JLNK` word was consumed, the replugged P0/P1 probe identified
STAR over SWD.  RTT found its control block at `0x28014260` and enumerated
channel 0 as `Terminal` and channel 1 as `/dev/ttyR1`.  RTT0 NSH returned:

```text
AP state=READY(2) error=0
RPTUN state=CONNECTED(4) error=0 flags=00007f3f
AP supervisor state=HEALTHY(2) reason=NONE(0)
CPU2 state=SCHEDULER_ONLINE(8) error=0
AP SMP state=PASSED(4) error=0 online=00000003
```

Supervisor fault/recovery counts and IPI duplicate/lost/failure/stale/spurious
counts were all zero.  With the RTT1 reader live, writing through
`/dev/ttyR1` produced the exact marker
`BK7258_RTT1_LIVE_PROBE_18_6_25`, proving the second device/channel path.  The
generated configuration contains `CONFIG_SYSLOG_RTT=y` with channel 1.

The final AP camera diagnostic at `0x28051ebc` contained magic `4d414342`,
state `PASSED(2)`, result `0`, H.264 size 24,836 bytes, checksum `c9c031ff` and
DVFS before/active/after ids `3/6/3`.  This is a final retained-profile check;
the two-cycle open/close/reopen proof remains the UART run below.  All J-Link
Commander, RTT Logger and RTT Client processes started for this check were
stopped afterward.

The target produced these two independent H.264 frames:

```text
BKCAMH264 PASS bytes=23020 checksum=bd9203ca size=640x480 dvfs=3->6->3 transitions=2
BKCAM LIFECYCLE cycle=1 PASS
BKCAMH264 PASS bytes=23484 checksum=7816b75a size=640x480 dvfs=3->6->3 transitions=2
BKCAM LIFECYCLE cycle=2 PASS
BKCAM LIFECYCLE PASS cycles=2
```

Frequency ids `3/6/3` are the wrapper's `120/480/120 MHz` policy points.  A
cycle PASS is emitted only after STREAMOFF and close succeed, DVFS returns to
120 MHz, both AP activity votes were observed active during capture and both
were zero after close.  The second STREAMON and distinct frame checksum prove
the reopen reached the physical H.264 pipeline rather than reusing the first
result.

The SDK also logged its existing GPIO27-already-mapped warning on the second
open, then completed STREAMON, frame capture and close successfully.  This is
recorded as retained SDK pin-map behavior, not hidden as a clean log.

## Evidence boundary

- No official SDK, NuttX or apps source was changed.
- No QEMU result is used as cache, interrupt, timing or hardware-lifecycle
  evidence.
- Sparse flash changed only the executable ranges described above.  No
  OTP/eFuse, security lifecycle, rollback fuse or debug-lock state was written.
- The standalone YUV/H.264 helper has no current production command endpoint;
  its new lifecycle path is source- and link-verified only.
- The board run closes DVP open-close-reopen only; it does not synthesize SDK
  cleanup failures or claim that every standalone-helper rollback branch ran
  on hardware.
- RTT1 device transport and compiled syslog routing were verified.  NSH in
  this profile has no `syslog` command, so the live marker was written through
  `/dev/ttyR1` rather than presented as an emitted application syslog record.
