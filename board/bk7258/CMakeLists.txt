# NuttX board-level CMake for the Beken BK7258 logical board.

include("${NUTTX_BOARD_DIR}/bk_idk/sdk-bundles.cmake")

set(BK7258_BOARD_VARIANT_COUNT 0)
foreach(BK7258_BOARD_SELECTOR
        CONFIG_BK7258_BOARD_AIDK_AI_TOY
        CONFIG_BK7258_BOARD_T5_BOARD
        CONFIG_BK7258_BOARD_T5AI_CORE)
  if(${BK7258_BOARD_SELECTOR})
    math(EXPR BK7258_BOARD_VARIANT_COUNT
         "${BK7258_BOARD_VARIANT_COUNT} + 1")
  endif()
endforeach()

if(NOT BK7258_BOARD_VARIANT_COUNT EQUAL 1)
  message(FATAL_ERROR "Select exactly one BK7258 physical board")
endif()

if(CONFIG_BK7258_BOARD_AIDK_AI_TOY)
  set(BK7258_BOARD_VARIANT_DIR
      "${NUTTX_BOARD_DIR}/boards/aidk_ai_toy")
elseif(CONFIG_BK7258_BOARD_T5_BOARD)
  set(BK7258_BOARD_VARIANT_DIR
      "${NUTTX_BOARD_DIR}/boards/t5_board")
elseif(CONFIG_BK7258_BOARD_T5AI_CORE)
  set(BK7258_BOARD_VARIANT_DIR
      "${NUTTX_BOARD_DIR}/boards/t5ai_core")
endif()

# board.h is consumed by board and architecture compilation units.  Keep the
# selected carrier header on both include paths; the classic Make backend
# mirrors this in scripts/Make.defs.

target_include_directories(board PUBLIC
  "${BK7258_BOARD_VARIANT_DIR}/include")
target_include_directories(arch PUBLIC
  "${BK7258_BOARD_VARIANT_DIR}/include")

add_subdirectory(src)

# Pin the role-specific linker script for the CMake backend.
if(CONFIG_BK7258_AP_CORE)
  set(BK7258_LDSCRIPT ld_ap.script)
else()
  set(BK7258_LDSCRIPT ld.script)
endif()

set_property(GLOBAL PROPERTY LD_SCRIPT
             "${NUTTX_BOARD_DIR}/scripts/${BK7258_LDSCRIPT}")

# NuttX Apps owns MCUboot's core sources while the BK7258 board owns the
# signing-policy overlay and P-256 support headers.  Register these before the
# apps tree is traversed so the board overlay remains first in the search path.

if(CONFIG_MCUBOOT_BOOTLOADER)
  nuttx_include_directories_for_all_apps(
    "${NUTTX_BOARD_DIR}/chip/include"
    "${NUTTX_APPS_DIR}/boot/mcuboot/mcuboot/ext/tinycrypt/lib/include"
    "${NUTTX_APPS_DIR}/boot/mcuboot/mcuboot/ext/mbedtls-asn1/include")
endif()

# Link the same immutable SDK archive closure as scripts/Make.defs.  Imported
# full-path libraries preserve the Classic Make ordering and avoid -l name
# rewriting across CMake versions.

function(bk7258_add_sdk_libraries)
  foreach(BK7258_LIBRARY ${ARGN})
    if(NOT EXISTS "${BK7258_LIBRARY}")
      message(FATAL_ERROR "BK7258 SDK library is missing: ${BK7258_LIBRARY}")
    endif()
    nuttx_add_extra_library("${BK7258_LIBRARY}")
  endforeach()
endfunction()

if(CONFIG_BK7258_AP_CORE)
  set(BK7258_SDK_LIBRARIES)

  if(CONFIG_BK7258_BT_IPC)
    list(APPEND BK7258_SDK_LIBRARIES
         "${BK7258_SDK_LIBS_DIR}/libbk_bluetooth.a")
  endif()

  if(CONFIG_BK7258_WIFI_VNET)
    list(APPEND BK7258_SDK_LIBRARIES
         "${BK7258_SDK_LIBS_DIR}/libbk_wifi.a"
         "${BK7258_SDK_LIBS_DIR}/libbk_wifi_driver.a")
  endif()

  if(CONFIG_BK7258_USBHOST)
    list(APPEND BK7258_SDK_LIBRARIES
         "${BK7258_SDK_LIBS_DIR}/libbk_usb.a")
  endif()

  if(CONFIG_BK7258_DVP)
    list(APPEND BK7258_SDK_LIBRARIES
         "${BK7258_SDK_LIBS_DIR}/libbk_dvp.a"
         "${BK7258_SDK_LIBS_DIR}/libbk_camera.a"
         "${BK7258_SDK_LIBS_DIR}/libbk_peripheral.a"
         "${BK7258_SDK_LIBS_DIR}/libmedia_utils.a"
         "${BK7258_SDK_LIBS_DIR}/libmultimedia.a")
    foreach(BK7258_SENSOR gc0308 gc0328c gc2145 hm1055 ov2640 sc101)
      target_link_options(nuttx PRIVATE
                          "LINKER:--undefined=${BK7258_SENSOR}_detect_sensor")
    endforeach()
  endif()

  if(CONFIG_BK7258_JPEG_DECODER)
    list(APPEND BK7258_SDK_LIBRARIES
         "${BK7258_SDK_LIBS_DIR}/libbk_jpeg_decoder.a")
  endif()

  list(APPEND BK7258_SDK_LIBRARIES
       "${BK7258_SDK_LIBS_DIR}/libdriver.a"
       "${BK7258_SDK_LIBS_DIR}/libbk7258_ap.a"
       "${BK7258_SDK_LIBS_DIR}/libbk_pm.a"
       "${BK7258_SDK_LIBS_DIR}/libbk_adapter.a"
       "${BK7258_SDK_LIBS_DIR}/libbk_common.a"
       "${BK7258_SDK_LIBS_DIR}/libcommon.a"
       "${BK7258_SDK_LIBS_DIR}/libbk_log.a"
       "${BK7258_SDK_LIBS_DIR}/libbk_event.a"
       "${BK7258_SDK_LIBS_DIR}/libavdk_utils.a"
       "${BK7258_SDK_LIBS_DIR}/libcm33.a")
