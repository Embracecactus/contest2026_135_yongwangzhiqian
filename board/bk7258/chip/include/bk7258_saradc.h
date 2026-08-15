/****************************************************************************
 * contest2026_135_yongwangzhiqian/board/bk7258/chip/include/
 * bk7258_saradc.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 SARADC — NuttX adc_dev_s lower-half wrapper.
 *
 * Wraps the AP-side Beken SARADC mailbox client as a NuttX ADC lower half.
 * The BK7258 SARADC has 16 channels (ADC_0..ADC_15); the selected channel
 * remains a chip-level configuration and physical pin ownership belongs to
 * the selected-board binding.
 *
 * IMPORTANT SDK SEMANTICS:
 *   - The AP bk_adc_* implementation performs blocking mailbox IPC against
 *     the CP SARADC server.  The paired CP image must enable
 *     CONFIG_BK7258_SARADC_SERVER.
 *   - External channels require an explicit bk_adc_chan_init_gpio() mapping;
 *     bk_adc_init() deliberately does not configure the pin in v3.1.1.9.
 *   - ADC ownership is serialized on CP by bk_adc_acquire()/release().  The
 *     lower half holds that ownership for one triggered conversion only, so
 *     an open file descriptor cannot starve the on-die temperature client.
 *   - The v3.1.1.9 CP command dispatcher does not implement the AP client's
 *     SARADC_CMD_SINGLE_READ request.  The lower half therefore uses the
 *     supported init/config/start/read/stop/deinit pipeline and publishes
 *     its averaged raw 16-bit result.
 ****************************************************************************/

#ifndef __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_H
#define __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default SARADC channel (ADC_0..ADC_15). */

#ifndef CONFIG_BK7258_SARADC_CHAN
#  define CONFIG_BK7258_SARADC_CHAN      0
#endif

/* Default /dev/adcN device name. */

#ifndef CONFIG_BK7258_SARADC_BUS
#  define CONFIG_BK7258_SARADC_BUS       0
#endif

#define BK7258_SARADC_STRINGIFY_(value)  #value
#define BK7258_SARADC_STRINGIFY(value)   BK7258_SARADC_STRINGIFY_(value)

#ifndef CONFIG_BK7258_SARADC_DEVNAME
#  define CONFIG_BK7258_SARADC_DEVNAME   "/dev/adc" \
    BK7258_SARADC_STRINGIFY(CONFIG_BK7258_SARADC_BUS)
#endif

#define BK7258_SARADC_DIAG_MAGIC         0x43444153u /* "SADC" */
#define BK7258_SARADC_DIAG_VERSION       1u
#define BK7258_SARADC_VALIDATION_MAGIC   0x56444153u /* "SADV" */
#define BK7258_SARADC_VALIDATION_VERSION 1u
#define BK7258_SARADC_VALIDATION_MAX_SAMPLES 64u

#define BK7258_SARADC_RESOURCE_GPIO      (1u << 0)
#define BK7258_SARADC_RESOURCE_ACQUIRED  (1u << 1)
#define BK7258_SARADC_RESOURCE_INITED    (1u << 2)
#define BK7258_SARADC_RESOURCE_STARTED   (1u << 3)

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum bk7258_saradc_state_e
{
  BK7258_SARADC_STATE_RESET = 0,
  BK7258_SARADC_STATE_READY,
  BK7258_SARADC_STATE_CONVERTING,
  BK7258_SARADC_STATE_FAULT,
};

enum bk7258_saradc_stage_e
{
  BK7258_SARADC_STAGE_NONE = 0,
  BK7258_SARADC_STAGE_GPIO_MAP,
  BK7258_SARADC_STAGE_ACQUIRE,
  BK7258_SARADC_STAGE_INIT,
  BK7258_SARADC_STAGE_CONFIG,
  BK7258_SARADC_STAGE_BYPASS_CALIBRATION,
  BK7258_SARADC_STAGE_START,
  BK7258_SARADC_STAGE_READ,
  BK7258_SARADC_STAGE_STOP,
  BK7258_SARADC_STAGE_DEINIT,
  BK7258_SARADC_STAGE_RELEASE,
  BK7258_SARADC_STAGE_DELIVER,
  BK7258_SARADC_STAGE_GPIO_UNMAP,
  BK7258_SARADC_STAGE_COMPLETE,
};

enum bk7258_saradc_active_direction_e
{
  BK7258_SARADC_ACTIVE_LOW = 0,
  BK7258_SARADC_ACTIVE_HIGH = 1,
};

enum bk7258_saradc_validation_state_e
{
  BK7258_SARADC_VALIDATION_RESET = 0,
  BK7258_SARADC_VALIDATION_RUNNING,
  BK7258_SARADC_VALIDATION_WAIT_ACTIVE,
  BK7258_SARADC_VALIDATION_WAIT_RELEASE,
  BK7258_SARADC_VALIDATION_PASSED,
  BK7258_SARADC_VALIDATION_FAILED,
};

enum bk7258_saradc_validation_stage_e
{
  BK7258_SARADC_VALIDATION_STAGE_NONE = 0,
  BK7258_SARADC_VALIDATION_STAGE_OPEN,
  BK7258_SARADC_VALIDATION_STAGE_BASELINE,
  BK7258_SARADC_VALIDATION_STAGE_WAIT_ACTIVE,
  BK7258_SARADC_VALIDATION_STAGE_ACTIVE,
  BK7258_SARADC_VALIDATION_STAGE_WAIT_RELEASE,
  BK7258_SARADC_VALIDATION_STAGE_RELEASED,
  BK7258_SARADC_VALIDATION_STAGE_CLOSE,
  BK7258_SARADC_VALIDATION_STAGE_REOPEN,
  BK7258_SARADC_VALIDATION_STAGE_DRIVER_DIAG,
  BK7258_SARADC_VALIDATION_STAGE_DONE,
};

