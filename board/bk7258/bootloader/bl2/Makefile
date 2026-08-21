# Project-owned BK7258 BL2 over the pinned upstream MCUboot sources.

.DEFAULT_GOAL := all

SOURCE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

ifeq ($(strip $(OUT)),)
$(error OUT is required)
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
ifeq ($(strip $(MCUBOOT_ROOT)),)
$(error MCUBOOT_ROOT is required)
endif
ifeq ($(strip $(KEY_SOURCE)),)
$(error KEY_SOURCE is required)
endif

OUT := $(abspath $(OUT))
TOOLCHAIN := $(abspath $(TOOLCHAIN))
PARTITION_HEADER := $(abspath $(PARTITION_HEADER))
CONFIG_HEADER := $(abspath $(CONFIG_HEADER))
MCUBOOT_ROOT := $(abspath $(MCUBOOT_ROOT))
KEY_SOURCE := $(abspath $(KEY_SOURCE))

CC := $(TOOLCHAIN)/arm-none-eabi-gcc
OBJCOPY := $(TOOLCHAIN)/arm-none-eabi-objcopy
SIZE := $(TOOLCHAIN)/arm-none-eabi-size
NM := $(TOOLCHAIN)/arm-none-eabi-nm

BOOTUTIL := $(MCUBOOT_ROOT)/boot/bootutil
TINYCRYPT := $(MCUBOOT_ROOT)/ext/tinycrypt/lib
ASN1 := $(MCUBOOT_ROOT)/ext/mbedtls-asn1

ifeq ($(wildcard $(CC)),)
$(error missing compiler: $(CC))
endif
ifeq ($(wildcard $(PARTITION_HEADER)),)
$(error missing partition header: $(PARTITION_HEADER))
endif
ifeq ($(wildcard $(CONFIG_HEADER)),)
$(error missing BL2 config header: $(CONFIG_HEADER))
endif
ifeq ($(wildcard $(KEY_SOURCE)),)
$(error missing build-local MCUboot public key source: $(KEY_SOURCE))
endif
ifeq ($(wildcard $(BOOTUTIL)/src/loader.c),)
$(error missing pinned upstream MCUboot sources)
endif

CPUFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
CPPFLAGS := -I$(SOURCE) -I$(SOURCE)/include -I$(SOURCE)/../../chip/include \
            -I$(dir $(PARTITION_HEADER)) -I$(BOOTUTIL)/include \
            -I$(BOOTUTIL)/src -I$(TINYCRYPT)/include -I$(ASN1)/include \
            -include $(CONFIG_HEADER)
CFLAGS := $(CPUFLAGS) -Os -g -ffreestanding -fno-builtin \
          -fdata-sections -ffunction-sections -Wall -Wextra $(CPPFLAGS)
ASFLAGS := $(CPUFLAGS) -Os -g -ffreestanding $(CPPFLAGS)
LDFLAGS := $(CPUFLAGS) -nostdlib -Wl,--gc-sections -Wl,--build-id=none \
           -T $(OUT)/bl2.ld -Wl,-Map=$(OUT)/bl2.map

OBJECTS := \
  $(OUT)/bl2_start.o \
  $(OUT)/bk7258_bl2_main.o \
  $(OUT)/bk7258_bl2_pair_policy.o \
  $(OUT)/bk7258_bl2_flash_map.o \
  $(OUT)/bk7258_bl2_security_cnt.o \
  $(OUT)/loader.o \
  $(OUT)/swap_scratch.o \
  $(OUT)/image_validate.o \
  $(OUT)/tlv.o \
  $(OUT)/bootutil_misc.o \
  $(OUT)/bootutil_public.o \
  $(OUT)/fault_injection_hardening.o \
  $(OUT)/image_ecdsa.o \
  $(OUT)/sha256.o \
  $(OUT)/utils.o \
  $(OUT)/ecc.o \
  $(OUT)/ecc_dsa.o \
  $(OUT)/asn1parse.o \
  $(OUT)/platform_util.o \
  $(OUT)/boot_libc.o \
  $(OUT)/boot_flash.o \
  $(OUT)/boot_runtime.o \
  $(OUT)/bk7258_bl2_keys.o

.PHONY: all verify clean

all: $(OUT)/bl2.elf $(OUT)/bl2.bin verify

$(OUT):
	mkdir -p $@

$(OUT)/bl2.ld: $(SOURCE)/bl2.ld $(PARTITION_HEADER) $(CONFIG_HEADER) | $(OUT)
	$(CC) -E -P -x c -D__LINKER__ $(CPPFLAGS) $< -o $@

$(OUT)/bl2_start.o: $(SOURCE)/bl2_start.S $(PARTITION_HEADER) \
                     $(CONFIG_HEADER) | $(OUT)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OUT)/bk7258_bl2_%.o: $(SOURCE)/bk7258_bl2_%.c $(PARTITION_HEADER) \
                        $(CONFIG_HEADER) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/loader.o $(OUT)/swap_scratch.o $(OUT)/image_validate.o \
$(OUT)/tlv.o $(OUT)/bootutil_misc.o $(OUT)/bootutil_public.o \
$(OUT)/fault_injection_hardening.o $(OUT)/image_ecdsa.o: $(OUT)/%.o: \
    $(BOOTUTIL)/src/%.c $(CONFIG_HEADER) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/sha256.o $(OUT)/utils.o $(OUT)/ecc.o $(OUT)/ecc_dsa.o: $(OUT)/%.o: \
    $(TINYCRYPT)/source/%.c $(CONFIG_HEADER) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/asn1parse.o $(OUT)/platform_util.o: $(OUT)/%.o: \
    $(ASN1)/src/%.c $(CONFIG_HEADER) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/boot_libc.o $(OUT)/boot_flash.o $(OUT)/boot_runtime.o: $(OUT)/%.o: \
    $(SOURCE)/../%.c $(CONFIG_HEADER) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/bk7258_bl2_keys.o: $(KEY_SOURCE) $(CONFIG_HEADER) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/bl2.elf: $(OBJECTS) $(OUT)/bl2.ld
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@
	$(SIZE) $@

$(OUT)/bl2.bin: $(OUT)/bl2.elf
	$(OBJCOPY) -O binary $< $@

verify: $(OUT)/bl2.elf $(OUT)/bl2.bin
	@for symbol in bk7258_bl2_reset bk7258_bl2_main boot_go ecdsa_pub_key; do \
	  $(NM) -n $(OUT)/bl2.elf | grep -qw "$$symbol" || { \
	    echo "BL2 missing required symbol: $$symbol" >&2; exit 1; \
	  }; \
	done

clean:
	$(RM) $(OBJECTS) $(OUT)/bl2.ld $(OUT)/bl2.elf $(OUT)/bl2.bin \
	      $(OUT)/bl2.map
