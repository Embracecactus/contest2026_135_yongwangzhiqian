#!/usr/bin/env bash
# scan-sdk.sh — Scan SDK directory and generate a structured index of reference materials.
# Generic: auto-detects SDK layout (RV1126B ATK Linux vs ARMINO embedded vs generic).
# Usage: bash scan-sdk.sh <sdk_path>
# Output: Markdown index to stdout

set -uo pipefail

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

# ---- Detect SDK layout ----
# Returns a label so the index notes which structure was used.
detect_layout() {
  if [ -d "$SDK/hal/lib/CMSIS" ]; then
    echo "rv1126b-atk-linux"
  elif [ -d "$SDK/ap" ] && [ -d "$SDK/cp" ]; then
    echo "armino-embedded"
  elif [ -d "$SDK/kernel" ]; then
    echo "linux-kernel"
  else
    echo "generic"
  fi
}

LAYOUT=$(detect_layout)
echo "> Detected layout: **$LAYOUT**"
echo

# ---- Section: Register headers / CMSIS ----
echo "## Register / CMSIS Headers"
echo
echo "| File | Key Defines | Path |"
echo "|------|-------------|------|"

# RV1126B: hal/lib/CMSIS
if [ -d "$SDK/hal/lib/CMSIS" ]; then
  find "$SDK/hal/lib/CMSIS" -name "*.h" -type f 2>/dev/null | while read -r f; do
    rel="${f#$SDK/}"
    defines=$(grep -oE '#define\s+[A-Z_]*(BASE|IRQ|INT|_IRQn)[A-Z_0-9]*\s+0x[0-9A-Fa-f]+' "$f" 2>/dev/null | head -5 | sed 's/#define\s*//' | tr '\n' ',' | sed 's/,$//') || true
    [ -n "$defines" ] && echo "| $(basename "$f") | $defines | $rel |"
  done
fi

# ARMINO: register headers are in middleware/soc/<chip>/soc/*_reg.h + include/soc/<chip>/reg_base.h
if [ "$LAYOUT" = "armino-embedded" ]; then
  # reg_base.h + *_reg.h (prefer chip-named dirs; skip empty stub headers). Use [[:space:]] for portability.
  find "$SDK" -type f \( -name "reg_base.h" -o -name "*_reg.h" \) 2>/dev/null | grep -v build | while read -r f; do
    rel="${f#$SDK/}"
    # ARMINO defines: #define SOC_XXX_BASE   (0x44010000 + SOC_ADDR_OFFSET). Extract NAME + first 0x.
    defines=$(grep -E "#define[[:space:]]+[A-Z_]*BASE.*0x" "$f" 2>/dev/null | grep -oE "[A-Z_][A-Z_0-9]*_BASE|0x[0-9A-Fa-f]{6,}" | paste - - | head -6 | sed 's/\t/=/g' | tr '\n' ',' | sed 's/,$//') || true
    [ -n "$defines" ] && echo "| $(basename "$f") | $defines | $rel |"
  done
  # Interrupt number headers
  find "$SDK" -type f -name "*_irq.h" 2>/dev/null | grep -v build | while read -r f; do
    rel="${f#$SDK/}"
    defines=$(grep -oE "#define[[:space:]]+[A-Z_]*(IRQn|_IRQ)[A-Z_0-9]*[[:space:]]+[0-9]+" "$f" 2>/dev/null | head -6 | sed 's/#define[[:space:]]*//' | tr '\n' ',' | sed 's/,$//') || true
    [ -n "$defines" ] && echo "| $(basename "$f") | $defines | $rel |"
  done
fi

echo

# ---- Section: Startup / System Init ----
echo "## Startup / System Init"
echo
echo "| File | Key Functions | Path |"
echo "|------|---------------|------|"

# Common startup filenames
find "$SDK" -type f \( -name "system_main.c" -o -name "startup*.c" -o -name "startup*.S" -o -name "*_cpustart.c" \) 2>/dev/null | grep -v build | while read -r f; do
  rel="${f#$SDK/}"
  funcs=$(grep -oE '\b(reset_handler|Reset_Handler|SystemInit[a-zA-Z0-9_]*|_start|entry_main|cpu[0-9]_boot|multicore_launch[a-zA-Z0-9_]*|start_cpu[0-9]_core)\b' "$f" 2>/dev/null | sort -u | head -5 | tr '\n' ',' | sed 's/,$//') || true
  echo "| $(basename "$f") | ${funcs:-(scan)} | $rel |"
