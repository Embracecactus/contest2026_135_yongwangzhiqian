/* Host harness for atomic selection plus format-2 transition append. */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_ota_rotation_control_core.h"
#include "bk7258_partition_layout.h"

struct host_context_s
{
  uint8_t banks[2][BK7258_BOOT_OTA_ROTATION_BANK_SIZE];
  const char *mode;
  uint32_t reads;
  uint32_t writes;
  bool locked;
  bool outside_access;
};

static void die(const char *message)
{
  fprintf(stderr, "BK7258 format-2 control harness FAIL: %s\n", message);
  exit(2);
}

static void read_bank(const char *path, uint8_t *bank)
{
  FILE *stream = fopen(path, "rb");

  if (stream == NULL ||
      fread(bank, 1, BK7258_BOOT_OTA_ROTATION_BANK_SIZE, stream) !=
        BK7258_BOOT_OTA_ROTATION_BANK_SIZE || fgetc(stream) != EOF ||
      fclose(stream) != 0)
    {
      die("cannot read exact bank");
    }
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

static int flash_read(void *arg, uint32_t address, uint8_t *data, size_t len)
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

  context->reads++;
  if (strcmp(context->mode, "read-error") == 0 && context->reads == 2)
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
      if (context->banks[bank][offset + index] != 0xffu)
        {
          context->outside_access = true;
          return -EIO;
        }
    }

  memcpy(context->banks[bank] + offset, data, len);
  return 0;
}

int main(int argc, char **argv)
{
  struct bk7258_boot_ota_rotation_control_result_s result;
  struct bk7258_boot_ota_rotation_bank_s info;
  struct bk7258_boot_ota_trial_ops_s ops;
  struct host_context_s context;
  uint8_t original[2][BK7258_BOOT_OTA_ROTATION_BANK_SIZE];
  uint8_t workspace[BK7258_BOOT_OTA_ROTATION_CONTROL_WORKSPACE_SIZE];
  uint64_t generation;
  long expected_state;
  long next_state;
  long selected_bank;
  bool expect_error;
  int ret;

  if (argc != 10)
    {
      die("usage: bank0 bank1 generation expected next mode ok|error bank");
    }

  memset(&context, 0, sizeof(context));
  read_bank(argv[1], context.banks[0]);
  read_bank(argv[2], context.banks[1]);
  memcpy(original, context.banks, sizeof(original));
  generation = strtoull(argv[3], NULL, 0);
  expected_state = strtol(argv[4], NULL, 0);
  next_state = strtol(argv[5], NULL, 0);
  context.mode = argv[6];
  expect_error = strcmp(argv[7], "error") == 0;
  selected_bank = strtol(argv[8], NULL, 0);
  if ((!expect_error && strcmp(argv[7], "ok") != 0) ||
      (selected_bank != -1 && selected_bank != 0 && selected_bank != 1) ||
      strcmp(argv[9], "end") != 0)
    {
      die("invalid expectation");
    }

  ops.arg = &context;
  ops.compile_write_enabled = compile_gate;
  ops.runtime_write_enabled = runtime_gate;
  ops.lock = flash_lock;
  ops.unlock = flash_unlock;
  ops.read = flash_read;
  ops.write = flash_write;
  ret = bk7258_boot_ota_rotation_control_transition(
    generation, (enum bk7258_boot_ota_rotation_state_e)expected_state,
    (enum bk7258_boot_ota_rotation_state_e)next_state, &ops, 1000,
    workspace, sizeof(workspace), &result);

  if ((expect_error && ret >= 0) || (!expect_error && ret != 0) ||
      result.status != ret || context.locked || context.outside_access)
    {
      die("result or guard invariant failed");
    }

  if (selected_bank >= 0 &&
      memcmp(original[selected_bank ^ 1], context.banks[selected_bank ^ 1],
             BK7258_BOOT_OTA_ROTATION_BANK_SIZE) != 0)
    {
      die("non-selected durable bank changed");
    }

  if (!expect_error &&
      (result.selected_bank != (uint32_t)selected_bank ||
       !result.readback_verified ||
       result.programmed_chunks != BK7258_BOOT_OTA_PROGRAM_CHUNKS ||
       result.verified_chunks != BK7258_BOOT_OTA_PROGRAM_CHUNKS ||
       bk7258_boot_ota_rotation_inspect(context.banks[selected_bank],
                                         &info) != 0 ||
       !info.trusted || info.state != next_state ||
       info.generation != generation))
    {
      die("committed transition invariant failed");
    }

  printf("BK7258 format-2 control harness PASS status=%d selected=%ld "
         "state=%ld->%ld writes=%u reads=%u\n",
         ret, selected_bank, expected_state, next_state,
         context.writes, context.reads);
  return 0;
}
