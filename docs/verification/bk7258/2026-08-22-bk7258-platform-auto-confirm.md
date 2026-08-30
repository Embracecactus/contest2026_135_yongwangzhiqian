# Verification: T5Board platform auto-confirm

- Date and time zone: 2026-08-22 Asia/Shanghai
- Branch: `feat/bk7258-ota-admission-hardening` (uncommitted)
- Hardware: T5Board, COM3 at 115200
- Policy scope: CP/AP, CPU2 and RPMsg platform health only

## Current result

**CLEAN_BUILD_PASS / DEADLINE_REVERT_PASS / SCOPE_CORRECTION_PASS /
AUTO_CONFIRM_SUCCESS_PASS / RETENTION_PASS.**

- CP policy and deadline workers mutually monitor liveness.  A pending pair
  requires a fresh generation-bound Supervisor sample for 10 continuous
  seconds; the trial deadline is 60 seconds.
- Pair confirmation revalidates health and directly observed AP generation
  under the Flash guard before AP `image_ok` and again before CP `image_ok`.
  Partial trailer mutation remains a whole-device reset condition.
- Multiple diagnostic pending trials deliberately failed the then-enabled
  higher-level product vote.  Every trial remained unconfirmed and the
  deadline caused BL2 to return to confirmed B without `B2BAD`, NMI or
  HardFault.  This directly proves the fail-closed timeout/revert path.
- Requiring Vela Claw UI/CLI/core health was judged outside the current OTA
  infrastructure phase.  All changes under `app/vela_claw/`, all product-vote
  RPMsg changes and their package path were removed before the final build.
- An in-progress diagnostic staging was canceled.  Hardware remained active B,
  confirmed `1.4.0+7`, counter 7 until the final acceptance staging.
- Final platform-only GCC10 clean build passed with layout
  `bk7258-5641c11040abf787` and BL2 copy size 13696 bytes.  No test target ran.

## Final acceptance (2026-08-22, owner-authorized)

Package: platform-only signed CP/AP `1.10.0+13`, counter 13,
sha256 `195325fd939ea25c36a41ad1bdfbea17d1cd579ff48aa2c82693eb99bc56da33`,
`bk7258 verify package` and `verify trust` both PASS before download.

1. Pre-staging status: active=B confirmed `1.4.0+7` counter 7, Supervisor
   healthy flags `0000001f`, manager idle-with-canceled-residue error -125.
2. Staging over Wi-Fi Range to inactive A: request accepted, manager
   progressed to phase 4 writing 1392640 bytes, completed progress=1/1
   error=0 (`autoconfirm-1p10-stage-status/serial.raw`).
3. Trial entry via `bkota reboot`: `B1PRIMARY`, `B2GOOK`, `B2SELA`,
   `BOTA TRIAL ARM slot=0 counter=13`, NSH up, then
   `BOTA TRIAL CONFIRMED slot=0 counter=13` with no manual confirm command
   (`autoconfirm-1p10-trial-boot/serial.raw`).  Success path proven.
4. Retention: one further AON watchdog reset; BL2 selected A again
   (`B2SELA`) and `bkota status` reported active=A inactive=B pair=confirmed
   `1.10.0+13` counter 13, manager state=0, trial state=0 NOT_PENDING with
   both workers exited (`autoconfirm-1p10-final-status/serial.raw`).

No whole-chip erase, no OTP/eFuse/lifecycle/calibration/persistent-data or
debug-lock writes, no test target run.
