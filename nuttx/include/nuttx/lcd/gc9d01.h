/****************************************************************************
 * include/nuttx/lcd/gc9d01.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __INCLUDE_NUTTX_LCD_GC9D01_H
#define __INCLUDE_NUTTX_LCD_GC9D01_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/compiler.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct lcd_dev_s;
struct gc9d01_lcd_s;

/* Board/SoC transport interface.  The generic panel driver owns the GC9D01
 * command sequence and address-window state.  The transport owns the bus,
 * reset/backlight GPIO implementation, byte order and transfer completion.
 */

struct gc9d01_lcd_s
{
  CODE int (*reset)(FAR struct gc9d01_lcd_s *lcd, bool asserted);
  CODE int (*writecmd)(FAR struct gc9d01_lcd_s *lcd, uint8_t cmd,
                       FAR const uint8_t *params, size_t nparams);

  /* Synchronously transfer native-endian RGB565 pixels into the address
   * window selected by the most recent CASET/RASET/RAMWR commands.  Stride is
   * expressed in bytes and may exceed width * sizeof(uint16_t).
   */

  CODE int (*writegram)(FAR struct gc9d01_lcd_s *lcd,
                        FAR const uint16_t *pixels, size_t width,
                        size_t height, size_t stride);

  /* Optional board backlight callback.  Power is in the NuttX range
   * 0..CONFIG_LCD_MAXPOWER.
   */

  CODE int (*backlight)(FAR struct gc9d01_lcd_s *lcd, int power);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: gc9d01_lcdinitialize
 *
 * Description:
 *   Bind one GC9D01 panel to a board/SoC transport and return the standard
 *   NuttX LCD lower half.  The panel is initialized and awake with its
 *   backlight off so an LCD framebuffer front end can upload its initial
 *   contents before calling setpower().
 *
 * Input Parameters:
 *   lcd   - Board/SoC transport callbacks.
 *   devno - Driver instance number in the range selected by
 *           CONFIG_LCD_GC9D01_NINTERFACES.
 *
 * Returned Value:
 *   A non-NULL LCD device on success; NULL on failure.
 *
 ****************************************************************************/

FAR struct lcd_dev_s *gc9d01_lcdinitialize(
  FAR struct gc9d01_lcd_s *lcd, int devno);

#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_LCD_GC9D01_H */
