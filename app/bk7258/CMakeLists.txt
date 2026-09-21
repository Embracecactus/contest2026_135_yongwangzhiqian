if(CONFIG_BK7258_PROVISION_GATT)
  # The pinned NuttX already carries the connection-observation interfaces;
  # use the original headers instead of widening a shared header.
  target_include_directories(apps PRIVATE ${NUTTX_DIR}/wireless/bluetooth)
  target_sources(apps PRIVATE bk7258_provision_gatt.c bk7258_provision_tls.c
                              bk7258_provision_claim.c bk7258_provision_store.c
                              bk7258_provision_pair.c bk7258_provision_scan.c bk7258_provision_settings.c bk7258_provision_config.c
                              bk7258_provision_storage.c bk7258_provision_owner.c
                              bk7258_provision_identity.c bk7258_provision_network.c
                              bk7258_provision_time.c bk7258_control_session.c
                              bk7258_control_pair.c bk7258_control_ota_request.c
                              bk7258_prov_service.c)
endif()

if(CONFIG_BK7258_AP_APPLICATION_LIFECYCLE)
  target_sources(apps PRIVATE bk7258_product_lifecycle.c)
  if(CONFIG_DOLPHIN_UI)
    target_include_directories(apps PRIVATE ${NUTTX_APPS_DIR}/system/dolphin)
  endif()
endif()

if(CONFIG_DOLPHIN_RECORDER AND CONFIG_BK7258_MIC AND NOT CONFIG_MEDIA)
  # Dolphin keeps its standalone NuttX audio recording adapter; the legacy
  # Shaniu voice path is not restored for it.
  target_include_directories(apps PRIVATE
      ${NUTTX_APPS_DIR}/frameworks/multimedia/media/include)
  target_sources(apps PRIVATE bk7258_agent_media_recorder.c)
endif()

if(CONFIG_BK7258_APP_AGENT)
  target_include_directories(apps PRIVATE ${NUTTX_APPS_DIR}/netutils/cjson/cJSON)
  include(${NUTTX_DIR}/../vendor/beken/frameworks/cmake/agent_framework.cmake)
  target_sources(apps PRIVATE bk7258_agent_product.c)
  if(CONFIG_BK7258_VISION_SERVICE)
    target_sources(apps PRIVATE bk7258_agent_vision.c)
  endif()
  if(CONFIG_MEDIA)
    target_sources(apps PRIVATE bk7258_voice_media.c)
  endif()
endif()

if(CONFIG_BK7258_APP_AGENT AND CONFIG_BK7258_VOICE_TLS)
  if(CONFIG_BK7258_PREFERENCES)
    target_sources(apps PRIVATE bk7258_agent_memory.c bk7258_agent_memory_codec.c)
  endif()
  if(CONFIG_BK7258_OTA_MANAGER AND CONFIG_BK7258_OTA_SOURCE_HTTP AND
     CONFIG_BK7258_VOICE_OTA_PERSISTENCE)
    target_sources(apps PRIVATE bk7258_agent_ota.c)
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/bk7258_agent_ota.c"
      TARGET_DIRECTORY apps APPEND PROPERTY COMPILE_OPTIONS "-fstack-usage"
      "-Werror=incompatible-pointer-types" "-Werror=int-conversion"
      "-Werror=implicit-function-declaration")
  endif()
  foreach(source bk7258_agent_product.c bk7258_agent_cloud.c bk7258_provision_identity.c
                 bk7258_agent_memory.c bk7258_agent_memory_codec.c)
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/${source}"
      TARGET_DIRECTORY apps APPEND PROPERTY COMPILE_OPTIONS "-fstack-usage")
  endforeach()
  target_sources(apps PRIVATE bk7258_voice_tls.c bk7258_voice_config.c
                              bk7258_cloud_config.c bk7258_cloud_request.c
                              bk7258_cloud_http.c bk7258_cloud_tts.c
                              bk7258_cloud_audio.c bk7258_agent_cloud.c)
endif()

