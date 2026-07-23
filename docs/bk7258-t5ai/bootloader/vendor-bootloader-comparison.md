# BK7258 Vendor Bootloader Comparison (Binary Review)

> Generated via `hardware-review-gate` binary/reverse-engineering mode.
> Mode: dimension 2 (registers) + dimension 3 (startup/linking); dimensions 1 & 4 N/A.

## Targets

| Bootloader | Size | SHA-256 (first 8) | Source |
|---|---|---|---|
| Tuya T5-AI | 65,536 B | `21563b36` | `zephyr-bk7258-port/tools/t5ai_bootloader.bin` |
| BK official | 52,352 B | `53120001` | `bk_avdk_smp/cp/components/bk_libs/bk7258/bootloader/normal_bootloader/bootloader.bin` |

## SDK Cross-Reference (preferred over pure disassembly)

From SDK startup source (`ap/components/bk_startup/system_main.c`, `ap/components/cmsis/.../smp/startup_cpuN.c`):

- **CRC is hardware-handled**: `get_partition_addr()` confirms flash uses 32-byte data + 2-byte CRC16; the flash controller decodes transparently. CPU sees logical addresses. Bootloader does NOT do software CRC decode.
- **Multi-core boot via sys registers**: `reset_cpu1_core()` → `sys_drv_set_cpu1_boot_address_offset(offset >> 8)` + `sys_drv_set_cpu1_reset(start_flag)`. CPU1/2 are started by the running core writing sys-ctrl registers, not by a bootloader chain.
- **App contract**: vector table at app base — `[0x000]`=MSP (SRAM range), `[0x004]`=Reset_Handler (Thumb), magic header at a fixed offset.

## Dimension 3: Startup / Linking Findings

| # | Severity | Offset | Tuya | BK official | Note |
|---|----------|--------|------|-------------|------|
| 1 | **Key diff** | `0x100` | `00..00 8029` (partition ptr) | `BK7236\x10\x00` (magic) | **Magic offset differs**: Tuya 0x110, BK 0x100 |
| 2 | Info | `0x000` | MSP `0x28030000` | MSP `0x28030000` | Same initial SP (SRAM) |
| 3 | Info | `0x004` | Reset `0x020101C1` | Reset `0x020101C1` | Same Reset_Handler (Thumb) |
| 4 | Info | `0x110/0x100` | magic `BK7236\x10\x00` | magic `BK7236\x10\x00` | Same magic value, different offset |
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

No register discrepancies found between the two beyond the header-layout difference.

## Conclusion

1. **Same code lineage**: Tuya bootloader = BK official core + Tuya-specific header extension (partition pointers, extra vectors). Code section is near-identical.
2. **Only the magic offset differs** (0x110 vs 0x100) — this is the one adaptation point for app images.
3. **CRC does not need bootloader handling** — flash controller does it in hardware. This overturns the earlier assumption that a custom bootloader must do CRC expansion.

## Implication for Custom Bootloader

- A custom bootloader only needs: header check (MSP/Reset/magic) → VTOR → MSP → branch.
- The CRC-expanded app image format is a **build/packaging** concern, not a bootloader concern.
- The existing minimal bootloader (`bk7236_min_bl.S`) is already functionally complete; it does not need CRC logic added.
