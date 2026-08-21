# Project-owned BK7258 BL1, built only into an explicit output directory.

.DEFAULT_GOAL := all

SOURCE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

ifeq ($(strip $(OUT)),)
$(error OUT is required)
endif
ifeq ($(strip $(MODE)),)
$(error MODE is required: direct or mcuboot)
endif
ifneq ($(filter $(MODE),direct mcuboot),$(MODE))
$(error MODE must be direct or mcuboot)
endif
ifeq ($(strip $(TOOLCHAIN)),)
$(error TOOLCHAIN is required)
endif
ifeq ($(strip $(PARTITION_HEADER)),)
$(error PARTITION_HEADER is required)
endif
ifeq ($(strip $(CONFIG_HEADER)),)
$(error CONFIG_HEADER is required)
endif

OUT := $(abspath $(OUT))
TOOLCHAIN := $(abspath $(TOOLCHAIN))
PARTITION_HEADER := $(abspath $(PARTITION_HEADER))
CONFIG_HEADER := $(abspath $(CONFIG_HEADER))

CC := $(TOOLCHAIN)/arm-none-eabi-gcc
OBJCOPY := $(TOOLCHAIN)/arm-none-eabi-objcopy
SIZE := $(TOOLCHAIN)/arm-none-eabi-size
NM := $(TOOLCHAIN)/arm-none-eabi-nm

ifeq ($(wildcard $(CC)),)
$(error missing compiler: $(CC))
endif
ifeq ($(wildcard $(PARTITION_HEADER)),)
$(error missing partition header: $(PARTITION_HEADER))
endif
ifeq ($(wildcard $(CONFIG_HEADER)),)
$(error missing BL1 config header: $(CONFIG_HEADER))
endif

CPUFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
CPPFLAGS := -I$(SOURCE) -I$(SOURCE)/../include \
            -I$(dir $(PARTITION_HEADER)) -include $(CONFIG_HEADER)
CFLAGS := $(CPUFLAGS) -Os -g -ffreestanding -fno-builtin -Wall -Wextra \
          -Werror -fdata-sections -ffunction-sections $(CPPFLAGS)
ASFLAGS := $(CPUFLAGS) -Os -g -ffreestanding $(CPPFLAGS)
LDFLAGS := $(CPUFLAGS) -T $(OUT)/bootloader.ld -nostdlib \
           -Wl,--gc-sections -Wl,--build-id=none -Wl,-Map=$(OUT)/bl.map

COMMON_OBJECTS := \
  $(OUT)/start.o \
  $(OUT)/boot_main.o \
  $(OUT)/boot_clock.o \
  $(OUT)/boot_runtime.o \
  $(OUT)/boot_flash.o \
  $(OUT)/boot_libc.o

OBJECTS := $(COMMON_OBJECTS)

ifeq ($(MODE),mcuboot)
ifeq ($(strip $(NUTTX_ROOT)),)
$(error NUTTX_ROOT is required for MODE=mcuboot)
endif
ifeq ($(strip $(BL1_KEY_SOURCE)),)
$(error BL1_KEY_SOURCE is required for MODE=mcuboot)
endif
NUTTX_ROOT := $(abspath $(NUTTX_ROOT))
BL1_KEY_SOURCE := $(abspath $(BL1_KEY_SOURCE))
MCUBOOT_ROOT := $(NUTTX_ROOT)/../apps/boot/mcuboot/mcuboot
TINYCRYPT_ROOT := $(MCUBOOT_ROOT)/ext/tinycrypt/lib
ifeq ($(wildcard $(BL1_KEY_SOURCE)),)
$(error missing build-local BL1 public key source: $(BL1_KEY_SOURCE))
endif
ifeq ($(wildcard $(TINYCRYPT_ROOT)/source/ecc.c),)
$(error missing official MCUboot tinycrypt source)
endif
CFLAGS += -I$(TINYCRYPT_ROOT)/include
OBJECTS += \
  $(OUT)/boot_sha256.o \
  $(OUT)/boot_bl1_manifest.o \
  $(OUT)/boot_bl1_boot_flag_core.o \
  $(OUT)/boot_bl1_handoff_core.o \
  $(OUT)/boot_bl1_policy.o \
  $(OUT)/boot_bl1_manifest_key.o \
  $(OUT)/tinycrypt_ecc.o \
  $(OUT)/tinycrypt_ecc_dsa.o
endif

.PHONY: all verify clean

all: $(OUT)/bl.elf $(OUT)/bl.bin verify

$(OUT):
	mkdir -p $@

$(OUT)/bootloader.ld: $(SOURCE)/bootloader.ld $(PARTITION_HEADER) \
                       $(CONFIG_HEADER) | $(OUT)
	$(CC) -E -P -x c -D__LINKER__ $(CPPFLAGS) $< -o $@

$(OUT)/start.o: $(SOURCE)/start.S $(PARTITION_HEADER) $(CONFIG_HEADER) | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/%.o: $(SOURCE)/%.c $(PARTITION_HEADER) $(CONFIG_HEADER) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/boot_bl1_manifest_key.o: $(BL1_KEY_SOURCE) $(PARTITION_HEADER) \
                                $(CONFIG_HEADER) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/tinycrypt_ecc.o: $(TINYCRYPT_ROOT)/source/ecc.c $(CONFIG_HEADER) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/tinycrypt_ecc_dsa.o: $(TINYCRYPT_ROOT)/source/ecc_dsa.c \
                            $(CONFIG_HEADER) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/bl.elf: $(OBJECTS) $(OUT)/bootloader.ld
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@
	$(SIZE) $@

$(OUT)/bl.bin: $(OUT)/bl.elf
	$(OBJCOPY) -O binary $< $@

verify: $(OUT)/bl.elf $(OUT)/bl.bin
	@for symbol in Reset_Handler c_main fal_partition_table; do \
	  $(NM) -n $(OUT)/bl.elf | grep -qw "$$symbol" || { \
	    echo "BL1 missing required symbol: $$symbol" >&2; exit 1; \
	  }; \
	done
ifeq ($(MODE),mcuboot)
	@$(NM) -n $(OUT)/bl.elf | grep -qw bk7258_bl1_manifest_verify_buffer || { \
	  echo "signed BL1 lacks its Manifest verifier" >&2; exit 1; \
	}
endif

clean:
	$(RM) $(OBJECTS) $(OUT)/bootloader.ld $(OUT)/bl.elf $(OUT)/bl.bin \
	      $(OUT)/bl.map
