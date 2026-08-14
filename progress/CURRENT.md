# Current Progress

Last updated: 2026-08-15
Updated by: Codex

## Current task

Complete genuine four-bit TF/SDIO operation on T5-Board from branch
`feat/bk7258-t5-board-tf-4bit`, based on merged upstream commit `7ede5ef`.

## Verified starting point

- The one-bit fixed-media profile completed two real-board FAT cycles and is
  the retained regression baseline.
- The existing four-bit NuttX profile, pin mux, ACMD6/widebus path and S1 route
  assertions are present but deliberately fail closed.
- The current AP SDK bundle was built without
  `CONFIG_SDCARD_BUSWIDTH_4LINE`; its private data-setup helpers overwrite the
  runtime width back to one bit.
- Four-bit runtime conflicts with the COM3 UART0 data route on P10/P11, not
  with P0/P1 SWD/RTT.  S1-1/S1-2 must be OFF while SDIO is running.

Canonical starting evidence:

- [T5-Board TF/SDIO verification](verification/2026-08-14-bk7258-t5-board-tf.md)
- [Pre-flash trust-chain verification](verification/2026-08-14-bk7258-preflash-trust-chain.md)

## Active work

Completed in the current worktree:

1. Built the AP-only `v3.1.1.9-sdio4` bundle from an unmodified official SDK
   source copy; its independent 707-entry manifest and provenance pass.
2. Bound the four-bit profile to that exact AP bundle while CP and all one-bit
   profiles retain `v3.1.1.9`; config, compile and link-map gates are active.
3. The first signed `18.6.63`, security-counter `117` image was downloaded
   apps-only, but S1-1/S1-2 remained ON during its run.  Its diagnostic proved
   the controller switched to four bits while FAT/GPT probing failed.
4. Non-halting register evidence isolated the failure: P2-P5 were SDIO mode 1,
   while P10/P11 remained UART0 mode 0 even though pull/drive configuration
   was correct.  The SDK's default GPIO table owns P10/P11 as UART0 and its
   grouped SDIO mapper silently drops per-pin busy errors.
5. The T5-Board wrapper now follows the official/Tuya ownership sequence:
   release the default-mapped profile pins before group selection.  P10/P11
   can therefore become SDIO D2/D3; one-bit profiles retain their UART route.
6. The corrected image has been rebuilt with the same version, counter and
   external trust roots.  One-/four-bit profile gates, both SDK manifests,
   Factory/RPTUN layout and role-specific link-map checks pass.  New apps-only
   segment hashes are CP `3326fb9919b14b0fda74558acee7df3a50377e64e1ef03c290defcb326c718da`
   and AP `c30fdcb9dad397b26d44c88d0ec4186ea06426143f90e5dbf09debe07210855e`;
   the public trust contract remains unchanged.
7. The corrected apps-only image was downloaded through COM3 after the
   non-halting trust preflight.  The loader reported both explicit success
   markers and the board stopped at the existing BL2 hold.
8. With S1-1/S1-2 both OFF, the image completed genuine four-bit validation:
   P10/P11 were SDIO mode 1, requested/active width was four, the transition
   count was one with zero failures, GPT exposed seven partitions, and two FAT
   cycles validated 8192 bytes with checksum `0x17c60dc5`.
9. Post-board release hardening made SDK provenance mandatory and bound it to
   the selected role, manifest, final archive and four-bit overlays; SDK import
   now uses the shared build lock plus a three-part rollback transaction, and
   interrupted AP one-/four-bit build trees can be recovered.  Three positive
   bundle checks, 12 isolated negative provenance cases and a complete signed
   dual-image rebuild passed.  These host-only changes were not downloaded and
   did not alter the runtime source that produced item 8.

Next action: the owner opens a web PR from
`Embracecactus:feat/bk7258-t5-board-tf-4bit` to
`open-vela:dev-ai-contest-2026`; implementation and verification are complete.

## Blockers

- None for the four-bit TF stage.  Leave S1-1/S1-2 OFF while this image is
  running; reconnect them only for a later COM3 download while BL2 is held.

## Fixed constraints

- Do not modify official NuttX/apps or Beken SDK source trees.
- Preserve the validated one-bit SDK/profile behavior and unrelated untracked
  artifacts.
- Never open COM4.
- Do not rotate trust roots or write boot-chain, OTP/eFuse, lifecycle or data
  regions without explicit owner authority.
- Keep P0/P1 SWD available for the mandatory trust preflight, BL2 hold release
  and non-halting result read; closing SWD does not free any TF pin.
- GPT-5.6-Luna delegation remains disabled.
