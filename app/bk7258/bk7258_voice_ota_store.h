/****************************************************************************
 * app/bk7258/bk7258_voice_ota_store.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_OTA_STORE_H
#define __APP_BK7258_BK7258_VOICE_OTA_STORE_H

#include <stdint.h>
#include <arch/chip/bk7258_mcuboot_format.h>

#define BKVOICE_OTA_STORE_ROOT "/cpdata/shaniu/voice-ota"

enum bkvoice_ota_state_e
{
  BKVOICE_OTA_DOWNLOADING = 1,
  BKVOICE_OTA_STAGED,
  BKVOICE_OTA_REBOOTING,
  BKVOICE_OTA_TRIAL,
};

struct bkvoice_ota_intent_s
{
  enum bkvoice_ota_state_e state;
  uint8_t manifest_sha256[32];
  struct bk7258_mcuboot_version_s source_version;
  uint32_t source_security_counter;
  struct bk7258_mcuboot_version_s target_version;
  uint32_t target_security_counter;
  uint32_t source_boot_generation;
};

/* Start only records the root.  It does not touch RPMsgFS. */
int bkvoice_ota_store_start(const char *root);
/* A tombstone returns -ENOENT while setting the durable revision. */
int bkvoice_ota_store_load(struct bkvoice_ota_intent_s *intent,
                           uint64_t *revision);
int bkvoice_ota_store_commit(const struct bkvoice_ota_intent_s *intent);
int bkvoice_ota_store_clear(const uint8_t expected_manifest_sha256[32]);
/* Drop cached state after an uncertain publication. */
int bkvoice_ota_store_reload(void);

#endif
