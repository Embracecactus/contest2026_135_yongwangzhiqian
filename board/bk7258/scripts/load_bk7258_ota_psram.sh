#!/usr/bin/env bash
# Load the N15-F validation artifacts into the fixed upper-PSRAM window.
# This tool never resets the target and never writes Flash.

set -Eeuo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
OPENVELA_ROOT=$(cd "${SCRIPT_DIR}/../../../.." && pwd)
PACKAGE_DEFAULT="${OPENVELA_ROOT}/nuttx/bk7258-dual-ota-validation/n15-ota-host-candidate"
JLINK_EXE_DEFAULT='/mnt/c/Program Files/SEGGER/JLink/JLink.exe'
CANDIDATE_ADDRESS=$((0x60800000))
DESCRIPTOR_ADDRESS=$((0x60a75000))
RECORD_ADDRESS=$((0x60a76000))
CANDIDATE_CHUNK_SIZE=$((64 * 1024))
CANDIDATE_BATCH_CHUNKS=1

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

The candidate is transferred as independently verified 64 KiB chunks, with a
fresh J-Link Commander process for every chunk.  This avoids the observed
J-Link V9 large-load and repeated-command failures while preserving an exact
full-range read-back gate.

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
command -v powershell.exe >/dev/null 2>&1 || {
  echo "ERROR: Windows PowerShell is required to locate a native temp directory" >&2
  exit 1
}
[[ -f $JLINK_EXE ]] || { echo "ERROR: missing J-Link Commander $JLINK_EXE" >&2; exit 1; }

WINDOWS_TEMP_WIN=$(powershell.exe -NoProfile -Command '[IO.Path]::GetTempPath()' |
  tr -d '\r\n')
[[ -n $WINDOWS_TEMP_WIN ]] || {
  echo "ERROR: Windows temporary directory query returned no path" >&2
  exit 1
}
WINDOWS_TEMP_WSL=$(wslpath -u "$WINDOWS_TEMP_WIN")
[[ -d $WINDOWS_TEMP_WSL ]] || {
  echo "ERROR: Windows temporary directory is unavailable from WSL: $WINDOWS_TEMP_WSL" >&2
  exit 1
}
RUN_TMP=$(mktemp -d "${WINDOWS_TEMP_WSL%/}/bk7258-ota-psram.XXXXXX")
COMMAND_DIR="${RUN_TMP}/commands"
mkdir -p "$COMMAND_DIR"
if [[ -z $LOG_FILE ]]; then
  LOG_FILE="${PACKAGE}/bk7258-ota-psram-load.log"
fi
mkdir -p "$(dirname "$LOG_FILE")"
trap 'rm -rf "$RUN_TMP"' EXIT

DESCRIPTOR_LOCAL="${RUN_TMP}/bk7258-ota-stage.bin"
RECORD_LOCAL="${RUN_TMP}/bk7258-ota-pending-record.bin"
cp "${PACKAGE}/bk7258-ota-stage.bin" "$DESCRIPTOR_LOCAL"
cp "${PACKAGE}/bk7258-ota-pending-record.bin" "$RECORD_LOCAL"
DESCRIPTOR_WIN=$(wslpath -w "$DESCRIPTOR_LOCAL")
RECORD_WIN=$(wslpath -w "$RECORD_LOCAL")

CANDIDATE_FILE="${PACKAGE}/s_app-candidate.bin"
CANDIDATE_SIZE=$(stat -c %s "$CANDIDATE_FILE")
CANDIDATE_CAPACITY=$((DESCRIPTOR_ADDRESS - CANDIDATE_ADDRESS))
[[ $CANDIDATE_SIZE -eq $CANDIDATE_CAPACITY ]] || {
  echo "ERROR: candidate size ${CANDIDATE_SIZE} does not fill fixed window ${CANDIDATE_CAPACITY}" >&2
  exit 3
}
command -v split >/dev/null 2>&1 || {
  echo "ERROR: GNU split is required for bounded J-Link transfers" >&2
  exit 1
}
split --bytes="$CANDIDATE_CHUNK_SIZE" --numeric-suffixes=0 \
  --suffix-length=4 --additional-suffix=.bin \
  "$CANDIDATE_FILE" "${RUN_TMP}/candidate."

