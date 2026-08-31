/****************************************************************************
 * include/nuttx/sensors/sc7a20.h
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

#ifndef __INCLUDE_NUTTX_SENSORS_SC7A20_H
#define __INCLUDE_NUTTX_SENSORS_SC7A20_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Register transport supplied by the board or SoC binding.  The generic
 * driver owns the SC7A20 protocol, identity check, sampling conversion and
 * NuttX sensor lower half; the transport owns only the physical bus access.
 */

struct sc7a20_config_s
{
  FAR void *arg;
  CODE int (*read)(FAR void *arg, uint8_t reg, FAR uint8_t *buffer,
                   size_t buflen);
  CODE int (*write)(FAR void *arg, uint8_t reg, uint8_t value);
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: sc7a20_register
 *
 * Description:
 *   Probe and configure one SC7A20 accelerometer, then register it with the
 *   standard NuttX sensor upper half as /dev/uorb/sensor_accelN.
 *
 * Input Parameters:
 *   devno  - Sensor instance number N.
 *   config - Register transport.  The structure is copied; config->arg and
 *            the objects it references must remain valid for the device
 *            lifetime.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int sc7a20_register(int devno, FAR const struct sc7a20_config_s *config);

#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_SENSORS_SC7A20_H */
