/*
 * boot_n17_select.c - BK7258 target adapter for the N17 reader.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "boot_n17_journal_core.h"
#include "boot_n17_ecc_wrapper.h"
#include "boot_n17_manifest_core.h"
#include "boot_n17_select.h"
#include "boot_sha256.h"

#define N17_SHA256_SIZE 32u
#define N17_HASH_CHUNK  256u

static int n17_sha256(void *arg, const uint8_t *data, size_t len,
                      uint8_t digest[N17_SHA256_SIZE])
{
  struct boot_sha256_context_s context;

  (void)arg;
  boot_sha256_init(&context);
  boot_sha256_update(&context, data, len);
  boot_sha256_final(&context, digest);
  return 0;
}

static int n17_verify_signature(void *arg, uint32_t key_id,
                                const uint8_t signed_data[448],
                                const uint8_t signature[64])
{
  (void)arg;
  return bk7258_boot_n17_verify_signature(key_id, signed_data, signature);
}

static int n17_raw_read(const struct bk7258_boot_ota_raw_ops_s *raw_ops,
                        uint32_t address, uint8_t *data, size_t len)
{
  if (raw_ops == NULL || raw_ops->read == NULL || data == NULL || len == 0)
    {
      return -1;
    }

  return raw_ops->read(raw_ops->arg, address, data, len) == 0 ? 0 : -1;
}

static int n17_bytes_equal(const uint8_t *left, const uint8_t *right,
                           size_t len)
{
  while (len-- != 0)
    {
      if (*left++ != *right++)
        {
          return 0;
        }
    }

  return 1;
}

static int n17_hash_pair(const struct bk7258_boot_ota_raw_ops_s *raw_ops,
                         uint32_t address, uint8_t *scratch,
                         size_t scratch_size, uint8_t digest[32])
{
  struct boot_sha256_context_s context;
  uint32_t offset;

  if (scratch == NULL || scratch_size < N17_HASH_CHUNK)
    {
      return -1;
    }

  boot_sha256_init(&context);
  for (offset = 0; offset < BK7258_ROLE_SLOT_B_PAIR_SIZE;
       offset += N17_HASH_CHUNK)
    {
      if (n17_raw_read(raw_ops, address + offset, scratch,
                       N17_HASH_CHUNK) < 0)
        {
          return -1;
        }

      boot_sha256_update(&context, scratch, N17_HASH_CHUNK);
    }

  boot_sha256_final(&context, digest);
  return 0;
}

static int n17_slot_evidence(
  const struct bk7258_boot_ota_raw_ops_s *raw_ops, uint32_t pair_address,
  const struct bk7258_boot_n17_manifest_info_s *manifest,
  uint8_t *scratch, size_t scratch_size,
  struct bk7258_boot_n17_slot_evidence_s *evidence)
{
  uint8_t pair_sha256[N17_SHA256_SIZE];
  uint32_t index;

  if (manifest == NULL || evidence == NULL ||
      n17_hash_pair(raw_ops, pair_address, scratch, scratch_size,
                    pair_sha256) < 0)
    {
      return -1;
    }

  /* The evidence field is the signed Manifest digest, not the image digest.
   * Reuse the first bytes of scratch only after the physical-pair digest has
   * been computed, then replace them with the canonical signed digest below.
   */

  if (!n17_bytes_equal(pair_sha256, manifest->pair_sha256,
                       N17_SHA256_SIZE) ||
      bk7258_boot_ota_validate_pair_headers(pair_address, raw_ops, scratch,
                                            scratch_size) < 0)
    {
      evidence->valid = false;
      return 0;
    }

  /* manifest_sha256 is compared with the journal; pair_sha256 above binds
   * every physical CP/AP byte, including its Beken CRC records. */

  for (index = 0; index < N17_SHA256_SIZE; index++)
    {
      evidence->manifest_sha256[index] = manifest->signed_sha256[index];
    }

  evidence->security_counter = manifest->security_counter;
  evidence->valid = true;
  return 0;
}

