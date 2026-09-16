/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_VISION_RECORD_H
#define __APP_BK7258_VISION_RECORD_H

#include <stddef.h>
#include <stdint.h>

#define BKVISION_RECORD_MAX_BYTES (64u * 1024u * 1024u)
/* Bounded, single-stream MJPEG AVI writer. Caller owns the fd and removes
 * incomplete output on any failure. No frame-sized allocations or index heap.
 * An optional caller-owned write buffer must stay alive until finish returns.
 */

struct bkvision_record_s
{
  int fd;
  uint32_t width;
  uint32_t height;
  uint32_t frames;
  uint32_t bytes;
  uint32_t largest;
  uint32_t payload_hash;
  uint8_t *write_buffer;
  size_t write_capacity;
  size_t write_limit;
  size_t write_used;
  int write_error;
};

int bkvision_record_begin(struct bkvision_record_s *record, int fd,
                          uint32_t width, uint32_t height);
/* Enable before the first frame; capacity must be a multiple of 512 bytes. */
int bkvision_record_set_buffer(struct bkvision_record_s *record,
                               void *buffer, size_t capacity);
int bkvision_record_frame(struct bkvision_record_s *record,
                          const void *data, size_t size);
int bkvision_record_finish(struct bkvision_record_s *record,
                           uint32_t elapsed_ms);
int bkvision_record_inspect(struct bkvision_record_s *record, int fd);
#endif
