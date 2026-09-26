/* SPDX-License-Identifier: Apache-2.0 */
#include "bk7258_pc_grants.h"
#include <errno.h>
#include <string.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

static bool zero(const uint8_t *data, size_t size)
{
  uint8_t bits = 0;
  for (size_t i = 0; i < size; i++) bits |= data[i];
  return bits == 0;
}
static uint32_t caps(const uint8_t *record)
{
  return (uint32_t)record[4] << 24 | (uint32_t)record[5] << 16 |
         (uint32_t)record[6] << 8 | record[7];
}
static int binding(const uint8_t key[32], uint8_t out[32])
{
  static const char domain[] = "SHANIU-PC-OWNER-v1";
  uint8_t material[sizeof(domain) - 1 + 32];
  memcpy(material, domain, sizeof(domain) - 1);
  memcpy(material + sizeof(domain) - 1, key, 32);
  int ret = mbedtls_sha256(material, sizeof(material), out, 0);
  mbedtls_platform_zeroize(material, sizeof(material));
  return ret == 0 ? 0 : -EIO;
}
static bool active(const struct bkpc_grants_s *state)
{
  return caps(state->record) != 0 &&
         !memcmp(state->record + 8, state->owner_binding, 32);
}
static int ready(const struct bkpc_grants_s *state)
{
  if (state == NULL) return -EINVAL;
  if (state->uncertain) return -EINPROGRESS;
  return state->ready ? 0 : -ENODEV;
}
int bkpc_grants_open(struct bkpc_grants_s *state, const char *root,
                     const uint8_t owner_key[32])
{
  uint8_t record[BKPC_GRANT_RECORD_SIZE] = {0};
  uint8_t fingerprint[32] = {0};
  size_t size = 0;
  int ret;
  if (!state || !owner_key || zero(owner_key, 32)) return -EINVAL;
  if (state->uncertain) return -EINPROGRESS;
  if (state->ready) return -EALREADY;
  ret = binding(owner_key, state->owner_binding);
  if (ret) return ret;
  ret = bkprov_store_open(&state->store, root);
  if (ret) return ret;
  ret = bkprov_store_load(&state->store, record, sizeof(record), &size,
                          &state->revision, state->transaction);
  if (ret == -ENOENT)
    {
      state->revision = 0;
      memset(state->transaction, 0, 16);
      memset(record, 0, sizeof(record));
      memcpy(record, "PCG1", 4);
      memcpy(record + 8, state->owner_binding, 32);
      ret = 0;
    }
  else if (ret == 0)
    {
      uint32_t capability = caps(record);
      if (size != sizeof(record) || memcmp(record, "PCG1", 4) ||
          zero(state->transaction, 16) || zero(record + 8, 32) ||
          (capability & ~BKPC_CAP_ALL) ||
          (capability && (zero(record + 40, 16) || zero(record + 56, 32))) ||
          (!capability && !zero(record + 40, 48))) ret = -EPROTO;
      if (!ret && capability)
        {
          ret = binding(record + 56, fingerprint);
          if (!ret && !memcmp(fingerprint, record + 8, 32)) ret = -EPROTO;
        }
    }
  if (!ret)
    {
      if (memcmp(record + 8, state->owner_binding, 32))
        {
          /* Retain revision/receipt, never cache the old principal's secret. */
          memset(record + 4, 0, 4);
          mbedtls_platform_zeroize(record + 40, 48);
        }
      memcpy(state->record, record, sizeof(record));
      state->ready = true;
    }
  mbedtls_platform_zeroize(record, sizeof(record));
  mbedtls_platform_zeroize(fingerprint, sizeof(fingerprint));
  return ret;
}
int bkpc_grants_set(struct bkpc_grants_s *state, uint64_t expected,
                    const uint8_t transaction[16], const uint8_t client[16],
                    const uint8_t key[32], uint32_t capability)
{
  uint8_t record[BKPC_GRANT_RECORD_SIZE] = {'P','C','G','1'};
  uint8_t fingerprint[32] = {0};
  int ret = ready(state);
  if (ret) return ret;
  if (!transaction || zero(transaction, 16) || (capability & ~BKPC_CAP_ALL) ||
      (capability && (!client || !key || zero(client, 16) || zero(key, 32))) ||
      (!capability && (client || key))) return -EINVAL;
  if (capability)
    {
      ret = binding(key, fingerprint);
      if (!ret && !memcmp(fingerprint, state->owner_binding, 32)) ret = -EACCES;
      mbedtls_platform_zeroize(fingerprint, sizeof(fingerprint));
      if (ret) return ret;
      record[7] = capability;
      memcpy(record + 40, client, 16);
      memcpy(record + 56, key, 32);
    }
  memcpy(record + 8, state->owner_binding, 32);
  if (!memcmp(transaction, state->transaction, 16))
    {
      ret = memcmp(record, state->record, sizeof(record)) ? -EEXIST :
            expected != UINT64_MAX && state->revision == expected + 1 ? 0 : -ESTALE;
    }
  else if (expected != state->revision) ret = -ESTALE;
  else if (expected == UINT64_MAX) ret = -EOVERFLOW;
  else
    {
      ret = bkprov_store_commit(&state->store, expected, transaction,
                                record, sizeof(record));
      if (ret == -EINPROGRESS)
        {
          state->uncertain = true;
          mbedtls_platform_zeroize(state->record + 40, 48);
        }
      if (!ret)
        {
          memcpy(state->record, record, sizeof(record));
          memcpy(state->transaction, transaction, 16);
          state->revision = expected + 1;
        }
    }
  mbedtls_platform_zeroize(record, sizeof(record));
  return ret;
}
int bkpc_grants_snapshot(const struct bkpc_grants_s *state, uint64_t *revision,
                         uint8_t client[16], uint32_t *capability)
{
  if (revision) *revision = 0;
  if (client) memset(client, 0, 16);
  if (capability) *capability = 0;
  if (!revision || !client || !capability) return -EINVAL;
  int ret = ready(state);
  if (ret) return ret;
  *revision = state->revision;
  if (active(state))
    {
      memcpy(client, state->record + 40, 16);
      *capability = caps(state->record);
    }
  return 0;
}
int bkpc_grants_key(const struct bkpc_grants_s *state, uint64_t expected,
                    uint8_t key[32], uint32_t *capability)
{
  if (key) mbedtls_platform_zeroize(key, 32);
  if (capability) *capability = 0;
  if (!key || !capability) return -EINVAL;
  int ret = ready(state);
  if (ret) return ret;
  if (expected != state->revision) return -ESTALE;
  if (!active(state)) return -EACCES;
  memcpy(key, state->record + 56, 32);
  *capability = caps(state->record);
  return 0;
}
