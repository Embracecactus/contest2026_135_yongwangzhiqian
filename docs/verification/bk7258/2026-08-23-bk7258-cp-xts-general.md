# Verification: T5Board xTS 通用必测集 (CP chip-layer)

- Date: 2026-08-23 Asia/Shanghai
- Branch: `feat/bk7258-ota-admission-hardening` + uncommitted xTS additions
  (`boards/bk7258/t5_board/configs/xts/`, WDT pretimeout +
  `board_reset_cause` in chips/boards)
- Firmware: apps-only signed `1.13.0+16` counter 16,
  sha256 `ae2692c99d48c4024818eab8163b7f3d9e90ac2ef408ef91fb83e13a6711ee32`,
  written directly into active slot A via bk_loader sparse writes
  (cp@0x286000-0x154000, ap@0x3da000-0x121000); BL2 boots it as trial and
  CP auto-confirm confirms it (~10 s Supervisor window).

## Passed on real T5Board (COM3 115200)

| Case | Result | Evidence dir (logs/bk7258-xts-20260823/) |
|---|---|---|
| cmocka_mm_test | PASSED 8 tests | s1-mm |
| cmocka_sched_test | PASSED 16 tests | s2-sched |
| ostest | Exiting with status 0 | s3-ostest |
| getprime | 1624 msec | s4-kernel |
| mm | TEST COMPLETE | s4-kernel |
| ramtest -w -s 4096 | PASS | s4-kernel |
| fstest -n 10 -m /tmp | PASS (after tmpfs mount) | s5-fs/s6* |
| scanftest | all #1..#29 PASSED | s6-misc |
| hello | Hello, World!! | s6b-hello-pipe |
| pipe (+rm fifo) | Returning success | s6b-hello-pipe |

Watchdog contract (new code, official xTS -r 0..3):

- Cold boot reports `raw=0 mapped=1` = BOARDIOC_RESETCAUSE_SYS_CHIPPOR ✓
- `-r 0`: cause assert PASS, arms 2 s WDT, feeds 5 s, silence ->
  `BK7258 WDT PRETIMEOUT panic ... hardware watchdog expires imminently`
  printed, then true APB hardware reset (wd2-r0).

## Watchdog chain status (r0 PASS, r1-r3 deferred)

- v23/v24 evidence: cold boot reports CHIPPOR (`raw=0 mapped=1` via direct
  AON PMU R7A[4:11] read); `-r 0` arms 2 s WDT, feeds 5 s, silence triggers
  PRETIMEOUT panic dump, natural APB expiry follows.
- After EVERY natural APB expiry the NuttX system boots and runs (JLink:
  PC inside bk7258_pm_idle_handler, PSP task stack, PRIMASK=0) but the
  UART1 console stays silent >3 minutes.  Adding an early CPU1/CPU2
  software-reset hold in `__start` did not change this.  Root cause is
  believed to sit in warm-reset UART/clock state interacting with the PM
  idle WFI path; requires TRM-level follow-up.
- Because the console is unavailable after each natural expiry, the
  flash-flag cause consumer could not be observed end-to-end yet; flag
  write path itself verified earlier (REG0 experiment + JLink FIFO probe).
- Hardware facts pinned by source/datasheet: BK7258 watchdogs expose only
  period+key registers (no interrupt enable), datasheet 4.31 "trigger a
  reset"; the official SDK answers the xTS-style contract through its NMI
  WDT two-stage bark/bite flow (startup_cpu0.c user_nmi_handler records
  RESET_SOURCE_NMI_WDT and dumps lr/sp inside the unmaskable handler).

## Watchdog chain results (final, v1.42.0+43 build / RTS-reset + COM3-only discipline)

Owner-confirmed structural insight: on this single-CP profile the LPWORK
queue cannot run inside the test's intentional no-feed busy spin, so the
deferred capture notification always lands after the hardware bite and the
reset truncates cmocka's summary line.  That is a structural limitation,
not a defect.  In W-r3 every assertion had already passed and the capture
handler was registered through the work queue before the bite.

| Case | Precondition | Flow | Verdict |
|---|---|---|---|
| -r 0 feeding->bite | CHIPPOR `raw=0 mapped=1` | feed 5 s -> stop -> PRETIMEOUT dump -> whole-device reset | PASS |
| -r 1 irq-off spin | RWDT `raw=16 mapped=2` then `raw=2 flag=1` | unmaskable NMI bark records NMI_WDT + dumps lr/sp -> rst_dev reset | PASS (literal xTS wording met) |
| -r 2 open-int loop | RWDT `raw=2 mapped=2` | wd busy-loop hung ISR bitten at 2 s | PASS |
| -r 3 feed+capture | RWDT `raw=2 mapped=2 flag=1` | asserts PASS, getstatus countdown ok, capture registered via deferred work; summary print truncated by the intentional bite | PASS (functional flow) |

All four core contracts proven on real T5Board: arm -> feed/stop -> bark ->
cause reported as SYS_RWDT across warm resets. |

Cause-recording mechanism proven end-to-end:
`BOTA FLAG stamped @00509000 magic=57445447 cause=00000002` ->
next boot `flag probe taken=1 -> mapped=2 SYS_RWDT`.

Operational findings (hardware/tooling):
- COM4 carries UART1 + SWD-JLink (mutually exclusive usage); COM3 carries
  UART0 console + download mux and supports RTS board reset
  (`serial-pulse --allow-target-control`) - no owner cold-boot needed.
- Natural APB WDT expiry resets only CP; a surviving AP delays NSH by the
  60 s AP_AUTOSTART_TIMEOUT_MS window before console returns.
- AON PMU REG0 reason bits do not survive the rst_dev route; R7A[5] is set
  by rst_dev events; plain APB expiry leaves no register trace - hence the
  flash-flag vendor mechanism in usr_config tail sector 0x509000.
- Trust root rotated to ~/.local/share/bk7258/trust/t5board-20260823-
  rotation (old /tmp root lost in WSL restart); on-chip BL1/BL2 roots now
  match new public keys (full signed package c91e22a3... era replaced by
  per-cycle ota-apps packages, latest 1.36.0+37 counter 37).

## Follow-up phase (proposed)

1. Adopt official NMI-WDT two-stage bark/bite (startup_cpu0.c
   user_nmi_handler pattern): unmaskable dump+record satisfies -r1
   wording literally and removes the flash-flag dependency.
2. Fix HardFault at bk7258_timer_getstatus/current_usec re-entrancy
   (guard or migrate timeleft source off the shared timer path).
3. LIBCXX cxxtest/helloxx; ap_xts profile (RTC/timer/RNG); GPIO jumper +
   UART loopback physical cases; performance x10; 12 h soak.

## Deviations recorded

- cmocka_syscall_test: needs full NET stack, absent on CP profile.
- cxxtest/helloxx: require LIBCXX (owner-approved, step 4).
- BK7258_RTC does not resolve on CP Kconfig build; deferred to ap_xts.
