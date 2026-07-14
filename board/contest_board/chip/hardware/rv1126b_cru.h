/****************************************************************************
 * NuttX - RV1126B BSP
 ****************************************************************************/

#ifndef __BOARD_HARDWARE_RV1126B_CRU_H
#define __BOARD_HARDWARE_RV1126B_CRU_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* CRU register base addresses */

#define RV1126B_TOPCRU_BASE           0x20000000UL
#define RV1126B_BUSCRU_BASE           0x20010000UL
#define RV1126B_PERICRU_BASE          0x20020000UL
#define RV1126B_CORECRU_BASE          0x20030000UL
#define RV1126B_PMUCRU_BASE           0x20040000UL

/* CRU register offset helpers (bank-based layout) */

#define RV1126B_CRU_CLKSEL_CON(n)     (0x0300 + ((n) * 4))
#define RV1126B_CRU_GATE_CON(n)       (0x0800 + ((n) * 4))
#define RV1126B_CRU_SOFTRST_CON(n)    (0x0A00 + ((n) * 4))

/* ======================= UART Clock Gates ======================= */
/* TOPCRU UART source clock gates (GATE_CON01, offset 0x804) */

#define RV1126B_CLK_UART_FRAC_MATRIX0_GATE   (1 << 3)
#define RV1126B_CLK_UART_FRAC_MATRIX1_GATE   (1 << 4)
#define RV1126B_SCLK_UART0_SRC_GATE          (1 << 9)
#define RV1126B_SCLK_UART1_SRC_GATE          (1 << 10)
#define RV1126B_SCLK_UART2_SRC_GATE          (1 << 11)
#define RV1126B_SCLK_UART3_SRC_GATE          (1 << 12)
#define RV1126B_SCLK_UART4_SRC_GATE          (1 << 13)
#define RV1126B_SCLK_UART5_SRC_GATE          (1 << 14)

/* TOPCRU UART source clock gates (GATE_CON02, offset 0x808) */

#define RV1126B_SCLK_UART6_SRC_GATE          (1 << 0)
#define RV1126B_SCLK_UART7_SRC_GATE          (1 << 1)

/* BUSCRU UART peripheral clock gates (GATE_CON04, offset 0x810) */

#define RV1126B_PCLK_UART1_GATE              (1 << 9)
#define RV1126B_PCLK_UART2_GATE              (1 << 10)
#define RV1126B_PCLK_UART3_GATE              (1 << 11)
#define RV1126B_PCLK_UART4_GATE              (1 << 12)
#define RV1126B_PCLK_UART5_GATE              (1 << 13)
#define RV1126B_PCLK_UART6_GATE              (1 << 14)
#define RV1126B_PCLK_UART7_GATE              (1 << 15)

/* PMUCRU UART0 peripheral clock gate (GATE_CON01, offset 0x804) */

#define RV1126B_PCLK_UART0_GATE              (1 << 8)
#define RV1126B_SCLK_UART0_GATE              (1 << 11)

/* ======================= UART Soft Resets ======================= */
/* BUSCRU UART resets (SOFTRST_CON05, offset 0xA14) */

#define RV1126B_SRST_PRESETN_UART1           (1 << 0)
#define RV1126B_SRST_SRESETN_UART1           (1 << 1)
#define RV1126B_SRST_PRESETN_UART2           (1 << 2)
#define RV1126B_SRST_SRESETN_UART2           (1 << 3)
#define RV1126B_SRST_PRESETN_UART3           (1 << 4)
#define RV1126B_SRST_SRESETN_UART3           (1 << 5)
#define RV1126B_SRST_PRESETN_UART4           (1 << 6)
#define RV1126B_SRST_SRESETN_UART4           (1 << 7)
#define RV1126B_SRST_PRESETN_UART5           (1 << 8)
#define RV1126B_SRST_SRESETN_UART5           (1 << 9)
#define RV1126B_SRST_PRESETN_UART6           (1 << 10)
#define RV1126B_SRST_SRESETN_UART6           (1 << 11)
#define RV1126B_SRST_PRESETN_UART7           (1 << 12)
#define RV1126B_SRST_SRESETN_UART7           (1 << 13)

