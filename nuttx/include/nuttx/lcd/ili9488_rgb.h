/****************************************************************************
 * include/nuttx/lcd/ili9488_rgb.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __INCLUDE_NUTTX_LCD_ILI9488_RGB_H
#define __INCLUDE_NUTTX_LCD_ILI9488_RGB_H

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdbool.h>
#include <stdint.h>

/* The panel driver owns the ILI9488 register sequence.  The caller owns
 * the physical three-wire bus, reset line and their lifetime.
 */

struct ili9488_rgb_ops_s
{
  /* Drive the panel reset line to the requested electrical level. */

  CODE int (*reset)(FAR void *arg, bool high);
  CODE int (*write)(FAR void *arg, bool data_phase, uint8_t value);
};

#ifdef __cplusplus
extern "C"
{
#endif

int ili9488_rgb_initialize(FAR const struct ili9488_rgb_ops_s *ops,
                           FAR void *arg);

#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_LCD_ILI9488_RGB_H */
