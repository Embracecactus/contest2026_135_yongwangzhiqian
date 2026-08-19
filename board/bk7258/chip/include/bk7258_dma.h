/****************************************************************************
 * board/bk7258/chip/include/bk7258_dma.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 general-purpose DMA (GDMA) character-device contract.
 *
 * The v3.1.1.9 AP SDK exports the full bk_dma_* channel API
 * (include/driver/dma.h) already used internally by AUD/MIC/JPEG/H264.  This
 * board wrapper publishes a user-space memory-to-memory DMA engine as
 * /dev/dma0.  It allocates its own channel via bk_dma_alloc() with a private
 * user token, so it never collides with the SDK channel owners above.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DMA_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DMA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/fs/ioctl.h>

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_DMA_DEVPATH        "/dev/dma0"

#define BK7258_DMA_MAX_TRANSFER   65536u

/* Board ioctl commands (board-local base 0x5e00). */

#define BKIOC_DMA_TRANSFER        _IOC(0x5e00, 0x01)
#define BKIOC_DMA_GET_STATUS      _IOC(0x5e00, 0x02)

#define BK7258_DMA_STATUS_IDLE    0
#define BK7258_DMA_STATUS_BUSY    1

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Memory-to-memory transfer request for BKIOC_DMA_TRANSFER.  src_addr and
 * dst_addr must be physical addresses reachable by the DMA engine; length
 * must be <= BK7258_DMA_MAX_TRANSFER and match the data width alignment.
 */

struct bk7258_dma_xfer_s
{
  uint32_t src_addr;
  uint32_t dst_addr;
  uint32_t length;               /* bytes, 1..65536 */
  uint32_t width;                /* 8, 16 or 32 */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_DMA) && defined(CONFIG_BK7258_AP_CORE)

/* Register /dev/dma0. */

int bk7258_dma_initialize(void);

#endif /* CONFIG_BK7258_DMA && CONFIG_BK7258_AP_CORE */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_DMA_H */
