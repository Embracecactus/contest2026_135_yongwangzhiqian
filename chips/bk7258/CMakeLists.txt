############################################################################
# contest2026_135_yongwangzhiqian/board/bk7258/chip/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
#
# CMake mirror of chip/Make.defs.  Source ordering and feature gates follow
# the Classic Make backend; backend-specific ARM common-source differences
# are documented where they occur.
############################################################################

if("$ENV{BK7258_SDK_DIR}" STREQUAL "")
  message(FATAL_ERROR
          "BK7258_SDK_DIR is required; use tools/bk7258/bk7258.py build")
endif()
set(BK7258_SDK_DIR "$ENV{BK7258_SDK_DIR}")
set(BK7258_SDK_INCLUDE_DIR "${BK7258_SDK_DIR}/include")
set(BK7258_SDK_CONFIG_DIR "${BK7258_SDK_DIR}/config")
set(BK7258_SDK_LIBS_DIR "${BK7258_SDK_DIR}/libs")

function(bk7258_wrap_sdk_symbol)
  foreach(BK7258_SYMBOL ${ARGN})
    target_link_options(nuttx PRIVATE
                        "LINKER:--wrap=${BK7258_SYMBOL}")
  endforeach()
endfunction()

set(BK7258_COMMON_FRONT_SRCS
    common/bk7258_lowputc.c
    common/bk7258_serial.c
    common/bk7258_irq.c
    common/bk7258_timerisr.c
    common/bk7258_allocateheap.c)

set(BK7258_COMMON_RUNTIME_SRCS
    common/bk7258_irq.c
    common/bk7258_timerisr.c
    common/bk7258_allocateheap.c
    common/bk7258_os_adapt.c
    common/bk7258_sdk_stubs.c
    common/bk7258_idle.c)

set(BK7258_COMMON_LATE_SRCS
    common/bk7258_os_adapt.c
    common/bk7258_sdk_stubs.c
    common/bk7258_idle.c)

# CPU0/CP sources.  Keep the same order as BK7258_CP_CSRCS in Make.defs.

set(BK7258_CP_SRCS
    cp/bk7258_vectors.c
    cp/bk7258_start.c)

if(CONFIG_BK7258_CONSOLE_RTT)
  list(APPEND BK7258_CP_SRCS common/bk7258_rtt_lowputc.c)
  target_include_directories(arch PRIVATE
      ${NUTTX_DIR}/drivers/segger/SystemView/SEGGER
      ${NUTTX_DIR}/drivers/segger/config)
else()
  # CONSOLE_NONE still needs the side-effect-free arm_lowputc()/up_putc()
  # hooks referenced by generic NuttX assert and syslog code.
  list(APPEND BK7258_CP_SRCS common/bk7258_lowputc.c)
endif()

if(CONFIG_BK7258_UART0 OR CONFIG_BK7258_UART1 OR CONFIG_BK7258_UART2)
  list(APPEND BK7258_CP_SRCS common/bk7258_serial.c)
endif()

list(APPEND BK7258_CP_SRCS
     ${BK7258_COMMON_RUNTIME_SRCS})

if(CONFIG_BK7258_SWD_DEBUG)
  list(APPEND BK7258_CP_SRCS cp/bk7258_swd_debug.c)
  if(NOT CONFIG_BK7258_AP_CORE)
    bk7258_wrap_sdk_symbol(gpio_hal_default_map_init)
  endif()
endif()

if(CONFIG_BK7258_SDK_IPC_RUNTIME)
  list(APPEND BK7258_CP_SRCS common/bk7258_sdk_runtime.c)
endif()

if(CONFIG_BK7258_PSRAM)
  list(APPEND BK7258_CP_SRCS common/bk7258_psram.c)
endif()

if(CONFIG_BK7258_AP_CONTROL)
  list(APPEND BK7258_CP_SRCS cp/bk7258_ap_control.c)
endif()

if(CONFIG_BK7258_AP_SUPERVISOR)
  list(APPEND BK7258_CP_SRCS cp/bk7258_ap_supervisor.c)
endif()

if(CONFIG_BK7258_BT_IPC OR CONFIG_BK7258_WIFI_VNET)
  list(APPEND BK7258_CP_SRCS cp/bk7258_bt_controller.c)
endif()

if(CONFIG_BK7258_WIFI_VNET)
  if(NOT CONFIG_BK7258_AP_CORE)
    bk7258_wrap_sdk_symbol(malloc)
  endif()
endif()

