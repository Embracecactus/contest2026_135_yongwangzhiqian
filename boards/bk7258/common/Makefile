############################################################################
# Beken BK7258 common board archive
#
# SPDX-License-Identifier: Apache-2.0
############################################################################

include $(TOPDIR)/Make.defs

include board/Make.defs
include src/Make.defs

DEPPATH += --dep-path board
DEPPATH += --dep-path src
VPATH += :board
VPATH += :src

include $(TOPDIR)/boards/Board.mk
