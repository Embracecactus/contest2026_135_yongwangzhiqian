/****************************************************************************
 * board/bk7258/chip/include/bk7258_sbc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SBC hardware decoder helper.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SBC_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SBC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stddef.h>
#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_sbc_decode_result_s
{
  const void *pcm;        /* Decoded PCM samples (int32 per sample) */
  size_t pcm_bytes;       /* Total decoded PCM bytes */
  unsigned int channels;  /* 1 or 2 */
  unsigned int sample_rate;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if defined(CONFIG_BK7258_SBC) && defined(CONFIG_BK7258_AP_CORE)

/* Initialize the chip-level singleton SBC decoder. */

int bk7258_sbc_initialize(void);

/* Decode one SBC frame into the caller-owned result view.  The returned PCM
 * view is valid until the next decode or uninitialize call.
 */

int bk7258_sbc_decode_frame(const void *data, size_t len,
                            struct bk7258_sbc_decode_result_s *result);

int bk7258_sbc_uninitialize(void);

#endif /* CONFIG_BK7258_SBC && CONFIG_BK7258_AP_CORE */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SBC_H */
