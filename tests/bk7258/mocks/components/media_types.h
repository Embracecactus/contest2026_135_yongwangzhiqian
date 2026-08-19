/****************************************************************************
 * tests/mocks/components/media_types.h
 *
 * Minimal shim for the v3.1.1.9 media_types.h.  Only the members consumed
 * by the AP JPEG decoder are provided; PIXEL_FMT values mirror the SDK enum.
 ****************************************************************************/

#ifndef __MOCK_COMPONENTS_MEDIA_TYPES_H
#define __MOCK_COMPONENTS_MEDIA_TYPES_H

#include <stdint.h>

#ifndef FAR
#define FAR
#endif

typedef enum
{
  UNKNOW_CAMERA,
  DVP_CAMERA,
  UVC_CAMERA,
  NET_CAMERA,
} camera_type_t;

typedef enum
{
  PIXEL_FMT_UNKNOW,
  PIXEL_FMT_JPEG,
  PIXEL_FMT_H264,
  PIXEL_FMT_H265,
  PIXEL_FMT_YUV444,
  PIXEL_FMT_YUYV,
  PIXEL_FMT_VYUY,
  PIXEL_FMT_UYVY,
  PIXEL_FMT_YYUV,
  PIXEL_FMT_VUYY,
  PIXEL_FMT_UVYY,
  PIXEL_FMT_YUV422,
  PIXEL_FMT_I420,
  PIXEL_FMT_YV12,
  PIEXL_FMT_YUV420P,
} pixel_format_t;

typedef struct frame_buffer_t frame_buffer_t;

struct frame_buffer_t
{
  uint32_t flag;
  FAR uint8_t *frame;
  uint32_t size;
  uint8_t frame_crc;
  camera_type_t type;
  pixel_format_t fmt;
  uint8_t crc;
  uint32_t timestamp;
  uint16_t width;
  uint16_t height;
  uint32_t length;
  uint32_t sequence;
  uint32_t h264_type;
};

#endif /* __MOCK_COMPONENTS_MEDIA_TYPES_H */