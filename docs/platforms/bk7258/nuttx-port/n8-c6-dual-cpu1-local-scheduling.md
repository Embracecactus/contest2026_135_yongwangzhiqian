# BK7258 N8-C6 -- Dual CPU1 local scheduling

Date: 2026-07-30

Status: `board-verified` (2026-07-30; local-yield correction completed exact `+3/+0/+3`)

## 1. Scope

N8-C6 creates two detached tasks both pinned to CPU1 (mask 0x2) with two
zero-count semaphores:

- Task A: executes 8 times.  CPU0 posts it once to start; then A posts B
  each round.  After B posts back (rounds 1..7), A blocks again.
- Task B: executes 8 times.  Blocks initially; posted by A each round.
  B posts A back only in rounds 1..7 (no post in round 8) so no waiter
  is stranded at completion.

## 2. Expected scheduler attribution

From before task creation through both PID releases:

- CPU0->CPU1 tx/rx: +3 (2 initial remote dispatches + 1 starter wake)
- CPU1->CPU0 tx/rx: +0
- Total smp_call_requests: +3
- Failures/coalesced/stale/spurious: 0

## 3. Record layout

Uses the shared generic `bk7258_ap_advanced_state_s` (32 words) at
shared-page offset 0x500.  Magic `0x4c554442` ("BDUL"), version 1.

task_id[0]/task_cpu[0] = task A; task_id[1]/task_cpu[1] = task B.
sequence[0] = A's last round (8), sequence[1] = B's last round (8).
aux[0] = PID-released A, aux[1] = PID-released B.

## 4. Config

`BK7258_AP_SMP_CPU1_DUALTASK` depends on N8-C4; mutually exclusive
with other N8-C5..D1 choices.  `CONFIG_SMP_DEFAULT_CPUSET` remains 0x1.

Defconfig: `configs/ap_smp_dualtask/defconfig`.

## 5. Static verification notes

- Field count: 32 words, static_assert verified.
- Non-overlap: contiguous with BP2P and BMIG records.
- Format correctness: all PRIu32/PRId32 match field types.
- One config per defconfig: only `BK7258_AP_SMP_CPU1_DUALTASK=y` added.

## 6. First board attempt and evidence-driven correction

The first `ap_smp_dualtask` image delayed NSH until the CP-side bounded AP
startup wait expired.  The retained shared records showed:

- AP `FAILED(5)`, error 6 (`BK7258_AP_ERROR_TIMEOUT`), heartbeat 0;
- CPU2 still reached `SCHEDULER_ONLINE`, online mask `0x3`;
- the prerequisite SMP/affinity/semaphore gates all passed;
- BDUL remained `RUNNING(2)`, error 0, requested/completed `8/0`;
- both tasks started on CPU1, but both stopped at sequence 2 and neither
  completed;
- the BDUL after-counters remained zero because the controller never reached
  its terminal snapshot before the CP timeout reset the AP.

This isolates the failure to the local CPU1 handshake.  Both same-priority
CPU1 tasks used `up_mdelay(1)` while polling for the peer's exact waiter or
sequence state.  CPU1 has no local SysTick in this port, and a busy delay does
not deterministically give the peer task a scheduling opportunity.  Each task
could therefore prevent the other from reaching the state it was polling.

The correction keeps the bounded delay but follows every missed same-CPU poll
with `sched_yield()`, creating an explicit local scheduling opportunity without
a timer-driven wake or cross-CPU scheduler IPI.  It also publishes both pthread
IDs immediately after creation and makes CPU0 prove task A is blocked before
the single starter post.  The target attribution remains `+3/+0/+3`.

## 7. Board verification closure

The local-yield correction was rebuilt and verified on the real T5-AI board
on 2026-07-30 through the normal AP autostart path.

Final evidence:

- AP `READY(2)`, error 0, heartbeat 148;
- CPU2 `SCHEDULER_ONLINE(8)`, error 0, ready 1, online `0x3`;
- BDUL `PASSED(3)`, error 0, requested/completed `8/8`;
- task A PID 5 ran on CPU1, started/completed `1/1`, sequence 8;
- task B PID 4 ran on CPU1, started/completed `1/1`, sequence 8;
- both PID-release flags are 1;
- CPU0->CPU1 tx/rx `10->13` (`+3`);
- CPU1->CPU0 tx/rx `1->1` (`+0`);
- `smp_call_requests` `11->14` (`+3`);
- global SMP tx/rx is CPU0 `13/1`, CPU1 `1/13`;
- IPI IRQ/wake is CPU0 `1/1`, CPU1 `13/13`;
- coalesced, send-failure, stale, and spurious counters are all zero.

Post-gate liveness remained healthy: AP heartbeat 148, CPU0 SysTick 1775,
and sleep enter/return `148/147` while CPU1 SysTick remained zero as
designed.  This closes N8-C6 as `board-verified` and proves that all sixteen
task rounds were local CPU1 scheduler handoffs: only the two initial remote
dispatches and CPU0's one exact starter wake contributed SMP traffic.

## 8. Preserved boundaries

- `CONFIG_SMP_DEFAULT_CPUSET` remains `0x1`.
- Exactly two CPU1-bound diagnostic tasks and two static semaphores are used.
- Each task executes exactly eight rounds.
- `sched_yield()` provides only a local same-priority scheduling opportunity;
  no CPU1 timer sleep or additional cross-CPU wake was introduced.
- No free migration, load balancing, runtime-variable loop, or stress test is
  enabled.
- No additional code review was performed as part of this board closure.
