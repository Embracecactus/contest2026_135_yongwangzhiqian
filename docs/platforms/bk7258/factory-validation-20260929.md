# Shaniu factory engineering validation — 2026-09-29

This report records one factory-init full-flash run and the unattended checks
that followed it. It does not treat a loader success, a boot marker, a host
test, or software-generated input as physical product acceptance.

## Bound identities

| Item | Value |
| --- | --- |
| Firmware source | `637733877ef4c9150473415b11cb709519f5caaf` |
| Agent dependency | `7f5fde721a0698e1e91ce0305f2097abc314e0d4` |
| CP / AP profiles | `app_bktest` / `openvela_ap_bktest` |
| Runtime version | `0.7.47+692`, security counter `692` |
| Layout | `bk7258-510173147382a879` (`510173147382a879c3c3a1012a142bb5d87d64ffa8cb5881c27f1e289d8c1657`) |
| Build manifest | `874413ec0af1bced1604c68ecbbb2ec283bb6bd3811a93050d82fbd0a2df8db1` |
| Factory image | 8 MiB, `c0080c36cb0ce1c258e8f390e3d0c5170fe55aba0918ffae555bb5a359299bfd` |
| Recovery full image | `1a5c47366f468f72632c53196d3272c9d8ef9cce048e917ea9fb76e1df18b6e8` |
| Signed package | `080fe3e9285815f140aee927261cb7180fcb56a4197248bfacd6abec413577e5` |

The release and evidence are under
`../out/shaniu-factory-bktest-20260929/release-692-63773387/`. The manifest
records the main product input as clean and hash-binds the `apps` and `nuttx`
dependency worktrees, which were recorded as dirty. The board image therefore
belongs to the exact manifest inputs above, not to a later checkout state.

The factory image used the formal same-device factory materialization. It
initialized `usr_config`, `persistent_data`, and `factory_state`, preserved the
device-specific areas declared by the release policy, and did not write
OTP/eFuse or change the trust roots. No standalone APK or standalone resource
archive was needed for this phone-independent BKTEST run; the signed firmware
package and runtime default-resource observations are the delivered evidence.

## Factory flash and startup

| Check | Result | Evidence and limit |
| --- | --- | --- |
| Package, manifest, and trust verification | **PASS** | `release-692-63773387/verify-*.log` |
| Full 8 MiB write at offset zero | **PASS** | `../out/shaniu-factory-bktest-20260929/hil-692-full-factory/result.json`; all required erase/write/finish markers present, no automatic retry |
| BL1/BL2 and CP/AP handoff | **PASS** | `boot-692-controlled/serial.txt`: `B2HANDOFF`, `SYSINIT PASS`, `FINALINIT PASS`, `RCS PASS` |
| Runtime identity after flash | **PASS** | `runtime-status-692/session.json`: `0.7.47+692`, counter 692, AP A confirmed, CP/RPMsg ready, supervisor faults/recoveries zero |
| USB CDC, Wi-Fi, BLE, voice, display, NFC, motion, haptic | **PASS (initialization)** | Controlled boot recorded native USB ready, Wi-Fi init result 0, BLE task, Media/voice, both LCDs, display render, NFC/motion/haptic services |
| Camera | **PASS (registration/startup only)** | Camera tool registered and deferred camera stage completed; no image-quality claim |
| Default display resource | **PASS (digital render)** | `shaniu-cyan-v3`, revision 3, two-screen render pass; no human screen inspection |
| Original local KWS identity | **PASS** | built-in `nihao_openvela`, frontend 1, SHA256 `922eba9175fcda60f7c8a4505ca4eb5a97c86ceb30fbe48c685fd612098ac910`, wake ready |
| First blank-device QR/claim journey | **BLOCKED** | The first capture contained only the boot tail; no phone was available for QR, claim, BLE, or provisioning acceptance |

The full controlled startup contained no `HardFault`, assertion, or panic
marker. BT HCI status `0x0c` was observed during initialization and remains a
separate non-blocking diagnostic item.

## Factory diagnostics and BKTEST

