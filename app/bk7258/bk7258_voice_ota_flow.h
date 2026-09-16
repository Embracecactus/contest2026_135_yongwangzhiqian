/****************************************************************************
 * app/bk7258/bk7258_voice_ota_flow.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VOICE_OTA_FLOW_H
#define __APP_BK7258_BK7258_VOICE_OTA_FLOW_H

#include <stdint.h>

#include <arch/chip/bk7258_ota.h>

#include "bk7258_voice_ota_store.h"

enum bkvoice_ota_flow_action_e
{
  BKVOICE_OTA_FLOW_RESTAGE = 1,
  BKVOICE_OTA_FLOW_REBOOT,
  BKVOICE_OTA_FLOW_TRIAL,
  BKVOICE_OTA_FLOW_CONFIRMED,
  BKVOICE_OTA_FLOW_ROLLED_BACK,
};

int bkvoice_ota_flow_decide(
  const struct bkvoice_ota_intent_s *intent,
  uint32_t current_boot_generation,
  const struct bk7258_ota_pair_snapshot_s *pair,
  enum bkvoice_ota_flow_action_e *action);

#endif
