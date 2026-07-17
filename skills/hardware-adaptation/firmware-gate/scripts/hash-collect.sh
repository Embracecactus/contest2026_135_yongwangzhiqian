#!/usr/bin/env bash
# hash-collect.sh — Collect SHA-256 hashes and sizes for firmware artifacts.
# Usage: bash hash-collect.sh <nuttx_dir> [sdk_output_dir]
# Output: structured hash table to stdout

set -euo pipefail

NUTTX_DIR="${1:?Usage: hash-collect.sh <nuttx_dir> [sdk_output_dir]}"
SDK_OUT="${2:-}"

echo "=== NuttX Build Artifacts ==="
for f in nuttx nuttx.bin nuttx.map; do
  p="$NUTTX_DIR/$f"
  if [ -f "$p" ]; then
    sz=$(stat -c%s "$p" 2>/dev/null || stat -f%z "$p" 2>/dev/null)
    hash=$(sha256sum "$p" | cut -d' ' -f1)
    echo "$f: ${sz} B  sha256=${hash}"
  else
    echo "$f: (not found)"
  fi
done

if [ -n "$SDK_OUT" ] && [ -d "$SDK_OUT" ]; then
  echo
  echo "=== SDK Output Artifacts ==="

  # rtt.bin (may be symlink)
  rtt="$SDK_OUT/rtt.bin"
  if [ -e "$rtt" ]; then
    real=$(readlink -f "$rtt")
    sz=$(stat -c%s "$real" 2>/dev/null || stat -f%z "$real" 2>/dev/null)
    hash=$(sha256sum "$real" | cut -d' ' -f1)
    echo "rtt.bin -> $(basename "$real"): ${sz} B  sha256=${hash}"
  else
    echo "rtt.bin: (not found)"
  fi

  # amp.img
  amp="$SDK_OUT/firmware/amp.img"
  if [ -f "$amp" ]; then
    sz=$(stat -c%s "$amp" 2>/dev/null || stat -f%z "$amp" 2>/dev/null)
    hash=$(sha256sum "$amp" | cut -d' ' -f1)
    echo "amp.img: ${sz} B  sha256=${hash}"
  else
    echo "amp.img: (not found)"
  fi

  # update.img
  update="$SDK_OUT/update/Image/update.img"
  if [ -f "$update" ]; then
    sz=$(stat -c%s "$update" 2>/dev/null || stat -f%z "$update" 2>/dev/null)
    hash=$(sha256sum "$update" | cut -d' ' -f1)
    echo "update.img: ${sz} B  sha256=${hash}"
  else
    echo "update.img: (not found)"
  fi
fi

echo
echo "=== Hash Consistency Check ==="
if [ -f "$NUTTX_DIR/nuttx.bin" ] && [ -e "${SDK_OUT:-}/rtt.bin" ]; then
  nuttx_hash=$(sha256sum "$NUTTX_DIR/nuttx.bin" | cut -d' ' -f1)
  rtt_hash=$(sha256sum "$(readlink -f "$SDK_OUT/rtt.bin")" | cut -d' ' -f1)
  if [ "$nuttx_hash" = "$rtt_hash" ]; then
    echo "nuttx.bin == rtt.bin target: MATCH"
  else
    echo "nuttx.bin != rtt.bin target: MISMATCH"
    echo "  nuttx.bin: $nuttx_hash"
    echo "  rtt.bin:   $rtt_hash"
  fi
else
  echo "(cannot compare — one or both files missing)"
fi
