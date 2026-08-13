# NuttX CMake for BK7258 (T5-AI) board source files.
# Keep the board source set shared by the Classic Make and CMake backends.

target_sources(board PRIVATE
    bk7258_appinit.c
    bk7258_boot.c
    bk7258_bringup.c
    bk7258_platform.c)

if(CONFIG_BK7258_AP_CORE)
  target_sources(board PRIVATE
      ${BK7258_BOARD_VARIANT_DIR}/src/bk7258_board_bringup.c)
endif()

target_include_directories(board PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../chip/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../chip/cp)
