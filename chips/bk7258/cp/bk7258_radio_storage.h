/****************************************************************************
 * chips/bk7258/cp/bk7258_radio_storage.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * CP-private radio persistence mechanism.
 ****************************************************************************/

#ifndef __CHIPS_BK7258_CP_BK7258_RADIO_STORAGE_H
#define __CHIPS_BK7258_CP_BK7258_RADIO_STORAGE_H

#include <nuttx/compiler.h>

#include <stdint.h>

#include <arch/chip/bk7258_storage_config.h>

#define BK7258_RADIO_BACKUP_MIN_SIZE  0x1000u
#define BK7258_RADIO_NETWORK_MIN_SIZE 6u

enum bk7258_radio_store_e
{
  BK7258_RADIO_STORE_BACKUP = 0,
  BK7258_RADIO_STORE_NETWORK
};

#ifdef __cplusplus
extern "C"
{
#endif

int bk7258_radio_storage_initialize(
  FAR const struct bk7258_radio_storage_config_s *config);
int bk7258_radio_storage_read(enum bk7258_radio_store_e store,
                              uint32_t offset, FAR uint8_t *buffer,
                              uint32_t length);
int bk7258_radio_storage_write(enum bk7258_radio_store_e store,
                               uint32_t offset,
                               FAR const uint8_t *buffer,
                               uint32_t length);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_CP_BK7258_RADIO_STORAGE_H */
