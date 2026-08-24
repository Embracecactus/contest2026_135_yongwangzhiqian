# BK7258 official openvela Agent AP integration

Date: 2026-08-25
Board: T5Board

## Accepted implementation

- Official `packages/ai_agent` runs on AP; local `app/vela_claw` remains
  retired.  CP retains UART0 NSH, OTA and platform health.
- The BK7258 chip layer implements the Agent recorder and PCM player ABIs on
  the public NuttX audio upper-half.  The full media framework remains off.
- The microphone lower-half verifies and, through public SDK calls, retries
  incomplete DMA setup.  ADC/DMA/IRQ now delivers real 16 kHz PCM.
- ADC and DAC lower-halves coexist in the image but hold a mutually exclusive
  shared-audio session while reserved.
- Recorder teardown accepts nonnegative NuttX ioctl success values and frees
  every audio buffer before closing its queue and device descriptor.
- An Agent-specific partition CSV allocates the aligned on-chip gap before
  immutable factory/calibration sectors.  The base CSV is unchanged.
- CP owns the persistent volume at `/data`; AP accesses it through RPMsgFS at
  `/cpdata`.  UIKit fonts and Agent data are read from that shared volume.
- The companion Agent UI uses a compact 320x480 layout and generation-guarded
  asynchronous PTT lifecycle.  That source belongs to `packages/ai_agent`,
  not this contest repository.

## Build and package verification

- Clean CP/AP/BL2/BL1 build: PASS.
- Signed package: `1.75.0+80`.
- Package SHA-256:
  `17e22736729d05081a950ee3eee8492b3765e087d07693f30e9a98b713f00f20`.
- Package structure and public BL1/BL2/CP/AP signature verification: PASS.
- Full MiSans data image SHA-256:
  `4c03f001c3d8d56f67d60753efe27aa601b23f956efdb9f66b1afc8363e32982`.

## Physical acceptance

- Nine-segment full flash, including the persistent data segment: PASS with
  `{All Finished Successfully}`.
- Non-halting target state: AP READY, error zero, Agent and LVGL UI running.
- Framebuffer and owner observation: official dark UI renders correctly on
  320x480; Chinese text is readable; idle PTT is blue and active PTT is red.
- Owner completed three consecutive PTT start/stop rounds.
- Recorder starts: 3; worker exits: 3; closes: 3.
- Captured bytes: 216320, 184960 and 229120.
- No `close incomplete`, `AUDIOIOC_ENQUEUEBUFFER -13`, or
  `voice_channel_start` failure occurred.
- Final framebuffer returned to blue `请说` and contained three
  `未识别到语音` results.  ASR returned `-2` because credentials were not
  configured; microphone delivery and repeated PTT teardown passed.

## Remaining checks

- Publish and review the companion `packages/ai_agent` UI change.
- Configure ASR/LLM and verify one real conversation.
- Verify TF mount/read/write and remaining performance/soak phases.
- Publish the separate NuttX GT9XX generic touchscreen ABI fix upstream.

## Publication boundary

- No NuttX, SDK, media framework or official Agent source is modified by the
  contest-repository commit.
- Hardware logs, J-Link temporaries, build products, keys and credentials are
  excluded.
- The immutable EasyFlash/RF/network tail and calibration data are unchanged.
