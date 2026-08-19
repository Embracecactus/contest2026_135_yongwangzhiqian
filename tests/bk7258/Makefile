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

# Real implementation, relative to this repo-level tests/bk7258 directory.
REAL_C  := ../../board/bk7258/chip/common/bk7258_rptun_mbox.c
REAL_H  := ../../board/bk7258/chip/common/bk7258_rptun_mbox.h

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

.PHONY: all clean bl1 bl2 ap run-bl1 run-bl2 run-ap

all: $(TARGET) $(POLICY_TARGET) $(PM_ACTIVITY_TARGET) \
	$(RPTUN_CORE_CP_TARGET) $(RPTUN_CORE_AP_TARGET) bl1 bl2 ap

############################################################################
# BL1 framework: mock_reg32 MMIO windows, XIP flash window, tinycrypt mock.
############################################################################

FRAMEWORK := framework
BL1_MODULES := modules/bl1
BOOTLOADER := ../../board/bk7258/bootloader
BOARD_INC  := ../../board/bk7258/include
CHIP_INC   := ../../board/bk7258/chip/include
PATCH      := python3 $(FRAMEWORK)/patch.py
CMOCKA     := $(shell pkg-config --cflags --libs cmocka)

BL1_INC := -I $(FRAMEWORK) -I mocks -I mocks/tinycrypt \
	-I $(BL1_MODULES) -I $(BOOTLOADER) -I $(BOARD_INC) -I $(CHIP_INC) \
	-I $(BUILD)/bl1
BL1_CFLAGS := -std=gnu11 -Wall -Wextra -O2 -g -DBK7258_BL1_CONSOLE_UART=1

# Framework objects shared by every BL1 test binary.
FRAMEWORK_OBJS := $(BUILD)/mock_reg32.o $(BUILD)/mock_flash.o \
	$(BUILD)/mock_tinycrypt.o

$(BUILD)/mock_reg32.o: $(FRAMEWORK)/mock_reg32.c $(FRAMEWORK)/mock_reg32.h
	$(CC) $(BL1_CFLAGS) -I $(FRAMEWORK) -c $< -o $@

$(BUILD)/mock_flash.o: $(FRAMEWORK)/mock_flash.c $(FRAMEWORK)/mock_flash.h
	$(CC) $(BL1_CFLAGS) -I $(FRAMEWORK) -c $< -o $@

$(BUILD)/mock_tinycrypt.o: $(FRAMEWORK)/mock_tinycrypt.c \
	$(FRAMEWORK)/mock_tinycrypt.h mocks/tinycrypt/ecc.h
	$(CC) $(BL1_CFLAGS) -I $(FRAMEWORK) -I mocks -c $< -o $@

$(BUILD)/mock_boot_jump.o: $(FRAMEWORK)/mock_boot_jump.c \
	$(FRAMEWORK)/mock_boot_jump.h
	$(CC) $(BL1_CFLAGS) -I $(FRAMEWORK) -c $< -o $@

# Throwaway patched copies of the real bootloader sources.  The real files
# are never modified.  -include mock_reg32.h injects the MMIO redirect into
# the patched translation unit.
$(BUILD)/bl1/%.c: $(BOOTLOADER)/%.c $(FRAMEWORK)/patch.py | $(BUILD)/bl1
	$(PATCH) $* $< $@

$(BUILD)/bl1/boot_bl1_manifest.c: $(BOOTLOADER)/boot_bl1_manifest.c \
	$(FRAMEWORK)/patch.py | $(BUILD)/bl1
	$(PATCH) bl1_manifest $< $@

$(BUILD)/bl1/boot_wdt.h: $(BOOTLOADER)/boot_wdt.h $(FRAMEWORK)/patch.py \
	| $(BUILD)/bl1
	$(PATCH) boot_wdt $< $@

$(BUILD)/bl1:
	mkdir -p $(BUILD)/bl1

