---
name: android-companion-hil
description: Validate an Android companion APK against its real connected device using existing ADB, UI and device logs, preserving application data and identity. Use for installation, connection lifecycle, settings readback, and App-driven OTA or cancellation regressions. Not for repairing session architecture, simulated BLE claims, voice-model training or firmware flashing outside the App.
---

# Android companion physical verification

## Inputs and prerequisites

Obtain APK path/hash, package ID, expected version, authorized phone serial,
paired device identity and the existing project procedure. Use installed ADB
and current UI/log tools; assign one operator to the phone and device control
connection. Derive selectors from the current UI, never copy coordinates or
serials from a previous project. If target identity is ambiguous, stop device
actions while continuing artifact inspection.

Installation, state changes, OTA and restarts require current task scope. Do
not clear App data, unpair, reclaim a device or rotate credentials merely to
make a regression easier. Preserve the observed starting state rather than a
historical document's claimed state.

## Execute

1. Establish actual identities separately. Use `adb devices -l` and then
   `adb -s "$phone_serial" shell dumpsys package "$package_id"` for the
   installed package. Inspect the candidate with existing SDK APK tools and
   `sha256sum "$apk_path"`. Read firmware version through the App/device
   procedure; an APK build or remote firmware note does not identify the board.
2. Before deployment, compare installed/candidate package and signing identity
   with existing SDK tools and check version compatibility. Preserve data via
   a supported update; if Android rejects a signer mismatch or downgrade,
   do not uninstall or clear data as a shortcut. Stop installation and obtain
   a compatible artifact. Record baseline settings before changing them.
   If deployment is in scope, use the existing installer or
   `adb -s "$phone_serial" install -r "$apk_path"`. Verify the installed
   package again. A version string alone cannot prove which same-version APK
   ran; record install result and candidate hash, comparing installed bytes
   where existing tooling supports it.
3. Observe the current UI and filtered logcat without clearing useful logs.
   Record connection generation, authentication, request type/result and
   relevant lifecycle transitions. Do not retain keys, passwords or payloads
   in screenshots/log extracts. UI output and transport evidence are separate.
4. Connect once. Visit all bottom tabs for the required repeated round trips,
   comparing scan/connect/auth counts, process identity and generation. Stay
   in the foreground beyond the affected timeout; then enter an external
   Activity or file picker and return. Do not force-stop the App during this
   continuity gate; that would invalidate the claim.
5. Exercise truthful state: transient STATUS failure versus actual disconnect,
   stale snapshot versus fresh value, unknown capability versus busy device,
   and write pending versus background polling. Use existing supported ways
   to reach errors, not a new probe or an unreviewed destructive command.
6. For a setting such as volume, record its initial verified value and allowed
   states. Set two moderate supported values, wait for ACK and actual readback,
   then observe the corresponding physical effect when available. A changed
   slider is not board confirmation. Restore the baseline if the operation
   was an acceptance exercise and report if it could not be re-read.
7. For OTA, exercise the **App's real supply/install/cancel path** with the
   authorized signed package and compatible device floor. An external full
   flash is a different gate. Record supply result, device apply/boot/version
   and any cancellation phase actually reached. Reuse the project's release
   and trust checks; do not introduce another serving or signing mechanism.
8. After an actual disconnect, verify consistent state and a fresh authenticated
   status read on reconnect. Confirm old writes/claim/delete/OTA commands were
   not replayed. Return temporary phone settings to their recorded baseline
   and release all tool owners.

## Failure branches

- **UI changed but device did not:** inspect ACK/readback and capability before
  calling it success. Keep the real device value.
- **ADB shell action gets a permission exception:** this is an Android OS gate,
  not evidence of an agent approval rejection. Use the normal authorized UI
  if supported; do not root or bypass OS privilege checks.
- **Text fields disagree with requested input:** Chinese/other IME composition,
  selection and commit can alter `input text`. After two failures inspect the
  current UI/field value. Do not keep tapping stale coordinates. Commit input
  with the control's actual UI action; Back may discard composition.
- **OTA supply succeeded but device did not update:** report the failed device
  phase; do not substitute a later direct flash as proof of App OTA.

## Output and done

Use the project's existing evidence directory. Record APK/firmware identities,
operations and counts/durations, relevant logs/UI, physical versus simulated
results, restoration status and exact remaining failures. This skill is
complete for the requested gate only when the real phone and device behavior
is observed. It does not certify an entire product or repair underlying code.
