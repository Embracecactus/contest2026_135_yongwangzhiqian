/****************************************************************************
 * app/bk7258/bk7258_health_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_HEALTH_SERVICE_H
#define __APP_BK7258_BK7258_HEALTH_SERVICE_H

#include <stdint.h>

#define BK7258_HEALTH_SNAPSHOT_BATTERY_STATE_VALID   (1u << 0)
#define BK7258_HEALTH_SNAPSHOT_BATTERY_VOLTAGE_VALID (1u << 1)

/* Latest AP-worker sample.  Callers only copy this cache; they never perform
 * battery or temperature I/O on latency-sensitive product owners.
 */

struct bk7258_health_service_snapshot_s
{
  uint32_t sequence;
  uint32_t flags;
  uint32_t battery_state;
  int32_t battery_voltage_mv;
};

int bk7258_health_service_prepare(void);
int bk7258_health_service_start(void);
int bk7258_health_service_snapshot(
  struct bk7258_health_service_snapshot_s *snapshot);

#endif /* __APP_BK7258_BK7258_HEALTH_SERVICE_H */
