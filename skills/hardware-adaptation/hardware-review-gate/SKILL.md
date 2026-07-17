---
name: hardware-review-gate
description: >
  Four-dimensional static review gate for BSP/board/chip code before submission.
  Reviews concurrency safety, hardware registers, startup/linking, and build consistency.
  Generates Blocker/High/Medium/Low report with evidence. Use when: reviewing hardware
  adaptation code, 提交前审查, hardware review, review gate, 审查代码, 驱动审查.
---

# Hardware Review Gate

Four-dimensional static review for BSP/board/chip code. Complements `driver-code-reviewer`
(which covers 59 NuttX driver patterns) with board/chip-level concerns.

## When to Use

- User says "审查代码" / "review gate" / "提交前审查" / "hardware review"
- Before creating a PR for board/BSP changes
- After implementing a new driver or peripheral support

## Prerequisites

- CodeGraph index available (`codegraph sync` completed)
- Changes exist in the target repository (git diff non-empty)

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

## References

- `references/rv1126b-review-example.md` — P2-A four-dimensional review instance
- `references/bsp-review-dimensions.md` — Dimension definitions and checklists