struct bk7258_saradc_validation_config_s
{
  FAR const char *devpath;
  uint32_t binding_id;
  uint32_t expected_channel;
  uint32_t active_direction;
  uint32_t samples_per_phase;
  uint32_t initial_delay_ms;
  uint32_t settle_ms;
  uint32_t poll_interval_ms;
  uint32_t phase_timeout_ms;
  uint32_t transition_confirm_samples;
  uint32_t minimum_delta_raw;
  uint32_t minimum_delta_permille;
  uint32_t minimum_release_tolerance_raw;
  uint32_t release_tolerance_permille;
  uint32_t maximum_noise_permille;
};

struct bk7258_saradc_diag_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t state;
  uint32_t channel;
  uint32_t resources;
  int32_t first_error;
  int32_t first_sdk_error;
  int32_t last_error;
  int32_t last_sdk_error;
  uint32_t last_stage;
  uint32_t setup_count;
  uint32_t shutdown_count;
  uint32_t trigger_count;
  uint32_t sample_count;
  uint32_t deliver_count;
  uint32_t callback_error_count;
  uint32_t gpio_map_count;
  uint32_t gpio_unmap_count;
  uint32_t acquire_count;
  uint32_t release_count;
  uint32_t init_count;
  uint32_t deinit_count;
  uint32_t config_count;
  uint32_t bypass_count;
  uint32_t start_count;
  uint32_t stop_count;
  uint32_t last_raw;
  uint32_t minimum_raw;
  uint32_t maximum_raw;
};

struct bk7258_saradc_validation_diag_s
{
  uint32_t magic;
  uint32_t version;
  uint32_t size;
  uint32_t state;
  int32_t result;
  uint32_t stage;
  uint32_t binding_id;
  uint32_t expected_channel;
  uint32_t active_direction;
  uint32_t samples_per_phase;
  uint32_t open_count;
  uint32_t close_count;
  uint32_t fifo_reset_count;
  uint32_t trigger_count;
  uint32_t read_count;
  uint32_t sample_count;
  uint32_t transition_count;
  uint32_t baseline_min;
  uint32_t baseline_max;
  uint32_t baseline_median;
  uint32_t active_min;
  uint32_t active_max;
  uint32_t active_median;
  uint32_t released_min;
  uint32_t released_max;
  uint32_t released_median;
  uint32_t required_delta;
  uint32_t observed_delta;
  uint32_t release_tolerance;
  uint32_t reopen_raw;
  uint32_t open_errors;
  uint32_t ioctl_errors;
  uint32_t read_errors;
  uint32_t short_read_errors;
  uint32_t channel_errors;
  uint32_t range_errors;
  uint32_t timeout_count;
  uint32_t driver_state;
  uint32_t driver_resources;
  int32_t driver_first_error;
  uint32_t driver_setup_delta;
  uint32_t driver_shutdown_delta;
  uint32_t driver_trigger_delta;
  uint32_t driver_sample_delta;
  uint32_t driver_deliver_delta;
  uint32_t driver_gpio_map_delta;
  uint32_t driver_gpio_unmap_delta;
  uint32_t driver_acquire_delta;
  uint32_t driver_release_delta;
  uint32_t driver_init_delta;
  uint32_t driver_deinit_delta;
  uint32_t driver_config_delta;
  uint32_t driver_bypass_delta;
  uint32_t driver_start_delta;
  uint32_t driver_stop_delta;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_BK7258_SARADC
#ifdef CONFIG_BK7258_AP_CORE

/****************************************************************************
 * Name: bk7258_saradc_initialize
 *
 * Description:
 *   Construct a NuttX ADC lower-half for the BK7258 SARADC channel
 *   CONFIG_BK7258_SARADC_CHAN and register it at /dev/adcN
 *   (N = CONFIG_BK7258_SARADC_BUS).  No hardware or pin mux is touched until
 *   the upper half opens the device; this only constructs the lower half and
 *   publishes the node.
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.  Calling it twice is
 *   harmless and returns OK.
 *
 ****************************************************************************/

int bk7258_saradc_initialize(void);

/****************************************************************************
 * Name: bk7258_saradc_get_diag
 *
 * Description:
 *   Copy the chip-level ownership and lifecycle diagnostics.  This is an
 *   observational interface for bounded validation; applications acquire
 *   samples exclusively through the standard NuttX ADC ABI.
 ****************************************************************************/

int bk7258_saradc_get_diag(struct bk7258_saradc_diag_s *diag);

#ifdef CONFIG_BK7258_SARADC_VALIDATION
extern volatile struct bk7258_saradc_validation_diag_s
  g_bk7258_saradc_validation_diag;

/****************************************************************************
 * Name: bk7258_saradc_validation_start
 *
 * Description:
 *   Start one bounded validation task whose sample acquisition uses the
 *   public NuttX ADC character-device ABI.  After closing the device it also
 *   checks this chip wrapper's lifecycle diagnostics.  The descriptor must
 *   remain valid for the life of the task and is normally supplied as
 *   selected-board constant data.
 ****************************************************************************/

int bk7258_saradc_validation_start(
  FAR const struct bk7258_saradc_validation_config_s *config);
#endif

#endif /* CONFIG_BK7258_AP_CORE */
#endif /* CONFIG_BK7258_SARADC */

#endif /* __ARCH_ARM_SRC_BK7258_INCLUDE_BK7258_SARADC_H */