BL1_PATCHED := $(BUILD)/bl1/boot_flash.c $(BUILD)/bl1/boot_clock.c \
	$(BUILD)/bl1/boot_runtime.c $(BUILD)/bl1/boot_bl1_manifest.c \
	$(BUILD)/bl1/boot_wdt.h

# boot_flash.c includes "../boot_wdt.h" - point it at the patched copy.
$(BUILD)/bl1/boot_flash.c: $(BOOTLOADER)/boot_flash.c $(FRAMEWORK)/patch.py \
	| $(BUILD)/bl1
	$(PATCH) boot_flash $< $@

# Helper: compile a patched .c with the framework headers injected.
define bl1_compile
	$(CC) $(BL1_CFLAGS) -include mock_reg32.h -include mock_flash.h \
	  $(BL1_INC) -c $< -o $@
endef

$(BUILD)/bl1/%.o: $(BUILD)/bl1/%.c
	$(bl1_compile)

# boot_runtime.c / boot_flash.c pull boot_wdt.h in by quote-include; make
# sure the patched header copy exists before compiling them.
$(BUILD)/bl1/boot_runtime.o: $(BUILD)/bl1/boot_wdt.h
$(BUILD)/bl1/boot_flash.o: $(BUILD)/bl1/boot_wdt.h

# Pure modules (no patch needed).
$(BUILD)/bl1/boot_libc.o: $(BOOTLOADER)/boot_libc.c
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

$(BUILD)/bl1/boot_sha256.o: $(BOOTLOADER)/boot_sha256.c \
	$(BOOTLOADER)/boot_sha256.h
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

$(BUILD)/bl1/boot_bl1_policy.o: $(BOOTLOADER)/boot_bl1_policy.c \
	$(BOOTLOADER)/boot_bl1_policy.h $(BOOTLOADER)/boot_bl1_boot_flag_core.h
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

$(BUILD)/bl1/boot_bl1_boot_flag_core.o: \
	$(BOOTLOADER)/boot_bl1_boot_flag_core.c
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

$(BUILD)/bl1/boot_bl1_manifest_key.o: $(BOOTLOADER)/boot_bl1_manifest_key.c
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

$(BUILD)/bl1/test_%.o: $(BL1_MODULES)/test_%.c
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

# Test binaries.
$(BUILD)/test_boot_libc: $(BUILD)/bl1/boot_libc.o \
	$(BUILD)/bl1/test_boot_libc.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_boot_sha256: $(BUILD)/bl1/boot_sha256.o \
	$(BUILD)/bl1/test_boot_sha256.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_boot_flash: $(BUILD)/bl1/boot_flash.o \
	$(BUILD)/bl1/test_boot_flash.o $(BUILD)/mock_reg32.o \
	$(BUILD)/mock_flash.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_boot_clock: $(BUILD)/bl1/boot_clock.o \
	$(BUILD)/bl1/test_boot_clock.o $(BUILD)/mock_reg32.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_boot_runtime: $(BUILD)/bl1/boot_runtime.o \
	$(BUILD)/bl1/test_boot_runtime.o $(BUILD)/mock_reg32.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_bl1_manifest: $(BUILD)/bl1/boot_bl1_manifest.o \
	$(BUILD)/bl1/boot_bl1_policy.o \
	$(BUILD)/bl1/boot_bl1_boot_flag_core.o \
	$(BUILD)/bl1/boot_bl1_manifest_key.o $(BUILD)/bl1/boot_sha256.o \
	$(BUILD)/bl1/test_bl1_manifest.o $(BUILD)/mock_reg32.o \
	$(BUILD)/mock_flash.o $(BUILD)/mock_tinycrypt.o
	$(CC) $^ -o $@ $(CMOCKA)

BL1_TARGETS := $(BUILD)/test_boot_libc $(BUILD)/test_boot_sha256 \
	$(BUILD)/test_boot_flash $(BUILD)/test_boot_clock \
	$(BUILD)/test_boot_runtime $(BUILD)/test_bl1_manifest

bl1: $(BL1_TARGETS)

