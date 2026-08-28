# BK7258 N8-C8 -- CPU1 timed wake

Date: 2026-07-30

Status: `board-verified` (2026-07-30; eight CPU1 timed wakes completed with exact +9/+0/+9 attribution)

## 1. Scope

N8-C8 creates one detached task with initial affinity mask 0x2 (CPU1).
After initial dispatch, the task performs exactly 8 `nxsig_usleep(20000)`
cycles (20 ms each).  After each sleep returns, the task verifies it is
still on CPU1.  The CPU0 system timer is the authoritative timer source
(CPU1 SysTick may remain zero).

## 2. Expected scheduler attribution

- CPU0->CPU1 tx/rx/irq/wake: +9 (initial CPU1 task dispatch + 8 timer-driven wakes)
- CPU1->CPU0: unchanged (+0)
- Total smp_call_requests: +9
- One PID released.
- Failures/coalesced/stale/spurious: 0

## 3. Record layout

Uses the shared generic `bk7258_ap_advanced_state_s` (32 words) at
shared-page offset 0x600.  Magic `0x4d495442` ("BTIM"), version 1.

task_id[0] = the timed task; task_cpu[0] = 1 (CPU1).
value[0] = sleep-entered cycle number, value[1] = sleep return code.
sequence[0] = 8.  aux[0] = sleep interval (20000 us).
aux[1] = PID-released flag.

## 4. Config

`BK7258_AP_SMP_CPU1_TIMED_WAKE` depends on N8-C4; mutually exclusive
with other N8-C5..D1 choices.  `CONFIG_SMP_DEFAULT_CPUSET` remains 0x1.

Defconfig: `configs/ap_smp_timedwait/defconfig`.

## 5. Static verification notes

- Field count: 32 words, static_assert verified.
- Non-overlap: contiguous with BMIG and BLCY records.
- Format correctness: all PRIu32/PRId32 match field types.
- One config per defconfig: only `BK7258_AP_SMP_CPU1_TIMED_WAKE=y`.

## 6. Current board gate

N8-C8 is now the CURRENT stage.  Build with
`AP_CONFIG_NAME=ap_smp_timedwait` and require:

- AP `READY`, error 0;
- BTIM `PASSED`, error 0, requested/completed `8/8`;
- task CPU 1, started/completed `1/1`, sequence 8;
- `value[0]=8`, `value[1]=0`, `aux[0]=20000`, `aux[1]=1`;
- CPU0->CPU1 `+9`, CPU1->CPU0 `+0`, calls `+9`;
- zero coalesced/send-failure/stale/spurious counts.

## 7. First board attempt: inherited prerequisite stall

The user rebuilt and downloaded the `ap_smp_timedwait` image, but generation 1
did not reach BTIM before CP's bounded startup timeout:

- AP ended `FAILED(5)`, error 6 (`BK7258_AP_ERROR_TIMEOUT`), heartbeat 0;
- CPU2 remained `SCHEDULER_ONLINE(8)`, error 0, ready 1, online `0x3`;
- the automatic SMP gate remained `PASSED`, requested/completed `2/2`;
- global calls were 3, exactly the two automatic SMP calls plus the affinity
  task's initial CPU0->CPU1 dispatch;
- handler call/delivered counts were fully closed at CPU0 `1/1` and CPU1
  `2/2`, with zero coalesced, send-failure, stale, or spurious counts;
- the shared affinity task had requested/observed mask `0x2`, started on CPU1,
  but had not completed or released its PID;
- BSEM remained `WAITING`, with wait entered/observed/value `1/0/0` and no
  post or return;
- BSWL likewise remained at its first wait, requested/completed `8/0`, with
  wait/post/wake sequence `1/0/0`;
- no BTIM record was published, so the timed-wake implementation was not
  exercised.

The per-gate `before->after` fields are incomplete because their terminal
snapshots were never reached; they must not be interpreted as negative
counter deltas.  The global counters instead show a clean, fully delivered
initial dispatch followed by a stall while the controller was waiting to
observe the inherited semaphore waiter.  AP error 6 was written by CP's outer
startup timeout and is not a BTIM failure.

