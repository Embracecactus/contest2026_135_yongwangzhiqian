# NuttX CMake for BK7258 (T5-AI) board source files.
# Keep the board source set shared by the Classic Make and CMake backends.

target_sources(board PRIVATE bk7258_bringup.c)

target_include_directories(board PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../chip/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../chip/cp)
