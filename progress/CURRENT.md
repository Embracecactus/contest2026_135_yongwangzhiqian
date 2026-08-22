# Current Progress

Last updated: 2026-08-22
Updated by: ox-alpha

## Objective

T5Board OTA admission/health hardening and CP-owned platform-health automatic
confirmation are complete and hardware-accepted on real T5Board, including the
success path (pending -> automatic confirmed) and AON-reset retention.

## Repository state

- Repository: `contest2026_135_yongwangzhiqian`.
- Branch: `feat/bk7258-ota-admission-hardening`, created from current
  `origin/dev-ai-contest-2026@abf9de87f993` with zero initial divergence.
- The implementation is uncommitted. Existing untracked project logs remain
  untouched. No test target ran and no file under `tests/` changed.
- Temporary MCUboot validation instrumentation and the temporary CH32 Kconfig
  placeholder were removed; the official `apps/boot/mcuboot` tree is clean.
- Scope review confirmed zero diff under `app/vela_claw/**`, no product-health
  RPMsg changes and no CH32 temporary files in the working tree.

## Implemented

- Pre-erase CP/AP header, CRC, version, protected-counter and pending-trailer
  admission; candidate version and counter must both exceed active.
- Staging requires a confirmed active pair, preventing a pending trial from
  erasing its fallback.
- Generation-bound finite-lock confirmation with lock-internal revalidation.
- Shared runtime/BL2 MCUboot format and trailer geometry.
- T5Board CP/AP Supervisor enabled; dedicated post-READY CPU2 heartbeat,
  coherent shared snapshots and always-available `healthy_age_ms`.
- File backend uses `pread()`.
- MCUboot signer explicitly uses fixed 72-byte ECDSA padding and publishes
  `ecdsa-der-pad72-v1` evidence. New-package verification enforces valid DER
  plus zero-only padding while retaining legacy package readability.
- OTA system reset uses the AON watchdog and PMU whole-device reset routing.
  The rejected SDK NMI-dump reboot adapter is absent.
- Auto-confirm starts only for a pending active pair.  Two CP workers mutually
  monitor liveness, enforce a 10-second stable platform-health window and a
  60-second trial deadline, and reset whole-device on timeout/generation drift.
- Confirmation consumes a fresh Supervisor sample and rechecks AP generation
  before AP trailer RMW and again before CP trailer RMW under the Flash guard.
- Business-application voting is explicitly outside this phase.  All temporary
  Vela Claw/UI/core and product-health RPMsg changes were removed.

## Verified checkpoint

- Final clean GCC10 CP/AP/BL2/BL1 build: PASS after hardware acceptance.
- Layout: `bk7258-5641c11040abf787`; current BL2 copy size: 13696 bytes.
- Owner-authorized disposable development-root rotation completed without chip
  erase or writes to OTP/eFuse/lifecycle/calibration/persistent-data.
- Current hardware: active B, confirmed `1.4.0+7`, counter 7.  Inactive A is
  intentionally invalid/partial after the owner-visible cancellation of an
  over-expanded diagnostic staging; it is not claimed as a fallback.
- AP READY, CPU2 scheduler-online, RPTUN connected and Supervisor HEALTHY.
- Real authenticated Range staging, pending `-EBUSY`, manual confirm, AON
  retention and same-version `-EPERM` admission all passed on T5Board.
- Pending auto-confirm diagnostic trials that did not satisfy the then-enabled
  higher-level product vote all hit the 60-second deadline and BL2 safely
  returned to confirmed B.  No failed trial was confirmed.
- Over-expanded staging was canceled before reboot; Manager reported CANCELED
  and active confirmed B remained unchanged.  After scope correction, the
  platform-only clean build and expected auto-confirm symbols passed.
- Final acceptance (platform-only `1.10.0+13`, counter 13,
  sha256 `195325fd939ea25c36a41ad1bdfbea17d1cd579ff48aa2c82693eb99bc56da33`):
  staged over Wi-Fi Range to inactive A, `bkota reboot` entered the trial
  (`B2SELA`, `BOTA TRIAL ARM slot=0 counter=13`), the pair was automatically
  confirmed with no manual command (`BOTA TRIAL CONFIRMED slot=0 counter=13`),
  and one further AON reset retained active A confirmed `1.10.0+13` counter 13
  (`B2SELA` again, trial state NOT_PENDING, both workers exited).
- Detailed evidence:
  [OTA admission hardening](verification/2026-08-22-bk7258-ota-admission-hardening.md),
  [platform auto-confirm](verification/2026-08-22-bk7258-platform-auto-confirm.md).

## Final artifacts

- Confirmed recovery package `1.3.0+6`, counter 6:
  `8705914f8555a0b69b099e38d06ea8e9499dfd4f66aa5a79fbdc3636b700d309`.
- Confirmed running package `1.10.0+13`, counter 13:
  `195325fd939ea25c36a41ad1bdfbea17d1cd579ff48aa2c82693eb99bc56da33`.
- Both pass package structure and public BL1/BL2/CP/AP trust verification.
- Private signing material remains outside the repository; never ask the owner
  to rediscover it or record its path/content.

## Exact next action

Owner decision: commit and publish the reviewed branch, or continue platform
scope.  The working tree is the hardware-accepted state; do not rebase onto a
moving upstream without a fresh clean build.

## Remaining platform scope

- Optional business-application health vote adapter as a separate later phase.
- Real T5Board TF `.bkpack` source and mount state.
- T5AI-Core HTTPS, BLE-only, NAND resume, UART/USB, resource/model and delta.

## Current prohibitions

- Do not read/reuse historical OTA adaptation or run/modify tests.
- Do not touch unrelated CH32 work.
- Do not erase the whole chip or write OTP/eFuse/lifecycle,
  calibration/persistent-data or debug-lock state.
- Never print or record private-key paths/contents or credentials.
