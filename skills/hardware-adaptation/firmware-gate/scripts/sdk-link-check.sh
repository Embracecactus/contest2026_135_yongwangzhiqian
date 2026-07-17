#!/usr/bin/env bash
# sdk-link-check.sh — Verify SDK packaging toolchain and link integrity.
# Usage: bash sdk-link-check.sh <sdk_path>
# Output: verification results to stdout

set -euo pipefail

SDK="${1:?Usage: sdk-link-check.sh <sdk_path>}"
OUT="$SDK/output"
FW="$OUT/firmware"

echo "=== SDK Path Verification ==="
echo "SDK: $SDK"
echo "OUT: $OUT"
echo "FW:  $FW"

echo
echo "=== Required Files ==="

# rtt.bin symlink
rtt="$OUT/rtt.bin"
if [ -L "$rtt" ]; then
  target=$(readlink -f "$rtt")
  if [ -f "$target" ]; then
    echo "rtt.bin: symlink -> $target (EXISTS)"
  else
    echo "rtt.bin: symlink -> $target (BROKEN)"
  fi
elif [ -f "$rtt" ]; then
  echo "rtt.bin: regular file (not symlink)"
else
  echo "rtt.bin: MISSING"
fi

# amp.its
its="$OUT/amp.its"
if [ -f "$its" ]; then
  echo "amp.its: EXISTS ($(stat -c%s "$its") B)"
  # Check what files amp.its references
  echo "  references:"
  grep -oE 'data\s*=\s*"[^"]*"' "$its" 2>/dev/null | sed 's/^/    /' || true
else
  echo "amp.its: MISSING"
fi

# mkimage tool
mkimage="$SDK/rtos/bsp/rockchip/tools/mkimage"
if [ -x "$mkimage" ]; then
  echo "mkimage: EXECUTABLE"
elif [ -f "$mkimage" ]; then
  echo "mkimage: EXISTS but not executable"
else
  echo "mkimage: MISSING"
fi

# build.sh updateimg
build="$SDK/build.sh"
if [ -x "$build" ]; then
  echo "build.sh: EXECUTABLE"
  # Check if updateimg target exists
  if grep -q 'updateimg' "$build" 2>/dev/null; then
    echo "  updateimg target: FOUND"
  else
    echo "  updateimg target: NOT FOUND in build.sh"
  fi
else
  echo "build.sh: MISSING or not executable"
fi

echo
echo "=== Output Directory State ==="
for d in "$OUT" "$FW" "$OUT/update/Image"; do
  if [ -d "$d" ]; then
    echo "$d: EXISTS"
  else
    echo "$d: MISSING"
  fi
done

echo
echo "=== amp.img (if exists) ==="
amp="$FW/amp.img"
if [ -f "$amp" ]; then
  sz=$(stat -c%s "$amp" 2>/dev/null || stat -f%z "$amp" 2>/dev/null)
  hash=$(sha256sum "$amp" | cut -d' ' -f1)
  echo "size: ${sz} B"
  echo "sha256: ${hash}"
else
  echo "(not found)"
fi

echo
echo "=== update.img (if exists) ==="
update="$OUT/update/Image/update.img"
if [ -f "$update" ]; then
  sz=$(stat -c%s "$update" 2>/dev/null || stat -f%z "$update" 2>/dev/null)
  hash=$(sha256sum "$update" | cut -d' ' -f1)
  echo "size: ${sz} B"
  echo "sha256: ${hash}"
else
  echo "(not found)"
fi
