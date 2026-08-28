# BK7258 N8-C7 -- Controlled migration

Date: 2026-07-30

Status: `board-verified` (2026-07-30; fixed 8-transition controlled migration completed with exact +4/+4/+8 attribution)

## 1. Scope

N8-C7 creates one detached task with initial affinity mask 0x1 (CPU0).
It requests exactly 8 controlled transitions through a target-CPU SMP
callback and one static zero-count semaphore:

- Odd transitions (1,3,5,7): target 0x2 (CPU1)
- Even transitions (2,4,6,8): target 0x1 (CPU0)

For each transition the task first starts the target callback, then blocks.
The callback proves the exact semaphore waiter, applies
`pthread_setaffinity_np(thread, target)` while the task is blocked, and posts
the semaphore locally on the target CPU.  After resuming, the task calls
`pthread_getaffinity_np` and `up_cpu_index()` to verify both match the target.

## 2. Expected scheduler attribution

- CPU0->CPU1 tx/rx: +4 (4 migrations to CPU1)
- CPU1->CPU0 tx/rx: +4 (4 migrations to CPU0)
- Total smp_call_requests: +8
- One PID released.
- Failures/coalesced/stale/spurious: 0

## 3. Record layout

Uses the shared generic `bk7258_ap_advanced_state_s` (32 words) at
shared-page offset 0x580.  Magic `0x47494d42` ("BMIG"), version 1.

task_id[0] = the migration task; task_cpu[0] = final CPU after
migration 8 (CPU0, since 8 is even).  sequence[0] = 8.
aux[0] = PID-released flag.

## 4. Config

`BK7258_AP_SMP_CONTROLLED_MIGRATION` depends on N8-C4; mutually
exclusive with other N8-C5..D1 choices.  `CONFIG_SMP_DEFAULT_CPUSET`
remains 0x1.

Defconfig: `configs/ap_smp_migration/defconfig`.

## 5. Static verification notes

- Field count: 32 words, static_assert verified.
- Non-overlap: contiguous with BDUL and BTIM records.
- Format correctness: all PRIu32/PRId32 match field types.
- One config per defconfig: only `BK7258_AP_SMP_CONTROLLED_MIGRATION=y`.

## 6. CPU0 controller yielding correction

The N8-C5 failed board attempt exposed that an AP-main controller cannot
busy-poll with `up_mdelay` while waiting for a newly created task on the same
logical CPU0.  N8-C7 has the same initial placement: both the controller and
the migration task start on CPU0.  Its completion poll therefore also uses
`nxsig_usleep(1000)` so the task can dispatch and later resume on CPU0 after
each even-numbered migration.  This local CPU0 sleep does not add an SMP IPI
and does not change the expected `+4/+4/+8` attribution.

## 7. First board attempt

The first `ap_smp_migration` image was downloaded to the real T5-AI board,
but CP reached its bounded AP startup timeout before the prerequisite N8-C4
loop completed.  The retained records showed:

- AP `FAILED(5)`, error 6 (`BK7258_AP_ERROR_TIMEOUT`), heartbeat 0;
- CPU2 `SCHEDULER_ONLINE(8)`, error 0, online `0x3`;
- the automatic SMP selftest remained `PASSED`, with no coalesced, send-fail,
  stale, or spurious counts;
- the shared affinity task started on CPU1 but had not completed or released;
- BSEM reached `WOKEN(5)`, error 0;
- BSWL reached `WOKEN(5)`, error 0, requested/completed `8/3`, with exact
  wait/observe/post/return sequences `3/3/3`;
- global CPU0->CPU1 traffic and calls reached 5 and 6 respectively, exactly
  matching the baseline SMP calls plus one affinity dispatch and three
  completed remote wakes.

No BMIG record was published because `bk7258_ap_smp_bmig_selftest()` is called
only after the prerequisite affinity/semaphore loop returns successfully.
This image therefore provides no failure evidence against the controlled
migration implementation itself, and no BMIG code change is justified yet.

The next minimal evidence step is to start the same already-flashed AP image
once more from the failed/held state with `apctl start 3000`, then read
`apctl status`.  This distinguishes a transient prerequisite handoff stall
from a reproducible image-specific failure without rebuilding or reflashing.

## 8. Same-image retry and BMIG correction

The requested same-image retry (`apctl start 3000`) completed all prerequisite
gates and reached BMIG on generation 2.  Direct BMIG evidence was:

- affinity, BSEM, and BSWL all `PASSED`, including BSWL `8/8`;
- BMIG `FAILED(4)`, error 5 (`BK7258_AP_BMIG_ERROR_BAD_CPU`),
  requested/completed `8/0`;
- PID 4 started and completed on CPU0, but sequence remained 0;
- the before snapshot was CPU0 tx/rx `10/10`, CPU1 tx/rx `1/1`, calls 11;
- no after snapshot was reached and global calls remained 11, proving that no
  migration scheduler IPI occurred;
- CP eventually reported `apctl: start failed: -110` and replaced the still
  STARTING AP state with timeout error 6.

