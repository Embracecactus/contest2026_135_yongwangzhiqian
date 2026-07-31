/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/common/
 * bk7258_rptun_mbox.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Team-owned N9 wrapper around the Beken SDK logical mailbox channel API.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_COMMON_BK7258_RPTUN_MBOX_H
#define __ARCH_ARM_SRC_BK7258_COMMON_BK7258_RPTUN_MBOX_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef void (*bk7258_rptun_notify_t)(uint32_t generation,
                                      uint32_t notify);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int bk7258_rptun_mbox_initialize(void);
int bk7258_rptun_mbox_send(uint32_t type, uint32_t generation,
                           uint32_t value);
int bk7258_rptun_mbox_notify(uint32_t generation, uint32_t value);
int bk7258_rptun_mbox_probe(uint32_t count, uint32_t timeout_ms);
uint32_t bk7258_rptun_mbox_take_lifecycle(void);
void bk7258_rptun_mbox_set_notify(bk7258_rptun_notify_t callback);

#endif /* __ARCH_ARM_SRC_BK7258_COMMON_BK7258_RPTUN_MBOX_H */
