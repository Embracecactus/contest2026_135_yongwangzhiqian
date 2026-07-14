/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/contest_board/chip/chip.h
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

#ifndef __BOARD_CHIP_CHIP_H
#define __BOARD_CHIP_CHIP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <arch/csr.h>

/* Hardware register definitions */

#include "hardware/rv1126b_memorymap.h"
#include "hardware/rv1126b_intmux.h"
#include "hardware/rv1126b_timer.h"
#include "hardware/rv1126b_uart.h"
#include "hardware/rv1126b_cru.h"

/****************************************************************************
 * RISC-V CSR Helper Macros
 *
 * These are needed for inline CSR access in C code (set_csr, clear_csr,
 * read_csr, write_csr).  They follow the same pattern as the SDK
 * riscv_csr_encoding.h.
 ****************************************************************************/

#ifndef __ASSEMBLY__

#ifdef __GNUC__

#define read_csr(reg) \
  ({ unsigned long __tmp; \
     asm volatile ("csrr %0, " #reg : "=r"(__tmp)); \
     __tmp; })

#define write_csr(reg, val) \
  ({ asm volatile ("csrw " #reg ", %0" :: "rK"(val)); })

#define set_csr(reg, bit) \
  ({ unsigned long __tmp; \
     asm volatile ("csrrs %0, " #reg ", %1" : "=r"(__tmp) : "rK"(bit)); \
     __tmp; })

#define clear_csr(reg, bit) \
  ({ unsigned long __tmp; \
     asm volatile ("csrrc %0, " #reg ", %1" : "=r"(__tmp) : "rK"(bit)); \
     __tmp; })

#endif /* __GNUC__ */

/****************************************************************************
 * RISC-V Interrupt Cause Codes
 ****************************************************************************/

#define IRQ_M_SOFT      3
#define IRQ_M_TIMER     7
#define IRQ_M_EXT       11

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void rv1126b_start(void);
void rv1126b_lowsetup(void);   /* Early UART5 init from rv1126b_lowputc.c */
void rv1126b_clockconfig(void); /* Verify preconfigured clocks. */

#endif /* !__ASSEMBLY__ */

#endif /* __BOARD_CHIP_CHIP_H */