if(CONFIG_BK7258_APP_AGENT)
  if(CONFIG_BK7258_PREFERENCES)
    target_sources(apps PRIVATE bk7258_preferences.c bk7258_preferences_storage.c)
    target_include_directories(apps PRIVATE
        ${NUTTX_APPS_DIR}/frameworks/system/utils/include
        ${NUTTX_APPS_DIR}/external/unqlite/unqlite)
  endif()
  if(CONFIG_BK7258_VOICE_VOLUME_PERSISTENCE)
    target_sources(apps PRIVATE bk7258_voice_volume_store.c)
  endif()
  if(CONFIG_BK7258_VOICE_OTA_PERSISTENCE)
    target_sources(apps PRIVATE bk7258_voice_ota_store.c
                                bk7258_voice_ota_flow.c)
  endif()
  if(CONFIG_BK7258_VOICE_KWS)
    include(${NUTTX_DIR}/../vendor/beken/frameworks/cmake/tflm.cmake)
    set(BKVOICE_TFLM ${NUTTX_APPS_DIR}/mlearning/tflite-micro/tflite-micro)
    set(BKVOICE_FRONTEND ${BKVOICE_TFLM}/tensorflow/lite/experimental/microfrontend/lib)
    set(BKVOICE_FRONTEND_SOURCES
        frontend.c frontend_util.c filterbank.c filterbank_util.c
        log_lut.c log_scale.c log_scale_util.c noise_reduction.c
        noise_reduction_util.c pcan_gain_control.c pcan_gain_control_util.c
        window.c window_util.c fft.cc fft_util.cc kiss_fft_int16.cc)
    list(TRANSFORM BKVOICE_FRONTEND_SOURCES PREPEND "${BKVOICE_FRONTEND}/")
    target_sources(apps PRIVATE bk7258_voice_kws_frontend.c
                                bk7258_voice_kws_model.cc
                                ${BKVOICE_FRONTEND_SOURCES})
    if(CONFIG_MEDIA_TRIGGER)
      target_sources(apps PRIVATE bk7258_voice_kws.c bk7258_agent_trigger.c
                                  bk7258_voice_wake_package.c)
    endif()
    target_include_directories(apps PRIVATE ${BKVOICE_TFLM}
        ${NUTTX_APPS_DIR}/math/kissfft/kissfft)
    # TfLiteTensor layout and kernel selection must match the runtime library.
    set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/bk7258_voice_kws_model.cc"
                 TARGET_DIRECTORY apps APPEND PROPERTY COMPILE_OPTIONS
                 "$<TARGET_PROPERTY:tflite_micro,COMPILE_OPTIONS>")
    target_link_libraries(apps PRIVATE tflite_micro)
  endif()
endif()

if(CONFIG_BK7258_DISPLAY_SERVICE)
  target_sources(apps PRIVATE bk7258_display_service.c
                              bk7258_display_store.c
                              bk7258_display_pack.c
                              bk7258_display_rpc.c
                              bk7258_display_rpc_core.c)
endif()

if(CONFIG_BK7258_HEALTH_SERVICE)
  target_sources(apps PRIVATE bk7258_health_service.c
                              bk7258_health_core.c)
endif()

if(CONFIG_BK7258_MOTION_SERVICE)
  target_sources(apps PRIVATE bk7258_motion_service.c
                              bk7258_motion_core.c)
endif()

if(CONFIG_BK7258_HAPTIC_SERVICE)
  target_sources(apps PRIVATE bk7258_haptic_service.c)
endif()

if(CONFIG_BK7258_APP_HAPTIC)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_HAPTIC_PROGNAME}
    SRCS bk7258_haptic_main.c bk7258_haptic_client.c
    STACKSIZE ${CONFIG_BK7258_APP_HAPTIC_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_HAPTIC_PRIORITY})
endif()

if(CONFIG_BK7258_NFC_SERVICE)
  target_include_directories(apps PRIVATE
      ${NUTTX_DIR}/../vendor/beken/nuttx/include)
  target_sources(apps PRIVATE bk7258_nfc_service.c bk7258_nfc_core.c)
endif()

if(CONFIG_BK7258_VISION_SERVICE)
  target_sources(apps PRIVATE bk7258_vision_service.c bk7258_vision_core.c
                              bk7258_vision_record.c)
  if(CONFIG_BK7258_DISPLAY_SERVICE)
    target_sources(apps PRIVATE bk7258_vision_feedback.c)
  endif()
endif()

if(CONFIG_BK7258_VISION_SERVICE OR CONFIG_BK7258_DISPLAY_SERVICE OR CONFIG_BK7258_PREFERENCES OR
   (CONFIG_BK7258_OTA_SOURCE_FILE AND CONFIG_BK7258_USBMODE))
  target_sources(apps PRIVATE bk7258_media_volume.c)
endif()