The failure occurs on the first odd transition.  `pthread_setaffinity_np()`
successfully changes the running task's mask from CPU0-only to CPU1-only, but
it does not guarantee that the call itself returns after the task has already
been rescheduled on CPU1.  The original task immediately called
`pthread_getaffinity_np()` and `up_cpu_index()`: the mask was already `0x2`
while execution was still on CPU0, so it published BAD_CPU before the first
scheduling point and left sequence at zero.

The correction calls `sched_yield()` immediately after each successful
self-affinity update.  Because the current CPU is then excluded by the new
mask, the yield is the explicit controlled scheduling point that transfers the
task to the target CPU before affinity/CPU verification.  It uses no timer
sleep and preserves the intended one scheduler handoff per transition, so the
target remains CPU0->CPU1 `+4`, CPU1->CPU0 `+4`, calls `+8`.

## 9. Post-affinity yield result and blocked-task rendezvous

The rebuilt post-affinity-yield image completed every prerequisite gate, but
BMIG remained `RUNNING(2)`, error 0, requested/completed `8/0`.  PID 4 started
on CPU0 but did not complete, sequence remained 0, and global calls remained
11.  CP eventually reset AP with startup timeout error 6.  This proves that
`sched_yield()` did not deliver the affinity-excluded ready task to CPU1.

The two sequence-zero images expose both sides of the same NuttX running-task
affinity gap:

1. Without an explicit scheduling point, `pthread_setaffinity_np()` could
   return while the task still executed on the now-excluded source CPU,
   producing immediate BAD_CPU.
2. With `sched_yield()`, the task could be switched out and left ready for the
   target CPU, but no scheduler IPI was sent to wake that target.  CPU1 has no
   local SysTick, so the task remained stranded and BMIG stayed RUNNING.

The relevant upstream path updates the mask in `nxsched_set_affinity()` and
reuses `nxsched_set_priority()` for a running task.  Its SMP running-task path
calls `nxsched_deliver_task(this_cpu(), tcb->cpu, SWITCH_EQUAL)` while
`tcb->cpu` still identifies the source CPU.  That can perform a local switch,
but it does not provide the missing target-CPU notification.  The official
NuttX tree remains unchanged.

The new team-overlay correction avoids changing affinity while the task is
running:

1. The migration task queues one asynchronous SMP callback to the target CPU
   and immediately blocks on one static zero-count semaphore.
2. The callback may arrive before or after the block; it waits until the exact
   task is waiting on that semaphore.
3. The target callback proves that exact waiter state before changing affinity.
4. While the task is blocked, the callback applies
   `pthread_setaffinity_np(thread, target)`.
5. The callback posts the semaphore locally on the target CPU, so the task is
   made ready and resumes there.
6. The task verifies the mask, `up_cpu_index()`, and sequence before requesting
   the next transition.

The task therefore never runs with an affinity mask that excludes its current
CPU.  The callback SMP command is also the one cross-CPU scheduler doorbell per
transition, preserving exact CPU0->CPU1 `+4`, CPU1->CPU0 `+4`, calls `+8`.
No timer sleep, free migration, default CPU1 placement, or official-tree
change is introduced.  BMIG `value[0]/value[1]` now expose the last callback
start/completion cycles and should finish at `8/8`.

## 10. Rendezvous board result and immediate-block correction

The first blocked-task rendezvous image reached a terminal BMIG failure rather
than the earlier AP startup timeout:

- AP `FAILED(5)`, error 18 (`BK7258_AP_ERROR_CPU2_BMIG`);
- every prerequisite gate passed;
- BMIG `FAILED(4)`, error 6 (`BK7258_AP_BMIG_ERROR_TIMEOUT`),
  requested/completed `8/0`;
- PID 4 started and completed, final recorded CPU was CPU1, sequence was 1,
  and PID release `aux[0]` was 1;
- callback start/completion values were `2/1`;
- CPU0->CPU1 changed `10->11` (`+1`), CPU1->CPU0 changed `1->2`
  (`+1`), and calls changed `11->13` (`+2`).

This proves cycle 1 completed the full target-callback, blocked-affinity-update,
target-local-post, and CPU1 resume path.  It also proves the reverse cycle-2
callback reached CPU0, but cycle 2 did not complete its rendezvous.

The task still contained an unnecessary ordering dependency: after queueing the
callback it busy-waited for `callback_started == cycle` before blocking.  A
delayed target IPI could consume that bounded acknowledgement window while the
callback, once entered, waited for the task to block.  Both sides could then
reach timeout without violating the underlying semaphore/affinity mechanism.

The correction removes the callback-start wait entirely.  Immediately after a
successful `nxsched_smp_call_single_async()`, the task blocks on the semaphore.
The callback already supports both arrival orders and waits for the exact
waiter, so no acknowledgement is needed for correctness.  The `value[0]` start
counter remains as observability only.  Counter targets remain `+4/+4/+8`.

## 11. Immediate-block image prerequisite failure

The rebuilt immediate-block image did not reach BMIG.  Its generation-1
records terminated in the inherited N8-C4 gate:

