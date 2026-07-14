/****************************************************************************
 * NuttX - RV1126B BSP
 ****************************************************************************/

#ifndef __BOARD_HARDWARE_RV1126B_MEMORYMAP_H
#define __BOARD_HARDWARE_RV1126B_MEMORYMAP_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* CRU (Clock Reset Unit) base addresses */

#define RV1126B_TOPCRU_BASE           0x20000000UL
#define RV1126B_BUSCRU_BASE           0x20010000UL
#define RV1126B_PERICRU_BASE          0x20020000UL
#define RV1126B_CORECRU_BASE          0x20030000UL
#define RV1126B_PMUCRU_BASE           0x20040000UL
#define RV1126B_PMU1CRU_BASE          0x20050000UL
#define RV1126B_DDRCRU_BASE           0x20060000UL
#define RV1126B_SUBDDRCRU_BASE        0x20068000UL
#define RV1126B_VICRU_BASE            0x20070000UL
#define RV1126B_VEPUCRU_BASE          0x20080000UL
#define RV1126B_NPUCRU_BASE           0x20090000UL
#define RV1126B_VDOCRU_BASE           0x200A0000UL
#define RV1126B_VCPCRU_BASE           0x200B0000UL
#define RV1126B_SBUSCRU_BASE          0x20200000UL
#define RV1126B_SPMUCRU_BASE          0x20210000UL

/* GRF (General Register Files) base addresses */

#define RV1126B_GRF_SYS_BASE          0x20100000UL
#define RV1126B_GRF_PMU_BASE          0x20130000UL

/* IOC (I/O Controller) base addresses */

#define RV1126B_GPIO0_IOC_BASE        0x201A0000UL
#define RV1126B_GPIO1_IOC_BASE        0x201B0000UL
#define RV1126B_GPIO2_IOC_BASE        0x201B8000UL
#define RV1126B_GPIO3_IOC_BASE        0x201C0000UL
#define RV1126B_GPIO4_IOC_BASE        0x201C8000UL
#define RV1126B_GPIO5_IOC_BASE        0x201D0000UL
#define RV1126B_GPIO6_IOC_BASE        0x201D8000UL
#define RV1126B_GPIO7_IOC_BASE        0x201E0000UL

/* INTMUX (Interrupt Multiplexer) base address */

#define RV1126B_INTMUX_BASE           0x20B40000UL

/* GPIO base addresses */

#define RV1126B_GPIO0_BASE            0x20600000UL
#define RV1126B_GPIO0_EXP1_BASE       0x20610000UL
#define RV1126B_GPIO0_EXP2_BASE       0x20620000UL
#define RV1126B_GPIO0_EXP3_BASE       0x20630000UL
#define RV1126B_GPIO1_BASE            0x21300000UL
#define RV1126B_GPIO1_EXP1_BASE       0x21310000UL
#define RV1126B_GPIO1_EXP2_BASE       0x21320000UL
#define RV1126B_GPIO1_EXP3_BASE       0x21330000UL
#define RV1126B_GPIO2_BASE            0x21700000UL
#define RV1126B_GPIO2_EXP1_BASE       0x21710000UL
#define RV1126B_GPIO2_EXP2_BASE       0x21720000UL
#define RV1126B_GPIO2_EXP3_BASE       0x21730000UL
#define RV1126B_GPIO3_BASE            0x21E00000UL
#define RV1126B_GPIO3_EXP1_BASE       0x21E10000UL
#define RV1126B_GPIO3_EXP2_BASE       0x21E20000UL
#define RV1126B_GPIO3_EXP3_BASE       0x21E30000UL
#define RV1126B_GPIO4_BASE            0x21800000UL
#define RV1126B_GPIO4_EXP1_BASE       0x21810000UL
#define RV1126B_GPIO4_EXP2_BASE       0x21820000UL
#define RV1126B_GPIO4_EXP3_BASE       0x21830000UL
#define RV1126B_GPIO5_BASE            0x21900000UL
#define RV1126B_GPIO5_EXP1_BASE       0x21910000UL
#define RV1126B_GPIO5_EXP2_BASE       0x21920000UL
#define RV1126B_GPIO5_EXP3_BASE       0x21930000UL
#define RV1126B_GPIO6_BASE            0x21A00000UL
#define RV1126B_GPIO6_EXP1_BASE       0x21A10000UL
#define RV1126B_GPIO6_EXP2_BASE       0x21A20000UL
#define RV1126B_GPIO6_EXP3_BASE       0x21A30000UL
#define RV1126B_GPIO7_BASE            0x21B00000UL
#define RV1126B_GPIO7_EXP1_BASE       0x21B10000UL
#define RV1126B_GPIO7_EXP2_BASE       0x21B20000UL
#define RV1126B_GPIO7_EXP3_BASE       0x21B30000UL

