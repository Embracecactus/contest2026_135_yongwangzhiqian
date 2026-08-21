# Beken BK7258 T5AI-Core board.

if(NOT CONFIG_BK7258_BOARD_T5AI_CORE)
  message(FATAL_ERROR "T5AI-Core directory requires BK7258_BOARD_T5AI_CORE")
endif()

add_subdirectory(src)
