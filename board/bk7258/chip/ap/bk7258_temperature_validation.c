/****************************************************************************
 * board/bk7258/chip/ap/bk7258_temperature_validation.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Bounded real-board validation for the CP/AP temperature path.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_TEMPERATURE_VALIDATION

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/signal.h>
#include <nuttx/wqueue.h>

#include <arch/chip/bk7258_temperature.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_TEMP_VALIDATION_MAGIC       0x504d5442u /* "BTMP" */
#define BK7258_TEMP_VALIDATION_VERSION     1u
#define BK7258_TEMP_VALIDATION_SAMPLES     8u
#define BK7258_TEMP_VALIDATION_DELAY_US    100000u

#define BK7258_TEMP_VALIDATION_RUNNING     1u
#define BK7258_TEMP_VALIDATION_PASSED      2u
#define BK7258_TEMP_VALIDATION_FAILED      3u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_temperature_validation_diag_s
{
  uint32_t magic;
  uint16_t version;
  uint16_t state;
  int32_t status;
  uint32_t generation;
  uint32_t successful_samples;
  uint32_t failed_samples;
  uint32_t minimum_raw;
  uint32_t maximum_raw;
  uint32_t last_raw;
  int32_t last_millicelsius;
  uint32_t last_flags;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

volatile struct bk7258_temperature_validation_diag_s
g_bk7258_temperature_validation_diag;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct work_s g_bk7258_temperature_validation_work;
static volatile bool g_bk7258_temperature_validation_started;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_temperature_validation_worker(void *arg)
{
  struct bk7258_temperature_sample_s sample;
  uint32_t expected_generation = 0;
  uint32_t minimum_raw = UINT32_MAX;
  uint32_t maximum_raw = 0;
  unsigned int index;
  int status = OK;

  (void)arg;
  for (index = 0; index < BK7258_TEMP_VALIDATION_SAMPLES; index++)
    {
      int ret = bk7258_temperature_read(&sample);

      if (ret < 0)
        {
          g_bk7258_temperature_validation_diag.failed_samples++;
          status = ret;
        }
      else if ((sample.flags & BK7258_TEMPERATURE_FLAG_RAW_VALID) == 0 ||
               sample.raw_code < BK7258_TEMPERATURE_RAW_MIN ||
               sample.raw_code > BK7258_TEMPERATURE_RAW_MAX)
        {
          g_bk7258_temperature_validation_diag.failed_samples++;
          status = -ERANGE;
        }
      else if (expected_generation != 0 &&
               sample.generation != expected_generation)
        {
          g_bk7258_temperature_validation_diag.failed_samples++;
          status = -ESTALE;
        }
      else
        {
          if (expected_generation == 0)
            {
              expected_generation = sample.generation;
            }

          if (sample.raw_code < minimum_raw)
            {
              minimum_raw = sample.raw_code;
            }

          if (sample.raw_code > maximum_raw)
            {
              maximum_raw = sample.raw_code;
            }

          g_bk7258_temperature_validation_diag.successful_samples++;
          g_bk7258_temperature_validation_diag.generation =
            sample.generation;
          g_bk7258_temperature_validation_diag.last_raw = sample.raw_code;
          g_bk7258_temperature_validation_diag.last_millicelsius =
            sample.temperature_millicelsius;
          g_bk7258_temperature_validation_diag.last_flags = sample.flags;
        }

      if (index + 1u < BK7258_TEMP_VALIDATION_SAMPLES)
        {
          nxsig_usleep(BK7258_TEMP_VALIDATION_DELAY_US);
        }
    }

  g_bk7258_temperature_validation_diag.minimum_raw =
    minimum_raw == UINT32_MAX ? 0 : minimum_raw;
  g_bk7258_temperature_validation_diag.maximum_raw = maximum_raw;
  g_bk7258_temperature_validation_diag.status = status;
  g_bk7258_temperature_validation_diag.state =
    status == OK &&
    g_bk7258_temperature_validation_diag.successful_samples ==
      BK7258_TEMP_VALIDATION_SAMPLES ?
      BK7258_TEMP_VALIDATION_PASSED :
      BK7258_TEMP_VALIDATION_FAILED;
  __asm volatile ("dmb sy" ::: "memory");

  syslog(LOG_INFO,
         "BTEMP %s status=%d generation=%lu samples=%lu/%u "
         "raw=%lu..%lu last=%lu flags=0x%lx\n",
         g_bk7258_temperature_validation_diag.state ==
           BK7258_TEMP_VALIDATION_PASSED ? "PASS" : "FAIL",
         status,
         (unsigned long)g_bk7258_temperature_validation_diag.generation,
         (unsigned long)
           g_bk7258_temperature_validation_diag.successful_samples,
         BK7258_TEMP_VALIDATION_SAMPLES,
         (unsigned long)g_bk7258_temperature_validation_diag.minimum_raw,
         (unsigned long)g_bk7258_temperature_validation_diag.maximum_raw,
         (unsigned long)g_bk7258_temperature_validation_diag.last_raw,
         (unsigned long)g_bk7258_temperature_validation_diag.last_flags);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_temperature_validation_start(void)
{
  bool expected = false;
  int ret;

  if (!__atomic_compare_exchange_n(
        &g_bk7258_temperature_validation_started, &expected, true,
        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      return OK;
    }

  g_bk7258_temperature_validation_diag.magic =
    BK7258_TEMP_VALIDATION_MAGIC;
  g_bk7258_temperature_validation_diag.version =
    BK7258_TEMP_VALIDATION_VERSION;
  g_bk7258_temperature_validation_diag.state =
    BK7258_TEMP_VALIDATION_RUNNING;
  g_bk7258_temperature_validation_diag.status = -EINPROGRESS;
  g_bk7258_temperature_validation_diag.generation = 0;
  g_bk7258_temperature_validation_diag.successful_samples = 0;
  g_bk7258_temperature_validation_diag.failed_samples = 0;
  g_bk7258_temperature_validation_diag.minimum_raw = 0;
  g_bk7258_temperature_validation_diag.maximum_raw = 0;
  g_bk7258_temperature_validation_diag.last_raw = 0;
  g_bk7258_temperature_validation_diag.last_millicelsius = 0;
  g_bk7258_temperature_validation_diag.last_flags = 0;
  __asm volatile ("dmb sy" ::: "memory");

  ret = work_queue(LPWORK, &g_bk7258_temperature_validation_work,
                   bk7258_temperature_validation_worker, NULL, 0);
  if (ret < 0)
    {
      g_bk7258_temperature_validation_diag.status = ret;
      g_bk7258_temperature_validation_diag.state =
        BK7258_TEMP_VALIDATION_FAILED;
      __atomic_store_n(&g_bk7258_temperature_validation_started, false,
                       __ATOMIC_RELEASE);
    }

  return ret;
}

#endif /* CONFIG_BK7258_TEMPERATURE_VALIDATION */
