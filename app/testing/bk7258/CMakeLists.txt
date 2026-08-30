# ##############################################################################
# SPDX-License-Identifier: Apache-2.0
# ##############################################################################

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