if(CONFIG_BK7258_WIFI_PACKET_DIAG)
  list(APPEND BK7258_CP_SRCS cp/bk7258_wifi_packet_diag.c)
  bk7258_wrap_sdk_symbol(ethernetif_input udp_sendto_if_src)
endif()

if(CONFIG_BK7258_BT_IPC AND NOT CONFIG_BK7258_AP_CORE)
  bk7258_wrap_sdk_symbol(bk_bluetooth_init bk_bluetooth_deinit)
endif()

if(CONFIG_BK7258_RPTUN_MBOX)
  list(APPEND BK7258_CP_SRCS common/bk7258_rptun_mbox.c)
endif()

if(CONFIG_BK7258_RPTUN)
  list(APPEND BK7258_CP_SRCS common/bk7258_rptun.c)
  if(CONFIG_BK7258_OTA_RPMSG)
    list(APPEND BK7258_CP_SRCS common/bk7258_ota_rpmsg.c)
  endif()
  if(CONFIG_BK7258_PM_CLOCK)
    list(APPEND BK7258_CP_SRCS cp/bk7258_pm_server.c)
  endif()
  if(CONFIG_BK7258_TEMPERATURE)
    list(APPEND BK7258_CP_SRCS cp/bk7258_temperature_server.c)
  endif()
  if(CONFIG_BK7258_AP_SUPERVISOR)
    list(APPEND BK7258_CP_SRCS common/bk7258_rpmsg_health.c)
  endif()
  if(CONFIG_BK7258_RPMSG_TEST)
    list(APPEND BK7258_CP_SRCS common/bk7258_rpmsg_test.c)
  endif()
  if(CONFIG_BK7258_RPMSGFS_TEST)
    list(APPEND BK7258_CP_SRCS common/bk7258_rpmsgfs_test.c)
  endif()
  if(CONFIG_BK7258_BT_IPC_TEST)
    list(APPEND BK7258_CP_SRCS common/bk7258_bt_test.c)
  endif()
endif()

if(CONFIG_BK7258_SDK_IRQ_BRIDGE)
  list(APPEND BK7258_CP_SRCS common/bk7258_sdk_irq.c)
endif()

if(CONFIG_BK7258_SDK_IRQ_TIMER_TEST)
  list(APPEND BK7258_CP_SRCS cp/bk7258_sdk_irq_timer_test.c)
endif()

if(CONFIG_BK7258_SDK_TIMER_SELFTEST)
  list(APPEND BK7258_CP_SRCS cp/bk7258_sdk_timer_selftest.c)
endif()

if(CONFIG_BK7258_GPIO_FOUNDATION_TEST)
  list(APPEND BK7258_CP_SRCS cp/bk7258_gpio_foundation_test.c)
endif()

if(CONFIG_BK7258_GPIO_IRQ_TEST)
  list(APPEND BK7258_CP_SRCS cp/bk7258_gpio_irq_test.c)
endif()

if(CONFIG_BK7258_GPIO_LOWERHALF)
  list(APPEND BK7258_CP_SRCS
       cp/bk7258_gpio_cp_irq.c
       cp/bk7258_gpio_lowerhalf.c)
endif()

if(CONFIG_BK7258_CLOCK_320M)
  list(APPEND BK7258_CP_SRCS cp/bk7258_clock.c)
endif()

# arm_m/CMakeLists.txt already selects arm_systick.c with CONFIG_ARM_SYSTICK.
# Classic Make adds it here only to work around its CONFIG_ARM_M_SYSTICK typo.

if(CONFIG_BK7258_DVFS)
  list(APPEND BK7258_CP_SRCS cp/bk7258_dvfs.c)
endif()

if(CONFIG_BK7258_PM_POLICY)
  list(APPEND BK7258_CP_SRCS cp/bk7258_pm_policy.c)
  bk7258_wrap_sdk_symbol(bk_pm_module_vote_cpu_freq)
endif()

if(CONFIG_BK7258_PM_COORDINATED_STANDBY)
  list(APPEND BK7258_CP_SRCS
       common/bk7258_pm_activity.c
       cp/bk7258_pm_coord_cp.c)
  bk7258_wrap_sdk_symbol(pm_low_voltage_bsp_restore
                         bk_pm_module_vote_sleep_ctrl)
endif()

if(CONFIG_BK7258_DVFS_PROCFS)
  list(APPEND BK7258_CP_SRCS cp/bk7258_dvfs_procfs.c)
endif()

if(CONFIG_BK7258_WDT)
  list(APPEND BK7258_CP_SRCS cp/bk7258_wdt.c)
endif()

