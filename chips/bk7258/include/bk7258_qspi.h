/****************************************************************************
 * chips/bk7258/include/bk7258_qspi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 AP QSPI lower-half declarations.
 *
 * The public Beken QSPI API and its implementation are AP-owned in SDK
 * v3.1.1.9.  This wrapper exposes the standard NuttX qspi_dev_s object and
 * does not select or configure a particular external Flash part.  Its
 * command capability is intentionally limited to the opcodes and 24-bit
 * indirect address forms the SDK generic Flash LL actually proves; arbitrary
 * Flash commands are not silently emulated.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_QSPI_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_QSPI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <nuttx/spi/qspi.h>
#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* BK7258 v3.1.1.9 exposes QSPI0 and QSPI1 on the AP. */

#define BK7258_QSPI_UNIT_COUNT          2

/* The SDK's generic Flash initialization selects a 480 MHz source with
 * source divider 4 (96 MHz) and QSPI controller divider 2.  The BK7258
 * clock-divider fields use F/(1+N), so the nominal controller rate is
 * 96 MHz / (2+1) = 32 MHz.  The public SDK provides no runtime frequency
 * setter; this is the fixed value reported by setfrequency().
 */

#ifndef BK7258_QSPI_DEFAULT_FREQUENCY
#  define BK7258_QSPI_DEFAULT_FREQUENCY 32000000u
#endif

#ifndef BK7258_QSPI_DEFAULT_MODE
#  define BK7258_QSPI_DEFAULT_MODE      QSPIDEV_MODE0
#endif

#ifndef BK7258_QSPI_DEFAULT_BITS
#  define BK7258_QSPI_DEFAULT_BITS      8
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_QSPI
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_qspi_initialize
 *
 * Description:
 *   Initialize one BK7258 AP QSPI unit and return its standard NuttX
 *   lower-half object.  A board integration may connect a compatible NuttX
 *   upper-half after auditing its command set; the SDK generic Flash LL does
 *   not provide arbitrary no-address commands such as write-disable, bulk
 *   erase, or reset, so this declaration makes no claim of compatibility
 *   with an arbitrary Flash MTD driver or part.
 *
 * Returned Value:
 *   A valid qspi_dev_s pointer on success; NULL if the interface number or
 *   SDK initialization is invalid or unavailable.
 ****************************************************************************/

FAR struct qspi_dev_s *bk7258_qspi_initialize(int intf);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_QSPI */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_QSPI_H */
