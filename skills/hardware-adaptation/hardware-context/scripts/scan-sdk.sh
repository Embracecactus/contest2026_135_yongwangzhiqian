#!/usr/bin/env bash
# scan-sdk.sh — Scan SDK directory and generate a structured index of reference materials.
# Usage: bash scan-sdk.sh <sdk_path>
# Output: Markdown index to stdout

set -euo pipefail

SDK="${1:?Usage: scan-sdk.sh <sdk_path>}"

if [ ! -d "$SDK" ]; then
  echo "ERROR: SDK directory not found: $SDK" >&2
  exit 1
fi

echo "# Hardware Context Index — $(basename "$SDK")"
echo
echo "Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "SDK path: $SDK"
echo

# CMSIS Headers
echo "## CMSIS Headers"
echo
echo "| File | Key Defines | Path |"
echo "|------|-------------|------|"

find "$SDK/hal/lib/CMSIS" -name "*.h" -type f 2>/dev/null | while read -r f; do
  rel="${f#$SDK/}"
  # Extract key defines (BASE addresses, IRQ numbers)
  defines=$(grep -oE '#define\s+[A-Z_]*(BASE|IRQ|INT)[A-Z_]*\s+0x[0-9A-Fa-f]+' "$f" 2>/dev/null | head -5 | sed 's/#define\s*//' | tr '\n' ', ' | sed 's/,$//')
  if [ -n "$defines" ]; then
    echo "| $(basename "$f") | $defines | $rel |"
  fi
done

echo

# HAL Examples
echo "## HAL Examples"
echo
echo "| File | Key Functions | Path |"
echo "|------|---------------|------|"

find "$SDK/hal/lib/hal/src" -name "*.c" -type f 2>/dev/null | while read -r f; do
  rel="${f#$SDK/}"
  # Extract function signatures
  funcs=$(grep -oE '[a-zA-Z_][a-zA-Z0-9_]*\s*\(' "$f" 2>/dev/null | grep -v '^if\|^for\|^while\|^switch\|^return' | head -5 | sed 's/($//' | tr '\n' ', ' | sed 's/,$//')
  if [ -n "$funcs" ]; then
    echo "| $(basename "$f") | $funcs | $rel |"
  fi
done

echo

# Linux DTS
echo "## Linux DTS"
echo
echo "| File | Key Nodes | Path |"
echo "|------|-----------|------|"

find "$SDK/kernel-6.1/arch/arm/boot/dts" -name "*.dtsi" -o -name "*.dts" 2>/dev/null | while read -r f; do
  rel="${f#$SDK/}"
  # Extract device nodes
  nodes=$(grep -oE '[a-z]+@[0-9a-f]+' "$f" 2>/dev/null | head -5 | tr '\n' ', ' | sed 's/,$//')
  if [ -n "$nodes" ]; then
    echo "| $(basename "$f") | $nodes | $rel |"
  fi
done

echo

# Linux Drivers
echo "## Linux Drivers"
echo
echo "| File | Key Functions | Path |"
echo "|------|---------------|------|"

for dir in mailbox rpmsg virtio remoteproc; do
  find "$SDK/kernel-6.1/drivers/$dir" -name "*.c" -type f 2>/dev/null | while read -r f; do
    rel="${f#$SDK/}"
    # Extract probe/init/send functions
    funcs=$(grep -oE '(probe|init|send|receive|callback|start|stop)\s*\(' "$f" 2>/dev/null | sed 's/($//' | sort -u | head -5 | tr '\n' ', ' | sed 's/,$//')
    if [ -n "$funcs" ]; then
      echo "| $(basename "$f") | $funcs | $rel |"
    fi
  done
done

echo

# RTOS References
echo "## RTOS References"
echo
echo "| File | Key Functions | Path |"
echo "|------|---------------|------|"

find "$SDK/rtos" -name "*.c" -type f 2>/dev/null | while read -r f; do
  rel="${f#$SDK/}"
  # Extract init/task functions
  funcs=$(grep -oE '[a-zA-Z_][a-zA-Z0-9_]*\s*\(' "$f" 2>/dev/null | grep -iE 'init|task|create|start|send|recv' | head -5 | sed 's/($//' | tr '\n' ', ' | sed 's/,$//')
  if [ -n "$funcs" ]; then
    echo "| $(basename "$f") | $funcs | $rel |"
  fi
done

echo

# Documentation
echo "## Documentation"
echo
echo "| File | Description | Path |"
echo "|------|-------------|------|"

find "$SDK" -maxdepth 3 -name "*.md" -o -name "*.txt" -o -name "*.rst" 2>/dev/null | while read -r f; do
  rel="${f#$SDK/}"
  # Get first line as description
  desc=$(head -1 "$f" 2>/dev/null | sed 's/^#\s*//' | cut -c1-50)
  echo "| $(basename "$f") | $desc | $rel |"
done
