#!/usr/bin/env bash
# Automate BK7258 build/download and Windows COM11 console capture from WSL2.
# Physical RESET is authoritative when performed manually; J-Link RESETPIN is experimental until BClk is observed.

set -Eeuo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
OPENVELA_ROOT=$(cd "$SCRIPT_DIR/../../../.." && pwd)
BUILD_SCRIPT="$SCRIPT_DIR/build_dual_image.sh"
CAPTURE_PS1="$SCRIPT_DIR/capture_windows_serial.ps1"
PULSE_PS1="$SCRIPT_DIR/pulse_windows_serial.ps1"
LOADER_DIR_DEFAULT='/mnt/c/Users/lijian/Downloads/BEKEN_BKFIL_V2.1.11.15_20241114/BEKEN_BKFIL_V2.1.11.15_20241114'
LOADER_DIR=${BK_LOADER_DIR:-$LOADER_DIR_DEFAULT}
LOADER_EXE="$LOADER_DIR/bk_loader.exe"
JLINK_EXE_DEFAULT='/mnt/c/Program Files/SEGGER/JLink/JLink.exe'
JLINK_EXE=${JLINK_EXE:-$JLINK_EXE_DEFAULT}
FIRMWARE="$OPENVELA_ROOT/nuttx/bk7258-dual/all-app-factory.bin"
LOG_ROOT="$OPENVELA_ROOT/logs/bk7258-auto-debug"

CP_CONFIG_NAME=${CP_CONFIG_NAME:-cp_nsh}
AP_CONFIG_NAME=${AP_CONFIG_NAME:-ap_smp}
DOWNLOAD_PORT=${BK_DOWNLOAD_PORT:-7}
CONSOLE_PORT=${BK_CONSOLE_PORT:-COM11}
CONSOLE_BAUD=${BK_CONSOLE_BAUD:-460800}
DOWNLOAD_BAUD=${BK_DOWNLOAD_BAUD:-6000000}
CAPTURE_SECONDS=${BK_CAPTURE_SECONDS:-25}

DO_BUILD=0
DO_FLASH=0
SPARSE_FLASH=0
COLD_CAPTURE=0
JLINK_RESET=0
RTS_RESET=0
ASSUME_YES=0
CP_CONFIG_EXPLICIT=0
AP_CONFIG_EXPLICIT=0

usage()
{
  cat <<USAGE
Usage: $(basename "$0") [options]

Actions:
  --build                 Build the selected CP_CONFIG_NAME/AP_CONFIG_NAME pair
  --flash                 Download all-app-factory.bin using Windows bk_loader.exe
  --sparse-flash          With --flash, update boot/CP/AP and preserve LittleFS
  --cold-capture          Capture COM11 and ask for a manual physical RESET; no download
  --rts-reset             Capture COM11, then pulse COM7 RTS (verified physical reset)
  --jlink-reset           Capture COM11, then try J-Link RSetType 2 (experimental)
  --yes                   Skip the factory-image erase confirmation

Options:
  --cp-config NAME        CP config (default: $CP_CONFIG_NAME)
  --ap-config NAME        AP config (default: $AP_CONFIG_NAME)
  --capture-seconds N     Serial capture duration (default: $CAPTURE_SECONDS)
  --download-port N       bk_loader port number (default: $DOWNLOAD_PORT / COM$DOWNLOAD_PORT)
  --console-port COMN     Windows console port (default: $CONSOLE_PORT)
  --console-baud N        Console baud (default: $CONSOLE_BAUD)
  --firmware PATH         Factory image path (default: $FIRMWARE)
  --log-root PATH         Output directory (default: $LOG_ROOT)
  -h, --help              Show this help

Examples:
  # Build an explicit CP/AP profile, flash, and capture the warm path:
  $(basename "$0") --build --flash --cp-config cp_nsh --ap-config ap_smp_bidir

  # Flash an already built image and capture:
  $(basename "$0") --flash

  # Capture a physical RESET performed manually after the prompt:
  $(basename "$0") --cold-capture --capture-seconds 30

  # Verified automated physical reset using COM7 RTS:
  $(basename "$0") --rts-reset --capture-seconds 30

  # Experimentally request J-Link RESETPIN reset and capture; require BClk:
  $(basename "$0") --jlink-reset --capture-seconds 30

Port assignment on the current CH342 adapter:
  COM7  = downloader / bk_loader (-p 7)
  COM11 = firmware console (460800 8N1)
USAGE
}

