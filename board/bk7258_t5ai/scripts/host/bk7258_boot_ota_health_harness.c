/*
 * Host adapter for the portable BK7258 N15-F health-confirm policy.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_ota_health_core.h"

static void die(const char *message)
{
  fprintf(stderr, "BK7258 N15-F health harness FAIL: %s\n", message);
  exit(2);
}

static uint8_t *read_metadata(const char *path)
{
  FILE *stream;
  uint8_t *metadata;
  long size;

  stream = fopen(path, "rb");
  if (stream == NULL || fseek(stream, 0, SEEK_END) != 0)
    {
      die("cannot open metadata");
    }

  size = ftell(stream);
  if (size != (long)BK7258_BOOT_OTA_METADATA_SIZE ||
      fseek(stream, 0, SEEK_SET) != 0)
    {
      die("metadata size mismatch");
    }

  metadata = malloc(BK7258_BOOT_OTA_METADATA_SIZE);
  if (metadata == NULL ||
      fread(metadata, 1, BK7258_BOOT_OTA_METADATA_SIZE, stream) !=
      BK7258_BOOT_OTA_METADATA_SIZE)
    {
      die("cannot read metadata");
    }

  if (fclose(stream) != 0)
    {
      die("cannot close metadata");
    }

  return metadata;
}

static uint64_t parse_u64(const char *value, const char *name)
{
  char *end;
  unsigned long long parsed;

  errno = 0;
  parsed = strtoull(value, &end, 0);
  if (errno != 0 || *value == '\0' || *end != '\0')
    {
      die(name);
    }

  return (uint64_t)parsed;
}

static uint32_t parse_u32(const char *value, const char *name)
{
  uint64_t parsed = parse_u64(value, name);

  if (parsed > UINT32_MAX)
    {
      die(name);
    }

  return (uint32_t)parsed;
}

static struct bk7258_boot_ota_health_sample_s parse_sample(const char *text)
{
  struct bk7258_boot_ota_health_sample_s sample;
  unsigned int secondary;
  unsigned int healthy;
  unsigned int fault_free;
  int converted;

  converted = sscanf(text, "%" SCNu64 ",%" SCNu32 ",%" SCNu32
                     ",%u,%u,%u",
                     &sample.now_ms, &sample.supervisor_generation,
                     &sample.supervisor_fault_count, &secondary, &healthy,
                     &fault_free);
  if (converted != 6 || secondary > 1 || healthy > 1 || fault_free > 1)
    {
      die("invalid sample");
    }

  sample.secondary_mapping_active = secondary != 0;
  sample.supervisor_healthy = healthy != 0;
  sample.supervisor_fault_free = fault_free != 0;
  return sample;
}

static void print_result(
  size_t index, int status,
  const struct bk7258_boot_ota_health_result_s *result,
  const struct bk7258_boot_ota_health_tracker_s *tracker)
{
  printf("STEP %zu status=%d reason=%u generation=%" PRIu64
         " stable_ms=%" PRIu64 " supervisor_generation=%" PRIu32
         " supervisor_fault_count=%" PRIu32 " ready=%u tracking=%u"
         " stable_since_ms=%" PRIu64 " tracker_generation=%" PRIu32
         " tracker_fault_count=%" PRIu32 "\n",
         index, status, (unsigned int)result->reason, result->generation,
         result->stable_ms, result->supervisor_generation,
         result->supervisor_fault_count, result->ready ? 1u : 0u,
         tracker->tracking ? 1u : 0u, tracker->stable_since_ms,
         tracker->supervisor_generation, tracker->supervisor_fault_count);
}

int main(int argc, char **argv)
{
  struct bk7258_boot_ota_health_tracker_s tracker = {0};
  struct bk7258_boot_ota_health_result_s result;
  struct bk7258_boot_ota_health_sample_s sample;
  struct bk7258_boot_ota_health_tracker_s *tracker_arg = &tracker;
  const struct bk7258_boot_ota_health_sample_s *sample_arg;
  const uint8_t *metadata_arg;
  uint8_t *metadata;
  uint64_t expected_generation;
  uint32_t required_stable_ms;
  bool null_sample = false;
  bool normal;
  int status;
  int index;

  if (argc < 6)
    {
      die("usage: metadata generation stable-ms mode sample...");
    }

  metadata = read_metadata(argv[1]);
  expected_generation = parse_u64(argv[2], "invalid generation");
  required_stable_ms = parse_u32(argv[3], "invalid stable window");
  metadata_arg = metadata;

  normal = strcmp(argv[4], "normal") == 0;
  if (!normal && argc != 6)
    {
      die("validation modes accept exactly one sample");
    }

  if (normal)
    {
      /* Normal multi-sample path. */
    }
  else if (strcmp(argv[4], "null-tracker") == 0)
    {
      tracker_arg = NULL;
    }
  else if (strcmp(argv[4], "null-metadata") == 0)
    {
      metadata_arg = NULL;
    }
  else if (strcmp(argv[4], "null-sample") == 0)
    {
      null_sample = true;
    }
  else if (strcmp(argv[4], "null-result") == 0)
    {
      sample = parse_sample(argv[5]);
      status = bk7258_boot_ota_health_update(
        &tracker, metadata, expected_generation, required_stable_ms,
        &sample, NULL);
      printf("NULL_RESULT status=%d tracking=%u\n", status,
             tracker.tracking ? 1u : 0u);
      free(metadata);
      return status == -EINVAL ? 0 : 1;
    }
  else
    {
      die("unknown mode");
    }

  for (index = 5; index < argc; index++)
    {
      sample = parse_sample(argv[index]);
      sample_arg = null_sample ? NULL : &sample;
      status = bk7258_boot_ota_health_update(
        tracker_arg, metadata_arg, expected_generation, required_stable_ms,
        sample_arg, &result);
      print_result((size_t)(index - 5), status, &result, &tracker);
    }

  free(metadata);
  return 0;
}