if(CONFIG_BK7258_PRODUCT_KEYS)
  if(CONFIG_BK7258_AP_CORE)
    target_sources(apps PRIVATE bk7258_agent_keys.c)
  else()
    nuttx_add_application(
      NAME bkkeys
      SRCS bk7258_keys_main.c bk7258_voice_button.c
      STACKSIZE 2048
      PRIORITY 100)
  endif()
endif()

if(CONFIG_BK7258_APP_DISPLAY)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_DISPLAY_PROGNAME}
    SRCS bk7258_display_main.c bk7258_display_client.c
    STACKSIZE ${CONFIG_BK7258_APP_DISPLAY_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_DISPLAY_PRIORITY})
endif()

if(CONFIG_BK7258_APP_HEALTH)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_HEALTH_PROGNAME}
    SRCS bk7258_health_main.c bk7258_health_client.c bk7258_health_core.c
    STACKSIZE ${CONFIG_BK7258_APP_HEALTH_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_HEALTH_PRIORITY})
endif()

if(CONFIG_BK7258_APP_MOTION)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_MOTION_PROGNAME}
    SRCS bk7258_motion_main.c bk7258_motion_client.c bk7258_motion_core.c
    STACKSIZE ${CONFIG_BK7258_APP_MOTION_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_MOTION_PRIORITY})
endif()

if(CONFIG_BK7258_APP_NFC)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_NFC_PROGNAME}
    SRCS bk7258_nfc_main.c bk7258_nfc_client.c bk7258_nfc_core.c
    STACKSIZE ${CONFIG_BK7258_APP_NFC_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_NFC_PRIORITY})
endif()

if(CONFIG_BK7258_APP_VISION)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_VISION_PROGNAME}
    SRCS bk7258_vision_main.c bk7258_vision_client.c bk7258_vision_core.c
    STACKSIZE ${CONFIG_BK7258_APP_VISION_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_VISION_PRIORITY})
endif()

if(CONFIG_BK7258_APP_OTA)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_OTA_PROGNAME}
    SRCS bk7258_ota_main.c
    STACKSIZE ${CONFIG_BK7258_APP_OTA_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_OTA_PRIORITY})
endif()

if(CONFIG_BK7258_APP_USBMODE)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_USBMODE_PROGNAME}
    SRCS bk7258_usbmode_main.c
    STACKSIZE ${CONFIG_BK7258_APP_USBMODE_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_USBMODE_PRIORITY})
endif()

if(CONFIG_BK7258_APP_RESET_ALIAS)
  nuttx_add_application(
    NAME reset
    SRCS bk7258_reset_main.c
    STACKSIZE ${CONFIG_BK7258_APP_RESET_ALIAS_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_RESET_ALIAS_PRIORITY})
endif()

if(CONFIG_BK7258_APP_IRQ_TIMER_TEST)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_IRQ_TIMER_TEST_PROGNAME}
    SRCS bk7258_sdk_irq_test_main.c
    STACKSIZE ${CONFIG_BK7258_APP_IRQ_TIMER_TEST_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_IRQ_TIMER_TEST_PRIORITY})
endif()

if(CONFIG_BK7258_APP_TIMER_SELFTEST)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_TIMER_SELFTEST_PROGNAME}
    SRCS bk7258_sdk_timer_selftest_main.c
    STACKSIZE ${CONFIG_BK7258_APP_TIMER_SELFTEST_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_TIMER_SELFTEST_PRIORITY})
endif()

if(CONFIG_BK7258_APP_GPIO_TEST)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_GPIO_TEST_PROGNAME}
    SRCS bk7258_gpio_foundation_test_main.c
    STACKSIZE ${CONFIG_BK7258_APP_GPIO_TEST_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_GPIO_TEST_PRIORITY})
endif()

if(CONFIG_BK7258_APP_GPIO_IRQ_TEST)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_GPIO_IRQ_TEST_PROGNAME}
    SRCS bk7258_gpio_irq_test_main.c
    STACKSIZE ${CONFIG_BK7258_APP_GPIO_IRQ_TEST_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_GPIO_IRQ_TEST_PRIORITY})
endif()

if(CONFIG_BK7258_APP_APCTL)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_APCTL_PROGNAME}
    SRCS bk7258_apctl_main.c
    STACKSIZE ${CONFIG_BK7258_APP_APCTL_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_APCTL_PRIORITY})
endif()