while (($#)); do
  case "$1" in
    --build) DO_BUILD=1 ;;
    --flash) DO_FLASH=1 ;;
    --sparse-flash) SPARSE_FLASH=1 ;;
    --cold-capture) COLD_CAPTURE=1 ;;
    --rts-reset) RTS_RESET=1 ;;
    --jlink-reset) JLINK_RESET=1 ;;
    --yes) ASSUME_YES=1 ;;
    --cp-config) CP_CONFIG_NAME=${2:?missing value}; CP_CONFIG_EXPLICIT=1; shift ;;
    --ap-config) AP_CONFIG_NAME=${2:?missing value}; AP_CONFIG_EXPLICIT=1; shift ;;
    --capture-seconds) CAPTURE_SECONDS=${2:?missing value}; shift ;;
    --download-port) DOWNLOAD_PORT=${2:?missing value}; shift ;;
    --console-port) CONSOLE_PORT=${2:?missing value}; shift ;;
    --console-baud) CONSOLE_BAUD=${2:?missing value}; shift ;;
    --firmware) FIRMWARE=${2:?missing value}; shift ;;
    --log-root) LOG_ROOT=${2:?missing value}; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

action_count=$((DO_FLASH + COLD_CAPTURE + RTS_RESET + JLINK_RESET))
if ((action_count != 1)); then
  echo "ERROR: choose exactly one of --flash, --cold-capture, --rts-reset, or --jlink-reset" >&2
  usage >&2
  exit 2
fi

if ((SPARSE_FLASH && !DO_FLASH)); then
  echo "ERROR: --sparse-flash requires --flash" >&2
  exit 2
fi

for n in "$CAPTURE_SECONDS" "$DOWNLOAD_PORT" "$CONSOLE_BAUD" "$DOWNLOAD_BAUD"; do
  [[ $n =~ ^[0-9]+$ ]] || { echo "ERROR: numeric option expected, got '$n'" >&2; exit 2; }
done

command -v powershell.exe >/dev/null 2>&1 || {
  echo "ERROR: WSL Windows interop is unavailable (powershell.exe not found)" >&2
  exit 1
}

[[ -f "$CAPTURE_PS1" ]] || { echo "ERROR: missing $CAPTURE_PS1" >&2; exit 1; }
if ((DO_FLASH)); then
  [[ -x "$LOADER_EXE" || -f "$LOADER_EXE" ]] || { echo "ERROR: missing $LOADER_EXE" >&2; exit 1; }
fi
if ((RTS_RESET)); then
  [[ -f "$PULSE_PS1" ]] || { echo "ERROR: missing $PULSE_PS1" >&2; exit 1; }
fi
if ((JLINK_RESET)); then
  [[ -x "$JLINK_EXE" || -f "$JLINK_EXE" ]] || { echo "ERROR: missing $JLINK_EXE" >&2; exit 1; }
fi

if ((DO_BUILD)); then
  echo "==> Building CP=$CP_CONFIG_NAME AP=$AP_CONFIG_NAME"
  CP_CONFIG_NAME="$CP_CONFIG_NAME" AP_CONFIG_NAME="$AP_CONFIG_NAME" "$BUILD_SCRIPT"
fi

[[ -f "$FIRMWARE" ]] || { echo "ERROR: missing firmware $FIRMWARE" >&2; exit 1; }

