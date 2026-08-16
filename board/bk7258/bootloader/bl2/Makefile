TOP := ../../../../../
MCUBOOT := $(TOP)apps/boot/mcuboot/mcuboot

CC := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
CRC_PACKER := ../../scripts/bk7258_crc_expand.py
PARTITION_CSV ?= ../../partitions/bk7258/auto_partitions.csv
PARTITION_GENERATOR ?= ../../scripts/gen_bk7258_partitions.py
PARTITION_CONTRACT_ROOT ?=
ifneq ($(strip $(PARTITION_CONTRACT_ROOT)),)
PARTITION_HEADER := $(PARTITION_CONTRACT_ROOT)/include/arch/board/bk7258_partition_layout.h
PARTITION_OUTPUT_DIR := $(PARTITION_CONTRACT_ROOT)/generated
else
PARTITION_HEADER := ../../include/bk7258_partition_layout.h
PARTITION_OUTPUT_DIR := ../../partitions/generated
endif
PARTITION_HEADER_DIR := $(dir $(PARTITION_HEADER))
PARTITION_INCLUDE_FLAGS := -I$(PARTITION_HEADER_DIR)
PARTITION_LAYOUT_ID ?=
PARTITION_LAYOUT_SHA256 ?=
ifneq ($(strip $(PARTITION_CONTRACT_ROOT)),)
ifeq ($(strip $(PARTITION_LAYOUT_ID)),)
$(error PARTITION_LAYOUT_ID is required with PARTITION_CONTRACT_ROOT)
endif
endif
PARTITION_ARGS := --input $(PARTITION_CSV) --header $(PARTITION_HEADER) \
	--output-dir $(PARTITION_OUTPUT_DIR)
ifneq ($(strip $(PARTITION_LAYOUT_ID)),)
ifeq ($(strip $(PARTITION_LAYOUT_SHA256)),)
$(error PARTITION_LAYOUT_SHA256 is required with PARTITION_LAYOUT_ID)
endif
PARTITION_ARGS += --expect-layout-id $(PARTITION_LAYOUT_ID) \
	--expect-layout-sha256 $(PARTITION_LAYOUT_SHA256)
