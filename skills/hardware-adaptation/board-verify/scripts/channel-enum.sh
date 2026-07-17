#!/usr/bin/env bash
# channel-enum.sh — Enumerate RPMsg channels and virtio devices on Linux.
# Usage: bash channel-enum.sh
# Output: channel/device listing to stdout

set -euo pipefail

echo "=== Virtio Devices ==="
if [ -d /sys/bus/virtio/devices ]; then
  ls -la /sys/bus/virtio/devices/ 2>/dev/null | grep -v '^total' | sed 's/^/  /'
else
  echo "  (no virtio devices directory)"
fi

echo
echo "=== RPMsg Devices ==="
if [ -d /sys/bus/rpmsg/devices ]; then
  for d in /sys/bus/rpmsg/devices/*; do
    [ -e "$d" ] || continue
    name=$(basename "$d")
    printf '  %-40s' "$name"
    if [ -r "$d/name" ]; then
      printf ' name=%s' "$(cat "$d/name")"
    fi
    if [ -r "$d/src" ]; then
      printf ' src=%s' "$(cat "$d/src")"
    fi
    if [ -r "$d/dst" ]; then
      printf ' dst=%s' "$(cat "$d/dst")"
    fi
    echo
  done
else
  echo "  (no rpmsg devices directory)"
fi

echo
echo "=== RPMsg Class ==="
if [ -d /sys/class/rpmsg ]; then
  ls -la /sys/class/rpmsg/ 2>/dev/null | grep -v '^total' | sed 's/^/  /'
else
  echo "  (no rpmsg class directory)"
fi

echo
echo "=== dmesg Channel Events ==="
dmesg 2>/dev/null | grep -Ei 'creating channel|rpmsg-demo|rpmsg.*addr' | tail -10 | sed 's/^/  /' || echo "  (no matching dmesg entries)"

echo
echo "=== Mailbox IRQ Counters ==="
if [ -f /proc/interrupts ]; then
  grep -Ei 'mailbox|20d00000|20d30000' /proc/interrupts 2>/dev/null | sed 's/^/  /' || echo "  (no matching IRQ lines)"
else
  echo "  (/proc/interrupts not available)"
fi
