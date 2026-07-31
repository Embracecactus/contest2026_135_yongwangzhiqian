#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  sed -n '/^# Usage:/,/^$/p' "$0" | sed 's/^# \{0,1\}//'
}

# Usage:
#   debug_session_wsl.sh --list-ports
#   debug_session_wsl.sh --console-port COM11 [options]
#
# Common options:
#   --baud N                    Console baud rate (default: 115200)
#   --duration SEC              Capture duration (default: 20)
#   --output-dir PATH           WSL output path (default: ./hardware-debug-*)
#   --allow-output-overwrite    Allow replacing known files in a nonempty dir
#   --action TYPE               capture, manual-reset, serial-pulse, jlink-reset
#   --expected-regex REGEX      Required target output; repeatable
#   --fail-regex REGEX          Forbidden target output; repeatable
#   --allow-empty-capture       Accept a zero-byte capture
#   --command TEXT              Send a console command; repeatable, capture only
#   --line-ending TYPE          CRLF, LF, CR, or None
#
# Serial pulse options:
#   --reset-port COMx           Omit to pulse DTR/RTS on the console port
#   --pulse-mode MODE           DTR, RTS, or BOTH (default: RTS)
#   --pulse-active-level LEVEL  Asserted or Deasserted
#   --pulse-ms N                Active pulse duration
#
# J-Link reset options:
#   --jlink-device NAME         SEGGER device name
#   --jlink-exe PATH            Windows or WSL path to JLink.exe
#   --jlink-interface TYPE      SWD or JTAG
#   --jlink-speed KHZ           Interface speed
#   --jlink-reset-type N        SEGGER reset type (-1 uses default)
#   --resume-after-reset        Issue Go after reset
#
# Safety:
#   --allow-target-control      Required for DTR/RTS and J-Link reset actions
#   -h, --help                  Show this help

find_powershell() {
  if [[ -n "${HARDWARE_DEBUG_POWERSHELL:-}" ]]; then
    printf '%s\n' "$HARDWARE_DEBUG_POWERSHELL"
    return
  fi
  if command -v powershell.exe >/dev/null 2>&1; then
    command -v powershell.exe
    return
  fi
  if command -v pwsh.exe >/dev/null 2>&1; then
    command -v pwsh.exe
    return
  fi
  printf '%s\n' \
    'ERROR: Windows PowerShell interop was not found. Enable WSL interop or set HARDWARE_DEBUG_POWERSHELL.' >&2
  return 1
}

to_windows_path() {
  local input_path=$1
  if [[ "$input_path" =~ ^[A-Za-z]:\\ ]] || [[ "$input_path" == \\\\* ]]; then
    printf '%s\n' "$input_path"
  else
    wslpath -w "$input_path"
  fi
}

encode_values() {
  local joined=
  local value
  local encoded
  for value in "$@"; do
    encoded="$(printf '%s' "$value" | base64 -w 0)"
    if [[ -n "$joined" ]]; then
      joined+=,
    fi
    joined+="$encoded"
  done
  printf '%s\n' "$joined"
}

powershell_exe="$(find_powershell)"
win_session_script="$(to_windows_path "$script_dir/debug_session.ps1")"
win_capture_script="$(to_windows_path "$script_dir/serial_capture.ps1")"

if [[ ${1:-} == --list-ports ]]; then
  exec "$powershell_exe" -NoProfile -ExecutionPolicy Bypass \
    -File "$win_capture_script" -ListPorts
fi

console_port=
baud=115200
duration=20
output_dir=
action=CaptureOnly
reset_port=
pulse_mode=RTS
pulse_active=Asserted
pulse_ms=150
line_ending=CRLF
jlink_device=
jlink_exe=
jlink_interface=SWD
jlink_speed=1000
jlink_reset_type=-1
allow_target_control=0
allow_empty=0
allow_output_overwrite=0
resume_after_reset=0
declare -a expected_regex=()
declare -a fail_regex=()
declare -a serial_commands=()