The engineering image accepted a volatile factory diagnostics principal over
the existing console, then required the native USB TLS leaf pin and SDC1
authentication. The final host-tool check is recorded in
`../out/shaniu-factory-bktest-20260929/final-host-hil/`:

- enrollment **PASS**, flags 7, ten-minute RAM grant, firmware and source
  identity matched;
- authenticated status **PASS**;
- revoke **PASS**, and the old profile was rejected;
- the protected host profile was removed; final public status was flags 5
  (`eligible | used`, inactive), TTL 0 and result 0.

Production builds keep this interface disabled. The test path injects virtual
key events through the production button receiver and power coordinator; it
does not call shutdown directly.

| BKTEST case | Result | Board observation |
| --- | --- | --- |
| K2 2999 ms, no held event | **PASS** | No power intent and zero PM requests |
| K2 3000 ms, blocked peer | **PASS** | One request; terminal `FAILED`, `-ECANCELED`, resolved |
| K2 3001 ms, declined peer | **PASS** | One request; terminal `FAILED`, `-ETIMEDOUT`, resolved |
| CP unknown | **PASS** | Bounded `WAIT_CP_OR_UNKNOWN`, then terminal `FAILED`, unresolved retained |
| CP pending timeout | **PASS** | 25 bounded pending observations, then terminal `FAILED`; no infinite pending |
| CP late ACK | **PASS** | Deadline won; the late reply did not reopen the terminal state |
| Repeated release, stale session, clock rollback, K1/K3 separation | **PASS (host production path)** | Selected contracts passed; these exact sequences were not repeated on the physical board |
| Physical K2 GPIO/debounce and wake | **BLOCKED** | No person or key actuator was available |
| Real CP deep sleep and current | **BLOCKED** | Engineering PM peer validates the coordinator boundary; no independent power/current fixture was available |

The K2 evidence is under the `k2-*-hil*` directories beside this report's
factory output. Native USB may disappear before acknowledging a long K2 UP;
the host never replays that command and uses the independent console power
observer to determine the terminal state.

## Audio lifecycle

`audio-close-hil/audio.json` is **PASS** for the production Agent/Media path:
24,576 fixed PCM bytes were accepted, EOF drained and closed once, cancel
returned `-ECANCELED` for write/drain without reopening the old session, and a
new session reached EOF and closed successfully. `status-after.json` confirms
that a fresh authenticated session remained usable. This is digital/Media
evidence only; speaker quality, clicks, underruns audible to a person, and
microphone capture are **BLOCKED** without acoustic observation.

## Host and CI gate

The final local selected gate collected all 844 required execution IDs:

- 843 **PASS**;
- 1 **FAIL_ASSERTION**: the pre-existing
  `RES-01.eye-install-disconnect-cleanup` resource-install regression;
- 0 **SETUP_ERROR** and 0 **NOT_RUN**;
- both negative mutations were detected and both restored controls passed.

Evidence: `out/shaniu-contract-20260929-1630-final/results.json`. An earlier
run caught a host regression in which a device-specific 1.1-second reopen wait
was applied to every TLS transport (830 PASS, 12 FAIL_ASSERTION, 1
SETUP_ERROR). The corrected client scopes close-notify/reopen waiting to the
real native serial transport, and the power observer now passes its remaining
deadline to the console subprocess. The same affected tests and the complete
gate were rerun after the correction.

The known resource-install Red is not converted to PASS or attributed to the
factory flash. It keeps the repository-wide CI gate non-green until that
separate production defect is closed.

## Remaining acceptance

| Status | Item |
| --- | --- |
| **BLOCKED** | Physical K2 press/release/restart, deep-sleep power/current, human screen and sound checks, microphone-to-speaker voice path |
| **BLOCKED** | Phone QR claim, BLE/App lifecycle, data-preserving APK upgrade, App OTA |
| **NOT RUN** | Runtime BKTEST factory-reset confirm/erase/reboot transaction; this run validated formal image-time factory-init instead |
| **NOT RUN** | NFC card interaction, physical motion/haptic behavior, destructive power-loss injection |

At handoff the board is running `0.7.47+692`; the factory diagnostics grant is
inactive, its old credential is rejected, and the temporary host profile has
been removed.
