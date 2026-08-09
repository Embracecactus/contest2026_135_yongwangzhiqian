/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_sdio.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI) SDIO host controller — NuttX sdio_dev_s lower-half wrapping
 * the official Beken bk_sdio_host_* SDK API.
 *
 * The SDIO host block is an AP-role peripheral: the 24 bk_sdio_host_* symbols
 * are defined ONLY in the AP libdriver.a (the CP archive ships the headers
 * but zero symbols), so this driver is AP-only, exactly like I2C/SPI/MIC.
 * Verified with `nm ap/libs/libdriver.a` (24 T bk_sdio_host_*) vs
 * `nm cp/libs/libdriver.a` (0).
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SDIO_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SDIO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The Beken SDIO host is a single unit on BK7258. */

#define BK7258_SDIO_UNIT               0

/* Default bus width / clock applied at init (identification-speed clock). */

#define BK7258_SDIO_BUS_WIDTH_4BIT    1   /* enable 4-bit by default */
#define BK7258_SDIO_CLK_IDMODE        400000u

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_SDIO
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_sdio_initialize
 *
 * Description:
 *   Bring up the BK7258 SDIO host controller and publish it as a NuttX SDIO
 *   lower-half (struct sdio_dev_s).  No card transaction happens until the
 *   MMCSD/SDIO upper half drives the slot.  Returns the lower-half pointer
 *   through the caller's argument; NULL on failure.
 *
 *   The Beken SDIO host driver (bk_sdio_host_driver_init) is initialised
 *   once here; the controller itself is initialised with a 4-bit,
 *   identification-speed configuration and re-configured at runtime by
 *   widebus()/clock().
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int bk7258_sdio_initialize(FAR struct sdio_dev_s **sdio_dev);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_SDIO */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SDIO_H */