while (($#)); do
  case "$1" in
    --console-port) console_port=${2:?missing value for --console-port}; shift 2 ;;
    --baud) baud=${2:?missing value for --baud}; shift 2 ;;
    --duration) duration=${2:?missing value for --duration}; shift 2 ;;
    --output-dir) output_dir=${2:?missing value for --output-dir}; shift 2 ;;
    --action)
      case ${2:?missing value for --action} in
        capture) action=CaptureOnly ;;
        manual-reset) action=ManualReset ;;
        serial-pulse) action=SerialPulse ;;
        jlink-reset) action=JLinkReset ;;
        *) printf 'ERROR: Unknown action: %s\n' "$2" >&2; exit 2 ;;
      esac
      shift 2
      ;;
    --reset-port) reset_port=${2:?missing value for --reset-port}; shift 2 ;;
    --pulse-mode) pulse_mode=${2:?missing value for --pulse-mode}; shift 2 ;;
    --pulse-active-level) pulse_active=${2:?missing value for --pulse-active-level}; shift 2 ;;
    --pulse-ms) pulse_ms=${2:?missing value for --pulse-ms}; shift 2 ;;
    --jlink-device) jlink_device=${2:?missing value for --jlink-device}; shift 2 ;;
    --jlink-exe) jlink_exe=${2:?missing value for --jlink-exe}; shift 2 ;;
    --jlink-interface) jlink_interface=${2:?missing value for --jlink-interface}; shift 2 ;;
    --jlink-speed) jlink_speed=${2:?missing value for --jlink-speed}; shift 2 ;;
    --jlink-reset-type) jlink_reset_type=${2:?missing value for --jlink-reset-type}; shift 2 ;;
    --expected-regex) expected_regex+=("${2:?missing value for --expected-regex}"); shift 2 ;;
    --fail-regex) fail_regex+=("${2:?missing value for --fail-regex}"); shift 2 ;;
    --command) serial_commands+=("${2:?missing value for --command}"); shift 2 ;;
    --line-ending) line_ending=${2:?missing value for --line-ending}; shift 2 ;;
    --allow-target-control) allow_target_control=1; shift ;;
    --allow-empty-capture) allow_empty=1; shift ;;
    --allow-output-overwrite) allow_output_overwrite=1; shift ;;
    --resume-after-reset) resume_after_reset=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) printf 'ERROR: Unknown argument: %s\n' "$1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "$console_port" ]]; then
  printf 'ERROR: --console-port is required.\n' >&2
  usage >&2
  exit 2
fi

if [[ -z "$output_dir" ]]; then
  output_dir="$PWD/hardware-debug-$(date +%Y%m%d-%H%M%S)"
fi
mkdir -p -- "$output_dir"
win_output_dir="$(to_windows_path "$output_dir")"

ps_args=(
  -ConsolePort "$console_port"
  -Baud "$baud"
  -DurationSec "$duration"
  -Action "$action"
  -OutputDirectory "$win_output_dir"
  -LineEnding "$line_ending"
  -PulseMode "$pulse_mode"
  -PulseActiveLevel "$pulse_active"
  -PulseMs "$pulse_ms"
  -JLinkInterface "$jlink_interface"
  -JLinkSpeed "$jlink_speed"
  -JLinkResetType "$jlink_reset_type"
)

[[ -n "$reset_port" ]] && ps_args+=(-ResetPort "$reset_port")
[[ -n "$jlink_device" ]] && ps_args+=(-JLinkDevice "$jlink_device")
if [[ -n "$jlink_exe" ]]; then
  ps_args+=(-JLinkExe "$(to_windows_path "$jlink_exe")")
fi
((allow_target_control)) && ps_args+=(-AllowTargetControl)
((allow_empty)) && ps_args+=(-AllowEmptyCapture)
((allow_output_overwrite)) && ps_args+=(-AllowOutputOverwrite)
((resume_after_reset)) && ps_args+=(-ResumeAfterReset)
if ((${#expected_regex[@]})); then
  ps_args+=(-ExpectedRegexBase64Csv "$(encode_values "${expected_regex[@]}")")
fi
if ((${#fail_regex[@]})); then
  ps_args+=(-FailRegexBase64Csv "$(encode_values "${fail_regex[@]}")")
fi
if ((${#serial_commands[@]})); then
  ps_args+=(-CommandBase64Csv "$(encode_values "${serial_commands[@]}")")
fi

exec "$powershell_exe" -NoProfile -ExecutionPolicy Bypass \
  -File "$win_session_script" "${ps_args[@]}"
