---
name: windows-hardware-debug
description: Explicit-invocation-only Windows UART, guarded J-Link, and BLE evidence tools. Use this skill only when the user names windows-hardware-debug or explicitly asks to use this debug skill for an identified target. Do not auto-select it from generic debug, serial, COM, embedded, or BK7258 keywords. BK7258 work starts from its board-specific workflow. This skill does not flash, erase, write memory, or change security state.
---

# Windows Hardware Debug

## Invocation and target boundary

- Load this skill only when the user explicitly names it. A request to debug,
  flash, collect logs, or validate firmware does not by itself invoke this
  generic skill. `agents/openai.yaml` disables implicit invocation.
- BK7258 tasks start from `bk7258-hil-download` and the applicable board
  instructions. That workflow may use these scripts as a reviewed transport
  implementation after freezing the board, COM role, action, and artifact;
  it does not authorize generic RTS/DTR or J-Link actions on the board.
- A COM number in an example or an old session is not target identification.
  Enumerate current ports and establish the requested device-to-port mapping
  before opening a port. Never probe unrelated ports to find a familiar log.
- `CaptureOnly` means no built-in reset action, not necessarily read-only:
  `--command` sends bytes to the target. Review every console command under
  the current task scope; do not use it to bypass reset/write boundaries.

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

When another action depends on a live UART state (for example, starting
physical audio after capture becomes ready), read
[correlated capture](references/correlated-capture.md). Use the existing ready
signal and live echo, then inspect final raw bytes; an empty buffered raw file
during capture does not by itself mean that the target is silent.

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
- For BK7258 downloads, use the sibling `bk7258-hil-download` skill for the
  profile-aware BK Loader stage, then return here for post-flash evidence.
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

- Before target control, read the applicable authorization and evidence
  checklist in `AI_AGENT_SOP.md`. For failure diagnosis, read only the
  checklist relevant to the failing action.
- Use the relevant section of `SOP.zh-CN.md` for installation, UART, J-Link or
  Windows/WSL2 troubleshooting. A known capture command does not require
  rereading the entire installation and debug guide.
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
