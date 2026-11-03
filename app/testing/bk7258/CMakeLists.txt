# ##############################################################################
# SPDX-License-Identifier: Apache-2.0
# ##############################################################################

if(CONFIG_TESTS_BK7258_RPMSG_EXEC)
  target_sources(apps PRIVATE bk7258_drivercheck.c)
endif()

if(CONFIG_TESTS_BK7258_NATIVE_APPS)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(BKNATIVE_FIXTURE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
  set(BKNATIVE_FIXTURE_OBJECT ${BKNATIVE_FIXTURE_DIR}/breathing.o)
  set(BKNATIVE_FIXTURE_ELF ${BKNATIVE_FIXTURE_DIR}/breathing.elf)
  set(BKNATIVE_FIXTURE_HEADER ${BKNATIVE_FIXTURE_DIR}/bk7258_native_fixture.h)
  set(BKNATIVE_FIXTURE_SCRIPT ${BKNATIVE_FIXTURE_DIR}/native_fixture.py)
  file(MAKE_DIRECTORY ${BKNATIVE_FIXTURE_DIR})
  file(WRITE ${BKNATIVE_FIXTURE_SCRIPT}
"from pathlib import Path\nimport sys\ndata = Path(sys.argv[1]).read_bytes()\nout = Path(sys.argv[2])\ntext = ['/* generated; do not edit */', '#ifndef BKNATIVE_FIXTURE_H', '#define BKNATIVE_FIXTURE_H', '#include <stddef.h>', '#include <stdint.h>', 'static const uint8_t g_bknative_fixture[] = {']\ntext.extend('0x%02x,' % value for value in data)\ntext.extend(['};', 'static const size_t g_bknative_fixture_size = sizeof(g_bknative_fixture);', '#endif'])\nout.write_text('\\n'.join(text) + '\\n', encoding='ascii')\n")
  add_custom_command(
    OUTPUT ${BKNATIVE_FIXTURE_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BKNATIVE_FIXTURE_DIR}
    COMMAND ${CMAKE_C_COMPILER}
      -march=armv8-m.main+dsp -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard
      -Os -std=gnu11 -ffreestanding -fno-builtin -fno-stack-protector
      -fno-common -fno-unwind-tables -fno-asynchronous-unwind-tables
      -ffunction-sections -fdata-sections -I${NUTTX_APPS_DIR}/system/bk7258
      -c ${NUTTX_APPS_DIR}/system/bk7258/native-apps/breathing.c
      -o ${BKNATIVE_FIXTURE_OBJECT}
    COMMAND ${CMAKE_C_COMPILER} -nostdlib -r
      -Wl,-e,shaniu_native_entry -o ${BKNATIVE_FIXTURE_ELF}
      ${BKNATIVE_FIXTURE_OBJECT}
    COMMAND ${Python3_EXECUTABLE} ${BKNATIVE_FIXTURE_SCRIPT}
      ${BKNATIVE_FIXTURE_ELF} ${BKNATIVE_FIXTURE_HEADER}
    DEPENDS ${NUTTX_APPS_DIR}/system/bk7258/native-apps/breathing.c
            ${NUTTX_APPS_DIR}/system/bk7258/bk7258_native_abi.h
            ${BKNATIVE_FIXTURE_SCRIPT}
    VERBATIM)
  add_custom_target(bk7258_native_fixture DEPENDS ${BKNATIVE_FIXTURE_HEADER})
  add_dependencies(apps bk7258_native_fixture)
  target_include_directories(apps PRIVATE ${BKNATIVE_FIXTURE_DIR}
                                          ${NUTTX_APPS_DIR}/system/bk7258)
  target_sources(apps PRIVATE bk7258_native_app_test.c)
endif()

if(CONFIG_TESTS_BK7258_CMOCKA)
  nuttx_add_application(
    NAME
    cmocka_bk7258_board_test
    PRIORITY
    ${CONFIG_TESTS_BK7258_CMOCKA_PRIORITY}
    STACKSIZE
    ${CONFIG_TESTS_BK7258_CMOCKA_STACKSIZE}
    MODULE
    ${CONFIG_TESTS_BK7258_CMOCKA}
    SRCS
    bk7258_board_test.c
    DEPENDS
    cmocka)
endif()

if(CONFIG_TESTS_BK7258_SMP_DIAG)
  nuttx_add_application(
    NAME
    bksmpdiag
    PRIORITY
    ${CONFIG_TESTS_BK7258_SMP_DIAG_PRIORITY}
    STACKSIZE
    ${CONFIG_TESTS_BK7258_SMP_DIAG_STACKSIZE}
    MODULE
    ${CONFIG_TESTS_BK7258_SMP_DIAG}
    SRCS
    bk7258_smp_diag.c)
endif()