int bk7258_boot_n17_select(
  const struct bk7258_boot_ota_raw_ops_s *raw_ops,
  uint8_t bank0[BK7258_BOOT_N17_BANK_SIZE],
  uint8_t bank1[BK7258_BOOT_N17_BANK_SIZE],
  uint8_t *scratch, size_t scratch_size, bool enabled, uint8_t *slot)
{
  struct bk7258_boot_n17_journal_ops_s journal_ops;
  struct bk7258_boot_n17_manifest_ops_s manifest_ops;
  struct bk7258_boot_n17_selection_s selection;
  struct bk7258_boot_n17_manifest_info_s manifests[2];
  struct bk7258_boot_n17_slot_evidence_s evidence[2];
  struct bk7258_boot_n17_boot_decision_s decision;
  enum bk7258_boot_n17_policy_e policy;
  enum bk7258_boot_n17_format_e format;
  uint64_t counters[2];
  uint32_t manifest_addresses[2] =
  {
    BK7258_ROLE_OTA_MANIFEST_A_OFFSET,
    BK7258_ROLE_OTA_MANIFEST_B_OFFSET
  };
  uint32_t pair_addresses[2] =
  {
    BK7258_ROLE_SLOT_A_CP_OFFSET,
    BK7258_ROLE_SLOT_B_PAIR_OFFSET
  };
  uint32_t index;

  if (raw_ops == NULL || bank0 == NULL || bank1 == NULL || scratch == NULL ||
      scratch_size < BK7258_BOOT_N17_MANIFEST_SIZE || slot == NULL)
    {
      return -1;
    }

  if (n17_raw_read(raw_ops, BK7258_ROLE_OTA_AUTH_POLICY_OFFSET, bank0,
                   BK7258_BOOT_N17_BANK_SIZE) < 0 ||
      bk7258_boot_n17_policy_classify(bank0, &policy) < 0 ||
      n17_raw_read(raw_ops, BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET, bank0,
                   BK7258_BOOT_N17_BANK_SIZE) < 0 ||
      n17_raw_read(raw_ops, BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET, bank1,
                   BK7258_BOOT_N17_BANK_SIZE) < 0)
    {
      return -1;
    }

  journal_ops.arg = NULL;
  journal_ops.sha256 = n17_sha256;
  if (bk7258_boot_n17_bank_select(bank0, bank1, &journal_ops,
                                  &selection) < 0)
    {
      return -1;
    }

  format = bk7258_boot_n17_select_format(policy, selection.present, true);
  if (format == BK7258_BOOT_N17_FORMAT2)
    {
      return 0;
    }

  if (format != BK7258_BOOT_N17_FORMAT3)
    {
      return -1;
    }

  /* A physically present N17 state must never silently fall back into the
   * older format.  Until both deployment gates are intentionally enabled,
   * this is a fail-closed condition rather than an invitation to boot it.
   */

  if (!enabled)
    {
      return -1;
    }

  manifest_ops.arg = NULL;
  manifest_ops.sha256 = n17_sha256;
  manifest_ops.verify_signature = n17_verify_signature;
  for (index = 0; index < 2; index++)
    {
      if (n17_raw_read(raw_ops, manifest_addresses[index], scratch,
                       BK7258_BOOT_N17_MANIFEST_SIZE) < 0 ||
          bk7258_boot_n17_manifest_verify(
            scratch, BK7258_BOOT_N17_MANIFEST_SIZE, 1, &manifest_ops,
            &manifests[index]) < 0)
        {
          return -1;
        }

      counters[index] = manifests[index].security_counter;
    }

  if (bk7258_boot_n17_counter_validate(&selection.bank.last, counters) < 0)
    {
      return -1;
    }

  for (index = 0; index < 2; index++)
    {
      if (n17_slot_evidence(raw_ops, pair_addresses[index],
                            &manifests[index], scratch, scratch_size,
                            &evidence[index]) < 0)
        {
          return -1;
        }
    }

  if (bk7258_boot_n17_decide_boot(&selection.bank.last, evidence,
                                  &decision) < 0 ||
      decision.action == BK7258_BOOT_N17_ACTION_FAIL_CLOSED ||
      decision.slot > 1)
    {
      return -1;
    }

  *slot = decision.slot;
  return 1;
}
