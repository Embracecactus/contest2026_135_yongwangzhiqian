/****************************************************************************
 * NuttX - RV1126B BSP
 ****************************************************************************/

#ifndef __BOARD_HARDWARE_RV1126B_GPIO_H
#define __BOARD_HARDWARE_RV1126B_GPIO_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

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
#define RV1126B_GPIO3_BASE            0x21C00000UL
#define RV1126B_GPIO3_EXP1_BASE       0x21C10000UL
#define RV1126B_GPIO3_EXP2_BASE       0x21C20000UL
#define RV1126B_GPIO3_EXP3_BASE       0x21C30000UL
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

/* GPIO register offsets */

#define RV1126B_GPIO_SWPORT_DR_L      0x0000  /* Port Data Register Low (pins 0-15) */
#define RV1126B_GPIO_SWPORT_DR_H      0x0004  /* Port Data Register High (pins 16-31) */
#define RV1126B_GPIO_SWPORT_DDR_L     0x0008  /* Port Data Direction Low */
#define RV1126B_GPIO_SWPORT_DDR_H     0x000C  /* Port Data Direction High */
#define RV1126B_GPIO_INT_EN_L         0x0010  /* Interrupt Enable Low */
#define RV1126B_GPIO_INT_EN_H         0x0014  /* Interrupt Enable High */
#define RV1126B_GPIO_INT_MASK_L       0x0018  /* Interrupt Mask Low */
#define RV1126B_GPIO_INT_MASK_H       0x001C  /* Interrupt Mask High */
#define RV1126B_GPIO_INT_TYPE_L       0x0020  /* Interrupt Type Low (level/edge) */
#define RV1126B_GPIO_INT_TYPE_H       0x0024  /* Interrupt Type High */
#define RV1126B_GPIO_INT_POLARITY_L   0x0028  /* Interrupt Polarity Low */
#define RV1126B_GPIO_INT_POLARITY_H   0x002C  /* Interrupt Polarity High */
#define RV1126B_GPIO_INT_BOTHEDGE_L   0x0030  /* Interrupt Both Edge Low */
#define RV1126B_GPIO_INT_BOTHEDGE_H   0x0034  /* Interrupt Both Edge High */
#define RV1126B_GPIO_DEBOUNCE_L       0x0038  /* Debounce Enable Low */
#define RV1126B_GPIO_DEBOUNCE_H       0x003C  /* Debounce Enable High */
#define RV1126B_GPIO_DBCLK_DIV_EN_L   0x0040  /* Debounce Clock Divider Enable Low */
#define RV1126B_GPIO_DBCLK_DIV_EN_H   0x0044  /* Debounce Clock Divider Enable High */
#define RV1126B_GPIO_DBCLK_DIV_CON    0x0048  /* Debake Clock Divider Control */
#define RV1126B_GPIO_INT_STATUS       0x0050  /* Interrupt Status (read) */
#define RV1126B_GPIO_INT_RAWSTATUS    0x0058  /* Interrupt Raw Status (read) */
#define RV1126B_GPIO_PORT_EOI_L       0x0060  /* Port End of Interrupt Low */
#define RV1126B_GPIO_PORT_EOI_H       0x0064  /* Port End of Interrupt High */
#define RV1126B_GPIO_EXT_PORT         0x0070  /* External Port Register (read) */
#define RV1126B_GPIO_FLOW_CTRL        0x0074  /* Flow Control */
#define RV1126B_GPIO_VER_ID           0x0078  /* Version ID (read) */
#define RV1126B_GPIO_STORE_ST_L       0x0080  /* Store Status Low */
#define RV1126B_GPIO_STORE_ST_H       0x0084  /* Store Status High */

/* GPIO direction values */

#define RV1126B_GPIO_INPUT            0
#define RV1126B_GPIO_OUTPUT           1

/* GPIO interrupt type values */

#define RV1126B_GPIO_INT_LEVEL        0       /* Level-sensitive */
#define RV1126B_GPIO_INT_EDGE         1       /* Edge-sensitive */

/* GPIO interrupt polarity values */

#define RV1126B_GPIO_INT_ACTIVE_LOW   0       /* Active low / falling edge */
#define RV1126B_GPIO_INT_ACTIVE_HIGH  1       /* Active high / rising edge */

/* GPIO interrupt both-edge values */

#define RV1126B_GPIO_INT_SINGLE_EDGE  0
#define RV1126B_GPIO_INT_BOTH_EDGE    1

/* GPIO0-7 maximum pin counts (each GPIO bank has up to 32 pins) */

#define RV1126B_GPIO_PINS_PER_BANK    32
#define RV1126B_GPIO_BANK_COUNT       8

#endif /* __BOARD_HARDWARE_RV1126B_GPIO_H */
