# Current Progress

Last updated: 2026-08-24
Updated by: Codex

## Objective

Run the official openvela Agent on the T5Board AP with LCD, GT1151 touch,
microphone and AP networking, while CP retains OTA/platform NSH.

## Current state

- Branch: `feat/bk7258-openvela-agent-ap`, created directly from
  `origin/dev-ai-contest-2026@912d6aad8094` for clean PR lineage.
- Contest changes are committed on this feature branch; unrelated untracked
  logs remain excluded.
- Local `app/vela_claw` is retired and its manifest/Kconfig/launch hooks are
  removed.  The historical AP config-directory name remains unchanged.
- Official `packages/ai_agent` is enabled on AP.  UART1 is disabled and the
  board switch selects J-Link SWD.
- CP WDT build/config regressions found during recovery are fixed.
- NuttX `drivers/input/gt9xx.c` has a separate working-tree change for the
  generic touchscreen ABI and nonblocking read behavior.  It is not part of
  this contest-repository publication under the official-source boundary.

## Hardware checkpoint

- Board runs signed full package `1.68.0+69`, counter 69, with the temporary
  microphone lifecycle option disabled again.
- Package SHA-256:
  `d0fba5dec040a95fa80be8074def2d3041c87f6c5f1064231d79797a2302fe55`.
- Package structure and public BL1/BL2/CP/AP trust verification: PASS.
- BKFIL eight-segment write: PASS.
- AP is READY with no fault.  Official Agent PID 25 reached launch stage 5.
- Generic LVGL and UIKit are running and the framebuffer is scanned out, but
  owner photos show CJK text as missing-glyph boxes; UI functional acceptance:
  FAIL.
- Owner physical touchscreen check: PASS.
- PTT click dispatch reaches the Agent voice callback, but recording startup
  fails before PCM capture; the button therefore remains blue.
- AP PSRAM contributes a 320 KiB NuttX system-heap region; the Agent profile
  uses one RGB565 framebuffer.  The driver retains generic two-page support.
- TF configuration is one-bit, inserted-before-boot, no card-detect.
- Microphone `/dev/audio/pcm0c` is enabled at 16 kHz.

## Root causes closed

- Stale adjacent package extraction caused an old image to be reflashed.
- PRETIMEOUT/reset-cause incremental-build residue caused the original CP
  HardFault; the failure was not Agent image overflow.
- BK7258 framebuffer erased the requested display page in `getplaneinfo()`.
- GT9XX lacked `TSIOC_GETMAXPOINTS` and performed I2C on idle nonblocking
  reads.
- Two full framebuffers exhausted AP PSRAM available for Agent stacks.
- LVGL scheduling initially starved Agent workers.
- UIKit was not initialized before Agent CJK font creation.
- TF profile incorrectly enabled unavailable card-detect.

## Active blocker

- The Agent microphone path cannot currently open a capture backend.  This
  profile has both `CONFIG_MEDIA` and `CONFIG_AI_AGENT_AUDIO_ALSA_DIRECT`
  disabled, so `audio_capture_open()` falls through to the weak
  `media_recorder_open()` stub, which always returns `NULL`.  The registered
  `/dev/audio/pcm0c` device is not reached by the Agent flow.
- The UI is configured for a 466x466 round display although this board is
  320x480.  It requests `/data/font/MiSans-Medium.ttf`, but no verified font
  resource or `/data` provisioning exists; UIKit silently falls back to
  Montserrat, which has no CJK glyphs.  Repeated six-box rows in the owner
  photo are repeated `录音启动失败` messages, not valid text rendering.
- A source-free diagnostic AP build using the existing bounded microphone
  lifecycle test was signed and flashed as v1.67 through COM3.  The test
  reached RECEIVE but timed out (`-ETIMEDOUT`) with two channels, zero
  completed buffers, zero samples and zero energy.  The NuttX lower-half
  therefore also has an unresolved ADC/DMA/IRQ data-path failure.
- The temporary diagnostic image was replaced by signed production-config
  v1.68 through the same eight-segment path.  AP is READY with no fault; the
  diagnostic option and symbol are absent.  No OpenVela/Agent source was
  changed for this investigation.

## Next actions

1. Without modifying OpenVela source in this task, prepare the required
   upstream fixes and gates: repair MIC ADC/DMA/IRQ completion, add a real
   Agent NuttX capture backend, derive layout from 320x480, and make missing
   CJK resources an explicit failure or readable fallback.
2. Require future acceptance to prove nonzero microphone buffers/samples/
   energy and readable glyphs; device registration or framebuffer pixels are
   insufficient.
3. Verify TF mount/read/write.
4. Configure an LLM through an approved UI/control path and run one dialog.
5. Resume remaining xTS, loopback, `/data`, performance and soak phases.

## References

- [Agent AP verification](verification/2026-08-24-bk7258-openvela-agent-ap.md)
- [ADR-034](../memory/decisions/ADR-034-openvela-agent-ap-and-t5-interaction-topology.md)

## Safety constraints

- Do not erase the whole chip or modify OTP/eFuse/lifecycle/calibration.
- Do not enable UART1 while the hardware switch selects SWD.
- Do not print or record private signing keys or credentials.
- Camera and RGB LCD/touch require an explicit runtime pin-mux design before
  simultaneous use is attempted.
