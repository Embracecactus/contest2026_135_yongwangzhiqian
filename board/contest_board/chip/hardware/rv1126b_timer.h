/****************************************************************************
 * NuttX - RV1126B BSP
 ****************************************************************************/

#ifndef __BOARD_HARDWARE_RV1126B_TIMER_H
#define __BOARD_HARDWARE_RV1126B_TIMER_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* MTIME (Core Local Timer / CTIMER) base address */

#define RV1126B_CTIMER_BASE           0xFF1E0000UL

/* MTIME register offsets (RISC-V SCR1 compatible) */

#define RV1126B_CTIMER_MTIME_CTRL     0x0000  /* Timer Control Register */
#define RV1126B_CTIMER_MTIME_DIV      0x0004  /* Timer Divider Register */
#define RV1126B_CTIMER_MTIME          0x0008  /* Timer Counter Low 32 bits */
#define RV1126B_CTIMER_MTIMEH         0x000C  /* Timer Counter High 32 bits */
#define RV1126B_CTIMER_MTIMECMP       0x0010  /* Timer Compare Low 32 bits */
#define RV1126B_CTIMER_MTIMECMPH      0x0014  /* Timer Compare High 32 bits */

/* MTIME_CTRL bit definitions */

#define RV1126B_MTIME_CTRL_EN         (1 << 0)  /* Timer Enable */
#define RV1126B_MTIME_CTRL_CLKSRC     (1 << 1)  /* Clock Source Select */
#define RV1126B_MTIME_CTRL_WR_MASK    0x03      /* Write mask for control bits */

/* MTIME_DIV bit definitions */

#define RV1126B_MTIME_DIV_WR_MASK     0x3FF     /* Write mask for divider bits */

/* Default timer configuration values */

#define RV1126B_MTIME_DEFAULT_DIV     1000      /* Default divider for 1ms tick */
#define RV1126B_MTIME_FREQ            396000000 /* SCR1 core frequency (396 MHz) */

/* RISC-V timer interrupt cause */

#define RV1126B_MCAUSE_TMR_IRQ        (1 << 31 | 7)

#endif /* __BOARD_HARDWARE_RV1126B_TIMER_H */
