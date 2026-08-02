# Windows / WSL2 Hardware Debug SOP

A board-independent toolkit for collecting exact UART logs, running guarded
SEGGER J-Link diagnostics, and publishing bounded BLE test advertisements from
Windows PowerShell or WSL2. It is designed for both humans and coding agents
such as Claude Code and Codex.

The toolkit deliberately does **not** implement flashing, erase, memory writes,
fuse/option-byte changes, or security-state changes. A successful host command
also does not prove that the target reset or changed state; target-side evidence
must be checked separately.

- 中文操作手册：[SOP.zh-CN.md](SOP.zh-CN.md)
- Claude/Codex 操作约束：[AI_AGENT_SOP.md](AI_AGENT_SOP.md)
- Agent Skill entry point: [SKILL.md](SKILL.md)
- Optional real-RF BLE beacon for Windows/WSL2:
  [BLE test advertiser](ble-advertiser/README.md)

## Requirements

- Windows 10/11 with Windows PowerShell 5.1 or PowerShell 7
- A Windows-visible USB serial adapter, only for UART actions
- WSL2 with Windows executable interop, only when running from Linux
- SEGGER J-Link Software, only for J-Link actions
- A Windows BLE adapter with peripheral-role support, only for RF advertising
- Visual Studio Build Tools with C++ and a Windows SDK, only when rebuilding
  the BLE advertiser

No third-party PowerShell module is required. SEGGER software and drivers are
not distributed by this project and remain subject to SEGGER's terms.

## Quick start on Windows

Open PowerShell in this directory:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\serial_capture.ps1 -ListPorts
```

Capture exact bytes without controlling the target:

```powershell
.\scripts\debug_session.ps1 `
  -ConsolePort COM11 -Baud 115200 -DurationSec 20 `
  -Action ManualReset -ExpectedRegex 'boot complete'
```

The script opens the console first, then asks you to reset the target. Replace
the example port, baud rate, and regex with board facts.

Pulse DTR/RTS on the same console adapter:

```powershell
.\scripts\debug_session.ps1 `
  -ConsolePort COM11 -Baud 115200 -DurationSec 20 `
  -Action SerialPulse -PulseMode DTR -PulseMs 100 `
  -AllowTargetControl -ExpectedRegex 'boot complete'
```

Use a separate control adapter by adding `-ResetPort COM7`.

Review a J-Link memory-read command without connecting to hardware:

```powershell
.\scripts\jlink_debug.ps1 `
  -Action ReadMemory -Device CORTEX-M33 `
  -Address 0x20000000 -Count 16 -Width 32 -DryRun
```

Remove `-DryRun` only after confirming the SEGGER device name and address.

## Quick start from WSL2

Windows owns the COM port; the WSL wrapper calls Windows PowerShell and converts
paths safely:

```bash
./scripts/debug_session_wsl.sh --list-ports

./scripts/debug_session_wsl.sh \
  --console-port COM11 \
  --baud 115200 \
  --duration 20 \
  --action manual-reset \
  --expected-regex 'boot complete' \
  --output-dir ./hardware-debug-logs/boot-001
```

For a J-Link reset-synchronized capture:

```bash
./scripts/debug_session_wsl.sh \
  --console-port COM11 \
  --baud 115200 \
  --action jlink-reset \
  --jlink-device CORTEX-M33 \
  --resume-after-reset \
  --allow-target-control \
  --expected-regex 'boot complete'
```

J-Link runs in command-file/no-GUI mode with exit-on-error enabled and an outer
timeout, following SEGGER's documented batch interface. See the
[J-Link Commander reference](https://kb.segger.com/J-Link_Commander).

## Output contract

Each `debug_session` directory contains:

| File | Meaning |
| --- | --- |
| `serial.raw` | Exact bytes received from the serial port |
| `serial.txt` | UTF-8 best-effort view for searching and regex checks |
| `serial.json` | Serial tool status, byte count, SHA-256, and timestamps |
| `serial-tool.log` | PowerShell capture process output |
| `action.log` | Manual, DTR/RTS, or J-Link action log |
| `action.json` | Structured action result when applicable |
| `session.json` | Combined verdict and evidence paths |

`session.json` uses `passed` only when every `ExpectedRegex` matched and no
`FailRegex` matched. Without expected evidence, a successful run is marked
`completed_unverified`.

The session refuses a nonempty output directory by default so prior evidence
cannot be mistaken for a new run. Use `-AllowOutputOverwrite` on Windows or
`--allow-output-overwrite` in WSL2 only when replacing the toolkit's known
output files is intentional.

## Use as an Agent Skill

The package root is also an Agent Skills-compatible directory. Keep the whole
directory together because `SKILL.md` refers to its scripts and SOP files.

For Codex, link or copy it into a repository's `.agents/skills/` or the user's
`$HOME/.agents/skills/`. Codex supports symlinked skill directories. See the
[official Codex skill documentation](https://developers.openai.com/plugins/build/skills).

For Claude Code, link or copy it into `.claude/skills/windows-hardware-debug/`
or `~/.claude/skills/windows-hardware-debug/`. See the
[official Claude Code skill documentation](https://code.claude.com/docs/en/skills).

Example repository-local links from a repository that contains this directory
at `tools/windows-hardware-debug`:

```bash
mkdir -p .agents/skills .claude/skills
ln -s ../../tools/windows-hardware-debug .agents/skills/windows-hardware-debug
ln -s ../../tools/windows-hardware-debug .claude/skills/windows-hardware-debug
```

On Windows, copy the folder when symlink creation is unavailable.

## Scope and license

All hardware-specific values remain caller parameters. Keep board profiles,
memory maps, reset polarity, and acceptance regexes in the board repository,
not in this generic toolkit.

The toolkit is licensed under Apache-2.0. Before publishing it independently,
confirm that the chosen copyright holder and the parent repository's licensing
policy agree with that license.
