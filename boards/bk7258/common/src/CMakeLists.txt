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

if(CONFIG_BOARDCTL_FINALINIT)
  target_sources(board PRIVATE bk7258_finalinit.c)
endif()

if(NOT CONFIG_BK7258_AP_CORE)
  target_sources(board PRIVATE
      bk7258_cp_bringup.c)
  if(CONFIG_BOARDCTL_RESET_CAUSE)
    target_sources(board PRIVATE bk7258_reset_cause.c)
  endif()
  if(CONFIG_BK7258_OTA OR CONFIG_BK7258_WDT_PRETIMEOUT_PANIC OR
     CONFIG_BK7258_FLASH_MTD OR
     CONFIG_BK7258_BT_IPC OR CONFIG_BK7258_WIFI_VNET)
    target_sources(board PRIVATE bk7258_storage_config.c)
  endif()
  if(CONFIG_BK7258_OTA)
    if(CONFIG_BK7258_OTA_AUTO_CONFIRM)
      target_sources(board PRIVATE bk7258_ota_trial.c)
    endif()
  endif()
endif()

target_include_directories(board PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
