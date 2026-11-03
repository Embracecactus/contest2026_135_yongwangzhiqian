############################################################################
# SPDX-License-Identifier: Apache-2.0
############################################################################

include $(APPDIR)/Make.defs

ifneq ($(CONFIG_TESTS_BK7258_CMOCKA),)
PROGNAME  = cmocka_bk7258_board_test
PRIORITY  = $(CONFIG_TESTS_BK7258_CMOCKA_PRIORITY)
STACKSIZE = $(CONFIG_TESTS_BK7258_CMOCKA_STACKSIZE)
MODULE    = $(CONFIG_TESTS_BK7258_CMOCKA)
MAINSRC   = bk7258_board_test.c
endif

ifneq ($(CONFIG_TESTS_BK7258_SMP_DIAG),)
PROGNAME  += bksmpdiag
PRIORITY  += $(CONFIG_TESTS_BK7258_SMP_DIAG_PRIORITY)
STACKSIZE += $(CONFIG_TESTS_BK7258_SMP_DIAG_STACKSIZE)
MODULE     = $(CONFIG_TESTS_BK7258_SMP_DIAG)
MAINSRC   += bk7258_smp_diag.c
endif

ifeq ($(CONFIG_TESTS_BK7258_RPMSG_EXEC),y)
CSRCS += bk7258_drivercheck.c
endif

include $(APPDIR)/Application.mk
