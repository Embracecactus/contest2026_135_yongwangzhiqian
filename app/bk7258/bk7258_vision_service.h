/****************************************************************************
 * app/bk7258/bk7258_vision_service.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VISION_SERVICE_H
#define __APP_BK7258_BK7258_VISION_SERVICE_H

#include <stddef.h>
#include <stdint.h>

int bk7258_vision_service_prepare(void);
int bk7258_vision_service_start(void);
/* Synchronous fresh V4L2 capture; the caller owns product presentation. */
int bk7258_vision_capture_jpeg(uint8_t *destination,
                                size_t destination_capacity,
                                size_t *destination_size);

#endif /* __APP_BK7258_BK7258_VISION_SERVICE_H */