done

echo

# ---- Section: Linker Scripts / Memory Layout ----
echo "## Linker Scripts / Memory Layout"
echo
echo "| File | Path |"
echo "|------|------|"

find "$SDK" -type f -name "*.ld" 2>/dev/null | grep -vE 'build|\.repo' | while read -r f; do
  rel="${f#$SDK/}"
  echo "| $(basename "$f") | $rel |"
done

echo

# ---- Section: Drivers / Middleware ----
echo "## Drivers / Middleware"
echo
echo "| Driver | Key Files | Path |"
echo "|--------|-----------|------|"

# ARMINO: middleware/driver/<name>/
if [ "$LAYOUT" = "armino-embedded" ]; then
  for side in ap cp; do
    [ -d "$SDK/$side/middleware/driver" ] || continue
    for d in "$SDK/$side/middleware/driver"/*/; do
      [ -d "$d" ] || continue
      name=$(basename "$d")
      rel="${d#$SDK/}"
      files=$(find "$d" -maxdepth 1 -name "*.c" -printf "%f\n" 2>/dev/null | head -3 | tr '\n' ',' | sed 's/,$//')
      echo "| $name ($side) | ${files:-(dir)} | $rel |"
    done
  done
fi

# RV1126B: hal/lib/hal/src
if [ -d "$SDK/hal/lib/hal/src" ]; then
  find "$SDK/hal/lib/hal/src" -name "hal_*.c" 2>/dev/null | while read -r f; do
    rel="${f#$SDK/}"
    funcs=$(grep -oE '\bHAL_[A-Z]+\w*\(' "$f" 2>/dev/null | tr -d '(' | sort -u | head -5 | tr '\n' ',' | sed 's/,$//') || true
    echo "| $(basename "$f") | ${funcs:-(scan)} | $rel |"
  done
fi

echo

# ---- Section: Linux DTS / Drivers (if present) ----
if [ -d "$SDK/kernel" ] || [ -n "$(find "$SDK" -maxdepth 3 -name '*.dtsi' 2>/dev/null | head -1)" ]; then
  echo "## Linux DTS / Drivers"
  echo
  echo "| File | Key Nodes/Functions | Path |"
  echo "|------|---------------------|------|"
  find "$SDK" -type f \( -name "*.dtsi" -o -name "*.dts" \) 2>/dev/null | grep -v build | head -20 | while read -r f; do
    rel="${f#$SDK/}"
    nodes=$(grep -oE '[a-z]+@[0-9a-f]+' "$f" 2>/dev/null | head -5 | tr '\n' ',' | sed 's/,$//') || true
    [ -n "$nodes" ] && echo "| $(basename "$f") | $nodes | $rel |"
  done
  echo
fi

# ---- Section: Bootloader ----
echo "## Bootloader"
echo
echo "| File | Size | Path |"
echo "|------|------|------|"
find "$SDK" -type f -name "bootloader*.bin" 2>/dev/null | grep -v build | while read -r f; do
  rel="${f#$SDK/}"
  sz=$(stat -c%s "$f" 2>/dev/null || stat -f%z "$f" 2>/dev/null)
  echo "| $(basename "$f") | $sz | $rel |"
done

echo

# ---- Section: Documentation ----
echo "## Documentation"
echo
echo "| File | Description | Path |"
echo "|------|-------------|------|"
find "$SDK" -maxdepth 3 -type f \( -name "*.md" -o -name "*.txt" -o -name "*.rst" \) 2>/dev/null | grep -vE 'build|\.repo|node_modules' | head -20 | while read -r f; do
  rel="${f#$SDK/}"
  desc=$(head -1 "$f" 2>/dev/null | sed 's/^#\s*//' | cut -c1-50)
  echo "| $(basename "$f") | $desc | $rel |"
done
