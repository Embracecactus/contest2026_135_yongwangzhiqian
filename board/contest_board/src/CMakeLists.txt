# NuttX CMake for RV1126B EVB board source files

set_property(GLOBAL PROPERTY LD_SCRIPT "${NUTTX_BOARD_DIR}/scripts/ld.script")

target_sources(board PRIVATE rv1126b_boot.c rv1126b_bringup.c)
