---
name: hardware-review-gate
description: >
  Four-dimensional static review gate for BSP/board/chip code before submission.
  Reviews concurrency safety, hardware registers, startup/linking, and build consistency.
  Supports two modes: source-code review (default) and binary/reverse-engineering review.
  Generates Blocker/High/Medium/Low report with evidence. Use when: reviewing hardware
  adaptation code, 提交前审查, hardware review, review gate, 审查代码, 驱动审查,
  逆向分析, bootloader review, 二进制审查.
---

# Hardware Review Gate

Four-dimensional static review for BSP/board/chip code. Complements `driver-code-reviewer`
(which covers 59 NuttX driver patterns) with board/chip-level concerns.

## When to Use

- User says "审查代码" / "review gate" / "提交前审查" / "hardware review"
- Before creating a PR for board/BSP changes
- After implementing a new driver or peripheral support
- User says "逆向分析" / "bootloader review" / "二进制审查" → binary mode

## Prerequisites

- CodeGraph index available (`codegraph sync` completed)
- For source mode: changes exist in the target repository (git diff non-empty)
- For binary mode: target binary (.bin) + SDK source as cross-reference

## Mode Detection

First determine the review mode:

- **Source mode** (default): target is C/assembly source code in a git repo
- **Binary mode**: target is a `.bin`/`.elf` (e.g., vendor bootloader, ROM dump)

If the target is a binary, `collect-diff-stats.sh` is NOT useful (it reports unrelated
repo diffs). Instead, see **Binary / Reverse-Engineering Mode** below.

## Four Review Dimensions

### Dimension 1: Concurrency Safety

Check for:
- Atomic operations on shared state (spin_lock_irqsave / up_irq_save)
- IRQ protection: are ISR-accessed fields protected at all access points?
- Memory barriers: UP_DSB/UP_DMB after MMIO writes, before callback dispatch
- Lock ordering: are multiple locks always acquired in the same order?
- Race windows: check-then-act patterns without locks

### Dimension 2: Hardware Registers

Check for:
- MMIO semantics: volatile access, Device memory ordering
- Read-Modify-Write (RMW): is it safe without locks?
- Write-1-to-Clear (W1C): are STATUS bits cleared correctly (read then W1C)?
- Split-access: 32-bit writes to 64-bit registers, CMD-then-DATA sequences
- Hiword write-enable: are upper bits used as write masks correctly?

### Dimension 3: Startup & Linking

Check for:
- Trap frame alignment (RISC-V: 4-byte instruction, stack alignment)
- Entry point: is the init function called at the right time (board_late_initialize)?
- Section layout: NOLOAD sections, MEMORY regions, ASSERTs
- Symbol resolution: are all init functions linked when CONFIG is enabled?
- Rollback: does init failure clean up correctly (reverse order)?

### Dimension 4: Build Consistency

Check for:
- Make.defs: are new source files conditionally included?
- Kconfig: do symbols have correct dependencies and defaults?
- CMakeLists.txt: is it in sync with Make.defs? (often intentionally disabled)
- defconfig vs .config: are savedefconfig results consistent?
- Linker constants: do MEMORY regions match Kconfig defaults and C compile-time checks?

## Steps

### 1. Collect Change Scope

```bash
bash <skill_dir>/scripts/collect-diff-stats.sh <repo_path>
```

### 2. Run Four Parallel Reviews

Launch one Agent per dimension. Each Agent:
- Reads the changed files (via CodeGraph when possible)
- Checks against the dimension's checklist
- Outputs findings as: `SEVERITY: file:line — description — evidence`

### 3. Adversarial Verification

For each finding, a second Agent tries to **refute** it:
- If refuted → discard
- If confirmed → keep with severity
- If uncertain → mark as NEEDS_RUNTIME

### 4. Synthesize Report

Combine all confirmed findings into a single report:

```markdown
| # | Severity | File:Line | Summary | Evidence | Recommendation |
|---|----------|-----------|---------|----------|----------------|
```

## Output Format

```markdown
# Hardware Review Gate Report

**Date**: YYYY-MM-DD
**Target**: <repo_path>
**Scope**: <files changed>

## Findings

### Blocker
(none or list)

### High
(none or list)

### Medium
(none or list)

### Low
(none or list)

## Verification Summary

| Dimension | Findings | Confirmed | Refuted |
|-----------|----------|-----------|---------|
| Concurrency | N | N | N |
| Registers | N | N | N |
| Startup/Linking | N | N | N |
| Build Consistency | N | N | N |
```

## Rules

- Only report findings backed by code evidence (file:line + actual code)
- Distinguish "code defect" from "needs runtime verification"
- If no findings in a dimension, explicitly state "PASS"
- Do not report style preferences as defects
- Cross-reference against SDK/datasheet when available

## Binary / Reverse-Engineering Mode

When the target is a vendor binary (bootloader `.bin`, ROM dump), the four dimensions
do NOT all apply. Use this reduced process instead.

### Applicable Dimensions

| Dimension | Applies? | Method |
|-----------|----------|--------|
| 1. Concurrency | No | Cannot reason from binary without full disassembly; skip |
| 2. Hardware Registers | **Yes** | Extract MMIO constant addresses from the binary (`xxd`/`objdump`), cross-check against SDK headers |
| 3. Startup/Linking | **Yes** | Parse vector table (offset 0x000: MSP, 0x004: Reset_Handler), entry point, magic/header fields |
| 4. Build Consistency | No | Binary has no build system; skip |

### Preferred Method: Read SDK Source, Not Pure Disassembly

Pure binary reverse-engineering is slow and error-prone. **Prefer reading the SDK's app-side
startup source to infer what the bootloader does**, because the app's Reset_Handler and boot
helpers document the contract the bootloader must satisfy.

For a vendor bootloader, locate in the SDK (use `hardware-context` scan):
- `startup_cpuN.c` / `startup_*.S` — vector table, Reset_Handler, SystemInit
- `system_main.c` — `start_cpuN_core()`, `reset_cpuN_core()`, boot address setup
- `*_reg.h` / `reg_base.h` — register bases to validate MMIO constants in the binary
- Linker scripts (`.ld`) — memory layout, section placement

### Binary Analysis Steps

1. `xxd -l 512 <binary>` — header: vector table, magic, version string
2. Compare two binaries byte-by-byte (`cmp`, `xxd` diff) if doing a differential review
3. Extract MMIO constants: `xxd <binary> | grep -E 'address patterns'`, validate against `reg_base.h`
4. Parse vector table: offset 0x000 = initial MSP, 0x004 = Reset_Handler (Thumb bit)
5. Locate magic/header (e.g., `BK7236\x10\x00`) and note its offset
6. Cross-reference findings with SDK startup source

### Output (Binary Mode)

Same severity report, but findings are anchored to **binary offset** (e.g., `0x110`) and
cross-referenced to SDK source (e.g., `startup_cpu0.c:360`). Mark dimension 1 and 4 as N/A.


## References

- `references/rv1126b-review-example.md` — P2-A four-dimensional review instance
- `references/bsp-review-dimensions.md` — Dimension definitions and checklists
