############################################################################
# BK7258 app - Makefile
#
# Support matrix: the maintained build backend for this application is the
# official CMake path ("bk7258.py build" -> official build --cmake).  This
# Makefile only keeps the NuttX Make backend able to compile the non-agent
# configurations (services, standalone test apps, the Dolphin recorder).
# The agent feature set (cloud, KWS, trigger, memory, OTA, preferences) is
# CMake-only: selecting CONFIG_BK7258_APP_AGENT under the Make backend
# fails below instead of silently producing an incomplete link.
############################################################################

include $(APPDIR)/Make.defs

ifeq ($(CONFIG_BK7258_PROVISION_GATT),y)
CSRCS += bk7258_provision_gatt.c bk7258_provision_tls.c bk7258_provision_claim.c
CSRCS += bk7258_provision_store.c bk7258_provision_pair.c bk7258_provision_owner.c
CSRCS += bk7258_provision_scan.c bk7258_provision_settings.c bk7258_provision_config.c bk7258_provision_storage.c
CSRCS += bk7258_provision_identity.c bk7258_provision_network.c bk7258_provision_time.c
CSRCS += bk7258_control_session.c bk7258_control_pair.c bk7258_control_ota_request.c
endif

ifneq ($(filter y,$(CONFIG_BK7258_VISION_SERVICE) $(CONFIG_BK7258_DISPLAY_SERVICE)),)
CSRCS += bk7258_media_volume.c
endif

ifeq ($(CONFIG_BK7258_AP_APPLICATION_LIFECYCLE),y)
CSRCS += bk7258_product_lifecycle.c
ifeq ($(CONFIG_DOLPHIN_UI),y)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)$(DELIM)system$(DELIM)dolphin
endif
endif

ifeq ($(CONFIG_BK7258_VISION_SERVICE),y)
	CSRCS += bk7258_vision_service.c bk7258_vision_core.c bk7258_vision_record.c
ifeq ($(CONFIG_BK7258_DISPLAY_SERVICE),y)
	CSRCS += bk7258_vision_feedback.c
endif
endif

ifeq ($(CONFIG_BK7258_HAPTIC_SERVICE),y)
CSRCS += bk7258_haptic_service.c
endif

ifeq ($(CONFIG_BK7258_APP_HAPTIC),y)
PROGNAME  += $(CONFIG_BK7258_APP_HAPTIC_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_HAPTIC_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_HAPTIC_STACKSIZE)
MAINSRC   += bk7258_haptic_main.c
CSRCS     += bk7258_haptic_client.c
endif

ifeq ($(CONFIG_BK7258_NFC_SERVICE),y)
ifeq ($(CONFIG_BK7258_PROVISION_GATT),y)
CSRCS += bk7258_nfc_bindings.c bk7258_nfc_control.c
endif
CFLAGS += ${INCDIR_PREFIX}$(TOPDIR)/../vendor/beken/nuttx/include
	CSRCS += bk7258_nfc_service.c bk7258_nfc_core.c
endif

ifeq ($(CONFIG_BK7258_HEALTH_SERVICE),y)
	CSRCS += bk7258_health_service.c bk7258_health_core.c
endif

ifeq ($(CONFIG_BK7258_MOTION_SERVICE),y)
	CSRCS += bk7258_motion_service.c bk7258_motion_core.c
endif

ifeq ($(CONFIG_BK7258_APP_NFC),y)
PROGNAME  += $(CONFIG_BK7258_APP_NFC_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_NFC_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_NFC_STACKSIZE)
MAINSRC   += bk7258_nfc_main.c
CSRCS     += bk7258_nfc_client.c bk7258_nfc_core.c
endif

ifeq ($(CONFIG_BK7258_APP_VISION),y)
PROGNAME  += $(CONFIG_BK7258_APP_VISION_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_VISION_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_VISION_STACKSIZE)
MAINSRC   += bk7258_vision_main.c
CSRCS     += bk7258_vision_client.c bk7258_vision_core.c
endif

ifeq ($(CONFIG_BK7258_APP_AGENT),y)
$(error app/bk7258: the agent feature set requires the CMake backend (official build --cmake); see docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md)
endif