if(CONFIG_BK7258_APP_RPMSG_TEST)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_RPMSG_TEST_PROGNAME}
    SRCS bk7258_rpmsg_test_main.c
    STACKSIZE ${CONFIG_BK7258_APP_RPMSG_TEST_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_RPMSG_TEST_PRIORITY})
endif()

if(CONFIG_BK7258_APP_RPMSGFS_TEST)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_RPMSGFS_TEST_PROGNAME}
    SRCS bk7258_rpmsgfs_test_main.c
    STACKSIZE ${CONFIG_BK7258_APP_RPMSGFS_TEST_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_RPMSGFS_TEST_PRIORITY})
endif()

if(CONFIG_BK7258_APP_BT_IPC_TEST)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_BT_IPC_TEST_PROGNAME}
    SRCS bk7258_bt_test_main.c
    STACKSIZE ${CONFIG_BK7258_APP_BT_IPC_TEST_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_BT_IPC_TEST_PRIORITY})
endif()

if(CONFIG_BK7258_APP_PSRAM_TEST)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_PSRAM_TEST_PROGNAME}
    SRCS bk7258_psram_test_main.c
    STACKSIZE ${CONFIG_BK7258_APP_PSRAM_TEST_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_PSRAM_TEST_PRIORITY})
endif()

if(CONFIG_BK7258_APP_BKVALIDATE)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_BKVALIDATE_PROGNAME}
    SRCS bkvalidate_main.c
    STACKSIZE ${CONFIG_BK7258_APP_BKVALIDATE_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_BKVALIDATE_PRIORITY})
endif()

if(CONFIG_BK7258_APP_WIFI)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_WIFI_PROGNAME}
    SRCS bk7258_wifi_main.c
    STACKSIZE ${CONFIG_BK7258_APP_WIFI_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_WIFI_PRIORITY})
endif()

if(CONFIG_BK7258_APP_IDENTITY)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_IDENTITY_PROGNAME}
    SRCS bk7258_identity_main.c
    STACKSIZE ${CONFIG_BK7258_APP_IDENTITY_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_IDENTITY_PRIORITY})
endif()

if(CONFIG_BK7258_APP_PROV)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_PROV_PROGNAME}
    SRCS bk7258_prov_main.c bk7258_prov_client.c
    STACKSIZE ${CONFIG_BK7258_APP_PROV_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_PROV_PRIORITY})
endif()

if(CONFIG_BK7258_APP_DATA)
  nuttx_add_application(
    NAME ${CONFIG_BK7258_APP_DATA_PROGNAME}
    SRCS bk7258_data_main.c
    STACKSIZE ${CONFIG_BK7258_APP_DATA_STACKSIZE}
    PRIORITY ${CONFIG_BK7258_APP_DATA_PRIORITY})
endif()

if(CONFIG_BK7258_APP_FACTORY)
  nuttx_add_application(NAME bkfactory SRCS bk7258_factory_main.c
                        STACKSIZE 4096 PRIORITY 100)
endif()

if(CONFIG_BK7258_PROVISION_NATIVE)
  target_sources(apps PRIVATE bk7258_provision_generate.c bk7258_provision_bootstrap.c)
  set(BKPROV_QR_ROOT ${NUTTX_APPS_DIR}/graphics/lvgl/lvgl)
  target_include_directories(apps PRIVATE ${BKPROV_QR_ROOT}/src/libs/qrcode)
  # Consume the manifest-pinned MIT Nayuki implementation already in LVGL.
  # AIDK has no LVGL runtime: compile only the pure encoder and assertion handler.
  if(CONFIG_GRAPHICS_LVGL)
    if(NOT CONFIG_LV_USE_QRCODE)
      message(FATAL_ERROR "Native onboarding with LVGL requires LV_USE_QRCODE")
    endif()
  else()
    target_sources(apps PRIVATE ${BKPROV_QR_ROOT}/src/libs/qrcode/qrcodegen.c
                                ${BKPROV_QR_ROOT}/src/misc/lv_assert.c)
    set_property(SOURCE ${BKPROV_QR_ROOT}/src/libs/qrcode/qrcodegen.c
                        ${BKPROV_QR_ROOT}/src/misc/lv_assert.c
      TARGET_DIRECTORY apps APPEND PROPERTY COMPILE_DEFINITIONS
      LV_CONF_SKIP LV_KCONFIG_IGNORE LV_USE_LOG=0
      "LV_ASSERT_HANDLER_INCLUDE=<stdlib.h>" "LV_ASSERT_HANDLER=abort()\;")
  endif()
endif()
