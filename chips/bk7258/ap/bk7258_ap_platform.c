/****************************************************************************
 * chips/bk7258/ap/
 * bk7258_ap_platform.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * One-shot AP-local platform preparation for the Beken BK7258.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>

#include <arch/chip/bk7258_ap_platform.h>

#ifdef CONFIG_BK7258_PM_CLOCK
#  include <arch/chip/bk7258_pm.h>
#endif

#ifdef CONFIG_BK7258_PSRAM
#  include <arch/chip/bk7258_psram.h>
#endif

#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
#  include <arch/chip/bk7258_sdk_runtime.h>
#endif

#ifdef CONFIG_BK7258_TEMPERATURE
#  include <arch/chip/bk7258_temperature.h>
#endif

#include <arch/chip/bk7258_stage_runner.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_ap_stage_execute(
  FAR void *context, FAR const struct bk7258_stage_desc_s *stage)
{
  int ret = 0;

  UNUSED(context);

  switch (stage->id)
    {
      case BK7258_AP_STAGE_ROLE_CONTRACT:
        break;

#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
      case BK7258_AP_STAGE_SDK_RUNTIME:
        ret = bk7258_sdk_runtime_initialize();
        break;
#endif

#ifdef CONFIG_BK7258_PM_CLOCK
      case BK7258_AP_STAGE_PM_CLIENT:
        ret = bk7258_pm_initialize();
        break;
#endif

#ifdef CONFIG_BK7258_TEMPERATURE
      case BK7258_AP_STAGE_TEMPERATURE_CLIENT:
        ret = bk7258_temperature_initialize();
        break;
#endif

#ifdef CONFIG_BK7258_PSRAM
      case BK7258_AP_STAGE_PSRAM:
        ret = bk7258_psram_initialize();
        break;
#endif

      default:
        ret = -EINVAL;
        break;
    }

  return ret;
}
/****************************************************************************
 * Private Data
 ****************************************************************************/

#define BK7258_AP_STAGE_BIT(_id) (UINT32_C(1) << (_id))
#define BK7258_AP_STAGE(_id, _flags, _requires) \
  {(_requires), (_id), BK7258_STAGE_MANDATORY, (_flags)}
#define BK7258_AP_STAGE_COUNT_VALUE \
  (sizeof(g_bk7258_ap_stages) / sizeof(g_bk7258_ap_stages[0]))

#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
#  define BK7258_AP_PM_REQUIRES \
  BK7258_AP_STAGE_BIT(BK7258_AP_STAGE_SDK_RUNTIME)
#else
#  define BK7258_AP_PM_REQUIRES \
  BK7258_AP_STAGE_BIT(BK7258_AP_STAGE_ROLE_CONTRACT)
#endif

#ifdef CONFIG_BK7258_PM_CLOCK
#  define BK7258_AP_TEMPERATURE_REQUIRES \
  BK7258_AP_STAGE_BIT(BK7258_AP_STAGE_PM_CLIENT)
#elif defined(CONFIG_BK7258_SDK_IPC_RUNTIME)
#  define BK7258_AP_TEMPERATURE_REQUIRES \
  BK7258_AP_STAGE_BIT(BK7258_AP_STAGE_SDK_RUNTIME)
#else
#  define BK7258_AP_TEMPERATURE_REQUIRES \
  BK7258_AP_STAGE_BIT(BK7258_AP_STAGE_ROLE_CONTRACT)
#endif

static const struct bk7258_stage_desc_s g_bk7258_ap_stages[] =
{
  BK7258_AP_STAGE(BK7258_AP_STAGE_ROLE_CONTRACT, 0, 0),
#ifdef CONFIG_BK7258_SDK_IPC_RUNTIME
  BK7258_AP_STAGE(
    BK7258_AP_STAGE_SDK_RUNTIME, 0,
    BK7258_AP_STAGE_BIT(BK7258_AP_STAGE_ROLE_CONTRACT)),
#endif
#ifdef CONFIG_BK7258_PM_CLOCK
  BK7258_AP_STAGE(
    BK7258_AP_STAGE_PM_CLIENT, 0,
    BK7258_AP_PM_REQUIRES),
#endif
#ifdef CONFIG_BK7258_TEMPERATURE
  BK7258_AP_STAGE(
    BK7258_AP_STAGE_TEMPERATURE_CLIENT, 0,
    BK7258_AP_TEMPERATURE_REQUIRES),
#endif
#ifdef CONFIG_BK7258_PSRAM
  /* The baseline late hook initialized AP PSRAM independently of the
   * SDK/PM/temperature chain.  Preserve that ordering and eligibility while
   * still retaining the first mandatory failure in the shared runner.
   */

  BK7258_AP_STAGE(
    BK7258_AP_STAGE_PSRAM, BK7258_STAGE_FLAG_ALWAYS_RUN,
    BK7258_AP_STAGE_BIT(BK7258_AP_STAGE_ROLE_CONTRACT)),
#endif
};

static struct bk7258_stage_runner_s g_bk7258_ap_runner =
{
  .lock = NXMUTEX_INITIALIZER,
  .stages = g_bk7258_ap_stages,
  .execute = bk7258_ap_stage_execute,
  .stage_count = BK7258_AP_STAGE_COUNT_VALUE,
  .state = BK7258_PLATFORM_NEW,
  .first_error_stage = BK7258_STAGE_ID_INVALID,
};

_Static_assert(BK7258_AP_STAGE_COUNT <= BK7258_STAGE_ID_LIMIT,
               "AP platform stage ID exceeds the status mask");
_Static_assert(BK7258_AP_STAGE_COUNT_VALUE > 0 &&
               BK7258_AP_STAGE_COUNT_VALUE <= BK7258_STAGE_ID_LIMIT,
               "invalid AP platform stage table size");

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_ap_platform_prepare(void)
{
  return bk7258_stage_runner_run(&g_bk7258_ap_runner, NULL);
}

int bk7258_ap_platform_result(void)
{
  return bk7258_stage_runner_result(&g_bk7258_ap_runner);
}

int bk7258_ap_platform_get_status(
  FAR struct bk7258_platform_status_s *status)
{
  return bk7258_stage_runner_snapshot(&g_bk7258_ap_runner, status);
}
