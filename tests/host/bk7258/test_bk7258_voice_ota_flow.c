/****************************************************************************
 * tests/host/bk7258/test_bk7258_voice_ota_flow.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include "bk7258_voice_ota_flow.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static struct bkvoice_ota_intent_s intent(enum bkvoice_ota_state_e state)
{
  struct bkvoice_ota_intent_s value;

  memset(&value, 0, sizeof(value));
  value.state = state;
  memset(value.manifest_sha256, 0xa5, sizeof(value.manifest_sha256));
  value.source_version = (struct bk7258_mcuboot_version_s){18, 6, 389, 449};
  value.source_security_counter = 449;
  value.target_version = (struct bk7258_mcuboot_version_s){18, 6, 390, 450};
  value.target_security_counter = 450;
  value.source_boot_generation = 7;
  return value;
}

static struct bk7258_ota_pair_snapshot_s pair(
  enum bk7258_ota_pair_state_e state, bool target)
{
  struct bk7258_ota_pair_snapshot_s value;

  memset(&value, 0, sizeof(value));
  value.state = state;
  value.security_counter_present = true;
  value.version = target ?
    (struct bk7258_mcuboot_version_s){18, 6, 390, 450} :
    (struct bk7258_mcuboot_version_s){18, 6, 389, 449};
  value.security_counter = target ? 450 : 449;
  return value;
}

static void expect(enum bkvoice_ota_state_e state, uint32_t boot,
                   enum bk7258_ota_pair_state_e pair_state, bool target,
                   enum bkvoice_ota_flow_action_e expected)
{
  struct bkvoice_ota_intent_s saved = intent(state);
  struct bk7258_ota_pair_snapshot_s active = pair(pair_state, target);
  enum bkvoice_ota_flow_action_e actual = 0;

  assert(bkvoice_ota_flow_decide(&saved, boot, &active, &actual) == 0);
  assert(actual == expected);
}

static void expect_same_generation_target(
  enum bkvoice_ota_state_e state,
  enum bk7258_ota_pair_state_e pair_state,
  enum bkvoice_ota_flow_action_e expected)
{
  struct bkvoice_ota_intent_s saved = intent(state);
  struct bk7258_ota_pair_snapshot_s active = pair(pair_state, true);
  enum bkvoice_ota_flow_action_e actual = 0;

  saved.source_boot_generation = 1;
  assert(bkvoice_ota_flow_decide(&saved, 1, &active, &actual) == 0);
  assert(actual == expected);
}

int main(void)
{
  struct bkvoice_ota_intent_s saved = intent(BKVOICE_OTA_STAGED);
  struct bk7258_ota_pair_snapshot_s active = pair(
    BK7258_OTA_PAIR_PENDING, false);
  enum bkvoice_ota_flow_action_e action;

  expect(BKVOICE_OTA_DOWNLOADING, 7, BK7258_OTA_PAIR_CONFIRMED, false,
         BKVOICE_OTA_FLOW_RESTAGE);
  expect(BKVOICE_OTA_DOWNLOADING, 8, BK7258_OTA_PAIR_PENDING, true,
         BKVOICE_OTA_FLOW_TRIAL);
  expect(BKVOICE_OTA_DOWNLOADING, 8, BK7258_OTA_PAIR_CONFIRMED, true,
         BKVOICE_OTA_FLOW_CONFIRMED);
  expect(BKVOICE_OTA_STAGED, 7, BK7258_OTA_PAIR_CONFIRMED, false,
         BKVOICE_OTA_FLOW_REBOOT);
  expect(BKVOICE_OTA_REBOOTING, 8, BK7258_OTA_PAIR_PENDING, true,
         BKVOICE_OTA_FLOW_TRIAL);
  expect(BKVOICE_OTA_STAGED, 8, BK7258_OTA_PAIR_CONFIRMED, true,
         BKVOICE_OTA_FLOW_CONFIRMED);
  expect(BKVOICE_OTA_TRIAL, 9, BK7258_OTA_PAIR_CONFIRMED, false,
         BKVOICE_OTA_FLOW_ROLLED_BACK);

  expect_same_generation_target(BKVOICE_OTA_DOWNLOADING,
                                BK7258_OTA_PAIR_PENDING,
                                BKVOICE_OTA_FLOW_TRIAL);
  expect_same_generation_target(BKVOICE_OTA_DOWNLOADING,
                                BK7258_OTA_PAIR_CONFIRMED,
                                BKVOICE_OTA_FLOW_CONFIRMED);
  expect_same_generation_target(BKVOICE_OTA_STAGED,
                                BK7258_OTA_PAIR_PENDING,
                                BKVOICE_OTA_FLOW_TRIAL);
  expect_same_generation_target(BKVOICE_OTA_STAGED,
                                BK7258_OTA_PAIR_CONFIRMED,
                                BKVOICE_OTA_FLOW_CONFIRMED);
  expect_same_generation_target(BKVOICE_OTA_REBOOTING,
                                BK7258_OTA_PAIR_PENDING,
                                BKVOICE_OTA_FLOW_TRIAL);
  expect_same_generation_target(BKVOICE_OTA_REBOOTING,
                                BK7258_OTA_PAIR_CONFIRMED,
                                BKVOICE_OTA_FLOW_CONFIRMED);
  expect_same_generation_target(BKVOICE_OTA_TRIAL,
                                BK7258_OTA_PAIR_PENDING,
                                BKVOICE_OTA_FLOW_TRIAL);
  expect_same_generation_target(BKVOICE_OTA_TRIAL,
                                BK7258_OTA_PAIR_CONFIRMED,
                                BKVOICE_OTA_FLOW_CONFIRMED);

  assert(bkvoice_ota_flow_decide(&saved, 7, &active, &action) == -ESTALE);
  active = pair(BK7258_OTA_PAIR_CONFIRMED, true);
  active.security_counter++;
  assert(bkvoice_ota_flow_decide(&saved, 8, &active, &action) == -ESTALE);
  active = pair(BK7258_OTA_PAIR_CONFIRMED, true);
  active.security_counter_present = false;
  assert(bkvoice_ota_flow_decide(&saved, 1, &active, &action) == -ESTALE);
  active = pair(BK7258_OTA_PAIR_CONFIRMED, true);
  active.version.revision++;
  assert(bkvoice_ota_flow_decide(&saved, 1, &active, &action) == -ESTALE);
  active = pair(BK7258_OTA_PAIR_CONFIRMED, true);
  active.security_counter++;
  assert(bkvoice_ota_flow_decide(&saved, 1, &active, &action) == -ESTALE);
  active = pair(BK7258_OTA_PAIR_CONFIRMED, true);
  active.state = (enum bk7258_ota_pair_state_e)99;
  assert(bkvoice_ota_flow_decide(&saved, 1, &active, &action) == -EINVAL);
  active = pair(BK7258_OTA_PAIR_CONFIRMED, true);
  saved.state = (enum bkvoice_ota_state_e)99;
  assert(bkvoice_ota_flow_decide(&saved, 1, &active, &action) == -EINVAL);
  saved = intent(BKVOICE_OTA_STAGED);
  saved.target_version = saved.source_version;
  saved.target_security_counter = saved.source_security_counter;
  active = pair(BK7258_OTA_PAIR_CONFIRMED, false);
  assert(bkvoice_ota_flow_decide(&saved, 1, &active, &action) == -EINVAL);

  assert(bkvoice_ota_flow_decide(NULL, 8, &active, &action) == -EINVAL);

  puts("BKVOICE_OTA_FLOW_HOST_PASS");
  return 0;
}
