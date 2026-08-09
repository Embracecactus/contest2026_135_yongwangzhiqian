/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_spi.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) SPI master — NuttX spi_dev_s lower-half wrapping the
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
#include <nuttx/spi/spi.h>

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Hardware SPI unit used.  BK7258 exposes SPI_ID_0 (and SPI_ID_1 if the
 * SoC wires SOC_SPI_UNIT_NUM > 1).  We drive a single master unit.
 */

#define BK7258_SPI_UNIT                0

/* Default bus parameters applied at init.  Mode 0, 8-bit, MSB first. */

#define BK7258_SPI_MODE_DEFAULT        0        /* SPIDEV_MODE0 */
#define BK7258_SPI_BITS_DEFAULT        8
#define BK7258_SPI_BAUD_DEFAULT        1000000u /* 1 MHz */

/* Default per-call blocking timeout handed to the Beken SDK (ms). */

#define BK7258_SPI_TIMEOUT_MS_DEFAULT  1000u

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Board-provided chip-select callback.  The Beken SPI driver has no CS
 * primitive; asserting/de-asserting the slave select is a board GPIO
 * responsibility.  The board assigns this hook (e.g. in board_bringup) so
 * the lower half's select() can toggle the right pin for a given devid.
 */

typedef void (*bk7258_spi_cs_cb_t)(uint32_t devid, bool selected);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_SPI
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_spi_initialize
 *
 * Description:
 *   Bring up the BK7258 SPI master unit and publish it as a NuttX SPI
 *   lower-half (struct spi_dev_s).  No transfer happens until the upper
 *   half calls lock()/select()/exchange().  Returns the lower-half pointer
 *   through the caller's argument; NULL on failure.
 *
 *   The underlying Beken SPI driver (bk_spi_driver_init) is initialised
 *   once here.  The caller (board code) typically wires the returned
 *   spi_dev_s into an SPI device / bus and sets the CS callback with
 *   bk7258_spi_set_csinfo().
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int bk7258_spi_initialize(FAR struct spi_dev_s **spi_dev);

/****************************************************************************
 * Name: bk7258_spi_set_csinfo
 *
 * Description:
 *   Install the board chip-select callback used by the lower half's
 *   select().  Must be called before any transfer that relies on CS
 *   toggling.  Passing NULL restores the default no-op (CS left to the
 *   board to drive externally).
 *
 ****************************************************************************/

void bk7258_spi_set_csinfo(bk7258_spi_cs_cb_t cs_cb);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_SPI */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SPI_H */
