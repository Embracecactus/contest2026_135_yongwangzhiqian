############################################################################
# contest2026_135_yongwangzhiqian/board/bk7258_t5ai/src/Makefile
#
# SPDX-License-Identifier: Apache-2.0
############################################################################

include $(TOPDIR)/Make.defs

BK7258_CHIP_DIR := $(TOPDIR)$(DELIM)arch$(DELIM)$(CONFIG_ARCH)$(DELIM)src$(DELIM)chip
CFLAGS += ${INCDIR_PREFIX}$(BK7258_CHIP_DIR)$(DELIM)common
CFLAGS += ${INCDIR_PREFIX}$(BK7258_CHIP_DIR)$(DELIM)cp

CSRCS = bk7258_bringup.c

include $(TOPDIR)/boards/Board.mk
