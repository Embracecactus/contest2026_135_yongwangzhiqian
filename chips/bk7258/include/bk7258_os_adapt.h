/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * chips/bk7258/include/bk7258_os_adapt.h
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

#ifndef __ARCH_ARM_SRC_BK7258_BK7258_OS_ADAPT_H
#define __ARCH_ARM_SRC_BK7258_BK7258_OS_ADAPT_H

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SDK_SRAM_HEAP
int bk7258_os_sram_heap_initialize(void);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_BK7258_OS_ADAPT_H */
