/****************************************************************************
 * BK7258 SWD route constants shared by BL1, BL2 and the CP runtime.
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_INCLUDE_BK7258_DEBUG_ROUTE_H
#define __BOARD_BK7258_CHIP_INCLUDE_BK7258_DEBUG_ROUTE_H

#define BK7258_SWD_PIN_GROUP_0       0 /* P20/P21 */
#define BK7258_SWD_PIN_GROUP_1       1 /* P0/P1 */

#define BK7258_SWD_TARGET_CPU0       0
#define BK7258_SWD_TARGET_CPU1       1
#define BK7258_SWD_TARGET_CPU2       2

#define BK7258_SYS_CPU_ROUTE_REG     0x44010008
#define BK7258_SYS_JTAG_CORE_MASK    0x00000180
#define BK7258_SYS_JTAG_CORE_SHIFT   7
#define BK7258_SYS_GPIO0_7_FUNC_REG  0x440100c0
#define BK7258_SYS_GPIO16_23_FUNC_REG 0x440100c8
#define BK7258_GPIO0_CTRL_REG        0x44000400
#define BK7258_GPIO1_CTRL_REG        0x44000404
#define BK7258_GPIO20_CTRL_REG       0x44000450
#define BK7258_GPIO21_CTRL_REG       0x44000454
#define BK7258_GPIO_FUNC_CTRL_MASK   0x0000006c
#define BK7258_GPIO_JTAG_CTRL        0x00000048

#define BK7258_SWD_GROUP1_FUNC_MASK  0x000000ff
#define BK7258_SWD_GROUP1_FUNC_VALUE 0x00000022
#define BK7258_SWD_GROUP0_FUNC_MASK  0x00ff0000
#define BK7258_SWD_GROUP0_FUNC_VALUE 0x00110000

#define BK7258_DHCSR_REG             0xe000edf0
#define BK7258_DHCSR_C_DEBUGEN       0x00000001
#define BK7258_DAUTHCTRL_REG         0xe000ee04

/* C_DEBUGEN is asserted near the beginning of a probe connection, before
 * J-Link has finished enumerating CoreSight and issued its first halt.  A
 * debugger must explicitly write the release word after it has attached.
 * The address is in the unallocated tail of the shared boot-state page; it is
 * used only before CP handoff and is cleared by both boot stages.
 */

#define BK7258_SWD_BOOT_RELEASE_ADDRESS 0x2809f7f0
#define BK7258_SWD_BOOT_RELEASE_MAGIC   0x4a4c4e4b /* "JLNK" */

#endif /* __BOARD_BK7258_CHIP_INCLUDE_BK7258_DEBUG_ROUTE_H */
