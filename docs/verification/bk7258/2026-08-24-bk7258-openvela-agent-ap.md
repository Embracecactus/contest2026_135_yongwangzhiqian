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
- The official Agent UI remains unchanged.  The disappearing active PTT was
  caused by insufficient NuttX heap for LVGL's transformed layer, not by the
  UI implementation.
- A generic BK7258 LVGL framebuffer adapter preserves the Agent's 466x466
  logical surface and scales it to a centered 320x320 physical surface through
  the chip DMA2D path.  It does not modify the official UI source.
- The generic BK7258 chip option `BK7258_LCD_FRAMEBUFFER_MEDIA` allocates the
  RGB scanout buffer from the official SDK media YUV slab.  Board profiles
  that do not select it retain the original AP-heap allocation policy.
- The T5Board Agent profile selects that option, enlarges its PSRAM system-heap
  region from 320 KiB to 512 KiB, selects `LV_USE_CLIB_MALLOC`, and enables
  generic NuttX `FB_SYNC` double buffering.  The existing chip EOF ISR applies
  the requested page at the frame boundary.

## Build and package verification

- Clean CP/AP/BL2/BL1 build: PASS.
- Generated AP configuration contains `BK7258_PSRAM_MEDIA=y`,
  `BK7258_PSRAM_SYSTEM_HEAP_SIZE=0x80000`,
  `BK7258_LCD_FRAMEBUFFER_MEDIA=y`, `FB_SYNC=y`, and
  `LV_USE_CLIB_MALLOC=y`.
- Current signed package: `1.86.33+125`; AP, CP and BL1 security counters are
  125.  Package SHA-256:
  `0b937f69000fe6b3eab4159e39ed31d165c576eea1811fc0f6935972c21bae09`.
- Package structure and public BL1/BL2/CP/AP signature verification: PASS.
- The one operator-facing BKFIL image is
  `openvela-agent-sdio-domain-v1.86.33+125-full.bin`, size `0x7fa000`, SHA-256
  `c3548a18fa52cde3348cb18d9b8924490467923d3d2eeff0b23a0f29830bc657`.
  It was independently materialized from two byte-identical board bases with
  the same result.
- Re-materialization through the sole existing public entry,
  `bk7258.py package materialize`, first passed public package trust and then
  reproduced that image byte-for-byte.  A forged full-update catalog signature
  and a symlink base were rejected without creating output; no new Python file
  or public entry was retained.
- Agent data image SHA-256:
  `c559e1277f73788dfcbbf17d5a3c5f041ccb54ce9ec7666b59fde4df89050084`.

## Physical acceptance

- COM3 single-file full Flash: PASS.  BKFIL received one `--infile`, reported
  only index `[0]` with length `0x7fa000`, then one `EraseFlash ->pass`, one
  `WriteFlash ->pass`, `Writing Flash OK`, and
  `{All Finished Successfully}`.  The write did not use chip erase and ended
  before OTP/eFuse, lifecycle, calibration and the immutable tail.
- Two 115200-baud post-write reads of every retained interval were
  byte-identical and matched the exact bytes embedded in the full image,
  including `usr_config` and all layout holes.
- Non-halting target state: AP READY, error zero, Agent and LVGL UI running.
- The owner confirmed that the official UI is fully visible at 320x320, the
  leftmost text and fonts render correctly, and the expected 80-pixel black
  bands remain above and below the centered surface.
- The two 320x480 RGB565 pages occupy `0x96000` bytes inside the official
  media YUV slab rather than the AP general-purpose heap.  During the PTT
  animation, scanout samples changed between `0x60669ff0` and `0x606b4ff0`,
  directly confirming EOF page flips.
- The NuttX heap descriptor reports `0xaa9e8` total bytes, `0x7e150` peak use,
  and `0x43210` current use after stop.
- In a controlled physical round, the first click produced
  `is_recording=1/is_processing=0`; the owner confirmed the original red
  circle appeared immediately and completely, with neither disappearance nor
  progressive single-buffer redraw.  The second click produced
  `is_recording=0/is_processing=0`, and the complete blue `请说` button was
  restored.
- Official `packages/ai_agent/src/ui/lvgl_ui_channel.c` has zero diff.
- The earlier owner acceptance completed three consecutive recorder
  start/stop rounds.
- Recorder starts: 3; worker exits: 3; closes: 3.
- Captured bytes: 216320, 184960 and 229120.
- No `close incomplete`, `AUDIOIOC_ENQUEUEBUFFER -13`, or
  `voice_channel_start` failure occurred.
- ASR still returns no recognized text because credentials are not configured;
  microphone delivery, repeated PTT teardown and the chip-layer UI-memory fix
  are independently accepted.

## TF/SDIO physical acceptance

- Git commit `3b4a971` records the accepted one-bit TF result and `a8cc60b`
  records the accepted four-bit result.  Both are minimal clock-only baselines.
- The pinned ADK source places every `BAKP_SDIO` vote under
  `CONFIG_SDIO_PM_CB_SUPPORT`; the active v3.1.1.9 AP profile sets that option
  to `n`.  The normal AP path therefore forwards only the SDIO clock request
  to CP.
- Full Agent probe v123 held BAKP for the cross-core SDIO clock lifetime and
  passed both historical FAT cycles:

  ```text
  BKTF cycle=1/2 PASS width=1 bytes=4096 checksum=bb921dc5
  BKTF cycle=2/2 PASS width=1 bytes=4096 checksum=17c60dc5
  BKTF PASS width=1 cycles=2 bytes=8192 media=fixed
  ```

- The production A/B was decisive.  Clean v124 removed the BAKP ownership and
  immediately reproduced CMD0/CMD8 status `0x60000847` with mount `-110`.
  Clean v125 restored only the paired ownership and produced:

  ```text
  wifi inited(1) ret(0)
  T5-Board TF: selected FAT partition /dev/mmcsd0p1
  T5-Board TF: mounted at /mnt/tf
  BK7258 LCD: ready ...
  AI Agent ready
  ```

  The only remaining CMD1 timeout has status 0 and is the expected non-SD card
  probe.  `0x60000847` is absent.  v125 uses the normal production profile; the
  temporary two-cycle validator is not enabled.
- Accepted fix: `chips/bk7258/cp/bk7258_pm_server.c` votes numeric ADK
  `BAKP_SDIO` ID 91 on before the first cross-core SDIO clock enable and
  releases it after the last clock disable, using the existing PM-server
  first/last reference lifecycle.

## Remaining checks

- Review and publish the contest-repository change only when requested.
- Configure ASR/LLM and verify one real conversation.
- Complete the remaining performance/soak phases.
- Publish the separate NuttX GT9XX generic touchscreen ABI fix upstream.

## Publication boundary

- No NuttX, SDK, media framework or official Agent source is modified by this
  change.
- Hardware logs, J-Link temporaries, build products, keys and credentials are
  excluded.
- The immutable EasyFlash/RF/network tail and calibration data are unchanged.
