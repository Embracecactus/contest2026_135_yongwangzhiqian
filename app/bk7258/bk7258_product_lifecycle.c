/****************************************************************************
 * app/bk7258/bk7258_product_lifecycle.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Single product-owned implementation of the chip AP lifecycle extension.
 * Individual applications contribute named prepare/start functions so that
 * adding a second AP product service cannot create duplicate chip callbacks.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_AP_APPLICATION_LIFECYCLE

#include <errno.h>
#include <syslog.h>

#include "bk7258_product_lifecycle.h"

#if defined(CONFIG_MEDIA) && defined(CONFIG_BK7258_VOICE_SERVICE)
#include "bk7258_voice_media.h"
#endif

#if defined(CONFIG_BK7258_OTA_SOURCE_FILE) && defined(CONFIG_BK7258_USBMODE)
#include "bk7258_media_volume.h"
#include <arch/chip/bk7258_ota_source_file.h>
#endif

#ifdef CONFIG_DOLPHIN_UI
#include "dolphin_ui.h"
#endif

#ifdef CONFIG_BK7258_PROVISION_GATT
#include "bk7258_provision_gatt.h"
#include "bk7258_provision_storage.h"
#include "bk7258_provision_time.h"
#endif

#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
#include "bk7258_voice_volume_store.h"
#endif

#ifdef CONFIG_BK7258_VOICE_OTA_PERSISTENCE
#include "bk7258_voice_ota_store.h"
#endif

#ifdef CONFIG_BK7258_HAPTIC_SERVICE
#include "bk7258_haptic_service.h"
#endif

int bk7258_ap_application_prepare(void)
{
  int ret = 0;

#if defined(CONFIG_BK7258_OTA_SOURCE_FILE) && defined(CONFIG_BK7258_USBMODE)
  ret = bk7258_ota_file_source_register(bk7258_media_volume_ota_prepare,
                                        bk7258_media_volume_ota_release);
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_APP_AGENT
  ret = bk7258_agent_product_prepare();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_NFC_SERVICE
  ret = bk7258_nfc_service_prepare();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_VISION_SERVICE
  ret = bk7258_vision_service_prepare();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_VOICE_SERVICE
  ret = bk7258_voice_service_prepare();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  ret = bk7258_display_service_prepare();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_HEALTH_SERVICE
  ret = bk7258_health_service_prepare();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_MOTION_SERVICE
  ret = bk7258_motion_service_prepare();
  if (ret < 0)
    {
      return ret;
    }
#endif

  return ret;
}

int bk7258_ap_application_start(void)
{
  int ret;

#if defined(CONFIG_MEDIA) && defined(CONFIG_BK7258_VOICE_SERVICE)
  ret = bkvoice_media_start();
  if (ret < 0)
    {
      /* Device management and signed recovery must remain reachable when an
       * audio route cannot start. Recorder/player calls report unavailable. */
      syslog(LOG_ERR, "BKVOICE audio unavailable ret=%d\n", ret);
    }
#endif

#ifdef CONFIG_DOLPHIN_UI
  ret = dolphin_ui_start();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_PROVISION_GATT
  /* HCI Host startup has completed before this product callback. Register
   * the sole product table before any service can open a claim window.
   * Registration alone enables neither advertising nor configuration.
   */
  ret = bkprov_time_start();
  if (ret < 0) return ret;
  ret = bkprov_gatt_register();
  if (ret < 0)
    {
      return ret;
    }
#ifdef CONFIG_BK7258_RPMSGFS
  /* The filesystem worker may await CP mounting /data. Product startup must
   * not wait for that I/O. Missing/corrupt stores keep claiming unavailable. */
  ret = bkprov_storage_start("/cpdata/shaniu");
  if (ret < 0) return ret;
#endif
#endif

#ifdef CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE
  /* Configuration is memory-only here; the first query or mutation lazily
   * reconciles CP LittleFS after RPMsgFS is ready.
   */
  ret = bkvoice_volume_store_start(BKVOICE_VOLUME_STORE_ROOT);
  if (ret < 0) return ret;
#endif

#ifdef CONFIG_BK7258_VOICE_OTA_PERSISTENCE
  /* The voice owner loads and reconciles the record lazily after RPMsgFS is
   * ready. Registration itself performs no filesystem operation.
   */
  ret = bkvoice_ota_store_start(BKVOICE_OTA_STORE_ROOT);
  if (ret < 0) return ret;
#endif

#ifdef CONFIG_TESTS_BK7258_RPMSG_EXEC
  ret = bk7258_drivercheck_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_HAPTIC_SERVICE
  ret = bkhaptic_service_initialize();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_NFC_SERVICE
  ret = bk7258_nfc_service_start();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_VISION_SERVICE
  ret = bk7258_vision_service_start();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_HEALTH_SERVICE
  ret = bk7258_health_service_start();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_MOTION_SERVICE
  ret = bk7258_motion_service_start();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_DISPLAY_SERVICE
  ret = bk7258_display_service_start();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_VOICE_SERVICE
  ret = bk7258_voice_service_start();
  if (ret < 0)
    {
      return ret;
    }
#endif

#ifdef CONFIG_BK7258_APP_AGENT
  ret = bk7258_agent_product_start();
  if (ret < 0)
    {
      return ret;
    }
#endif

  return 0;
}

#endif /* CONFIG_BK7258_AP_APPLICATION_LIFECYCLE */