if(CONFIG_BK7258_IRDA)
  list(APPEND BK7258_CP_SRCS cp/bk7258_irda.c)
endif()

if(CONFIG_BK7258_SARADC_SERVER)
  list(APPEND BK7258_CP_SRCS cp/bk7258_saradc_server.c)
endif()

if(CONFIG_BK7258_TOUCH)
  list(APPEND BK7258_CP_SRCS cp/bk7258_touch.c)
endif()

# Physical CPU1/AP sources.  AP-UP and AP-SMP share the same peripheral set.

set(BK7258_AP_BASE_SRCS
    ap/bk7258_ap_vectors.c
    ap/bk7258_ap_start.c
    ${BK7258_COMMON_FRONT_SRCS}
    ap/bk7258_ap_main.c
    ap/bk7258_peripherals.c)

if(CONFIG_BK7258_PM_COORDINATED_STANDBY)
  list(APPEND BK7258_AP_BASE_SRCS
       common/bk7258_pm_activity.c
       ap/bk7258_pm_coord_ap.c)
endif()

if(CONFIG_BK7258_SDK_IPC_RUNTIME)
  list(APPEND BK7258_AP_BASE_SRCS common/bk7258_sdk_runtime.c)
endif()

if(CONFIG_BK7258_PSRAM)
  list(APPEND BK7258_AP_BASE_SRCS common/bk7258_psram.c)
endif()

if(CONFIG_BK7258_BT_IPC)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_bt_hci.c)
endif()

if(CONFIG_BK7258_BLE_GATT)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_ble_gatt.c)
endif()

if(CONFIG_BK7258_OTA_MANAGER)
  if("$ENV{BK7258_OTA_CATALOG_PUBLIC_SOURCE}" STREQUAL "")
    message(FATAL_ERROR
            "BK7258_OTA_MANAGER requires the generated catalog public root")
  endif()
  list(APPEND BK7258_AP_BASE_SRCS
       ap/bk7258_ota_manager.c
       ap/bk7258_ota_catalog.c
       "$ENV{BK7258_OTA_CATALOG_PUBLIC_SOURCE}")
  target_include_directories(arch PRIVATE ${CMAKE_BINARY_DIR}/apps/include)
endif()

if(CONFIG_BK7258_OTA_SOURCE_FILE)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_ota_source_file.c)
endif()

if(CONFIG_BK7258_OTA_SOURCE_HTTP)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_ota_source_http.c)
endif()

if(CONFIG_BK7258_WIFI_VNET)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_wifi.c)
endif()

foreach(BK7258_AP_PERIPHERAL AUD GPIOE I2C I2S)
  if(CONFIG_BK7258_${BK7258_AP_PERIPHERAL})
    string(TOLOWER ${BK7258_AP_PERIPHERAL} BK7258_AP_PERIPHERAL_FILE)
    list(APPEND BK7258_AP_BASE_SRCS
         ap/bk7258_${BK7258_AP_PERIPHERAL_FILE}.c)
  endif()
endforeach()

if(CONFIG_BK7258_LCD)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_lcd_rgb.c)
endif()

if(CONFIG_BK7258_LCD_SPI)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_lcd_spi.c)
endif()

if(CONFIG_BK7258_LIN)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_lin.c)
endif()

if(CONFIG_BK7258_ETH)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_eth.c)
endif()

if(CONFIG_BK7258_DMA)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_dma.c)
endif()

if(CONFIG_BK7258_SLCD)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_slcd.c)
endif()

if(CONFIG_BK7258_SBC)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_sbc.c)
endif()

if(CONFIG_BK7258_DMIC)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_dmic.c)
endif()

if(CONFIG_BK7258_USBCDC)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_usbcdc.c)
endif()

if(CONFIG_BK7258_LCD_8080)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_lcd_8080.c)
endif()

if(CONFIG_BK7258_PWM)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_pwm.c)
endif()

if(CONFIG_BK7258_AUD OR CONFIG_BK7258_DVP OR
   CONFIG_BK7258_JPEG_ENCODER OR CONFIG_BK7258_YUV_H264 OR
   CONFIG_BK7258_MIC)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_media_root.c)
endif()

if(CONFIG_BK7258_AUD_LIFECYCLE_VALIDATION)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_aud_validation.c)
endif()

if(CONFIG_BK7258_DVP)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_dvp.c)
endif()

if(CONFIG_BK7258_DMA2D)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_dma2d.c)
  bk7258_wrap_sdk_symbol(sys_drv_dma2d_set)
endif()

if(CONFIG_BK7258_LVGL_FB_ACCEL)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_lvgl_fb.c)
endif()