DUAL_DIR="$OPENVELA_ROOT/nuttx/bk7258-dual"
BOOT_IMAGE="$DUAL_DIR/bl_crc.bin"
CP_IMAGE="$DUAL_DIR/app_crc_flash.bin"
AP_IMAGE="$DUAL_DIR/app1_crc_flash.bin"
if ((SPARSE_FLASH)); then
  for image in "$BOOT_IMAGE" "$CP_IMAGE" "$AP_IMAGE"; do
    [[ -f "$image" ]] || { echo "ERROR: missing sparse image $image" >&2; exit 1; }
  done

  BOOT_IMAGE_SIZE=$(stat -c %s "$BOOT_IMAGE")
  CP_IMAGE_SIZE=$(stat -c %s "$CP_IMAGE")
  AP_IMAGE_SIZE=$(stat -c %s "$AP_IMAGE")

  # Physical flash uses Beken's 34-byte-for-32-byte coding expansion:
  # boot 0x000000..0x011000, CP 0x011000..0x110000,
  # LittleFS 0x110000..0x220000, AP 0x220000..0x440000.
  # Refuse malformed/oversized artifacts before bk_loader can erase across a
  # partition boundary.  This is the hard guarantee behind "preserve
  # LittleFS", independent of the build script's own size checks.

  ((BOOT_IMAGE_SIZE > 0 && BOOT_IMAGE_SIZE <= 0x11000)) || {
    echo "ERROR: sparse boot image length $BOOT_IMAGE_SIZE exceeds 0x11000" >&2
    exit 1
  }
  ((CP_IMAGE_SIZE > 0 && 0x11000 + CP_IMAGE_SIZE <= 0x110000)) || {
    echo "ERROR: sparse CP image length $CP_IMAGE_SIZE crosses LittleFS at 0x110000" >&2
    exit 1
  }
  ((AP_IMAGE_SIZE > 0 && 0x220000 + AP_IMAGE_SIZE <= 0x440000)) || {
    echo "ERROR: sparse AP image length $AP_IMAGE_SIZE exceeds its partition" >&2
    exit 1
  }
fi

PROFILE_FILE="$OPENVELA_ROOT/nuttx/bk7258-dual/build-profile.txt"
PACKAGED_CP_CONFIG=unknown
PACKAGED_AP_CONFIG=unknown
if [[ -f "$PROFILE_FILE" ]]; then
  PACKAGED_CP_CONFIG=$(sed -n 's/^CP_CONFIG_NAME=//p' "$PROFILE_FILE" | head -1)
  PACKAGED_AP_CONFIG=$(sed -n 's/^AP_CONFIG_NAME=//p' "$PROFILE_FILE" | head -1)
fi

if ((!DO_BUILD)); then
  if [[ $PACKAGED_CP_CONFIG == unknown || $PACKAGED_AP_CONFIG == unknown ]]; then
    echo "WARNING: packaged CP/AP profile is unknown; rebuild once with the current builder to create build-profile.txt" >&2
  else
    echo "==> Packaged profile: CP=$PACKAGED_CP_CONFIG AP=$PACKAGED_AP_CONFIG"
    if ((CP_CONFIG_EXPLICIT)) && [[ $CP_CONFIG_NAME != "$PACKAGED_CP_CONFIG" ]]; then
      echo "ERROR: expected CP=$CP_CONFIG_NAME but packaged CP=$PACKAGED_CP_CONFIG" >&2
      exit 1
    fi
    if ((AP_CONFIG_EXPLICIT)) && [[ $AP_CONFIG_NAME != "$PACKAGED_AP_CONFIG" ]]; then
      echo "ERROR: expected AP=$AP_CONFIG_NAME but packaged AP=$PACKAGED_AP_CONFIG" >&2
      exit 1
    fi
  fi
fi

