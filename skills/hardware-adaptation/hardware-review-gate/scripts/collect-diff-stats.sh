#!/usr/bin/env bash
# collect-diff-stats.sh — Collect git diff statistics for review scope.
# Usage: bash collect-diff-stats.sh <repo_path>
# Output: structured diff stats to stdout

set -euo pipefail

REPO="${1:?Usage: collect-diff-stats.sh <repo_path>}"

if [ ! -d "$REPO/.git" ]; then
  echo "ERROR: $REPO is not a git repository" >&2
  exit 1
fi

echo "=== Branch & HEAD ==="
echo "Branch: $(git -C "$REPO" branch --show-current)"
echo "HEAD: $(git -C "$REPO" log -1 --format='%h %s')"
echo

echo "=== Tracked Modified ==="
git -C "$REPO" diff --stat
echo

echo "=== Untracked Files ==="
git -C "$REPO" status --porcelain=v1 | grep '^??' | awk '{print $2}'
echo

echo "=== diff --check ==="
git -C "$REPO" diff --check 2>/dev/null && echo "(clean)" || echo "(has whitespace issues)"
echo

echo "=== Changed File Categories ==="
MODIFIED=$(git -C "$REPO" diff --name-only 2>/dev/null)
UNTRACKED=$(git -C "$REPO" status --porcelain=v1 | grep '^??' | awk '{print $2}')

echo "Modified files:"
echo "$MODIFIED" | sed 's/^/  /'
echo
echo "New files:"
echo "$UNTRACKED" | sed 's/^/  /'
echo

echo "=== Key File Types ==="
echo "$MODIFIED $UNTRACKED" | tr ' ' '\n' | grep -E '\.(c|h|S|ld|config|defconfig|Kconfig|Makefile|Make\.defs|CMakeLists\.txt)$' | sort | sed 's/^/  /' || echo "(none)"
