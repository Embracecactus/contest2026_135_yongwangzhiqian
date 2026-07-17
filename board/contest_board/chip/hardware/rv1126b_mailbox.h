/****************************************************************************
 * NuttX - RV1126B BSP
 ****************************************************************************/

#ifndef __BOARD_HARDWARE_RV1126B_MAILBOX_H
#define __BOARD_HARDWARE_RV1126B_MAILBOX_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* RV1126B Mailbox V2 register layout (single-channel per instance)
 *
 * Source: SDK hal/lib/CMSIS/Device/RV1126B/Include/rv1126b.h (MBOX_REG)
 *
 * Each mailbox instance has one A2B and one B2A channel with CMD+DATA
 * registers.  The Linux rockchip-mailbox.c V2 driver uses CMD/DAT pairs.
 *
 * Register offsets (relative to instance base):
 */

#define RV1126B_MBOX_A2B_INTEN_OFFSET  0x00  /* A2B interrupt enable */
#define RV1126B_MBOX_A2B_STATUS_OFFSET 0x04  /* A2B interrupt status (W1C) */
#define RV1126B_MBOX_A2B_CMD_OFFSET    0x08  /* A2B command register */
#define RV1126B_MBOX_A2B_DATA_OFFSET   0x0C  /* A2B data register */
#define RV1126B_MBOX_B2A_INTEN_OFFSET  0x10  /* B2A interrupt enable */
#define RV1126B_MBOX_B2A_STATUS_OFFSET 0x14  /* B2A interrupt status (W1C) */
#define RV1126B_MBOX_B2A_CMD_OFFSET    0x18  /* B2A command register */
#define RV1126B_MBOX_B2A_DATA_OFFSET   0x1C  /* B2A data register */

/* INTEN/STATUS bit definitions
 *
 * V2.0 single-channel register set uses hiword write-enable scheme:
 * bits 16-31 are write-enable mask for bits 0-15.  To set bit N, write
 * (1 << (N + 16)) | (1 << N).  To clear bit N, write (1 << (N + 16)) | 0.
 */

#define RV1126B_MBOX_INT_TX_DONE       (1u << 0)   /* TX done */
#define RV1126B_MBOX_INT_RX_DONE       (1u << 1)   /* RX done */
#define RV1126B_MBOX_INT_TRIG_MODE     (1u << 8)   /* Trigger mode: CMD+DATA */

/* Hiword write-enable helpers */

#define RV1126B_MBOX_INTEN_SET(bit)    ((1u << ((bit) + 16)) | (1u << (bit)))
#define RV1126B_MBOX_INTEN_CLR(bit)    ((1u << ((bit) + 16)) | 0)

/* Pre-composed enable/disable for common operations */

#define RV1126B_MBOX_A2B_INTEN_ENABLE   RV1126B_MBOX_INTEN_SET(0)
                                            /* Enable A2B TX_DONE IRQ (bit0) */
#define RV1126B_MBOX_A2B_INTEN_DISABLE  RV1126B_MBOX_INTEN_CLR(0)
                                            /* Disable A2B TX_DONE IRQ */
#define RV1126B_MBOX_B2A_TRIGMODE_SET   RV1126B_MBOX_INTEN_SET(8)
                                            /* Enable B2A TRIG_MODE (bit8) */
#define RV1126B_MBOX_B2A_TRIGMODE_CLR   RV1126B_MBOX_INTEN_CLR(8)
                                            /* Disable B2A TRIG_MODE */

/* AMP instance assignment (verified from SDK CMSIS soc.h):
 *
 * HPMCU_MBOX0 = hardware MBOX4 (PMU domain, base 0x20D00000)
 * HPMCU_MBOX3 = hardware MBOX7 (PMU domain, base 0x20D30000)
 *
 * RX (Linux -> HPMCU): MBOX7, A2B direction
 *   Linux writes MBOX7 A2B_CMD/DATA; HPMCU reads A2B_CMD/DATA.
 *   Interrupt: HPMCU_MBOX3_BB_IRQn = 116
 *
 * TX vqid0 (HPMCU -> Linux): MBOX4, B2A direction
 *   HPMCU writes MBOX4 B2A_CMD/DATA; Linux reads on its side.
 *
 * TX vqid1 (HPMCU -> Linux): MBOX7, B2A direction
 *   HPMCU writes MBOX7 B2A_CMD/DATA; Linux reads on its side.
 *
 * TX ALL: both MBOX4 B2A and MBOX7 B2A.
 *
 * INTMUX sources:
 *   HPMCU_MBOX0_BB_IRQn = 113 (MBOX4 BB)
 *   HPMCU_MBOX3_BB_IRQn = 116 (MBOX7 BB)
 */

#define RV1126B_MBOX_RX_INST           7   /* MBOX7: Linux -> HPMCU (A2B) */
#define RV1126B_MBOX_TX0_INST          4   /* MBOX4: HPMCU -> Linux (B2A, vqid0) */
#define RV1126B_MBOX_TX1_INST          7   /* MBOX7: HPMCU -> Linux (B2A, vqid1) */

/* Mailbox instance base addresses are taken directly from
 * rv1126b_memorymap.h:
 *   RV1126B_MBOX4_BASE = 0x20D00000UL
 *   RV1126B_MBOX7_BASE = 0x20D30000UL
 *
 * No linear base-address formula is provided here because MBOX0-3
 * (0x205xxxxx series) and MBOX4-7 (0x20Dxxxxx series) are in
 * different address ranges.
 */

/* INTMUX source for HPMCU_MBOX3 BB (receive) interrupt.
 * Source: SDK CMSIS soc.h — HPMCU_MBOX3_BB_IRQn = 116
 */

#define RV1126B_MBOX3_BB_INTMUX_SOURCE  116

#endif /* __BOARD_HARDWARE_RV1126B_MAILBOX_H */