else ifneq ($(strip $(PARTITION_LAYOUT_SHA256)),)
$(error PARTITION_LAYOUT_ID is required with PARTITION_LAYOUT_SHA256)
endif
# This is the exact logical length signed by BL1 and padded before CRC
# expansion.  It is not the 128 KiB BL2 partition capacity.
BL2_LOGICAL_SIZE ?= 0x3000
BL2_LOGICAL_CAPACITY ?= 0x20000
BL2_LOGICAL_CAPACITY_BYTES ?= 131072
BL2_PHYSICAL_SIZE ?= 0x22000
BL2_PHYSICAL_SIZE_BYTES ?= 139264
BL2_XIP_BASE ?= 0x024d0000
BL2_EXECUTION_BASE ?= 0x28020000
# Development-only, read-only downgrade floor.  A non-zero value is compiled
# into this board-owned BL2 and is never written back to Flash/OTP/eFuse.
# The default 0 accepts any valid MCUboot security counter; a
# verification build can raise it to prove the fail-closed path.
BL2_SECURITY_COUNTER_FLOOR ?= 0
BL2_SWD_ENABLE ?= 0
BL2_SWD_PIN_GROUP ?= 1
BL2_SWD_TARGET ?= 0
BL2_SWD_BOOT_HOLD ?= 0
BL2_CONSOLE_UART ?= 1
BL2_CONSOLE_BAUD ?= 460800
BL2_CONSOLE_DATA_BITS ?= 8
BL2_CONSOLE_PARITY ?= 0
BL2_CONSOLE_STOP_BITS ?= 1
BL2_UART2_PIN_GROUP ?= 0
BL2_COMPILE_ONLY ?= 0
BL2_COMPILE_POLICY ?= raw
BL2_KEY_SOURCE ?= bk7258_bl2_keys.c
ifeq ($(BL2_COMPILE_ONLY),1)
ifneq ($(BL2_KEY_SOURCE),bk7258_bl2_keys.c)
$(error BL2 compile-only key source must be the staged public source)
endif
endif
ifneq ($(filter $(BL2_SWD_ENABLE),0 1),$(BL2_SWD_ENABLE))
$(error BL2_SWD_ENABLE must be exactly 0 or 1)
endif
ifneq ($(filter $(BL2_SWD_PIN_GROUP),0 1),$(BL2_SWD_PIN_GROUP))
$(error BL2_SWD_PIN_GROUP must be 0 (P20/P21) or 1 (P0/P1))
endif
ifneq ($(filter $(BL2_SWD_TARGET),0 1 2),$(BL2_SWD_TARGET))
$(error BL2_SWD_TARGET must be 0, 1 or 2)
endif
ifneq ($(filter $(BL2_SWD_BOOT_HOLD),0 1),$(BL2_SWD_BOOT_HOLD))
$(error BL2_SWD_BOOT_HOLD must be exactly 0 or 1)
endif
ifneq ($(filter $(BL2_CONSOLE_UART),0 1 2 3),$(BL2_CONSOLE_UART))
$(error BL2_CONSOLE_UART must be 0, 1, 2 or 3 (RTT/NONE))
endif
ifneq ($(filter $(BL2_CONSOLE_DATA_BITS),5 6 7 8),$(BL2_CONSOLE_DATA_BITS))
$(error BL2_CONSOLE_DATA_BITS must be 5, 6, 7 or 8)
endif
ifneq ($(filter $(BL2_CONSOLE_PARITY),0 1 2),$(BL2_CONSOLE_PARITY))
$(error BL2_CONSOLE_PARITY must be 0, 1 or 2)
endif
ifneq ($(filter $(BL2_CONSOLE_STOP_BITS),1 2),$(BL2_CONSOLE_STOP_BITS))
$(error BL2_CONSOLE_STOP_BITS must be 1 or 2)
endif
ifneq ($(filter $(BL2_UART2_PIN_GROUP),0 1),$(BL2_UART2_PIN_GROUP))
$(error BL2_UART2_PIN_GROUP must be 0 (P30/P31) or 1 (P40/P41))
endif
ifneq ($(filter $(BL2_COMPILE_ONLY),0 1),$(BL2_COMPILE_ONLY))
$(error BL2_COMPILE_ONLY must be exactly 0 or 1)
endif
ifneq ($(filter $(BL2_COMPILE_POLICY),raw mcuboot),$(BL2_COMPILE_POLICY))
$(error BL2_COMPILE_POLICY must be raw or mcuboot)
endif
ifeq ($(BL2_COMPILE_ONLY),1)
ifneq ($(BL2_COMPILE_POLICY),mcuboot)
$(error BL2 compile-only policy must be mcuboot)
endif
endif
CFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -Os -ffreestanding -fno-builtin -fdata-sections -ffunction-sections -Wall -Wextra
CFLAGS += -DBK7258_BL2_COPY_SIZE=$(BL2_LOGICAL_SIZE)
CFLAGS += -DBK7258_BL2_SECURITY_COUNTER_FLOOR=$(BL2_SECURITY_COUNTER_FLOOR)u
CFLAGS += -DBK7258_BL2_SWD_ENABLE=$(BL2_SWD_ENABLE)
CFLAGS += -DBK7258_BL2_SWD_PIN_GROUP=$(BL2_SWD_PIN_GROUP)
CFLAGS += -DBK7258_BL2_SWD_TARGET=$(BL2_SWD_TARGET)
CFLAGS += -DBK7258_BL2_SWD_BOOT_HOLD=$(BL2_SWD_BOOT_HOLD)
CFLAGS += -DBK7258_BL2_CONSOLE_UART=$(BL2_CONSOLE_UART)
CFLAGS += -DBK7258_BL2_CONSOLE_BAUD=$(BL2_CONSOLE_BAUD)
CFLAGS += -DBK7258_BL2_CONSOLE_DATA_BITS=$(BL2_CONSOLE_DATA_BITS)
CFLAGS += -DBK7258_BL2_CONSOLE_PARITY=$(BL2_CONSOLE_PARITY)
CFLAGS += -DBK7258_BL2_CONSOLE_STOP_BITS=$(BL2_CONSOLE_STOP_BITS)
CFLAGS += -DBK7258_BL2_UART2_PIN_GROUP=$(BL2_UART2_PIN_GROUP)
CFLAGS += $(PARTITION_INCLUDE_FLAGS) -Iinclude -I../../chip/include -include assert.h -I$(MCUBOOT)/boot/bootutil/include -I$(MCUBOOT)/boot/bootutil/src
CFLAGS += -I$(MCUBOOT)/ext/tinycrypt/lib/include -I$(MCUBOOT)/ext/mbedtls-asn1/include
LDFLAGS := -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -nostdlib -Wl,--gc-sections -T bl2.ld -Wl,-Map=bl2.map

