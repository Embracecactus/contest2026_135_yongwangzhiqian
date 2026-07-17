#!/usr/bin/env bash
# devmem-read.sh — Read RV1126B mailbox registers via devmem.
# Usage: bash devmem-read.sh [base_address]
# Default: reads all standard mailbox register sets
# Output: register values to stdout

set -euo pipefail

# Detect devmem availability
if command -v devmem >/dev/null 2>&1; then
  DEVMEM="devmem"
elif command -v busybox >/dev/null 2>&1 && busybox --list 2>/dev/null | grep -q devmem; then
  DEVMEM="busybox devmem"
else
  echo "ERROR: devmem not available on this system" >&2
  exit 1
fi

read_reg() {
  local addr="$1"
  local name="$2"
  local val
  val=$($DEVMEM "$addr" 32 2>/dev/null) || val="ERROR"
  printf '%-14s %-30s %s\n' "$addr" "$name" "$val"
}

echo "=== RV1126B Mailbox Registers ==="
echo "--- MBOX7 (0x20d30000) A2B: Linux → HPMCU ---"
read_reg 0x20d30000 "A2B_INTEN"
read_reg 0x20d30004 "A2B_STATUS"
read_reg 0x20d30008 "A2B_CMD"
read_reg 0x20d3000c "A2B_DATA"

echo
echo "--- MBOX4 (0x20d00000) B2A: HPMCU → Linux (vqid0) ---"
read_reg 0x20d00010 "B2A_INTEN"
read_reg 0x20d00014 "B2A_STATUS"

echo
echo "--- MBOX7 (0x20d30000) B2A: HPMCU → Linux (vqid1) ---"
read_reg 0x20d30010 "B2A_INTEN"
read_reg 0x20d30014 "B2A_STATUS"

echo
echo "=== Interpretation ==="

A2B_INTEN=$(devmem 0x20d30000 32 2>/dev/null || echo "0x0")
A2B_STATUS=$(devmem 0x20d30004 32 2>/dev/null || echo "0x0")

if [ "$A2B_INTEN" = "0x101" ] || [ "$A2B_INTEN" = "0x00000101" ]; then
  echo "A2B_INTEN: bit0=1 (RX enabled), bit8=1 (TRIG_MODE) — GOOD"
elif [ "$A2B_INTEN" = "0x100" ] || [ "$A2B_INTEN" = "0x00000100" ]; then
  echo "A2B_INTEN: bit0=0 (RX DISABLED), bit8=1 — handshake not received or callback not registered"
else
  echo "A2B_INTEN: $A2B_INTEN — unexpected value"
fi

if [ "$A2B_STATUS" = "0x0" ] || [ "$A2B_STATUS" = "0x00000000" ]; then
  echo "A2B_STATUS: 0 (consumed or no event) — normal after handshake"
else
  echo "A2B_STATUS: $A2B_STATUS — pending event"
fi
