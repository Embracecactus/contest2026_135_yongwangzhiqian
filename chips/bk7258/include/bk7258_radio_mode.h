/****************************************************************************
 * chips/bk7258/include/bk7258_radio_mode.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_RADIO_MODE_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_RADIO_MODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum bk7258_radio_mode_e
{
  BK7258_RADIO_MODE_IDLE = 0,
  BK7258_RADIO_MODE_WIFI_MONITOR,
  BK7258_RADIO_MODE_WIFI_SCAN,
  BK7258_RADIO_MODE_BLE_SCAN
};

struct bk7258_radio_mode_status_s
{
  uint32_t mode;
  uint32_t generation;
  uint32_t conflicts;
};

#ifdef CONFIG_BK7258_AP_CORE
int bk7258_radio_mode_acquire(enum bk7258_radio_mode_e mode);
int bk7258_radio_mode_release(enum bk7258_radio_mode_e mode);
int bk7258_radio_mode_status(struct bk7258_radio_mode_status_s *status);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_RADIO_MODE_H */
