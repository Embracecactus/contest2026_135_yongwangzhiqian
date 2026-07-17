---
name: board-verify
description: >
  Board-side verification and diagnostics for hardware adaptation.
  Supports multiple connection modes: manual paste, SSH, serial (minicom/screen),
  JLink (JTAG/SWD), and RTT. Provides devmem register reads, IRQ counter comparison,
  channel enumeration, and device node checks. Use when: 板端验证, 检查寄存器,
  验证 channel, board verify, verifying mailbox, checking RPMsg channels,
  连接开发板, 调试手段, jlink, ssh, 串口.
---

# Board Verify

Board-side verification checklist and diagnostic commands for hardware adaptation.

## When to Use

- User says "板端验证" / "检查寄存器" / "验证 channel" / "board verify"
- After flashing new firmware
- When debugging mailbox/IRQ/channel issues

## Prerequisites

- Target board powered on and accessible via serial/Linux shell
- Firmware flashed (via `firmware-gate` or manual flash)

## Connection Modes

Board verification can be done in multiple ways. Ask the user which mode they prefer:

### Mode 1: Manual (Default)

User copies commands to clipboard and pastes results back.

```text
AI 生成命令 → 用户复制到串口/SSH → 用户粘贴结果回来 → AI 判定
```

Best for: one-off verification, first-time bring-up, user prefers manual control.

### Mode 2: SSH

If the target Linux has SSH access, AI can run commands directly:

```bash
# Configuration (user provides once)
export BOARD_SSH="root@<board-ip>"
export BOARD_SSH_OPTS="-o StrictHostKeyChecking=no"

# Example: read registers remotely
ssh $BOARD_SSH_OPTS $BOARD_SSH "for addr in 0x20d30000 0x20d30004; do printf '%s: ' \$addr; devmem \$addr 32; done"
```

Best for: Linux-side checks (dmesg, /proc/interrupts, sysfs), repeated verification.

### Mode 3: Serial (minicom/screen)

If NuttX console is on serial:

```bash
# Capture serial output (background)
minicom -D /dev/ttyUSB0 -b 115200 -C /tmp/nuttx-serial.log &
MINICOM_PID=$!

# Send command via minicom key injection or screen
# (requires tmux Skill or manual paste)

# Stop capture
kill $MINICOM_PID
```

Best for: NuttX shell commands (uname, ps, ls /dev, rpmsgchar).

### Mode 4: JLink (JTAG/SWD)

For register reads via JLink (bypasses OS, reads raw memory):

```bash
# Read mailbox registers via JLink
JLinkExe -device RV1126B -if JTAG -speed 4000 <<'EOF'
connect
mem32 0x20d30000 4
mem32 0x20d00010 2
mem32 0x20d30010 2
exit
EOF
```

Best for: reading registers when NuttX devmem not available, debugging boot failures, verifying hardware state before OS starts.

### Mode 5: JLink RTT (Real-Time Transfer)

For live NuttX console + log output without serial:

```bash
# Start JLink RTT server
JLinkRTTLogger -Device RV1126B -If JTAG -Speed 4000 -RTTChannel 0 /tmp/rtt.log &

# Or use JLink RTT Telnet
telnet localhost 19021
```

Best for: live log capture, debug output, when serial port not available.

### Mode Selection

```text
Which connection mode do you prefer?
1. Manual — I'll give you commands, you paste results back
2. SSH — connect to Linux shell on the board
3. Serial — NuttX console via serial port
4. JLink — raw register reads via JTAG
5. JLink RTT — live NuttX output via RTT
```

## Verification Checklist

### 1. Boot Verification

```bash
# NuttX side
uname -a                           # check build time matches expected
ls -l /dev                         # basic device nodes exist
ps                                 # check for expected threads (rptun, rpmsg)
```

```bash
# Linux side
uname -a                           # Linux kernel version
dmesg | grep -Ei 'mailbox|rpmsg'   # probe messages
```

### 2. Device Node Check

