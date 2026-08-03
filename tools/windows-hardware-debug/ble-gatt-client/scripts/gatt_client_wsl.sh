#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
power_shell_script=$(wslpath -w "$script_dir/gatt_client.ps1")

address=
name=
expected_device_name=
scan_timeout_ms=10000
operation_timeout_ms=15000
rediscover_timeout_ms=10000
connect_attempts=3
result_file=
probe=0
scan_only=0
no_rediscover=0
n13=0
n13_negative=0
n13_cached_discovery=0
n13_targeted_discovery=0
n13_burst_count=100
build=0

usage()
{
  cat <<'EOF'
Usage: gatt_client_wsl.sh [options]

Options:
  --probe                       Check Windows BLE central-role support
  --build                       Rebuild before running
  --address MAC                 Exact six-octet target address
  --name NAME                   Exact advertised local name
  --expect-device-name NAME     Exact GAP Device Name value
  --n13                         Run N13 service/read/write/notify gates
  --n13-negative                Require length/magic/version/CRC rejection
  --n13-cached-discovery        Cache handles only for the negative gate
  --n13-targeted-discovery      Query only GAP/N13 UUIDs, uncached
  --n13-burst-count N           Notify frames, 1..100 (default 100)
  --scan-timeout-ms MS          Initial scan deadline (default 10000)
  --operation-timeout-ms MS     Per-WinRT-operation deadline (default 15000)
  --rediscover-timeout-ms MS    Post-close scan deadline (default 10000)
  --connect-attempts N          Fresh uncached connection attempts (default 3)
  --scan-only                   Stop after matching advertisement
  --no-rediscover               Skip post-close advertising check
  --result-file PATH            Success JSON output path
  -h, --help                    Show this help
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
    --scan-only)
      scan_only=1
      shift
      ;;
    --no-rediscover)
      no_rediscover=1
      shift
      ;;
    --n13)
      n13=1
      shift
      ;;
    --n13-negative)
      n13_negative=1
      shift
      ;;
    --n13-cached-discovery)
      n13_cached_discovery=1
      shift
      ;;
    --n13-targeted-discovery)
      n13_targeted_discovery=1
      shift
      ;;
    --address|--name|--expect-device-name|--n13-burst-count|--scan-timeout-ms|--operation-timeout-ms|--rediscover-timeout-ms|--connect-attempts|--result-file)
      if (($# < 2)); then
        echo "ERROR: $1 requires a value" >&2
        exit 2
      fi

      case "$1" in
        --address) address=$2 ;;
        --name) name=$2 ;;
        --expect-device-name) expected_device_name=$2 ;;
        --n13-burst-count) n13_burst_count=$2 ;;
        --scan-timeout-ms) scan_timeout_ms=$2 ;;
        --operation-timeout-ms) operation_timeout_ms=$2 ;;
        --rediscover-timeout-ms) rediscover_timeout_ms=$2 ;;
        --connect-attempts) connect_attempts=$2 ;;
        --result-file) result_file=$2 ;;
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
  if [[ -z "$address" && -z "$name" ]]; then
    echo "ERROR: --address or --name is required unless --probe is used" >&2
    exit 2
  fi

  [[ -z "$address" ]] || arguments+=(-Address "$address")
  [[ -z "$name" ]] || arguments+=(-Name "$name")
  [[ -z "$expected_device_name" ]] ||
    arguments+=(-ExpectedDeviceName "$expected_device_name")
  ((n13 == 0)) || arguments+=(-N13 -N13BurstCount "$n13_burst_count")
  ((n13_negative == 0)) || arguments+=(-N13Negative)
  ((n13_cached_discovery == 0)) || arguments+=(-N13CachedDiscovery)
  ((n13_targeted_discovery == 0)) || arguments+=(-N13TargetedDiscovery)

  arguments+=(
    -ScanTimeoutMs "$scan_timeout_ms"
    -OperationTimeoutMs "$operation_timeout_ms"
    -RediscoverTimeoutMs "$rediscover_timeout_ms"
    -ConnectAttempts "$connect_attempts"
  )

  ((scan_only == 0)) || arguments+=(-ScanOnly)
  ((no_rediscover == 0)) || arguments+=(-NoRediscover)

  if [[ -n "$result_file" ]]; then
    result_absolute=$(realpath -m -- "$result_file")
    mkdir -p -- "$(dirname -- "$result_absolute")"
    arguments+=(-ResultFile "$(wslpath -w "$result_absolute")")
  fi
fi

powershell.exe "${arguments[@]}"
