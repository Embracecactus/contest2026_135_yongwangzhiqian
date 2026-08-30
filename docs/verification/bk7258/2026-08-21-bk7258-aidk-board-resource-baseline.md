# BK7258 AIDK AI Toy board-resource baseline

Date: 2026-08-21

## Conclusion

`CODE_PASS / HOST_ASSETS_PRESERVED / HARDWARE_NOT_RUN`

The AIDK AI Toy now has maintained CP/AP configuration seeds for the existing
BK7258 build path.  The resolved CP uses the CH340-backed UART0 console; the
resolved AP owns the soldered SD NAND, FAT support and mono speaker playback.
The final direct CP/AP build passed.  No image was written to the board.

## Hardware and source evidence

- Windows enumerated COM8 as a healthy CH340 USB serial device.
- The owner-provided V1.0 schematic shows BK7258 P10/P11 (`DL_0RX/DL_0TX`)
  routed as `RX0/TX0` through CH340E to the USB-to-UART Type-C connector.
- The device mass-storage volume was a 125,833,216-byte FAT volume.
- A workspace-level, non-Git backup at
  `../aitoy-official-device-backup-2026-08-21/` contains 16 WAV files, 10 AVI
  files, the schematic and `SHA256SUMS`.  Source-to-backup SHA-256 comparison
  covered all 27 files with zero mismatches; `sha256sum -c` also passed.
- Schematic SHA-256:
  `b10cf08785a1c8767d9b4c51d05e6c8685a4cb60a720b51022734d742149bb2d`.
- All WAV files are 16-bit/16-kHz PCM.  Fourteen are mono; the two ASR standby
  and wakeup files are stereo despite `mono` in their names.  All AVI files
  are 320x160 MJPEG without audio; `genie_eye.avi` is 25 fps and the others
  are 20 fps.

## Configuration result

- Added maintained `aidk_ai_toy_cp_base` and `aidk_ai_toy_ap_base` profile
  pairs (`aidk_ai_toy_base_v1`).
- Removed the generated full-config layer
  `aidk_ai_toy_personal/{cp,ap}.config`; no compatibility wrapper replaced it.
- CP resolves AIDK, UART0 115200 8N1 without flow control, RPTUN and the
  CP-owned clock service. RTT and SWD are disabled.
- AP resolves no console/serial, fixed-block storage, AIDK SD NAND,
  SDIO/MMCSD, FAT and `pcm0p` 16-kHz mono playback. Card-detect and
  write-protect capabilities are disabled for the soldered medium; MIC remains
  disabled because the current BK7258 AUD/MIC wrappers are mutually exclusive.
- The BK7258 RPTUN Kconfig now selects the NuttX `BOARDCTL` type dependency
  required by both CP and AP builds.
- `tools/bk7258/` still has exactly one tracked top-level public entry:
  `bk7258.py`; no SDK version, layout address or public command was added.

## Build evidence

Clean direct build: PASS

```text
layout: bk7258-381e2cdd1286ac59
CP seed:   eab5650384f0beebda9be9a15215ffbe7b3e189540ccad1bcd4e2e14b13fcab2
CP config: 2abf2a46c824b1c2485af848aa5c27b308247dcf697451521e18e6f70fb87b5a
AP seed:   93f95a1c35851df635114d72570cb6b90f749629b3ea063b0c72af555b4a6f51
AP config: 560618c7d15b2c221e5c88576115866a9b2936f53d9df3d0c999166eb3a289c2
boot.bin:  82bd9fdbc7f4a9ba76a1dec242b4268dcfe106aeb70f32280ee6d084449ea3c0
cp.bin:    335df765c9d3b7838d5473cb9a0c87ee1f6c4048bcfb6cdeaf6338a4c6f0c743
ap.bin:    b35d2db54a0626024e9a19c7c6b93cdb7ddf92d7c4825fd16705185883e70cc8
pair.bin:  39fb2c680d4b92f6e8c1f44f0d4ad9d266ffec815ceb3f608813eb91eb64e0bb
```

The AP ELF contains the AIDK audio and SDIO bindings,
`bk7258_aud_initialize`, `bk7258_sdio_initialize` and
`mmcsd_slotinitialize`.  The consolidated resolved-config, ELF-symbol,
duplicate-truth, public-entry, backup-manifest and `git diff --check` gate
passed.

## Open gate

- This build has not booted on AIDK hardware and is not a signed delivery
  build.  No Flash, reset, chip erase, format or device-volume write occurred.
- FAT is compiled, but no AP resource service currently mounts `/dev/mmcsd0`
  or validates the official files at runtime.
- AIDK initial-provisioning trust and exact target offsets must be accepted
  before any Flash operation. USB-device MSC is not enabled by this phase.
