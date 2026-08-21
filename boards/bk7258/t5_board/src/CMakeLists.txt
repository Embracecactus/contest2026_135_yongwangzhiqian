# T5-Board physical-device bindings.

target_sources(board PRIVATE bk7258_board_bringup.c)

if(CONFIG_BK7258_AUD)
  target_sources(board PRIVATE bk7258_t5_board_audio.c)
endif()

if(CONFIG_BK7258_LCD)
  target_sources(board PRIVATE bk7258_t5_board_lcd.c)
endif()

if(CONFIG_BK7258_GT1151)
  target_sources(board PRIVATE bk7258_t5_board_gt1151.c)
endif()

if(CONFIG_BK7258_T5_BOARD_RGB_LCD_PWM_VALIDATION)
  target_sources(board PRIVATE bk7258_t5_board_rgb_lcd_backlight.c)
endif()

if(CONFIG_BK7258_T5_BOARD_CAMERA)
  target_sources(board PRIVATE
      bk7258_t5_board_camera.c
      bk7258_t5_board_camera_i2c.c)
endif()

if(CONFIG_BK7258_T5_BOARD_TF_SLOT)
  target_sources(board PRIVATE bk7258_t5_board_sdio.c)
  if(CONFIG_BK7258_T5_BOARD_TF_VALIDATION)
    target_sources(board PRIVATE bk7258_t5_board_tf_validation.c)
  endif()
endif()
