#!/usr/bin/env bash
# collect-evidence.sh — Collect build/config/git evidence for stage restore prompts.
# Multi-board aware: detects which board the workspace is currently built for.
# Usage: bash collect-evidence.sh <contest_repo_path> [workspace_path]
# Output: structured evidence to stdout (no files written)

set -uo pipefail

CONTEST="${1:?Usage: collect-evidence.sh <contest_repo_path> [workspace_path]}"
WORKSPACE="${2:-$(cd "$CONTEST/.." && pwd)}"

if [ ! -d "$CONTEST/.git" ]; then
  echo "ERROR: $CONTEST is not a git repository" >&2
  exit 1
fi

# ---- Detect which board the nuttx build targets (multi-board workspace) ----
# Reads CONFIG_ARCH_CHIP_CUSTOM_NAME / CONFIG_ARCH_BOARD_CUSTOM_NAME from .config.
NUTTX_CONFIG="$WORKSPACE/nuttx/.config"
BOARD_HINT="(unknown)"
if [ -f "$NUTTX_CONFIG" ]; then
  chip=$(grep -E '^CONFIG_ARCH_CHIP_CUSTOM_NAME=' "$NUTTX_CONFIG" 2>/dev/null | sed "s/.*=\"//;s/\"//")
  board=$(grep -E '^CONFIG_ARCH_BOARD_CUSTOM_NAME=' "$NUTTX_CONFIG" 2>/dev/null | sed "s/.*=\"//;s/\"//")
  [ -n "${chip:-}" ] && BOARD_HINT="chip=$chip"
  [ -n "${board:-}" ] && BOARD_HINT="$BOARD_HINT board=$board"
fi

echo "=== Git Status ==="
echo "Branch: $(git -C "$CONTEST" branch --show-current)"
echo "Tracking: $(git -C "$CONTEST" rev-parse --abbrev-ref --symbolic-full-name @{u} 2>/dev/null || echo '(none)')"
echo "HEAD: $(git -C "$CONTEST" rev-parse --short HEAD)"
echo
git -C "$CONTEST" status --short --branch

echo
echo "=== Active Board (from .config) ==="
echo "$BOARD_HINT"
echo "  NOTE: in a multi-board workspace, .config reflects the LAST board built."
echo "  Confirm this matches the board you intend to restore before relying on it."

echo
echo "=== .config Key Symbols ==="
if [ -f "$NUTTX_CONFIG" ]; then
  grep -E '^CONFIG_(RPTUN|RPMSG|RPMSG_VIRTIO|RPMSG_CHAR|BUILTIN|NSH_BUILTIN|DEV_SIMPLE_ADDRENV|BOARD_LATE_INITIALIZE|FS_PROCFS|SYSLOG_DEVPATH|ARCH_CORTEXM|ARCH_RV|ARCH_CHIP)' \
    "$NUTTX_CONFIG" 2>/dev/null | head -30 || echo "(no matching symbols)"
else
  echo "(no .config found — board may not be built yet, e.g. exploration phase)"
fi

echo
echo "=== Build Artifacts ==="
# Detect if any nuttx build artifact exists; if not, this is likely an exploration phase
# for a board that hasn't been built yet (e.g., a second board in the same workspace).
if [ -f "$WORKSPACE/nuttx/nuttx.bin" ]; then
  for f in "$WORKSPACE/nuttx/nuttx" "$WORKSPACE/nuttx/nuttx.bin" "$WORKSPACE/nuttx/nuttx.map"; do
    if [ -f "$f" ]; then
      sz=$(stat -c%s "$f" 2>/dev/null || stat -f%z "$f" 2>/dev/null)
      hash=$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)
      echo "$(basename "$f"): ${sz} B  sha256=${hash}"
    else
      echo "$(basename "$f"): (not found)"
    fi
  done
else
  echo "(no nuttx.bin — current .config/artifacts belong to the board above, not necessarily the restore target)"
fi

echo
echo "=== Size (text/data/bss) ==="
if command -v size >/dev/null 2>&1 && [ -f "$WORKSPACE/nuttx/nuttx" ]; then
  size "$WORKSPACE/nuttx/nuttx" 2>/dev/null || echo "(size command failed)"
else
  echo "(size not available or nuttx not found)"
fi

echo
echo "=== .linux_rpmsg Section (RV1126B-specific) ==="
if [ -f "$WORKSPACE/nuttx/nuttx.map" ]; then
  grep -E '_rpmsg_beg|_rpmsg_end|linux_rpmsg' "$WORKSPACE/nuttx/nuttx.map" 2>/dev/null | head -5 || echo "(not found in map — may be a non-RV1126B board)"
else
  echo "(no nuttx.map)"
fi

echo
echo "=== SDK Artifacts ==="
if [ -n "${SDK:-}" ]; then
  for f in "$SDK/output/rtt.bin" "$SDK/output/firmware/amp.img" "$SDK/output/update/Image/update.img"; do
    if [ -e "$f" ]; then
      real=$(readlink -f "$f" 2>/dev/null || echo "$f")
      sz=$(stat -c%s "$real" 2>/dev/null || stat -f%z "$real" 2>/dev/null)
      hash=$(sha256sum "$real" 2>/dev/null | cut -d' ' -f1)
      echo "$(basename "$f"): ${sz} B  sha256=${hash}  (real: $real)"
    else
      echo "$(basename "$f"): (not found)"
    fi
  done
else
  echo "(SDK not set — skipping)"
fi

echo
echo "=== diff --check ==="
git -C "$CONTEST" diff --check 2>/dev/null && echo "(clean)" || echo "(has issues)"

echo
echo "=== diff --stat ==="
git -C "$CONTEST" diff --stat 2>/dev/null || echo "(no changes)"
