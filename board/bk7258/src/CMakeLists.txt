# NuttX CMake for BK7258 logical-board source files.
# Keep the board source set shared by the Classic Make and CMake backends.

target_sources(board PRIVATE
    bk7258_appinit.c
    bk7258_boot.c
    bk7258_bringup.c
    bk7258_platform.c
    ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_board_bringup.c)

# The product Flash layout and its NuttX MTD composition belong to the
# logical board.  The chip/SDK layer supplies only the raw Flash mechanism.
# CP remains the sole Flash owner, so AP never links this policy wrapper.

if(NOT CONFIG_BK7258_AP_CORE)
  target_sources(board PRIVATE bk7258_sdk_partition.c)
  if(CONFIG_BK7258_BT_IPC OR CONFIG_BK7258_WIFI_VNET)
    target_sources(board PRIVATE bk7258_mac_storage.c)
  endif()
  if(CONFIG_BK7258_FLASH_MTD)
    target_sources(board PRIVATE
        bk7258_flash_mtd.c
        bk7258_flash_guard.c)
  endif()
endif()

# The immutable physical-board descriptor is linked into both roles.  AP
# lower halves consume MIC/SDIO/audio bindings, while CP GPIO consumers use
# the same selected board's LED/button binding.  Unavailable capabilities are
# rejected by Kconfig before their accessor can be referenced.

# Physical-device bindings are board sources.  The chip target contains only
# reusable BK7258 controller/lower-half code.

if(CONFIG_BK7258_AUD)
  if(CONFIG_BK7258_BOARD_T5_BOARD)
    target_sources(board PRIVATE
        ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_t5_board_audio.c)
  elseif(CONFIG_BK7258_BOARD_T5AI_CORE)
    target_sources(board PRIVATE
        ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_t5ai_core_audio.c)
  elseif(CONFIG_BK7258_BOARD_AIDK_AI_TOY)
    target_sources(board PRIVATE
        ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_aidk_board_audio.c)
  else()
    message(FATAL_ERROR
        "BK7258_AUD requires a physical-board audio binding")
  endif()
endif()

if(CONFIG_BK7258_AIDK_SD_NAND)
  target_sources(board PRIVATE
      ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_aidk_board_sdio.c)
endif()

if(CONFIG_BK7258_LCD)
  target_sources(board PRIVATE
      ${NUTTX_BOARD_DIR}/panels/ili9488_rgb.c
      ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_t5_board_lcd.c)
  target_include_directories(board PRIVATE ${NUTTX_BOARD_DIR}/panels)
endif()

if(CONFIG_BK7258_GT1151)
  target_sources(board PRIVATE
      ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_t5_board_gt1151.c)
endif()

if(CONFIG_BK7258_T5_BOARD_RGB_LCD_PWM_VALIDATION)
  target_sources(board PRIVATE
      ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_t5_board_rgb_lcd_backlight.c)
endif()

if(CONFIG_BK7258_T5_BOARD_CAMERA)
  target_sources(board PRIVATE
      ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_t5_board_camera.c
      ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_t5_board_camera_i2c.c)
endif()

if(CONFIG_BK7258_T5_BOARD_TF_SLOT)
  target_sources(board PRIVATE
      ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_t5_board_sdio.c)
  if(CONFIG_BK7258_T5_BOARD_TF_VALIDATION)
    target_sources(board PRIVATE
        ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_t5_board_tf_validation.c)
  endif()
endif()

target_include_directories(board PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../chip/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../chip/cp
    ${BK7258_SDK_INCLUDE_DIR}
    ${BK7258_SDK_CONFIG_DIR})

# Board bindings include the same immutable SDK headers as the chip lower
# halves.  NuttX exports arch include directories to the board target, but
# target-private compile definitions are not inherited, so mirror the SDK
# header policy explicitly after moving physical bindings into the board
# archive.

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
