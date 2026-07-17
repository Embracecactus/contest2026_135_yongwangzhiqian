#!/usr/bin/env bash
# irq-count-compare.sh — Compare Linux IRQ counters before/after an action.
# Usage: bash irq-count-compare.sh [before_file] [after_file]
# If no files given, reads /proc/interrupts once and shows current state.
# Output: IRQ counter comparison to stdout

set -euo pipefail

show_irqs() {
  local label="$1"
  local file="$2"
  echo "=== $label ==="
  if [ -f "$file" ]; then
    grep -Ei 'mailbox|rpmsg|20d00000|20d30000' "$file" || echo "(no matching IRQ lines)"
  elif [ "$file" = "/proc/interrupts" ]; then
    grep -Ei 'mailbox|rpmsg|20d00000|20d30000' "$file" 2>/dev/null || echo "(no matching IRQ lines)"
  else
    echo "(file not found: $file)"
  fi
}

compare_irqs() {
  local before="$1"
  local after="$2"
  echo
  echo "=== IRQ Counter Changes ==="

  while IFS= read -r line; do
    local irq_name
    irq_name=$(echo "$line" | awk '{print $NF}')
    local before_count after_count

    before_count=$(grep "$irq_name" "$before" 2>/dev/null | head -1 | awk '{for(i=2;i<=NF;i++) if($i ~ /^[0-9]+$/) {print $i; exit}}' || echo "0")
    after_count=$(grep "$irq_name" "$after" 2>/dev/null | head -1 | awk '{for(i=2;i<=NF;i++) if($i ~ /^[0-9]+$/) {print $i; exit}}' || echo "0")

    if [ -n "$before_count" ] && [ -n "$after_count" ]; then
      local diff=$((after_count - before_count))
      if [ "$diff" -gt 0 ]; then
        echo "  $irq_name: $before_count → $after_count (+$diff) ← CHANGED"
      elif [ "$diff" -eq 0 ]; then
        echo "  $irq_name: $before_count → $after_count (no change)"
      fi
    fi
  done < <(grep -Ei 'mailbox|rpmsg|20d00000|20d30000' "$after" 2>/dev/null)
}

if [ $# -eq 2 ]; then
  show_irqs "BEFORE" "$1"
  show_irqs "AFTER" "$2"
  compare_irqs "$1" "$2"
elif [ $# -eq 1 ]; then
  show_irqs "CURRENT" "$1"
else
  show_irqs "CURRENT" "/proc/interrupts"
  echo
  echo "Usage: $0 [before_file] [after_file]"
  echo "  No args: show current /proc/interrupts"
  echo "  1 arg:   show that file"
  echo "  2 args:  compare before/after"
fi
