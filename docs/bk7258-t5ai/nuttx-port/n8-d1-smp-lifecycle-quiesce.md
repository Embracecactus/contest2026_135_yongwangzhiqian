# BK7258 N8-D1 -- SMP scheduler quiesce/resume foundation

Date: 2026-07-30

Status: `board-verified` (2026-07-30; bounded CPU1 scheduler quiesce/resume completed with exact +1/+0/+1 attribution)

## 1. Scope

N8-D1 dispatches exactly one asynchronous NuttX SMP callback from AP logical
CPU0 to logical CPU1.  The callback verifies CPU1 execution, publishes a
quiesced handshake, remains in a bounded `wfe` loop, then resumes when CPU0
publishes the resume sequence and executes `sev`.

This is a scheduler-quiesce foundation only:

- CPU1 remains present in NuttX's online mask (`0x3`).
- CPU1 interrupts are not explicitly disabled.
- Physical CPU2 is not halted, reset, powered down, or rebooted.
- `bk7258_ap_smp_secondary_stop()` must still return `-ENOTSUP` while the
  scheduler-online configuration is active.
- No generic NuttX CPU hot-unplug capability is claimed.

## 2. Expected scheduler attribution

From immediately before the asynchronous callback through callback return:

- CPU0->CPU1 tx/rx: +1
- CPU1->CPU0 tx/rx: +0
- Total `smp_call_requests`: +1
- AP SMP online mask remains `0x3`
- CPU2 state remains `SCHEDULER_ONLINE`

## 3. Record layout

Uses the shared generic `bk7258_ap_advanced_state_s` (32 words) at
shared-page offset `0x680`.  Magic `0x59434c42` ("BLCY"), version 1.

- `requested/completed` = 1/1
- `task_cpu[0]` = callback entry CPU (1)
- `task_cpu[1]` = callback exit CPU (1)
- `task_started[0]` = callback entered (1)
- `task_completed[0]` = callback exited (1)
- `sequence[0]` = quiesce publication sequence (1)
- `sequence[1]` = resume request sequence (1)
- `value[0]` = callback return value (0)
- `value[1]` = scheduler-online secondary-stop result (`-ENOTSUP`)
- `aux[0]` = quiesce publication observed (1)
- `aux[1]` = resume observed by CPU1 (1)

## 4. Config

`BK7258_AP_SMP_LIFECYCLE_QUIESCE` depends on the N8-C4 baseline through the
mutually exclusive advanced-stage choice.  `CONFIG_SMP_DEFAULT_CPUSET`
remains `0x1`.

Defconfig: `configs/ap_smp_lifecycle/defconfig`.

## 5. Static verification notes

- Field count remains 32 four-byte words (`0x80` bytes).
- BLCY starts at `0x680` and ends at `0x700`, inside the reserved shared page.
- The callback uses a static `smp_call_data_s`, so its storage remains valid
  throughout the asynchronous call.
- Abort paths publish terminal failure and execute `sev` so a callback already
  waiting in `wfe` is not intentionally stranded.
- The lifecycle option is mutually exclusive with N8-C5 through N8-C8.

## 6. Board acceptance criteria

The accepted image was built with `AP_CONFIG_NAME=ap_smp_lifecycle` and
required:

- AP `READY`, error 0;
- BLCY `PASSED`, error 0, requested/completed `1/1`;
- callback entry/exit CPU `1/1`, started/completed `1/1`;
- quiesce/resume sequences `1/1`, values `0/-ENOTSUP`, aux `1/1`;
- CPU0->CPU1 `+1`, CPU1->CPU0 `+0`, calls `+1`;
- CPU2 remains `SCHEDULER_ONLINE` and online mask remains `0x3`;
- zero coalesced/send-failure/stale/spurious counts.

## 7. Final board verification

The `ap_smp_lifecycle` image passed on the real T5-AI normal autostart path:

- AP reached `READY(2)`, error 0, generation 1, heartbeat 727;
- CPU2 remained `SCHEDULER_ONLINE(8)`, error 0, ready 1, online `0x3`;
- affinity, BSEM, and BSWL all remained `PASSED`, including BSWL `8/8`;
- BLCY reached `PASSED(3)`, error 0, requested/completed `1/1`;
- callback entry/exit CPU was `1/1`, started/completed `1/1`, and
  quiesce/resume sequence was `1/1`;
- `value[0]/value[1]` was `0/-138`; `-138` is this NuttX configuration's
  `-ENOTSUP`, and BLCY's passing stop-gate check proves the exact match;
- `aux[0]/aux[1]` was `1/1`;
- CPU0->CPU1 tx/rx changed `10->11` (`+1`), CPU1->CPU0 remained `1->1`
  (`+0`), and calls changed `11->12` (`+1`);
- handler call/delivered closed at CPU0 `1/1` and CPU1 `11/11`, with zero
  coalesced, send-failure, stale, or spurious counts.

Post-gate liveness was already substantial in the retained sample: AP
heartbeat 727, CPU0 SysTick 8090, and sleep enter/return `727/726`, while CPU1
SysTick remained zero as designed.  CPU1 stayed NuttX-online throughout and no
CPU2 reset or power transition occurred.  This closes N8-D1 as a bounded
scheduler quiesce/resume foundation, not CPU hot-unplug.

## 8. Review status and limitations

- No new code review was performed; review remains separate and requires fresh
  explicit authorization.
- This stage does not implement runtime CPU offline/online.
- A future CP/AP OpenAMP/RPTUN layer can coordinate whole-AP services and
  lifecycle, but it does not replace AP-internal NuttX SMP CPU hotplug support.
- `CONFIG_SMP_DEFAULT_CPUSET` remains `0x1` and official NuttX remains
  unchanged.
