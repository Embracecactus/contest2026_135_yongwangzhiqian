#!/usr/bin/env bash
# Load the N15-F validation artifacts into the fixed upper-PSRAM window.
# This tool never resets the target and never writes Flash.

set -Eeuo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
OPENVELA_ROOT=$(cd "${SCRIPT_DIR}/../../../.." && pwd)
PACKAGE_DEFAULT="${OPENVELA_ROOT}/nuttx/bk7258-dual-ota-validation/n15-ota-host-candidate"
JLINK_EXE_DEFAULT='/mnt/c/Program Files/SEGGER/JLink/JLink.exe'

PACKAGE=${BK7258_OTA_PACKAGE:-$PACKAGE_DEFAULT}
JLINK_EXE=${JLINK_EXE:-$JLINK_EXE_DEFAULT}
JLINK_SPEED=${BK7258_JLINK_SPEED:-1000}
GENERATION=
TOKEN=
EXECUTE=0
WATCHDOG_STOPPED=0
LOG_FILE=

usage()
{
  cat <<USAGE
Usage: $(basename "$0") --generation N --token N15-WRITE-N [options]

Options:
  --package DIR          Verified n15-ota-host-candidate directory
  --jlink-exe PATH       Windows JLink.exe visible from WSL2
  --speed KHZ            SWD speed (default: ${JLINK_SPEED})
  --log-file PATH        Write the J-Link transcript outside the frozen package
  --watchdog-stopped     Assert that bkota prepare-transfer succeeded
  --execute              Perform the volatile PSRAM load (default: dry run)
  -h, --help             Show this help

Required target sequence before --execute:
  bkota buffer
  bkota prepare-transfer N N15-WRITE-N

The command loads and verifies only:
  0x60800000  s_app-candidate.bin
  0x60a75000  bk7258-ota-stage.bin
  0x60a76000  bk7258-ota-pending-record.bin

It does not reset the board or invoke any Flash command.
USAGE
}

while (($#)); do
  case "$1" in
    --package) PACKAGE=${2:?missing value}; shift ;;
    --jlink-exe) JLINK_EXE=${2:?missing value}; shift ;;
    --speed) JLINK_SPEED=${2:?missing value}; shift ;;
    --log-file) LOG_FILE=${2:?missing value}; shift ;;
    --generation) GENERATION=${2:?missing value}; shift ;;
    --token) TOKEN=${2:?missing value}; shift ;;
    --watchdog-stopped) WATCHDOG_STOPPED=1 ;;
    --execute) EXECUTE=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

[[ $GENERATION =~ ^[1-9][0-9]*$ ]] || {
  echo "ERROR: --generation must be a positive decimal integer" >&2
  exit 2
}
[[ $JLINK_SPEED =~ ^[1-9][0-9]*$ ]] || {
  echo "ERROR: --speed must be a positive decimal integer" >&2
  exit 2
}
[[ $TOKEN == "N15-WRITE-${GENERATION}" ]] || {
  echo "ERROR: token must be exactly N15-WRITE-${GENERATION}" >&2
  exit 3
}
[[ -d $PACKAGE ]] || { echo "ERROR: missing package $PACKAGE" >&2; exit 1; }

PYTHONPYCACHEPREFIX=/tmp/bk7258-n15-pycache \
  python3 "${SCRIPT_DIR}/verify_bk7258_ota_transfer.py" \
    --package "$PACKAGE" --check-only

REPORT="${PACKAGE}/bk7258-ota-transfer.json"
grep -Eq "\"generation\": ${GENERATION},?$" "$REPORT" || {
  echo "ERROR: requested generation does not match verified transfer report" >&2
  exit 3
}

echo "Verified volatile transfer plan:"
echo "  candidate  0x60800000  ${PACKAGE}/s_app-candidate.bin"
echo "  descriptor 0x60a75000  ${PACKAGE}/bk7258-ota-stage.bin"
echo "  record     0x60a76000  ${PACKAGE}/bk7258-ota-pending-record.bin"
echo "  generation ${GENERATION}"

if ((EXECUTE == 0)); then
  echo "DRY RUN: add --watchdog-stopped --execute only after target preparation."
  exit 0
fi

((WATCHDOG_STOPPED == 1)) || {
  echo "ERROR: --execute requires --watchdog-stopped" >&2
  exit 3
}
command -v wslpath >/dev/null 2>&1 || {
  echo "ERROR: this executable path requires WSL2 (wslpath unavailable)" >&2
  exit 1
}
[[ -f $JLINK_EXE ]] || { echo "ERROR: missing J-Link Commander $JLINK_EXE" >&2; exit 1; }

CANDIDATE_WIN=$(wslpath -w "${PACKAGE}/s_app-candidate.bin")
DESCRIPTOR_WIN=$(wslpath -w "${PACKAGE}/bk7258-ota-stage.bin")
RECORD_WIN=$(wslpath -w "${PACKAGE}/bk7258-ota-pending-record.bin")
RUN_TMP=$(mktemp -d /tmp/bk7258-ota-psram.XXXXXX)
COMMAND_FILE="${RUN_TMP}/load.jlink"
if [[ -z $LOG_FILE ]]; then
  LOG_FILE="${PACKAGE}/bk7258-ota-psram-load.log"
fi
mkdir -p "$(dirname "$LOG_FILE")"
trap 'rm -rf "$RUN_TMP"' EXIT

printf '%s\n' \
  halt \
  "loadfile \"${CANDIDATE_WIN}\" 0x60800000 noreset" \
  "verifybin \"${CANDIDATE_WIN}\", 0x60800000" \
  "loadfile \"${DESCRIPTOR_WIN}\" 0x60a75000 noreset" \
  "verifybin \"${DESCRIPTOR_WIN}\", 0x60a75000" \
  "loadfile \"${RECORD_WIN}\" 0x60a76000 noreset" \
  "verifybin \"${RECORD_WIN}\", 0x60a76000" \
  go \
  exit > "$COMMAND_FILE"

set +e
"$JLINK_EXE" -device CORTEX-M33 -if SWD -speed "$JLINK_SPEED" \
  -autoconnect 1 -ExitOnError 1 < "$COMMAND_FILE" 2>&1 | tee "$LOG_FILE"
JLINK_STATUS=${PIPESTATUS[0]}
set -e
if ((JLINK_STATUS != 0)); then
  echo "ERROR: J-Link PSRAM transfer failed with status ${JLINK_STATUS}" >&2
  exit "$JLINK_STATUS"
fi

echo "PSRAM transfer verified. Do not reset before validate-mem/stage-mem/publish-mem."
