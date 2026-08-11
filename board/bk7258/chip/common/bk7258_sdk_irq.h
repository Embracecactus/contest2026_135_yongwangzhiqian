/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/common/bk7258_sdk_irq.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Private definitions for the Beken SDK-to-NuttX IRQ bridge.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_BK7258_SDK_IRQ_H
#define __ARCH_ARM_SRC_BK7258_BK7258_SDK_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <arch/chip/irq.h>

#include <driver/int_types.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_SDK_IRQ_FIRST           BK7258_IRQ_FIRST
#define BK7258_SDK_IRQ_COUNT           BK7258_EXTERNAL_IRQS
#define BK7258_SDK_IRQ_PRIORITY_BITS   3
#define BK7258_SDK_IRQ_PRIORITY_SHIFT  (8 - BK7258_SDK_IRQ_PRIORITY_BITS)
#define BK7258_SDK_IRQ_DEFAULT_PRIORITY 4
#define BK7258_SDK_IRQ_LCD_PRIORITY     0

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

void bk7258_clear_pending_irq(int irq);
void interrupt_init(void);
void interrupt_deinit(void);
#ifdef CONFIG_BK7258_AP_SMP_SCHED_ONLINE
int bk7258_sdk_irq_secondary_initialize(void);
int bk7258_sdk_irq_secondary_online(void);
#endif
bk_err_t bk7258_sdk_irq_snapshot_handler(icu_int_src_t source,
                                         int_group_isr_t *handler);

#if defined(CONFIG_BK7258_SDK_IRQ_TIMER_TEST) || \
    defined(CONFIG_BK7258_GPIO_IRQ_TEST)
bk_err_t bk7258_sdk_irq_test_snapshot_handler(icu_int_src_t source,
                                               int_group_isr_t *handler);
#endif

#ifdef CONFIG_BK7258_GPIO_IRQ_TEST
void bk7258_sdk_irq_test_reset_dispatch_counts(void);
bk_err_t bk7258_sdk_irq_test_snapshot_dispatch_count(icu_int_src_t source,
                                                     uint32_t *count);
#endif

#endif /* __ARCH_ARM_SRC_BK7258_BK7258_SDK_IRQ_H */
