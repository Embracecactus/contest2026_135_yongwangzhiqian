#!/usr/bin/env bash
# serial-run.sh — Send a command to NuttX via serial and capture output.
# Usage: bash serial-run.sh <command> [serial_device] [baud_rate]
# Requires: screen or minicom
# Output: command output to stdout (with timeout)

set -euo pipefail

CMD="${1:?Usage: serial-run.sh <command> [serial_device] [baud_rate]}"
SERIAL="${2:-/dev/ttyUSB0}"
BAUD="${3:-115200}"
TIMEOUT=5

# Check serial device exists
if [ ! -e "$SERIAL" ]; then
  echo "ERROR: Serial device $SERIAL not found" >&2
  echo "Available:" >&2
  ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | sed 's/^/  /' >&2 || echo "  (none)" >&2
  exit 1
fi

# Detect available tool
if command -v screen >/dev/null 2>&1; then
  TOOL="screen"
elif command -v minicom >/dev/null 2>&1; then
  TOOL="minicom"
else
  echo "ERROR: Neither screen nor minicom found" >&2
  exit 1
fi

TMPFILE=$(mktemp /tmp/serial-XXXXXX.log)
trap "rm -f $TMPFILE" EXIT

if [ "$TOOL" = "screen" ]; then
  # Start screen in background, send command, capture output
  screen -dmS nuttx-serial -L -Logfile "$TMPFILE" "$SERIAL" "$BAUD"
  sleep 0.5
  screen -S nuttx-serial -p 0 -X stuff "$CMD\n"
  sleep "$TIMEOUT"
  screen -S nuttx-serial -X quit 2>/dev/null || true
  cat "$TMPFILE"
elif [ "$TOOL" = "minicom" ]; then
  # Start minicom with capture
  minicom -D "$SERIAL" -b "$BAUD" -C "$TMPFILE" &
  MINICOM_PID=$!
  sleep 0.5
  # Send command via minicom's key injection
  echo -e "$CMD\r" > "$SERIAL"
  sleep "$TIMEOUT"
  kill $MINICOM_PID 2>/dev/null || true
  cat "$TMPFILE"
fi
