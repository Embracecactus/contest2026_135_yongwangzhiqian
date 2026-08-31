/****************************************************************************
 * chips/bk7258/common/
 * bk7258_rpmsg_health.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_COMMON_BK7258_RPMSG_HEALTH_H
#define __ARCH_ARM_SRC_BK7258_COMMON_BK7258_RPMSG_HEALTH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_rpmsg_health_result_s
{
  uint32_t generation;
  uint32_t sequence;
  uint32_t primary_heartbeat;
  uint32_t secondary_heartbeat;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bk7258_rpmsg_health_initialize(void);

#ifndef CONFIG_BK7258_AP_CORE
bool bk7258_rpmsg_health_ready(void);
int bk7258_rpmsg_health_probe(
  uint32_t generation, uint32_t timeout_ms,
  struct bk7258_rpmsg_health_result_s *result);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_COMMON_BK7258_RPMSG_HEALTH_H */
