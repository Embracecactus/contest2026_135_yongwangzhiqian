# Next-stage restore prompt: RV1126B P0/P1 BSP fixes before PR

Use this prompt after a context reset to continue the RV1126B HPMCU openvela / NuttX adaptation from the targeted code review result.

```text
We are continuing in the openvela 2026 contest workspace. Goal: implement Choice A from the 2026-07-15 targeted review — fix P0/P1 RV1126B board/BSP blockers before creating/finalizing the PR.

Strict interaction rules:

- Use Socratic communication: if scope or intent is unclear, stop and ask me before expanding.
- Do not proactively load skills unless I explicitly ask. In particular, do not run build/flash/SDK packaging skills automatically.
- Do not use Workflow for this project.
- Keep the main model focused on planning/review; delegate repetitive inspection or mechanical verification to normal Agents only when practical and only within the requested scope.
- Only modify `$CONTEST` team repository. Do not directly modify outer official checkouts such as `$WORKSPACE/nuttx/`, `$WORKSPACE/apps/`, `$WORKSPACE/packages/`, `$WORKSPACE/vendor/`, or `$WORKSPACE/frameworks/`.
- Before using CodeGraph, run `codegraph sync`; then call CodeGraph with `projectPath: "$WORKSPACE"`. If sync or CodeGraph is unavailable, state the fallback before using manual reads/search.
- Do not run long builds, SDK packaging, flashing, push, PR creation, delete, reset, or overwrite operations without confirmation.
- Documentation must not contain personal absolute paths. Use `$WORKSPACE`, `$CONTEST`, `$SDK`, `$OUT`, `$FW`.
- README should remain close to the official template until I explicitly say we are entering the final project README stage.

Path conventions:

```bash
export WORKSPACE=/absolute/path/to/open-vela
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export SDK=/absolute/path/to/rv1126b-sdk
export OUT="$SDK/output"
export FW="$SDK/output/firmware"
```

Current repository context:

- Team repo: `$CONTEST`
- Branch: `submit-rv1126b-nsh-baseline`
- Tracking remote: `fork/submit-rv1126b-nsh-baseline`
- Latest known pushed commit before the P0/P1 fix stage: `9b5f438 logs: sync latest session`
- Existing PR target:
  - base repository: `open-vela/contest2026_135_yongwangzhiqian`
  - base branch: `dev-ai-contest-2026`
  - compare repository: `Embracecactus/contest2026_135_yongwangzhiqian`
  - compare branch: `submit-rv1126b-nsh-baseline`

Start-of-session checks:

```bash
git -C "$CONTEST" status --short --branch
git -C "$CONTEST" log -5 --oneline
git -C "$CONTEST" status --short -- logs docs
python3 "$WORKSPACE/.claude/skills/contest-log-collector/tools/validate-log.py" "$CONTEST/logs/"
```

If new logs were appended, ask me before committing/pushing them.

Key review document to read first:

```text
$CONTEST/docs/review/2026-07-15-rv1126b-board-targeted-code-review.md
```

The strict review result was approximately:

```text
Score: 52/100
Conclusion: REJECT / at least NEEDS_FIX
Design health: 4/10, C
```

Reason: current board evidence is useful, but code-quality blockers exist in atomic builtins, IRQ namespace, timer dispatch, RV32 MTIME split access, trap-frame alignment, and Make/CMake source-set consistency.

Important already verified board facts:

1. 2026-07-15 15:58 PROCFS / `ps` recheck passed:
   - `nuttx.bin` / `$OUT/rtt.bin` hash: `b428e9d2a259addc72574f2080c2038b9a948befa0fab29a48180e1e27619b43`
   - `nuttx.bin` / `$OUT/rtt.bin` size: 98464 bytes
   - `$FW/amp.img` hash: `d390e0f738507ed58d59770e3a2dd9ee236f399f95134920dcc8336f69982835`
   - `$FW/amp.img` size: 103424 bytes
   - Board NSH passed: `ps`, `uname -a`, UART RX/TX.
2. Correct `$OUT/rtt.bin` replacement preserves symlink and copies to target:

```bash
RTT_TARGET="$(readlink -f "$OUT/rtt.bin")"
cp -av "$WORKSPACE/nuttx/nuttx.bin" "$RTT_TARGET"
```

3. Full `update.img` flashing has NOT been verified. Do not claim it.
4. RPMsg / Linux A-core to HPMCU communication is NOT complete. Do not claim it.
5. CMake/classic Make equivalence is NOT verified. Do not claim it.
6. DCache enablement is NOT complete/verified. Do not claim it.

Primary P0/P1 fix list from review:

P0 / blockers:

1. Fix custom GCC atomic builtins in:
   - `$CONTEST/board/contest_board/chip/rv1126b_atomic.c`
   - Current issue: plain load/modify/store in `__atomic_*_4`; no AMO, no interrupt masking, no barriers.
   - Minimum acceptable fix: use `up_irq_save()` / `up_irq_restore()` around every 32-bit RMW operation, preserve compare-exchange expected update semantics, and add necessary compiler barrier semantics.

2. Fix external IRQ namespace:
   - `$CONTEST/board/contest_board/chip/include/irq.h`
   - `$CONTEST/board/contest_board/chip/rv1126b_irq.c`
   - `$CONTEST/board/contest_board/chip/rv1126b_irq_dispatch.c`
   - `$CONTEST/board/contest_board/chip/rv1126b_serial.c`
   - Current issue: `NR_IRQS` is based on `RISCV_IRQ_EXT + NR_INTMUX_IRQS`, but dispatch and UART attach use raw INTMUX source ids.
   - Target convention: NuttX IRQ number = `RISCV_IRQ_EXT + intmux_source_id`; controller code converts back to source id internally.

3. Fix timer dispatch path:
   - `$CONTEST/board/contest_board/chip/rv1126b_irq_dispatch.c`
   - `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c`
   - Current issue: machine timer trap directly calls `up_timer_int()` and `nxsched_process_timer()`, bypassing common `riscv_doirq()` bookkeeping.
   - Target: timer should be registered with `irq_attach(RISCV_IRQ_MTIMER, ...)` or equivalent, and timer traps should be dispatched through `riscv_doirq(RISCV_IRQ_MTIMER, regs)`.

4. Fix RV32 MTIME / MTIMECMP split register access:
   - `$CONTEST/board/contest_board/chip/rv1126b_timerisr.c`
   - Current issue: MTIME read is high+low only; MTIMECMP write is low then high.
   - Target: high-low-high stable read; safe compare write sequence such as high=max, low, high.

P1 / strongly recommended before PR:

5. Fix trap frame and stack alignment:
   - `$CONTEST/board/contest_board/chip/rv1126b_head.S`
   - `$CONTEST/board/contest_board/scripts/ld.script`
   - Current issue: `_ebss` only `ALIGN(4)`, and `XCPTCONTEXT_SIZE = 33 * 4 = 132`, not 16-byte aligned before calling C.
   - Target: keep RISC-V 16-byte stack alignment at C call boundaries. Align `_ebss` / heap start and round trap frame size to 16.

6. Fix Make/CMake source set mismatch:
   - `$CONTEST/board/contest_board/chip/Make.defs`
   - `$CONTEST/board/contest_board/chip/CMakeLists.txt`
   - Reference: `$WORKSPACE/nuttx/arch/risc-v/src/common/CMakeLists.txt`
   - Current issue: Make excludes `riscv_mtimer.c`; CMake common path still includes it.
   - Target: either make CMake exclude the common mtimer source too, migrate to common mtimer design, or clearly mark CMake as unsupported/unverified without implying parity.

Additional P2 items to handle after P0/P1 or document as known limitations:

- Raw binary entry should explicitly keep `.text.start` first in linker script.
- `rv1126b_clockconfig()` should not silently continue after GPLL lock verification failure.
- Timer tick should derive from `CONFIG_USEC_PER_TICK` or the appropriate NuttX tick-rate constant instead of literal `100`.
- GPIO3 base address differs between `rv1126b_memorymap.h` and `rv1126b_gpio.h`; verify against SDK/TRM and unify.
- `g_console_port` in `rv1126b_serial.c` can likely become `static` if not externally referenced.

Suggested implementation strategy:

1. First discuss and confirm whether to implement all P0/P1 in one pass or split commits.
2. Before editing, run:

```bash
codegraph sync
```

Then use CodeGraph to inspect the exact symbols/files before changes.

3. Prefer small commits:
   - Commit 1: atomic and IRQ namespace fixes.
   - Commit 2: timer dispatch + MTIME split access.
   - Commit 3: trap/linker alignment + Make/CMake consistency.
   - Optional Commit 4: P2 cleanup/docs.

4. Do not edit outer official trees. If a fix truly requires changing `$WORKSPACE/nuttx/`, stop and ask me about cross-repo PR handling instead.

After implementation, suggested checks, but ask me before long-running builds:

Lightweight checks:

```bash
git -C "$CONTEST" diff --check origin/dev-ai-contest-2026...HEAD
git -C "$CONTEST" diff --stat origin/dev-ai-contest-2026...HEAD -- board/contest_board/
python3 "$WORKSPACE/.claude/skills/contest-log-collector/tools/validate-log.py" "$CONTEST/logs/"
```

Build only after confirmation:

```bash
cd "$WORKSPACE"
./build.sh vendor/openvela/boards/contest2026_135_board/configs/nsh -j8
```

Board/SDK steps should be provided as instructions for me to run unless I explicitly authorize you to run them:

- Preserve `$OUT/rtt.bin` symlink and replace `readlink -f "$OUT/rtt.bin"` target.
- Repack AMP image.
- Flash intended AMP partition only unless full `update.img` validation is explicitly requested.
- Re-test NSH prompt, UART RX/TX, `help`, `uname -a`, and `ps`.

PR description after fixes should still avoid claiming:

- full `update.img` flashing passed;
- RPMsg/Linux A-core communication completed;
- CMake equivalence unless verified;
- DCache enabled/verified.

Start by reading the review document and asking me which fix split I want, unless I explicitly say to proceed with all P0/P1.
```
