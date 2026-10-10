/****************************************************************************
 * app/bk7258/bk7258_vision_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VISION_SERVICE_H
#define __APP_BK7258_BK7258_VISION_SERVICE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "bk7258_vision_protocol.h"

int bk7258_vision_service_prepare(void);
int bk7258_vision_service_start(void);
/* Seals new captures only while the existing owner is idle; a failed
 * power-down leaves recoverable state and never tears down V4L2.
 */
int bk7258_vision_quiesce(bool quiesce);
/* Synchronous fresh V4L2 capture; the caller owns product presentation. */
int bk7258_vision_capture_jpeg(uint8_t *destination,
                                size_t destination_capacity,
                                size_t *destination_size);
int bk7258_vision_pc_wake(void);
int bk7258_vision_pc_capture(uint8_t *destination, size_t capacity,
  struct bkvision_rpc_response_s *metadata, bool (*canceled)(void));

#endif /* __APP_BK7258_BK7258_VISION_SERVICE_H */