UPSTREAM := $(MCUBOOT)/boot/bootutil/src
TC := $(MCUBOOT)/ext/tinycrypt/lib/source
ASN1 := $(MCUBOOT)/ext/mbedtls-asn1/src
SRCS := bl2_start.S bk7258_bl2_main.c bk7258_bl2_flash_map.c \
  bk7258_bl2_security_cnt.c \
  $(UPSTREAM)/loader.c $(UPSTREAM)/swap_scratch.c $(UPSTREAM)/image_validate.c \
  $(UPSTREAM)/tlv.c $(UPSTREAM)/bootutil_misc.c $(UPSTREAM)/bootutil_public.c \
  $(UPSTREAM)/fault_injection_hardening.c $(UPSTREAM)/image_ecdsa.c \
  $(TC)/sha256.c $(TC)/utils.c $(TC)/ecc.c $(TC)/ecc_dsa.c \
  $(ASN1)/asn1parse.c $(ASN1)/platform_util.c ../boot_libc.c \
  ../boot_runtime.c
OBJS := $(patsubst %.c,%.o,$(notdir $(filter %.c,$(SRCS)))) $(patsubst %.S,%.o,$(notdir $(filter %.S,$(SRCS)))) bk7258_bl2_keys.o

.PHONY: all compile-only clean partition-check FORCE

FORCE:

all: partition-check bl2.bin bl2_crc.bin

# Compile-only executor boundary: publish only the unsigned ELF/BIN pair.
# CRC expansion and any later manifest/sign/package step are deliberately not
# prerequisites and must not be implied by this target.
compile-only: partition-check bl2.elf bl2.bin

$(OBJS) bl2.elf bl2.bin bl2_crc.bin: | partition-check

$(OBJS): $(PARTITION_HEADER)

$(OBJS): FORCE

partition-check:
	python3 $(PARTITION_GENERATOR) $(PARTITION_ARGS) --check

bl2.elf: $(OBJS) bl2.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@

bl2.bin: bl2.elf
	$(OBJCOPY) -O binary $< $@
	@size=$$(wc -c < $@); test $$size -le $(BL2_LOGICAL_CAPACITY_BYTES) || { echo "BL2 exceeds 128 KiB capacity"; exit 1; }; echo "BL2 raw size: $$size bytes"

bl2_crc.bin: bl2.bin $(CRC_PACKER) FORCE
	@test "$$(( $$(python3 $(PARTITION_GENERATOR) $(PARTITION_ARGS) --get bl2.xip_start) ))" -eq \
		"$$(( $(BL2_XIP_BASE) ))" || { echo "BL2 XIP base disagrees with partition contract" >&2; exit 1; }
	@test "$$(( $$(python3 $(PARTITION_GENERATOR) $(PARTITION_ARGS) --get bl2.logical_size) ))" -eq \
		"$$(( $(BL2_LOGICAL_CAPACITY) ))" || { echo "BL2 capacity disagrees with partition contract" >&2; exit 1; }
	python3 $(CRC_PACKER) --in $< --out $@ \
		--xip-base $(BL2_XIP_BASE) --execution-base $(BL2_EXECUTION_BASE) \
		--max-size $(BL2_LOGICAL_CAPACITY) --pad-size $(BL2_LOGICAL_SIZE)

bk7258_bl2_keys.o: $(BL2_KEY_SOURCE)
	$(CC) $(CFLAGS) -c $< -o $@

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
	$(CC) -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
		$(PARTITION_INCLUDE_FLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) bl2.elf bl2.bin bl2_crc.bin bl2.bin.json bl2_crc.bin.json bl2.map