.PHONY: run-bl1
run-bl1: bl1
	./$(BUILD)/test_boot_libc
	./$(BUILD)/test_boot_sha256
	./$(BUILD)/test_boot_flash
	./$(BUILD)/test_boot_clock
	./$(BUILD)/test_boot_runtime
	./$(BUILD)/test_bl1_manifest

############################################################################
# BL2/MCUboot board modules: security counter, flash-map backend, key hook.
############################################################################

BL2_MODULES := modules/bl2
BL2_DIR     := $(BUILD)/bl2
BL2_INC := -I $(BL2_DIR) -I $(BUILD) -I $(FRAMEWORK) -I mocks \
	-I $(BL2_MODULES) -I $(BOOTLOADER) -I $(BOOTLOADER)/bl2/include \
	-I $(BOARD_INC) -I $(CHIP_INC)
BL2_CFLAGS := -std=gnu11 -Wall -Wextra -O2 -g \
	-DBK7258_BL2_SECURITY_COUNTER_FLOOR=7

# The board flash_map backend pulls ../boot_wdt.h in by quote-include; the
# patched header lives in $(BUILD) and is shared with BL1 modules.
$(BUILD)/boot_wdt.h: $(BOOTLOADER)/boot_wdt.h $(FRAMEWORK)/patch.py \
	| $(BUILD)
	$(PATCH) boot_wdt $< $@

$(BL2_DIR):
	mkdir -p $(BL2_DIR)

$(BL2_DIR)/bk7258_bl2_security_cnt.c: \
	$(BOOTLOADER)/bl2/bk7258_bl2_security_cnt.c $(FRAMEWORK)/patch.py \
	| $(BL2_DIR)
	$(PATCH) bl2_security_cnt $< $@

$(BL2_DIR)/bk7258_bl2_flash_map.c: \
	$(BOOTLOADER)/bl2/bk7258_bl2_flash_map.c $(FRAMEWORK)/patch.py \
	| $(BL2_DIR)
	$(PATCH) bl2_flash_map $< $@

$(BL2_DIR)/bk7258_bl2_mcuboot_boot.c: \
	$(BOOTLOADER)/bl2/bk7258_bl2_mcuboot_boot.c $(FRAMEWORK)/patch.py \
	| $(BL2_DIR)
	$(PATCH) bl2_mcuboot_boot $< $@

# Patched BL2 modules are compiled with the framework MMIO injected.
$(BL2_DIR)/%.o: $(BL2_DIR)/%.c | $(BUILD)/boot_wdt.h $(BL2_DIR)
	$(CC) $(BL2_CFLAGS) -include mock_reg32.h -include mock_flash.h \
	  $(BL2_INC) -c $< -o $@

# Pure module: no MMIO, no patch.
$(BL2_DIR)/bk7258_bl2_keys.o: $(BOOTLOADER)/bl2/bk7258_bl2_keys.c \
	mocks/bootutil/sign_key.h | $(BL2_DIR)
	$(CC) $(BL2_CFLAGS) $(BL2_INC) -c $< -o $@

# The mcuboot handoff module additionally needs the boot-jump hook injected.
# Its include path deliberately omits $(BOOTLOADER)/bl2/include: that dir
# carries a bare-metal assert.h (no static_assert) which would shadow the
# host <assert.h> pulled in by the real image-layout header.
BL2_BOOT_INC := -I $(BL2_DIR) -I $(BUILD) -I $(FRAMEWORK) -I mocks \
	-I $(BL2_MODULES) -I $(BOOTLOADER) -I $(BOARD_INC) -I $(CHIP_INC)
$(BL2_DIR)/bk7258_bl2_mcuboot_boot.o: $(BL2_DIR)/bk7258_bl2_mcuboot_boot.c \
	$(FRAMEWORK)/mock_boot_jump.h | $(BL2_DIR)
	$(CC) $(BL2_CFLAGS:-g=) -include mock_reg32.h -include mock_flash.h \
	  -include mock_boot_jump.h $(BL2_BOOT_INC) -c $< -o $@