if(CONFIG_BK7258_SCALE_ROTATE)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_scale_rotate.c)
endif()

if(CONFIG_BK7258_JPEG_DECODER)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_jpeg_decoder.c)
endif()

if(CONFIG_BK7258_JPEG_M2M)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_jpeg_m2m.c)
endif()

if(CONFIG_BK7258_JPEG_M2M_VALIDATION)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_jpeg_m2m_validation.c)
endif()

if(CONFIG_BK7258_JPEG_ENCODER)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_jpeg_encoder.c)
endif()

if(CONFIG_BK7258_YUV_H264)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_yuv_h264.c)
endif()

if(CONFIG_BK7258_DVP_BOARD_GLUE)
  bk7258_wrap_sdk_symbol(
    bk_i2c_init_v2
    bk_i2c_deinit_v2
    bk_i2c_memory_read_v2
    bk_i2c_memory_write_v2
    dvp_camera_mclk_enable)
endif()

foreach(BK7258_AP_PERIPHERAL MIC RTC SARADC)
  if(CONFIG_BK7258_${BK7258_AP_PERIPHERAL})
    string(TOLOWER ${BK7258_AP_PERIPHERAL} BK7258_AP_PERIPHERAL_FILE)
    list(APPEND BK7258_AP_BASE_SRCS
         ap/bk7258_${BK7258_AP_PERIPHERAL_FILE}.c)
  endif()
endforeach()

if(CONFIG_BK7258_SARADC_VALIDATION)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_saradc_validation.c)
endif()

if(CONFIG_BK7258_MIC_LIFECYCLE_VALIDATION)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_mic_validation.c)
endif()

if(CONFIG_EXAMPLES_AI_AGENT_VELA AND NOT CONFIG_MEDIA AND
   (CONFIG_BK7258_MIC OR CONFIG_BK7258_AUD))
  set(BK7258_AGENT_MEDIA_INC
      ${CMAKE_SOURCE_DIR}/../frameworks/multimedia/media/include)
  if(CONFIG_BK7258_MIC)
    list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_agent_media_recorder.c)
    list(APPEND BK7258_AGENT_MEDIA_DEFINE
         CONFIG_BK7258_AGENT_MEDIA_RECORDER=1)
  endif()
  if(CONFIG_BK7258_AUD)
    list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_agent_media_player.c)
    list(APPEND BK7258_AGENT_MEDIA_DEFINE
         CONFIG_BK7258_AGENT_MEDIA_PLAYER=1)
  endif()
endif()

if(CONFIG_BK7258_SDIO)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_sdio.c)
endif()

foreach(BK7258_AP_PERIPHERAL SDMADC SPI QSPI CAN TIMER TRNG)
  if(CONFIG_BK7258_${BK7258_AP_PERIPHERAL})
    string(TOLOWER ${BK7258_AP_PERIPHERAL} BK7258_AP_PERIPHERAL_FILE)
    list(APPEND BK7258_AP_BASE_SRCS
         ap/bk7258_${BK7258_AP_PERIPHERAL_FILE}.c)
  endif()
endforeach()

if(CONFIG_BK7258_USBHOST)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_usbhost.c)
  bk7258_wrap_sdk_symbol(usbh_initialize usbh_deinitialize
                         usbh_roothub_thread_send_queue)
endif()

if(CONFIG_BK7258_USBHOST_CH34X)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_usbserial_ch34x.c)
endif()

if(CONFIG_BK7258_BT_IPC_TRACE)
  bk7258_wrap_sdk_symbol(
    bt_conn_receive
    bt_l2cap_receive
    bt_l2cap_send
    bt_conn_send
    bt_send
    bt_gatt_connected
    bt_gatt_disconnected
    bt_l2cap_create_pdu)
endif()

if(CONFIG_BK7258_RPTUN_MBOX)
  list(APPEND BK7258_AP_BASE_SRCS common/bk7258_rptun_mbox.c)
endif()

