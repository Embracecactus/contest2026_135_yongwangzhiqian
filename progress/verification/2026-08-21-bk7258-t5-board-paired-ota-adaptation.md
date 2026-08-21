# Verification: T5Board standard MCUboot paired OTA

- Date and time zone: 2026-08-21 Asia/Shanghai
- Verifier: Codex
- Commit or artifact: dirty tree on `89384390ea87757665633003a4affd20fdfc5009`
- Environment: OpenVela workspace, ARM GCC 13.4.0, T5Board on COM3

## Result

**BUILD_PASS / PACKAGE_TRUST_PASS / ROOT_ROTATION_PASS /
PENDING_CONFIRM_PASS / DIRECT_XIP_REVERT_PASS.**

The owner explicitly authorized a recoverable full development-root rotation.
No chip erase, OTP/eFuse, lifecycle or debug-lock operation occurred. The
board is left with confirmed v1.0.1+4 in active B and confirmed v1.0.0+3 in
fallback A.

## Signed artifacts

- New BL1 public fingerprint:
  `2128d8c49a23d62a0fc7648fee8e9ba01669ad474af299418b5e7706b9ea89de`.
- New MCUboot public fingerprint:
  `f0364029b1afbe72816a7381a08124e6c9b2053e9833c86cf7a30445f3574f6a`.
- Confirmed full-chain v1.0.0+3 package:
  `afe63f486496b8942d839399cab29f951284ac28f1b6f6057d289e0c1ee1bb90`.
- Pending v1.0.1+4 package:
  `fb7e7fa77c725836aa6712d5485fdd5263e1f566e8ba9923de329632be2ce605`.
- Pending revert-probe v1.0.2+5 package:
  `54b1ab4d1d3fea0896e5580733a58088771a35e40372061392da86dc3075e0e5`.
- The full package passed eight-image structural verification and public
  BL1/BL2/CP/AP signature verification. Both apps-only packages passed
  `signed-ota` structure, `pending-v1`, `target=inactive` and public MCUboot
  CP/AP signature verification.

Private keys are stored outside the repository with a 0700 parent directory
and 0600 private files. Their paths and contents are intentionally absent from
project memory and Git.

## Build evidence

- Clean `bk7258.py build` passed for T5 CP + Vela-Claw AP, MCUboot mode,
  `bk7258_ab_removable_block.csv` and rollback floor 3.
- CP resolved `CONFIG_BK7258_OTA=y` and `CONFIG_BK7258_APP_OTA=y`; AP resolved
  `CONFIG_FB_SYNC=y`.
- BL1, BL2, CP and AP all linked. Final BL2 copy size is 13,536 bytes.
- The CP ELF contains `bk7258_ota_stage_pair`, `bk7258_ota_confirm_pair`,
  `bk7258_ota_inactive_geometry` and `bkota`.

## Hardware sequence

1. COM3 at 115200 8N1 returned NSH. The loader entered ROM mode, reported the
   compatible BK7236 downloader family and wrote the authorized eight sparse
   full-chain segments with BL1 last. Every segment reported erase/write pass,
   Flash protection restore and `Writing Flash OK`.
2. The new chain booted v1.0.0+3 from A. `bkota status` reported
   `active=A inactive=B`.
3. v1.0.1+4 pending AP then CP were written only to B. The next boot reported
   `active=B`; this exercised BL2 selection, standard `copy_done`, CRC RMW,
   status restore and remap.
4. `bkota confirm` returned `active CP/AP pair confirmed`. A reset retained B.
5. v1.0.2+5 pending AP then CP were written only to A. Its first boot reported
   `active=A`. No confirmation was issued.
6. The next reset produced `B2TRYB`, `B2BRET`, `B2SELB`, `B2APOK` and
   `B2HANDOFF`, then `bkota status` reported `active=B`. This is the accepted
   direct-XIP revert result.
7. Confirmed v1.0.0+3 AP/CP were restored to inactive A. Final status remains
   `active=B inactive=A`, leaving two recoverable signed pairs.

## Preservation and readback

- Pre/post `usr_config` `[0x4fc000,0x50a000)` is byte-identical, SHA-256
  `d078e2a26c51299488a166281edd3c6daebc1611316ef04580d24d6583e79acf`.
- Pre/post calibration tail `[0x7fa000,0x800000)` is byte-identical, SHA-256
  `ff7081ce06421a9b3ee592c308370ca4935d0d67fea7aa42f83e6abf78922b5e`.
- Two independent 115200 readbacks of BL1, Manifest A/B and BL2 A/B are
  byte-identical. Their package prefixes match exactly.
- Root readback SHA-256 values: BL1 `18120c47...b4f979`, Manifest A
  `385688ce...4f1df0`, Manifest B `70cd6c3c...967c98`, BL2 A/B
  `3a5f7124...144b33`.
- The pre-existing two byte-identical 8 MiB recovery images remain available;
  SHA-256 is `f3009741f940f137ffd97fcadf11eb40d41c675f919a3f6216de4956f86a6b25`.

## Residual scope

- The transport-neutral `bk7258_ota_stage_pair()` compiled into the real CP
  image, but its file-backed `bkota stage` path was not run because CP exposes
  no block/file source (`/dev` contains only console/GPIO/watchdog devices).
  Inactive-slot delivery for this hardware sequence used the ROM loader.
- Runtime `bkota confirm`, boot-stage trailer mutation, pair selection and
  revert were physically exercised.
- `bkota` is a trusted operator interface. It does not parse `.bkpack` or
  implement an AP/RPMsg health policy; package ingestion and automatic health
  acceptance remain outside this checkpoint.
- Per owner instruction, no test target ran and no test file changed. Another
  model owns later host/unit tests.

## Evidence locations

- Packages, extracted members and protected-region captures:
  `../out/bk7258/t5board-ota-20260821/`.
- Root readbacks: local BKFIL directory, prefixes `ota-rb1-*` and `ota-rb2-*`.
- Source/configuration evidence: active repository working tree.
