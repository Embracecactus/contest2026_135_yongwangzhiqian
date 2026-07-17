#!/usr/bin/env bash
# ssh-run.sh — Run a command on the target board via SSH and capture output.
# Usage: bash ssh-run.sh <command> [ssh_target] [ssh_opts]
# Env: BOARD_SSH, BOARD_SSH_OPTS (override defaults)
# Output: command output to stdout

set -euo pipefail

CMD="${1:?Usage: ssh-run.sh <command> [ssh_target] [ssh_opts]}"
TARGET="${2:-${BOARD_SSH:-root@192.168.1.100}}"
OPTS="${3:-${BOARD_SSH_OPTS:--o StrictHostKeyChecking=no -o ConnectTimeout=5}}"

# Check SSH connectivity
if ! ssh $OPTS "$TARGET" "echo ok" >/dev/null 2>&1; then
  echo "ERROR: Cannot connect to $TARGET via SSH" >&2
  echo "Check: board IP, SSH enabled, network connectivity" >&2
  exit 1
fi

# Run the command
ssh $OPTS "$TARGET" "$CMD"
