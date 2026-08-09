# NuttX board-level CMake for the Beken BK7258 T5-AI board family.
#
# Stage N1 is validated with the classic Make backend.  This file mirrors
# the structure of contest_board/CMakeLists.txt so a later stage can switch
# to CMake without restructuring.

if(CONFIG_BK7258_BOARD_T5_BOARD)
  set(BK7258_BOARD_VARIANT_DIR
      "${NUTTX_BOARD_DIR}/boards/t5_board")
else()
  set(BK7258_BOARD_VARIANT_DIR
      "${NUTTX_BOARD_DIR}/boards/t5ai_core")
endif()

# board.h is consumed by board and architecture compilation units.  Keep the
# selected carrier header on both include paths; the classic Make backend
# mirrors this in scripts/Make.defs.

target_include_directories(board PUBLIC
  "${BK7258_BOARD_VARIANT_DIR}/include")
target_include_directories(arch PUBLIC
  "${BK7258_BOARD_VARIANT_DIR}/include")

add_subdirectory(src)

# Pin the role-specific linker script for the CMake backend.
if(CONFIG_BK7258_AP_CORE)
  set_property(GLOBAL PROPERTY LD_SCRIPT
               "${NUTTX_BOARD_DIR}/scripts/ld_ap.script")
else()
  set_property(GLOBAL PROPERTY LD_SCRIPT
               "${NUTTX_BOARD_DIR}/scripts/ld.script")
endif()
