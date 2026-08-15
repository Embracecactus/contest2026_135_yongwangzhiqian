############################################################################
# Host-side unit-test build for the mailbox transport and BL1 policy cores.
#
# The REAL implementation is compiled unmodified, except that the ARM-only
# "__asm volatile ("dmb sy" ::: "memory");" barrier is neutralized in a
# *throwaway copy* (the real source file is never touched).  Everything else
# is mocked under mocks/: the Beken SDK mailbox transport, the AMP boot
# state, and the NuttX sem/kthread/spinlock/clock APIs.  The transport is
# controllable from the test so the notify coalescing / -EAGAIN retry /
# lost-TX-complete paths are exercised deterministically on the host.
############################################################################

CC      ?= gcc
CFLAGS  := -std=gnu11 -pthread -Wall -Wextra -O2
MOCKS   := mocks
BUILD   := build

# Real implementation, relative to this tests/ directory.
REAL_C  := ../chip/common/bk7258_rptun_mbox.c
REAL_H  := ../chip/common/bk7258_rptun_mbox.h

TARGET  := $(BUILD)/test_bk7258_rptun_mbox
POLICY_TARGET := $(BUILD)/test_boot_bl1_policy
PM_ACTIVITY_TARGET := $(BUILD)/test_bk7258_pm_activity
RPTUN_CORE_CP_TARGET := $(BUILD)/test_bk7258_rptun_core_cp
RPTUN_CORE_AP_TARGET := $(BUILD)/test_bk7258_rptun_core_ap
POLICY_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g \
	-fsanitize=address,undefined -fno-omit-frame-pointer
PM_ACTIVITY_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2
RPTUN_CORE_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 \
	-DTEST_BK7258_RPTUN_CORE

.PHONY: all clean

all: $(TARGET) $(POLICY_TARGET) $(PM_ACTIVITY_TARGET) \
	$(RPTUN_CORE_CP_TARGET) $(RPTUN_CORE_AP_TARGET)

$(BUILD):
	mkdir -p $(BUILD)

# Throwaway copy of the REAL implementation with the ARM "dmb sy" barrier
# neutralized (host x86 has no such instruction).  The real file is NOT
# modified.
$(BUILD)/bk7258_rptun_mbox.c: $(REAL_C) | $(BUILD)
	cp $(REAL_C) $@
	sed -i 's|__asm volatile ("dmb sy".*|__asm__ volatile ("" ::: "memory");|g' $@

$(BUILD)/bk7258_rptun_mbox.h: $(REAL_H) | $(BUILD)
	cp $(REAL_H) $@

$(BUILD)/bk7258_rptun.c: ../chip/common/bk7258_rptun.c | $(BUILD)
	cp $< $@
	sed -i 's|__asm volatile ("dmb sy.*|__asm__ volatile ("" ::: "memory");|g' $@

$(TARGET): $(BUILD)/bk7258_rptun_mbox.c $(BUILD)/bk7258_rptun_mbox.h \
           test_bk7258_rptun_mbox.c mocks/mock_sdk.c
	$(CC) $(CFLAGS) \
	  test_bk7258_rptun_mbox.c \
	  mocks/mock_sdk.c \
	  $(BUILD)/bk7258_rptun_mbox.c \
	  -I $(MOCKS) -I $(BUILD) \
	  -o $@ -lpthread

$(POLICY_TARGET): test_boot_bl1_policy.c \
                 ../bootloader/boot_bl1_boot_flag_core.c \
                 ../bootloader/boot_bl1_boot_flag_core.h \
                 ../bootloader/boot_bl1_handoff_core.c \
                 ../bootloader/boot_bl1_handoff_core.h \
                 ../bootloader/boot_bl1_policy.c \
                 ../bootloader/boot_bl1_policy.h \
                 ../bootloader/boot_bl1_manifest.h
	$(CC) $(POLICY_CFLAGS) \
	  test_boot_bl1_policy.c \
	  ../bootloader/boot_bl1_boot_flag_core.c \
	  ../bootloader/boot_bl1_handoff_core.c \
	  ../bootloader/boot_bl1_policy.c \
	  -I ../bootloader \
	  -o $@

$(PM_ACTIVITY_TARGET): test_bk7258_pm_activity.c \
                      ../chip/common/bk7258_pm_activity.c \
                      ../chip/common/bk7258_pm_activity.h
	$(CC) $(PM_ACTIVITY_CFLAGS) \
	  test_bk7258_pm_activity.c \
	  ../chip/common/bk7258_pm_activity.c \
	  -I ../chip/common \
	  -o $@

$(RPTUN_CORE_CP_TARGET): $(BUILD)/bk7258_rptun.c \
                         test_bk7258_rptun_core.c
	$(CC) $(RPTUN_CORE_CFLAGS) \
	  test_bk7258_rptun_core.c \
	  $(BUILD)/bk7258_rptun.c \
	  -I $(MOCKS) -I ../chip/common \
	  -o $@

$(RPTUN_CORE_AP_TARGET): $(BUILD)/bk7258_rptun.c \
                         test_bk7258_rptun_core.c
	$(CC) $(RPTUN_CORE_CFLAGS) -DTEST_BK7258_RPTUN_AP \
	  test_bk7258_rptun_core.c \
	  $(BUILD)/bk7258_rptun.c \
	  -I $(MOCKS) -I ../chip/common \
	  -o $@

clean:
	rm -rf $(BUILD)
