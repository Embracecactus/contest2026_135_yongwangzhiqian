/****************************************************************************
 * board/bk7258/chip/include/bk7258_touch.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 CP capacitive-touch buttons lower-half declarations.
 *
 * The channel mask uses one bit per BK7258 touch channel (bits 0..15), but
 * the v3.1.1.9 touch_driver_v1_1.c implementation proves only one selected
 * channel per controller instance: its multi-channel setter is a BK_OK
 * no-op.  The lower-half therefore rejects multi-bit masks instead of
 * claiming simultaneous 16-channel scanning.  The three numeric tuning fields
 * are the values of the v3.1.1.9 SDK public
 * touch_sensitivity_level_t, touch_detect_threshold_t, and
 * touch_detect_range_t enumerations respectively.  Keeping the board config
 * independent of SDK typedefs lets the CP lower-half remain the only SDK
 * consumer while still exposing every configuration value the SDK accepts.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_TOUCH_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_TOUCH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/input/buttons.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_TOUCH_CHANNEL_COUNT 16u
#define BK7258_TOUCH_CHANNEL_MASK  ((1u << BK7258_TOUCH_CHANNEL_COUNT) - 1u)

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct bk7258_touch_config_s
{
  uint32_t channel_mask;       /* Exactly one bit from bits 0..15 */
  uint32_t poll_interval_ms;   /* NuttX work-queue sample period */
  uint8_t  sensitivity_level;  /* SDK values 0..3 */
  uint8_t  detect_threshold;   /* SDK values 0..7 */
  uint8_t  detect_range;       /* SDK values 0..3 */
  bool     calibrate;          /* Run SDK calibration for the channel */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_TOUCH

/****************************************************************************
 * Name: bk7258_touch_initialize
 *
 * Description:
 *   Configure the selected CP touch channel and return the standard NuttX
 *   buttons lower-half object.  The caller owns registration with btn_register
 *   and must not deinitialize the lower half while that upper half is open.
 *   Deinitialization is a task-context operation and synchronously waits for
 *   the polling work; it must not be called from that work item's callback.
 *   Sampling is deferred to the NuttX low-priority LPWORK queue because this
 *   is periodic polling rather than an IRQ bottom half.  The SDK touch ISR
 *   unconditionally consumes TIMER_ID1 for release detection; this
 *   lower-half deliberately leaves the SDK touch interrupt disabled.  The
 *   board integration must enable CONFIG_SCHED_LPWORK.
 *
 * Returned Value:
 *   Zero on success; a negated errno value on validation or SDK failure.
 ****************************************************************************/

int bk7258_touch_initialize(FAR struct btn_lowerhalf_s **lower,
                            FAR const struct bk7258_touch_config_s *config);

/****************************************************************************
 * Name: bk7258_touch_deinitialize
 *
 * Description:
 *   Cancel polling, disable CP touch interrupts, and power down the SDK touch
 *   block.  The public v3.1.1.9 SDK has no GPIO-unmap or driver-deinit API, so
 *   GPIO touch mux ownership cannot be restored by this wrapper.
 ****************************************************************************/

int bk7258_touch_deinitialize(void);

/****************************************************************************
 * Name: bk7258_touch_last_error
 *
 * Description:
 *   Return the last mapped SDK/lower-half error.  This is useful because the
 *   NuttX bl_enable contract is void and therefore cannot return an errno.
 ****************************************************************************/

int bk7258_touch_last_error(void);

#endif /* CONFIG_BK7258_TOUCH */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_TOUCH_H */
