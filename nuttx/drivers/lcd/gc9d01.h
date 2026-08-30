/****************************************************************************
 * drivers/lcd/gc9d01.h
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

#ifndef __DRIVERS_LCD_GC9D01_H
#define __DRIVERS_LCD_GC9D01_H

#define GC9D01_SLPIN    0x10 /* Sleep In */
#define GC9D01_SLPOUT   0x11 /* Sleep Out */
#define GC9D01_DISPOFF  0x28 /* Display Off */
#define GC9D01_DISPON   0x29 /* Display On */
#define GC9D01_CASET    0x2a /* Column Address Set */
#define GC9D01_RASET    0x2b /* Row Address Set */
#define GC9D01_RAMWR    0x2c /* Memory Write */
#define GC9D01_TEOFF    0x34 /* Tearing Effect Line Off */
#define GC9D01_MADCTL   0x36 /* Memory Access Control */
#define GC9D01_COLMOD   0x3a /* Interface Pixel Format */

#define GC9D01_XRES     160
#define GC9D01_YRES     160
#define GC9D01_BPP      16

#endif /* __DRIVERS_LCD_GC9D01_H */
