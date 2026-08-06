/****************************************************************************
 * Board overlay for the NuttX MCUboot configuration.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * The NuttX Apps MCUboot port intentionally keeps the signing algorithm
 * generic.  BK7258 selects P-256 here, in the board port, so apps/, NuttX,
 * and the immutable SDK remain unmodified.
 ****************************************************************************/

#ifndef __BK7258_MCUBOOT_CONFIG_OVERLAY_H
#define __BK7258_MCUBOOT_CONFIG_OVERLAY_H

#include_next <mcuboot_config/mcuboot_config.h>

#define MCUBOOT_SIGN_EC256

#endif /* __BK7258_MCUBOOT_CONFIG_OVERLAY_H */
