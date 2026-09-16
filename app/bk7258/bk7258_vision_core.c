/****************************************************************************
 * app/bk7258/bk7258_vision_core.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host-testable validation for the metadata-only snapshot contract.
 ****************************************************************************/

#include "bk7258_vision_core.h"
#include <errno.h>
#include <string.h>

bool bkvision_rpc_request_valid(const struct bkvision_rpc_request_s *request)
{
  if (request == NULL || request->magic != BKVISION_RPC_MAGIC ||
      request->version != BKVISION_RPC_VERSION ||
      request->session_id == 0 || request->sequence == 0)
    {
      return false;
    }

  if (request->command == BKVISION_RPC_CHECK_RECORD)
    {
      return request->duration_ms != 0 && request->reserved != 0;
    }

  return request->reserved == 0 &&
         ((request->command == BKVISION_RPC_SNAPSHOT &&
           request->duration_ms == 0) ||
          (request->command == BKVISION_RPC_RECORD &&
           request->duration_ms >= 1000 &&
           request->duration_ms <= BKVISION_RECORD_MAX_SECONDS * 1000));
}

bool bkvision_rpc_response_valid(const struct bkvision_rpc_response_s *response)
{
  const uint32_t success_flags = BKVISION_FLAG_JPEG_SOI |
    BKVISION_FLAG_JPEG_EOI;
  const uint32_t known_flags = success_flags | BKVISION_FLAG_V4L2_ERROR |
    BKVISION_FLAG_RECORDED | BKVISION_FLAG_CHECKED;

  if (response == NULL || response->magic != BKVISION_RPC_MAGIC ||
      response->version != BKVISION_RPC_VERSION ||
      response->command != BKVISION_RPC_RESPONSE ||
      response->session_id == 0 || response->sequence == 0 ||
      (response->flags & ~known_flags) != 0 || response->rpc_status > 0 ||
      response->operation_status > 0)
    {
      return false;
    }

  if (response->flags == BKVISION_FLAG_CHECKED)
    {
      return response->rpc_status == 0 && response->operation_status == 0 &&
             response->width != 0 && response->height != 0 &&
             response->pixel_format == BKVISION_PIXEL_FORMAT_JPEG &&
             response->bytes_used > 224 && response->reserved[0] >= 2 &&
             response->capture_sequence == 0;
    }

  if ((response->flags & BKVISION_FLAG_RECORDED) != 0)
    {
      return response->rpc_status == 0 && response->operation_status == 0 &&
             response->width != 0 && response->height != 0 &&
             response->pixel_format == BKVISION_PIXEL_FORMAT_JPEG &&
             response->bytes_used > 224u && response->reserved[0] >= 2 &&
             response->reserved[1] > 0 &&
             response->flags == (success_flags | BKVISION_FLAG_RECORDED);
    }

  if (response->reserved[0] != 0 || response->reserved[1] != 0)
    {
      return false;
    }

  if (response->rpc_status < 0)
    {
      return response->operation_status < 0 && response->width == 0 &&
             response->height == 0 && response->pixel_format == 0 &&
             response->bytes_used == 0 &&
             response->capture_sequence == 0 && response->flags == 0;
    }

  if (response->operation_status < 0)
    {
      return response->width == 0 && response->height == 0 &&
             response->pixel_format == 0 && response->bytes_used == 0 &&
             response->capture_sequence == 0 &&
             ((response->flags == 0) ||
              (response->flags == BKVISION_FLAG_V4L2_ERROR &&
               response->operation_status == -EIO));
    }

  return response->operation_status == 0 && response->width != 0 &&
         response->height != 0 && response->pixel_format ==
         BKVISION_PIXEL_FORMAT_JPEG && response->bytes_used != 0 &&
         response->flags == success_flags;
}

void bkvision_rpc_make_response(struct bkvision_rpc_response_s *response,
                                const struct bkvision_rpc_request_s *request,
                                int rpc_status)
{
  if (response == NULL)
    {
      return;
    }
  memset(response, 0, sizeof(*response));
  response->magic = BKVISION_RPC_MAGIC;
  response->version = BKVISION_RPC_VERSION;
  response->command = BKVISION_RPC_RESPONSE;
  response->rpc_status = rpc_status;
  response->operation_status = rpc_status < 0 ? rpc_status : -ENODATA;
  if (request != NULL)
    {
      response->session_id = request->session_id;
      response->sequence = request->sequence;
    }
}

