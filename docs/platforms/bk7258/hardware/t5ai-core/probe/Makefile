# Makefile - BK7258 (Cortex-M33) bare probe.
#
# Produces probe.elf / probe.bin in this directory. The binary is the raw
# logical image linked at 0x02010000; CRC-expand it (see README) before
# burning at physical flash offset 0x11000.

# Toolchain: prefer arm-none-eabi- (any Cortex-M capable gcc works).
CROSS   ?= arm-none-eabi-
CC      = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
SIZE    = $(CROSS)size

CFLAGS  = -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -Os -g -ffreestanding -Wall -Wextra
LDFLAGS = -T probe.ld -nostdlib -Wl,--gc-sections -Wl,-Map=probe.map

TARGET  = probe
OBJS    = probe.o

.PHONY: all clean size

all: $(TARGET).bin

$(TARGET).o: probe.c probe.ld
	$(CC) $(CFLAGS) -c probe.c -o $@

$(TARGET).elf: $(OBJS) probe.ld
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

size: $(TARGET).elf
	$(SIZE) $<

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin $(TARGET).map
