# Current Progress

Last updated: 2026-08-20
Updated by: Codex

## Objective

Validate the merged six-domain BK7258 architecture on a real T5-Board running
Vela-Claw, including the manifest-pinned toolchain, project BL1/BL2, MCUboot
signing, package verification, Flash delivery and CP/AP runtime handoff.

## Repository state

- Repository: `contest2026_135_yongwangzhiqian`
- Branch: `fix/bk7258-postmerge-validation`
- Base `HEAD`: `ce81821f622ce20dee3af1127ac1df860f4ab021`
- Validation commit: `5bbcaf2e59c1d80fd1c486ec745dd545c21758d5`
- Remote: `fork/fix/bk7258-postmerge-validation`; validation commit pushed.
- Owner-untracked logs, `bootloader.tmp`, doc-stress helpers and
  `build_package.sh` remain untouched.

## Implemented fixes

- MCUboot role configs are always materialized under the build output and
  include the selected board `Make.defs`; no source config is rewritten.
- BL1 and BL2 disable GNU build-id notes, linker-shared integer constants are
  valid in both C and LD preprocessing, and BL1 receives the explicit rollback
  floor and Manifest constants.
- The BL2 trust key section is retained, MCUboot assertions fail closed, and
  MCUboot images carry the KEYHASH TLV expected by the compiled BL2 policy.
- The AP MCUboot vector is the slot start plus its `0x200` image header. A new
  host gate binds the raw BL2 vector words to its ELF before signing.
- Vela-Claw now references its maintained source path correctly.

## Verified result

- [T5-Board signed hardware checkpoint](verification/2026-08-20-bk7258-t5-board-signed-hardware.md)
- Manifest-pinned ARM GCC checkout `948af44a...` is clean; GCC 13.4.0 was used.
- A clean T5-Board CP/AP + project BL1/BL2 build passed with layout
  `bk7258-5641c11040abf787`.
- Hardware package SHA-256 `cbbbbad5...` passed package/trust verification and
  all eight declared segments were downloaded through COM3 without chip erase.
- Hardware reached NSH, the AP boot state was `READY` with an increasing
  heartbeat, and the owner observed the Vela-Claw UI.
- The initially observed backlight flicker stopped. Ten GPIO9 samples remained
  output-high (`0x00000003`), so no LCD/backlight source change was made.
- A later host-only package, SHA-256 `18010f98...`, exercised the new BL2 vector
  gate and again passed package and public trust verification; it was not
  flashed.
- `git diff --check` passes.

## Remaining boundaries and risks

- Validation used temporary development P-256 keys. No production key or
  hardware root was provisioned; no OTP/eFuse, lifecycle or debug-lock state
  changed. The rollback floor is software evidence, not hardware monotonic
  anti-rollback.
- Flashing consumed the package contract manually. There is still no reviewed
  `.bkpack`-to-BKFIL executor that mechanically restricts writes to declared
  segments.
- COM3 is both download and UART0 at 115200; COM4 produced no output. The
  Windows capture wrapper preserved raw bytes but failed to emit `serial.json`
  over its UNC path.
- J-Link reset/connect did not succeed, and removable-block/TF-card I/O was not
  exercised in this checkpoint.
- The host BL2 vector gate binds MSP/reset bytes to the ELF, but its MSP check
  can later be tightened to the exact BL2 SRAM window. Board BL1 already
  enforces the runtime range.

## Exact next action

Open a PR from `Embracecactus:fix/bk7258-postmerge-validation` to
`open-vela:dev-ai-contest-2026` using the prepared Chinese description.
Hardware-root provisioning and a package-driven Flash executor are separate
follow-up phases.

Do not infer authority for OTP/eFuse, lifecycle, debug-lock or chip-erase
operations.
