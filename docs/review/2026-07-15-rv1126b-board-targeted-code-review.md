# RV1126B board/BSP targeted code review

Date: 2026-07-15

Scope:

```text
$CONTEST/board/contest_board/
```

Diff baseline:

```text
origin/dev-ai-contest-2026...HEAD
```

Review mode:

- PR-before-merge targeted review using the `driver-code-reviewer` criteria.
- Read-only review; no build, flash, commit, push, or file changes were performed during the review itself.
- Round 1 subagent completed. Round 2 subagent failed due to an API error, so the main model manually verified the critical paths before writing this report.
- CodeGraph was synced before the final CodeGraph lookups.

Diff size:

```text
38 files changed, 3832 insertions(+), 31 deletions(-)
```

Lightweight checks before deep review:

- `git diff --check origin/dev-ai-contest-2026...HEAD` produced no whitespace/style errors.
- Changed paths are limited to the contest overlay/docs/logs area:
  - `board/contest_board/`
  - `docs/`
  - `logs/`
- AI log validation had already passed before this review.

## Overall result

Strict `driver-code-reviewer` quality gate result:

```text
Score: approximately 52/100
Conclusion: REJECT / at least NEEDS_FIX before claiming BSP quality convergence
Design health: approximately 4/10, C
```

Practical interpretation:

- The existing branch has useful board evidence: AMP partition update boots into NSH, UART RX/TX works, `help`, `uname -a`, and `ps` were verified.
- However, the current BSP code has several core-path risks in atomic operations, interrupt namespace, timer dispatch, MTIME split access, trap-frame alignment, and Make/CMake source-set consistency.
- Recommended PR strategy is **Choice A**: fix P0/P1 blockers before creating or finalizing the PR.

## P0 / PR blocker findings

### DR-001: `__atomic_*_4` implementations are not atomic [Critical]

Files:

- `$CONTEST/board/contest_board/chip/rv1126b_atomic.c:19`
- `$CONTEST/board/contest_board/chip/rv1126b_atomic.c:31`
- `$CONTEST/board/contest_board/chip/rv1126b_atomic.c:41`
- `$CONTEST/board/contest_board/chip/rv1126b_atomic.c:52`
- `$CONTEST/board/contest_board/chip/rv1126b_atomic.c:63`
- `$CONTEST/board/contest_board/chip/rv1126b_atomic.c:74`

Severity: Critical

Dimension:

- L1-2 concurrency safety
- Atomicity / memory-ordering violation

Evidence:

The custom `__atomic_compare_exchange_4()`, `__atomic_fetch_add_4()`, `__atomic_fetch_sub_4()`, `__atomic_fetch_or_4()`, and `__atomic_fetch_and_4()` functions perform plain load/modify/store sequences and ignore the `memorder` arguments. There is no AMO instruction, `up_irq_save()` / `up_irq_restore()`, spinlock, or barrier.

Single-core does not make these operations atomic: an ISR or preemption point can interrupt the read-modify-write sequence.

Failure scenario:

A kernel or driver path uses a GCC atomic builtin for a shared flag or reference count. An interrupt arrives between load and store. Both contexts update from the same old value, causing lost update, corrupted reference count, or stale flag state.

Recommendation:

If RV1126B SCR1 lacks the RISC-V A extension, minimally wrap each 32-bit read-modify-write operation with `up_irq_save()` / `up_irq_restore()` and add the required compiler barriers. Prefer a real libatomic/AMO implementation if available.

Blocker: yes.

---

### DR-002: external IRQ namespace does not follow the NuttX RISC-V `RISCV_IRQ_EXT + source` convention [High]

Files:

- `$CONTEST/board/contest_board/chip/include/irq.h:32`
- `$CONTEST/board/contest_board/chip/include/irq.h:33`
- `$CONTEST/board/contest_board/chip/rv1126b_irq_dispatch.c:136`
- `$CONTEST/board/contest_board/chip/rv1126b_irq_dispatch.c:214`
- `$CONTEST/board/contest_board/chip/rv1126b_serial.c:51`
- `$CONTEST/board/contest_board/chip/rv1126b_serial.c:258`
- `$CONTEST/board/contest_board/chip/rv1126b_serial.c:263`

Severity: High

Dimension:

- L1-7 embedded IRQ correctness
- L1-6 boundary/namespace correctness

Evidence:

The chip IRQ header defines:

```c
#define NR_INTMUX_IRQS      256
#define NR_IRQS             (RISCV_IRQ_EXT + NR_INTMUX_IRQS)
```

This implies external interrupt numbers should be represented as:

```text
RISCV_IRQ_EXT + intmux_source_id
```

However, the dispatcher returns and dispatches the raw INTMUX source id:

```c
return i * INTMUX_IRQS_PER_GROUP + bit;
regs = riscv_doirq(irq, regs);
```

