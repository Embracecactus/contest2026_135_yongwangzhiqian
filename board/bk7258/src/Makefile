############################################################################
# contest2026_135_yongwangzhiqian/board/bk7258/src/Makefile
#
# SPDX-License-Identifier: Apache-2.0
############################################################################

include $(TOPDIR)/Make.defs

BK7258_CHIP_DIR := $(TOPDIR)$(DELIM)arch$(DELIM)$(CONFIG_ARCH)$(DELIM)src$(DELIM)chip
CFLAGS += ${INCDIR_PREFIX}$(BK7258_CHIP_DIR)$(DELIM)common
CFLAGS += ${INCDIR_PREFIX}$(BK7258_CHIP_DIR)$(DELIM)cp
VPATH += $(BK7258_BOARD_VARIANT_DIR)$(DELIM)src
DEPPATH += --dep-path $(BK7258_BOARD_VARIANT_DIR)$(DELIM)src

CSRCS = bk7258_appinit.c bk7258_boot.c bk7258_bringup.c
CSRCS += bk7258_platform.c

ifeq ($(CONFIG_BK7258_AP_CORE),y)
CSRCS += bk7258_board_bringup.c
endif

include $(TOPDIR)/boards/Board.mk
