/****************************************************************************
 * NuttX - RV1126B BSP
 ****************************************************************************/

#ifndef __BOARD_HARDWARE_RV1126B_INTMUX_H
#define __BOARD_HARDWARE_RV1126B_INTMUX_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* INTMUX base address */

#define RV1126B_INTMUX_BASE           0x20B40000UL

/* INTMUX register layout:
 *   Enable registers:  base + 0x00 + (group * 4)
 *   Status registers:  base + 0x80 + (group * 4)
 * Each group controls 32 interrupt sources.
 * The INTMUX has 8 groups (0-7), covering interrupts 0-255.
 */

#define RV1126B_INTMUX_ENABLE_OFFSET  0x0000
#define RV1126B_INTMUX_STATUS_OFFSET  0x0080
#define RV1126B_INTMUX_GROUP_STRIDE   0x0004
#define RV1126B_INTMUX_NGROUPS        8

/* INTMUX enable register addresses for each group */

#define RV1126B_INTMUX_ENABLE0        (RV1126B_INTMUX_BASE + 0x0000)
#define RV1126B_INTMUX_ENABLE1        (RV1126B_INTMUX_BASE + 0x0004)
#define RV1126B_INTMUX_ENABLE2        (RV1126B_INTMUX_BASE + 0x0008)
#define RV1126B_INTMUX_ENABLE3        (RV1126B_INTMUX_BASE + 0x000C)
#define RV1126B_INTMUX_ENABLE4        (RV1126B_INTMUX_BASE + 0x0010)
#define RV1126B_INTMUX_ENABLE5        (RV1126B_INTMUX_BASE + 0x0014)
#define RV1126B_INTMUX_ENABLE6        (RV1126B_INTMUX_BASE + 0x0018)
#define RV1126B_INTMUX_ENABLE7        (RV1126B_INTMUX_BASE + 0x001C)

/* INTMUX status register addresses for each group */

#define RV1126B_INTMUX_STATUS0        (RV1126B_INTMUX_BASE + 0x0080)
#define RV1126B_INTMUX_STATUS1        (RV1126B_INTMUX_BASE + 0x0084)
#define RV1126B_INTMUX_STATUS2        (RV1126B_INTMUX_BASE + 0x0088)
#define RV1126B_INTMUX_STATUS3        (RV1126B_INTMUX_BASE + 0x008C)
#define RV1126B_INTMUX_STATUS4        (RV1126B_INTMUX_BASE + 0x0090)
#define RV1126B_INTMUX_STATUS5        (RV1126B_INTMUX_BASE + 0x0094)
#define RV1126B_INTMUX_STATUS6        (RV1126B_INTMUX_BASE + 0x0098)
#define RV1126B_INTMUX_STATUS7        (RV1126B_INTMUX_BASE + 0x009C)

/* Helper macro to compute enable/status register address from IRQ number */

#define RV1126B_INTMUX_ENABLE(n)      (RV1126B_INTMUX_BASE + 0x0000 + (((n) >> 5) * 4))
#define RV1126B_INTMUX_STATUS(n)      (RV1126B_INTMUX_BASE + 0x0080 + (((n) >> 5) * 4))

#endif /* __BOARD_HARDWARE_RV1126B_INTMUX_H */
