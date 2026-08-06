/*
 * boot_n17_journal_core.h - portable N17 format-3 reader and selector.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BK7258_BOOT_N17_JOURNAL_CORE_H
#define BK7258_BOOT_N17_JOURNAL_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "boot_n17_manifest_core.h"

#define BK7258_BOOT_N17_RECORD_SIZE       256u
#define BK7258_BOOT_N17_BANK_SIZE        4096u
#define BK7258_BOOT_N17_RECORD_COUNT       16u
#define BK7258_BOOT_N17_NO_BANK      UINT32_MAX

enum bk7258_boot_n17_phase_e
{
  BK7258_BOOT_N17_STABLE = 1,
  BK7258_BOOT_N17_PENDING = 2,
  BK7258_BOOT_N17_TRIAL = 3
};

enum bk7258_boot_n17_outcome_e
{
  BK7258_BOOT_N17_NONE = 0,
  BK7258_BOOT_N17_FACTORY = 1,
  BK7258_BOOT_N17_MIGRATED = 2,
  BK7258_BOOT_N17_CONFIRMED = 3,
  BK7258_BOOT_N17_ROLLED_BACK = 4
};

enum bk7258_boot_n17_policy_e
{
  BK7258_BOOT_N17_POLICY_UNARMED = 0,
  BK7258_BOOT_N17_POLICY_ARMED_CANONICAL = 1,
  BK7258_BOOT_N17_POLICY_ARMED_DEGRADED = 2
};

enum bk7258_boot_n17_format_e
{
  BK7258_BOOT_N17_FORMAT_FAIL_CLOSED = 0,
  BK7258_BOOT_N17_FORMAT2 = 2,
  BK7258_BOOT_N17_FORMAT3 = 3
};

enum bk7258_boot_n17_action_e
{
  BK7258_BOOT_N17_ACTION_STABLE = 0,
  BK7258_BOOT_N17_ACTION_TARGET_TRIAL = 1,
  BK7258_BOOT_N17_ACTION_STABLE_TARGET_REJECTED = 2,
  BK7258_BOOT_N17_ACTION_STABLE_TRIAL_CONSUMED = 3,
  BK7258_BOOT_N17_ACTION_SIGNED_FALLBACK = 4,
  BK7258_BOOT_N17_ACTION_FAIL_CLOSED = 5
};

struct bk7258_boot_n17_record_s
{
  enum bk7258_boot_n17_phase_e phase;
  enum bk7258_boot_n17_outcome_e outcome;
  uint8_t stable_slot;
  uint8_t target_slot;
  uint64_t sequence;
  uint64_t generation;
  uint64_t accepted_security_counter;
  uint32_t created_timestamp;
  uint8_t slot_a_manifest_sha256[32];
  uint8_t slot_b_manifest_sha256[32];
  uint8_t previous_record_sha256[32];
};

struct bk7258_boot_n17_bank_s
{
  struct bk7258_boot_n17_record_s first;
  struct bk7258_boot_n17_record_s last;
  uint32_t valid_records;
  uint32_t tail_slot;
  bool eligible;
  bool tail_dirty;
};

struct bk7258_boot_n17_selection_s
{
  struct bk7258_boot_n17_bank_s bank;
  uint32_t bank_index;
  bool present;
  bool rejected_newer;
};

struct bk7258_boot_n17_slot_evidence_s
{
  uint8_t manifest_sha256[32];
  uint64_t security_counter;
  bool valid;
};

struct bk7258_boot_n17_boot_decision_s
{
  enum bk7258_boot_n17_action_e action;
  uint8_t slot;
};

struct bk7258_boot_n17_journal_ops_s
{
  void *arg;
  bk7258_boot_n17_sha256_t sha256;
};

int bk7258_boot_n17_record_parse(
  const uint8_t record[BK7258_BOOT_N17_RECORD_SIZE],
  struct bk7258_boot_n17_record_s *parsed);

int bk7258_boot_n17_record_encode(
  const struct bk7258_boot_n17_record_s *record,
  uint8_t encoded[BK7258_BOOT_N17_RECORD_SIZE]);

int bk7258_boot_n17_bank_inspect(
  const uint8_t bank[BK7258_BOOT_N17_BANK_SIZE],
  const struct bk7258_boot_n17_journal_ops_s *ops,
  struct bk7258_boot_n17_bank_s *info);

int bk7258_boot_n17_bank_select(
  const uint8_t bank0[BK7258_BOOT_N17_BANK_SIZE],
  const uint8_t bank1[BK7258_BOOT_N17_BANK_SIZE],
  const struct bk7258_boot_n17_journal_ops_s *ops,
  struct bk7258_boot_n17_selection_s *selection);

int bk7258_boot_n17_policy_classify(
  const uint8_t sector[BK7258_BOOT_N17_BANK_SIZE],
  enum bk7258_boot_n17_policy_e *policy);

enum bk7258_boot_n17_format_e bk7258_boot_n17_select_format(
  enum bk7258_boot_n17_policy_e policy, bool format3_present,
  bool format2_valid);

int bk7258_boot_n17_counter_validate(
  const struct bk7258_boot_n17_record_s *record,
  const uint64_t counters[2]);

int bk7258_boot_n17_decide_boot(
  const struct bk7258_boot_n17_record_s *record,
  const struct bk7258_boot_n17_slot_evidence_s evidence[2],
  struct bk7258_boot_n17_boot_decision_s *decision);

#endif /* BK7258_BOOT_N17_JOURNAL_CORE_H */
