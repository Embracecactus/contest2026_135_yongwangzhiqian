/****************************************************************************
 * board/bk7258/chip/ap/bk7258_media_root.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private AP-wide ownership boundary for immutable SDK media drivers.
 ****************************************************************************/

#ifndef __BOARD_BK7258_CHIP_AP_BK7258_MEDIA_ROOT_H
#define __BOARD_BK7258_CHIP_AP_BK7258_MEDIA_ROOT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_MEDIA_ROOT_DMA   (1u << 0)
#define BK7258_MEDIA_ROOT_YUV   (1u << 1)
#define BK7258_MEDIA_ROOT_JPEG  (1u << 2)
#define BK7258_MEDIA_ROOT_H264  (1u << 3)
#define BK7258_MEDIA_ROOT_ALL   (BK7258_MEDIA_ROOT_DMA | \
                                 BK7258_MEDIA_ROOT_YUV | \
                                 BK7258_MEDIA_ROOT_JPEG | \
                                 BK7258_MEDIA_ROOT_H264)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bk7258_media_root_initialize(uint32_t roots);

#endif /* __BOARD_BK7258_CHIP_AP_BK7258_MEDIA_ROOT_H */