$(BL2_DIR)/test_%.o: $(BL2_MODULES)/test_%.c
	$(CC) $(BL2_CFLAGS) $(BL2_BOOT_INC) -c $< -o $@

$(BUILD)/test_bl2_security_cnt: $(BL2_DIR)/bk7258_bl2_security_cnt.o \
	$(BL2_DIR)/test_bl2_security_cnt.o $(BUILD)/mock_reg32.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_bl2_flash_map: $(BL2_DIR)/bk7258_bl2_flash_map.o \
	$(BL2_DIR)/test_bl2_flash_map.o $(BUILD)/mock_reg32.o \
	$(BUILD)/mock_flash.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_bl2_keys: $(BL2_DIR)/bk7258_bl2_keys.o \
	$(BL2_DIR)/test_bl2_keys.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_bl2_mcuboot_boot: $(BL2_DIR)/bk7258_bl2_mcuboot_boot.o \
	$(BL2_DIR)/test_bl2_mcuboot_boot.o $(BUILD)/mock_reg32.o \
	$(BUILD)/mock_flash.o $(BUILD)/mock_boot_jump.o
	$(CC) $^ -o $@ $(CMOCKA)

BL2_TARGETS := $(BUILD)/test_bl2_security_cnt $(BUILD)/test_bl2_flash_map \
	$(BUILD)/test_bl2_keys $(BUILD)/test_bl2_mcuboot_boot

bl2: $(BL2_TARGETS)

.PHONY: run-bl2
run-bl2: bl2
	./$(BUILD)/test_bl2_security_cnt
	./$(BUILD)/test_bl2_flash_map
	./$(BUILD)/test_bl2_keys
	./$(BUILD)/test_bl2_mcuboot_boot

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

$(BUILD)/bk7258_rptun.c: ../../board/bk7258/chip/common/bk7258_rptun.c | $(BUILD)
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
                 ../../board/bk7258/bootloader/boot_bl1_boot_flag_core.c \
                 ../../board/bk7258/bootloader/boot_bl1_boot_flag_core.h \
                 ../../board/bk7258/bootloader/boot_bl1_handoff_core.c \
                 ../../board/bk7258/bootloader/boot_bl1_handoff_core.h \
                 ../../board/bk7258/bootloader/boot_bl1_policy.c \
                 ../../board/bk7258/bootloader/boot_bl1_policy.h \
                 ../../board/bk7258/bootloader/boot_bl1_manifest.h
	$(CC) $(POLICY_CFLAGS) \
	  test_boot_bl1_policy.c \
	  ../../board/bk7258/bootloader/boot_bl1_boot_flag_core.c \
	  ../../board/bk7258/bootloader/boot_bl1_handoff_core.c \
	  ../../board/bk7258/bootloader/boot_bl1_policy.c \
	  -I ../../board/bk7258/bootloader \
	  -I ../../board/bk7258/include \
	  -o $@

$(PM_ACTIVITY_TARGET): test_bk7258_pm_activity.c \
                      ../../board/bk7258/chip/common/bk7258_pm_activity.c \
                      ../../board/bk7258/chip/common/bk7258_pm_activity.h
	$(CC) $(PM_ACTIVITY_CFLAGS) \
	  test_bk7258_pm_activity.c \
	  ../../board/bk7258/chip/common/bk7258_pm_activity.c \
	  -I ../../board/bk7258/chip/common \
	  -o $@

$(RPTUN_CORE_CP_TARGET): $(BUILD)/bk7258_rptun.c \
                         test_bk7258_rptun_core.c
	$(CC) $(RPTUN_CORE_CFLAGS) \
	  test_bk7258_rptun_core.c \
	  $(BUILD)/bk7258_rptun.c \
	  -I $(MOCKS) -I ../../board/bk7258/chip/common \
	  -o $@

