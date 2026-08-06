/*
 * bk7258_ota_n17_publish.h - CP-side normal format-3 OTA publication.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_N17_PUBLISH_H
#define __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_N17_PUBLISH_H

#include <stdbool.h>
#include <stdint.h>

#include "bk7258_ota_n17_auth.h"

struct bk7258_ota_n17_pending_request_s
{
  uint8_t target_slot;
  uint64_t generation;
  uint32_t timestamp;
  uint8_t manifest[BK7258_OTA_N17_MANIFEST_SIZE];
};

struct bk7258_ota_n17_pending_result_s
{
  uint8_t stable_slot;
  uint8_t target_slot;
  uint32_t journal_bank;
  uint64_t generation;
  uint64_t accepted_security_counter;
};

#ifdef CONFIG_BK7258_OTA_STAGING
int bk7258_ota_n17_publish_initialize(void);

/* Publish only a normal successor to an existing stable format-3 bank.
 * It cannot migrate format-2 metadata or arm the authentication policy. */

int bk7258_ota_n17_publish_pending(
  const struct bk7258_ota_n17_pending_request_s *request,
  struct bk7258_ota_n17_pending_result_s *result);

/* These lifecycle commits never change an image or Manifest sector.  They
 * append one record to the currently selected format-3 journal bank. */

int bk7258_ota_n17_publish_trial(uint64_t expected_generation);
int bk7258_ota_n17_publish_confirm(uint64_t expected_generation);
int bk7258_ota_n17_publish_rollback(uint64_t expected_generation);

bool bk7258_ota_n17_publish_write_enabled(void);

#ifdef CONFIG_BK7258_OTA_N17_WRITE
int bk7258_ota_n17_publish_set_write_enabled(bool enabled);
#endif
#endif

#endif /* __ARCH_ARM_INCLUDE_BK7258_BK7258_OTA_N17_PUBLISH_H */