ifeq ($(CONFIG_DOLPHIN_RECORDER),y)
ifeq ($(CONFIG_MEDIA),)
ifeq ($(CONFIG_BK7258_MIC),y)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)$(DELIM)frameworks$(DELIM)multimedia$(DELIM)media$(DELIM)include
CSRCS += bk7258_agent_media_recorder.c
endif
endif
endif

ifeq ($(CONFIG_BK7258_DISPLAY_SERVICE),y)
	CSRCS += bk7258_display_service.c bk7258_display_trial_control.c bk7258_display_store.c \
		bk7258_display_pack.c bk7258_display_rpc.c \
		bk7258_display_rpc_core.c
endif

ifeq ($(CONFIG_BK7258_PRODUCT_KEYS),y)
ifeq ($(CONFIG_BK7258_AP_CORE),y)
CSRCS     += bk7258_agent_keys.c
else
PROGNAME  += bkkeys
PRIORITY  += 100
STACKSIZE += 2048
MAINSRC   += bk7258_keys_main.c
CSRCS     += bk7258_voice_button.c
endif
endif

ifeq ($(CONFIG_BK7258_APP_DISPLAY),y)
PROGNAME  += $(CONFIG_BK7258_APP_DISPLAY_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_DISPLAY_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_DISPLAY_STACKSIZE)
MAINSRC   += bk7258_display_main.c
CSRCS     += bk7258_display_client.c
endif

ifeq ($(CONFIG_BK7258_APP_HEALTH),y)
PROGNAME  += $(CONFIG_BK7258_APP_HEALTH_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_HEALTH_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_HEALTH_STACKSIZE)
MAINSRC   += bk7258_health_main.c
CSRCS     += bk7258_health_client.c bk7258_health_core.c
endif

ifeq ($(CONFIG_BK7258_APP_MOTION),y)
PROGNAME  += $(CONFIG_BK7258_APP_MOTION_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_MOTION_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_MOTION_STACKSIZE)
MAINSRC   += bk7258_motion_main.c
CSRCS     += bk7258_motion_client.c bk7258_motion_core.c
endif

ifeq ($(CONFIG_BK7258_APP_OTA),y)
PROGNAME  += $(CONFIG_BK7258_APP_OTA_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_OTA_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_OTA_STACKSIZE)
MAINSRC   += bk7258_ota_main.c
endif

ifeq ($(CONFIG_BK7258_APP_USBMODE),y)
PROGNAME  += $(CONFIG_BK7258_APP_USBMODE_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_USBMODE_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_USBMODE_STACKSIZE)
MAINSRC   += bk7258_usbmode_main.c
endif

ifeq ($(CONFIG_BK7258_APP_RESET_ALIAS),y)
PROGNAME  += reset
PRIORITY  += $(CONFIG_BK7258_APP_RESET_ALIAS_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_RESET_ALIAS_STACKSIZE)
MAINSRC   += bk7258_reset_main.c
endif

ifeq ($(CONFIG_BK7258_APP_IRQ_TIMER_TEST),y)
PROGNAME  += $(CONFIG_BK7258_APP_IRQ_TIMER_TEST_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_IRQ_TIMER_TEST_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_IRQ_TIMER_TEST_STACKSIZE)
MAINSRC   += bk7258_sdk_irq_test_main.c
endif

ifeq ($(CONFIG_BK7258_APP_TIMER_SELFTEST),y)
PROGNAME  += $(CONFIG_BK7258_APP_TIMER_SELFTEST_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_TIMER_SELFTEST_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_TIMER_SELFTEST_STACKSIZE)
MAINSRC   += bk7258_sdk_timer_selftest_main.c
endif

ifeq ($(CONFIG_BK7258_APP_GPIO_TEST),y)
PROGNAME  += $(CONFIG_BK7258_APP_GPIO_TEST_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_GPIO_TEST_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_GPIO_TEST_STACKSIZE)
MAINSRC   += bk7258_gpio_foundation_test_main.c
endif

