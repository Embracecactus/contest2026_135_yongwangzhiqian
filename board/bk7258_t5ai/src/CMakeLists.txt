# NuttX CMake for BK7258 (T5-AI) board source files.
# Stage N1 is validated with classic Make; this file mirrors the source set
# for forward compatibility with the CMake backend.

target_sources(board PRIVATE bk7258_bringup.c)

target_include_directories(board PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../chip/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../chip/cp)
