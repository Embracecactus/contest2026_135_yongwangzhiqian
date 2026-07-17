---
name: hardware-context
description: >
  Index and query hardware adaptation reference materials (CMSIS headers, HAL examples,
  Linux DTS/drivers, RTOS references, datasheets). Provides structured context to Agents
  during code implementation. Use when: 编码参考, 查找资料, SDK 参考, hardware context,
  硬件资料, CMSIS, HAL, DTS, datasheet, 参考手册.
---

# Hardware Context

Indexes and queries hardware adaptation reference materials for Agent coding sessions.

## When to Use

- Agent needs to find register definitions, IRQ numbers, or hardware addresses
- Agent needs to see how Linux/RTOS implements similar functionality
- Agent needs to verify hardware assumptions against SDK documentation
- User says "查找资料" / "SDK 参考" / "hardware context" / "编码参考"

## Prerequisites

- SDK path known (`$SDK` set)
- SDK directory accessible

## Core Concept

When adapting new hardware, Agents need to reference:

1. **CMSIS headers** — register definitions, IRQ numbers, base addresses
2. **HAL examples** — how the vendor implements hardware operations
3. **Linux DTS** — hardware description, address mappings, interrupt routing
4. **Linux drivers** — how Linux implements the same functionality
5. **RTOS references** — how the vendor's RTOS implements similar features
6. **Documentation** — integration guides, known issues, workarounds

These are scattered across the SDK. This Skill creates a structured index and provides query tools.

## Steps

### 1. Scan SDK (Run Once)

Generate a structured index of all reference materials:

```bash
bash <skill_dir>/scripts/scan-sdk.sh "$SDK" > "$CONTEST/docs/hardware-context-index.md"
```

This creates a Markdown index with:
- All CMSIS headers with key defines (base addresses, IRQ numbers)
- All HAL source files with function signatures
- All DTS files with hardware node descriptions
- All Linux drivers with probe/init functions
- All RTOS references with init/task functions

### 2. Query Context (During Coding)

When an Agent needs to implement a feature (e.g., "mailbox driver"), query the index:

```bash
bash <skill_dir>/scripts/query-context.sh "$CONTEST/docs/hardware-context-index.md" "mailbox"
```

Output: all mailbox-related reference files with paths and key content.

### 3. Verify Against SDK

When an Agent makes hardware assumptions, verify against SDK:

```bash
bash <skill_dir>/scripts/diff-versions.sh "$SDK" "rv1126b.h" "current"
```

Output: confirms the file version and shows any differences from expected.

## Query Patterns

### Pattern 1: "I need to implement X"

```bash
# Find all references related to X
bash scripts/query-context.sh index.md "X"

# Example: implementing mailbox
bash scripts/query-context.sh index.md "mailbox"
# → CMSIS: rv1126b.h (MBOX_BASE, IRQ numbers)
# → HAL: hal_mbox.c (HAL_MBOX_SendMsg)
# → DTS: rv1126b.dtsi (mailbox nodes)
# → Linux: rockchip-mailbox.c (probe, send_data)
# → RTOS: (if exists)
```

### Pattern 2: "What is the register address for X?"

```bash
# Search CMSIS headers
grep -r "MBOX.*BASE\|MBOX.*IRQ" "$SDK/hal/lib/CMSIS/" | head -20
```

### Pattern 3: "How does Linux implement X?"

```bash
# Find Linux driver
find "$SDK/kernel-6.1/drivers" -name "*X*" -type f
# Read probe/init functions
grep -n "probe\|init\|send\|receive" "$SDK/kernel-6.1/drivers/X.c" | head -20
```

### Pattern 4: "What DTS nodes are related to X?"

```bash
# Search DTS files
grep -rn "X\|compatible.*X" "$SDK/kernel-6.1/arch/arm/boot/dts/" | head -20
```

## Output Format

The index file (`hardware-context-index.md`) has this structure:

```markdown
# Hardware Context Index — <chip_name>

## CMSIS Headers
| File | Key Defines | Path |
|------|-------------|------|
| rv1126b.h | MBOX4_BASE=0x20D00000, MBOX7_BASE=0x20D30000 | hal/lib/CMSIS/... |

## HAL Examples
| File | Key Functions | Path |
|------|---------------|------|
| hal_mbox.c | HAL_MBOX_Init, HAL_MBOX_SendMsg | hal/lib/hal/src/... |

## Linux DTS
| File | Key Nodes | Path |
|------|-----------|------|
| rv1126b.dtsi | mailbox@20d00000, mailbox@20d30000 | kernel-6.1/arch/arm/boot/dts/... |

## Linux Drivers
| File | Key Functions | Path |
|------|---------------|------|
| rockchip-mailbox.c | probe, send_data, rx_callback | kernel-6.1/drivers/mailbox/... |

## RTOS References
| File | Key Functions | Path |
|------|---------------|------|
| rpmsg_core.c | rpmsg_lite_remote_init | rtos/bsp/rockchip/... |
```

## Rules

- Index should be regenerated when SDK is updated
- Always reference the index file, not raw SDK paths (index includes version info)
- When multiple versions exist, note which version is being used
- Cross-reference with CodeGraph for code-level understanding

## References

- `references/rv1126b-context-example.md` — RV1126B hardware context instance
