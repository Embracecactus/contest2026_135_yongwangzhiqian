# contest2026_135_yongwangzhiqian/board/bk7258_t5ai/tests/qemu_mbox_proxy/Makefile
#
# Builds a freestanding dual-core (Cortex-M33) proxy for the BK7258 RPTUN
# MBOX0 notify logic and runs it under QEMU mps2-an521 (-smp 2).
# Pure test/board code: does not touch the SDK or firmware.

ARM     = arm-none-eabi
CFLAGS  = -mcpu=cortex-m33 -mthumb \
          -ffreestanding -nostdlib -nostartfiles -nodefaultlibs \
          -Wall -Wextra -O2 -g -mfloat-abi=soft

TARGET  = firmware.elf

all: $(TARGET)

$(TARGET): startup.o mbox_proxy.o
	$(ARM)-gcc -T link.ld $(CFLAGS) $^ -o $@

startup.o: startup.S
	$(ARM)-gcc -mcpu=cortex-m33 -mthumb -c $< -o $@

mbox_proxy.o: mbox_proxy.c
	$(ARM)-gcc $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.elf

.PHONY: all clean