/* PMUCRU UART0 reset (SOFTRST_CON01, offset 0xA04) */

#define RV1126B_SRST_PRESETN_UART0           (1 << 4)
#define RV1126B_SRST_SRESETN_UART0           (1 << 5)

/* ======================= GPIO Clock Gates ======================= */
/* PERICRU GPIO1 clock gate (GATE_CON00, offset 0x800) */

#define RV1126B_PCLK_GPIO1_GATE              (1 << 5)
#define RV1126B_DBCLK_GPIO1_GATE             (1 << 6)

/* PMUCRU GPIO0 clock gate (GATE_CON00, offset 0x800) */

#define RV1126B_PCLK_PMU_GPIO0_GATE          (1 << 7)
#define RV1126B_DBCLK_PMU_GPIO0_GATE         (1 << 8)

/* VICRU GPIO2/4/5/6/7 clock gates (GATE_CON01, offset 0x804) */

#define RV1126B_PCLK_GPIO2_GATE              (1 << 0)
#define RV1126B_DBCLK_GPIO2_GATE             (1 << 1)
#define RV1126B_PCLK_GPIO4_GATE              (1 << 2)
#define RV1126B_DBCLK_GPIO4_GATE             (1 << 3)
#define RV1126B_PCLK_GPIO5_GATE              (1 << 4)
#define RV1126B_DBCLK_GPIO5_GATE             (1 << 5)
#define RV1126B_PCLK_GPIO6_GATE              (1 << 6)
#define RV1126B_DBCLK_GPIO6_GATE             (1 << 7)
#define RV1126B_PCLK_GPIO7_GATE              (1 << 8)
#define RV1126B_DBCLK_GPIO7_GATE             (1 << 9)

/* VEPUCRU GPIO3 clock gate (GATE_CON00, offset 0x800) */

#define RV1126B_PCLK_GPIO3_GATE              (1 << 7)
#define RV1126B_DBCLK_GPIO3_GATE             (1 << 8)

/* ======================= GPIO Soft Resets ======================= */
/* PERICRU GPIO1 reset (SOFTRST_CON00, offset 0xA00) */

#define RV1126B_SRST_PRESETN_GPIO1           (1 << 5)
#define RV1126B_SRST_DBRESETN_GPIO1          (1 << 6)

/* PMUCRU GPIO0 reset (SOFTRST_CON00, offset 0xA00) */

#define RV1126B_SRST_PRESETN_PMU_GPIO0       (1 << 7)
#define RV1126B_SRST_DBRESETN_PMU_GPIO0      (1 << 8)

/* VICRU GPIO2/4/5/6/7 resets (SOFTRST_CON01, offset 0xA04) */

#define RV1126B_SRST_PRESETN_GPIO2           (1 << 0)
#define RV1126B_SRST_DBRESETN_GPIO2          (1 << 1)
#define RV1126B_SRST_PRESETN_GPIO4           (1 << 2)
#define RV1126B_SRST_DBRESETN_GPIO4          (1 << 3)
#define RV1126B_SRST_PRESETN_GPIO5           (1 << 4)
#define RV1126B_SRST_DBRESETN_GPIO5          (1 << 5)
#define RV1126B_SRST_PRESETN_GPIO6           (1 << 6)
#define RV1126B_SRST_DBRESETN_GPIO6          (1 << 7)
#define RV1126B_SRST_PRESETN_GPIO7           (1 << 8)
#define RV1126B_SRST_DBRESETN_GPIO7          (1 << 9)

/* VEPUCRU GPIO3 reset (SOFTRST_CON00, offset 0xA00) */

#define RV1126B_SRST_PRESETN_GPIO3           (1 << 7)
#define RV1126B_SRST_DBRESETN_GPIO3          (1 << 8)

/* ======================= I2C Clock Gates ======================= */
/* BUSCRU I2C clock gates (GATE_CON03, offset 0x80C) */

