/* Host harness for format-2 symmetric trial health policy. */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_ota_rotation_health_core.h"

static void die(const char *message)
{
  fprintf(stderr, "BK7258 format-2 health harness FAIL: %s\n", message);
  exit(2);
}

static uint8_t *read_bank(const char *path)
{
  FILE *stream = fopen(path, "rb");
  uint8_t *bank = malloc(BK7258_BOOT_OTA_ROTATION_BANK_SIZE);

  if (stream == NULL || bank == NULL ||
      fread(bank, 1, BK7258_BOOT_OTA_ROTATION_BANK_SIZE, stream) !=
        BK7258_BOOT_OTA_ROTATION_BANK_SIZE || fgetc(stream) != EOF ||
      fclose(stream) != 0)
    {
      die("cannot read exact bank");
    }

  return bank;
}

static uint64_t parse_u64(const char *value)
{
  char *end;
  unsigned long long parsed;

  errno = 0;
  parsed = strtoull(value, &end, 0);
  if (errno != 0 || *value == '\0' || *end != '\0')
    {
      die("invalid integer");
    }

  return (uint64_t)parsed;
}

static struct bk7258_boot_ota_rotation_health_sample_s parse_sample(
  const char *text)
{
  struct bk7258_boot_ota_rotation_health_sample_s sample;
  unsigned int active;
  unsigned int healthy;
  unsigned int fault_free;

  if (sscanf(text, "%" SCNu64 ",%" SCNu32 ",%" SCNu32 ",%u,%u,%u",
             &sample.now_ms, &sample.supervisor_generation,
             &sample.supervisor_fault_count, &active, &healthy,
             &fault_free) != 6 || active > 1 || healthy > 1 ||
      fault_free > 1)
    {
      die("invalid sample");
    }

  sample.active_slot = (enum bk7258_boot_ota_slot_e)active;
  sample.supervisor_healthy = healthy != 0;
  sample.supervisor_fault_free = fault_free != 0;
  return sample;
}

int main(int argc, char **argv)
{
  struct bk7258_boot_ota_rotation_health_tracker_s tracker = {0};
  struct bk7258_boot_ota_rotation_health_result_s result;
  struct bk7258_boot_ota_rotation_health_sample_s sample;
  uint8_t *bank;
  uint64_t generation;
  uint32_t stable_ms;
  int status;
  int index;

  if (argc < 5)
    {
      die("usage: bank generation stable-ms sample...");
    }

  bank = read_bank(argv[1]);
  generation = parse_u64(argv[2]);
  stable_ms = (uint32_t)parse_u64(argv[3]);
  for (index = 4; index < argc; index++)
    {
      sample = parse_sample(argv[index]);
      status = bk7258_boot_ota_rotation_health_update(
        &tracker, bank, generation, stable_ms, &sample, &result);
      printf("STEP %d status=%d reason=%u state=%u target=%u "
             "stable_ms=%" PRIu64 " ready=%u tracking=%u\n",
             index - 4, status, (unsigned int)result.reason,
             (unsigned int)result.state, (unsigned int)result.target_slot,
             result.stable_ms, result.ready ? 1u : 0u,
             tracker.tracking ? 1u : 0u);
    }

  free(bank);
  return 0;
}
