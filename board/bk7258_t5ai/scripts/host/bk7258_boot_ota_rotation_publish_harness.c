/* Host harness for format-2 dual-bank pending publication. */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "boot_ota_rotation_publish_core.h"
#include "boot_ota_trial_core.h"
#include "bk7258_partition_layout.h"

struct host_hash_s
{
  EVP_MD_CTX *context;
};

struct host_context_s
{
  uint8_t banks[2][BK7258_BOOT_OTA_ROTATION_BANK_SIZE];
  uint8_t *slots[2];
  const char *mode;
  enum bk7258_boot_ota_slot_e active_slot;
  uint64_t now;
  uint32_t metadata_reads;
  uint32_t raw_reads;
  uint32_t erases;
  uint32_t writes;
  bool locked;
  bool outside_access;
};

static void die(const char *message)
{
  fprintf(stderr, "BK7258 format-2 publish harness FAIL: %s\n", message);
  exit(2);
}

static uint8_t *read_exact(const char *path, size_t size)
{
  FILE *stream = fopen(path, "rb");
  uint8_t *data = malloc(size);

  if (stream == NULL || data == NULL || fread(data, 1, size, stream) != size ||
      fgetc(stream) != EOF || fclose(stream) != 0)
    {
      die("cannot read exact input");
    }

  return data;
}

static uint64_t host_now(void *arg)
{
  return ((struct host_context_s *)arg)->now;
}

static bool compile_gate(void *arg)
{
  return strcmp(((struct host_context_s *)arg)->mode,
                "compile-off") != 0;
}

static bool runtime_gate(void *arg)
{
  return strcmp(((struct host_context_s *)arg)->mode,
                "runtime-off") != 0;
}

static enum bk7258_boot_ota_slot_e active_slot(void *arg)
{
  return ((struct host_context_s *)arg)->active_slot;
}

