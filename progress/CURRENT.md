# Current Progress

Last updated: 2026-08-26
Updated by: Codex

## Objective

Run the official openvela Agent on the T5Board AP with LCD, GT1151 touch,
microphone, speaker, TF storage and AP networking, while CP retains
OTA/platform NSH.

## Current state

- The Agent audio commits are merged in `origin/dev-ai-contest-2026` at
  `8ff9deaf9e389ca9029a17602643abf08bc2d705`.  The clean follow-up branch
  `fix/bk7258-agent-display-tf-full-image` contains published implementation
  commit `d961ad4fa0d0930b9e52b0007af9d2dd441a0111`; this docs-only checkpoint
  records that state.
- The BK7258 chip layer supplies the official Agent recorder and PCM-player
  ABIs over NuttX audio.  Recorder close releases all buffers and shared
  ADC/DAC ownership prevents conflicting sessions.
- CP owns the Agent persistent volume at `/data`; AP consumes it through
  RPMsgFS at `/cpdata`.  UIKit loads MiSans from `/cpdata/font`, and Agent data
  lives at `/cpdata/agent`.
- The official Agent UI remains unchanged.  A generic BK7258 LVGL adapter uses
  DMA2D to aspect-fit its 466x466 canvas into the centered 320x320 panel area.
  Two media-YUV-backed RGB565 pages flip on LCD EOF; the 80-pixel top and
  bottom bands remain black.
- The Agent profile uses a 512 KiB AP PSRAM system-heap region, LVGL's C
  allocator and generic `FB_SYNC` double buffering.
- The pinned AP SDK disables `CONFIG_SDIO_PM_CB_SUPPORT`.  The CP cross-core PM
  service therefore owns a paired SDIO BAKP+clock lifetime for the complete
  Agent/Wi-Fi workload.
- The separate NuttX GT9XX working-tree change remains outside this contest
  repository publication.

## Hardware acceptance

- Clean production CP/AP/BL2/BL1 build: PASS.  CP config SHA-256 is
  `1b8ff42d7380af40339a71cceed02e418e9d423ad500845b60a3a894b67d70f7`;
  AP config SHA-256 is
  `17ea1506701b107a824bc6b6a07573503bea7158855fd710614e1ad2a0a94011`.
- Signed internal package `1.86.33+125`: structure and public BL1/BL2/CP/AP
  trust PASS; SHA-256
  `0b937f69000fe6b3eab4159e39ed31d165c576eea1811fc0f6935972c21bae09`.
- The sole operator image is one `0x7fa000`-byte BKFIL input with SHA-256
  `c3548a18fa52cde3348cb18d9b8924490467923d3d2eeff0b23a0f29830bc657`.
  Two base captures produced the same image; two later 115200-baud retained-
  range reads matched the image byte-for-byte.
- COM3 BKFIL saw one `[0]` input, one erase and one write, then reported
  `Writing Flash OK` and `{All Finished Successfully}`.  The immutable tail,
  OTP/eFuse, lifecycle and calibration were not written.
- Final COM3 boot: Wi-Fi init, `/dev/mmcsd0p1` FAT mount, LCD and `AI Agent
  ready` all PASS.  SDIO status `0x60000847` is absent.
- The owner accepted the complete centered UI, Chinese font rendering, EOF
  page flips and one controlled PTT start/stop round.  Earlier three-round
  recorder teardown acceptance remains valid.

## TF/SDIO acceptance

- Git commits `3b4a971` and `a8cc60b` remain the minimal one-bit/four-bit TF
  baselines; they did not exercise the full Agent/Wi-Fi PM workload.
- Full-product v123 passed two 4096-byte FAT write/read cycles.  Clean v124
  removed BAKP ownership and reproduced CMD0/CMD8 status `0x60000847` with
  mount `-110`; otherwise matched v125 restored only the composite ownership
  and mounted FAT with Wi-Fi, LCD and Agent ready.
- `chips/bk7258/cp/bk7258_pm_server.c` votes ADK `BAKP_SDIO` ID 91 on before
  the first SDIO clock enable and releases it after the last disable, using
  the existing PM-server reference lifecycle.  No temporary validator remains
  enabled in the production profile.

## Packaging acceptance

- `bk7258.py package materialize` is the sole public full-image command; no
  additional Python file or entry point was added.
- The command cryptographically verifies the signed full-update package,
  binds the exact base SHA-256, preserves `usr_config` and layout holes, and
  stops at the immutable-tail boundary.
- Re-materializing v125 through that command produced the accepted byte-exact
  image.  A forged full-update signature and a symlink base were both rejected
  without output; signed v1 package structure/trust compatibility also PASS.

## Remaining work

1. The owner creates and reviews the remote PR from the published branch.
2. Configure approved ASR/LLM credentials and complete one real dialog.
3. Complete remaining xTS, loopback, performance and soak phases.
4. Keep the NuttX GT9XX generic ABI fix in its own upstream change.

## References

- [Agent AP verification](verification/2026-08-24-bk7258-openvela-agent-ap.md)
- [ADR-034](../memory/decisions/ADR-034-openvela-agent-ap-and-t5-interaction-topology.md)

## Safety constraints

- Do not erase the whole chip or modify OTP/eFuse/lifecycle/calibration.
- Do not enable UART1 while the hardware switch selects SWD.
- Do not print or record private signing keys or credentials.
- Camera and RGB LCD/touch require an explicit runtime pin-mux design before
  simultaneous use is attempted.
