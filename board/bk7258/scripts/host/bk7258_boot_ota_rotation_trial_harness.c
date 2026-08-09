/* Host harness for the format-2 metadata append/read-back controller. */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_ota_rotation_trial_core.h"
#include "bk7258_partition_layout.h"

struct host_context_s
{
  uint8_t *bank;
  uint32_t bank_address;
  const char *mode;
  uint32_t reads;
  uint32_t writes;
  bool locked;
};

static void die(const char *message)
{
  fprintf(stderr, "BK7258 format-2 trial harness FAIL: %s\n", message);
  exit(2);
}

static uint8_t *read_bank(const char *path)
{
  FILE *stream = fopen(path, "rb");
  uint8_t *bank;
  long size;

  if (stream == NULL || fseek(stream, 0, SEEK_END) != 0)
    {
      die("cannot open bank");
    }

  size = ftell(stream);
  if (size != (long)BK7258_BOOT_OTA_ROTATION_BANK_SIZE ||
      fseek(stream, 0, SEEK_SET) != 0)
    {
      die("bank size mismatch");
    }

  bank = malloc(BK7258_BOOT_OTA_ROTATION_BANK_SIZE);
  if (bank == NULL ||
      fread(bank, 1, BK7258_BOOT_OTA_ROTATION_BANK_SIZE, stream) !=
        BK7258_BOOT_OTA_ROTATION_BANK_SIZE)
    {
      die("cannot read bank");
    }

  fclose(stream);
  return bank;
}

static bool compile_gate(void *arg)
{
  struct host_context_s *context = arg;

  return strcmp(context->mode, "compile-gate-off") != 0;
}

static bool runtime_gate(void *arg)
{
  struct host_context_s *context = arg;

  return strcmp(context->mode, "runtime-gate-off") != 0;
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
      die("unlock without lock");
    }

  context->locked = false;
}

static bool in_bank(struct host_context_s *context, uint32_t address,
                    size_t len)
{
  return address >= context->bank_address &&
         address - context->bank_address <=
           BK7258_BOOT_OTA_ROTATION_BANK_SIZE &&
         len <= BK7258_BOOT_OTA_ROTATION_BANK_SIZE -
                (address - context->bank_address);
}

static int flash_read(void *arg, uint32_t address, uint8_t *data, size_t len)
{
  struct host_context_s *context = arg;

  if (!in_bank(context, address, len))
    {
      return -EINVAL;
    }

  context->reads++;
  if (strcmp(context->mode, "read-error") == 0 && context->reads == 1)
    {
      return -EIO;
    }

  memcpy(data, context->bank + address - context->bank_address, len);
  if (strcmp(context->mode, "readback-corrupt") == 0 &&
      context->writes != 0)
    {
      data[0] ^= 1;
    }

  return 0;
}

static int flash_write(void *arg, uint32_t address, const uint8_t *data,
                       size_t len)
{
  struct host_context_s *context = arg;
  uint8_t *destination;
  size_t index;

  if (!context->locked || len != BK7258_BOOT_OTA_PROGRAM_GRANULE ||
      !in_bank(context, address, len))
    {
      return -EINVAL;
    }

  context->writes++;
  if (strcmp(context->mode, "write-error") == 0)
    {
      return -EIO;
    }

  destination = context->bank + address - context->bank_address;
  for (index = 0; index < len; index++)
    {
      if (destination[index] != 0xffu)
        {
          return -EIO;
        }
    }

  memcpy(destination, data, len);
  return 0;
}

int main(int argc, char **argv)
{
  struct bk7258_boot_ota_trial_ops_s ops;
  struct bk7258_boot_ota_rotation_trial_result_s result;
  struct bk7258_boot_ota_rotation_bank_s info;
  struct host_context_s context;
  uint8_t metadata[BK7258_BOOT_OTA_ROTATION_BANK_SIZE];
  uint8_t scratch[BK7258_BOOT_OTA_TRIAL_SCRATCH_SIZE];
  uint64_t generation;
  long bank_index;
  long expected_state;
  long next_state;
  bool expect_error;
  int ret;

  if (argc != 8)
    {
      die("usage: harness bank bank_index generation expected next mode ok|error");
    }

  memset(&context, 0, sizeof(context));
  context.bank = read_bank(argv[1]);
  bank_index = strtol(argv[2], NULL, 0);
  context.bank_address = bank_index == 0 ?
    BK7258_ROLE_OTA_METADATA_PRIMARY_OFFSET :
    (bank_index == 1 ? BK7258_ROLE_OTA_METADATA_MIRROR_OFFSET : 0x123000u);
  generation = strtoull(argv[3], NULL, 0);
  expected_state = strtol(argv[4], NULL, 0);
  next_state = strtol(argv[5], NULL, 0);
  context.mode = argv[6];
  expect_error = strcmp(argv[7], "error") == 0;
  if (!expect_error && strcmp(argv[7], "ok") != 0)
    {
      die("result expectation must be ok or error");
    }

  ops.arg = &context;
  ops.compile_write_enabled = compile_gate;
  ops.runtime_write_enabled = runtime_gate;
  ops.lock = flash_lock;
  ops.unlock = flash_unlock;
  ops.read = flash_read;
  ops.write = flash_write;
  ret = bk7258_boot_ota_rotation_trial_transition(
    context.bank_address, generation,
    (enum bk7258_boot_ota_rotation_state_e)expected_state,
    (enum bk7258_boot_ota_rotation_state_e)next_state, &ops, 1000,
    metadata, scratch, &result);
  if ((expect_error && ret >= 0) || (!expect_error && ret != 0) ||
      result.status != ret || context.locked)
    {
      die("transition result invariant failed");
    }

  if (!expect_error)
    {
      if (!result.current_boot_trial || !result.readback_verified ||
          result.programmed_chunks != BK7258_BOOT_OTA_PROGRAM_CHUNKS ||
          result.verified_chunks != BK7258_BOOT_OTA_PROGRAM_CHUNKS ||
          bk7258_boot_ota_rotation_inspect(context.bank, &info) != 0 ||
          info.state !=
            (enum bk7258_boot_ota_rotation_state_e)next_state ||
          info.generation != generation)
        {
          die("committed transition invariant failed");
        }
    }

  printf("BK7258 format-2 trial harness PASS status=%d bank=%ld "
         "state=%ld->%ld writes=%u reads=%u\n",
         ret, bank_index, expected_state, next_state, context.writes,
         context.reads);
  free(context.bank);
  return 0;
}
