# Beken BK7258 shared board sources.

target_sources(board PRIVATE
    bk7258_appinit.c
    bk7258_boot.c
    bk7258_bringup.c)

if(CONFIG_BK7258_AP_CORE)
  target_sources(board PRIVATE
      bk7258_ap_bringup.c
      bk7258_ap_entry.c)
endif()

if(CONFIG_EXAMPLES_AI_AGENT_VELA)
  target_sources(board PRIVATE bk7258_product.c)
  if(NOT CONFIG_MEDIA)
    if(CONFIG_BK7258_MIC OR CONFIG_BK7258_AUD)
      target_include_directories(board PRIVATE
          ${CMAKE_SOURCE_DIR}/../frameworks/multimedia/media/include)
      if(CONFIG_BK7258_MIC)
        target_sources(board PRIVATE bk7258_product_media_recorder.c)
        target_compile_definitions(board PRIVATE
            CONFIG_BK7258_AGENT_MEDIA_RECORDER=1)
      endif()
      if(CONFIG_BK7258_AUD)
        target_sources(board PRIVATE bk7258_product_media_player.c)
        target_compile_definitions(board PRIVATE
            CONFIG_BK7258_AGENT_MEDIA_PLAYER=1)
      endif()
    endif()
  endif()
endif()

if(CONFIG_BOARDCTL_FINALINIT)
  target_sources(board PRIVATE bk7258_finalinit.c)
endif()

if(NOT CONFIG_BK7258_AP_CORE)
  target_sources(board PRIVATE
      bk7258_cp_bringup.c
      bk7258_sdk_partition.c)
  if(CONFIG_BOARDCTL_RESET_CAUSE)
    target_sources(board PRIVATE bk7258_reset_cause.c)
  endif()
  if(CONFIG_BK7258_OTA OR CONFIG_BK7258_WDT_PRETIMEOUT_PANIC OR
     CONFIG_BK7258_FLASH_MTD OR
     CONFIG_BK7258_BT_IPC OR CONFIG_BK7258_WIFI_VNET)
    target_sources(board PRIVATE bk7258_storage_config.c)
  endif()
  if(CONFIG_BK7258_FLASH_MTD)
    target_sources(board PRIVATE bk7258_flash_mtd.c)
  endif()
  if(CONFIG_BK7258_OTA)
    if(CONFIG_BK7258_OTA_AUTO_CONFIRM)
      target_sources(board PRIVATE bk7258_ota_trial.c)
    endif()
  endif()
endif()

if(CONFIG_BK7258_LCD)
  target_sources(board PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../panels/ili9488_rgb.c)
  target_include_directories(board PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../panels)
endif()

target_include_directories(board PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_include_directories(board SYSTEM PRIVATE
    ${BK7258_SDK_INCLUDE_DIR}
    ${BK7258_SDK_CONFIG_DIR})
