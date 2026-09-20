/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "bk7258_voice_media.h"
#include <sys/types.h>
#include <nuttx/drivers/ramdisk.h>
#include <nuttx/signal.h>
#include <errno.h>
#include <media_policy.h>
#include <media_utils.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <syslog.h>

#define BKVOICE_MEDIA_CAPTURE_LIFECYCLE "CaptureLifecycle"

/* Board ROMFS supplies physical routes. The product starts the official
 * daemon once, before any recorder/player client or wake listener. */
extern const unsigned char shaniu_media_img[];
extern const unsigned int shaniu_media_img_len;
extern int mediad_main(int argc, char *argv[]);

static int bkvoice_media_source_name(const char *source, char *name, size_t size)
{
  char filter[96];
  int ret = media_policy_get_string(source, filter, sizeof(filter));
  if (ret < 0) return ret;
  const char *separator = strchr(filter, '@');
  if (separator == NULL || separator[1] == '\0') return -EPROTO;
  if (strlcpy(name, separator + 1, size) >= size) return -E2BIG;
  return 0;
}

int bkvoice_media_source_stage_active(const char *source)
{
  char name[96];
  int ret = bkvoice_media_source_name(source, name, sizeof(name));
  if (ret < 0) return ret;

  /* Preserve the cold-boot contract while changing sources: publish the
   * desired policy state first, but do not start pcm0c until Recorder has
   * negotiated the real format. */
  ret = media_policy_set_string(BKVOICE_MEDIA_CAPTURE_LIFECYCLE, "Ready",
                                MEDIA_POLICY_NOT_APPLY);
  if (ret < 0) return ret;
  return media_policy_include("ActiveStreams", name, MEDIA_POLICY_NOT_APPLY);
}

int bkvoice_media_source_apply_active(void)
{
  /* The service applies every staged criterion after a successful mutation.
   * Re-setting Ready is the public apply boundary and is idempotent. */
  return media_policy_set_string(BKVOICE_MEDIA_CAPTURE_LIFECYCLE, "Ready",
                                 MEDIA_POLICY_APPLY);
}

int bkvoice_media_source_set_active(const char *source, bool active)
{
  char name[96];
  int ret;

  if (active)
    {
      ret = bkvoice_media_source_stage_active(source);
      return ret < 0 ? ret : bkvoice_media_source_apply_active();
    }

  ret = bkvoice_media_source_name(source, name, sizeof(name));
  if (ret < 0) return ret;
  return media_policy_exclude("ActiveStreams", name, MEDIA_POLICY_APPLY);
}

/* Official policy applies to the active Speaker graph as well as the next
 * player. Physical keys and the turn adapter share this mapping/readback. */
static int media_volume_update(bool apply, unsigned int requested, int steps,
                                unsigned int *volume)
{
  int minimum;
  int maximum;
  int index;
  int observed;
  int ret;

  if (volume == NULL || (apply && requested > 100u))
    {
      return -EINVAL;
    }

  ret = media_policy_get_range(MEDIA_STREAM_MUSIC MEDIA_POLICY_VOLUME,
                                &minimum, &maximum);
  if (ret < 0)
    {
      return ret;
    }

  if (minimum < 0 || maximum <= minimum)
    {
      return -ERANGE;
    }

  index = minimum + (int)(((uint64_t)requested *
                           (maximum - minimum) + 50u) / 100u);
  if (steps != 0)
    {
      /* Physical keys step through the official volume scale and share the
       * settings and read-back with the App.
       */
      ret = media_policy_get_stream_volume(MEDIA_STREAM_MUSIC, &index);
      if (ret < 0) return ret;
      if (index < minimum || index > maximum) return -EIO;
      index += steps;
      if (index < minimum) index = minimum;
      if (index > maximum) index = maximum;
    }
  if (apply)
    {
      ret = media_policy_set_stream_volume(MEDIA_STREAM_MUSIC, index);
      if (ret < 0)
        {
          return ret;
        }
    }

  ret = media_policy_get_stream_volume(MEDIA_STREAM_MUSIC, &observed);
  if (ret < 0)
    {
      return ret;
    }

  if (observed < minimum || observed > maximum ||
      (apply && observed != index))
    {
      return -EIO;
    }

  *volume = (unsigned int)(((uint64_t)(observed - minimum) * 100u +
                            (maximum - minimum) / 2u) / (maximum - minimum));
  return 0;
}

int bkvoice_media_volume(bool apply, unsigned int requested, unsigned int *volume)
{
  return media_volume_update(apply, requested, 0, volume);
}

int bkvoice_media_volume_step(int steps, unsigned int *volume)
{
  if (steps < -15 || steps > 15 || steps == 0) return -EINVAL;
  return media_volume_update(true, 0, steps, volume);
}

int bkvoice_media_start(void)
{
  static pid_t daemon;
  int volume;
  int ret;

  if (daemon > 0)
    return media_policy_get_stream_volume(MEDIA_STREAM_MUSIC, &volume);
  ret = romdisk_register(3, shaniu_media_img,
                         (shaniu_media_img_len + 63u) / 64u, 64);
  if (ret < 0) return ret;
  if (mkdir("/etc", 0755) < 0 && errno != EEXIST) return -errno;
  if (mkdir(CONFIG_MEDIA_SERVER_CONFIG_PATH, 0755) < 0 && errno != EEXIST)
    return -errno;
  if (mount("/dev/ram3", CONFIG_MEDIA_SERVER_CONFIG_PATH, "romfs",
            MS_RDONLY, NULL) < 0) return -errno;
  daemon = task_create(CONFIG_MEDIA_SERVER_PROGNAME,
                       CONFIG_MEDIA_SERVER_PRIORITY,
                       CONFIG_MEDIA_SERVER_STACKSIZE, mediad_main, NULL);
  if (daemon < 0) return -errno;

  for (unsigned int i = 0; i < 40; i++)
    {
      ret = media_policy_get_stream_volume(MEDIA_STREAM_MUSIC, &volume);
      if (ret >= 0)
        {
          syslog(LOG_NOTICE, "BKVOICE MEDIA ready volume=%d\n", volume);
          return 0;
        }
      nxsig_usleep(50000);
    }
  syslog(LOG_ERR, "BKVOICE MEDIA startup failed ret=%d\n", ret);
  return ret;
}