else()
  file(GLOB BK7258_SDK_LIBRARIES CONFIGURE_DEPENDS
       "${BK7258_SDK_LIBS_DIR}/*.a")
  set(BK7258_CP_EXCLUDE_LIBS
      libcmsis.a libcoredump.a libunity.a libat_server.a
      liblwip_intf_v2_1.a libwpa_supplicant-2.10.a
      libiperf.a libhmac_sha_256.a libpsa_mbedtls.a
      libwifi.a libwifi_csi.a libwifi_csi_nspe.a libwifi_nspe.a libbk_wifi.a
      libble_wifi_exchange.a
      libbluetooth_controller.a
      libbluetooth_controller_ble.a
      libbluetooth_controller_ble_cp_slave_observer.a
      libbluetooth_controller_ble_cp_slave_observer_nspe.a
      libbluetooth_controller_ble_cp_slave_only.a
      libbluetooth_controller_ble_cp_slave_only_nspe.a
      libbluetooth_controller_ble_nspe.a
      libbluetooth_controller_ble_non_calibration.a
      libbluetooth_controller_ble_non_calibration_nspe.a
      libbluetooth_controller_ble_reduce.a
      libbluetooth_controller_ble_reduce_nspe.a
      libbluetooth_controller_ble_slave_only.a
      libbluetooth_controller_ble_slave_only_nspe.a
      libbluetooth_controller_bt.a
      libbluetooth_controller_bt_nspe.a
      libbluetooth_controller_controller_only_ble.a
      libbluetooth_controller_controller_only_ble_nspe.a
      libbluetooth_controller_controller_only_dual.a
      libbluetooth_controller_controller_only_dual_nspe.a
      libbluetooth_controller_dual.a
      libbluetooth_controller_dual_nspe.a
      libbluetooth_host_ble.a
      libbluetooth_host_ble_only.a
      libbluetooth_host_ble_reduce.a
      libbluetooth_host_ble_slave_only.a
      libbluetooth_host_dm.a
      libbluetooth_host_dm_bt.a
      libbluetooth_host_dm_dual.a
      libbk_phy_nspe.a
      libate_phy_nspe.a
      libcom_phy_nspe.a
      libcom_phy.a
      libate_phy.a
      libbk_rtos.a
      libbk_system.a
      libos_source.a)

  foreach(BK7258_EXCLUDE_LIB ${BK7258_CP_EXCLUDE_LIBS})
    list(REMOVE_ITEM BK7258_SDK_LIBRARIES
         "${BK7258_SDK_LIBS_DIR}/${BK7258_EXCLUDE_LIB}")
  endforeach()

  if(CONFIG_BK7258_BT_IPC)
    list(APPEND BK7258_SDK_LIBRARIES
         "${BK7258_SDK_LIBS_DIR}/libbluetooth_controller_controller_only_ble.a"
         "${BK7258_SDK_LIBS_DIR}/libcom_phy.a"
         "${BK7258_SDK_LIBS_DIR}/libwifi.a"
         "${BK7258_SDK_LIBS_DIR}/libbk_wifi.a"
         "${BK7258_SDK_LIBS_DIR}/liblwip_intf_v2_1.a")
  endif()

  if(CONFIG_BK7258_WIFI_VNET)
    list(APPEND BK7258_SDK_LIBRARIES
         "${BK7258_SDK_LIBS_DIR}/libcom_phy.a"
         "${BK7258_SDK_LIBS_DIR}/libwifi.a"
         "${BK7258_SDK_LIBS_DIR}/libbk_wifi.a"
         "${BK7258_SDK_LIBS_DIR}/libcontroller_if.a"
         "${BK7258_SDK_LIBS_DIR}/libwpa_supplicant-2.10.a"
         "${BK7258_SDK_LIBS_DIR}/libpsa_mbedtls.a"
         "${BK7258_SDK_LIBS_DIR}/liblwip_intf_v2_1.a")
  endif()
endif()

bk7258_add_sdk_libraries(${BK7258_SDK_LIBRARIES})

# Mirror the Classic Make POSTBUILD contract in the CMake output directory.

if(CONFIG_BK7258_AP_CORE)
  set(BK7258_IMAGE_ROLE ap)
else()
  set(BK7258_IMAGE_ROLE cp)
endif()

add_custom_target(nuttx_post_build
  COMMAND "${NUTTX_BOARD_DIR}/scripts/postbuild.sh"
          "${CMAKE_BINARY_DIR}" "${NUTTX_BOARD_DIR}"
          "${BK7258_IMAGE_ROLE}"
  VERBATIM)
