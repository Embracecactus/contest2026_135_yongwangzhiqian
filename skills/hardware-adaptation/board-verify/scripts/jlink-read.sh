#!/usr/bin/env bash
# jlink-read.sh — Read memory via JLink (JTAG/SWD).
# Usage: bash jlink-read.sh <address> [count] [device]
# Output: memory values to stdout

set -euo pipefail

ADDR="${1:?Usage: jlink-read.sh <address> [count] [device]}"
COUNT="${2:-1}"
DEVICE="${3:-RV1126B}"

if ! command -v JLinkExe >/dev/null 2>&1; then
  echo "ERROR: JLinkExe not found in PATH" >&2
  echo "Install JLink tools from https://www.segger.com/downloads/jlink/" >&2
  exit 1
fi

# Build JLink command script
SCRIPT="connect
mem32 $ADDR $COUNT
exit"

# Run JLink and extract output
OUTPUT=$(JLinkExe -device "$DEVICE" -if JTAG -speed 4000 -nogui 1 -CommandFile <(echo "$SCRIPT") 2>/dev/null)

# Extract memory values (lines starting with the address)
echo "$OUTPUT" | grep -E "^$ADDR" | while IFS= read -r line; do
  echo "$line"
done

# If no matching lines, show full output for debugging
if ! echo "$OUTPUT" | grep -qE "^$ADDR"; then
  echo "(no memory output parsed — showing full JLink output)"
  echo "$OUTPUT" | tail -20
fi
