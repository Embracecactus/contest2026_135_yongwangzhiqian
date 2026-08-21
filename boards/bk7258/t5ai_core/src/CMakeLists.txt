# T5AI-Core physical-device bindings.

target_sources(board PRIVATE bk7258_board_bringup.c)

if(CONFIG_BK7258_AUD)
  target_sources(board PRIVATE bk7258_t5ai_core_audio.c)
endif()
