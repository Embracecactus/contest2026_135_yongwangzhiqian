---
name: windows-hardware-debug
description: Capture and verify embedded-device UART logs and perform guarded SEGGER J-Link diagnostics from Windows or WSL2. Use for serial boot-log collection, reset-synchronized capture, DTR/RTS reset diagnosis, J-Link register or memory inspection, and reproducible hardware-debug evidence. Do not use it for flashing, erasing, fuse/security changes, or unreviewed arbitrary J-Link commands.
---

# Windows Hardware Debug

Use the scripts in `scripts/` to collect evidence without hard-coding a board,
COM port, baud rate, CPU, address, or reset polarity.

## Workflow

1. Identify whether the shell is Windows PowerShell or WSL2. In WSL2, run
   `scripts/debug_session_wsl.sh`; in Windows, run the PowerShell scripts.
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
7. Report the exact command, `session.json` path, evidence, and unresolved
   uncertainty.

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
- Stop and explain the ambiguity when the target device, memory address, reset
  polarity, or COM-port ownership is unknown.

## Resources

- Read `AI_AGENT_SOP.md` for the evidence and authorization checklist before
  controlling a target or diagnosing a failure.
- Read `SOP.zh-CN.md` for installation, commands, output layout, and Windows/WSL2
  troubleshooting.
- Use `scripts/jlink_debug.ps1 -DryRun` to review generated J-Link commands
  without connecting to hardware.
