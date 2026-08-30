# BK7258 Vendor Bootloader Comparison（2026-07-31 Snapshot）

> This is a dated comparison of two retained binary samples, not a statement of the
> current boot, trust, or download architecture. Current entry points are the
> [BK7258 platform documentation](../README_EN.md) and the
> [build, flash, and debug SOP](../nuttx-port/bk7258-build-flash-debug-sop.md).
>
> **2026-07-31 update:** the original BK row used an older SDK sample. The current
> support SDK v3.1.1.9 official normal bootloader is 52,352 bytes, SHA-256
> `105161bb603eedafbffcb5efb8f7c06a0c8503e42ba4da46490c2c21ed813de6`,
> version `bc31115`. Ghidra review also disproved the old implication that
> header check plus branch is the complete cold-reset contract.

## Targets

| Bootloader | Size | SHA-256 (first 8) | Source |
|---|---|---|---|
| Tuya T5-AI | 65,536 B | `21563b36` | retained sample `t5ai_bootloader.bin` |
| BK official v3.1.1.9 | 52,352 B | `105161bb` | support SDK `bk_avdk_smp-release-v3.1.1.9` normal bootloader |

## SDK Cross-Reference (preferred over pure disassembly)

From SDK startup source (`ap/components/bk_startup/system_main.c`, `ap/components/cmsis/.../smp/startup_cpuN.c`):

- **CRC is hardware-handled**: `get_partition_addr()` confirms flash uses 32-byte data + 2-byte CRC16; the flash controller decodes transparently. CPU sees logical addresses. Bootloader does NOT do software CRC decode.
- **Multi-core boot via sys registers**: `reset_cpu1_core()` → `sys_drv_set_cpu1_boot_address_offset(offset >> 8)` + `sys_drv_set_cpu1_reset(start_flag)`. CPU1/2 are started by the running core writing sys-ctrl registers, not by a bootloader chain.
- **App contract**: vector table at app base — `[0x000]`=MSP (SRAM range), `[0x004]`=Reset_Handler (Thumb), magic header at a fixed offset.

## Dimension 3: Startup / Linking Findings

| # | Severity | Offset | Tuya | BK official | Note |
|---|----------|--------|------|-------------|------|
| 1 | Correction | logical `0x100` / packed physical `0x110` | magic at packed physical `0x110` | magic at logical `0x100` | Same logical location; the original comparison mixed physical and logical views |
| 2 | Info | `0x000` | MSP `0x28030000` | MSP `0x28030000` | Same initial SP (SRAM) |
| 3 | Info | `0x004` | Reset `0x020101C1` | Reset `0x020101C1` | Same Reset_Handler (Thumb) |
| 4 | Info | representation | CRC-expanded physical bytes | logical bytes | Raw offsets cannot be compared without stating the representation |
| 5 | Info | version | `116253e` @0x120 | `bc31115` @0x110 | Different build versions |
| 6 | Info | `0x020-0x03F` | extra vectors + `0xdddb0000` | zeros | Tuya extends vector table |

## Dimension 2: Hardware Register Findings

Both bootloaders share the same code core (UART1 bring-up, WDT feed, SWD enable) — confirmed
by identical instruction sequences at the code entry. Register constants match the SDK
(`reg_base.h`):
- `0x44000600` AON_WDT_CTRL, `0x44800008/10` APB WDT — key sequence `0x5A/0xA5`
- `0x440100C0/C4` GPIO peripheral mode, `0x44000400+` GPIO cfg
- `0x45830008/10/1C` UART1 global_ctrl/config/fifo
- `0x440100E0` SWD/debug, `0xE000ED08` SCB_VTOR

No material register discrepancies were found. The apparent magic-offset discrepancy was
a physical-versus-logical representation difference, not a different logical header contract.

## Conclusion

1. **Shared lineage and many shared primitives** are visible, but this comparison does not
   prove full semantic equivalence between the Tuya and current v3.1.1.9 binaries.
2. Header/magic layout is one important adaptation point, not the only cold-reset contract.
   Cache/MPU, WDT, UART, runtime initialization and app handoff state are also material.
3. **CRC does not need bootloader handling** — flash controller does it in hardware. This overturns the earlier assumption that a custom bootloader must do CRC expansion.

## Implication for Custom Bootloader

- A custom bootloader must preserve its chosen partition/header contract and also leave
  cache, MPU, watchdog, core-power, UART/runtime and interrupt state in a deterministic
  form before VTOR/MSP/branch.
- The CRC-expanded app image format is a **build/packaging** concern, not a bootloader concern.
- The minimal bootloader in this snapshot was not functionally complete for repeatable cold
  reset. The dated Tier-1 work added clean-room reset/cache/MPU/core hardening while
  deliberately not copying official RBL/OTA/download protocols. See the
  [dated synthesis](full-reverse-synthesis.md).
