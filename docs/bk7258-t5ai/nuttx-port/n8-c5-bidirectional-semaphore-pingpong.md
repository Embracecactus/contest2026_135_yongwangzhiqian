# BK7258 N8-C5 -- CPU0-bound + CPU1-bound bidirectional semaphore pingpong

Date: 2026-07-30

Status: `board-verified` (2026-07-30; explicit FIFO/controller+1 priority produced exact `+9/+8/+17`)

## 1. Scope

N8-C5 implements two detached tasks doing exactly 8 bidirectional semaphore
round trips:

- Initiator: detached pthread, affinity mask 0x1 (CPU0), blocks on
  sem[0], proves responder is waiting on sem[1] before each post.
- Responder: detached pthread, affinity mask 0x2 (CPU1), blocks on
  sem[1], proves initiator is waiting on sem[0] before each post.
- Two static zero-count semaphores (no priority inheritance by default).
- Fixed 8 cycles; no runtime-configurable loop count.

## 2. Expected scheduler attribution

From before task creation through both PID releases:

- CPU0->CPU1 tx/rx: +9 (1 responder dispatch + 8 wakes)
- CPU1->CPU0 tx/rx: +8 (8 wakes)
- Total smp_call_requests: +17
- Failures/coalesced/stale/spurious: 0

## 3. Record layout

Uses the shared generic `bk7258_ap_advanced_state_s` (32 words) at
shared-page offset 0x480.  Magic `0x50325042` ("BP2P"), version 1.

## 4. Config

`BK7258_AP_SMP_BIDIR_PINGPONG` depends on N8-C4
(`BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP`); mutually exclusive with other
N8-C5..D1 choices.  `CONFIG_SMP_DEFAULT_CPUSET` remains 0x1.

Defconfig: `configs/ap_smp_bidir/defconfig`.

## 5. Static verification notes

- Preprocessor nesting: outer `CONFIG_BK7258_AP_SMP_BOOTSTRAP` gate
  in Make.defs/CMakeLists; inner `#if defined(...)` gate in source file.
- Field count: struct `bk7258_ap_advanced_state_s` is exactly 32 words
  (0x80 bytes), verified by static_assert.
- Non-overlap: contiguous 0x80-byte records at 0x480/0x500/0x580/0x600/0x680,
  verified by static_assert chain.
- Format correctness: all printf format specifiers match field types
  (PRIu32 for uint32_t, PRId32 for int32_t, PRIx32 for hex).
- One config per defconfig: only `BK7258_AP_SMP_BIDIR_PINGPONG=y` added.

## 6. First board attempt and correction

The first user-built `ap_smp_bidir` image reached the CP bootloader jump and
early HAL GPIO messages, then did not reach NSH and accepted no console input.
That image is rejected.

The focused post-validation inspection found a source-level scheduling defect
consistent with the stall: the AP main selftest controller and the initiator
pthread are both bound to logical CPU0.  After creating the initiator, the
controller polled completion with `up_mdelay(1)`, which is a busy delay and
does not yield CPU0 to the new same-priority task.  N8-C4 did not expose this
because its diagnostic task was CPU1-bound.

The controller-side completion poll now uses `nxsig_usleep(1000)`.  This
blocks/yields AP logical CPU0 so the initiator can dispatch.  Task-side polling
remains bounded and cross-CPU, and the expected `+9/+8/+17` SMP attribution is
unchanged because this is a local CPU0 timer sleep.

The second user-built image contained the yielding correction but showed the
same pre-NSH stall.  That proves the first correction was insufficient.

## 7. Authorized focused code review

The one authorized review produced these conclusions:

1. A BP2P task deadlock alone cannot explain an indefinitely missing CP shell.
   `bk7258_ap_start()` waits only 3000 ms; its failure path resets AP, and
   `board_app_initialize()` logs the error but still returns zero.  NSH should
   therefore appear after a bounded AP failure.
2. The current test arrangement nevertheless runs AP autostart synchronously
   inside `board_app_initialize()` before NSH exists.  This makes every
   experimental AP scheduler gate share the CP boot critical path and removes
   the only convenient runtime diagnostic channel when hardware reset/wait
   behavior deviates from the intended timeout path.
3. The built artifacts are role-correct: the saved CP ELF contains `nsh_main`
   and no AP entrypoint; the AP ELF contains `bk7258_ap_main` and the BP2P
   selftest.  The AP ELF also contains the `nxsig_usleep(1000)` correction.
