/****************************************************************************
 * chips/bk7258/include/bk7258_spi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SPI master — NuttX spi_dev_s lower-half wrapping the
 * official Beken bk_spi_* SDK API.
 *
 * The SPI block is an AP-role peripheral: bk_spi_* (37 symbols) are only
 * defined in the AP libdriver.a.  The CP archive exports the headers but
 * zero symbols, so this driver is AP-only, exactly like the I2C and MIC
 * drivers.  Verified with `nm ap/libs/libdriver.a` (37 T bk_spi_*) vs
 * `nm cp/libs/libdriver.a` (0).
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SPI_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SPI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <nuttx/spi/spi.h>

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Hardware SPI unit used.  BK7258 exposes SPI_ID_0 and SPI_ID_1.  Keep the
 * hardware instance and NuttX /dev/spiN number aligned so board/product
 * profiles do not need a second alias table.
 */

#define BK7258_SPI_UNIT                CONFIG_BK7258_SPI_BUS

/* Default bus parameters applied at init.  Mode 0, 8-bit, MSB first. */

#define BK7258_SPI_MODE_DEFAULT        0        /* SPIDEV_MODE0 */
#define BK7258_SPI_BITS_DEFAULT        8
#define BK7258_SPI_BAUD_DEFAULT        1000000u /* 1 MHz */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_SPI
#ifdef CONFIG_BK7258_AP_CORE

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: bk7258_spi_initialize
 *
 * Description:
 *   Bring up the BK7258 SPI master unit and publish it as a NuttX SPI
 *   lower-half (struct spi_dev_s).  No transfer happens until the upper
 *   half calls lock()/select()/exchange().  Returns the lower-half pointer
 *   through the caller's argument.
 *
 *   The underlying Beken SPI driver (bk_spi_driver_init) is initialised
 *   once here.  Board logic must provide the bus-specific select/status
 *   hooks declared below, following the standard NuttX SPI lower-half
 *   contract.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int bk7258_spi_initialize(FAR struct spi_dev_s **spi_dev);

/****************************************************************************
 * Name: bk7258_spiNselect / bk7258_spiNstatus
 *
 * Description:
 *   These hooks are implemented by the selected physical board.  The select
 *   hook drives the attached device's chip-select GPIO; status reports any
 *   board/device-specific SPI status bits.  This is the same compile-time
 *   board hook model used by NuttX architecture SPI lower halves.
 *
 ****************************************************************************/

#if CONFIG_BK7258_SPI_BUS == 0
void bk7258_spi0select(FAR struct spi_dev_s *dev, uint32_t devid,
                       bool selected);
uint8_t bk7258_spi0status(FAR struct spi_dev_s *dev, uint32_t devid);
#elif CONFIG_BK7258_SPI_BUS == 1
void bk7258_spi1select(FAR struct spi_dev_s *dev, uint32_t devid,
                       bool selected);
uint8_t bk7258_spi1status(FAR struct spi_dev_s *dev, uint32_t devid);
#else
#  error "CONFIG_BK7258_SPI_BUS must select SPI controller 0 or 1"
#endif

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_SPI */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SPI_H */