if ((DO_FLASH && !SPARSE_FLASH && !ASSUME_YES)); then
  echo "WARNING: all-app-factory.bin pads the LittleFS region with 0xff."
  if [[ -t 0 ]]; then
    read -r -p "Type FLASH to continue: " answer
    [[ $answer == FLASH ]] || { echo "Cancelled"; exit 3; }
  else
    echo "ERROR: non-interactive factory download requires --yes" >&2
    exit 3
  fi
fi

# Verify the two independent CH342 ports before starting a destructive action.
PORTS=$(powershell.exe -NoProfile -Command '[System.IO.Ports.SerialPort]::GetPortNames()' | tr -d '\r')
grep -qx "COM${DOWNLOAD_PORT}" <<<"$PORTS" || {
  echo "ERROR: downloader COM${DOWNLOAD_PORT} is not present" >&2
  printf '%s\n' "$PORTS" >&2
  exit 1
}
grep -qx "$CONSOLE_PORT" <<<"$PORTS" || {
  echo "ERROR: console $CONSOLE_PORT is not present" >&2
  printf '%s\n' "$PORTS" >&2
  exit 1
}

STAMP=$(date +%Y%m%d-%H%M%S)
RUN_DIR="$LOG_ROOT/$STAMP"
mkdir -p "$RUN_DIR"
SERIAL_RAW="$RUN_DIR/serial.raw"
SERIAL_TEXT="$RUN_DIR/serial.txt"
SERIAL_STDOUT="$RUN_DIR/serial-capture.stdout.log"
READY_FILE="$RUN_DIR/serial.ready"
DOWNLOAD_LOG="$RUN_DIR/download.log"
SUMMARY_FILE="$RUN_DIR/summary.txt"
JLINK_LOG="$RUN_DIR/jlink-reset.log"
RESET_LOG="$RUN_DIR/serial-reset.log"
ARTIFACT_FILE="$RUN_DIR/artifacts.sha256"

{
  printf 'ACTION_BUILD=%s\n' "$DO_BUILD"
  if ((DO_BUILD)); then
    printf 'REQUESTED_CP_CONFIG_NAME=%s\n' "$CP_CONFIG_NAME"
    printf 'REQUESTED_AP_CONFIG_NAME=%s\n' "$AP_CONFIG_NAME"
  fi
  printf 'PACKAGED_CP_CONFIG_NAME=%s\n' "$PACKAGED_CP_CONFIG"
  printf 'PACKAGED_AP_CONFIG_NAME=%s\n' "$PACKAGED_AP_CONFIG"
  sha256sum "$FIRMWARE"
  stat -c '%y %s %n' "$FIRMWARE"
  if ((SPARSE_FLASH)); then
    sha256sum "$BOOT_IMAGE" "$CP_IMAGE" "$AP_IMAGE"
    stat -c '%y %s %n' "$BOOT_IMAGE" "$CP_IMAGE" "$AP_IMAGE"
  fi
  if [[ -f "$PROFILE_FILE" ]]; then
    cat "$PROFILE_FILE"
  fi
} > "$ARTIFACT_FILE"

PS1_WIN=$(wslpath -w "$CAPTURE_PS1")
RAW_WIN=$(wslpath -w "$SERIAL_RAW")
READY_WIN=$(wslpath -w "$READY_FILE")

