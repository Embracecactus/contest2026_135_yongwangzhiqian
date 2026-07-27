############################################################################
# contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
#
# Mirror of chip/Make.defs for the CMake backend.  Stage N1 is validated
# against the classic Make backend (matching the RV1126B precedent); this
# file exists so a CMake-driven build does not silently pull in an
# inconsistent source set.
############################################################################

# Sources mirror chip/Make.defs CHIP_CSRCS.
if(CONFIG_BK7258_AP_CORE)
  set(SRCS
      bk7258_ap_vectors.c
      bk7258_ap_start.c
      bk7258_lowputc.c
      bk7258_serial.c
      bk7258_irq.c
      bk7258_timerisr.c
      bk7258_allocateheap.c
      bk7258_ap_main.c)
else()
  set(SRCS
      bk7258_vectors.c
      bk7258_start.c
      bk7258_lowputc.c
      bk7258_serial.c
      bk7258_irq.c
      bk7258_timerisr.c
      bk7258_allocateheap.c
      bk7258_os_adapt.c
      bk7258_sdk_stubs.c)
endif()

# NOTE: The common armv8-m CMake path still adds arm_vectors.c.  When this
# chip is built under CMake, that file would collide with bk7258_vectors.c
# (duplicate _vectors symbol).  We do not exercise CMake for N1; if a later
# stage switches to CMake, filter arm_vectors.c out of the arch target here.

# Conditional sources mirroring chip/Make.defs (the Make backend is the one
# actually exercised; kept in sync here so a future CMake switch does not pull
# an inconsistent source set).

if(CONFIG_BK7258_AP_CONTROL)
  list(APPEND SRCS bk7258_ap_control.c)
endif()

if(CONFIG_BK7258_CLOCK_320M)
  list(APPEND SRCS bk7258_clock.c)
endif()

if(CONFIG_BK7258_FLASH_MTD)
  list(APPEND SRCS bk7258_flash_mtd.c)
endif()

if(CONFIG_ARM_SYSTICK)
  list(APPEND SRCS arm_systick.c)
endif()

if(CONFIG_BK7258_DVFS)
  list(APPEND SRCS bk7258_dvfs.c)
endif()

if(CONFIG_BK7258_DVFS_PROCFS)
  list(APPEND SRCS bk7258_dvfs_procfs.c)
endif()

if(CONFIG_BK7258_WDT)
  list(APPEND SRCS bk7258_wdt.c)
endif()

if(CONFIG_BK7258_SDK_IRQ_BRIDGE)
  list(APPEND SRCS bk7258_sdk_irq.c)
endif()

if(CONFIG_BK7258_SDK_IRQ_TIMER_TEST)
  list(APPEND SRCS bk7258_sdk_irq_timer_test.c)
endif()

target_sources(chip PRIVATE ${SRCS})

# Beken SDK include paths for the prebuilt library headers.

target_include_directories(chip PRIVATE
    ${BOARD_DIR}/bk_idk/armino_as_lib/cp/include
)