/* UART base addresses */

#define RV1126B_UART0_BASE            0x20810000UL
#define RV1126B_UART1_BASE            0x21160000UL
#define RV1126B_UART2_BASE            0x21170000UL
#define RV1126B_UART3_BASE            0x21180000UL
#define RV1126B_UART4_BASE            0x21190000UL
#define RV1126B_UART5_BASE            0x211A0000UL
#define RV1126B_UART6_BASE            0x211B0000UL
#define RV1126B_UART7_BASE            0x211C0000UL

/* I2C base addresses */

#define RV1126B_I2C0_BASE             0x21100000UL
#define RV1126B_I2C1_BASE             0x21110000UL
#define RV1126B_I2C2_BASE             0x20800000UL
#define RV1126B_I2C3_BASE             0x21120000UL
#define RV1126B_I2C4_BASE             0x21130000UL
#define RV1126B_I2C5_BASE             0x21140000UL

/* SPI base addresses */

#define RV1126B_SPI0_BASE             0x211E0000UL
#define RV1126B_SPI1_BASE             0x211F0000UL
#define RV1126B_SPI2AHB_BASE          0x208A0000UL

/* PWM base addresses */

#define RV1126B_PWM0_BASE             0x20E00000UL
#define RV1126B_PWM1_BASE             0x20700000UL
#define RV1126B_PWM2_BASE             0x20F00000UL
#define RV1126B_PWM3_BASE             0x21000000UL

/* Timer base addresses */

#define RV1126B_TIMER0_BASE           0x20C00000UL
#define RV1126B_TIMER1_BASE           0x20C10000UL
#define RV1126B_TIMER2_BASE           0x20C20000UL
#define RV1126B_TIMER3_BASE           0x20C30000UL
#define RV1126B_TIMER4_BASE           0x20C40000UL
#define RV1126B_TIMER5_BASE           0x20C50000UL

/* MTIME (Core Timer) base address */

#define RV1126B_CTIMER_BASE           0xFF1E0000UL

/* Watchdog base addresses */

#define RV1126B_WDT_BASE              0x20B50000UL

/* Mailbox base addresses */

#define RV1126B_MBOX0_BASE            0x20500000UL
#define RV1126B_MBOX1_BASE            0x20510000UL
#define RV1126B_MBOX2_BASE            0x20520000UL
#define RV1126B_MBOX3_BASE            0x20530000UL
#define RV1126B_MBOX4_BASE            0x20D00000UL
#define RV1126B_MBOX5_BASE            0x20D10000UL
#define RV1126B_MBOX6_BASE            0x20D20000UL
#define RV1126B_MBOX7_BASE            0x20D30000UL

/* Other peripherals */

#define RV1126B_SPINLOCK_BASE         0x21210000UL
#define RV1126B_RTC_BASE              0x21280000UL
#define RV1126B_FSPI0_BASE            0x21460000UL
#define RV1126B_FSPI1_BASE            0x208C0000UL
#define RV1126B_MMC0_BASE             0x21470000UL
#define RV1126B_SARADC0_BASE          0x21C80000UL
#define RV1126B_SARADC1_BASE          0x21CB0000UL
#define RV1126B_SARADC2_BASE          0x21CC0000UL
#define RV1126B_CAN0_BASE             0x218C0000UL
#define RV1126B_CAN1_BASE             0x218D0000UL
#define RV1126B_GMAC_BASE             0x21880000UL

/* Cache controller */

#define RV1126B_ICACHE_BASE           0x209D0000UL
#define RV1126B_DCACHE_BASE           0x209D0000UL

#endif /* __BOARD_HARDWARE_RV1126B_MEMORYMAP_H */
