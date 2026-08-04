/*
 * boot_ota_health_core.c - portable N15-F trial health-confirm policy.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "boot_ota_health_core.h"

static void tracker_reset(struct bk7258_boot_ota_health_tracker_s *tracker)
{
  tracker->stable_since_ms = 0;
  tracker->supervisor_generation = 0;
  tracker->supervisor_fault_count = 0;
  tracker->tracking = false;
}

static int finish(struct bk7258_boot_ota_health_result_s *result,
                  enum bk7258_boot_ota_health_reason_e reason, int status)
{
  result->reason = reason;
  result->status = status;
  return status;
}

int bk7258_boot_ota_health_update(
  struct bk7258_boot_ota_health_tracker_s *tracker,
  const uint8_t metadata[BK7258_BOOT_OTA_METADATA_SIZE],
  uint64_t expected_generation, uint32_t required_stable_ms,
  const struct bk7258_boot_ota_health_sample_s *sample,
  struct bk7258_boot_ota_health_result_s *result)
{
  struct bk7258_boot_ota_metadata_info_s info;
  uint64_t stable_ms;
  int ret;

  if (result == NULL)
    {
      return -EINVAL;
    }

  result->status = -EINPROGRESS;
  result->reason = BK7258_BOOT_OTA_HEALTH_NONE;
  result->generation = expected_generation;
  result->stable_ms = 0;
  result->supervisor_generation = sample == NULL ? 0 :
                                  sample->supervisor_generation;
  result->supervisor_fault_count = sample == NULL ? 0 :
                                   sample->supervisor_fault_count;
  result->ready = false;

  if (tracker == NULL || metadata == NULL || sample == NULL ||
      expected_generation == 0 || required_stable_ms == 0)
    {
      result->status = -EINVAL;
      return -EINVAL;
    }

  ret = bk7258_boot_ota_metadata_inspect(metadata, &info);
  if (ret != 0 || info.erased || !info.trusted)
    {
      tracker_reset(tracker);
      return finish(result, BK7258_BOOT_OTA_HEALTH_METADATA_INVALID,
                    ret != 0 ? ret : -EBADMSG);
    }

  result->generation = info.generation;
  if (info.generation != expected_generation)
    {
      tracker_reset(tracker);
      return finish(result, BK7258_BOOT_OTA_HEALTH_GENERATION_STALE,
                    -ESTALE);
    }

  if (info.state == BK7258_BOOT_OTA_META_CONFIRMED_B)
    {
      tracker_reset(tracker);
      return finish(result, BK7258_BOOT_OTA_HEALTH_NOT_TRIAL,
                    -EALREADY);
    }

  if (info.state != BK7258_BOOT_OTA_META_TRIAL_STARTED)
    {
      tracker_reset(tracker);
      return finish(result, BK7258_BOOT_OTA_HEALTH_NOT_TRIAL, -EPERM);
    }

  if (!sample->secondary_mapping_active)
    {
      tracker_reset(tracker);
      return finish(result, BK7258_BOOT_OTA_HEALTH_PRIMARY_MAPPING,
                    -EPERM);
    }

  if (!sample->supervisor_healthy || !sample->supervisor_fault_free)
    {
      tracker_reset(tracker);
      return finish(result, BK7258_BOOT_OTA_HEALTH_SUPERVISOR_UNHEALTHY,
                    -EAGAIN);
    }

  if (!tracker->tracking ||
      tracker->supervisor_generation != sample->supervisor_generation ||
      tracker->supervisor_fault_count != sample->supervisor_fault_count)
    {
      tracker->stable_since_ms = sample->now_ms;
      tracker->supervisor_generation = sample->supervisor_generation;
      tracker->supervisor_fault_count = sample->supervisor_fault_count;
      tracker->tracking = true;
      return finish(result, BK7258_BOOT_OTA_HEALTH_STABILIZING,
                    -EAGAIN);
    }

  /* The target uses CONFIG_SYSTEM_TIME64, so an ordinary backward jump is
   * never evidence of a completed stable interval.  Preserve true uint64
   * wrap behavior only when tracking began within one required window of
   * UINT64_MAX; otherwise restart the window and report the discontinuity.
   */

  if (sample->now_ms < tracker->stable_since_ms &&
      tracker->stable_since_ms < UINT64_MAX - required_stable_ms)
    {
      tracker->stable_since_ms = sample->now_ms;
      return finish(result, BK7258_BOOT_OTA_HEALTH_CLOCK_REGRESSED,
                    -EAGAIN);
    }

  stable_ms = sample->now_ms - tracker->stable_since_ms;
  result->stable_ms = stable_ms;
  if (stable_ms < required_stable_ms)
    {
      return finish(result, BK7258_BOOT_OTA_HEALTH_STABILIZING,
                    -EAGAIN);
    }

  result->ready = true;
  return finish(result, BK7258_BOOT_OTA_HEALTH_READY, 0);
}
