############################################################################
# contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
#
# Mirror of chip/Make.defs for the CMake backend.
############################################################################

set(BK7258_COMMON_EARLY_SRCS
    common/bk7258_lowputc.c
    common/bk7258_serial.c
    common/bk7258_irq.c
    common/bk7258_timerisr.c
    common/bk7258_allocateheap.c)

set(BK7258_COMMON_LATE_SRCS
    common/bk7258_os_adapt.c
    common/bk7258_sdk_stubs.c)

set(BK7258_COMMON_SRCS
    ${BK7258_COMMON_EARLY_SRCS}
    ${BK7258_COMMON_LATE_SRCS})

# Preserve the classic Make source order so a CMake backend does not introduce
# role-specific archive/link ordering differences.

set(BK7258_CP_SRCS
    cp/bk7258_vectors.c
    cp/bk7258_start.c
    ${BK7258_COMMON_SRCS}
    cp/bk7258_sdk_partition.c)

if(NOT CONFIG_BK7258_AP_CORE)
  target_link_options(
    nuttx PRIVATE
    "LINKER:--wrap=bk_flash_partition_get_info"
    "LINKER:--wrap=bk_flash_partition_read"
    "LINKER:--wrap=bk_flash_partition_write"
    "LINKER:--wrap=bk_flash_partition_erase"
    "LINKER:--wrap=bk_flash_partition_write_perm_check_by_addr")
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
  if(CONFIG_BK7258_BT_IPC AND NOT CONFIG_BK7258_AP_CORE)
    target_link_options(
      nuttx PRIVATE
      "LINKER:--wrap=bk_bluetooth_init"
      "LINKER:--wrap=bk_bluetooth_deinit")
  endif()
endif()

if(CONFIG_BK7258_WIFI_VNET)
  list(APPEND BK7258_CP_SRCS common/bk7258_wifi_control.c)
  if(NOT CONFIG_BK7258_AP_CORE)
    target_link_options(
      nuttx PRIVATE
      "LINKER:--wrap=malloc")
  endif()
endif()

if(CONFIG_BK7258_RPTUN_MBOX)
  list(APPEND BK7258_CP_SRCS common/bk7258_rptun_mbox.c)
endif()

if(CONFIG_BK7258_RPTUN)
  list(APPEND BK7258_CP_SRCS common/bk7258_rptun.c)
  if(CONFIG_BK7258_AP_SUPERVISOR)
    list(APPEND BK7258_CP_SRCS common/bk7258_rpmsg_health.c)
  endif()
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

# The role-neutral SDK IRQ bridge lives in common/ and is selected separately
# by the CP bridge option or the AP N8-B2 IPI option.

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

if(CONFIG_BK7258_FLASH_MTD)
  list(APPEND BK7258_CP_SRCS
       cp/bk7258_flash_mtd.c
       cp/bk7258_flash_guard.c)
endif()

if(CONFIG_MCUBOOT_BOOTLOADER)
  list(APPEND BK7258_CP_SRCS
       ../bootloader/bl2/bk7258_bl2_mcuboot_boot.c
       ../bootloader/bl2/bk7258_bl2_mcuboot_ecdsa.c
       ../bootloader/bl2/bk7258_bl2_mcuboot_ecc.c
       ../bootloader/bl2/bk7258_bl2_mcuboot_ecc_dsa.c
       ../bootloader/bl2/bk7258_bl2_mcuboot_asn1.c
       ../bootloader/bl2/bk7258_bl2_mcuboot_keys.c)
endif()

if(CONFIG_BK7258_OTA_STAGING)
  list(APPEND BK7258_CP_SRCS
       cp/bk7258_ota_staging_core.c
       cp/bk7258_ota_staging.c
       cp/bk7258_ota_n17_auth.c
       common/bk7258_ota_n17_release_keys.c
       cp/bk7258_ota_n17_journal_core.c
       cp/bk7258_ota_n17_publish.c)
