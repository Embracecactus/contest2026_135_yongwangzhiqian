# Beken BK7258 T5-Board.

if(NOT CONFIG_BK7258_BOARD_T5_BOARD)
  message(FATAL_ERROR "T5-Board directory requires BK7258_BOARD_T5_BOARD")
endif()

add_subdirectory(src)
