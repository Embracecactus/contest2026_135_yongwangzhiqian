# Current Progress

Last updated: 2026-08-28
Updated by: Codex

## Objective

Run the official openvela Agent on the T5Board AP with LCD, GT1151 touch,
microphone, speaker, TF storage and AP networking, while CP retains
OTA/platform NSH.

## Current state

- The active branch `refactor/bk7258-platform-ownership` is based on
  `ae83523e6d40c472a76fac4278784eaea6f34e5e`.  The BK7258 platform
  orchestrator has been split by ownership: CP/AP SoC sequencing, raw reset
  source, raw Flash, boot-slot, OTA mechanics and Wi-Fi control are in the chip
  layer; the NuttX late hook, storage topology/guards, OTA product policy,
  `BOARDIOC` mapping and physical electrical bindings remain in the board
  layer. Host/header gates, the maintained CP/AP build matrix, fresh-key
  generation-155 xTS and generation-156 production full downloads, the
  current minimal xTS core and final production cold/status hardware gates all
  pass.
- The P0 follow-up branch `feat/bk7258-p0-xts-completion` is based on
  `ecc1c0a185896d6afce165d20ebbf1a270782683`.  Its maintained host fixture
  passes the common gates and 281/281 cmocka cases.  The CP XTS profile now
  registers 64 KiB of role-local PSRAM as a second system-heap region while
  retaining the standard 16 KiB testsuites runner stack.
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
- The external NuttX and apps repositories have zero tracked modifications.
  The T5-Board uses a board-local LVGL capability adapter over the unchanged
  generic GT9XX character ABI; no upstream patch is part of this change.

## Agent product hardware acceptance (generation 125)

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

## P0 xTS acceptance (generation 149)

- A clean MCUboot CP/AP/BL1/BL2 build used fresh, distinct BL1 and MCUboot
  P-256 keys with version/counters `1.88.5+149` / 149 / 149.  Package
  structure, public trust and flash contract PASS; the temporary private-key
  directory was deleted after acceptance.
- The sole COM3 BK Loader input was one `0x7fa000` operator image at address
  zero.  Erase/write/protect and final success markers PASS; `usr_config` and
  the full Agent persistent range were preserved, and immutable tail/OTP/
  lifecycle/calibration were excluded.
- Cold boot reports `BPSR SYSTEM HEAP PASS size=65536`; Umem total is 183,192
  bytes, exactly 64 KiB above generation 148.  `cmocka_mm_test` 8/8,
  `cmocka_sched_test` 16/16, ostest status 0, getprime, mm, 4 KiB ramtest,
  tmpfs scanftest 164/0, hello/FIFO/pipe and the final cold boot all PASS.
- Destructive storage tests, fixture-bound GPIO/UART and AP peripheral cases,
  controlled fault and non-zero critmon thresholds remain separate gates.
  The owner deferred the 12-hour soak on 2026-08-27.

## Chip/board ownership refactor (generations 152-156 complete acceptance)

- The former board-owned monolithic `bk7258_platform.c` and parallel board OTA,
  boot-slot and Wi-Fi mechanism files are retired.  A typed one-shot stage
  runner retains the first mandatory failure, honors explicit prerequisites
  and permits only declared independent always-run leaves.
- The full BK7258 host suite passes with `BK7258_HOST_TEST_PASS`; all 86
  audited public/private boundary headers pass C11 and C++17 self-containment
  in default, CP and AP feature modes with warnings as errors.  Clean
  T5AI-Core, AIDK AI Toy, T5-Board production and T5-Board xTS CP/AP builds
  pass.  Chip sources have no `<arch/board>`/physical-board/`BOARD_HAS_*`
  dependency, and board sources include no chip-private directory or deep
  relative header path.
- The initial xTS package failed closed 6,316 bytes above the protected trailer
  threshold. The xTS-only profile now removes the redundant local OTA/Wi-Fi
  operator commands and GPIO sample while retaining chip OTA/RPMsg, Wi-Fi VNET,
  GPIO lower-half, `ALLSYMS` and backtraces. Generation 153 signs with about
  3.7 KiB margin and passes MM/scheduler cmocka, getprime, allocator/RAM,
  scanftest 164/0, pipe and complete `ostest`, followed by a clean cold boot.
- Full packages `18.6.98+152`, `+153` and `+154` used three new, distinct
  BL1/MCUboot key generations. Structure, public trust and Flash contract pass
  for each. Their SHA-256 values are `adb82349...a3f7`,
  `9668f1f9...1f22` and `ac5e2fff...bc8c` respectively.
- Each physical download used one address-zero `0x7fa000` BK Loader input and
  passed erase/write/reprotect/final-success checks without touching the
  immutable tail. Final generation 154 cold boot, AP/CPU2/RPTUN/supervisor,
  PSRAM, WDT and Wi-Fi control status pass; CP whole-device watchdog reboot
  restores the complete signed boot chain and the same confirmed generation.
- The final strict pass used two further independent fresh trust generations.
  Generation 155 package/operator SHA-256 values are
  `42c534e8...c9b49` / `9060e09c...10f31`; its cold boot, MM 8/8,
  scheduler 16/16, watchdog/RPTUN/RPMsg nodes and CP PSRAM pass.  Generation
  156 production package/operator values are `dfdb0cea...64e73` /
  `32e638e9...2fbec`; runtime confirms `18.6.98+156`, counter 156, healthy
  AP/CPU2/RPTUN/supervisor, Wi-Fi control, nodes and CP PSRAM.  The
  `usr_config`, `reset_marker` and complete persistent-data ranges are
  byte-identical from generation 154 through 156.  Both temporary private-key
  directories and Windows staging copies were deleted after acceptance.

## Remaining work

1. Review and publish `refactor/bk7258-platform-ownership`; the owner then
   creates and reviews the remote PR.
2. Configure approved ASR/LLM credentials and complete one real dialog.
3. Complete the remaining fixture-bound/isolated xTS phases: LIBCXX,
   GPIO/UART loopback, AP RTC/timer/RNG, driver tests, controlled fault and
   destructive-storage tests.  The core current-generation non-destructive
   xTS is complete; 12-hour soak is owner-deferred as of 2026-08-27.

## References

- [Agent AP verification](verification/2026-08-24-bk7258-openvela-agent-ap.md)
- [P0 xTS generation 149](verification/2026-08-27-bk7258-p0-xts-completion.md)
- [BK7258 chip/board orchestrator refactor](verification/2026-08-27-bk7258-chip-board-orchestrator-refactor.md)
- [BK7258 host regression fixture](verification/2026-08-27-bk7258-host-regression-fixture.md)
- [ADR-034](../memory/decisions/ADR-034-openvela-agent-ap-and-t5-interaction-topology.md)

## Safety constraints

- Do not erase the whole chip or modify OTP/eFuse/lifecycle/calibration.
- Do not enable UART1 while the hardware switch selects SWD.
- Do not print or record private signing keys or credentials.
- Keep the external NuttX/apps trees unmodified; adapt through project-owned
  chip/board code and report upstream warnings without patching them here.
- Camera and RGB LCD/touch require an explicit runtime pin-mux design before
  simultaneous use is attempted.