static int flash_lock(void *arg, uint32_t timeout_ms)
{
  struct host_context_s *context = arg;

  if (timeout_ms == 0 || strcmp(context->mode, "lock-error") == 0)
    {
      return -ETIMEDOUT;
    }

  if (context->locked)
    {
      return -EBUSY;
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
}

static bool bank_range(uint32_t address, size_t len, uint32_t *bank,
                       uint32_t *offset)
{
  uint32_t starts[2] =
  {
    BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET,
    BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET
  };
  uint32_t index;

  for (index = 0; index < 2; index++)
    {
      if (address >= starts[index] &&
          address - starts[index] <= BK7258_BOOT_OTA_ROTATION_BANK_SIZE &&
          len <= BK7258_BOOT_OTA_ROTATION_BANK_SIZE -
                 (address - starts[index]))
        {
          *bank = index;
          *offset = address - starts[index];
          return true;
        }
    }

  return false;
}

static bool slot_range(uint32_t address, size_t len, uint32_t *slot,
                       uint32_t *offset)
{
  uint32_t starts[2] =
  {
    BK7258_ROLE_SLOT_A_CP_OFFSET,
    BK7258_ROLE_SLOT_B_PAIR_OFFSET
  };
  uint32_t index;

  for (index = 0; index < 2; index++)
    {
      if (address >= starts[index] &&
          address - starts[index] <= BK7258_ROLE_SLOT_B_PAIR_SIZE &&
          len <= BK7258_ROLE_SLOT_B_PAIR_SIZE - (address - starts[index]))
        {
          *slot = index;
          *offset = address - starts[index];
          return true;
        }
    }

  return false;
}

static int metadata_read(void *arg, uint32_t address, uint8_t *data,
                         size_t len)
{
  struct host_context_s *context = arg;
  uint32_t bank;
  uint32_t offset;

  if (!context->locked || data == NULL || len == 0 ||
      !bank_range(address, len, &bank, &offset))
    {
      context->outside_access = true;
      return -EINVAL;
    }

  context->metadata_reads++;
  if (strcmp(context->mode, "metadata-read-error") == 0 &&
      context->metadata_reads == 2)
    {
      return -EIO;
    }

  memcpy(data, context->banks[bank] + offset, len);
  if (strcmp(context->mode, "readback-corrupt") == 0 &&
      context->writes != 0 && len <= BK7258_BOOT_OTA_ROTATION_RECORD_SIZE)
    {
      data[0] ^= 1;
    }

  return 0;
}

static int raw_read(void *arg, uint32_t address, uint8_t *data, size_t len)
{
  struct host_context_s *context = arg;
  uint32_t slot;
  uint32_t offset;

  if (!context->locked || data == NULL || len == 0 ||
      !slot_range(address, len, &slot, &offset))
    {
      context->outside_access = true;
      return -EINVAL;
    }

  context->raw_reads++;
  memcpy(data, context->slots[slot] + offset, len);
  return 0;
}

static int flash_erase(void *arg, uint32_t address)
{
  struct host_context_s *context = arg;
  uint32_t bank;
  uint32_t offset;

  if (!context->locked ||
      !bank_range(address, BK7258_BOOT_OTA_ROTATION_BANK_SIZE,
                  &bank, &offset) || offset != 0)
    {
      context->outside_access = true;
      return -EINVAL;
    }

  context->erases++;
  if (strcmp(context->mode, "erase-error") == 0)
    {
      return -EIO;
    }

  memset(context->banks[bank], 0xff,
         BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  return 0;
}

static int flash_write(void *arg, uint32_t address, const uint8_t *data,
                       size_t len)
{
  struct host_context_s *context = arg;
  uint32_t bank;
  uint32_t offset;
  size_t index;

  if (!context->locked || data == NULL ||
      len != BK7258_BOOT_OTA_PROGRAM_GRANULE ||
      !bank_range(address, len, &bank, &offset))
    {
      context->outside_access = true;
      return -EINVAL;
    }

  context->writes++;
  if (strcmp(context->mode, "write-error") == 0 && context->writes == 4)
    {
      return -EIO;
    }

  for (index = 0; index < len; index++)
    {
      if (((uint8_t)~context->banks[bank][offset + index] & data[index]) != 0)
        {
          context->outside_access = true;
          return -EIO;
        }

      context->banks[bank][offset + index] &= data[index];
    }

  return 0;
}

static void hash_init(void *arg)
{
  struct host_hash_s *hash = arg;

  hash->context = EVP_MD_CTX_new();
  if (hash->context == NULL ||
      EVP_DigestInit_ex(hash->context, EVP_sha256(), NULL) != 1)
    {
      die("SHA-256 init failed");
    }
}

static void hash_update(void *arg, const uint8_t *data, size_t len)
{
  struct host_hash_s *hash = arg;

  if (hash->context == NULL ||
      EVP_DigestUpdate(hash->context, data, len) != 1)
    {
      die("SHA-256 update failed");
    }
}

static void hash_final(void *arg, uint8_t digest[32])
{
  struct host_hash_s *hash = arg;
  unsigned int len = 0;

  if (hash->context == NULL ||
      EVP_DigestFinal_ex(hash->context, digest, &len) != 1 || len != 32)
    {
      die("SHA-256 final failed");
    }

  EVP_MD_CTX_free(hash->context);
  hash->context = NULL;
}

int main(int argc, char **argv)
{
  struct bk7258_boot_ota_rotation_publish_result_s result;
  struct bk7258_boot_ota_rotation_publish_ops_s ops;
  struct bk7258_boot_ota_rotation_bank_s infos[2];
  struct bk7258_boot_ota_rotation_view_s before;
  struct bk7258_boot_ota_rotation_view_s after;
  struct bk7258_boot_ota_raw_ops_s raw_ops;
  struct bk7258_ota_hash_ops_s hash_ops;
  struct host_context_s context;
  uint8_t original[2][BK7258_BOOT_OTA_ROTATION_BANK_SIZE];
  uint8_t workspace[BK7258_BOOT_OTA_ROTATION_PUBLISH_WORKSPACE_SIZE];
  uint8_t *record;
  uint64_t generation;
  long expected_bank;
  bool expect_error;
  int before_ret;
  int ret;
  uint32_t index;

  if (argc != 13)
    {
      die("usage: bank0 bank1 slotA slotB record generation active mode "
          "ok|error expected-bank end spare");
    }

  memset(&context, 0, sizeof(context));
  record = read_exact(argv[5], BK7258_BOOT_OTA_ROTATION_RECORD_SIZE);
  context.slots[0] = read_exact(argv[3], BK7258_ROLE_SLOT_B_PAIR_SIZE);
  context.slots[1] = read_exact(argv[4], BK7258_ROLE_SLOT_B_PAIR_SIZE);
  {
    uint8_t *input0 = read_exact(argv[1], BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
    uint8_t *input1 = read_exact(argv[2], BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
    memcpy(context.banks[0], input0, BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
    memcpy(context.banks[1], input1, BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
    free(input0);
    free(input1);
  }

  memcpy(original, context.banks, sizeof(original));
  generation = strtoull(argv[6], NULL, 0);
  context.active_slot = (enum bk7258_boot_ota_slot_e)strtol(argv[7], NULL, 0);
  context.mode = argv[8];
  expect_error = strcmp(argv[9], "error") == 0;
  expected_bank = strtol(argv[10], NULL, 0);
  if ((!expect_error && strcmp(argv[9], "ok") != 0) ||
      (expected_bank != -1 && expected_bank != 0 && expected_bank != 1) ||
      strcmp(argv[11], "end") != 0 || strcmp(argv[12], "spare") != 0)
    {
      die("invalid expectation");
    }

  for (index = 0; index < 2; index++)
    {
      if (bk7258_boot_ota_rotation_inspect(original[index], &infos[index]) < 0)
        {
          memset(&infos[index], 0, sizeof(infos[index]));
        }
    }
  before_ret = bk7258_boot_ota_rotation_select(infos, &before);

  ops.arg = &context;
  ops.now_ms = host_now;
  ops.compile_write_enabled = compile_gate;
  ops.runtime_write_enabled = runtime_gate;
  ops.active_slot = active_slot;
  ops.lock = flash_lock;
  ops.unlock = flash_unlock;
  ops.read = metadata_read;
  ops.erase_sector = flash_erase;
  ops.write = flash_write;
  raw_ops.arg = &context;
  raw_ops.read = raw_read;
  hash_ops.context_size = sizeof(struct host_hash_s);
  hash_ops.init = hash_init;
  hash_ops.update = hash_update;
  hash_ops.final = hash_final;
  ret = bk7258_boot_ota_rotation_publish_pending(
    record, generation, &raw_ops, &hash_ops, &ops, 1000,
    workspace, sizeof(workspace), &result);

  if ((expect_error && ret >= 0) || (!expect_error && ret != 0) ||
      result.status != ret || context.locked || context.outside_access)
    {
      die("result or guard invariant failed");
    }

  if (before_ret == 0 && before.metadata_present &&
      memcmp(original[before.selected_bank],
             context.banks[before.selected_bank],
             BK7258_BOOT_OTA_ROTATION_BANK_SIZE) != 0)
    {
      die("previous durable bank changed");
    }

  if (!expect_error)
    {
      for (index = 0; index < 2; index++)
        {
          if (bk7258_boot_ota_rotation_inspect(context.banks[index],
                                                &infos[index]) < 0)
            {
              die("committed bank no longer parses");
            }
        }

      if (bk7258_boot_ota_rotation_select(infos, &after) < 0 ||
          !after.metadata_present ||
          after.selected_bank != (uint32_t)expected_bank ||
          after.generation != generation || !after.trial_required ||
          !result.base_verified || !result.candidate_verified ||
          !result.readback_verified)
        {
          die("committed publication invariant failed");
        }

      if ((strcmp(context.mode, "idempotent") == 0) != result.idempotent ||
          (strcmp(context.mode, "reclaim") == 0) != result.bank_reclaimed)
        {
          die("idempotent/reclaim result drift");
        }
    }

  printf("BK7258 format-2 publish harness PASS status=%d bank=%ld "
         "generation=%llu erases=%u writes=%u raw_reads=%u\n",
         ret, expected_bank, (unsigned long long)generation,
         context.erases, context.writes, context.raw_reads);
  free(record);
  free(context.slots[0]);
  free(context.slots[1]);
  return 0;
}