$(RPTUN_CORE_AP_TARGET): $(BUILD)/bk7258_rptun.c \
                         test_bk7258_rptun_core.c
	$(CC) $(RPTUN_CORE_CFLAGS) -DTEST_BK7258_RPTUN_AP \
	  test_bk7258_rptun_core.c \
	  $(BUILD)/bk7258_rptun.c \
	  -I $(MOCKS) -I ../../board/bk7258/chip/common \
	  -o $@

############################################################################
# AP board helpers.  bk7258_jpeg_decoder.c is compiled UNMODIFIED: it has no
# bare-metal MMIO or ARM barriers.  -no-pie keeps every static/allocated
# address below 4 GiB, which the driver requires (its SDK talks to 32-bit
# JPEG registers and rejects out-of-range caller buffers).
############################################################################

AP_MODULES := modules/ap
AP_DIR     := $(BUILD)/ap
AP_INC := -I $(FRAMEWORK) -I mocks -I $(AP_MODULES) -I $(CHIP_INC)
AP_CFLAGS := -std=gnu11 -Wall -Wextra -O2 -g -no-pie

$(AP_DIR):
	mkdir -p $(AP_DIR)

$(BUILD)/mock_nuttx_ap.o: mocks/mock_nuttx_ap.c mocks/nuttx/cache.h \
	mocks/nuttx/kmalloc.h mocks/nuttx/mutex.h
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(BUILD)/mock_sdk_jpeg.o: $(FRAMEWORK)/mock_sdk_jpeg.c \
	$(FRAMEWORK)/mock_sdk_jpeg.h
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(AP_DIR)/bk7258_jpeg_decoder.o: ../../board/bk7258/chip/ap/bk7258_jpeg_decoder.c \
	$(FRAMEWORK)/mock_sdk_jpeg.h mocks/mock_nuttx_ap.c | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(AP_DIR)/test_bk7258_jpeg_decoder.o: $(AP_MODULES)/test_bk7258_jpeg_decoder.c \
	$(FRAMEWORK)/mock_sdk_jpeg.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) -Wno-unused-parameter $(AP_INC) -c $< -o $@

$(BUILD)/test_bk7258_jpeg_decoder: $(AP_DIR)/bk7258_jpeg_decoder.o \
	$(AP_DIR)/test_bk7258_jpeg_decoder.o $(BUILD)/mock_nuttx_ap.o \
	$(BUILD)/mock_sdk_jpeg.o
	$(CC) $(AP_CFLAGS) $^ -o $@ $(CMOCKA) -lpthread

# bk7258_yuv_h264.c: same zero-patch template.  The suite mock TU
# implements the nxsem/clock shims, the CPU1 interrupt-route ABI and the
# media-root helper, and mirrors the DMA/H.264/YUV-BUF driver bundle.
# The yuv model needs the deterministic counting-semaphore + controllable
# clock variants of the nuttx shims; -I mocks/nuttx_yuv (before -I mocks)
# selects them for the three yuv TUs only, leaving the rptun/jpeg builds
# on the shared POSIX-sem based mocks.
MOCK_YUV_INC := -I mocks/nuttx_yuv

$(BUILD)/mock_sdk_yuv_h264.o: $(FRAMEWORK)/mock_sdk_yuv_h264.c \
	$(FRAMEWORK)/mock_sdk_yuv_h264.h mocks/driver/dma.h \
	mocks/driver/h264.h mocks/driver/yuv_buf.h mocks/nuttx_yuv/nuttx/semaphore.h \
	mocks/nuttx_yuv/nuttx/clock.h
	$(CC) $(AP_CFLAGS) $(MOCK_YUV_INC) $(AP_INC) -c $< -o $@

$(AP_DIR)/bk7258_yuv_h264.o: ../../board/bk7258/chip/ap/bk7258_yuv_h264.c \
	$(FRAMEWORK)/mock_sdk_yuv_h264.h mocks/driver/dma.h \
	mocks/driver/h264.h mocks/driver/yuv_buf.h mocks/nuttx_yuv/nuttx/semaphore.h \
	mocks/nuttx_yuv/nuttx/clock.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(MOCK_YUV_INC) $(AP_INC) -c $< -o $@

