# Beken BK7258 AIDK AI Toy board.

if(NOT CONFIG_BK7258_BOARD_AIDK_AI_TOY)
  message(FATAL_ERROR "AIDK AI Toy directory requires BK7258_BOARD_AIDK_AI_TOY")
endif()

add_subdirectory(src)
