/****************************************************************************
 * chips/bk7258/include/bk7258_sdio.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SDIO host controller — NuttX sdio_dev_s lower-half wrapping
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
#include <nuttx/compiler.h>
#include <nuttx/sdio.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The Beken SDIO host is a single unit on BK7258. */

#define BK7258_SDIO_UNIT               0

/* Default bus width / clock applied at init (identification-speed clock).
 * The selected physical-board binding owns whether the extra data lines are
 * routed safely and therefore whether CONFIG_BK7258_SDIO_4BIT is selectable.
 */

#ifdef CONFIG_BK7258_SDIO_4BIT
#  define BK7258_SDIO_BUS_WIDTH_4BIT  1
#else
#  define BK7258_SDIO_BUS_WIDTH_4BIT  0
#endif
#define BK7258_SDIO_CLK_IDMODE        400000u

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Runtime width evidence for board validation and fault diagnosis.  The
 * SDK data-path bundle has a compile-time maximum width, so callers must be
 * able to distinguish the routed physical instance from a completed runtime
 * host-width transition.
 */

struct bk7258_sdio_runtime_s
{
  uint32_t initialized;
  uint32_t bus_width;
  uint32_t width_transitions;
  uint32_t width_failures;
  int32_t last_width_error;
};

/* One board-selected route through the BK7258 SDIO pin groups.  The board
 * records its physical wiring; the chip lower half validates the route and
 * owns SDK pinmux calls.
 */

struct bk7258_sdio_pin_config_s
{
  uint8_t map_mode;
  uint8_t clk_pin;
  uint8_t cmd_pin;
  uint8_t data_pin[4];
};

/* Physical slot wiring and media-detect policy.  Board bring-up passes this
 * immutable record to the chip lower half; the chip never discovers a board
 * through a global symbol.
 */

struct bk7258_sdio_board_s
{
  FAR const struct bk7258_sdio_pin_config_s *pins;
  bool card_detect_available;
  uint32_t media_poll_ms;
  int (*prepare)(bool widebus);
  bool (*card_present)(void);
};

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
 *   once here; the controller itself is initialised with the configured bus
 *   width at identification speed and re-configured at runtime by
 *   widebus()/clock().
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int bk7258_sdio_initialize(
  FAR struct sdio_dev_s **sdio_dev,
  FAR const struct bk7258_sdio_board_s *board);

/* Return a coherent-enough diagnostic snapshot after MMC/SD probing.  Width
 * transitions are serialized by the upper half during card initialization;
 * this API does not control the host or initiate I/O.
 */

int bk7258_sdio_get_runtime(FAR struct bk7258_sdio_runtime_s *runtime);

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_SDIO */

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SDIO_H */
