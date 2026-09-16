/****************************************************************************
 * app/bk7258/bk7258_vision_core.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_VISION_CORE_H
#define __APP_BK7258_BK7258_VISION_CORE_H

#include <stdbool.h>
#include "bk7258_vision_protocol.h"

bool bkvision_rpc_request_valid(const struct bkvision_rpc_request_s *request);
bool bkvision_rpc_response_valid(
  const struct bkvision_rpc_response_s *response);
void bkvision_rpc_make_response(struct bkvision_rpc_response_s *response,
                                const struct bkvision_rpc_request_s *request,
                                int rpc_status);
int bkvision_rpc_validate_frame(struct bkvision_rpc_response_s *response,
                                const uint8_t *frame, size_t capacity,
                                size_t bytes_used, uint32_t driver_flags,
                                uint32_t width, uint32_t height,
                                uint32_t pixel_format,
                                uint32_t capture_sequence);
int bkvision_copy_jpeg_frame(struct bkvision_rpc_response_s *response,
                             uint8_t *destination,
                             size_t destination_capacity,
                             size_t *destination_size,
                             const uint8_t *frame, size_t frame_capacity,
                             size_t bytes_used, uint32_t driver_flags,
                             uint32_t width, uint32_t height,
                             uint32_t pixel_format,
                             uint32_t capture_sequence);

#endif /* __APP_BK7258_BK7258_VISION_CORE_H */
