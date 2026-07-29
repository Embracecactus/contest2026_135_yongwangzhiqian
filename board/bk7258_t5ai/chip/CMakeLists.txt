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
    ${BK7258_COMMON_SRCS})

if(CONFIG_BK7258_AP_CONTROL)
  list(APPEND BK7258_CP_SRCS cp/bk7258_ap_control.c)
endif()

# The role-neutral SDK IRQ bridge lives in common/ and is selected separately
# by the CP bridge option or the AP N8-B2 IPI option.

if(CONFIG_BK7258_SDK_IRQ_BRIDGE)
  list(APPEND BK7258_CP_SRCS common/bk7258_sdk_irq.c)
endif()

if(CONFIG_BK7258_SDK_IRQ_TIMER_TEST)
  list(APPEND BK7258_CP_SRCS cp/bk7258_sdk_irq_timer_test.c)
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
  list(APPEND BK7258_CP_SRCS cp/bk7258_flash_mtd.c)
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

set(BK7258_AP_BASE_SRCS
    ap/bk7258_ap_vectors.c
    ap/bk7258_ap_start.c
    ${BK7258_COMMON_EARLY_SRCS}
    ap/bk7258_ap_main.c)

set(BK7258_AP_UP_SRCS
    ${BK7258_AP_BASE_SRCS}
    ap/bk7258_cpu2_probe.c
    ${BK7258_COMMON_LATE_SRCS})

set(BK7258_AP_SMP_SRCS
    ${BK7258_AP_BASE_SRCS}
    ap/bk7258_ap_smp.c)

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

# NOTE: The common armv8-m CMake path still adds arm_vectors.c.  When this
# chip is built under CMake, that file would collide with bk7258_vectors.c
# (duplicate _vectors symbol).  The classic Make backend remains authoritative
# until the CMake arch source set filters arm_vectors.c for custom vectors.

target_sources(chip PRIVATE ${SRCS})

target_include_directories(chip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/cp
    ${CMAKE_CURRENT_SOURCE_DIR}/ap
    ${BOARD_DIR}/bk_idk/armino_as_lib/${BK7258_SDK_ROLE}/include
    ${BOARD_DIR}/bk_idk/armino_as_lib/${BK7258_SDK_ROLE}/config)