cleanup()
{
  if [[ -n ${CAPTURE_PID:-} ]] && kill -0 "$CAPTURE_PID" 2>/dev/null; then
    kill "$CAPTURE_PID" 2>/dev/null || true
    wait "$CAPTURE_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PS1_WIN" \
  -Port "$CONSOLE_PORT" \
  -Baud "$CONSOLE_BAUD" \
  -DurationSec "$CAPTURE_SECONDS" \
  -OutputFile "$RAW_WIN" \
  -ReadyFile "$READY_WIN" \
  >"$SERIAL_STDOUT" 2>&1 &
CAPTURE_PID=$!

for _ in $(seq 1 100); do
  [[ -f "$READY_FILE" ]] && break
  if ! kill -0 "$CAPTURE_PID" 2>/dev/null; then
    wait "$CAPTURE_PID" || true
    echo "ERROR: serial capture failed before becoming ready" >&2
    cat "$SERIAL_STDOUT" >&2 || true
    exit 1
  fi
  sleep 0.05
done

[[ -f "$READY_FILE" ]] || {
  echo "ERROR: timed out opening $CONSOLE_PORT" >&2
  cat "$SERIAL_STDOUT" >&2 || true
  exit 1
}

echo "==> Capturing $CONSOLE_PORT at $CONSOLE_BAUD baud"

if ((DO_FLASH)); then
  if ((SPARSE_FLASH)); then
    BOOT_IMAGE_WIN=$(wslpath -m "$BOOT_IMAGE")
    CP_IMAGE_WIN=$(wslpath -m "$CP_IMAGE")
    AP_IMAGE_WIN=$(wslpath -m "$AP_IMAGE")
    printf -v BOOT_LENGTH_HEX '0x%x' "$(stat -c %s "$BOOT_IMAGE")"
    printf -v CP_LENGTH_HEX '0x%x' "$(stat -c %s "$CP_IMAGE")"
    printf -v AP_LENGTH_HEX '0x%x' "$(stat -c %s "$AP_IMAGE")"
    MAIN_BIN_MULTI="${BOOT_IMAGE_WIN}@0x0-${BOOT_LENGTH_HEX},"
    MAIN_BIN_MULTI+="${CP_IMAGE_WIN}@0x11000-${CP_LENGTH_HEX},"
    MAIN_BIN_MULTI+="${AP_IMAGE_WIN}@0x220000-${AP_LENGTH_HEX}"
    echo "==> Sparse download through COM${DOWNLOAD_PORT}; LittleFS preserved"
  else
    FIRMWARE_WIN=$(wslpath -m "$FIRMWARE")
    MAIN_BIN_MULTI="${FIRMWARE_WIN}@0x0"
    echo "==> Factory download through COM${DOWNLOAD_PORT}: $FIRMWARE_WIN"
  fi
  set +e
  (
    cd "$LOADER_DIR"
    "$LOADER_EXE" download \
      -p "$DOWNLOAD_PORT" \
      -b "$DOWNLOAD_BAUD" \
      --uart-type OTHER \
      --mainBin-multi "$MAIN_BIN_MULTI" \
      --reboot 1 \
      --fast-link 1
  ) 2>&1 | tee "$DOWNLOAD_LOG"
  loader_rc=${PIPESTATUS[0]}
  set -e
  if grep -aEq -- '->[[:space:]]*fail' "$DOWNLOAD_LOG"; then
    echo "ERROR: bk_loader reported a flash operation failure; refusing the global success banner" >&2
    loader_rc=1
  elif grep -aFq 'Writing Flash OK' "$DOWNLOAD_LOG" &&
     grep -aFq '{All Finished Successfully}' "$DOWNLOAD_LOG"; then
    if ((loader_rc != 0)); then
      echo "WARNING: bk_loader returned $loader_rc despite explicit success markers; normalizing to success" >&2
    fi
    loader_rc=0
  elif ((loader_rc != 0)); then
    echo "ERROR: bk_loader exited with $loader_rc and no complete success markers" >&2
  fi
elif ((RTS_RESET)); then
  echo "==> Pulsing COM${DOWNLOAD_PORT} RTS for a verified physical reset"
  PULSE_WIN=$(wslpath -w "$PULSE_PS1")
  set +e
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PULSE_WIN" \
    -Port "COM${DOWNLOAD_PORT}" -Mode RTS -PulseMs 150 \
    2>&1 | tee "$RESET_LOG"
  reset_rc=${PIPESTATUS[0]}
  set -e
  if ((reset_rc != 0)); then
    echo "ERROR: COM${DOWNLOAD_PORT} RTS reset exited with $reset_rc" >&2
  fi
elif ((JLINK_RESET)); then
  echo "==> Resetting through J-Link RESETPIN strategy (RSetType 2)"
  set +e
  printf 'RSetType 2\nReset\nGo\nExit\n' | \
    "$JLINK_EXE" -device CORTEX-M33 -if SWD -speed 1000 -autoconnect 1 \
    2>&1 | tee "$JLINK_LOG"
  jlink_rc=${PIPESTATUS[1]}
  set -e
  if ((jlink_rc != 0)); then
    echo "ERROR: J-Link reset exited with $jlink_rc" >&2
  fi
else
  echo
  echo "==> Serial capture is ready. Press the board physical RESET now."
  echo "==> Capture will stop automatically after ${CAPTURE_SECONDS}s."
fi

set +e
wait "$CAPTURE_PID"
capture_rc=$?
set -e
CAPTURE_PID=

python3 - "$SERIAL_RAW" "$SERIAL_TEXT" "$SUMMARY_FILE" <<'PY'
from pathlib import Path
import sys

raw_path, text_path, summary_path = map(Path, sys.argv[1:])
data = raw_path.read_bytes() if raw_path.exists() else b""
text = data.decode("utf-8", errors="replace").replace("\x00", "")
text_path.write_text(text)

ordered = [
    "BClk", "S0", "U0", "G1", "U1", "U2", "U3", "U4", "U5",
    "C0", "C1", "C2", "C3", "A0", "A1", "A2", "A3", "A4",
    "A5", "A6", "W0", "W1", "A7", "F1", "F2", "C4", "C5", "C6", "C7", "C8",
]
checkpoint_lines = {line.strip() for line in text.splitlines() if line.strip()}
present = [m for m in ordered if m in checkpoint_lines]
last = present[-1] if present else "none"
if "NuttShell (NSH)" in text or "nsh>" in text:
    verdict = "PASS_NSH"
elif "U1" in checkpoint_lines and "U2" not in checkpoint_lines:
    verdict = "STOP_BETWEEN_U1_U2"
elif "C8" in checkpoint_lines:
    verdict = "STOP_AFTER_C8_BEFORE_NSH"
elif present:
    verdict = f"STOP_AFTER_{last}"
else:
    verdict = "NO_CHECKPOINT"

lines = [
    f"serial_bytes={len(data)}",
    f"verdict={verdict}",
    f"last_checkpoint={last}",
    f"checkpoints={' '.join(present) if present else 'none'}",
    f"cold_path={'yes' if any(line.startswith('BClk ') for line in checkpoint_lines) else 'no'}",
    f"uart_init_returned={'yes' if 'U2' in checkpoint_lines else 'no'}",
    f"ap_timeout_cleanup={'yes' if 'F1' in checkpoint_lines and 'F2' in checkpoint_lines else 'no'}",
    f"nsh={'yes' if verdict == 'PASS_NSH' else 'no'}",
]
summary = "\n".join(lines) + "\n"
summary_path.write_text(summary)
print(summary, end="")
PY

cat "$SUMMARY_FILE"
echo "==> Logs: $RUN_DIR"

if ((capture_rc != 0)); then
  echo "ERROR: serial capture exited with $capture_rc" >&2
  exit "$capture_rc"
fi
if ((DO_FLASH && loader_rc != 0)); then
  exit "$loader_rc"
fi
if ((RTS_RESET && reset_rc != 0)); then
  exit "$reset_rc"
fi
if ((RTS_RESET)) && ! grep -qx 'cold_path=yes' "$SUMMARY_FILE"; then
  echo "ERROR: RTS toggled but no BClk cold-reset signature was captured" >&2
  exit 1
fi
if ((JLINK_RESET && jlink_rc != 0)); then
  exit "$jlink_rc"
fi