```bash
# NuttX
ls /dev/rptun/<cpuname> && echo "PASS" || echo "FAIL"
ls /dev/rpmsg/<cpuname> && echo "PASS" || echo "FAIL"
```

Pass criteria:
- `/dev/rptun/<cpuname>` exists → RPTUN registered
- `/dev/rpmsg/<cpuname>` exists → RPMsg device created (after Linux handshake)

### 3. Mailbox Register Check

```bash
# NuttX (if devmem available)
devmem 0x20d30000 32    # MBOX7 A2B_INTEN (RX enable)
devmem 0x20d30004 32    # MBOX7 A2B_STATUS
devmem 0x20d30008 32    # MBOX7 A2B_CMD
devmem 0x20d3000c 32    # MBOX7 A2B_DATA
devmem 0x20d00010 32    # MBOX4 B2A_INTEN (TX enable)
devmem 0x20d00014 32    # MBOX4 B2A_STATUS
devmem 0x20d30010 32    # MBOX7 B2A_INTEN (TX enable)
devmem 0x20d30014 32    # MBOX7 B2A_STATUS
```

Or run the diagnostic script:
```bash
bash <skill_dir>/scripts/devmem-read.sh
```

Pass criteria (Linux→HPMCU handshake):
- A2B_INTEN bit0=1 (RX enabled)
- A2B_STATUS bit0=0 (consumed)
- CMD=0x03, DATA=0x524d5347

Pass criteria (HPMCU→Linux TX):
- B2A_INTEN bit8=1 (TRIG_MODE set)
- B2A_STATUS bit0=0 (consumed by Linux) or bit0=1 (pending)

### 4. IRQ Counter Check

```bash
# Linux side
grep -Ei 'mailbox|rpmsg' /proc/interrupts
```

Or compare before/after:
```bash
bash <skill_dir>/scripts/irq-count-compare.sh
```

Pass criteria:
- `20d00000.mailbox` (MBOX4 B2A) count > 0 if HPMCU sent any TX
- `20d30000.mailbox` (MBOX7 B2A) count may be 0 if no vqid1 TX

### 5. Channel Enumeration

```bash
# Linux side
ls -l /sys/bus/rpmsg/devices/
dmesg | grep -Ei 'creating channel|rpmsg-'
```

Or run the enumeration script:
```bash
bash <skill_dir>/scripts/channel-enum.sh
```

Pass criteria:
- At least `virtio0.rpmsg_ctrl.0.0` and `virtio0.rpmsg_ns.53.53` exist
- After HPMCU creates a named endpoint, `virtio0.<name>.*` appears
- dmesg shows `creating channel <name> addr 0x<addr>`

### 6. Data Path (Optional)

```bash
# NuttX: trigger NS announce (requires rpmsgchar or equivalent)
rpmsgchar -c /dev/rpmsg/<cpuname> -n <channel-name>

# Linux: verify channel appears
ls /sys/bus/rpmsg/devices/ | grep <channel-name>
```

## Output Format

```markdown
| # | Check | Result | Evidence |
|---|-------|--------|----------|
| 1 | Boot | PASS/FAIL | uname output |
| 2 | Device nodes | PASS/FAIL | ls output |
| 3 | Registers | PASS/FAIL | register values |
| 4 | IRQ counters | PASS/FAIL | before/after counts |
| 5 | Channel enum | PASS/FAIL | sysfs/dmesg output |
| 6 | Data path | PASS/FAIL/NEEDS_RUNTIME | test output |
```

## Diagnostic Scripts

- `scripts/devmem-read.sh` — Read mailbox registers (NuttX or Linux devmem)
- `scripts/irq-count-compare.sh` — Compare IRQ counters before/after an action
- `scripts/channel-enum.sh` — Enumerate RPMsg channels and devices

## Rules

- Only mark "PASS" for board-observed behavior
- "NEEDS_RUNTIME" for checks that require hardware-specific timing
- Do not modify any registers during verification (read-only)
- Record all evidence for stage-restore-prompt

## References

- `references/rv1126b-board-verify-example.md` — P2-A board verification instance
