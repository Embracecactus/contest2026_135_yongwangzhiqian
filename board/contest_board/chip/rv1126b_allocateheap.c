/****************************************************************************
 * NuttX - RV1126B Heap Allocation
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version
 * 2.0 (the "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_addregion
 *
 * Description:
 *   Add additional memory regions to the heap.
 *
 *   For the RV1126B HPMCU, the heap is set up from the linker script
 *   symbols (_end to _ebss + RAM size) during early initialization in
 *   riscv_allocateheap.c (common).  This function is provided as a
 *   board-level hook for adding extra memory regions if needed.
 *
 *   Currently this is a stub - the default heap region provided by the
 *   common RISC-V heap initialization is sufficient for the HPMCU.
 *
 ****************************************************************************/

void riscv_addregion(void)
{
  /* No additional heap regions to add.
   * The default heap is configured from _end to the end of SRAM
   * by the common RISC-V architecture code.
   */
}
