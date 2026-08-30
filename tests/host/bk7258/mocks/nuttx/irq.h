/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/host/bk7258/mocks/nuttx/irq.h
 *
 * Host stand-in for <nuttx/irq.h>.  irqstate_t is provided by the spinlock
 * shim.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_IRQ_H
#define __MOCK_NUTTX_IRQ_H

typedef unsigned long irqstate_t;

irqstate_t up_irq_save(void);
void up_irq_restore(irqstate_t flags);

#endif /* __MOCK_NUTTX_IRQ_H */