int bkvision_rpc_validate_frame(struct bkvision_rpc_response_s *response,
                                const uint8_t *frame, size_t capacity,
                                size_t bytes_used, uint32_t driver_flags,
                                uint32_t width, uint32_t height,
                                uint32_t pixel_format,
                                uint32_t capture_sequence)
{
  uint32_t flags = 0;
  int ret = 0;

  if (response == NULL)
    {
      return -EINVAL;
    }
  response->flags = 0;
  response->width = 0;
  response->height = 0;
  response->pixel_format = 0;
  response->bytes_used = 0;
  response->capture_sequence = 0;

  /* driver_flags is the caller-provided V4L2 error mask; any set bit means
   * that the dequeued buffer must not be treated as a valid frame. */

  if (bytes_used > UINT32_MAX)
    {
      response->operation_status = -EOVERFLOW;
      return -EOVERFLOW;
    }

  if (driver_flags != 0)
    {
      response->flags = BKVISION_FLAG_V4L2_ERROR;
      response->operation_status = -EIO;
      return -EIO;
    }

  if (frame == NULL || capacity < 4 || bytes_used < 4 ||
      bytes_used > capacity || width == 0 || height == 0)
    {
      response->operation_status = -EINVAL;
      return -EINVAL;
    }

  if (pixel_format != BKVISION_PIXEL_FORMAT_JPEG)
    {
      response->operation_status = -EPROTONOSUPPORT;
      return -EPROTONOSUPPORT;
    }

  if (frame[0] == 0xff && frame[1] == 0xd8)
    {
      flags |= BKVISION_FLAG_JPEG_SOI;
    }
  else
    {
      ret = -EPROTO;
    }

  if (frame[bytes_used - 2] == 0xff && frame[bytes_used - 1] == 0xd9)
    {
      flags |= BKVISION_FLAG_JPEG_EOI;
    }
  else
    {
      ret = -EPROTO;
    }

  if (ret != 0)
    {
      response->operation_status = ret;
      return ret;
    }

  response->width = width;
  response->height = height;
  response->pixel_format = pixel_format;
  response->bytes_used = (uint32_t)bytes_used;
  response->capture_sequence = capture_sequence;
  response->flags = flags;
  response->operation_status = 0;
  return 0;
}

int bkvision_copy_jpeg_frame(struct bkvision_rpc_response_s *response,
                             uint8_t *destination,
                             size_t destination_capacity,
                             size_t *destination_size,
                             const uint8_t *frame, size_t frame_capacity,
                             size_t bytes_used, uint32_t driver_flags,
                             uint32_t width, uint32_t height,
                             uint32_t pixel_format,
                             uint32_t capture_sequence)
{
  int ret;

  if (destination_size != NULL)
    {
      *destination_size = 0;
    }

  if (destination == NULL || destination_size == NULL)
    {
      if (response != NULL)
        {
          response->operation_status = -EINVAL;
          response->width = 0;
          response->height = 0;
          response->pixel_format = 0;
          response->bytes_used = 0;
          response->capture_sequence = 0;
          response->flags = 0;
          response->reserved[0] = 0;
          response->reserved[1] = 0;
        }
      return -EINVAL;
    }

  ret = bkvision_rpc_validate_frame(response, frame, frame_capacity,
                                    bytes_used, driver_flags, width, height,
                                    pixel_format, capture_sequence);
  if (ret < 0)
    {
      return ret;
    }

  if (bytes_used > destination_capacity)
    {
      response->operation_status = -ENOSPC;
      response->width = 0;
      response->height = 0;
      response->pixel_format = 0;
      response->bytes_used = 0;
      response->capture_sequence = 0;
      response->flags = 0;
      response->reserved[0] = 0;
      response->reserved[1] = 0;
      return -ENOSPC;
    }

  memcpy(destination, frame, bytes_used);
  *destination_size = bytes_used;
  return 0;
}
