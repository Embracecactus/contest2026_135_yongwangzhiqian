# Current Progress

Last updated: 2026-08-25
Updated by: Codex

## Objective

Run the official openvela Agent on the T5Board AP with LCD, GT1151 touch,
microphone, speaker and AP networking, while CP retains OTA/platform NSH.

## Current state

- PR #76 is merged in `origin/dev-ai-contest-2026`.  The follow-up branch
  `fix/bk7258-openvela-agent-audio` remains directly based on that branch.
- The BK7258 chip layer now supplies the official Agent's `media_recorder_*`
  and PCM buffer-mode `media_player_*` ABIs over the public NuttX audio
  upper-half.  The profile still does not enable the full media framework.
- The microphone lower-half now verifies the pinned SDK DMA programming,
  retries through the SDK API when it is incomplete, and delivers real PCM
  through ADC/DMA/IRQ.  ADC and DAC sessions share an explicit chip-layer
  ownership guard.
- Recorder close treats every nonnegative `AUDIOIOC_FREEBUFFER` return as
  success.  This releases all buffers, the message queue and the file
  descriptor, so repeated PTT sessions no longer fail with `-EACCES`.
- The Agent uses a separate on-chip persistent partition CSV.  The base
  partition layout remains unchanged.  CP owns the persistent volume at
  `/data`; AP mounts it through RPMsgFS at `/cpdata`.
- UIKit loads the provisioned MiSans resource from `/cpdata/font`, and the
  Agent data directory is `/cpdata/agent`.
- The compact 320x480 PTT/UI lifecycle change is in the separate
  `packages/ai_agent` repository and must be published as a companion PR.
- The separate NuttX GT9XX working-tree change remains outside this contest
  repository publication.

## Hardware acceptance

- Clean CP/AP/BL2/BL1 build: PASS.
- Signed full package: `1.75.0+80`; SHA-256
  `17e22736729d05081a950ee3eee8492b3765e087d07693f30e9a98b713f00f20`.
- Package structure and public BL1/BL2/CP/AP trust verification: PASS.
- Nine-segment full flash, including the Agent data partition: PASS.
- AP READY with error zero; official Agent and LVGL UI running.
- Full MiSans resource provisioned; Chinese text and the compact official
  dark UI render correctly.  Idle PTT is blue and active PTT is red without
  disappearing.
- Owner completed three consecutive PTT start/stop rounds.  Recorder start,
  worker exit and close counts were all 3; captured byte counts were 216320,
  184960 and 229120.  There was no incomplete close, enqueue `-13`, or
  `voice_channel_start` failure.
- Final framebuffer returned to the blue `请说` state and showed three
  `未识别到语音` results.  ASR returned `-2` because credentials are not yet
  configured; this is no longer a microphone/PTT transport failure.

## Remaining work

1. Publish the companion `packages/ai_agent` compact-display PTT/UI change.
2. Configure approved ASR/LLM credentials and complete one real dialog.
3. Verify TF mount/read/write.
4. Complete remaining xTS, loopback, performance and soak phases.
5. Keep the NuttX GT9XX generic ABI fix in its own upstream change.

## References

- [Agent AP verification](verification/2026-08-24-bk7258-openvela-agent-ap.md)
- [ADR-034](../memory/decisions/ADR-034-openvela-agent-ap-and-t5-interaction-topology.md)

## Safety constraints

- Do not erase the whole chip or modify OTP/eFuse/lifecycle/calibration.
- Do not enable UART1 while the hardware switch selects SWD.
- Do not print or record private signing keys or credentials.
- Camera and RGB LCD/touch require an explicit runtime pin-mux design before
  simultaneous use is attempted.
