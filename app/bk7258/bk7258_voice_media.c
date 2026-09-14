/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include "bk7258_voice_media.h"
#include <sys/types.h>
#include <nuttx/drivers/ramdisk.h>
#include <nuttx/signal.h>
#include <errno.h>
#include <media_policy.h>
#include <sched.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <syslog.h>

/* Board ROMFS supplies physical routes. The product starts the official
 * daemon once, before any recorder/player client or wake listener. */
extern const unsigned char shaniu_media_img[];
extern const unsigned int shaniu_media_img_len;
extern int mediad_main(int argc, char *argv[]);

int bkvoice_media_source_set_active(const char *source, bool active)
{
  char filter[96];
  int ret = media_policy_get_string(source, filter, sizeof(filter));
  if (ret < 0) return ret;
  const char *name = strchr(filter, '@');
  if (name == NULL || name[1] == '\0') return -EPROTO;
  return active ? media_policy_include("ActiveStreams", name + 1, MEDIA_POLICY_APPLY) :
                  media_policy_exclude("ActiveStreams", name + 1, MEDIA_POLICY_APPLY);
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
