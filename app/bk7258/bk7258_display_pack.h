/****************************************************************************
 * app/bk7258/bk7258_display_pack.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Portable reader for the shaniu-eye-pack-v1 display resource contract.
 ****************************************************************************/

#ifndef __APP_BK7258_BK7258_DISPLAY_PACK_H
#define __APP_BK7258_BK7258_DISPLAY_PACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BKDISPLAY_PACK_ID_SIZE       32u
#define BKDISPLAY_EXPRESSION_SIZE    22u
#define BKDISPLAY_PACK_PATH_SIZE     256u
#define BKDISPLAY_CANVAS_WIDTH       160u
#define BKDISPLAY_CANVAS_HEIGHT      160u
#define BKDISPLAY_CANVAS_PIXELS      \
  (BKDISPLAY_CANVAS_WIDTH * BKDISPLAY_CANVAS_HEIGHT)
#define BKDISPLAY_MAX_PACK_BYTES     (32u * 1024u * 1024u)

enum bkdisplay_side_e
{
  BKDISPLAY_SIDE_UNMAPPED = 0,
  BKDISPLAY_SIDE_LEFT = 1,
  BKDISPLAY_SIDE_RIGHT = 2,
};

struct bkdisplay_pack_s;

struct bkdisplay_pack_info_s
{
  char pack_id[BKDISPLAY_PACK_ID_SIZE];
  uint32_t revision;
  uint16_t renderer_api;
  uint16_t width;
  uint16_t height;
  uint16_t entry_count;
  uint16_t palette_count;
  uint32_t total_size;
  uint8_t source_sha256[32];
};

/* Open and fully validate a regular pack file.  Validation covers the fixed
 * header and TOC contract, zero padding, all CRC32 values, all RLE8 streams,
 * palette references, canonical names, side pairs and the mandatory neutral
 * expression.  The returned object owns its file descriptor.
 */

int bkdisplay_pack_open(const char *path, struct bkdisplay_pack_s **pack,
                        struct bkdisplay_pack_info_s *info);
void bkdisplay_pack_close(struct bkdisplay_pack_s *pack);

/* Decode one logical expression directly to native RGB565 pixels.  An
 * unmapped request accepts only a shared frame and deliberately ignores the
 * optional right-eye mirror bit.  This gives first boot a safe two-identical-
 * screens fallback until the physical left/right board mapping is verified.
 */

int bkdisplay_pack_render(struct bkdisplay_pack_s *pack,
                          const char *expression,
                          enum bkdisplay_side_e side,
                          uint16_t *pixels, size_t pixel_count);

#endif /* __APP_BK7258_BK7258_DISPLAY_PACK_H */
