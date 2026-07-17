# NuttX board-level CMake for the Beken BK7258 (T5-AI) board.
#
# Stage N1 is validated with the classic Make backend.  This file mirrors
# the structure of contest_board/CMakeLists.txt so a later stage can switch
# to CMake without restructuring.

add_subdirectory(src)

# Pin the linker script for the CMake backend.
set_property(GLOBAL PROPERTY LD_SCRIPT "${NUTTX_BOARD_DIR}/scripts/ld.script")
