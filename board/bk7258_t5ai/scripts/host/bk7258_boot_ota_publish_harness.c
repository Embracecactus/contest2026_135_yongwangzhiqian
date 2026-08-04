/*
 * Host adapter for the portable BK7258 N15-E pending publisher.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "boot_ota_publish_core.h"
#include "boot_ota_trial_core.h"
#include "bk7258_partition_layout.h"

#define PRIMARY_START   BK7258_ROLE_SLOT_A_CP_OFFSET
#define PRIMARY_SIZE    BK7258_ROLE_SLOT_B_PAIR_SIZE
#define SECONDARY_START BK7258_ROLE_SLOT_B_PAIR_OFFSET
#define SECONDARY_SIZE  BK7258_ROLE_SLOT_B_PAIR_SIZE

struct host_hash_s
{
  EVP_MD_CTX *context;
};

struct host_context_s
{
  uint8_t flash[BK7258_BOOT_OTA_METADATA_SIZE];
  uint8_t *primary;
  uint8_t *secondary;
  const char *mode;
  long fault_chunk;
  uint64_t now;
  uint32_t lock_calls;
  uint32_t unlock_calls;
  uint32_t metadata_read_calls;
  uint32_t raw_read_calls;
  uint32_t erase_calls;
  uint32_t write_calls;
  uint32_t chunk_read_calls;
  bool compile_gate;
  bool runtime_gate;
  bool primary_active;
  bool publishing;
  bool locked;
  bool outside_access;
};

static void die(const char *message)
{
  fprintf(stderr, "BK7258 N15-E harness FAIL: %s\n", message);
  exit(2);
}

static uint8_t *read_file(const char *path, size_t expected_size)
{
  FILE *stream = fopen(path, "rb");
  uint8_t *data;
  long size;

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

static void write_file(const char *path, const uint8_t *data, size_t len)
{
  FILE *stream = fopen(path, "wb");

  if (stream == NULL || fwrite(data, 1, len, stream) != len ||
      fclose(stream) != 0)
    {
      die("cannot write metadata result");
    }
}

static uint64_t host_now(void *arg)
{
  return ((struct host_context_s *)arg)->now;
}

static bool compile_gate(void *arg)
{
  return ((struct host_context_s *)arg)->compile_gate;
}

static bool runtime_gate(void *arg)
{
  return ((struct host_context_s *)arg)->runtime_gate;
}

static bool primary_active(void *arg)
{
  return ((struct host_context_s *)arg)->primary_active;
}

static int flash_lock(void *arg, uint32_t timeout_ms)
{
  struct host_context_s *context = arg;

  context->lock_calls++;
  if (strcmp(context->mode, "lock-error") == 0)
    {
      return -ETIMEDOUT;
    }

  if (strcmp(context->mode, "lock-short") == 0)
    {
      return 1;
    }

  if (context->locked || timeout_ms == 0)
    {
      return -EINVAL;
    }

  context->locked = true;
  return 0;
}

static void flash_unlock(void *arg)
{
  struct host_context_s *context = arg;

  if (!context->locked)
    {
      context->outside_access = true;
      return;
    }

  context->locked = false;
  context->unlock_calls++;
}

static bool metadata_range(uint32_t address, size_t len, uint32_t *offset)
{
  if (address < BK7258_BOOT_OTA_METADATA_START ||
      address - BK7258_BOOT_OTA_METADATA_START >
        BK7258_BOOT_OTA_METADATA_SIZE ||
      len > BK7258_BOOT_OTA_METADATA_SIZE -
            (address - BK7258_BOOT_OTA_METADATA_START))
    {
      return false;
    }

  *offset = address - BK7258_BOOT_OTA_METADATA_START;
  return true;
}

static int flash_read(void *arg, uint32_t address, uint8_t *data, size_t len)
{
  struct host_context_s *context = arg;
  uint32_t offset;

  if (!context->locked || data == NULL || len == 0 ||
      !metadata_range(address, len, &offset))
    {
      context->outside_access = true;
      return -EINVAL;
    }

  if (len == BK7258_BOOT_OTA_METADATA_SIZE)
    {
      uint32_t call = context->metadata_read_calls++;

      if ((call == 0 && strcmp(context->mode, "initial-read-error") == 0) ||
          (call == 1 && strcmp(context->mode, "erase-read-error") == 0) ||
          (context->write_calls == BK7258_BOOT_OTA_PROGRAM_CHUNKS &&
           strcmp(context->mode, "final-read-error") == 0))
        {
          return -ETIMEDOUT;
        }

      memcpy(data, context->flash + offset, len);
      if ((call == 0 && strcmp(context->mode, "initial-read-short") == 0) ||
          (call == 1 && strcmp(context->mode, "erase-read-short") == 0) ||
          (context->write_calls == BK7258_BOOT_OTA_PROGRAM_CHUNKS &&
           strcmp(context->mode, "final-read-short") == 0))
        {
          return 1;
        }

      if (call == 1 && strcmp(context->mode, "erase-read-mismatch") == 0)
        {
          data[0] = 0;
        }
      else if (context->write_calls == BK7258_BOOT_OTA_PROGRAM_CHUNKS &&
               strcmp(context->mode, "final-read-mismatch") == 0)
        {
          data[0] ^= 1;
        }

      return 0;
    }

  if (len == BK7258_BOOT_OTA_PROGRAM_GRANULE)
    {
      long call = (long)context->chunk_read_calls++;

      if (call == context->fault_chunk &&
          strcmp(context->mode, "chunk-read-error") == 0)
        {
          return -ETIMEDOUT;
        }

      memcpy(data, context->flash + offset, len);
      if (call == context->fault_chunk &&
          strcmp(context->mode, "chunk-read-mismatch") == 0)
        {
          data[0] ^= 1;
        }

      return call == context->fault_chunk &&
             strcmp(context->mode, "chunk-read-short") == 0 ? 1 : 0;
    }

  context->outside_access = true;
  return -EINVAL;
}

static int flash_erase(void *arg, uint32_t address)
{
  struct host_context_s *context = arg;
  size_t count = BK7258_BOOT_OTA_METADATA_SIZE;

  context->erase_calls++;
  if (!context->locked || address != BK7258_BOOT_OTA_METADATA_START)
    {
      context->outside_access = true;
      return -EINVAL;
    }

  if (strcmp(context->mode, "erase-before-error") == 0)
    {
      return -ETIMEDOUT;
    }

  if (strcmp(context->mode, "erase-incomplete-error") == 0)
    {
      count = BK7258_BOOT_OTA_RECORD_SIZE;
    }

  memset(context->flash, 0xff, count);
  if (strcmp(context->mode, "erase-after-error") == 0 ||
      strcmp(context->mode, "erase-incomplete-error") == 0)
    {
      return -ETIMEDOUT;
    }

  if (strcmp(context->mode, "timeout-after-erase") == 0)
    {
      context->now = 1000;
    }

  return strcmp(context->mode, "erase-short") == 0 ? 1 : 0;
}

static int flash_write(void *arg, uint32_t address, const uint8_t *data,
                       size_t len)
{
  struct host_context_s *context = arg;
  long call = (long)context->write_calls++;
  uint32_t offset;
  size_t count = len;
  size_t index;

  if (!context->locked || data == NULL ||
      len != BK7258_BOOT_OTA_PROGRAM_GRANULE ||
      !metadata_range(address, len, &offset) ||
      (address & (BK7258_BOOT_OTA_PROGRAM_GRANULE - 1u)) != 0)
    {
      context->outside_access = true;
      return -EINVAL;
    }

  if (call == context->fault_chunk &&
      strcmp(context->mode, "write-before-error") == 0)
    {
      return -ETIMEDOUT;
    }

  if (call == context->fault_chunk &&
      strcmp(context->mode, "write-torn-error") == 0)
    {
      count /= 2;
    }

  for (index = 0; index < count; index++)
    {
      if (((uint8_t)~context->flash[offset + index] & data[index]) != 0)
        {
          context->outside_access = true;
          return -EIO;
        }

      context->flash[offset + index] &= data[index];
    }

  if (call == context->fault_chunk &&
      (strcmp(context->mode, "write-after-error") == 0 ||
       strcmp(context->mode, "write-torn-error") == 0))
    {
      return -ETIMEDOUT;
    }

  return call == context->fault_chunk &&
         strcmp(context->mode, "write-after-short") == 0 ? 1 : 0;
}

static void hash_init(void *arg)
{
  struct host_hash_s *hash = arg;

  hash->context = EVP_MD_CTX_new();
  if (hash->context == NULL ||
      EVP_DigestInit_ex(hash->context, EVP_sha256(), NULL) != 1)
    {
      die("OpenSSL SHA-256 init failed");
    }
}

static void hash_update(void *arg, const uint8_t *data, size_t len)
{
  struct host_hash_s *hash = arg;

  if (EVP_DigestUpdate(hash->context, data, len) != 1)
    {
      die("OpenSSL SHA-256 update failed");
    }
}

static void hash_final(void *arg, uint8_t digest[32])
{
  struct host_hash_s *hash = arg;
  unsigned int len = 0;

  if (EVP_DigestFinal_ex(hash->context, digest, &len) != 1 || len != 32)
    {
      die("OpenSSL SHA-256 final failed");
    }

  EVP_MD_CTX_free(hash->context);
  hash->context = NULL;
}

static int raw_read(void *arg, uint32_t address, uint8_t *data, size_t len)
{
  struct host_context_s *context = arg;
  const uint8_t *source;
  uint32_t start;
  uint32_t size;

  context->raw_read_calls++;
  if (!context->locked || data == NULL || len == 0)
    {
      context->outside_access = true;
      return -EINVAL;
    }

  if (context->publishing && strcmp(context->mode, "raw-read-error") == 0)
    {
      return -ETIMEDOUT;
    }

  if (address >= PRIMARY_START && address - PRIMARY_START <= PRIMARY_SIZE &&
      len <= PRIMARY_SIZE - (address - PRIMARY_START))
    {
      source = context->primary;
      start = PRIMARY_START;
      size = PRIMARY_SIZE;
    }
  else if (address >= SECONDARY_START &&
           address - SECONDARY_START <= SECONDARY_SIZE &&
           len <= SECONDARY_SIZE - (address - SECONDARY_START))
    {
      source = context->secondary;
      start = SECONDARY_START;
      size = SECONDARY_SIZE;
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

  memcpy(data, source + address - start, len);
  return context->publishing && strcmp(context->mode, "raw-read-short") == 0 ?
         1 : 0;
}

int main(int argc, char **argv)
{
  struct bk7258_boot_ota_publish_ops_s publish_ops;
  struct bk7258_boot_ota_publish_result_s publish_result;
  struct bk7258_boot_ota_raw_ops_s raw_ops;
  struct bk7258_boot_ota_result_s select_result;
  struct bk7258_ota_hash_ops_s hash_ops;
  struct host_context_s context;
  uint8_t *workspace;
  uint8_t *select_scratch;
  uint8_t *input;
  uint8_t *pending;
  uint64_t generation;
  long expected_status;
  long expected_decision;
  long expected_reason;
  bool expected_reclaimed;
  bool expected_idempotent;
  int ret;

  if (argc != 14)
    {
      die("usage: harness current pending primary secondary generation mode "
          "fault_chunk status decision reason reclaimed idempotent output");
    }

  memset(&context, 0, sizeof(context));
  input = read_file(argv[1], BK7258_BOOT_OTA_METADATA_SIZE);
  memcpy(context.flash, input, sizeof(context.flash));
  free(input);
  pending = read_file(argv[2], BK7258_BOOT_OTA_RECORD_SIZE);
  context.primary = read_file(argv[3], PRIMARY_SIZE);
  context.secondary = read_file(argv[4], SECONDARY_SIZE);
  generation = strtoull(argv[5], NULL, 0);
  context.mode = argv[6];
  context.fault_chunk = strtol(argv[7], NULL, 0);
  expected_status = strtol(argv[8], NULL, 0);
  expected_decision = strtol(argv[9], NULL, 0);
  expected_reason = strtol(argv[10], NULL, 0);
  expected_reclaimed = strtol(argv[11], NULL, 0) != 0;
  expected_idempotent = strtol(argv[12], NULL, 0) != 0;
  context.compile_gate = strcmp(context.mode, "compile-disabled") != 0;
  context.runtime_gate = strcmp(context.mode, "runtime-disabled") != 0;
  context.primary_active = strcmp(context.mode, "remap-active") != 0;
  context.publishing = true;

  raw_ops.arg = &context;
  raw_ops.read = raw_read;
  hash_ops.context_size = sizeof(struct host_hash_s);
  hash_ops.init = hash_init;
  hash_ops.update = hash_update;
  hash_ops.final = hash_final;
  publish_ops.arg = &context;
  publish_ops.now_ms = host_now;
  publish_ops.compile_write_enabled = compile_gate;
  publish_ops.runtime_write_enabled = runtime_gate;
  publish_ops.primary_mapping_active = primary_active;
  publish_ops.lock = flash_lock;
  publish_ops.unlock = flash_unlock;
  publish_ops.read = flash_read;
  publish_ops.erase_sector = flash_erase;
  publish_ops.write = flash_write;
  workspace = malloc(BK7258_BOOT_OTA_PUBLISH_WORKSPACE_SIZE);
  if (workspace == NULL)
    {
      die("publisher workspace allocation failed");
    }

  ret = bk7258_boot_ota_publish_pending(
    pending, generation, &raw_ops, &hash_ops, &publish_ops, 1000,
    workspace, BK7258_BOOT_OTA_PUBLISH_WORKSPACE_SIZE, &publish_result);
  context.publishing = false;
  if (ret != expected_status || publish_result.status != ret ||
      publish_result.sector_reclaimed != expected_reclaimed ||
      publish_result.idempotent != expected_idempotent ||
      context.outside_access || context.locked || context.lock_calls > 1 ||
      context.unlock_calls > context.lock_calls ||
      (publish_result.lock_acquired && context.unlock_calls != 1) ||
      (!publish_result.lock_acquired && context.unlock_calls != 0) ||
      (ret == 0 && !publish_result.readback_verified) ||
      (ret == 0 && !publish_result.idempotent &&
       (publish_result.programmed_chunks !=
          BK7258_BOOT_OTA_PROGRAM_CHUNKS ||
        publish_result.verified_chunks !=
          BK7258_BOOT_OTA_PROGRAM_CHUNKS)))
    {
      die("publication result invariant failed");
    }

  /* Persistent boot decisions are evaluated after the simulated reset, so
   * raw reads no longer run under the publication lock. */

  context.locked = true;
  select_scratch = malloc(BK7258_OTA_STAGE_SCRATCH_SIZE);
  if (select_scratch == NULL)
    {
      die("selector scratch allocation failed");
    }

  ret = bk7258_boot_ota_select_core(
    context.flash, &raw_ops, &hash_ops, select_scratch,
    BK7258_OTA_STAGE_SCRATCH_SIZE, &select_result);
  context.locked = false;
  if (ret != 0 || select_result.status != 0 ||
      select_result.decision !=
        (enum bk7258_boot_ota_decision_e)expected_decision ||
      select_result.reason !=
        (enum bk7258_boot_ota_reason_e)expected_reason ||
      context.outside_access)
    {
      die("persistent selector invariant failed");
    }

  write_file(argv[13], context.flash, sizeof(context.flash));
  printf("BK7258 N15-E harness PASS mode=%s chunk=%ld status=%ld "
         "persistent=%ld/%ld erase=%u writes=%u reclaimed=%u idempotent=%u\n",
         context.mode, context.fault_chunk, expected_status,
         expected_decision, expected_reason, context.erase_calls,
         context.write_calls, publish_result.sector_reclaimed ? 1u : 0u,
         publish_result.idempotent ? 1u : 0u);

  free(select_scratch);
  free(workspace);
  free(context.secondary);
  free(context.primary);
  free(pending);
  return 0;
}
