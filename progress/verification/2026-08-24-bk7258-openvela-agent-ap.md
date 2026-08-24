# BK7258 official openvela Agent AP integration

Date: 2026-08-24
Board: T5Board

## Accepted implementation

- Official `packages/ai_agent` runs on AP; local `app/vela_claw` is retired.
- CP retains UART0 NSH/OTA/platform health.  UART1 remains disabled; J-Link
  SWD is the AP diagnostic path.
- BK7258 RGB framebuffer now preserves the requested `display` and exposes
  page 0/page 1 information compatible with generic LVGL NuttX fbdev.
- The tested workspace's separate NuttX GT9XX working-tree change implements
  `TSIOC_GETMAXPOINTS` and honors `O_NONBLOCK` when no touch IRQ or synthetic
  TOUCH_UP is pending.  Official NuttX source is not part of this contest PR.
- The board starts generic `lv_nuttx_init()`, generic touchscreen and UIKit in
  the official order, with a bounded LVGL timer cadence.
- A 320 KiB block from the AP-local PSRAM allocator is registered as NuttX
  system-heap region 2.  The Agent profile uses a single 320x480 RGB565
  framebuffer, leaving room for Agent and worker stacks.
- TF is configured for one-bit, inserted-before-boot operation with
  `MMCSD_HAVE_CARDDETECT` disabled.  MIC remains enabled at 16 kHz.

## Verification

- Final clean CP/AP/BL2/BL1 build: PASS.
- `git diff --check`: PASS in the contest repository and for the NuttX GT9XX
  change.
- Initial integration package before physical diagnostics: `1.66.0+67`,
  security counter 67:
  `545707dced69d22bc1f2eea9405e0c205e320d2a7bfdabe9054695da10d49c35`.
- Package structure: PASS, eight images.
- Public BL1/BL2/CP/AP signature verification: PASS.
- BKFIL eight-segment write: all writes PASS, final
  `{All Finished Successfully}`.
- Final non-halting SWD probe:
  - AP READY, error zero, no AP fault record;
  - official Agent PID 25, launch stage 5;
  - LVGL UI initialized/running (`0x00000101`);
  - LVGL loop advanced 2294 iterations, last idle 2 ms, sleep returned zero;
  - scanout base `0x60770020`;
  - framebuffer contains dark Agent background `0x18c5` and rendered button
    pixels `0x4c9b/0xffff`.
- Owner photo confirms framebuffer scanout and widgets, but visible CJK text
  is rendered as missing-glyph boxes: UI functional acceptance FAIL.
- Owner physical touchscreen check: PASS.
- Physical PTT attempt: FAIL before capture.  The click reached
  `voice_channel_start()`, but `media_recorder_open()` returned `NULL`,
  `voice_channel_start()` returned `-EIO`, and the PTT button remained blue.
- Root-cause verification: this AP profile disables both `CONFIG_MEDIA` and
  `CONFIG_AI_AGENT_AUDIO_ALSA_DIRECT`; the linked `media_recorder_open` symbol
  is the weak Agent stub that unconditionally returns `NULL`.  Therefore the
  registered `/dev/audio/pcm0c` device was not opened and no microphone PCM
  samples were captured.
- UI root-cause verification: `lvgl_ui_channel.c` hard-codes a 466x466 round
  layout on the 320x480 panel and requests `MiSans-Medium` from the unprovisioned
  `/data/font` path.  UIKit's enabled default-font fallback returns Montserrat,
  so all Chinese labels and repeated `录音启动失败` messages become boxes.
- Source-free microphone diagnostic preparation: existing BK7258 lifecycle
  validation was enabled only in generated build state; AP build, signed
  v1.67 package structure and public BL1/BL2/CP/AP verification: PASS.
- COM3 eight-segment write of diagnostic v1.67: PASS.  The lifecycle test
  opened, reserved, configured and started the two-channel device, then failed
  at RECEIVE with `-ETIMEDOUT`: zero completed buffers, zero samples and zero
  energy.  This proves an additional lower-half ADC/DMA/IRQ delivery failure;
  ambient room sound never reached a NuttX audio buffer.
- Production restoration: diagnostic config removed, AP rebuilt from unchanged
  source, signed package `1.68.0+69` created and publicly verified, then all
  eight segments written through COM3 with `{All Finished Successfully}`.
  Package SHA-256:
  `d0fba5dec040a95fa80be8074def2d3041c87f6c5f1064231d79797a2302fe55`.
  Final non-halting SWD state: AP READY, error zero, Agent/UI initialized and
  running; microphone diagnostic symbol absent.

## Remaining physical checks

- Repair and re-verify PTT plus `/dev/audio/pcm0c` capture through the Agent
  voice flow.
- Repair and re-verify panel-sized layout plus CJK font provisioning/fallback;
  framebuffer pixels alone are not UI acceptance evidence.
- TF mount/read/write verification, LLM configuration and a real conversation.
- Camera runtime pin mux, remaining xTS, GPIO/UART loopback, `/data`,
  performance runs and soak.
