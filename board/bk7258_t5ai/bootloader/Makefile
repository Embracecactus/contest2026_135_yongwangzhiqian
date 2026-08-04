# Makefile - BK7258 Tier-1 bootloader (arm-none-eabi-gcc, freestanding).
#
# Products:
#   bl.elf       linked ELF
#   bl.bin       flat logical image (vectors + magic + code + rodata)
#   bl_crc.bin   Beken CRC-expanded image (32B data + 2B CRC), physical 0x11000
#
# Build:  make            # in this directory
# Clean:  make clean

.DEFAULT_GOAL := all

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
NM      := $(CROSS)nm
OBJDUMP := $(CROSS)objdump

CPUFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
CFLAGS   := $(CPUFLAGS) -Os -g -ffreestanding -fno-builtin -Wall -Wextra \
            -Werror \
            -fdata-sections -ffunction-sections -I../chip/include
ASFLAGS  := $(CPUFLAGS) -Os -g -ffreestanding
LDFLAGS  := $(CPUFLAGS) -T bootloader.tmp -nostdlib -Wl,--gc-sections \
            -Wl,-Map=bl.map

PARTITION_CSV := ../partitions/bk7258/auto_partitions.csv
PARTITION_GENERATOR := ../scripts/gen_bk7258_partitions.py
PARTITION_HEADER := ../chip/include/bk7258_partition_layout.h

# The normal image is immutable and fail-closed.  Only the separately named
# N15 validation package may build a selector whose six const gates are one.
# This variable affects compilation only; it does not flash a board.

N15_OTA_VALIDATION ?= 0
ifneq ($(filter $(N15_OTA_VALIDATION),0 1),$(N15_OTA_VALIDATION))
$(error N15_OTA_VALIDATION must be exactly 0 or 1)
endif

ifeq ($(N15_OTA_VALIDATION),1)
CFLAGS += -DBK7258_BOOT_OTA_SELECT_COMPILE_GATE=1u \
          -DBK7258_BOOT_OTA_SELECT_RUNTIME_GATE=1u \
          -DBK7258_BOOT_OTA_REMAP_COMPILE_GATE=1u \
          -DBK7258_BOOT_OTA_REMAP_RUNTIME_GATE=1u \
          -DBK7258_BOOT_OTA_TRIAL_COMPILE_GATE=1u \
          -DBK7258_BOOT_OTA_TRIAL_RUNTIME_GATE=1u
endif

OBJS := start.o boot_main.o boot_clock.o boot_runtime.o boot_ota_select.o \
	        boot_ota_select_core.o boot_ota_trial_core.o \
	        boot_ota_rotation_core.o boot_ota_rotation_select_core.o \
	        boot_ota_rotation_trial_core.o \
	        boot_ota_flash_program.o boot_ota_staging_core.o boot_sha256.o \
	        boot_libc.o

$(OBJS): $(PARTITION_HEADER)

.PHONY: all clean verify partition-check

all: partition-check bl.bin bl_crc.bin

partition-check:
	python3 $(PARTITION_GENERATOR) --check

bootloader.tmp: bootloader.ld $(PARTITION_HEADER)
	$(CC) -E -P -x c -I../chip/include $< -o $@

start.o: start.S
	$(CC) $(ASFLAGS) -c $< -o $@

boot_main.o: boot_main.c
	$(CC) $(CFLAGS) -c $< -o $@

boot_clock.o: boot_clock.c
	$(CC) $(CFLAGS) -c $< -o $@

boot_runtime.o: boot_runtime.c
	$(CC) $(CFLAGS) -c $< -o $@

boot_ota_select.o: boot_ota_select.c boot_ota_select.h \
		boot_ota_select_core.h boot_ota_rotation_select_core.h \
		boot_ota_rotation_trial_core.h boot_sha256.h boot_wdt.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_ota_select_core.o: boot_ota_select_core.c boot_ota_select_core.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_ota_trial_core.o: boot_ota_trial_core.c boot_ota_trial_core.h \
		boot_ota_select_core.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_ota_rotation_core.o: boot_ota_rotation_core.c \
		boot_ota_rotation_core.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_ota_rotation_select_core.o: boot_ota_rotation_select_core.c \
		boot_ota_rotation_select_core.h boot_ota_rotation_core.h \
		boot_ota_select_core.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_ota_rotation_trial_core.o: boot_ota_rotation_trial_core.c \
		boot_ota_rotation_trial_core.h boot_ota_rotation_core.h \
		boot_ota_trial_core.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_ota_flash_program.o: boot_ota_flash_program.c boot_ota_flash_program.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_ota_staging_core.o: ../chip/cp/bk7258_ota_staging_core.c \
		../chip/cp/bk7258_ota_staging_core.h \
		../chip/include/bk7258_ota_staging.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_sha256.o: boot_sha256.c boot_sha256.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_libc.o: boot_libc.c
	$(CC) $(CFLAGS) -c $< -o $@

bl.elf: $(OBJS) bootloader.tmp
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

bl.bin: bl.elf
	$(OBJCOPY) -O binary $< $@

bl_crc.bin: bl.bin bk7236_pack_min_bootloader.py
	python3 bk7236_pack_min_bootloader.py --in bl.bin --out bl_crc.bin

verify: bl_crc.bin
	$(NM) -n bl.elf | grep -E 'Reset_Handler|c_main|fal_partition_table|boot_ota_select_app|bk7258_boot_ota_select_core'
	$(OBJDUMP) -s -j .rodata bl.elf | sed -n '1,40p'

clean:
	rm -f *.o *.su bl.elf bl.bin bl.map bl_crc.bin bl_crc.json bootloader.tmp
