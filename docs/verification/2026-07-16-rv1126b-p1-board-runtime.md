# RV1126B P1 Runtime Verification

> **历史 RV1126B 证据：**当前参赛主线为 BK7258；本记录只保留当日构建或板测事实，
> 不代表当前实现、活动配置或下一步计划。

Date: 2026-07-16
Board: rv1126b_evb (HPMCU core)
Candidate: P1 convergence (submit-rv1126b-nsh-baseline)

## Firmware Identity

| Item | Value |
|---|---|
| `nuttx.bin` sha256 | `26f4ae0ea2fc4398054c814bd2d79304c62ba1b530ec992835af9133ce9dbf00` |
| `amp.img` sha256 | `585602012d9af4d3ba8980b7922d7a3273a4b1edcae212475b180b0f54b425e9` |
| `amp.img` size | 103936 bytes (101.50 KiB) |
| FIT hash algo | sha256 |
| FIT loadable | hpmcu |

## Flash Method

- Tool: RKDevTool.exe (Windows GUI)
- Partition: AMP only (not full `update.img`)
- No command-line transcript available (GUI tool).

## Serial Configuration

- UART5 M0
- 1500000 baud, 8N1

## Boot Transcript

```text
WARNING: GPLL not locked

NuttShell (NSH)
nsh> ps
  PID GROUP PRI POLICY   TYPE    NPX STATE    EVENT     SIGMASK            STACK COMMAND
    0     0   0 FIFO     Kthread   - Ready              0000000000000000 0002016 CPU0 IDLE
    4     4 100 FIFO     Task      - Running            0000000000000000 0004032 nsh_main
nsh> uname -r
0.0.0
nsh> a
nsh: a: command not found
nsh> uname -a
NuttX 0.0.0 e02f581e23 Jul 16 2026 02:59:57 risc-v rv1126b_evb
```

## Verification Summary

| Check | Result |
|---|---|
| GPLL warning | Present as expected (`WARNING: GPLL not locked`) — clock config keep-alive path working |
| NSH prompt | Responsive |
| `ps` | IDLE kthread (PID 0) + nsh_main (PID 4) running — normal |
| `uname -a` | `NuttX 0.0.0 e02f581e23 Jul 16 2026 02:59:57 risc-v rv1126b_evb` |
| UART RX/TX | Interactive input/output confirmed (`a` → `command not found`) |

## Notes

- `uname -r` reports `0.0.0` because the NuttX version string defaults to `0.0.0` when no
  `CONFIG_VERSION_STRING` is set. This is the upstream NuttX default, not a build error.
- The `e02f581e23` kernel version hash in `uname -a` is derived from the NuttX build, not from
  the contest repo commit.
- GPLL warning is expected behavior: the BSP warns but does not PANIC, preserving existing
  register state and continuing boot.
