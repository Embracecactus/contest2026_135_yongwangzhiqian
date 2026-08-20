# Current Progress

Last updated: 2026-08-20
Updated by: Codex

## Objective

Replace the historical BK7258 framework/wrapper tree with one public
`tools/bk7258/bk7258.py build|sdk|package|verify` and six internal domains.

## Repository state

- Repository: `contest2026_135_yongwangzhiqian`
- Branch: `refactor/bk7258-sdk-manifest`
- Implementation commit: `2ee0fe37290ce9497f4c54be723466a41ef62bd7`
- Remote: `fork/refactor/bk7258-sdk-manifest`; implementation commit pushed.
- Owner-untracked logs, `bootloader.tmp`, doc-stress helpers and
  `build_package.sh` remain untouched.

## Implemented architecture

- The only tracked public tool is `bk7258.py`; `_lib` contains exactly
  `build.py`, `sdk.py`, `layout.py`, `image.py`, `package.py`, `trust.py`.
- The team manifest pins SDK `cb080de...` and OpenVela ARM prebuilt
  `948af44a...`. OpenVela, SDK rebuild and project BL1/BL2 share it with no
  compiler PATH fallback.
- SDK profiles are `cp`, `ap`, `ap-sdio4`; each owns one accepted bundle-tree
  hash and NuttX closure omissions. Registry/set/lock/provenance are gone.
- Three board-name-independent CSVs select `onchip-persistent`,
  `removable-block` or `fixed-block`. They retain the verified BL1,
  Manifest/BL2 A/B and CP/AP A/B initial geometry; sizes remain CSV-editable.
- Project BL1 and the board-verified freestanding project BL2 build only under
  `out/`. The official Beken bootloader is reference-only. The unused NuttX
  BL2 config/glue and tracked development public keys are deleted.
- `--boot direct|mcuboot` is explicit. MCUboot uses build-local defconfig
  overlays and public-key C sources, not tracked boot-mode config copies.
- `image.py` alone owns CRC/pair/final Flash bytes. `package.py` stores those
  bytes unchanged and records every preserved external artifact.
- Signed release uses explicit public/private PEMs, separate BL1/MCUboot
  counters and pinned imgtool. Public verification covers Manifest A/B,
  compiled BL1/BL2 roots and CP/AP signatures. No private path is packaged.
- Hardware/debug/Flash transport remains outside `_lib` in the Windows SOP.

## Evidence obtained

- [Boot/storage source checkpoint](verification/2026-08-20-bk7258-single-cli-boot-storage-source.md)
- SDK CP/AP/AP-SDIO4 rebuild and tree verification passed before this slice;
  hashes remain recorded in the earlier real-build checkpoint.
- Before switching from the host compiler to the newly pinned OpenVela
  prebuilt, a real official CP/AP build plus the new out-of-tree project BL1
  passed. The resulting project-BL1 unsigned package was deterministic and
  independently verified (`d39ad4c...`). This is exploratory path evidence,
  not final prebuilt-toolchain acceptance.
- Three new layout CSVs parse successfully.
- Host-only temporary P-256 public-source generation and 256-byte BL1
  Manifest signing passed; the temporary private keys were deleted.
- Python source compilation and `git diff --check` pass as of the latest
  checkpoint.

## Not yet verified

- The pinned ARM prebuilt checkout is not fully synchronized. A slow fetch was
  stopped at the owner's direction; no final prebuilt-based firmware build ran.
- The new BL2 Makefile, signed CP/AP release, public package trust verifier and
  all three storage topologies have not run end to end.
- No new artifact has been flashed. No erase, OTP/eFuse, lifecycle, debug-lock
  or other irreversible hardware operation occurred.

## Exact next action

Open a PR from `Embracecactus:refactor/bk7258-sdk-manifest` to
`open-vela:dev-ai-contest-2026` and review the source-only boundary. Toolchain
synchronization, signed integration build and recoverable hardware validation
remain explicit later steps rather than inferred PASS results.

Do not commit or push without fresh explicit authority.
