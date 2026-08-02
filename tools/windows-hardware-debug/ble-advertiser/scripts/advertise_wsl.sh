#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
power_shell_script=$(wslpath -w "$script_dir/advertise.ps1")

duration=30
startup_timeout_ms=5000
company_id=0xFFFE
payload=BK7258-N12V
payload_hex=
ready_file=
probe=0
build=0

usage()
{
  cat <<'EOF'
Usage: advertise_wsl.sh [options]

Options:
  --probe                     Check Windows BLE peripheral-role support
  --build                     Rebuild before running
  --company-id VALUE          Test company ID (default 0xFFFE)
  --payload TEXT              UTF-8 payload (default BK7258-N12V)
  --payload-hex HEX           Raw payload; excludes --payload
  --duration SECONDS          Broadcast time; 0 means until Ctrl+C
  --startup-timeout-ms MS     Wait for Started (default 5000)
  --ready-file PATH           Evidence file, written only after Started
  -h, --help                  Show this help
EOF
}

while (($# > 0)); do
  case "$1" in
    --probe)
      probe=1
      shift
      ;;
    --build)
      build=1
      shift
      ;;
    --company-id|--payload|--payload-hex|--duration|--startup-timeout-ms|--ready-file)
      if (($# < 2)); then
        echo "ERROR: $1 requires a value" >&2
        exit 2
      fi

      case "$1" in
        --company-id) company_id=$2 ;;
        --payload) payload=$2; payload_hex= ;;
        --payload-hex) payload_hex=$2 ;;
        --duration) duration=$2 ;;
        --startup-timeout-ms) startup_timeout_ms=$2 ;;
        --ready-file) ready_file=$2 ;;
      esac
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "ERROR: unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if ! command -v powershell.exe >/dev/null 2>&1; then
  echo "ERROR: powershell.exe is unavailable; enable WSL Windows interop" >&2
  exit 3
fi

arguments=(
  -NoProfile
  -ExecutionPolicy Bypass
  -File "$power_shell_script"
)

if ((build)); then
  arguments+=(-Build)
fi

if ((probe)); then
  arguments+=(-Probe)
else
  arguments+=(
    -CompanyId "$company_id"
    -DurationSec "$duration"
    -StartupTimeoutMs "$startup_timeout_ms"
  )

  if [[ -n "$payload_hex" ]]; then
    arguments+=(-PayloadHex "$payload_hex")
  else
    arguments+=(-Payload "$payload")
  fi

  if [[ -n "$ready_file" ]]; then
    ready_absolute=$(realpath -m -- "$ready_file")
    mkdir -p -- "$(dirname -- "$ready_absolute")"
    arguments+=(-ReadyFile "$(wslpath -w "$ready_absolute")")
  fi
fi

powershell.exe "${arguments[@]}"