endif()

if(CONFIG_BK7258_OTA_TRIAL)
  list(APPEND BK7258_CP_SRCS
       cp/bk7258_ota_trial_core_select.c
       cp/bk7258_ota_trial_core_rotation.c
       cp/bk7258_ota_trial_core_publish.c
       cp/bk7258_ota_trial_core_control.c
       cp/bk7258_ota_trial_core_health.c
       cp/bk7258_ota_trial.c)
endif()

if(CONFIG_BK7258_OTA_FAULT_INJECTION)
  list(APPEND BK7258_CP_SRCS
       cp/bk7258_ota_fault_core.c
       cp/bk7258_ota_fault.c)
endif()

if(CONFIG_BK7258_CLOCK_320M)
  list(APPEND BK7258_CP_SRCS cp/bk7258_clock.c)
endif()

if(CONFIG_ARM_SYSTICK)
  list(APPEND BK7258_CP_SRCS arm_systick.c)
endif()

if(CONFIG_BK7258_DVFS)
  list(APPEND BK7258_CP_SRCS cp/bk7258_dvfs.c)
endif()

if(CONFIG_BK7258_DVFS_PROCFS)
  list(APPEND BK7258_CP_SRCS cp/bk7258_dvfs_procfs.c)
endif()

if(CONFIG_BK7258_WDT)
  list(APPEND BK7258_CP_SRCS cp/bk7258_wdt.c)
endif()

if(CONFIG_BK7258_SARADC_SERVER)
  list(APPEND BK7258_CP_SRCS cp/bk7258_saradc_server.c)
endif()

set(BK7258_AP_BASE_SRCS
    ap/bk7258_ap_vectors.c
    ap/bk7258_ap_start.c
    ${BK7258_COMMON_EARLY_SRCS}
    ap/bk7258_ap_main.c
    ap/bk7258_peripherals.c)

if(CONFIG_BK7258_PSRAM)
  list(APPEND BK7258_AP_BASE_SRCS common/bk7258_psram.c)
endif()

if(CONFIG_BK7258_BT_IPC)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_bt_hci.c)
endif()

if(CONFIG_BK7258_BLE_GATT)
  list(APPEND BK7258_AP_BASE_SRCS ap/bk7258_ble_gatt.c)
endif()

if(CONFIG_BK7258_WIFI_VNET)
  list(APPEND BK7258_AP_BASE_SRCS
       ap/bk7258_wifi.c
       common/bk7258_wifi_control.c)
endif()

# AP-owned NuttX peripheral wrappers.  Add these to AP_BASE rather than the
# UP-only list so the normal AP-SMP image and the reduced AP-UP diagnostic
# image compile the same lower halves.

if(CONFIG_BK7258_AUD AND CONFIG_BK7258_MIC)
  message(FATAL_ERROR
    "BK7258_AUD and BK7258_MIC cannot both own the AUD ADC block")
endif()

foreach(BK7258_AP_PERIPHERAL AUD GPIOE I2C I2S LCD MIC RTC SARADC SDIO SDMADC
                              SPI TIMER)
  if(CONFIG_BK7258_${BK7258_AP_PERIPHERAL})
    string(TOLOWER ${BK7258_AP_PERIPHERAL} BK7258_AP_PERIPHERAL_FILE)
    list(APPEND BK7258_AP_BASE_SRCS
         ap/bk7258_${BK7258_AP_PERIPHERAL_FILE}.c)
  endif()
endforeach()

if(CONFIG_BK7258_BT_IPC_TRACE)
  target_link_options(
    nuttx PRIVATE
    "LINKER:--wrap=bt_conn_receive"
    "LINKER:--wrap=bt_l2cap_receive"
    "LINKER:--wrap=bt_l2cap_send"
    "LINKER:--wrap=bt_conn_send"
    "LINKER:--wrap=bt_send"
    "LINKER:--wrap=bt_gatt_connected"
    "LINKER:--wrap=bt_gatt_disconnected"
    "LINKER:--wrap=bt_l2cap_create_pdu")