The UART driver also uses the raw source id:

```c
#define CONSOLE_UART_IRQ 61
irq_attach(priv->irq, rv1126b_uart_interrupt, dev);
up_enable_irq(priv->irq);
```

Failure scenario:

A peripheral source id overlaps the RISC-V exception/core interrupt namespace. Some IRQs may attach into the wrong vector table slot or be treated as exceptions/core interrupts. UART5 may currently work because source id 61 is above the core range, but the BSP convention is still wrong and unsafe for future peripherals.

Recommendation:

Define peripheral IRQs as NuttX IRQ numbers, for example:

```c
#define RV1126B_IRQ_UART5 (RISCV_IRQ_EXT + 61)
```

In dispatch:

```c
source = i * 32 + bit;
regs = riscv_doirq(RISCV_IRQ_EXT + source, regs);
```

In `up_enable_irq()` / `up_disable_irq()`, validate `irq > RISCV_IRQ_EXT && irq < NR_IRQS`, then convert back to INTMUX source id with `source = irq - RISCV_IRQ_EXT`.

Blocker: yes.

---

### DR-003: timer interrupt bypasses `riscv_doirq()` bookkeeping [High]

Files:

- `$CONTEST/board/contest_board/chip/rv1126b_irq_dispatch.c:189`
- `$CONTEST/board/contest_board/chip/rv1126b_irq_dispatch.c:193`
- `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c:166`
- `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c:188`
- Reference: `$WORKSPACE/nuttx/arch/risc-v/src/common/riscv_doirq.c:125`
- Reference: `$WORKSPACE/nuttx/arch/risc-v/src/common/riscv_doirq.c:128`
- Reference: `$WORKSPACE/nuttx/arch/risc-v/src/common/riscv_doirq.c:158`
- Reference: `$WORKSPACE/nuttx/arch/risc-v/src/common/riscv_doirq.c:162`

Severity: High

Dimension:

- L1-7 embedded interrupt handling
- L1-2 scheduler/concurrency safety

Evidence:

Timer trap dispatch currently does:

```c
if (cause == IRQ_M_TIMER)
  {
    up_timer_int();
  }
```

`up_timer_int()` directly calls:

```c
nxsched_process_timer();
```

The common `riscv_doirq()` path is bypassed. That path normally sets interrupt context, suspends/resumes the scheduler, runs `irq_dispatch()`, and handles context-switch bookkeeping.

Failure scenario:

The timer tick triggers scheduler activity or watchdog callbacks without the common RISC-V IRQ context being established. This can produce incorrect interrupt-context state or missed context-switch bookkeeping as the BSP grows beyond the current minimal NSH baseline.

Recommendation:

Use the standard IRQ path for the machine timer:

- attach a timer ISR using `irq_attach(RISCV_IRQ_MTIMER, ...)`;
- dispatch timer traps via `riscv_doirq(RISCV_IRQ_MTIMER, regs)`;
- let the ISR acknowledge the timer and call `nxsched_process_timer()` through the normal IRQ dispatch framework.

Blocker: yes.

---

### DR-004: RV32 split MTIME / MTIMECMP access is not safe [High]

Files:

- `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c:177`
- `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c:178`
- `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c:183`
- `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c:184`

Severity: High

Dimension:

- L1-7 embedded timer/register correctness
- L1-5 numeric/split-register safety

Evidence:

Current MTIME read uses one high read followed by one low read. Current MTIMECMP write writes low first, then high.

On RV32, 64-bit timer registers require stable split-register access. Reads usually need a high-low-high retry pattern. Writes to compare registers usually need a high-to-max / low / final-high sequence to avoid transient compare values.

Failure scenario:

If low rolls over between high and low reads, the computed `mtime` is inconsistent. If `MTIMECMP` is written low first, a transient compare value can be in the past or too near, causing interrupt storm, tick drift, or long tick gaps.

Recommendation:

Reuse or mirror the common RISC-V mtimer helper patterns, such as:

- stable `riscv_mtimer_get()`-style read;
- safe `riscv_mtimer_set()`-style compare write.

Blocker: yes.

---

### DR-005: trap frame and initial stack do not guarantee 16-byte RISC-V ABI alignment [High]

Files:

- `$CONTEST/board/contest_board/chip/rv1126b_head.S:62`
- `$CONTEST/board/contest_board/chip/rv1126b_head.S:63`
- `$CONTEST/board/contest_board/chip/rv1126b_head.S:99`
- `$CONTEST/board/contest_board/chip/rv1126b_head.S:137`
- `$CONTEST/board/contest_board/scripts/ld.script:88`
- `$CONTEST/board/contest_board/scripts/ld.script:89`
- Reference: `$WORKSPACE/nuttx/arch/risc-v/include/irq.h:54`
- Reference: `$WORKSPACE/nuttx/arch/risc-v/include/irq.h:56`

