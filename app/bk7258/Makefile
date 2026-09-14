############################################################################
# BK7258 maintainer commands - Makefile
############################################################################

include $(APPDIR)/Make.defs

ifeq ($(CONFIG_BK7258_PROVISION_GATT),y)
CSRCS += bk7258_provision_gatt.c bk7258_provision_tls.c bk7258_provision_claim.c
CSRCS += bk7258_provision_store.c bk7258_provision_pair.c bk7258_provision_owner.c
CSRCS += bk7258_provision_settings.c bk7258_provision_storage.c bk7258_provision_identity.c
CSRCS += bk7258_provision_network.c bk7258_provision_time.c bk7258_control_session.c
CSRCS += bk7258_control_pair.c bk7258_control_ota_request.c bk7258_cloud_memory.c
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
CSRCS += bk7258_agent_product.c
endif

ifneq ($(filter y,$(CONFIG_BK7258_APP_AGENT) $(CONFIG_BK7258_VOICE_SERVICE) $(CONFIG_DOLPHIN_RECORDER)),)
ifeq ($(CONFIG_MEDIA),)
ifeq ($(CONFIG_BK7258_MIC),y)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)$(DELIM)frameworks$(DELIM)multimedia$(DELIM)media$(DELIM)include
CSRCS += bk7258_agent_media_recorder.c
endif
endif
endif

ifneq ($(filter y,$(CONFIG_BK7258_APP_AGENT) $(CONFIG_BK7258_VOICE_SERVICE)),)
ifeq ($(CONFIG_MEDIA),)
ifeq ($(CONFIG_BK7258_AUD),y)
CFLAGS += ${INCDIR_PREFIX}$(APPDIR)$(DELIM)frameworks$(DELIM)multimedia$(DELIM)media$(DELIM)include
CSRCS += bk7258_agent_media_player.c
endif
endif
endif

ifeq ($(CONFIG_BK7258_VOICE_SERVICE),y)
	CSRCS += bk7258_voice_service.c bk7258_voice_pack.c \
		bk7258_voice_companion.c bk7258_voice_capture.c \
		bk7258_voice_ptt.c bk7258_voice_transport.c \
		bk7258_voice_turn.c \
		bk7258_voice_turn_audio.c
ifeq ($(CONFIG_BK7258_VOICE_LEGACY_GATEWAY),y)
	CSRCS += bk7258_voice_wss.c bk7258_voice_gateway.c \
		bk7258_voice_session.c
endif
ifeq ($(CONFIG_BK7258_VOICE_TLS),y)
	CSRCS += bk7258_voice_tls.c bk7258_voice_config.c bk7258_voice_runtime.c
	CSRCS += bk7258_cloud_config.c bk7258_cloud_request.c bk7258_cloud_http.c bk7258_cloud_client.c bk7258_cloud_history.c bk7258_cloud_tts.c bk7258_cloud_playback.c bk7258_cloud_runtime.c
endif
ifeq ($(CONFIG_BK7258_PREFERENCES),y)
$(error BK7258_PREFERENCES requires the maintained CMake build and KVDB patches)
endif
ifeq ($(CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE),y)
	CSRCS += bk7258_voice_volume_store.c
endif
ifeq ($(CONFIG_BK7258_VOICE_OTA_PERSISTENCE),y)
	CSRCS += bk7258_voice_ota_store.c bk7258_voice_ota_flow.c
endif
ifeq ($(CONFIG_BK7258_VOICE_KWS),y)
BKVOICE_TFLM := $(APPDIR)/mlearning/tflite-micro/tflite-micro
BKVOICE_FRONTEND := $(BKVOICE_TFLM)/tensorflow/lite/experimental/microfrontend/lib
CSRCS += bk7258_voice_kws.c bk7258_voice_kws_frontend.c \
	bk7258_voice_wake_window.c
ifeq ($(CONFIG_BK7258_VOICE_WAKE_RUNTIME),y)
$(error BK7258_VOICE_WAKE_RUNTIME requires the maintained CMake build and Media Trigger integration)
endif
CSRCS += $(addprefix $(BKVOICE_FRONTEND)/,frontend.c frontend_util.c \
  filterbank.c filterbank_util.c log_lut.c log_scale.c log_scale_util.c \
  noise_reduction.c noise_reduction_util.c pcan_gain_control.c \
  pcan_gain_control_util.c window.c window_util.c)
CXXEXT := .cc
CXXSRCS += bk7258_voice_kws_model.cc
CXXSRCS += $(addprefix $(BKVOICE_FRONTEND)/,fft.cc fft_util.cc kiss_fft_int16.cc)
CFLAGS += ${INCDIR_PREFIX}$(BKVOICE_TFLM)
CXXFLAGS += ${INCDIR_PREFIX}$(BKVOICE_TFLM)
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/kissfft/kissfft
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/system/flatbuffers/flatbuffers/include
CXXFLAGS += ${INCDIR_PREFIX}$(APPDIR)/math/gemmlowp/gemmlowp
CXXFLAGS += -DTFLITE_EMULATE_FLOAT -DTF_LITE_DISABLE_X86_NEON
ifeq ($(CONFIG_MLEARNING_CMSIS_NN),y)
CXXFLAGS += -DCMSIS_NN
endif
endif
endif

ifeq ($(CONFIG_BK7258_DISPLAY_SERVICE),y)
	CSRCS += bk7258_display_service.c bk7258_display_store.c \
		bk7258_display_pack.c bk7258_display_rpc.c \
		bk7258_display_rpc_core.c
endif

ifeq ($(CONFIG_BK7258_VOICE_SERVICE),y)
ifeq ($(CONFIG_BK7258_DISPLAY_SERVICE),y)
	CSRCS += bk7258_voice_feedback.c
endif
endif

ifneq ($(filter y,$(CONFIG_BK7258_VOICE_PTT_BUTTON) $(CONFIG_BK7258_PRODUCT_KEYS)),)
CSRCS     += bk7258_voice_button.c
endif

ifeq ($(CONFIG_BK7258_APP_VOICE),y)
PROGNAME  += $(CONFIG_BK7258_APP_VOICE_PROGNAME)
PRIORITY  += $(CONFIG_BK7258_APP_VOICE_PRIORITY)
STACKSIZE += $(CONFIG_BK7258_APP_VOICE_STACKSIZE)
MAINSRC   += bk7258_voice_main.c
CSRCS     += bk7258_voice_client.c
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

MODULE = y

include $(APPDIR)/Application.mk
