/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __TEST_BK7258_MOCK_SYSTEM_RESET_H
#define __TEST_BK7258_MOCK_SYSTEM_RESET_H

typedef unsigned long irqstate_t;

irqstate_t up_irq_save(void);
void up_irq_restore(irqstate_t flags);
void up_systemreset(void) __attribute__((noreturn));

#endif /* __TEST_BK7258_MOCK_SYSTEM_RESET_H */
