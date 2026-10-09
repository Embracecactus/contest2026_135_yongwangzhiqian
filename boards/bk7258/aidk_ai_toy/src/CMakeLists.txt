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
  set(wake_reply_staged "${CMAKE_CURRENT_BINARY_DIR}/etc/media/wake_reply.pcm")
  set(wake_reply "")
  if(CONFIG_BK7258_AIDK_WAKE_REPLY_PCM)
    if(CONFIG_BK7258_AIDK_PRIVATE_WAKE_REPLY_PCM)
      set(wake_reply "${NUTTX_APPS_DIR}/system/bk7258/assets/wake_reply.pcm")
      if(NOT EXISTS "${wake_reply}")
        message(FATAL_ERROR "Personalized wake reply is selected but its authorized PCM is missing")
      endif()
      configure_file("${wake_reply}" "${wake_reply_staged}" COPYONLY)
      message(STATUS "AIDK personalized wake reply selected")
    elseif(CONFIG_BK7258_AIDK_PUBLIC_WAKE_REPLY_PCM AND
           NOT CONFIG_BK7258_AIDK_WAKE_REPLY_TONE_FALLBACK)
      set(wake_reply "${NUTTX_APPS_DIR}/system/bk7258/assets/wake_reply_public.pcm")
      if(NOT EXISTS "${wake_reply}")
        message(FATAL_ERROR "Public wake reply asset is missing")
      endif()
      configure_file("${wake_reply}" "${wake_reply_staged}" COPYONLY)
      set(wake_reply "${wake_reply_staged}")
      message(STATUS "AIDK public wake reply selected")
    else()
      if(NOT CONFIG_BK7258_AIDK_WAKE_REPLY_TONE_FALLBACK)
        message(FATAL_ERROR "Select a public or private wake reply, or explicitly enable the tone fallback")
      endif()
      set(wake_reply_generator
        "${NUTTX_APPS_DIR}/system/bk7258/assets/generate_wake_reply.py")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${wake_reply_generator}")
      execute_process(
        COMMAND python3 "${wake_reply_generator}" --output "${wake_reply_staged}"
        RESULT_VARIABLE wake_reply_result)
      if(NOT wake_reply_result EQUAL 0)
        message(FATAL_ERROR "Public wake acknowledgement generation failed")
      endif()
      set(wake_reply "${wake_reply_staged}")
      message(STATUS "AIDK public synthesized wake reply staged")
    endif()
    file(SHA256 "${wake_reply}" wake_reply_sha256)
    message(STATUS "AIDK wake reply PCM sha256=${wake_reply_sha256}")
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
