TOP := ../../../../../
MCUBOOT := $(TOP)apps/boot/mcuboot/mcuboot

CC := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
CRC_PACKER := ../../scripts/bk7258_crc_expand.py
# This is the exact logical length signed by BL1 and padded before CRC
# expansion.  It is not the 128 KiB BL2 partition capacity.
BL2_LOGICAL_SIZE ?= 0x3000
BL2_LOGICAL_CAPACITY := 0x20000
BL2_LOGICAL_CAPACITY_BYTES := 131072
# Development-only, read-only downgrade floor.  A non-zero value is compiled
# into this board-owned BL2 and is never written back to Flash/OTP/eFuse.
# The default 0 accepts any valid MCUboot security counter; a
# verification build can raise it to prove the fail-closed path.
BL2_SECURITY_COUNTER_FLOOR ?= 0
CFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -Os -ffreestanding -fno-builtin -fdata-sections -ffunction-sections -Wall -Wextra
CFLAGS += -DBK7258_BL2_COPY_SIZE=$(BL2_LOGICAL_SIZE)
CFLAGS += -DBK7258_BL2_SECURITY_COUNTER_FLOOR=$(BL2_SECURITY_COUNTER_FLOOR)u
CFLAGS += -Iinclude -include assert.h -I$(MCUBOOT)/boot/bootutil/include -I$(MCUBOOT)/boot/bootutil/src
CFLAGS += -I$(MCUBOOT)/ext/tinycrypt/lib/include -I$(MCUBOOT)/ext/mbedtls-asn1/include
LDFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -nostdlib -Wl,--gc-sections -T bl2.ld -Wl,-Map=bl2.map

UPSTREAM := $(MCUBOOT)/boot/bootutil/src
TC := $(MCUBOOT)/ext/tinycrypt/lib/source
ASN1 := $(MCUBOOT)/ext/mbedtls-asn1/src
SRCS := bl2_start.S bk7258_bl2_main.c bk7258_bl2_flash_map.c bk7258_bl2_keys.c \
  bk7258_bl2_security_cnt.c \
  $(UPSTREAM)/loader.c $(UPSTREAM)/swap_scratch.c $(UPSTREAM)/image_validate.c \
  $(UPSTREAM)/tlv.c $(UPSTREAM)/bootutil_misc.c $(UPSTREAM)/bootutil_public.c \
  $(UPSTREAM)/fault_injection_hardening.c $(UPSTREAM)/image_ecdsa.c \
  $(TC)/sha256.c $(TC)/utils.c $(TC)/ecc.c $(TC)/ecc_dsa.c \
  $(ASN1)/asn1parse.c $(ASN1)/platform_util.c ../boot_libc.c \
  ../boot_runtime.c
OBJS := $(patsubst %.c,%.o,$(notdir $(filter %.c,$(SRCS)))) $(patsubst %.S,%.o,$(notdir $(filter %.S,$(SRCS))))

all: bl2.bin bl2_crc.bin

bl2.elf: $(OBJS) bl2.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@

bl2.bin: bl2.elf
	$(OBJCOPY) -O binary $< $@
	@size=$$(wc -c < $@); test $$size -le $(BL2_LOGICAL_CAPACITY_BYTES) || { echo "BL2 exceeds 128 KiB capacity"; exit 1; }; echo "BL2 raw size: $$size bytes"

bl2_crc.bin: bl2.bin $(CRC_PACKER)
	python3 $(CRC_PACKER) --in $< --out $@ \
		--xip-base 0x024d0000 --execution-base 0x28020000 \
		--max-size $(BL2_LOGICAL_CAPACITY) --pad-size $(BL2_LOGICAL_SIZE)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: $(UPSTREAM)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: $(TC)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: $(ASN1)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: ../%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -c $< -o $@

clean:
	rm -f $(OBJS) bl2.elf bl2.bin bl2_crc.bin bl2.bin.json bl2_crc.bin.json bl2.map