ifeq ($(CONFIG_BK7258_APP_GPIO_IRQ_TEST),y)
PROGNAME  += $(CONFIG_BK7258_APP_GPIO_IRQ_TEST_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_GPIO_IRQ_TEST_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_GPIO_IRQ_TEST_STACKSIZE)
MAINSRC   += bk7258_gpio_irq_test_main.c
endif

ifeq ($(CONFIG_BK7258_APP_APCTL),y)
PROGNAME  += $(CONFIG_BK7258_APP_APCTL_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_APCTL_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_APCTL_STACKSIZE)
MAINSRC   += bk7258_apctl_main.c
endif

ifeq ($(CONFIG_BK7258_APP_RPMSG_TEST),y)
PROGNAME  += $(CONFIG_BK7258_APP_RPMSG_TEST_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_RPMSG_TEST_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_RPMSG_TEST_STACKSIZE)
MAINSRC   += bk7258_rpmsg_test_main.c
endif

ifeq ($(CONFIG_BK7258_APP_RPMSGFS_TEST),y)
PROGNAME  += $(CONFIG_BK7258_APP_RPMSGFS_TEST_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_RPMSGFS_TEST_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_RPMSGFS_TEST_STACKSIZE)
MAINSRC   += bk7258_rpmsgfs_test_main.c
endif

ifeq ($(CONFIG_BK7258_APP_BT_IPC_TEST),y)
PROGNAME  += $(CONFIG_BK7258_APP_BT_IPC_TEST_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_BT_IPC_TEST_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_BT_IPC_TEST_STACKSIZE)
MAINSRC   += bk7258_bt_test_main.c
endif

ifeq ($(CONFIG_BK7258_APP_WIFI),y)
PROGNAME  += $(CONFIG_BK7258_APP_WIFI_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_WIFI_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_WIFI_STACKSIZE)
MAINSRC   += bk7258_wifi_main.c
endif

ifeq ($(CONFIG_BK7258_APP_IDENTITY),y)
PROGNAME  += $(CONFIG_BK7258_APP_IDENTITY_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_IDENTITY_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_IDENTITY_STACKSIZE)
MAINSRC   += bk7258_identity_main.c
endif

ifeq ($(CONFIG_BK7258_APP_PSRAM_TEST),y)
PROGNAME  += $(CONFIG_BK7258_APP_PSRAM_TEST_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_PSRAM_TEST_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_PSRAM_TEST_STACKSIZE)
MAINSRC   += bk7258_psram_test_main.c
endif

ifeq ($(CONFIG_BK7258_APP_BKVALIDATE),y)
PROGNAME  += $(CONFIG_BK7258_APP_BKVALIDATE_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_BKVALIDATE_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_BKVALIDATE_STACKSIZE)
MAINSRC   += bkvalidate_main.c
endif

ifeq ($(CONFIG_BK7258_APP_FACTORY),y)
PROGNAME += bkfactory
PRIORITY += 100
STACKSIZE += 4096
MAINSRC += bk7258_factory_main.c
endif

ifeq ($(CONFIG_BK7258_PROVISION_NATIVE),y)
CSRCS += bk7258_provision_generate.c bk7258_provision_bootstrap.c
BKPROV_QR_ROOT = $(APPDIR)/graphics/lvgl/lvgl
CFLAGS += ${INCDIR_PREFIX}$(BKPROV_QR_ROOT)/src/libs/qrcode
ifeq ($(CONFIG_GRAPHICS_LVGL),y)
ifneq ($(CONFIG_LV_USE_QRCODE),y)
$(error Native onboarding with LVGL requires LV_USE_QRCODE)
endif
else
CSRCS += $(BKPROV_QR_ROOT)/src/libs/qrcode/qrcodegen.c
CSRCS += $(BKPROV_QR_ROOT)/src/misc/lv_assert.c
# These flags affect this non-LVGL product archive, not the official LVGL tree.
CFLAGS += -DLV_CONF_SKIP -DLV_KCONFIG_IGNORE -DLV_USE_LOG=0
CFLAGS += '-DLV_ASSERT_HANDLER_INCLUDE=<stdlib.h>' '-DLV_ASSERT_HANDLER=abort();'
endif
endif

MODULE = y

include $(APPDIR)/Application.mk