CANDIDATE_OFFSET=0
CHUNK_COUNT=0
BATCH_COUNT=0
for CHUNK_FILE in "${RUN_TMP}"/candidate.*; do
  if ((CHUNK_COUNT % CANDIDATE_BATCH_CHUNKS == 0)); then
    printf -v COMMAND_FILE '%s/batch-%03d.jlink' \
      "$COMMAND_DIR" "$BATCH_COUNT"
    printf '%s\n' halt > "$COMMAND_FILE"
    BATCH_COUNT=$((BATCH_COUNT + 1))
  fi

  CHUNK_SIZE=$(stat -c %s "$CHUNK_FILE")
  CHUNK_ADDRESS=$((CANDIDATE_ADDRESS + CANDIDATE_OFFSET))
  CHUNK_WIN=$(wslpath -w "$CHUNK_FILE")
  printf 'loadfile "%s" 0x%08x noreset\n' \
    "$CHUNK_WIN" "$CHUNK_ADDRESS" >> "$COMMAND_FILE"
  printf 'verifybin "%s", 0x%08x\n' \
    "$CHUNK_WIN" "$CHUNK_ADDRESS" >> "$COMMAND_FILE"
  CANDIDATE_OFFSET=$((CANDIDATE_OFFSET + CHUNK_SIZE))
  CHUNK_COUNT=$((CHUNK_COUNT + 1))
  if ((CHUNK_COUNT % CANDIDATE_BATCH_CHUNKS == 0)); then
    printf '%s\n' go exit >> "$COMMAND_FILE"
  fi
done
[[ $CANDIDATE_OFFSET -eq $CANDIDATE_SIZE ]] || {
  echo "ERROR: chunk coverage ${CANDIDATE_OFFSET} != candidate size ${CANDIDATE_SIZE}" >&2
  exit 3
}
if ((CHUNK_COUNT % CANDIDATE_BATCH_CHUNKS != 0)); then
  printf '%s\n' go exit >> "$COMMAND_FILE"
fi

printf -v COMMAND_FILE '%s/batch-%03d.jlink' \
  "$COMMAND_DIR" "$BATCH_COUNT"
BATCH_COUNT=$((BATCH_COUNT + 1))
printf '%s\n' halt > "$COMMAND_FILE"
printf 'loadfile "%s" 0x%08x noreset\n' \
  "$DESCRIPTOR_WIN" "$DESCRIPTOR_ADDRESS" >> "$COMMAND_FILE"
printf 'verifybin "%s", 0x%08x\n' \
  "$DESCRIPTOR_WIN" "$DESCRIPTOR_ADDRESS" >> "$COMMAND_FILE"
printf 'loadfile "%s" 0x%08x noreset\n' \
  "$RECORD_WIN" "$RECORD_ADDRESS" >> "$COMMAND_FILE"
printf 'verifybin "%s", 0x%08x\n' \
  "$RECORD_WIN" "$RECORD_ADDRESS" >> "$COMMAND_FILE"
printf '%s\n' go exit >> "$COMMAND_FILE"

echo "  J-Link chunks ${CHUNK_COUNT} x <= ${CANDIDATE_CHUNK_SIZE} bytes"
echo "  J-Link batches ${BATCH_COUNT} x <= ${CANDIDATE_BATCH_CHUNKS} candidate chunks"

: > "$LOG_FILE"
BATCH_INDEX=0
for COMMAND_FILE in "${COMMAND_DIR}"/batch-*.jlink; do
  BATCH_NUMBER=$((BATCH_INDEX + 1))
  echo "J-Link PSRAM batch ${BATCH_NUMBER}/${BATCH_COUNT}"
  echo "J-Link PSRAM batch ${BATCH_NUMBER}/${BATCH_COUNT}" >> "$LOG_FILE"
  set +e
  "$JLINK_EXE" -device CORTEX-M33 -if SWD -speed "$JLINK_SPEED" \
    -autoconnect 1 -ExitOnError 1 < "$COMMAND_FILE" >> "$LOG_FILE" 2>&1
  JLINK_STATUS=$?
  set -e
  if ((JLINK_STATUS != 0)); then
    # ExitOnError can stop before the command file reaches `go`.  Best-effort
    # resume preserves the script's no-reset contract and does not mask the
    # original transfer status.
    printf '%s\n' go exit | \
      "$JLINK_EXE" -device CORTEX-M33 -if SWD -speed "$JLINK_SPEED" \
        -autoconnect 1 >> "$LOG_FILE" 2>&1 || true
    tail -n 80 "$LOG_FILE" >&2
    echo "ERROR: J-Link PSRAM batch ${BATCH_NUMBER} failed with status ${JLINK_STATUS}" >&2
    exit "$JLINK_STATUS"
  fi

  echo "J-Link PSRAM batch ${BATCH_NUMBER}/${BATCH_COUNT} PASS"
  BATCH_INDEX=$((BATCH_INDEX + 1))
done
[[ $BATCH_INDEX -eq $BATCH_COUNT ]] || {
  echo "ERROR: executed ${BATCH_INDEX} J-Link batches, expected ${BATCH_COUNT}" >&2
  exit 3
}

echo "PSRAM transfer verified. Do not reset before validate-mem/stage-mem/publish-mem."
