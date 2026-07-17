#!/usr/bin/env bash
# query-context.sh — Query the hardware context index for references related to a topic.
# Usage: bash query-context.sh <index_file> <keyword>
# Output: matching entries to stdout

set -euo pipefail

INDEX="${1:?Usage: query-context.sh <index_file> <keyword>}"
KEYWORD="${2:?Usage: query-context.sh <index_file> <keyword>}"

if [ ! -f "$INDEX" ]; then
  echo "ERROR: Index file not found: $INDEX" >&2
  echo "Run scan-sdk.sh first to generate the index." >&2
  exit 1
fi

echo "=== Query: '$KEYWORD' ==="
echo

# Search for keyword in all sections
FOUND=0
CURRENT_SECTION=""

while IFS= read -r line; do
  # Track section headers
  if [[ "$line" =~ ^##\  ]]; then
    CURRENT_SECTION="$line"
    continue
  fi

  # Skip non-table lines
  if [[ ! "$line" =~ ^\| ]]; then
    continue
  fi

  # Check if line contains keyword (case-insensitive)
  if echo "$line" | grep -qi "$KEYWORD"; then
    if [ "$FOUND" -eq 0 ]; then
      echo "Found references:"
      echo
    fi
    echo "$CURRENT_SECTION"
    echo "$line"
    echo
    FOUND=$((FOUND + 1))
  fi
done < "$INDEX"

if [ "$FOUND" -eq 0 ]; then
  echo "No references found for '$KEYWORD'"
  echo
  echo "Try:"
  echo "  - Broader keyword (e.g., 'mbox' instead of 'mailbox')"
  echo "  - Different keyword (e.g., 'rpmsg' instead of 'rpmsgchar')"
  echo "  - Check SDK path: $(head -3 "$INDEX" | grep 'SDK path' | cut -d: -f2-)"
fi

echo
echo "=== Summary: $FOUND reference(s) found ==="
