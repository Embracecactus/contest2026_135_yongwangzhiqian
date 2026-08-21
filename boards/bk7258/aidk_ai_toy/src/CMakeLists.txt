# AIDK AI Toy physical-device bindings.

target_sources(board PRIVATE bk7258_board_bringup.c)

if(CONFIG_BK7258_AUD)
  target_sources(board PRIVATE bk7258_aidk_board_audio.c)
endif()

if(CONFIG_BK7258_AIDK_SD_NAND)
  target_sources(board PRIVATE bk7258_aidk_board_sdio.c)
endif()
