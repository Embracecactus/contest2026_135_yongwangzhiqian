/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __APP_BK7258_MEDIA_VOLUME_H
#define __APP_BK7258_MEDIA_VOLUME_H

enum bk7258_media_volume_owner_e
{
  BK7258_MEDIA_VOLUME_DISPLAY = 1,
  BK7258_MEDIA_VOLUME_VISION,
  BK7258_MEDIA_VOLUME_PREFERENCES,
  BK7258_MEDIA_VOLUME_OTA
};

/* Exclusive AP mount ownership, plus the USB MSC exclusion lease. Release
 * only after unmount; retain ownership when unmount or release fails.
 */
int bk7258_media_volume_acquire(enum bk7258_media_volume_owner_e owner);
int bk7258_media_volume_release(enum bk7258_media_volume_owner_e owner);

#if defined(CONFIG_BK7258_OTA_SOURCE_FILE) && defined(CONFIG_BK7258_USBMODE)
int bk7258_media_volume_ota_prepare(const char *root);
int bk7258_media_volume_ota_release(void);
#endif
#endif
