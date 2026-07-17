#!/usr/bin/env bash
# diff-versions.sh — Verify SDK file version and show differences if any.
# Usage: bash diff-versions.sh <sdk_path> <filename> [expected_hash]
# Output: version verification results to stdout

set -euo pipefail

SDK="${1:?Usage: diff-versions.sh <sdk_path> <filename> [expected_hash]}"
FILENAME="${2:?Usage: diff-versions.sh <sdk_path> <filename> [expected_hash]}"
EXPECTED_HASH="${3:-}"

# Find the file in SDK
FOUND=$(find "$SDK" -name "$FILENAME" -type f 2>/dev/null | head -5)

if [ -z "$FOUND" ]; then
  echo "ERROR: File not found in SDK: $FILENAME"
  echo "SDK path: $SDK"
  exit 1
fi

echo "=== File Version Verification ==="
echo "File: $FILENAME"
echo "SDK: $SDK"
echo

while IFS= read -r f; do
  REL="${f#$SDK/}"
  SZ=$(stat -c%s "$f" 2>/dev/null || stat -f%z "$f" 2>/dev/null)
  HASH=$(sha256sum "$f" | cut -d' ' -f1)
  MTIME=$(stat -c%Y "$f" 2>/dev/null || stat -f%m "$f" 2>/dev/null)

  echo "Path: $REL"
  echo "Size: $SZ B"
  echo "SHA-256: $HASH"
  echo "Modified: $(date -d "@$MTIME" 2>/dev/null || date -r "$MTIME" 2>/dev/null || echo "$MTIME")"

  if [ -n "$EXPECTED_HASH" ]; then
    if [ "$HASH" = "$EXPECTED_HASH" ]; then
      echo "Status: MATCH (expected hash)"
    else
      echo "Status: MISMATCH"
      echo "  Expected: $EXPECTED_HASH"
      echo "  Actual:   $HASH"
    fi
  else
    echo "Status: (no expected hash provided)"
  fi
  echo
done <<< "$FOUND"

if [ "$(echo "$FOUND" | wc -l)" -gt 1 ]; then
  echo "WARNING: Multiple files found with name '$FILENAME'"
  echo "Using first match. All matches:"
  echo "$FOUND" | sed 's/^/  /'
fi
