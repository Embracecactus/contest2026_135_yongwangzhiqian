/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>

#include <nuttx/config.h>
#include <arch/chip/bk7258_pm.h>
#include <common/bk_err.h>
#include <nuttx/mutex.h>

#include "bk7258_media_root.h"

static int g_pm_get_result;
static int g_pm_put_result;
static unsigned int g_pm_get_count;
static unsigned int g_pm_put_count;

int nxmutex_lock(mutex_t *mutex)
{
  return pthread_mutex_lock(mutex) == 0 ? OK : -EINVAL;
}

int nxmutex_unlock(mutex_t *mutex)
{
  return pthread_mutex_unlock(mutex) == 0 ? OK : -EINVAL;
}

int bk7258_pm_clock_get(enum bk7258_pm_clock_e clock)
{
  assert(clock == BK7258_PM_CLOCK_AUDIO);
  g_pm_get_count++;
  return g_pm_get_result;
}

int bk7258_pm_clock_put(enum bk7258_pm_clock_e clock)
{
  assert(clock == BK7258_PM_CLOCK_AUDIO);
  g_pm_put_count++;
  return g_pm_put_result;
}

int bk7258_pm_frequency_vote(enum bk7258_pm_freq_client_e client,
                            enum bk7258_pm_cpu_freq_e frequency)
{
  assert(client == BK7258_PM_FREQ_CLIENT_AUDIO);
  assert(frequency == BK7258_PM_OPP_480M ||
         frequency == BK7258_PM_OPP_DEFAULT);
  return OK;
}

bk_err_t bk_dma_driver_init(void)
{
  return BK_OK;
}

bk_err_t bk_yuv_buf_driver_init(void)
{
  return BK_OK;
}

bk_err_t bk_jpeg_enc_driver_init(void)
{
  return BK_OK;
}

bk_err_t bk_h264_driver_init(void)
{
  return BK_OK;
}

static void test_normal_ownership(void)
{
  unsigned int gets = g_pm_get_count;
  unsigned int puts = g_pm_put_count;

  assert(bk7258_media_audio_session_acquire(0) == -EINVAL);
  assert(bk7258_media_audio_session_release(0) == -EINVAL);
  assert(bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_MIC) == OK);
  assert(g_pm_get_count == gets + 1);
  assert(bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_DAC) == OK);
  assert(g_pm_get_count == gets + 1);
  assert(bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_MIC) ==
         -EBUSY);
  assert(bk7258_media_audio_session_release(BK7258_MEDIA_AUDIO_MIC) == OK);
  assert(g_pm_put_count == puts);
  assert(bk7258_media_audio_session_release(BK7258_MEDIA_AUDIO_DAC) == OK);
  assert(g_pm_put_count == puts + 1);
}

static void test_failed_get_is_compensated(void)
{
  unsigned int gets = g_pm_get_count;
  unsigned int puts = g_pm_put_count;

  g_pm_get_result = -ETIMEDOUT;
  g_pm_put_result = -EALREADY;
  assert(bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_MIC) ==
         -ETIMEDOUT);
  assert(g_pm_get_count == gets + 1);
  assert(g_pm_put_count == puts + 1);

  g_pm_get_result = OK;
  g_pm_put_result = OK;
  assert(bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_MIC) == OK);
  assert(g_pm_get_count == gets + 2);
  assert(g_pm_put_count == puts + 1);
  assert(bk7258_media_audio_session_release(BK7258_MEDIA_AUDIO_MIC) == OK);
}

static void test_uncertain_get_recovers_before_retry(void)
{
  unsigned int gets = g_pm_get_count;
  unsigned int puts = g_pm_put_count;

  g_pm_get_result = -ETIMEDOUT;
  g_pm_put_result = -ETIMEDOUT;
  assert(bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_DAC) ==
         -ETIMEDOUT);
  assert(g_pm_get_count == gets + 1);
  assert(g_pm_put_count == puts + 1);

  g_pm_get_result = OK;
  g_pm_put_result = OK;
  assert(bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_DAC) == OK);
  assert(g_pm_get_count == gets + 2);
  assert(g_pm_put_count == puts + 2);
  assert(bk7258_media_audio_session_release(BK7258_MEDIA_AUDIO_DAC) == OK);
}

static void test_failed_release_retains_owner(void)
{
  unsigned int gets;

  g_pm_get_result = OK;
  g_pm_put_result = OK;
  assert(bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_MIC) == OK);
  gets = g_pm_get_count;

  g_pm_put_result = -ETIMEDOUT;
  assert(bk7258_media_audio_session_release(BK7258_MEDIA_AUDIO_MIC) ==
         -ETIMEDOUT);
  assert(bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_DAC) == OK);
  assert(g_pm_get_count == gets);
  assert(bk7258_media_audio_session_release(BK7258_MEDIA_AUDIO_DAC) == OK);

  g_pm_put_result = OK;
  assert(bk7258_media_audio_session_release(BK7258_MEDIA_AUDIO_MIC) == OK);

  assert(bk7258_media_audio_session_acquire(BK7258_MEDIA_AUDIO_DAC) == OK);
  g_pm_put_result = -EALREADY;
  assert(bk7258_media_audio_session_release(BK7258_MEDIA_AUDIO_DAC) == OK);
}

int main(void)
{
  test_normal_ownership();
  test_failed_get_is_compensated();
  test_uncertain_get_recovers_before_retry();
  test_failed_release_retains_owner();

  puts("BK7258_MEDIA_AUDIO_SESSION_PASS");
  return 0;
}