if(CONFIG_BK7258_RPTUN)
  list(APPEND BK7258_AP_BASE_SRCS common/bk7258_rptun.c)
  if(CONFIG_BK7258_OTA_RPMSG)
    list(APPEND BK7258_AP_BASE_SRCS common/bk7258_ota_rpmsg.c)
  endif()
  if(CONFIG_BK7258_PM_CLOCK)
    list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_pm_client.c)
    if(CONFIG_BK7258_AP_CORE)
      bk7258_wrap_sdk_symbol(bk_pm_clock_ctrl
                             bk_pm_module_vote_cpu_freq)
      if(CONFIG_BK7258_AUD OR CONFIG_BK7258_MIC OR CONFIG_BK7258_DMA2D OR
         CONFIG_BK7258_SCALE_ROTATE OR
         CONFIG_BK7258_JPEG_DECODER OR CONFIG_BK7258_JPEG_ENCODER OR
         CONFIG_BK7258_YUV_H264 OR CONFIG_BK7258_DVP)
        bk7258_wrap_sdk_symbol(bk_pm_module_vote_power_ctrl)
      endif()
      if(CONFIG_BK7258_DVP_H264_COMPAT)
        bk7258_wrap_sdk_symbol(
          bk_yuv_buf_start
          bk_h264_encode_enable
          sys_drv_int_group2_enable
          sys_drv_core_intr_group2_enable
          dvp_camera_i2c_write_uint8
          video_register)
      endif()
    endif()
  endif()
  if(CONFIG_BK7258_TEMPERATURE)
    list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_temperature_client.c)
  endif()
  if(CONFIG_BK7258_TEMPERATURE_VALIDATION)
    list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_temperature_validation.c)
  endif()
  if(CONFIG_BK7258_AP_SUPERVISOR)
    list(APPEND BK7258_AP_BASE_SRCS
         common/bk7258_rpmsg_health.c
         ap/bk7258_ap_health.c)
  endif()
  if(CONFIG_BK7258_RPMSG_TEST)
    list(APPEND BK7258_AP_BASE_SRCS common/bk7258_rpmsg_test.c)
  endif()
  if(CONFIG_BK7258_RPMSGFS)
    list(APPEND BK7258_AP_BASE_SRCS common/bk7258_rpmsgfs.c)
  endif()
  if(CONFIG_BK7258_RPMSGFS_TEST)
    list(APPEND BK7258_AP_BASE_SRCS common/bk7258_rpmsgfs_test.c)
  endif()
  if(CONFIG_BK7258_BT_IPC_TEST)
    list(APPEND BK7258_AP_BASE_SRCS common/bk7258_bt_test.c)
  endif()
endif()

set(BK7258_AP_UP_SRCS
    ${BK7258_AP_BASE_SRCS}
    ap/bk7258_cpu2_probe.c
    ${BK7258_COMMON_LATE_SRCS})

set(BK7258_AP_SMP_SRCS
    ${BK7258_AP_BASE_SRCS}
    ap/bk7258_ap_smp.c
    ap/bk7258_ap_smp_advanced.c)

if(CONFIG_BK7258_AP_IPI)
  list(APPEND BK7258_AP_SMP_SRCS
       common/bk7258_sdk_irq.c
       ap/bk7258_ap_ipi.c)
endif()

list(APPEND BK7258_AP_SMP_SRCS ${BK7258_COMMON_LATE_SRCS})

if(CONFIG_BK7258_AP_CORE)
  if(CONFIG_BK7258_AP_SMP_BOOTSTRAP)
    set(BK7258_SRCS ${BK7258_AP_SMP_SRCS})
  else()
    set(BK7258_SRCS ${BK7258_AP_UP_SRCS})
  endif()
else()
  set(BK7258_SRCS ${BK7258_CP_SRCS})
endif()

target_sources(arch PRIVATE ${BK7258_SRCS})

target_include_directories(arch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/cp
    ${CMAKE_CURRENT_SOURCE_DIR}/ap
    ${NUTTX_DIR}/arch/arm/src/arm_m
    ${NUTTX_DIR}/arch/arm/src/armv8-m
    ${BK7258_AGENT_MEDIA_INC})
target_include_directories(arch SYSTEM PRIVATE
    ${BK7258_SDK_INCLUDE_DIR}
    ${BK7258_SDK_CONFIG_DIR})

# The exported sdkconfig.h is part of the immutable static-library ABI and stays
# authoritative.  Keep only OpenVela feature-local definitions here.

if(BK7258_AGENT_MEDIA_DEFINE)
  target_compile_definitions(arch PRIVATE ${BK7258_AGENT_MEDIA_DEFINE})
endif()

if(NOT CONFIG_BK7258_AP_CORE)
  bk7258_wrap_sdk_symbol(arm_doirq nxsched_resume_scheduler)
elseif(CONFIG_BK7258_AP_SMP_SCHED_ONLINE)
  bk7258_wrap_sdk_symbol(arm_doirq nxsched_resume_scheduler nx_bringup)
endif()

target_link_options(nuttx PRIVATE
                    "LINKER:--build-id=none"
                    "LINKER:--entry=_vectors")
