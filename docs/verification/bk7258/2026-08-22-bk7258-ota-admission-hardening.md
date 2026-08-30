# Verification: T5Board OTA admission hardening

- Date and time zone: 2026-08-22 Asia/Shanghai
- Verifier: Codex
- Branch: `feat/bk7258-ota-admission-hardening` (uncommitted)
- Hardware: T5Board, COM3 at 115200, one-line TF profile
- Toolchain: content-locked Arm GCC 10.3.1

## Current result

**CLEAN_BUILD_PASS / PACKAGE_TRUST_PASS / DEVELOPMENT_ROOT_ROTATION_PASS /
PAD72_BOOT_PASS / SUPERVISOR_HEALTHY_PASS / AON_WHOLE_RESET_PASS /
WIFI_RANGE_STAGE_PASS / PENDING_EXCLUSION_PASS / CONFIRM_RETENTION_PASS /
ROLLBACK_ADMISSION_PASS.**

No test target ran.  Verification used the official `bk7258.py` build/package
surface, public package verification, exact sparse BKFIL writes, 115200
read-back and real UART runtime evidence.

## Implementation outcome

- Staging refuses to erase an inactive slot while the active pair is pending.
- Candidate CP/AP MCUboot header, version, protected counter, CRC groups and
  pending trailers are checked before erase; version and counter must both
  exceed the active pair.
- Confirm binds slot/version/counter, revalidates under a finite Flash guard
  and treats any partial trailer RMW as a whole-device reset condition.
- Runtime and BL2 share one MCUboot format/trailer geometry definition.
- T5Board enables the CP/AP Supervisor.  Post-READY CPU2 heartbeat has one
  pinned-task writer, shared snapshots are generation/state coherent, and
  stale probe results clear HEALTHY immediately.
- MCUboot signing now explicitly uses the pinned imgtool `--pad-sig` profile.
  New package evidence records `ecdsa-der-pad72-v1`; the verifier requires a
  72-byte EC256 TLV containing valid DER followed only by zero compatibility
  padding.  Legacy packages without this evidence remain readable.
- The first AON reset adapter incorrectly called the SDK NMI-dump reboot path
  and produced an NMI record.  It was replaced by the SDK AON watchdog plus
  PMU whole-device reset routing; the rejected adapter is absent from source.

## Build and package evidence

- Final clean CP/AP/BL2/BL1 build after hardware acceptance: PASS; layout
  `bk7258-5641c11040abf787`; current BL2 copy size 13696 bytes.
- New public BL1 fingerprint:
  `36f725b2c5b23f163011d89fa175cf83cd0f5d4c41c3d11312be405dd2f4ad74`.
- New public MCUboot fingerprint:
  `eb25d4e5fa0a720232a51058e3b2f6000354e6dc5c05f3210b37a8d27885b1c9`.
- Final confirmed recovery package `1.3.0+6`, counter 6:
  `8705914f8555a0b69b099e38d06ea8e9499dfd4f66aa5a79fbdc3636b700d309`.
- Final pending OTA package `1.4.0+7`, counter 7:
  `519106e9d55e0ce82e774cf82cb1f545301999100d626ff9f70d68fba85bd7e5`.
- Both final packages pass structure and public trust verification.  Private
  signing material remains outside the repository and is not identified here.

## Root-rotation and runtime evidence

- The owner explicitly authorized replacement of the disposable development
  root.  Writes were recoverably ordered: new app A; BL2/Manifest B; BL1
  switch; app B; BL2/Manifest A.  No chip erase occurred.
- BL1, Manifest B and BL2 B were read at 115200 and matched the package
  byte-for-byte.  A pre-boot CP A read also matched byte-for-byte.
- An unpadded first signature was accepted by host imgtool/OpenSSL but rejected
  by target MCUboot; re-signing unchanged executable bytes booted.  Official
  MCUboot documents fixed 72-byte padding for cross-version ECDSA
  compatibility, which is now an explicit generation and verification gate.
- A true removal of every board power source retained active A, confirmed
  `1.3.0+6`, counter 6.  AP returned READY, CPU2 scheduler-online, RPTUN
  connected and Manager idle.
- Supervisor reported HEALTHY, primary/secondary/transport age `10/10/0` ms,
  healthy age 19880 ms, fault count 0 and recovery count 0.
- `bkota reboot` through the corrected AON watchdog re-entered BL1/BL2,
  selected A and returned to NSH without NMI, HardFault or `B2BAD`.
- Hardware evidence is retained outside Git under
  `/home/lijian/project/open-vela/logs/bk7258-ota-admission-20260822`.
  One credential-fragment capture caused by a mistimed hidden-input attempt
  was immediately deleted and is not retained.

## Wi-Fi OTA and admission evidence

- The first HTTP command was longer than the configured 80-byte NSH line and
  lost its `catalog.json` suffix.  This explained asynchronous Manager
  `-EINVAL` with no server request; a 22-character Base64URL encoding retained
  the full 128-bit temporary token while keeping the operator command within
  the shell limit.  No firmware change was needed.
- Wi-Fi associated with runtime link state 3.  The temporary server was bound
  only to the Windows Ethernet address, allowlisted only the router NAT source,
  and required the random path token.  Initial staging produced two HTTP 200
  catalog/signature responses plus HTTP 206 image reads.
- Manager advanced CHECKING -> STAGING_AP -> STAGING_CP ->
  READY_TO_REBOOT and ended at phase 6, progress 1/1, error 0.  Active A and
  Supervisor HEALTHY remained stable throughout.
- AON reset re-entered BL1/BL2 with `B1PRIMARY`, `B2GOOK`, `B2SELB`; B ran as
  pending `1.4.0+7`, counter 7 with healthy AP/CPU2/RPTUN state.
- After Wi-Fi reconnect, pending restaging fetched only catalog/signature,
  returned `-EBUSY` at phase/progress 0 and produced no image Range requests.
- Pair confirm succeeded.  A second AON reset selected B again; final runtime
  retained active B, confirmed `1.4.0+7`, counter 7 and Supervisor HEALTHY.
- Reapplying the same confirmed package performed only bounded metadata Range
  reads, then returned `-EPERM` at phase/progress 0 before any erase/write.
- The temporary server was stopped and its token and helper deleted.  One
  mistimed hidden-input capture was immediately deleted; no credential-bearing
  capture is retained.
- No OTP/eFuse/lifecycle, calibration, persistent-data or debug-lock write
  occurred.  The TF card and preserved/immutable Flash ranges were untouched.
