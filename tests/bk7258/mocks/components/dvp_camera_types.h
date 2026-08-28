#ifndef __TESTS_BK7258_MOCKS_COMPONENTS_DVP_CAMERA_TYPES_H
#define __TESTS_BK7258_MOCKS_COMPONENTS_DVP_CAMERA_TYPES_H

#include <stdint.h>

/* Header-audit stand-in for the SDK-owned configuration value embedded by
 * the public BK7258 DVP contract.  Runtime DVP tests use the real SDK types.
 */

typedef struct
{
  uint32_t opaque;
} bk_dvp_config_t;

#endif /* __TESTS_BK7258_MOCKS_COMPONENTS_DVP_CAMERA_TYPES_H */
