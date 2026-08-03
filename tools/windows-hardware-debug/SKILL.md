---
name: windows-hardware-debug
description: Capture and verify embedded-device UART logs, perform guarded SEGGER J-Link diagnostics, publish bounded Windows BLE test advertisements, and run bounded no-GUI Windows BLE Central/GATT probes from Windows or WSL2. Use for serial boot-log collection, reset-synchronized capture, DTR/RTS reset diagnosis, J-Link register or memory inspection, real-RF BLE scan validation, GATT read/write/notify validation, and reproducible hardware-debug evidence. Do not use it for flashing, erasing, fuse/security changes, unreviewed arbitrary J-Link commands, pairing-state changes, or treating host-side BLE status as complete target proof.
---

# Windows Hardware Debug

Use the scripts in `scripts/` to collect evidence without hard-coding a board,
COM port, baud rate, CPU, address, or reset polarity.

## Workflow

1. Identify whether the shell is Windows PowerShell or WSL2. In WSL2, run
   `scripts/debug_session_wsl.sh` for UART/J-Link sessions or
   `ble-advertiser/scripts/advertise_wsl.sh` for BLE publication, or
   `ble-gatt-client/scripts/gatt_client_wsl.sh` for BLE Central/GATT; in
   Windows, run the corresponding PowerShell scripts.
2. List Windows serial ports with `serial_capture.ps1 -ListPorts`. Ask for the
   port mapping if it cannot be inferred safely. Never open one COM port from
   two processes at once.
3. Begin with capture-only or read-only inspection. Preserve exact UART bytes
   in `serial.raw`; do not rely only on terminal-rendered text.
4. Before a reset, DTR/RTS pulse, halt, or resume, confirm that the user placed
   that target-control action in scope. Pass `-AllowTargetControl` only then.
5. Open UART capture before triggering reset. Use `debug_session.ps1` for this
   ordering instead of hand-starting concurrent commands.
6. Verify effects from target-side evidence such as a boot signature, restart
   counter, expected state transition, or requested regex. `PULSE_OK` and
   `JLINK_OK` prove only that the host-side tool completed.
7. Report the exact command, `session.json` path when applicable, BLE host
   evidence when applicable, target evidence, and unresolved uncertainty.

For a BLE scan test, first run the advertiser with `--probe`; require
`low_energy=1 peripheral=1`. Use a non-secret, uniquely identifiable payload
and bounded duration, start the advertiser before target capture/scan, and
require the target to report the expected company ID and payload. Windows can
interleave its own advertisement, so search all reports instead of assuming
index 0. Treat the advertiser ready file and `Started` state as host evidence
only; retain the target's raw address, RSSI, and advertising bytes as RF proof.

## Safety boundaries

- Treat J-Link attachment and memory/register reads as conservative diagnosis,
  not perfectly non-invasive observation; debugger attachment can affect timing.
- Require explicit user authorization for reset, halt, resume, and control-line
  pulses. Keep the script's authorization switch intact.
- Do not flash, erase, load an image, write memory, change option bytes/fuses,
  or alter security state with this skill.
- Do not use `jlink_debug.ps1 -Action CommandFile` unless the user explicitly
  requests the reviewed file and accepts its effects. Never run an untrusted
  command file.
- Do not put credentials or private keys in serial commands. Logs preserve raw
  output and may contain secrets.
- BLE advertisements are public radio transmissions. Use only test identifiers
  and non-secret payloads, keep the duration bounded, and stop the publisher on
  success or failure. Do not claim a fixed device identity from a Windows BLE
  address because privacy randomization may change it.
- Stop and explain the ambiguity when the target device, memory address, reset
  polarity, or COM-port ownership is unknown.

## Resources

- Read `AI_AGENT_SOP.md` for the evidence and authorization checklist before
  controlling a target or diagnosing a failure.
- Read `SOP.zh-CN.md` for installation, commands, output layout, and Windows/WSL2
  troubleshooting.
- Read `ble-advertiser/README.md` before generating a real BLE RF source; use
  its scripts rather than reimplementing Windows Runtime publication ad hoc.
- Read `ble-gatt-client/README.md` before connecting to a target; use an exact
  address/name, bounded deadlines, and a fresh result path. Do not pair a peer
  or accept a cached service index/read as uncached board-level GATT discovery
  proof. The client's explicit cached N13-negative recovery may only supply
  already-frozen handles; require its uncached reads, real ATT rejections,
  valid echo, JSON cache marker, and matching board counters. The separate
  UUID-targeted mode is uncached, but it is evidence only after the complete
  requested gate succeeds; a Controller link or timed-out query is not.
- Use `scripts/jlink_debug.ps1 -DryRun` to review generated J-Link commands
  without connecting to hardware.