Severity: High

Dimension:

- L1-5 type/numeric ABI safety
- L1-7 startup/trap correctness

Evidence:

The public RISC-V header defines 16-byte stack-frame alignment. The linker script aligns `_ebss` only to 4 bytes. The trap frame size is defined as `33 * 4 = 132`, which is not a multiple of 16. The trap handler subtracts this size and then calls C code.

Failure scenario:

A trap enters with a 16-byte aligned stack, subtracts 132 bytes, and calls `riscv_dispatch_irq()` with a misaligned stack. This violates the RISC-V psABI and may fail under compiler optimizations, future FPU/TLS usage, or stricter stack assumptions.

Recommendation:

- Align `_ebss` and heap start to at least 16 bytes.
- Round the saved context frame size to a 16-byte boundary.
- Prefer reusing common NuttX RISC-V trap-frame constants/macros where possible.

Blocker: yes.

---

### DR-006: Make and CMake source sets are inconsistent for `riscv_mtimer.c` [High]

Files:

- `$CONTEST/board/contest_board/chip/Make.defs:3`
- `$CONTEST/board/contest_board/chip/Make.defs:6`
- `$CONTEST/board/contest_board/chip/CMakeLists.txt:20`
- `$CONTEST/board/contest_board/chip/CMakeLists.txt:35`
- Reference: `$WORKSPACE/nuttx/arch/risc-v/src/common/CMakeLists.txt:30`

Severity: High

Dimension:

- L1-4 build integration / error handling
- Design consistency

Evidence:

Make path explicitly excludes the common mtimer implementation:

```make
CMN_CSRCS := $(filter-out riscv_mtimer.c,$(CMN_CSRCS))
```

The common RISC-V CMake file appends `riscv_mtimer.c`, while the RV1126B chip CMake file only adds RV1126B sources and does not remove the common mtimer source.

Failure scenario:

Classic Make and CMake builds compile different source sets. CMake can include both the custom `rv1126b_timerisr.c` and common `riscv_mtimer.c`, which can cause compile/link problems or semantic conflicts. This also contradicts any claim that CMake and Make are equivalent.

Recommendation:

- Add a CMake-side exclusion or option equivalent to `filter-out riscv_mtimer.c`;
- or migrate to the common mtimer design and remove the custom timer conflict;
- or explicitly mark CMake as unsupported/unverified and avoid implying parity.

Blocker for CMake parity: yes.

## P1 / high-value follow-up findings

### DR-007: raw binary entry depends on link order; linker script does not explicitly keep `.text.start` first [Medium / Plausible]

Files:

- `$CONTEST/board/contest_board/chip/rv1126b_head.S:80`
- `$CONTEST/board/contest_board/scripts/ld.script:23`
- `$CONTEST/board/contest_board/scripts/ld.script:25`
- `$CONTEST/board/contest_board/chip/CMakeLists.txt:20`
- `$CONTEST/board/contest_board/chip/CMakeLists.txt:32`
- `$CONTEST/board/contest_board/chip/CMakeLists.txt:35`

Evidence:

`__start` is placed in `.text.start`, but the linker script first collects `*(.start .start.*)` and then `*(.text .text.*)`. It does not explicitly use `KEEP(*(.text.start))` at the beginning of `.text`. Raw binary output does not carry ELF entry metadata.

Recommendation:

Put `KEEP(*(.text.start))` at the start of `.text`, before generic `.text*` collection. Optionally add a linker assertion that the image start equals `__start`.

---

### DR-008: clock verification failure is silently ignored [Medium]

Files:

- `$CONTEST/board/contest_board/chip/rv1126b_clockconfig.c:152`
- `$CONTEST/board/contest_board/chip/rv1126b_clockconfig.c:154`
- `$CONTEST/board/contest_board/chip/rv1126b_start.c:135`
- `$CONTEST/board/contest_board/chip/rv1126b_start.c:139`
- `$CONTEST/board/contest_board/chip/rv1126b_start.c:143`

Evidence:

If GPLL lock verification fails, `rv1126b_clockconfig()` returns without reporting failure, and startup proceeds to board initialization and `nx_start()`.

Recommendation:

Return `int` from clock verification and either select a safe fallback, log the failure, or stop boot with `PANIC()`.

---

### DR-009: system tick is hard-coded to 100 Hz instead of using NuttX tick configuration [Medium]

Files:

- `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c:59`
- `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c:60`
- `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c:114`
- `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c:115`

Evidence:

`RV1126B_MTIME_TICK_VALUE` divides by literal `100`; the comment assumes `CONFIG_USEC_PER_TICK = 10000` but the code does not use the config value.

Recommendation:

Compute the tick period from `CONFIG_USEC_PER_TICK` or the appropriate NuttX tick-rate constant.

---

### DR-010: GPIO3 base address is inconsistent between hardware headers [Medium / Plausible]

Files:

- `$CONTEST/board/contest_board/chip/hardware/rv1126b_memorymap.h:64`
- `$CONTEST/board/contest_board/chip/hardware/rv1126b_gpio.h:26`

Evidence:

`rv1126b_memorymap.h` defines GPIO3 base as `0x21E00000UL`; `rv1126b_gpio.h` defines GPIO3 base as `0x21C00000UL`.

Recommendation:

Use the SDK/TRM as the source of truth and avoid duplicating base addresses across peripheral headers. Peripheral headers should reference `rv1126b_memorymap.h` for base addresses.

---

### DR-011: INTMUX enable/disable performs unprotected read-modify-write [Medium / Plausible]

Files:

- `$CONTEST/board/contest_board/chip/rv1126b_irq.c:83`
- `$CONTEST/board/contest_board/chip/rv1126b_irq.c:88`
- `$CONTEST/board/contest_board/chip/rv1126b_irq.c:103`
- `$CONTEST/board/contest_board/chip/rv1126b_irq.c:108`

Evidence:

INTMUX enable bits are updated via `getreg32()` / bit operation / `putreg32()` with no critical section.

Recommendation:

If the hardware lacks atomic set/clear registers, protect INTMUX enable/disable RMW with `up_irq_save()` / `up_irq_restore()` and add range checks.

## Design observations

### DP-01: duplicated common RISC-V trap/CSR/timer mechanisms

The BSP currently hand-writes RISC-V trap save/restore, CSR helpers, atomic stubs, and timer handling. This may be necessary for early bring-up, but it increases divergence from common NuttX RISC-V mechanisms.

Files:

- `$CONTEST/board/contest_board/chip/rv1126b_head.S`
- `$CONTEST/board/contest_board/chip/chip.h:53`
- `$CONTEST/board/contest_board/chip/chip.h:58`
- `$CONTEST/board/contest_board/chip/chip.h:61`
- `$CONTEST/board/contest_board/chip/chip.h:66`

Recommendation:

Where practical, reuse `<arch/csr.h>`, common RISC-V interrupt constants, and common timer/trap helpers.

### DP-02: global UART device symbol can be static

File:

- `$CONTEST/board/contest_board/chip/rv1126b_serial.c:140`

`g_console_port` is a non-static global. If there is no external reference, it should be `static` to reduce symbol pollution.

## Recommended next implementation order

Choice A should be split into small reviewable commits.

1. Fix atomic builtins:
   - Add interrupt masking around all custom 32-bit atomic RMW operations.
   - Preserve expected-value update behavior for compare-exchange.
2. Fix IRQ namespace:
   - Define external IRQ numbers as `RISCV_IRQ_EXT + source`.
   - Convert between NuttX IRQ number and INTMUX source id inside controller code.
   - Update UART5 IRQ usage.
3. Fix timer dispatch:
   - Register machine timer through `irq_attach()`.
   - Dispatch timer through `riscv_doirq(RISCV_IRQ_MTIMER, regs)`.
   - Keep `nxsched_process_timer()` inside the actual timer ISR path.
4. Fix RV32 split MTIME/MTIMECMP access.
5. Fix stack/trap alignment and linker alignment.
6. Fix or explicitly gate CMake source-set mismatch.
7. Then handle P1 cleanup items:
   - `.text.start` KEEP / entry assertion;
   - clock failure reporting;
   - tick-rate config binding;
   - GPIO base duplication;
   - static global cleanup.

## Verification guidance after fixes

Do not run these automatically without user confirmation. Suggested staged verification:

1. Lightweight checks:

```bash
git -C "$CONTEST" diff --check origin/dev-ai-contest-2026...HEAD
git -C "$CONTEST" diff --stat origin/dev-ai-contest-2026...HEAD -- board/contest_board/
```

2. Classic openvela build from `$WORKSPACE`, after user confirms:

```bash
cd "$WORKSPACE"
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

3. SDK AMP packaging and board verification remain manual/user-driven:

- Replace `$OUT/rtt.bin` target by preserving the symlink and copying to `readlink -f "$OUT/rtt.bin"`.
- Repack AMP image.
- Flash only the intended AMP partition unless the user explicitly enters full `update.img` validation.
- Re-test NSH prompt, UART RX/TX, `help`, `uname -a`, and `ps`.

## Submission note

Until the blocker fixes are applied and verified, PR text should not claim:

- IRQ/timer paths are fully upstream-quality;
- external IRQ expansion is complete;
- CMake and classic Make are equivalent;
- DCache, RPMsg, or full `update.img` flashing are verified.