4. The split-image package is easy to misuse: root `all-app.bin` remains CP-only.
   Updating BP2P requires flashing `app1_crc.bin` at physical `0x220000`, or
   flashing `all-app-factory.bin` at physical zero.
5. BP2P still ignores pthread detach/affinity attribute return values and may
   inspect task ID zero before the initiator publishes its PID.  These are real
   robustness defects, but neither explains why the independent CP timeout
   path fails to produce NSH; changing them before obtaining shared-state
   evidence would continue diagnosis by guesswork.

The next minimal evidence step is therefore a dedicated CP manual-start
configuration: AP control and `apctl` remain enabled, but AP autostart is off.
This separates CP/NSH boot from the AP experiment.  After NSH appears,
`apctl start 3000` can run the same AP image and `apctl status` can read BP2P,
AP fault, CPU2, IPI and SMP records even after the bounded start fails.

## 8. Second-image shared-state evidence

The user recovered NSH with the existing build invocation and captured the
second image's shared records.  The AP reached the BP2P terminal gate and
reported AP error 16 / BP2P error 8 (`COUNT_MISMATCH`); it did not deadlock:

- initiator PID 5 ran on CPU0, completed once, sequence 8, return value 0;
- responder PID 4 ran on CPU1, completed once, sequence 8, return value 0;
- both PID-release flags are 1;
- CPU0->CPU1 tx/rx changed 10->20 (`+10`);
- CPU1->CPU0 tx/rx stayed 1->1 (`+0`);
- calls changed 11->21 (`+10`).

This proves all eight application-level semaphore round trips completed, but
none of the responder's CPU1->CPU0 posts required an immediate scheduler IPI.
The CPU0 initiator had the same inherited scheduling priority as the CPU0
controller, so making it ready did not force preemption; CPU0's local scheduler
and timer eventually ran it instead.  The stage therefore measured one-way
remote scheduling rather than the intended bidirectional remote wake.

The evidence-driven correction is to create both BP2P tasks with explicit
`SCHED_FIFO` policy at controller priority + 1.  In particular, the initiator
must outrank the CPU0 controller so every responder post forces a CPU1->CPU0
scheduler IPI.  The same change checks all pthread attribute return values and
publishes each pthread ID immediately after creation, avoiding PID-zero probes.
The target attribution remains `+9/+8/+17`.

## 9. Board verification closure

The explicit-priority correction was rebuilt, downloaded through the normal
autostart configuration, and verified on the real T5-AI board on 2026-07-30.

Final evidence:

- AP `READY(2)`, error 0, heartbeat 298;
- CPU2 `SCHEDULER_ONLINE(8)`, error 0, ready 1, online `0x3`, calls 28;
- BP2P `PASSED(3)`, error 0, requested/completed `8/8`;
- initiator PID 5 ran on CPU0, started/completed `1/1`, sequence 8;
- responder PID 4 ran on CPU1, started/completed `1/1`, sequence 8;
- both wait results are 0 and both PID-release flags are 1;
- CPU0->CPU1 tx/rx `10->19` (`+9`);
- CPU1->CPU0 tx/rx `1->9` (`+8`);
- `smp_call_requests` `11->28` (`+17`);
- global SMP tx/rx is CPU0 `19/9`, CPU1 `9/19`;
- IPI IRQ/wake is CPU0 `9/9`, CPU1 `19/19`;
- coalesced/send-failure/stale/spurious counters are all zero.

Post-gate liveness also remained healthy: AP heartbeat 298, CPU0 SysTick 3475,
and sleep enter/return `298/297` while CPU1 SysTick remained zero as designed.

This closes N8-C5 as `board-verified`.  The rejected `+10/+0/+10` image remains
historical evidence that equal-priority CPU0 scheduling does not prove reverse
remote wake; explicit `SCHED_FIFO` at controller priority + 1 is the verified
condition that produces exact bidirectional attribution.

## 10. Preserved boundaries

- `CONFIG_SMP_DEFAULT_CPUSET` remains `0x1`.
- Exactly two diagnostic tasks and two ping-pong semaphores are used.
- The loop remains fixed at eight rounds.
- No free migration, load balancing, runtime-variable loop, or stress test is
  enabled.
- `cp_nsh_manual` remains a diagnostic fallback; the verified closure used the
  normal CP autostart configuration.
