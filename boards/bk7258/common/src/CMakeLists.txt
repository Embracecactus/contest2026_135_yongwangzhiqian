# Beken BK7258 shared board sources.

target_sources(board PRIVATE
    bk7258_appinit.c
    bk7258_boot.c
    bk7258_bringup.c
    bk7258_platform.c)

if(NOT CONFIG_BK7258_AP_CORE)
  target_sources(board PRIVATE
      bk7258_boot_slot.c
      bk7258_sdk_partition.c)
  if(CONFIG_BK7258_BT_IPC OR CONFIG_BK7258_WIFI_VNET)
    target_sources(board PRIVATE bk7258_mac_storage.c)
  endif()
  if(CONFIG_BK7258_FLASH_MTD)
    target_sources(board PRIVATE
        bk7258_flash_mtd.c
        bk7258_flash_guard.c)
  endif()
  if(CONFIG_BK7258_OTA)
    target_sources(board PRIVATE
        bk7258_ota_flash.c
        bk7258_ota_pair.c
        bk7258_ota_boot_control.c)
    if(NOT CONFIG_BK7258_FLASH_MTD)
      target_sources(board PRIVATE bk7258_flash_guard.c)
    endif()
  endif()
endif()

if(CONFIG_BK7258_LCD)
  target_sources(board PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../panels/ili9488_rgb.c)
  target_include_directories(board PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../panels)
endif()

if(CONFIG_BK7258_WIFI_VNET)
  target_sources(board PRIVATE bk7258_wifi_control.c)
  if(CONFIG_BK7258_AP_CORE)
    target_include_directories(board PRIVATE ${NUTTX_APPS_DIR}/include)
  endif()
endif()

target_include_directories(board PRIVATE
    ${NUTTX_CHIP_ABS_DIR}/common
    ${NUTTX_CHIP_ABS_DIR}/cp
    ${BK7258_SDK_INCLUDE_DIR}
    ${BK7258_SDK_CONFIG_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)

target_compile_definitions(board PRIVATE
    CONFIG_FREERTOS=0
    CONFIG_FREERTOS_SMP=0
    CONFIG_INT_WDT=0
    CONFIG_TASK_WDT=0
    CONFIG_INT_AON_WDT=0
    CONFIG_SYSTEM_CTRL=1
    CONFIG_NMI_WDT_EN=1
    CONFIG_DEBUG_VERSION=0
    CONFIG_GPIO_RETENTION_SUPPORT=0
    CONFIG_AON_RTC=0
    CONFIG_AON_RTC_64BIT=0
    CONFIG_SOC_BK7256XX=0
    CONFIG_SOC_BK7236XX=0
    CONFIG_SOC_BK7239XX=0)
