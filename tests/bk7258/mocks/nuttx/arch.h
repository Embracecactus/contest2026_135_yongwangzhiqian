/* SPDX-License-Identifier: Apache-2.0 */
/****************************************************************************
 * tests/bk7258/mocks/nuttx/arch.h
 *
 * Host stand-in for <nuttx/arch.h>.  The critical-section helpers the IrDA
 * driver calls are provided by the spinlock shim.
 ****************************************************************************/

#ifndef __MOCK_NUTTX_ARCH_H
#define __MOCK_NUTTX_ARCH_H

#include <stdbool.h>

bool up_interrupt_context(void);

#endif /* __MOCK_NUTTX_ARCH_H */
