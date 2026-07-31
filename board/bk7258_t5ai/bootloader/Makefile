# Makefile - BK7258 Tier-1 bootloader (arm-none-eabi-gcc, freestanding).
#
# Products:
#   bl.elf       linked ELF
#   bl.bin       flat logical image (vectors + magic + code + rodata)
#   bl_crc.bin   Beken CRC-expanded image (32B data + 2B CRC), physical 0x11000
#
# Build:  make            # in this directory
# Clean:  make clean

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
NM      := $(CROSS)nm
OBJDUMP := $(CROSS)objdump

CPUFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft
CFLAGS   := $(CPUFLAGS) -Os -g -ffreestanding -fno-builtin -Wall -Wextra \
            -fdata-sections -ffunction-sections
ASFLAGS  := $(CPUFLAGS) -Os -g -ffreestanding
LDFLAGS  := $(CPUFLAGS) -T bootloader.ld -nostdlib -Wl,--gc-sections \
            -Wl,-Map=bl.map

OBJS := start.o boot_main.o boot_clock.o boot_runtime.o

.PHONY: all clean verify

all: bl.bin bl_crc.bin

start.o: start.S
	$(CC) $(ASFLAGS) -c $< -o $@

boot_main.o: boot_main.c
	$(CC) $(CFLAGS) -c $< -o $@

boot_clock.o: boot_clock.c
	$(CC) $(CFLAGS) -c $< -o $@

boot_runtime.o: boot_runtime.c
	$(CC) $(CFLAGS) -c $< -o $@

bl.elf: $(OBJS) bootloader.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

bl.bin: bl.elf
	$(OBJCOPY) -O binary $< $@

bl_crc.bin: bl.bin bk7236_pack_min_bootloader.py
	python3 bk7236_pack_min_bootloader.py --in bl.bin --out bl_crc.bin

verify: bl_crc.bin
	$(NM) -n bl.elf | grep -E 'Reset_Handler|c_main|fal_partition_table'
	$(OBJDUMP) -s -j .rodata bl.elf | sed -n '1,40p'

clean:
	rm -f *.o bl.elf bl.bin bl.map bl_crc.bin bl_crc.json
