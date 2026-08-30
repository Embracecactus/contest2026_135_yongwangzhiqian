# AIDK AI Toy physical-device bindings.

target_sources(board PRIVATE bk7258_board_bringup.c)

if(CONFIG_ETC_ROMFS)
  nuttx_add_romfs(
    NAME etc
    MOUNTPOINT etc
    RCSRCS
    etc/init.d/rc.sysinit
    etc/init.d/rcS
    PATH
    ${CMAKE_CURRENT_BINARY_DIR}/etc)
  target_link_libraries(board PRIVATE romfs_etc)
endif()

if(CONFIG_BK7258_AUD)
  target_sources(board PRIVATE bk7258_aidk_board_audio.c)
endif()

if(CONFIG_BK7258_AIDK_SD_NAND)
  target_sources(board PRIVATE bk7258_aidk_board_sdio.c)
endif()

if(CONFIG_BK7258_AIDK_CAMERA_PHASE0)
  target_sources(board PRIVATE bk7258_aidk_camera_phase0.c)
endif()

if(CONFIG_BK7258_AIDK_CAMERA)
  target_sources(board PRIVATE
      bk7258_aidk_camera.c
      bk7258_aidk_camera_glue.c)
endif()

if(CONFIG_BK7258_AIDK_SC7A20_PHASE0)
  target_sources(board PRIVATE bk7258_aidk_sc7a20_phase0.c)
endif()

if(CONFIG_BK7258_AIDK_SC7A20)
  target_sources(board PRIVATE bk7258_aidk_sc7a20.c)
endif()

if(CONFIG_BK7258_AIDK_MFRC522)
  target_sources(board PRIVATE bk7258_aidk_mfrc522.c)
endif()

if(CONFIG_BK7258_AIDK_BATTERY)
  target_sources(board PRIVATE bk7258_aidk_battery.c)
endif()

if(CONFIG_BK7258_AIDK_DUAL_LCD)
  target_sources(board PRIVATE bk7258_aidk_dual_lcd.c)
endif()