No C8 code change is justified from this attempt.  The next minimal evidence
step is to restart the same already-flashed image with `apctl start 3000`, then
read `apctl status`.  Rebuilding or changing the timed-wake implementation
before that retry would not test the observed boundary.

## 8. Same-image retry: timed-wake path completed

The generation-2 same-image retry passed every inherited prerequisite and
exercised BTIM directly:

- AP published error 19 (`BK7258_AP_ERROR_CPU2_BTIM`) after BTIM returned
  `-EIO`; this was an AP selftest rejection, not CP startup timeout;
- affinity, BSEM, and BSWL all reached `PASSED`, including BSWL `8/8`;
- BTIM reached `FAILED`, error 6
  (`BK7258_AP_BTIM_ERROR_COUNT_MISMATCH`), requested/completed `8/0`;
- PID 4 ran on CPU1, started/completed `1/1`, reached sequence 8, returned 0
  from the final sleep, and released its PID (`aux[1]=1`);
- `value[0]/value[1]=8/0` and `aux[0]=20000` prove all eight 20 ms sleeps
  completed successfully on CPU1;
- CPU0->CPU1 tx/rx changed `10->19` (`+9`), CPU1->CPU0 remained `1->1`
  (`+0`), and calls changed `11->20` (`+9`);
- handler call/delivered closed at CPU0 `1/1` and CPU1 `19/19`, with zero
  coalesced, send-failure, stale, or spurious counts.

The functional timed-wake path therefore completed.  The only rejected check
was the expected attribution: the before snapshot is taken before
`pthread_create()`, so it includes one CPU0->CPU1 scheduler IPI for the
explicitly CPU1-bound task's initial dispatch, followed by eight timer-driven
wakes.  The correct total is `+9/+0/+9`, not `+8/+0/+8`.  `completed` remains
zero because the controller publishes 8 only after this terminal counter
check; it does not mean that the task or sleep loop was incomplete.

The team-overlay correction changes only the terminal expectation to
`BK7258_AP_ADV_CYCLES + 1` for CPU0 tx, CPU1 rx, and SMP-call counts.  Task
behavior, interval, affinity, cycle count, and reverse-direction expectation
are unchanged.  This correction is static-only and requires one rebuild and
board retry.

## 9. Corrected-image board verification

The rebuilt image again encountered the inherited generation-1 first-waiter
stall before BTIM, but the requested same-image `apctl start 3000` retry passed
all prerequisites and closed N8-C8 on generation 2:

- AP reached `READY(2)`, error 0, generation 2;
- CPU2 remained `SCHEDULER_ONLINE(8)`, error 0, ready 1, online `0x3`;
- affinity, BSEM, and BSWL all reached `PASSED`, including BSWL `8/8`;
- BTIM reached `PASSED(3)`, error 0, requested/completed `8/8`;
- PID 4 ran on CPU1, started/completed `1/1`, reached sequence 8, and
  published `value[0]/value[1]=8/0` plus `aux[0]/aux[1]=20000/1`;
- CPU0->CPU1 tx/rx changed `10->19` (`+9`), CPU1->CPU0 remained `1->1`
  (`+0`), and calls changed `11->20` (`+9`);
- handler call/delivered closed at CPU0 `1/1` and CPU1 `19/19`, with zero
  coalesced, send-failure, stale, or spurious counts.

The retained post-gate sample also proved liveness: AP heartbeat increased
`1->62`, CPU0 SysTick increased `52->730`, and sleep enter/return increased
`1/0->62/61`, while CPU1 SysTick remained zero as designed.  This confirms the
correct attribution is one initial CPU1 dispatch plus eight timer wakes and
closes N8-C8 as board-verified on the same-image generation-2 restart path.
The generation-1 prerequisite stall remains inherited evidence and is not a
BTIM functional failure.

## 10. Review status

- No new code review was performed; review remains separate and requires fresh
  explicit authorization.
- `CONFIG_SMP_DEFAULT_CPUSET` remains `0x1` and official NuttX remains
  unchanged.
