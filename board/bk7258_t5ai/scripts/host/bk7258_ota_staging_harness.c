/****************************************************************************
 * BK7258 N15-B portable staging-core host fault-injection harness.
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

#include "bk7258_ota_staging_core.h"
#include "bk7258_partition_layout.h"

#define FLASH_SIZE       BK7258_FLASH_SIZE
#define STAGING_START    BK7258_ROLE_SLOT_B_PAIR_OFFSET
#define STAGING_SIZE     BK7258_ROLE_SLOT_B_PAIR_SIZE
#define SECTOR_SIZE      BK7258_FLASH_ERASE_SIZE
#define SENTINEL         0xa5u

struct host_hash_s
{
  EVP_MD_CTX *context;
};

struct host_context_s
{
  uint8_t *source;
  uint8_t *flash;
  size_t source_size;
  const char *mode;
  uint64_t now_ms;
  uint32_t zero_reads;
  uint32_t erase_count;
  uint32_t write_count;
  uint32_t bytes_written;
  uint32_t lock_count;
  uint32_t unlock_count;
  uint32_t min_address;
  uint32_t max_end;
  uint32_t staging_start;
  bool compile_gate;
  bool runtime_gate;
  bool locked;
  bool outside_access;
  bool source_swapped;
};

static void die(const char *message)
{
  fprintf(stderr, "BK7258 N15-B harness FAIL: %s\n", message);
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
  if (size < 0 || (expected_size != 0 && (size_t)size != expected_size) ||
      fseek(stream, 0, SEEK_SET) != 0)
    {
      die("input size mismatch");
    }

  data = malloc((size_t)size);
  if (data == NULL || fread(data, 1, (size_t)size, stream) != (size_t)size)
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

static int source_read(void *arg, uint32_t offset, uint8_t *buffer, size_t len)
{
  struct host_context_s *context = arg;

  if (offset > context->source_size || len > context->source_size - offset)
    {
      return -EINVAL;
    }

  if (strcmp(context->mode, "source-short") == 0 && offset != 0)
    {
      return 1;
    }

  if (offset == 0)
    {
      context->zero_reads++;
      if (strcmp(context->mode, "source-swap") == 0 &&
          context->zero_reads == 2 && !context->source_swapped)
        {
          context->source[0] ^= 1;
          context->source_swapped = true;
        }
    }

  memcpy(buffer, context->source + offset, len);
  return 0;
}

static uint64_t now_ms(void *arg)
{
  struct host_context_s *context = arg;

  context->now_ms++;
  return context->now_ms;
}

static bool compile_enabled(void *arg)
{
  return ((struct host_context_s *)arg)->compile_gate;
}

static bool runtime_enabled(void *arg)
{
  return ((struct host_context_s *)arg)->runtime_gate;
}

static bool range_ok(struct host_context_s *context, uint32_t address,
                     size_t len)
{
  if (address < context->staging_start || len > STAGING_SIZE ||
      address - context->staging_start > STAGING_SIZE - len)
    {
      context->outside_access = true;
      return false;
    }

  if (address < context->min_address)
    {
      context->min_address = address;
    }

  if (address + len > context->max_end)
    {
      context->max_end = address + (uint32_t)len;
    }

  return true;
}

static int flash_lock(void *arg, uint32_t timeout_ms)
{
  struct host_context_s *context = arg;

  if (timeout_ms == 0 || context->locked)
    {
      return -EINVAL;
    }

  if (strcmp(context->mode, "lock-timeout") == 0)
    {
      return -ETIMEDOUT;
    }

  context->locked = true;
  context->lock_count++;
  return 0;
}

static void flash_unlock(void *arg)
{
  struct host_context_s *context = arg;

  if (!context->locked)
    {
      die("unbalanced unlock");
    }

  context->locked = false;
  context->unlock_count++;
}

static int flash_erase(void *arg, uint32_t address)
{
  struct host_context_s *context = arg;

  if (!context->locked || (address & (SECTOR_SIZE - 1u)) != 0 ||
      !range_ok(context, address, SECTOR_SIZE))
    {
      return -EINVAL;
    }

  if (strcmp(context->mode, "erase-fail") == 0 &&
      context->erase_count == 0)
    {
      return -EIO;
    }

  memset(context->flash + address, 0xff, SECTOR_SIZE);
  context->erase_count++;
  return 0;
}

static int flash_write(void *arg, uint32_t address, const uint8_t *data,
                       size_t len)
{
  struct host_context_s *context = arg;
  size_t index;

  if (!context->locked || !range_ok(context, address, len))
    {
      return -EINVAL;
    }

  if (strcmp(context->mode, "write-fail") == 0 &&
      context->write_count == 0)
    {
      return -EIO;
    }

  for (index = 0; index < len; index++)
    {
      if ((uint8_t)(context->flash[address + index] | data[index]) !=
          context->flash[address + index])
        {
          return -EIO;
        }

      context->flash[address + index] &= data[index];
    }

  context->write_count++;
  context->bytes_written += (uint32_t)len;
  return 0;
}

static int flash_read(void *arg, uint32_t address, uint8_t *data, size_t len)
{
  struct host_context_s *context = arg;

  if (!context->locked || !range_ok(context, address, len))
    {
      return -EINVAL;
    }

  memcpy(data, context->flash + address, len);
  if (strcmp(context->mode, "erase-verify") == 0 &&
      context->erase_count == 1 && context->write_count == 0)
    {
      data[0] = 0;
    }
  else if (strcmp(context->mode, "readback") == 0 &&
           context->write_count == 1)
    {
      data[0] ^= 1;
    }
  else if (strcmp(context->mode, "final-digest") == 0 &&
           context->bytes_written == STAGING_SIZE &&
           address == context->staging_start)
    {
      data[0] ^= 1;
    }

  return 0;
}

static bool untouched(const uint8_t *flash, uint32_t start, uint32_t end)
{
  uint32_t index;

  for (index = start; index < end; index++)
    {
      if (flash[index] != SENTINEL)
        {
          return false;
        }
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
  struct bk7258_ota_flash_ops_s flash_ops;
  struct bk7258_ota_source_s source;
  struct bk7258_ota_expected_s expected;
  struct bk7258_ota_stage_result_s result;
  struct host_context_s context;
  uint8_t *descriptor;
  uint8_t *scratch;
  uint32_t timeout_ms = 10000000u;
  unsigned long long generation;
  unsigned long timestamp;
  int ret;
  bool validation_only;
  bool expect_success;
  bool expect_no_mutation;
  bool slot_a_mode;
  bool active_reject_mode;

  if (argc != 8)
    {
      die("usage: harness descriptor image generation version base timestamp mode");
    }

  memset(&context, 0, sizeof(context));
  memset(&expected, 0, sizeof(expected));
  context.mode = argv[7];
  context.source_size = STAGING_SIZE;
  context.min_address = UINT32_MAX;
  slot_a_mode = strcmp(context.mode, "validate-a") == 0 ||
                strcmp(context.mode, "success-a") == 0 ||
                strcmp(context.mode, "active-reject-a") == 0;
  active_reject_mode = strcmp(context.mode, "active-reject-a") == 0 ||
                       strcmp(context.mode, "active-reject-b") == 0;
  context.staging_start = slot_a_mode ?
    BK7258_ROLE_SLOT_A_CP_OFFSET : STAGING_START;
  context.compile_gate = strcmp(context.mode, "compile-gate") != 0;
  context.runtime_gate = strcmp(context.mode, "runtime-gate") != 0;
  descriptor = read_file(argv[1], BK7258_OTA_STAGE_DESCRIPTOR_SIZE);
  context.source = read_file(argv[2], STAGING_SIZE);
  context.flash = malloc(FLASH_SIZE);
  scratch = malloc(BK7258_OTA_STAGE_SCRATCH_SIZE);
  if (context.flash == NULL || scratch == NULL)
    {
      die("allocation failed");
    }

  memset(context.flash, SENTINEL, FLASH_SIZE);
  generation = strtoull(argv[3], NULL, 0);
  timestamp = strtoul(argv[6], NULL, 0);
  expected.generation = (uint64_t)generation;
  expected.timestamp = (uint32_t)timestamp;
  if (snprintf(expected.version, sizeof(expected.version), "%s", argv[4]) >=
        (int)sizeof(expected.version) ||
      snprintf(expected.base_version, sizeof(expected.base_version), "%s",
               argv[5]) >= (int)sizeof(expected.base_version))
    {
      die("identity string too long");
    }

  source.read = source_read;
  source.arg = &context;
  source.size = STAGING_SIZE;
  flash_ops.arg = &context;
  flash_ops.now_ms = now_ms;
  flash_ops.compile_write_enabled = compile_enabled;
  flash_ops.runtime_write_enabled = runtime_enabled;
  flash_ops.lock = flash_lock;
  flash_ops.unlock = flash_unlock;
  flash_ops.erase_sector = flash_erase;
  flash_ops.write = flash_write;
  flash_ops.read = flash_read;

  validation_only = strcmp(context.mode, "validate") == 0 ||
                    strcmp(context.mode, "validate-a") == 0 ||
                    strcmp(context.mode, "validate-fail") == 0;
  expect_success = strcmp(context.mode, "validate") == 0 ||
                   strcmp(context.mode, "validate-a") == 0 ||
                   strcmp(context.mode, "success") == 0 ||
                   strcmp(context.mode, "success-a") == 0;
  expect_no_mutation = validation_only ||
                       active_reject_mode ||
                       strcmp(context.mode, "compile-gate") == 0 ||
                       strcmp(context.mode, "runtime-gate") == 0 ||
                       strcmp(context.mode, "timeout") == 0 ||
                       strcmp(context.mode, "lock-timeout") == 0 ||
                       strcmp(context.mode, "source-short") == 0;
  if (strcmp(context.mode, "timeout") == 0)
    {
      timeout_ms = 1;
    }

  if (validation_only)
    {
      ret = slot_a_mode ?
        bk7258_ota_core_validate_at(
          context.staging_start, descriptor, &expected, &source, &hash_ops,
          scratch, BK7258_OTA_STAGE_SCRATCH_SIZE, &result) :
        bk7258_ota_core_validate(descriptor, &expected, &source, &hash_ops,
                                 scratch, BK7258_OTA_STAGE_SCRATCH_SIZE,
                                 &result);
    }
  else if (slot_a_mode || active_reject_mode)
    {
      uint32_t active_start;

      if (active_reject_mode)
        {
          active_start = context.staging_start;
        }
      else
        {
          active_start = context.staging_start ==
                         BK7258_ROLE_SLOT_A_CP_OFFSET ?
            BK7258_ROLE_SLOT_B_PAIR_OFFSET :
            BK7258_ROLE_SLOT_A_CP_OFFSET;
        }

      ret = bk7258_ota_core_stage_inactive(
        context.staging_start, active_start, descriptor, &expected, &source,
        &hash_ops, &flash_ops, timeout_ms, scratch,
        BK7258_OTA_STAGE_SCRATCH_SIZE, &result);
    }
  else
    {
      ret = bk7258_ota_core_stage(descriptor, &expected, &source, &hash_ops,
                                  &flash_ops, timeout_ms, scratch,
                                  BK7258_OTA_STAGE_SCRATCH_SIZE, &result);
    }

  if ((expect_success && ret != 0) || (!expect_success && ret >= 0) ||
      context.outside_access || context.locked ||
      context.lock_count != context.unlock_count ||
      !untouched(context.flash, 0, context.staging_start) ||
      !untouched(context.flash, context.staging_start + STAGING_SIZE,
                 FLASH_SIZE) ||
      (expect_no_mutation &&
       !untouched(context.flash, context.staging_start,
                  context.staging_start + STAGING_SIZE)))
    {
      die("result or address-bound invariant failed");
    }

  if ((strcmp(context.mode, "success") == 0 ||
       strcmp(context.mode, "success-a") == 0) &&
      (memcmp(context.flash + context.staging_start, context.source,
              STAGING_SIZE) != 0 ||
       result.sectors_erased != STAGING_SIZE / SECTOR_SIZE ||
       result.bytes_programmed != STAGING_SIZE ||
       result.bytes_readback != STAGING_SIZE ||
       context.min_address != context.staging_start ||
       context.max_end != context.staging_start + STAGING_SIZE))
    {
      die("successful staging result mismatch");
    }

  printf("BK7258 N15-B harness PASS mode=%s status=%d erased=%u written=%u "
         "range=%08x..%08x\n",
         context.mode, ret, context.erase_count, context.bytes_written,
         context.min_address == UINT32_MAX ? 0 : context.min_address,
         context.max_end);
  free(scratch);
  free(context.flash);
  free(context.source);
  free(descriptor);
  return 0;
}
