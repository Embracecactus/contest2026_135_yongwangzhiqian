/****************************************************************************
 * board/bk7258/chip/ap/bk7258_saradc_validation.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Chip-generic bounded sample validation through the public NuttX ADC ABI.
 * Chip diagnostics are read only after the sample path closes to prove
 * resource and lifecycle symmetry.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_SARADC_VALIDATION

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <unistd.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/cache.h>
#include <nuttx/kthread.h>

#include <arch/chip/bk7258_saradc.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SADV_STACKSIZE                    4096
#define SADV_PRIORITY                     SCHED_PRIORITY_DEFAULT
#define SADV_PERMILLE                     1000u

_Static_assert(sizeof(struct adc_msg_s) == 5,
               "unexpected NuttX adc_msg_s wire size");

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sadv_phase_s
{
  uint32_t minimum;
  uint32_t maximum;
  uint32_t median;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

volatile struct bk7258_saradc_validation_diag_s
g_bk7258_saradc_validation_diag;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR const struct bk7258_saradc_validation_config_s *g_sadv_config;
static volatile bool g_sadv_started;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int sadv_errno(void)
{
  return errno > 0 ? -errno : -EIO;
}

static uint32_t sadv_max(uint32_t left, uint32_t right)
{
  return left > right ? left : right;
}

static uint32_t sadv_delta(uint32_t left, uint32_t right)
{
  return left > right ? left - right : right - left;
}

static void sadv_publish(void)
{
  __asm volatile ("dmb sy" ::: "memory");
  up_clean_dcache((uintptr_t)&g_bk7258_saradc_validation_diag,
                  (uintptr_t)&g_bk7258_saradc_validation_diag +
                  sizeof(g_bk7258_saradc_validation_diag));
}

static void sadv_set_stage(uint32_t state, uint32_t stage)
{
  g_bk7258_saradc_validation_diag.state = state;
  g_bk7258_saradc_validation_diag.stage = stage;
  sadv_publish();
}

static int sadv_reset_fifo(int fd)
{
  if (ioctl(fd, ANIOC_RESET_FIFO, 0) < 0)
    {
      g_bk7258_saradc_validation_diag.ioctl_errors++;
      return sadv_errno();
    }

  g_bk7258_saradc_validation_diag.fifo_reset_count++;
  return OK;
}

static int sadv_sample(int fd, uint16_t *raw)
{
  struct adc_msg_s message;
  ssize_t nread;

  if (ioctl(fd, ANIOC_TRIGGER, 0) < 0)
    {
      g_bk7258_saradc_validation_diag.ioctl_errors++;
      return sadv_errno();
    }

  g_bk7258_saradc_validation_diag.trigger_count++;
  nread = read(fd, &message, sizeof(message));
  if (nread < 0)
    {
      g_bk7258_saradc_validation_diag.read_errors++;
      return sadv_errno();
    }

  if (nread != sizeof(message))
    {
      g_bk7258_saradc_validation_diag.short_read_errors++;
      return -EIO;
    }

  g_bk7258_saradc_validation_diag.read_count++;
  if (message.am_channel != g_sadv_config->expected_channel)
    {
      g_bk7258_saradc_validation_diag.channel_errors++;
      return -ENXIO;
    }

  if (message.am_data < 0 || message.am_data > UINT16_MAX)
    {
      g_bk7258_saradc_validation_diag.range_errors++;
      return -ERANGE;
    }

  *raw = (uint16_t)message.am_data;
  g_bk7258_saradc_validation_diag.sample_count++;
  return OK;
}

static void sadv_sort(uint16_t *samples, uint32_t count)
{
  uint32_t outer;

  for (outer = 1; outer < count; outer++)
    {
      uint16_t value = samples[outer];
      uint32_t inner = outer;

      while (inner > 0 && samples[inner - 1] > value)
        {
          samples[inner] = samples[inner - 1];
          inner--;
        }

      samples[inner] = value;
    }
}

static int sadv_collect_phase(int fd, struct sadv_phase_s *phase)
{
  uint16_t samples[BK7258_SARADC_VALIDATION_MAX_SAMPLES];
  uint32_t index;
  int ret;

  ret = sadv_reset_fifo(fd);
  if (ret < 0)
    {
      return ret;
    }

  for (index = 0; index < g_sadv_config->samples_per_phase; index++)
    {
      ret = sadv_sample(fd, &samples[index]);
      if (ret < 0)
        {
          return ret;
        }

      if (index + 1u < g_sadv_config->samples_per_phase)
        {
          usleep(g_sadv_config->poll_interval_ms * 1000u);
        }
    }

  sadv_sort(samples, g_sadv_config->samples_per_phase);
  phase->minimum = samples[0];
  phase->maximum = samples[g_sadv_config->samples_per_phase - 1u];
  phase->median = samples[g_sadv_config->samples_per_phase / 2u];
  return OK;
}

static bool sadv_is_active(uint32_t raw, uint32_t baseline,
                           uint32_t required)
{
  if (g_sadv_config->active_direction == BK7258_SARADC_ACTIVE_LOW)
    {
      return baseline >= required && raw <= baseline - required;
    }

  return raw >= baseline && raw - baseline >= required;
}

static bool sadv_is_released(uint32_t raw, uint32_t baseline,
                             uint32_t tolerance)
{
  return sadv_delta(raw, baseline) <= tolerance;
}

static int sadv_wait_transition(int fd, uint32_t baseline,
                                uint32_t threshold, bool active)
{
  uint32_t required_consecutive =
    g_sadv_config->transition_confirm_samples;
  uint32_t iterations = g_sadv_config->phase_timeout_ms /
                        g_sadv_config->poll_interval_ms + 1u;
  uint32_t consecutive = 0;
  uint32_t index;
  int ret;

  ret = sadv_reset_fifo(fd);
  if (ret < 0)
    {
      return ret;
    }

  for (index = 0; index < iterations; index++)
    {
      uint16_t raw;
      bool matched;

      ret = sadv_sample(fd, &raw);
      if (ret < 0)
        {
          return ret;
        }

      matched = active ? sadv_is_active(raw, baseline, threshold) :
                         sadv_is_released(raw, baseline, threshold);
      if (matched)
        {
          consecutive++;
          if (consecutive >= required_consecutive)
            {
              g_bk7258_saradc_validation_diag.transition_count++;
              return OK;
            }
        }
      else
        {
          consecutive = 0;
        }

      usleep(g_sadv_config->poll_interval_ms * 1000u);
    }

  g_bk7258_saradc_validation_diag.timeout_count++;
  return -ETIMEDOUT;
}

static int sadv_open(void)
{
  int fd = open(g_sadv_config->devpath, O_RDONLY | O_NONBLOCK | O_CLOEXEC);

  if (fd < 0)
    {
      g_bk7258_saradc_validation_diag.open_errors++;
      return sadv_errno();
    }

  g_bk7258_saradc_validation_diag.open_count++;
  return fd;
}

static int sadv_close(int fd)
{
  if (close(fd) < 0)
    {
      return sadv_errno();
    }

  g_bk7258_saradc_validation_diag.close_count++;
  return OK;
}

static uint32_t sadv_counter_delta(uint32_t after, uint32_t before)
{
  return after - before;
}

static int sadv_publish_driver_diag(
  const struct bk7258_saradc_diag_s *before,
  const struct bk7258_saradc_diag_s *after)
{
  volatile struct bk7258_saradc_validation_diag_s *diag =
    &g_bk7258_saradc_validation_diag;

  diag->driver_state = after->state;
  diag->driver_resources = after->resources;
  diag->driver_first_error = after->first_error;
  diag->driver_setup_delta =
    sadv_counter_delta(after->setup_count, before->setup_count);
  diag->driver_shutdown_delta =
    sadv_counter_delta(after->shutdown_count, before->shutdown_count);
  diag->driver_trigger_delta =
    sadv_counter_delta(after->trigger_count, before->trigger_count);
  diag->driver_sample_delta =
    sadv_counter_delta(after->sample_count, before->sample_count);
  diag->driver_deliver_delta =
    sadv_counter_delta(after->deliver_count, before->deliver_count);
  diag->driver_gpio_map_delta =
    sadv_counter_delta(after->gpio_map_count, before->gpio_map_count);
  diag->driver_gpio_unmap_delta =
    sadv_counter_delta(after->gpio_unmap_count, before->gpio_unmap_count);
  diag->driver_acquire_delta =
    sadv_counter_delta(after->acquire_count, before->acquire_count);
  diag->driver_release_delta =
    sadv_counter_delta(after->release_count, before->release_count);
  diag->driver_init_delta =
    sadv_counter_delta(after->init_count, before->init_count);
  diag->driver_deinit_delta =
    sadv_counter_delta(after->deinit_count, before->deinit_count);
  diag->driver_config_delta =
    sadv_counter_delta(after->config_count, before->config_count);
  diag->driver_bypass_delta =
    sadv_counter_delta(after->bypass_count, before->bypass_count);
  diag->driver_start_delta =
    sadv_counter_delta(after->start_count, before->start_count);
  diag->driver_stop_delta =
    sadv_counter_delta(after->stop_count, before->stop_count);

  if (after->state != BK7258_SARADC_STATE_RESET ||
      after->resources != 0 || after->first_error != OK ||
      diag->driver_setup_delta != 2 ||
      diag->driver_shutdown_delta != 2 ||
      diag->driver_gpio_map_delta != 2 ||
      diag->driver_gpio_unmap_delta != 2 ||
      diag->driver_trigger_delta == 0 ||
      diag->driver_trigger_delta != diag->driver_sample_delta ||
      diag->driver_sample_delta != diag->driver_deliver_delta ||
      diag->driver_acquire_delta != diag->driver_trigger_delta ||
      diag->driver_release_delta != diag->driver_trigger_delta ||
      diag->driver_init_delta != diag->driver_trigger_delta ||
      diag->driver_deinit_delta != diag->driver_trigger_delta ||
      diag->driver_config_delta != diag->driver_trigger_delta ||
      diag->driver_bypass_delta != diag->driver_trigger_delta ||
      diag->driver_start_delta != diag->driver_trigger_delta ||
      diag->driver_stop_delta != diag->driver_trigger_delta)
    {
      return -EIO;
    }

  return OK;
}

static int sadv_validate_config(
  FAR const struct bk7258_saradc_validation_config_s *config)
{
  if (config == NULL || config->devpath == NULL ||
      config->expected_channel > 15 ||
      config->active_direction > BK7258_SARADC_ACTIVE_HIGH ||
      config->samples_per_phase == 0 ||
      config->samples_per_phase > BK7258_SARADC_VALIDATION_MAX_SAMPLES ||
      config->poll_interval_ms == 0 || config->phase_timeout_ms == 0 ||
      config->transition_confirm_samples == 0 ||
      config->transition_confirm_samples > config->samples_per_phase ||
      config->minimum_delta_permille > SADV_PERMILLE ||
      config->release_tolerance_permille > SADV_PERMILLE ||
      config->maximum_noise_permille > SADV_PERMILLE)
    {
      return -EINVAL;
    }

  return OK;
}

static int sadv_thread(int argc, char **argv)
{
  struct bk7258_saradc_diag_s before;
  struct bk7258_saradc_diag_s after;
  struct sadv_phase_s baseline;
  struct sadv_phase_s active;
  struct sadv_phase_s released;
  uint32_t allowed_noise;
  uint16_t reopen_raw;
  bool before_valid = false;
  int fd = -1;
  int ret;
  int cleanup_ret;

  (void)argc;
  (void)argv;

  ret = bk7258_saradc_get_diag(&before);
  if (ret < 0)
    {
      goto done;
    }

  before_valid = true;
  usleep(g_sadv_config->initial_delay_ms * 1000u);
  sadv_set_stage(BK7258_SARADC_VALIDATION_RUNNING,
                 BK7258_SARADC_VALIDATION_STAGE_OPEN);
  fd = sadv_open();
  if (fd < 0)
    {
      ret = fd;
      fd = -1;
      goto done;
    }

  syslog(LOG_INFO,
         "SADV binding=0x%08" PRIx32 " keep endpoint released\n",
         g_sadv_config->binding_id);
  sadv_set_stage(BK7258_SARADC_VALIDATION_RUNNING,
                 BK7258_SARADC_VALIDATION_STAGE_BASELINE);
  ret = sadv_collect_phase(fd, &baseline);
  if (ret < 0)
    {
      goto done;
    }

  g_bk7258_saradc_validation_diag.baseline_min = baseline.minimum;
  g_bk7258_saradc_validation_diag.baseline_max = baseline.maximum;
  g_bk7258_saradc_validation_diag.baseline_median = baseline.median;
  g_bk7258_saradc_validation_diag.required_delta =
    sadv_max(g_sadv_config->minimum_delta_raw,
             baseline.median * g_sadv_config->minimum_delta_permille /
             SADV_PERMILLE);
  g_bk7258_saradc_validation_diag.release_tolerance =
    sadv_max(g_sadv_config->minimum_release_tolerance_raw,
             baseline.median * g_sadv_config->release_tolerance_permille /
             SADV_PERMILLE);

  syslog(LOG_INFO,
         "SADV activate binding=0x%08" PRIx32 " within %" PRIu32 " ms\n",
         g_sadv_config->binding_id, g_sadv_config->phase_timeout_ms);
  sadv_set_stage(BK7258_SARADC_VALIDATION_WAIT_ACTIVE,
                 BK7258_SARADC_VALIDATION_STAGE_WAIT_ACTIVE);
  ret = sadv_wait_transition(
          fd, baseline.median,
          g_bk7258_saradc_validation_diag.required_delta, true);
  if (ret < 0)
    {
      goto done;
    }

  usleep(g_sadv_config->settle_ms * 1000u);
  sadv_set_stage(BK7258_SARADC_VALIDATION_RUNNING,
                 BK7258_SARADC_VALIDATION_STAGE_ACTIVE);
  ret = sadv_collect_phase(fd, &active);
  if (ret < 0)
    {
      goto done;
    }

  g_bk7258_saradc_validation_diag.active_min = active.minimum;
  g_bk7258_saradc_validation_diag.active_max = active.maximum;
  g_bk7258_saradc_validation_diag.active_median = active.median;
  if (!sadv_is_active(active.median, baseline.median,
                      g_bk7258_saradc_validation_diag.required_delta))
    {
      ret = -ERANGE;
      goto done;
    }

  g_bk7258_saradc_validation_diag.observed_delta =
    sadv_delta(active.median, baseline.median);

  syslog(LOG_INFO,
         "SADV release binding=0x%08" PRIx32 " within %" PRIu32 " ms\n",
         g_sadv_config->binding_id, g_sadv_config->phase_timeout_ms);
  sadv_set_stage(BK7258_SARADC_VALIDATION_WAIT_RELEASE,
                 BK7258_SARADC_VALIDATION_STAGE_WAIT_RELEASE);
  ret = sadv_wait_transition(
          fd, baseline.median,
          g_bk7258_saradc_validation_diag.release_tolerance, false);
  if (ret < 0)
    {
      goto done;
    }

  usleep(g_sadv_config->settle_ms * 1000u);
  sadv_set_stage(BK7258_SARADC_VALIDATION_RUNNING,
                 BK7258_SARADC_VALIDATION_STAGE_RELEASED);
  ret = sadv_collect_phase(fd, &released);
  if (ret < 0)
    {
      goto done;
    }

  g_bk7258_saradc_validation_diag.released_min = released.minimum;
  g_bk7258_saradc_validation_diag.released_max = released.maximum;
  g_bk7258_saradc_validation_diag.released_median = released.median;
  if (!sadv_is_released(
        released.median, baseline.median,
        g_bk7258_saradc_validation_diag.release_tolerance))
    {
      ret = -ERANGE;
      goto done;
    }

  allowed_noise = sadv_max(
    1u, g_bk7258_saradc_validation_diag.observed_delta *
        g_sadv_config->maximum_noise_permille / SADV_PERMILLE);
  if (baseline.maximum - baseline.minimum > allowed_noise ||
      active.maximum - active.minimum > allowed_noise ||
      released.maximum - released.minimum > allowed_noise)
    {
      ret = -ERANGE;
      goto done;
    }

  sadv_set_stage(BK7258_SARADC_VALIDATION_RUNNING,
                 BK7258_SARADC_VALIDATION_STAGE_CLOSE);
  ret = sadv_close(fd);
  fd = -1;
  if (ret < 0)
    {
      goto done;
    }

  sadv_set_stage(BK7258_SARADC_VALIDATION_RUNNING,
                 BK7258_SARADC_VALIDATION_STAGE_REOPEN);
  fd = sadv_open();
  if (fd < 0)
    {
      ret = fd;
      fd = -1;
      goto done;
    }

  ret = sadv_reset_fifo(fd);
  if (ret == OK)
    {
      ret = sadv_sample(fd, &reopen_raw);
    }

  if (ret == OK)
    {
      g_bk7258_saradc_validation_diag.reopen_raw = reopen_raw;
      if (!sadv_is_released(
            reopen_raw, baseline.median,
            g_bk7258_saradc_validation_diag.release_tolerance))
        {
          ret = -ERANGE;
        }
    }

done:
  if (fd >= 0)
    {
      cleanup_ret = sadv_close(fd);
      if (ret == OK && cleanup_ret < 0)
        {
          ret = cleanup_ret;
        }
    }

  sadv_set_stage(BK7258_SARADC_VALIDATION_RUNNING,
                 BK7258_SARADC_VALIDATION_STAGE_DRIVER_DIAG);
  if (before_valid)
    {
      cleanup_ret = bk7258_saradc_get_diag(&after);
      if (cleanup_ret == OK)
        {
          cleanup_ret = sadv_publish_driver_diag(&before, &after);
        }

      if (ret == OK && cleanup_ret < 0)
        {
          ret = cleanup_ret;
        }
    }

  g_bk7258_saradc_validation_diag.result = ret;
  g_bk7258_saradc_validation_diag.stage =
    BK7258_SARADC_VALIDATION_STAGE_DONE;
  __asm volatile ("dmb sy" ::: "memory");
  g_bk7258_saradc_validation_diag.state =
    ret == OK ? BK7258_SARADC_VALIDATION_PASSED :
                BK7258_SARADC_VALIDATION_FAILED;
  sadv_publish();

  syslog(ret == OK ? LOG_INFO : LOG_ERR,
         "SADV %s binding=0x%08" PRIx32 " ret=%d transitions=%" PRIu32
         " raw=%" PRIu32 "/%" PRIu32 "/%" PRIu32
         " samples=%" PRIu32 "\n",
         ret == OK ? "PASS" : "FAIL", g_sadv_config->binding_id, ret,
         g_bk7258_saradc_validation_diag.transition_count,
         g_bk7258_saradc_validation_diag.baseline_median,
         g_bk7258_saradc_validation_diag.active_median,
         g_bk7258_saradc_validation_diag.released_median,
         g_bk7258_saradc_validation_diag.sample_count);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_saradc_validation_start(
  FAR const struct bk7258_saradc_validation_config_s *config)
{
  bool expected = false;
  int ret;

  ret = sadv_validate_config(config);
  if (ret < 0)
    {
      return ret;
    }

  if (!__atomic_compare_exchange_n(&g_sadv_started, &expected, true, false,
                                   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    {
      return OK;
    }

  memset((void *)&g_bk7258_saradc_validation_diag, 0,
         sizeof(g_bk7258_saradc_validation_diag));
  g_bk7258_saradc_validation_diag.magic =
    BK7258_SARADC_VALIDATION_MAGIC;
  g_bk7258_saradc_validation_diag.version =
    BK7258_SARADC_VALIDATION_VERSION;
  g_bk7258_saradc_validation_diag.size =
    sizeof(g_bk7258_saradc_validation_diag);
  g_bk7258_saradc_validation_diag.state =
    BK7258_SARADC_VALIDATION_RUNNING;
  g_bk7258_saradc_validation_diag.result = -EINPROGRESS;
  g_bk7258_saradc_validation_diag.binding_id = config->binding_id;
  g_bk7258_saradc_validation_diag.expected_channel =
    config->expected_channel;
  g_bk7258_saradc_validation_diag.active_direction =
    config->active_direction;
  g_bk7258_saradc_validation_diag.samples_per_phase =
    config->samples_per_phase;
  g_sadv_config = config;
  sadv_publish();

  ret = kthread_create("saradc-validate", SADV_PRIORITY, SADV_STACKSIZE,
                       sadv_thread, NULL);
  if (ret < 0)
    {
      g_bk7258_saradc_validation_diag.result = ret;
      g_bk7258_saradc_validation_diag.state =
        BK7258_SARADC_VALIDATION_FAILED;
      sadv_publish();
      __atomic_store_n(&g_sadv_started, false, __ATOMIC_RELEASE);
      return ret;
    }

  return OK;
}

#endif /* CONFIG_BK7258_SARADC_VALIDATION */