#define RV1126B_PCLK_I2C0_GATE               (1 << 0)
#define RV1126B_CLK_I2C0_GATE                (1 << 1)
#define RV1126B_PCLK_I2C1_GATE               (1 << 2)
#define RV1126B_CLK_I2C1_GATE                (1 << 3)
#define RV1126B_PCLK_I2C3_GATE               (1 << 4)
#define RV1126B_CLK_I2C3_GATE                (1 << 5)
#define RV1126B_PCLK_I2C4_GATE               (1 << 6)
#define RV1126B_CLK_I2C4_GATE                (1 << 7)
#define RV1126B_PCLK_I2C5_GATE               (1 << 8)
#define RV1126B_CLK_I2C5_GATE                (1 << 9)

/* PMUCRU I2C2 clock gate (GATE_CON01, offset 0x804) */

#define RV1126B_PCLK_I2C2_GATE               (1 << 6)
#define RV1126B_CLK_I2C2_GATE                (1 << 7)

/* ======================= I2C Soft Resets ======================= */
/* BUSCRU I2C resets (SOFTRST_CON03, offset 0xA0C) */

#define RV1126B_SRST_PRESETN_I2C0            (1 << 0)
#define RV1126B_SRST_RESETN_I2C0             (1 << 1)
#define RV1126B_SRST_PRESETN_I2C1            (1 << 2)
#define RV1126B_SRST_RESETN_I2C1             (1 << 3)
#define RV1126B_SRST_PRESETN_I2C3            (1 << 4)
#define RV1126B_SRST_RESETN_I2C3             (1 << 5)
#define RV1126B_SRST_PRESETN_I2C4            (1 << 6)
#define RV1126B_SRST_RESETN_I2C4             (1 << 7)
#define RV1126B_SRST_PRESETN_I2C5            (1 << 8)
#define RV1126B_SRST_RESETN_I2C5             (1 << 9)

/* PMUCRU I2C2 reset (SOFTRST_CON01, offset 0xA04) */

#define RV1126B_SRST_PRESETN_I2C2            (1 << 2)
#define RV1126B_SRST_RESETN_I2C2             (1 << 3)

/* ======================= SPI Clock Gates ======================= */
/* BUSCRU SPI clock gates (GATE_CON03, offset 0x80C) */

#define RV1126B_PCLK_SPI0_GATE               (1 << 10)
#define RV1126B_PCLK_SPI1_GATE               (1 << 12)

/* TOPCRU SPI source clock gates (GATE_CON08, offset 0x820) */

#define RV1126B_CLK_SPI0_SRC_GATE            (1 << 10)
#define RV1126B_CLK_SPI1_SRC_GATE            (1 << 11)

/* ======================= SPI Soft Resets ======================= */
/* BUSCRU SPI resets (SOFTRST_CON03, offset 0xA0C) */

#define RV1126B_SRST_PRESETN_SPI0            (1 << 10)
#define RV1126B_SRST_RESETN_SPI0             (1 << 11)
#define RV1126B_SRST_PRESETN_SPI1            (1 << 12)
#define RV1126B_SRST_RESETN_SPI1             (1 << 13)

/* ======================= Timer Clock Gates ======================= */
/* BUSCRU Timer clock gates (GATE_CON02, offset 0x808) */

#define RV1126B_PCLK_TIMER_GATE              (1 << 5)
#define RV1126B_CLK_TIMER0_GATE              (1 << 6)
#define RV1126B_CLK_TIMER1_GATE              (1 << 7)
#define RV1126B_CLK_TIMER2_GATE              (1 << 8)
#define RV1126B_CLK_TIMER3_GATE              (1 << 9)
#define RV1126B_CLK_TIMER4_GATE              (1 << 10)
#define RV1126B_CLK_TIMER5_GATE              (1 << 11)

/* TOPCRU Timer source clock gate (GATE_CON07, offset 0x81C) */

#define RV1126B_CLK_TIMER_SRC_GATE           (1 << 13)

#endif /* __BOARD_HARDWARE_RV1126B_CRU_H */