- AP `FAILED(5)`, error 15 (`BK7258_AP_ERROR_CPU2_SEM_WAKE_LOOP`);
- CPU2 remained `SCHEDULER_ONLINE`, online `0x3`;
- BSEM remained `WOKEN(5)`, error 0;
- BSWL `FAILED(8)`, error 7
  (`BK7258_AP_SEM_WAKE_LOOP_ERROR_SEQUENCE`), requested/completed `8/5`;
- wait/observe/post/return and wait/post/wake sequences were all exactly 5;
- affinity propagated the terminal failure as error 7/count mismatch;
- global calls reached 8, matching baseline 2 + one affinity dispatch + five
  completed semaphore wakes;
- no BMIG record was published.

This result does not exercise or reject the immediate-block migration
correction.  The same prerequisite loop was previously board-verified and has
also passed in multiple N8-C7 images, so changing BMIG or BSWL from this single
attempt would be diagnosis by guesswork.

The next minimal evidence step is a same-image `apctl start 3000` followed by
`apctl status`.  If the retry passes BSWL and reaches BMIG, evaluate the
immediate-block correction directly.  If it reproduces BSWL sequence error 7
at cycle 5, the prerequisite failure becomes the current reproducible blocker.

## 12. Same-image retry: six migrations completed

The same-image `apctl start 3000` retry passed all prerequisite gates and
executed the immediate-block BMIG path through six complete transitions:

- BMIG remained `RUNNING(2)`, error 0, requested/completed `8/0` when CP's
  outer startup timeout reset AP;
- PID 4 was last recorded on CPU0, started/not-completed `1/0`, sequence 6;
- callback start/completion values were `7/6`;
- CPU0->CPU1 changed from 10 to 14 (`+4`);
- CPU1->CPU0 changed from 1 to 4 (`+3`);
- calls changed from 11 to 18 (`+7`);
- CPU1 call-handler/delivered counts were `14/13`, showing CPU1 was still
  inside the cycle-7 callback when the snapshot was taken;
- coalesced, send-failure, stale, and spurious counts remained zero.

The attribution is exact: cycles 1..6 contributed three forward and three
reverse callbacks, and the started-but-not-completed cycle 7 contributed the
fourth forward callback.  This validates immediate blocking and six complete
blocked-affinity/target-local-wake migrations.  The remaining stall is inside
the target callback before it completes the cycle-7 waiter rendezvous.

The callback previously entered `enter_critical_section()` on every polling
iteration before the source task had necessarily completed its semaphore
blocking path.  In target IRQ context this creates an avoidable cross-core
critical-section ordering window.  `nxsem_get_value()` is an atomic count read
and does not require that outer critical section.

The correction now polls only the atomic semaphore count until it becomes
`-1`.  Only after the count proves a waiter exists does the callback enter the
scheduler critical section once to verify `TSTATE_WAIT_SEM`, the exact
`waitobj`, and the still-negative count.  It then performs the blocked affinity
update and target-local post exactly as before.  Counter targets remain
`+4/+4/+8`; official NuttX remains unchanged.

## 13. Final board verification

The lock-free count-poll / one-shot exact-check correction was rebuilt,
downloaded, and completed on the real T5-AI board through the normal autostart
path:

- AP reached `READY(2)`, error 0, generation 1;
- CPU2 remained `SCHEDULER_ONLINE(8)`, error 0, ready 1, online `0x3`;
- BSMP, affinity, BSEM, and BSWL all remained `PASSED`, including BSWL `8/8`;
- BMIG reached `PASSED(3)`, error 0, requested/completed `8/8`;
- PID 4 started and completed, finished on CPU0, reached sequence 8, and
  published callback start/completion values `8/8`;
- `aux[0]/aux[1]` was `1/0`, proving detached PID release without an error
  latch;
- CPU0->CPU1 tx/rx changed `10->14` (`+4`), CPU1->CPU0 changed `1->5`
  (`+4`), and calls changed `11->19` (`+8`);
- call-handler/delivered counts closed at CPU0 `5/5` and CPU1 `14/14`;
- coalesced, send-failure, stale, and spurious counts were all zero.

The three retained status samples also proved post-gate liveness: AP heartbeat
increased `58->155->258`, CPU0 SysTick increased `789->1849->2989`, and sleep
enter/return increased `58/57->155/154->258/257`, while CPU1 SysTick remained
zero as designed.  An `apctl start 3000` issued while AP was already READY
returned `-16` (`-EBUSY`); the subsequent status was unchanged and healthy, so
this is the active-state guard rather than a selftest failure.

This closes all eight alternating blocked-affinity/target-local-wake
transitions with the exact planned `+4/+4/+8` attribution.  The lock-free
semaphore-count gate is therefore board-verified, and N8-C7 is accepted as the
latest verified baseline.

## 14. Review status and preserved boundaries

- No new code review was performed; review remains separate and requires fresh
  explicit authorization.
- `CONFIG_SMP_DEFAULT_CPUSET` remains `0x1`.
- The test remains one detached task, one static semaphore, and exactly eight
  controlled transitions; it does not enable free migration or load balancing.
- Official NuttX remains unchanged.
