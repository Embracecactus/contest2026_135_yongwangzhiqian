/****************************************************************************
 * chips/bk7258/include/bk7258_ap_lifecycle.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Chip-owned AP lifecycle phases used by the board initial application.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AP_LIFECYCLE_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AP_LIFECYCLE_H

#include <nuttx/compiler.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Complete AP-local runtime, SMP, transport and radio startup.  On failure,
 * return a negated errno and store the BK7258_AP_ERROR_* value which the
 * board entry passes to bk7258_ap_lifecycle_fail_and_park().
 */

int bk7258_ap_lifecycle_startup(FAR uint32_t *failure);

/* Drop the bounded startup clock vote and publish the AP READY handshake.
 * Board devices and synchronous product preparation must be complete first.
 */

int bk7258_ap_lifecycle_publish_ready(FAR uint32_t *failure);

#ifdef CONFIG_BK7258_AP_APPLICATION_LIFECYCLE
/* Product-owned extension points around READY publication.  The selected
 * application supplies both functions; the chip contract intentionally has
 * no knowledge of a specific product.
 */

int bk7258_ap_application_prepare(void);
int bk7258_ap_application_start(void);
#endif

/* Publish a terminal startup failure, release transient chip resources and
 * park the AP with interrupts disabled.
 */

void bk7258_ap_lifecycle_fail_and_park(uint32_t failure)
  noreturn_function;

/* Enter the permanent AP command/heartbeat supervisor.  This function also
 * applies the post-READY scheduler priority and never returns.
 */

void bk7258_ap_lifecycle_supervise(void) noreturn_function;

#ifdef __cplusplus
}
#endif

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_AP_LIFECYCLE_H */
