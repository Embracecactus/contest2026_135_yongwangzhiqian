# AIDK AI Toy physical-device bindings.

target_sources(board PRIVATE bk7258_board_bringup.c)

if(CONFIG_BK7258_AIDK_BUTTONS)
  target_sources(board PRIVATE bk7258_aidk_buttons.c)
endif()

if(CONFIG_BK7258_AIDK_MOTOR)
  target_sources(board PRIVATE bk7258_aidk_motor.c)
endif()

if(CONFIG_ETC_ROMFS)
  nuttx_add_romfs(
    NAME etc
    MOUNTPOINT etc
    RCSRCS
    etc/init.d/rc.sysinit
    etc/init.d/rcS
    PATH
    ${CMAKE_CURRENT_BINARY_DIR}/etc)
  target_link_libraries(board PRIVATE romfs_etc)
endif()

if(CONFIG_MEDIA AND CONFIG_MEDIA_SERVER)
  set(media_assets etc/media/graph.conf etc/media/criteria.txt etc/media/settings.pfw)
  if(CONFIG_BK7258_VOICE_KWS)
    set(wake_model "${NUTTX_APPS_DIR}/system/bk7258/models/nihao_openvela.tflite")
    file(SHA256 "${wake_model}" wake_model_sha256)
    string(REPLACE "\"" "" wake_model_expected_sha256
      "${CONFIG_BK7258_VOICE_KWS_MODEL_SHA256}")
    if(NOT wake_model_sha256 STREQUAL wake_model_expected_sha256)
      message(FATAL_ERROR
        "KWS model SHA256 does not match CONFIG_BK7258_VOICE_KWS_MODEL_SHA256; update the reviewed model and its configured hash together")
    endif()
    set(wake_model_staged
      "${CMAKE_CURRENT_BINARY_DIR}/etc/media/nihao_openvela.tflite")
    configure_file("${wake_model}" "${wake_model_staged}" COPYONLY)
  endif()
  # The private, operator-authorized acknowledgement PCM enters ROMFS only
  # when explicitly selected; it is not distributed with the public sources
  # and is not pulled into a shared image merely because a copy happens to
  # exist on this machine.
  set(wake_reply_staged "${CMAKE_CURRENT_BINARY_DIR}/etc/media/wake_reply.pcm")
  set(wake_reply "")
  if(CONFIG_BK7258_AIDK_WAKE_REPLY_PCM)
    set(wake_reply "${NUTTX_APPS_DIR}/system/bk7258/assets/wake_reply.pcm")
    if(NOT EXISTS "${wake_reply}")
      message(FATAL_ERROR
        "CONFIG_BK7258_AIDK_WAKE_REPLY_PCM is enabled but ${wake_reply} is "
        "missing; supply the operator-authorized asset locally or disable the option")
    endif()
    file(SHA256 "${wake_reply}" wake_reply_sha256)
    message(STATUS
      "AIDK wake reply PCM staged (private): wake_reply.pcm sha256=${wake_reply_sha256}")
    configure_file("${wake_reply}" "${wake_reply_staged}" COPYONLY)
  else()
    file(REMOVE "${wake_reply_staged}")
    message(STATUS
      "AIDK wake reply PCM not staged: CONFIG_BK7258_AIDK_WAKE_REPLY_PCM=n")
  endif()
  nuttx_add_romfs(
    NAME media
    PREFIX shaniu_media
    NONCONST
    RCRAWS
    ${media_assets}
    DEPENDS ${wake_model} ${wake_model_staged} ${wake_reply}
    PATH
    ${CMAKE_CURRENT_BINARY_DIR}/etc/media)
  target_link_libraries(board PRIVATE romfs_media)
endif()

if(CONFIG_BK7258_AUD)
  target_sources(board PRIVATE bk7258_aidk_board_audio.c)
endif()

if(CONFIG_BK7258_AIDK_SD_NAND)
  target_sources(board PRIVATE bk7258_aidk_board_sdio.c)
endif()

if(CONFIG_BK7258_AIDK_CAMERA)
  target_sources(board PRIVATE
      bk7258_aidk_camera.c
      bk7258_aidk_camera_glue.c)
endif()

if(CONFIG_BK7258_AIDK_SC7A20_PHASE0)
  target_sources(board PRIVATE bk7258_aidk_sc7a20_phase0.c)
endif()

if(CONFIG_BK7258_AIDK_SC7A20)
  target_sources(board PRIVATE bk7258_aidk_sc7a20.c)
endif()

if(CONFIG_BK7258_AIDK_MFRC522)
  target_sources(board PRIVATE bk7258_aidk_mfrc522.c)
endif()

if(CONFIG_BK7258_AIDK_BATTERY)
  target_sources(board PRIVATE bk7258_aidk_battery.c)
endif()

if(CONFIG_BK7258_AIDK_DUAL_LCD)
  target_sources(board PRIVATE bk7258_aidk_dual_lcd.c)
endif()
