/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/contest_board/chip/rv1126b_start.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * RV1126B HPMCU C startup for NuttX
 *
 * Called from rv1126b_head.S after basic CPU state (GP, SP, mtvec) is set.
 * Performs:
 *   1. Early UART5 setup for the console
 *   2. BSS initialization (zero-fill)
 *   3. Data section initialization (copy from ROM if needed)
 *   4. Preserve the verified DCache-bypass policy
 *   5. Clock verification
 *   6. Board-level initialization
 *   7. Transfer control to the NuttX kernel (nx_start)
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/init.h>
#include <arch/board/board.h>

#include "chip.h"

/****************************************************************************
 * External Symbols (provided by linker script)
 ****************************************************************************/

extern uint32_t _sbss;     /* Start of BSS section */
extern uint32_t _ebss;     /* End of BSS section */
extern uint32_t _sdata;    /* Start of data section (in RAM) */
extern uint32_t _edata;    /* End of data section (in RAM) */
extern uint32_t _eronly;   /* Load address of data section (in ROM/flash) */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_bss_init
 *
 * Description:
 *   Zero-fill the BSS section from _sbss to _ebss.
 *
 ****************************************************************************/

static void rv1126b_bss_init(void)
{
  uint32_t *dest = &_sbss;

  while (dest < &_ebss)
    {
      *dest++ = 0;
    }
}

/****************************************************************************
 * Name: rv1126b_data_init
 *
 * Description:
 *   Copy initialized data from ROM/flash load address (_eronly) to RAM
 *   (_sdata.._edata).  If the data section is loaded directly into RAM
 *   (e.g., XIP from flash with data in-place), this is a no-op.
 *
 ****************************************************************************/

static void rv1126b_data_init(void)
{
  uint32_t *src  = &_eronly;
  uint32_t *dest = &_sdata;

  while (dest < &_edata)
    {
      *dest++ = *src++;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rv1126b_start
 *
 * Description:
 *   C entry point called from rv1126b_head.S _start.
 *   Performs all hardware and OS initialization, then transfers control
 *   to the NuttX kernel.
 *
 ****************************************************************************/

void rv1126b_start(void)
{
  /* Initialize early UART before BSS/data/cache setup. */

  rv1126b_lowsetup();

  /* 1. Initialize BSS section. */

  rv1126b_bss_init();

  /* 2. Initialize data section (copy from ROM/flash to RAM) */

  rv1126b_data_init();

  /* 3. DCache is intentionally bypassed in this verified baseline. */

  /* 4. Verify the preconfigured clocks. */

  rv1126b_clockconfig();

  /* 5. Board-level initialization */

  boardinitialize();

  /* 6. Start the NuttX kernel -- this call does not return */

  nx_start();

  /* Should never reach here */

  for (; ; )
    {
    }
}
