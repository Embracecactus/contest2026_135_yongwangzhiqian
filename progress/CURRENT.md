# Current Progress

Last updated: 2026-08-13
Updated by: Codex

## Active objective

The merged baseline is `22c477a` on `dev-ai-contest-2026`.  The current
uncommitted branch is `fix/bk7258-wrapper-lifecycle` and addresses the next
architecture-review item: wrapper resource ownership and teardown symmetry.

The implementation, DVP hardware lifecycle check and final retained RTT-profile
check are complete on `fix/bk7258-wrapper-lifecycle` and ready for owner PR
review.  It does not redo camera bring-up, pinmux, PM or the SDK ABI phase.

## Implemented

- DVP now uses the SDK `camera_handle_t` as its only open/close ownership
  token; the duplicate `sdk_open` boolean was removed.
- A nominal successful `bk_dvp_open()` without a published handle is rejected.
- Post-open failures still use the normal `bk_dvp_close()` and PM release path.
- If SDK close succeeds but a PM put fails, a repeated imgdata uninitialize now
  retries the retained PM ownership bits even though the SDK handle is NULL.
- Standalone YUV/H.264 failed initialization and normal uninitialize now share
  one teardown function.
- Teardown stops the DMA consumer, stops H.264/YUV producers, unregisters
  callbacks and CPU1 routing, deinitializes per-stream H.264/YUV state, then
  deinitializes and frees DMA.
- Cleanup flags and the DMA id are retained when an SDK cleanup call fails.
  A later initialize retries teardown before accepting a new owner.
- Complete cleanup resets all caller pointers, dimensions, counters and state
  in one place.  Shared SDK `*_driver_init` roots remain board-lifetime state.
- The existing camera validation profile now performs two complete V4L2
  open/capture/close cycles and emits one final lifecycle verdict.

## SDK/Tuya ownership result

- Official v3.1.1.9 publishes the DVP handle only after successful open;
  failed open performs its own internal cleanup.  `bk_dvp_close()` consumes
  the published handle, frees it and returns `BK_OK`.
- Official and Tuya H.264 pipelines stop/reset producers, deinitialize H.264
  and YUV, then deinitialize/free DMA.  The wrapper follows the same ownership
  dependencies while stopping DMA consumption first.
- The SDK initially routes H.264 IRQ to CPU2.  The standalone AP helper moves
  it to CPU1 and removes CPU1 routing before deinit.  No extra CPU2 restore is
  needed: deinit closes the instance and the next `bk_h264_init()` enables its
  normal CPU2 route again.

## Verification

- Full signed `cp_nsh_drivercheck_mcuboot + ap_smp_drivercheck_mcuboot`
  final-tree build passed with version `18.6.20`, security counter `74`,
  covering the standalone YUV/H.264 helper.
- Full signed retained-profile build
  `cp_nsh_drivercheck_rtt_mcuboot + ap_smp_camera_h264_mcuboot` passed with
  final version `18.6.25`, security counter `79`.
- A signed UART0 validation variant, version `18.6.22`, counter `76`, passed
  the same build/layout gates and was sparsely written through COM3.  It kept
  P0/P1 SWD enabled, disabled only the BL2 attach hold, and never opened COM4.
- Real T5-Board H.264 cycle 1 captured 23,020 bytes with checksum `bd9203ca`;
  cycle 2 reopened the same V4L2 device and captured 23,484 bytes with checksum
  `7816b75a`.  Each cycle restored DVFS `120 -> 480 -> 120 MHz` and the target
  ended `BKCAM LIFECYCLE PASS cycles=2`.
- The first restored RTT image exposed a profile regression rather than a
  camera or probe failure: early polled RTT output did not register an NSH
  console, and the profile had drifted from the current T5/SWD, supervisor and
  coordinated-PM baseline.  The profile now registers RTT0 as the console and
  RTT1 as syslog while retaining those board features.
- Final `18.6.25` was sparsely written through COM3.  J-Link identified STAR,
  RTT0 ran NSH, RTT1 transferred a live `/dev/ttyR1` probe, and `apctl status`
  reported AP READY, RPTUN CONNECTED, supervisor HEALTHY, CPU2 online and SMP
  PASSED with no supervisor or IPI errors.
- The final image's camera diagnostic reported PASSED/result 0, a 24,836-byte
  H.264 frame with checksum `c9c031ff`, and DVFS restored `3 -> 6 -> 3`.
  `git diff --check` passes; no J-Link/RTT process was left running.

Detailed evidence and remaining runtime boundary are in the
[wrapper lifecycle record](verification/2026-08-13-bk7258-wrapper-lifecycle.md).

## Next action

Open and review the web PR from `fix/bk7258-wrapper-lifecycle`.  The standalone
YUV/H.264 helper still has no production command endpoint, so its
failure-injection cleanup path remains source/link verified rather than a
separate direct-board command test.

## Fixed constraints

- Official NuttX/apps and Beken SDK source remain read-only.
- Preserve P0/P1 SWD/RTT and COM3; never open COM4.
- Do not add one-off verification scripts when an existing real-board path is
  available.
- Do not commit, push, flash, or touch OTP/eFuse/lifecycle/debug locks without
  the corresponding owner authority.
- GPT-5.6-Luna delegation remains disabled until the owner re-enables it.
