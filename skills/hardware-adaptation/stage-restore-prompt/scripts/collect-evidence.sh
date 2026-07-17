#!/usr/bin/env bash
# collect-evidence.sh — Collect build/config/git evidence for stage restore prompts.
# Usage: bash collect-evidence.sh <contest_repo_path> [workspace_path]
# Output: structured evidence to stdout (no files written)

set -euo pipefail

CONTEST="${1:?Usage: collect-evidence.sh <contest_repo_path> [workspace_path]}"
WORKSPACE="${2:-$(cd "$CONTEST/.." && pwd)}"

if [ ! -d "$CONTEST/.git" ]; then
  echo "ERROR: $CONTEST is not a git repository" >&2
  exit 1
fi

echo "=== Git Status ==="
echo "Branch: $(git -C "$CONTEST" branch --show-current)"
echo "Tracking: $(git -C "$CONTEST" rev-parse --abbrev-ref --symbolic-full-name @{u} 2>/dev/null || echo '(none)')"
echo "HEAD: $(git -C "$CONTEST" rev-parse --short HEAD)"
echo
git -C "$CONTEST" status --short --branch
echo
echo "=== .config Key Symbols ==="
if [ -f "$WORKSPACE/nuttx/.config" ]; then
  grep -E '^CONFIG_(RPTUN|RPMSG|RPMSG_VIRTIO|RPMSG_CHAR|BUILTIN|NSH_BUILTIN|DEV_SIMPLE_ADDRENV|BOARD_LATE_INITIALIZE|FS_PROCFS|SYSLOG_DEVPATH)=' \
    "$WORKSPACE/nuttx/.config" 2>/dev/null || echo "(no matching symbols)"
else
  echo "(no .config found)"
fi

echo
echo "=== Build Artifacts ==="
for f in "$WORKSPACE/nuttx/nuttx" "$WORKSPACE/nuttx/nuttx.bin" "$WORKSPACE/nuttx/nuttx.map"; do
  if [ -f "$f" ]; then
    sz=$(stat -c%s "$f" 2>/dev/null || stat -f%z "$f" 2>/dev/null)
    hash=$(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)
    echo "$(basename "$f"): ${sz} B  sha256=${hash}"
  else
    echo "$(basename "$f"): (not found)"
  fi
done

echo
echo "=== Size (text/data/bss) ==="
if command -v size >/dev/null 2>&1 && [ -f "$WORKSPACE/nuttx/nuttx" ]; then
  size "$WORKSPACE/nuttx/nuttx" 2>/dev/null || echo "(size command failed)"
else
  echo "(size not available or nuttx not found)"
fi

echo
echo "=== .linux_rpmsg Section ==="
if [ -f "$WORKSPACE/nuttx/nuttx.map" ]; then
  grep -E '_rpmsg_beg|_rpmsg_end|linux_rpmsg' "$WORKSPACE/nuttx/nuttx.map" 2>/dev/null | head -5 || echo "(not found in map)"
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
