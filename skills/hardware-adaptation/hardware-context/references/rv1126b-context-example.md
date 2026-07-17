# RV1126B Hardware Context Example

Real-world hardware context usage from the RV1126B openvela P2-A RPMsg/RPTun adaptation.

## Context Query: "mailbox"

When implementing the mailbox driver, the following references were needed:

### CMSIS Headers

| File | Key Defines | Path |
|------|-------------|------|
| rv1126b.h | MBOX4_BASE=0x20D00000, MBOX7_BASE=0x20D30000 | hal/lib/CMSIS/Device/RV1126B/Include/rv1126b.h |
| soc.h | HPMCU_MBOX3_BB_IRQn=116, HPMCU_MBOX3=MBOX7 | hal/lib/CMSIS/Device/RV1126B/Include/soc.h |

### HAL Examples

| File | Key Functions | Path |
|------|---------------|------|
| hal_mbox.c | HAL_MBOX_Init, HAL_MBOX_SendMsg, HAL_MBOX_RecvMsg | hal/lib/hal/src/hal_mbox.c |

### Linux DTS

| File | Key Nodes | Path |
|------|-----------|------|
| rv1126b.dtsi | mailbox@20d00000, mailbox@20d30000 | kernel-6.1/arch/arm/boot/dts/rockchip/rv1126b.dtsi |
| rv1126b-amp.dtsi | hpmcu_mbox0, hpmcu_mbox3, rpmsg-dma@48c4c000 | kernel-6.1/arch/arm/boot/dts/rockchip/rv1126b-amp.dtsi |

### Linux Drivers

| File | Key Functions | Path |
|------|---------------|------|
| rockchip-mailbox.c | probe, send_data, rx_callback, startup | kernel-6.1/drivers/mailbox/rockchip-mailbox.c |
| rockchip_rpmsg_mbox.c | probe, handshake, notify, rx/tx_callback | kernel-6.1/drivers/rpmsg/rockchip_rpmsg_mbox.c |

## How the References Were Used

1. **CMSIS headers** provided register base addresses and IRQ numbers
2. **HAL examples** showed how to use hiword write-enable for INTEN/TRIG_MODE
3. **Linux DTS** confirmed hardware node structure and interrupt routing
4. **Linux drivers** revealed the exact CMD/DATA protocol and handshake sequence
5. **Cross-referencing** ensured NuttX implementation matched Linux expectations

## Key Insight

The most valuable reference was `rockchip_rpmsg_mbox.c` — it showed:
- CMD=0x03 (link_id from DTS)
- DATA=0x524d5347 (RPMSG_MBOX_MAGIC)
- Two 32-bit writes (CMD then DATA)
- TRIG_MODE=1 (DATA write triggers hardware)

This saved hours of trial-and-error.