endif()

if(CONFIG_BK7258_RPTUN_MBOX)
  list(APPEND BK7258_AP_BASE_SRCS common/bk7258_rptun_mbox.c)
endif()

if(CONFIG_BK7258_RPTUN)
  list(APPEND BK7258_AP_BASE_SRCS common/bk7258_rptun.c)
  if(CONFIG_BK7258_AP_SUPERVISOR)
    list(APPEND BK7258_AP_BASE_SRCS
         common/bk7258_rpmsg_health.c
         ap/bk7258_ap_health.c)
  endif()
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

if(CONFIG_ARM_SYSTICK)
  list(APPEND BK7258_AP_UP_SRCS arm_systick.c)
  list(APPEND BK7258_AP_SMP_SRCS arm_systick.c)
endif()

if(CONFIG_BK7258_AP_CORE)
  if(CONFIG_BK7258_AP_SMP_BOOTSTRAP)
    set(SRCS ${BK7258_AP_SMP_SRCS})
  else()
    set(SRCS ${BK7258_AP_UP_SRCS})
  endif()
  set(BK7258_SDK_ROLE ap)
else()
  set(SRCS ${BK7258_CP_SRCS})
  set(BK7258_SDK_ROLE cp)
endif()

set(BK7258_SDK_SUPPORTED_BUNDLE_VERSIONS legacy v3.1.1.9)
if(DEFINED ENV{BK7258_SDK_BUNDLE_VERSION} AND
   NOT "$ENV{BK7258_SDK_BUNDLE_VERSION}" STREQUAL "")
  set(BK7258_SDK_BUNDLE_VERSION "$ENV{BK7258_SDK_BUNDLE_VERSION}")
else()
  set(BK7258_SDK_BUNDLE_VERSION v3.1.1.9)
endif()

if(NOT BK7258_SDK_BUNDLE_VERSION IN_LIST
   BK7258_SDK_SUPPORTED_BUNDLE_VERSIONS)
  message(FATAL_ERROR
    "Unsupported BK7258_SDK_BUNDLE_VERSION='${BK7258_SDK_BUNDLE_VERSION}'; "
    "supported: ${BK7258_SDK_SUPPORTED_BUNDLE_VERSIONS}")
endif()

set(BK7258_SDK_BUNDLE_BASE ${BOARD_DIR}/bk_idk/armino_as_lib)
set(BK7258_SDK_BUNDLE_ROOT
    ${BK7258_SDK_BUNDLE_BASE}/versions/${BK7258_SDK_BUNDLE_VERSION})
set(BK7258_SDK_ROLE_DIR
    ${BK7258_SDK_BUNDLE_ROOT}/${BK7258_SDK_ROLE})

if(NOT IS_DIRECTORY ${BK7258_SDK_ROLE_DIR}/include OR
   NOT IS_DIRECTORY ${BK7258_SDK_ROLE_DIR}/config)
  message(FATAL_ERROR
    "BK7258 ${BK7258_SDK_ROLE} SDK bundle "
    "'${BK7258_SDK_BUNDLE_VERSION}' is missing: ${BK7258_SDK_ROLE_DIR}")
endif()

# NOTE: The common armv8-m CMake path still adds arm_vectors.c.  When this
# chip is built under CMake, that file would collide with bk7258_vectors.c
# (duplicate _vectors symbol).  The classic Make backend remains authoritative
# until the CMake arch source set filters arm_vectors.c for custom vectors.

target_sources(chip PRIVATE ${SRCS})

target_include_directories(chip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/cp
    ${CMAKE_CURRENT_SOURCE_DIR}/ap
    ${BK7258_SDK_ROLE_DIR}/include
    ${BK7258_SDK_ROLE_DIR}/config)
