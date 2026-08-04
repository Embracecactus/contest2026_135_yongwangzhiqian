/****************************************************************************
 * BK7258 N15-C portable boot-selector host harness.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "boot_ota_select_core.h"
#include "bk7258_partition_layout.h"

#define PRIMARY_START    BK7258_ROLE_SLOT_A_CP_OFFSET
#define PRIMARY_SIZE     BK7258_ROLE_SLOT_B_PAIR_SIZE
#define SECONDARY_START  BK7258_ROLE_SLOT_B_PAIR_OFFSET
#define SECONDARY_SIZE   BK7258_ROLE_SLOT_B_PAIR_SIZE

struct host_hash_s
{
  EVP_MD_CTX *context;
};

struct host_context_s
{
  uint8_t *primary;
  uint8_t *secondary;
  const char *mode;
  uint32_t reads;
  uint32_t primary_reads;
  uint32_t secondary_reads;
  bool outside_access;
  bool injected;
};

static void die(const char *message)
{
  fprintf(stderr, "BK7258 N15-C harness FAIL: %s\n", message);
  exit(2);
}

static uint8_t *read_file(const char *path, size_t expected_size)
{
  FILE *stream;
  uint8_t *data;
  long size;

  stream = fopen(path, "rb");
  if (stream == NULL || fseek(stream, 0, SEEK_END) != 0)
    {
      die("cannot open input");
    }

  size = ftell(stream);
  if (size < 0 || (size_t)size != expected_size ||
      fseek(stream, 0, SEEK_SET) != 0)
    {
      die("input size mismatch");
    }

  data = malloc(expected_size);
  if (data == NULL || fread(data, 1, expected_size, stream) != expected_size)
    {
      die("cannot read input");
    }

  fclose(stream);
  return data;
}

static void hash_init(void *context)
{
  struct host_hash_s *hash = context;

  hash->context = EVP_MD_CTX_new();
  if (hash->context == NULL ||
      EVP_DigestInit_ex(hash->context, EVP_sha256(), NULL) != 1)
    {
      die("OpenSSL SHA-256 init failed");
    }
}

static void hash_update(void *context, const uint8_t *data, size_t len)
{
  struct host_hash_s *hash = context;

  if (EVP_DigestUpdate(hash->context, data, len) != 1)
    {
      die("OpenSSL SHA-256 update failed");
    }
}

static void hash_final(void *context, uint8_t digest[32])
{
  struct host_hash_s *hash = context;
  unsigned int length = 0;

  if (EVP_DigestFinal_ex(hash->context, digest, &length) != 1 || length != 32)
    {
      die("OpenSSL SHA-256 final failed");
    }

  EVP_MD_CTX_free(hash->context);
  hash->context = NULL;
}

static int raw_read(void *arg, uint32_t address, uint8_t *buffer, size_t len)
{
  struct host_context_s *context = arg;
  const uint8_t *source;
  uint32_t start;
  uint32_t size;

  context->reads++;
  if (address >= PRIMARY_START && address - PRIMARY_START <= PRIMARY_SIZE &&
      len <= PRIMARY_SIZE - (address - PRIMARY_START))
    {
      source = context->primary;
      start = PRIMARY_START;
      size = PRIMARY_SIZE;
      context->primary_reads++;
      if (!context->injected &&
          strcmp(context->mode, "read-error-primary") == 0)
        {
          context->injected = true;
          return -EIO;
        }
    }
  else if (address >= SECONDARY_START &&
           address - SECONDARY_START <= SECONDARY_SIZE &&
           len <= SECONDARY_SIZE - (address - SECONDARY_START))
    {
      source = context->secondary;
      start = SECONDARY_START;
      size = SECONDARY_SIZE;
      context->secondary_reads++;
      if (!context->injected &&
          strcmp(context->mode, "read-error-secondary") == 0)
        {
          context->injected = true;
          return -EIO;
        }
    }
  else
    {
      context->outside_access = true;
      return -EINVAL;
    }

  if (address - start > size || len > size - (address - start))
    {
      context->outside_access = true;
      return -EINVAL;
    }

  memcpy(buffer, source + address - start, len);
  if (!context->injected && strcmp(context->mode, "short-read") == 0)
    {
      context->injected = true;
      return 1;
    }

  return 0;
}

static bool parse_bool(const char *value)
{
  if (strcmp(value, "0") == 0)
    {
      return false;
    }

  if (strcmp(value, "1") != 0)
    {
      die("boolean argument must be 0 or 1");
    }

  return true;
}

int main(int argc, char **argv)
{
  struct bk7258_ota_hash_ops_s hash_ops =
  {
    .context_size = sizeof(struct host_hash_s),
    .init = hash_init,
    .update = hash_update,
    .final = hash_final
  };
  struct bk7258_boot_ota_raw_ops_s raw_ops;
  struct bk7258_boot_ota_result_s result;
  struct host_context_s context;
  uint8_t *metadata;
  uint8_t *scratch;
  bool expect_error;
  bool expected_metadata_valid;
  bool expected_primary_full;
  bool expected_secondary;
  long expected_decision;
  long expected_reason;
  int ret;

  if (argc != 11)
    {
      die("usage: harness metadata primary secondary ok|error decision reason "
          "metadata_valid primary_full secondary_verified mode");
    }

  memset(&context, 0, sizeof(context));
  context.mode = argv[10];
  metadata = read_file(argv[1], BK7258_BOOT_OTA_METADATA_SIZE);
  context.primary = read_file(argv[2], PRIMARY_SIZE);
  context.secondary = read_file(argv[3], SECONDARY_SIZE);
  scratch = malloc(BK7258_OTA_STAGE_SCRATCH_SIZE);
  if (scratch == NULL)
    {
      die("scratch allocation failed");
    }

  if (strcmp(argv[4], "ok") == 0)
    {
      expect_error = false;
    }
  else if (strcmp(argv[4], "error") == 0)
    {
      expect_error = true;
    }
  else
    {
      die("return expectation must be ok or error");
    }

  expected_decision = strtol(argv[5], NULL, 0);
  expected_reason = strtol(argv[6], NULL, 0);
  expected_metadata_valid = parse_bool(argv[7]);
  expected_primary_full = parse_bool(argv[8]);
  expected_secondary = parse_bool(argv[9]);
  raw_ops.arg = &context;
  raw_ops.read = raw_read;

  ret = bk7258_boot_ota_select_core(metadata, &raw_ops, &hash_ops, scratch,
                                    BK7258_OTA_STAGE_SCRATCH_SIZE, &result);
  if ((expect_error && ret >= 0) || (!expect_error && ret != 0) ||
      result.status != ret || context.outside_access || context.reads == 0 ||
      result.decision != (enum bk7258_boot_ota_decision_e)expected_decision ||
      result.reason != (enum bk7258_boot_ota_reason_e)expected_reason ||
      result.metadata_valid != expected_metadata_valid ||
      result.primary_full_verified != expected_primary_full ||
      result.secondary_verified != expected_secondary ||
      (expect_error && result.primary_verified) ||
      (!expect_error && !result.primary_verified))
    {
      die("selector result invariant failed");
    }

  if ((result.primary_full_verified && context.primary_reads < 2) ||
      (result.secondary_verified && context.secondary_reads == 0))
    {
      die("verification result is not backed by bounded reads");
    }

  printf("BK7258 N15-C harness PASS mode=%s status=%d decision=%d reason=%d "
         "records=%u generation=%llu primary_full=%u secondary=%u "
         "reads=%u/%u\n",
         context.mode, ret, (int)result.decision, (int)result.reason,
         result.valid_records, (unsigned long long)result.generation,
         result.primary_full_verified ? 1u : 0u,
         result.secondary_verified ? 1u : 0u, context.primary_reads,
         context.secondary_reads);

  free(scratch);
  free(context.secondary);
  free(context.primary);
  free(metadata);
  return 0;
}