$(AP_DIR)/test_bk7258_yuv_h264.o: $(AP_MODULES)/test_bk7258_yuv_h264.c \
	$(FRAMEWORK)/mock_sdk_yuv_h264.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) -Wno-unused-parameter $(MOCK_YUV_INC) $(AP_INC) -c $< -o $@

$(BUILD)/test_bk7258_yuv_h264: $(AP_DIR)/bk7258_yuv_h264.o \
	$(AP_DIR)/test_bk7258_yuv_h264.o $(BUILD)/mock_nuttx_ap.o \
	$(BUILD)/mock_sdk_yuv_h264.o
	$(CC) $(AP_CFLAGS) $^ -o $@ $(CMOCKA) -lpthread

# bk7258_scale_rotate.c: same template.  The suite mock TU implements the
# nxsem/clock shims and mirrors the scale (hw_scale) + rotator (rott)
# driver bundle plus the CPU1 interrupt-route ABI.  Two mechanical patches
# apply to the throwaway copy (framework/patch.py): the ARM dsb barrier
# and the Scale1 write-burst register poke (routed to mock_reg32); the
# relative board include is re-pointed at the real header via CHIP_INC.
$(BUILD)/bk7258_scale_rotate.c: ../../board/bk7258/chip/ap/bk7258_scale_rotate.c \
	framework/patch.py | $(BUILD)
	python3 framework/patch.py scale_rotate $< $@

$(BUILD)/mock_sdk_scale_rotate.o: $(FRAMEWORK)/mock_sdk_scale_rotate.c \
	$(FRAMEWORK)/mock_sdk_scale_rotate.h mocks/driver/hw_scale.h \
	mocks/driver/rott_driver.h mocks/nuttx_yuv/nuttx/semaphore.h \
	mocks/nuttx_yuv/nuttx/clock.h
	$(CC) $(AP_CFLAGS) $(MOCK_YUV_INC) $(AP_INC) -c $< -o $@

$(AP_DIR)/bk7258_scale_rotate.o: $(BUILD)/bk7258_scale_rotate.c \
	$(FRAMEWORK)/mock_sdk_scale_rotate.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(MOCK_YUV_INC) $(AP_INC) -I $(CHIP_INC) -c $< -o $@

$(AP_DIR)/test_bk7258_scale_rotate.o: $(AP_MODULES)/test_bk7258_scale_rotate.c \
	$(FRAMEWORK)/mock_sdk_scale_rotate.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) -Wno-unused-parameter $(MOCK_YUV_INC) $(AP_INC) \
		-I $(CHIP_INC) -c $< -o $@

$(BUILD)/test_bk7258_scale_rotate: $(AP_DIR)/bk7258_scale_rotate.o \
	$(AP_DIR)/test_bk7258_scale_rotate.o $(BUILD)/mock_nuttx_ap.o \
	$(BUILD)/mock_sdk_scale_rotate.o $(BUILD)/mock_reg32.o
	$(CC) $(AP_CFLAGS) $^ -o $@ $(CMOCKA) -lpthread

# bk7258_irda.c: board-owned register-level NEC decoder (CP-owned).  The
# throwaway copy is patched by framework/patch.py so the direct IRDA
# register accessors route through mock_reg32; the SDK surface
# (gpio_dev_map, bk_int_isr_register, register_driver) is programmable via
# framework/mock_sdk_irda, and the key-debounce wdog fires explicitly via
# framework/mock_wdog.
$(BUILD)/bk7258_irda.c: ../../board/bk7258/chip/cp/bk7258_irda.c \
	framework/patch.py | $(BUILD)
	python3 framework/patch.py irda $< $@

