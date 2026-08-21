# Current Progress

Last updated: 2026-08-21
Updated by: Codex

## Objective

Bring up the AIDK AI Toy resource path without altering the verified BK7258
boot chain or risking the official device assets.

## Repository state

- Repository: `contest2026_135_yongwangzhiqian`
- Branch: `fix/bk7258-vela-claw-poweron-flicker`
- AIDK baseline commit: `4cce51e` (`feat(bk7258): add AIDK fixed-block
  MCUboot profiles`); not pushed.
- Owner-untracked logs, `bootloader.tmp`, doc-stress helpers and
  `build_package.sh` remain untouched.

## Completed baseline

- COM8 is a healthy CH340 device. The schematic confirms CH340E connects
  UART0 on P10/P11; board facts and Kconfig text now match it.
- The device FAT volume's 16 WAV and 10 AVI assets plus the AIDK schematic are
  backed up outside Git at `../aitoy-official-device-backup-2026-08-21/` with
  a verified 27-file SHA-256 manifest.
- Maintained `aidk_ai_toy_{cp,ap}_base` seeds replace the generated
  `aidk_ai_toy_personal/*.config` layer.
- CP owns UART0 115200 8N1. AP owns fixed SD NAND, SDIO/MMCSD, FAT and
  16-kHz mono `pcm0p`; AP has no console and MIC is deferred.
- [AIDK board-resource baseline](verification/2026-08-21-bk7258-aidk-board-resource-baseline.md)
  records the resolved configs, resource formats, hashes and limitations.
- Final clean direct build passed with layout `bk7258-381e2cdd1286ac59`, CP
  config `2abf2a46...87b5a`, AP config `560618c7...89c2`, CP image
  `335df765...0c743` and AP image `b35d2db5...70cc8`.
- [Signed AIDK MCUboot package](verification/2026-08-21-bk7258-aidk-mcuboot-package.md)
  passed clean BL1/BL2/CP/AP build, eight-image package verification and
  public-signature verification. Package SHA-256 is `29aa1757...dc11e`.

## Paused prior phase

- T5-Board display stabilization remains a verified partial improvement in
  [its page-flip checkpoint](verification/2026-08-21-bk7258-t5-board-display-flip.md).
- Residual gray-level flicker is intentionally deferred while aitoy board
  adaptation is active.

## Open risks

- The signed AIDK images have not run on hardware.
- The owner confirms a recoverable factory firmware backup exists, but the
  current bounded inventory did not independently locate it. ADR-030 accepts
  the owner's instruction to proceed without factory identity/backup recheck.
- No AP service mounts `/dev/mmcsd0` or reads/plays the official assets yet.
- Two ASR WAV files are stereo despite `mono` in their names. The AVI files
  are 320x160 MJPEG without audio; `genie_eye.avi` uses 25 fps, others 20 fps.
- USB-device MSC, LCD, camera, NFC, gyro and motor are outside this baseline.

## Exact next action

Apply [ADR-030](../memory/decisions/ADR-030-aidk-first-provision-project-boot-chain.md):
when the owner is physically present, manually reset/CEN the board while
BKFIL waits on COM8, capture two byte-identical 8 MiB factory Flash reads at
115200, then provision the already verified package's eight declared segments
with boot last and capture the complete UART boot path.

## Current prohibitions

- Do not chip-erase, program OTP/eFuse or enable debug lock.
- ADR-030 explicitly authorizes replacing the AIDK factory BL1/BL2/MCUboot
  chain with the verified project chain; it does not authorize any other
  boot-chain mutation.
- Do not Flash unless the new full-chain package passes layout, image and
  public-signature verification.
- Do not format or write the device FAT volume; resource bring-up is read-only.
- Do not modify or remove the owner-untracked files listed above.