$(BUILD)/mock_sdk_irda.o: $(FRAMEWORK)/mock_sdk_irda.c \
	$(FRAMEWORK)/mock_sdk_irda.h mocks/driver/gpio.h \
	mocks/driver/gpio_types.h mocks/driver/int.h mocks/driver/int_types.h \
	mocks/nuttx/fs/fs.h
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(BUILD)/mock_wdog.o: $(FRAMEWORK)/mock_wdog.c mocks/nuttx/wdog.h
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(BUILD)/mock_sem.o: $(FRAMEWORK)/mock_sem.c mocks/nuttx/semaphore.h
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(AP_DIR)/bk7258_irda.o: $(BUILD)/bk7258_irda.c \
	$(FRAMEWORK)/mock_sdk_irda.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(AP_INC) -I $(CHIP_INC) \
		-DCONFIG_BK7258_IRDA=1 -c $< -o $@

$(AP_DIR)/test_bk7258_irda.o: $(AP_MODULES)/test_bk7258_irda.c \
	$(FRAMEWORK)/mock_sdk_irda.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) -Wno-unused-parameter $(AP_INC) \
		-I $(CHIP_INC) -c $< -o $@

$(BUILD)/test_bk7258_irda: $(AP_DIR)/bk7258_irda.o \
	$(AP_DIR)/test_bk7258_irda.o $(BUILD)/mock_nuttx_ap.o \
	$(BUILD)/mock_sdk_irda.o $(BUILD)/mock_wdog.o $(BUILD)/mock_sem.o \
	$(BUILD)/mock_reg32.o
	$(CC) $(AP_CFLAGS) $^ -o $@ $(CMOCKA) -lpthread

# bk7258_can.c: zero-patch template -- no bare-metal MMIO or ARM
# barriers.  The suite mock TU provides its own real-blocking nxsem_*
# (mocks/nuttx_can/nuttx/semaphore.h, selected by MOCK_CAN_INC) and the
# real-pthread kthread surface; the rx thread drains the SDK FIFO mock
# and the upper-half records.  The real arch/chip public headers are
# re-pointed at the mock variants via MOCKS first in AP_INC.
MOCK_CAN_INC := -I mocks/nuttx_can

$(BUILD)/mock_sdk_can.o: $(FRAMEWORK)/mock_sdk_can.c \
	$(FRAMEWORK)/mock_sdk_can.h mocks/nuttx_can/nuttx/semaphore.h \
	mocks/nuttx/can/can.h mocks/driver/hal/hal_can_types.h
	$(CC) $(AP_CFLAGS) $(MOCK_CAN_INC) $(AP_INC) -c $< -o $@

$(AP_DIR)/bk7258_can.o: ../../board/bk7258/chip/ap/bk7258_can.c \
	$(FRAMEWORK)/mock_sdk_can.h mocks/nuttx_can/nuttx/semaphore.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(MOCK_CAN_INC) $(AP_INC) -c $< -o $@

$(AP_DIR)/test_bk7258_can.o: $(AP_MODULES)/test_bk7258_can.c \
	$(FRAMEWORK)/mock_sdk_can.h mocks/nuttx_can/nuttx/semaphore.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) -Wno-unused-parameter $(MOCK_CAN_INC) $(AP_INC) \
		-c $< -o $@

$(BUILD)/test_bk7258_can: $(AP_DIR)/bk7258_can.o \
	$(AP_DIR)/test_bk7258_can.o $(BUILD)/mock_nuttx_ap.o \
	$(BUILD)/mock_sdk_can.o
	$(CC) $(AP_CFLAGS) $^ -o $@ $(CMOCKA) -lpthread

AP_TARGETS := $(BUILD)/test_bk7258_jpeg_decoder \
	$(BUILD)/test_bk7258_yuv_h264 \
	$(BUILD)/test_bk7258_scale_rotate \
	$(BUILD)/test_bk7258_can \
	$(BUILD)/test_bk7258_irda

ap: $(AP_TARGETS)

.PHONY: run-ap
run-ap: ap
	./$(BUILD)/test_bk7258_jpeg_decoder
	./$(BUILD)/test_bk7258_yuv_h264
	./$(BUILD)/test_bk7258_scale_rotate
	./$(BUILD)/test_bk7258_can
	./$(BUILD)/test_bk7258_irda

clean:
	rm -rf $(BUILD)
