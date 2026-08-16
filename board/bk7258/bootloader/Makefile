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
ASFLAGS  := $(CPUFLAGS) -Os -g -ffreestanding -I../chip/include
LDFLAGS  := $(CPUFLAGS) -T bootloader.tmp -nostdlib -Wl,--gc-sections \
            -Wl,-Map=bl.map

PARTITION_CSV ?= ../partitions/bk7258/auto_partitions.csv
PARTITION_GENERATOR ?= ../scripts/gen_bk7258_partitions.py
PARTITION_CONTRACT_ROOT ?=
ifneq ($(strip $(PARTITION_CONTRACT_ROOT)),)
PARTITION_HEADER := $(PARTITION_CONTRACT_ROOT)/include/arch/board/bk7258_partition_layout.h
PARTITION_OUTPUT_DIR := $(PARTITION_CONTRACT_ROOT)/generated
else
PARTITION_HEADER := ../include/bk7258_partition_layout.h
PARTITION_OUTPUT_DIR := ../partitions/generated
endif
PARTITION_HEADER_DIR := $(dir $(PARTITION_HEADER))
PARTITION_INCLUDE_FLAGS := -I$(PARTITION_HEADER_DIR)
CFLAGS += $(PARTITION_INCLUDE_FLAGS)
ASFLAGS += $(PARTITION_INCLUDE_FLAGS)
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
MEMORYMAP_HEADER := ../chip/include/bk7258_memorymap.h
BL1_CONTROL_GENERATOR := make_bk7258_bl1_control.py
BL2_IMAGE ?= bl2/bl2.bin
BL1_CONTROL_IMAGE ?= bl1_control.bin

# BL1 is the first owner of debug and early-console pinmux.  The dual-image
# builder derives these values from the CP defconfig and passes the identical
# route to BL2.  Standalone builds retain the established UART1 baseline.
BL1_SWD_ENABLE ?= 0
BL1_SWD_PIN_GROUP ?= 1
BL1_SWD_TARGET ?= 0
BL1_SWD_BOOT_HOLD ?= 0
BL1_USE_BL2 ?= 0
BL1_CONSOLE_UART ?= 1
BL1_CONSOLE_BAUD ?= 460800
BL1_CONSOLE_DATA_BITS ?= 8
BL1_CONSOLE_PARITY ?= 0
BL1_CONSOLE_STOP_BITS ?= 1
BL1_UART2_PIN_GROUP ?= 0
ifneq ($(filter $(BL1_SWD_ENABLE),0 1),$(BL1_SWD_ENABLE))
$(error BL1_SWD_ENABLE must be exactly 0 or 1)
endif
ifneq ($(filter $(BL1_SWD_PIN_GROUP),0 1),$(BL1_SWD_PIN_GROUP))
$(error BL1_SWD_PIN_GROUP must be 0 (P20/P21) or 1 (P0/P1))
endif
ifneq ($(filter $(BL1_SWD_TARGET),0 1 2),$(BL1_SWD_TARGET))
$(error BL1_SWD_TARGET must be 0, 1 or 2)
endif
ifneq ($(filter $(BL1_SWD_BOOT_HOLD),0 1),$(BL1_SWD_BOOT_HOLD))
$(error BL1_SWD_BOOT_HOLD must be exactly 0 or 1)
endif
ifneq ($(filter $(BL1_USE_BL2),0 1),$(BL1_USE_BL2))
$(error BL1_USE_BL2 must be exactly 0 or 1)
endif
ifneq ($(filter $(BL1_CONSOLE_UART),0 1 2 3),$(BL1_CONSOLE_UART))
$(error BL1_CONSOLE_UART must be 0, 1, 2 or 3 (RTT/NONE))
endif
ifneq ($(filter $(BL1_CONSOLE_DATA_BITS),5 6 7 8),$(BL1_CONSOLE_DATA_BITS))
$(error BL1_CONSOLE_DATA_BITS must be 5, 6, 7 or 8)
endif
ifneq ($(filter $(BL1_CONSOLE_PARITY),0 1 2),$(BL1_CONSOLE_PARITY))
$(error BL1_CONSOLE_PARITY must be 0, 1 or 2)
endif
ifneq ($(filter $(BL1_CONSOLE_STOP_BITS),1 2),$(BL1_CONSOLE_STOP_BITS))
$(error BL1_CONSOLE_STOP_BITS must be 1 or 2)
endif
ifneq ($(filter $(BL1_UART2_PIN_GROUP),0 1),$(BL1_UART2_PIN_GROUP))
$(error BL1_UART2_PIN_GROUP must be 0 (P30/P31) or 1 (P40/P41))
endif
BOOT_IO_DEFINES = \
	-DBK7258_BL1_SWD_ENABLE=$(BL1_SWD_ENABLE) \
	-DBK7258_BL1_SWD_PIN_GROUP=$(BL1_SWD_PIN_GROUP) \
	-DBK7258_BL1_SWD_TARGET=$(BL1_SWD_TARGET) \
	-DBK7258_BL1_SWD_BOOT_HOLD=$(BL1_SWD_BOOT_HOLD) \
	-DBK7258_BL1_USE_BL2=$(BL1_USE_BL2) \
	-DBK7258_BL1_CONSOLE_UART=$(BL1_CONSOLE_UART) \
	-DBK7258_BL1_CONSOLE_BAUD=$(BL1_CONSOLE_BAUD) \
	-DBK7258_BL1_CONSOLE_DATA_BITS=$(BL1_CONSOLE_DATA_BITS) \
	-DBK7258_BL1_CONSOLE_PARITY=$(BL1_CONSOLE_PARITY) \
	-DBK7258_BL1_CONSOLE_STOP_BITS=$(BL1_CONSOLE_STOP_BITS) \
	-DBK7258_BL1_UART2_PIN_GROUP=$(BL1_UART2_PIN_GROUP)
NUTTX_ROOT ?= ../../../../nuttx
MCUBOOT_ROOT := $(NUTTX_ROOT)/../apps/boot/mcuboot/mcuboot
TINYCRYPT_ROOT := $(MCUBOOT_ROOT)/ext/tinycrypt/lib
TINYCRYPT_CFLAGS := $(CFLAGS) -I$(TINYCRYPT_ROOT)/include

# The self-owned BL1 Manifest is opt-in while the board remains on a
# recoverable development chain.  Enabling it requires an externally signed
# 256-byte record; neither SDK/NuttX sources nor OTP/eFuse are modified.
BL1_MANIFEST_ENFORCE ?= 0
BL1_COMPILE_ONLY ?= 0
BL1_COMPILE_POLICY ?= raw
ifneq ($(filter $(BL1_COMPILE_ONLY),0 1),$(BL1_COMPILE_ONLY))
$(error BL1_COMPILE_ONLY must be exactly 0 or 1)
endif
ifneq ($(filter $(BL1_COMPILE_POLICY),raw mcuboot),$(BL1_COMPILE_POLICY))
$(error BL1_COMPILE_POLICY must be raw or mcuboot)
endif
ifeq ($(BL1_COMPILE_ONLY),1)
ifneq ($(strip $(BL1_MANIFEST_PRIMARY)),)
$(error BL1 compile-only must not receive a signed manifest path)
endif
ifneq ($(strip $(BL1_MANIFEST_SECONDARY)),)
$(error BL1 compile-only must not receive a signed manifest path)
endif
ifeq ($(BL1_COMPILE_POLICY),mcuboot)
# Compile-only MCUboot mode must compile the BL1 -> BL2 handoff and verifier,
# but it deliberately has no signed manifest record to read or package.
override BL1_USE_BL2 := 1
override BL1_MANIFEST_ENFORCE := 1
else ifeq ($(BL1_COMPILE_POLICY),raw)
override BL1_USE_BL2 := 0
override BL1_MANIFEST_ENFORCE := 0
endif
endif
ifeq ($(BL1_COMPILE_POLICY),mcuboot)
ifneq ($(BL1_COMPILE_ONLY),1)
$(error BL1_COMPILE_POLICY=mcuboot is compile-only-only)
endif
endif
ifneq ($(filter $(BL1_MANIFEST_ENFORCE),0 1),$(BL1_MANIFEST_ENFORCE))
$(error BL1_MANIFEST_ENFORCE must be exactly 0 or 1)
endif

# Append after compile-only policy resolution so command-line attempts to
# force direct-boot values cannot leak into the MCUboot compile target.
CFLAGS += $(BOOT_IO_DEFINES)
ASFLAGS += $(BOOT_IO_DEFINES)

# Optional validation path for the board-owned 4 KiB BL1 Manifest partitions.
# It is intentionally separate from the established executable XIP-tail
# records and is never enabled by a normal build.
BL1_MANIFEST_RAW_PAGE ?= 0
ifneq ($(filter $(BL1_MANIFEST_RAW_PAGE),0 1),$(BL1_MANIFEST_RAW_PAGE))
$(error BL1_MANIFEST_RAW_PAGE must be exactly 0 or 1)
endif
ifeq ($(BL1_MANIFEST_RAW_PAGE),1)
ifeq ($(BL1_USE_BL2),0)
$(error BL1_MANIFEST_RAW_PAGE requires BL1_USE_BL2=1)
endif
ifeq ($(BL1_MANIFEST_ENFORCE),0)
$(error BL1_MANIFEST_RAW_PAGE requires BL1_MANIFEST_ENFORCE=1)
endif
CFLAGS += -DBK7258_BL1_MANIFEST_RAW_PAGE=1u
endif

BL1_MANIFEST ?=
BL1_MANIFEST_PRIMARY ?= $(BL1_MANIFEST)
BL1_MANIFEST_SECONDARY ?=
# A normal source-tree build keeps the repository development root.  Secure
# image packaging overrides this with a public-only C file generated from the
# caller's external private key; the private key is never a compiler input.
BL1_MANIFEST_KEY_SOURCE ?= boot_bl1_manifest_key.c
ifeq ($(BL1_COMPILE_ONLY),1)
ifneq ($(BL1_MANIFEST_KEY_SOURCE),boot_bl1_manifest_key.c)
$(error BL1 compile-only key source must be the staged public source)
endif
endif
# Candidate Beken/Armino records use the read-only Dubhe OTP shadow as the
# production root when provisioned.  The current board is CM with an empty
# hash, so this policy falls back to the recoverable software development
# root.  No OTP/eFuse write is reachable from this switch.
BL1_OTP_ROOT_POLICY ?= 1
ifneq ($(filter $(BL1_OTP_ROOT_POLICY),0 1),$(BL1_OTP_ROOT_POLICY))
$(error BL1_OTP_ROOT_POLICY must be exactly 0 or 1)
endif
CFLAGS += -DBK7258_BL1_OTP_ROOT_POLICY=$(BL1_OTP_ROOT_POLICY)u

BL1_TRUSTENGINE_PROBE ?= 0
ifneq ($(filter $(BL1_TRUSTENGINE_PROBE),0 1),$(BL1_TRUSTENGINE_PROBE))
$(error BL1_TRUSTENGINE_PROBE must be exactly 0 or 1)
endif
ifeq ($(BL1_TRUSTENGINE_PROBE),1)
CFLAGS += -DBK7258_BL1_TRUSTENGINE_PROBE=1u
endif

# Compile-only provider for the documented 12 KiB secureboot staging layout.
# It reads the boot-control record through the raw Flash path; the ordinary
# legacy image must leave this disabled because raw offset 0x1000 still belongs
# to its CRC-expanded boot envelope.
BL1_BOOT_CONTROL_STAGING ?= 0
ifneq ($(filter $(BL1_BOOT_CONTROL_STAGING),0 1),$(BL1_BOOT_CONTROL_STAGING))
$(error BL1_BOOT_CONTROL_STAGING must be exactly 0 or 1)
endif
ifeq ($(BL1_BOOT_CONTROL_STAGING),1)
ifeq ($(BL1_USE_BL2),0)
$(error BL1_BOOT_CONTROL_STAGING requires BL1_USE_BL2=1)
endif
CFLAGS += -DBK7258_BL1_BOOT_CONTROL_STAGING=1u \
          -DBK7258_BL1_BOOT_CONTROL_RAW_OFFSET=0x1000u \
          -DBK7258_BL1_PRIMARY_MANIFEST_XIP=0x02003000u \
          -DBK7258_BL1_SECONDARY_MANIFEST_XIP=0x02004000u
endif

ifeq ($(BL1_MANIFEST_ENFORCE),1)
ifeq ($(BL1_USE_BL2),0)
$(error BL1_MANIFEST_ENFORCE requires BL1_USE_BL2=1)
endif
ifeq ($(BL1_COMPILE_ONLY),0)
ifeq ($(strip $(BL1_MANIFEST_PRIMARY)),)
$(error BL1_MANIFEST_PRIMARY must name a signed record when BL1_MANIFEST_ENFORCE=1)
endif
endif

# BL2_LOGICAL_SIZE is the signed/CRC-padded logical length of the specific
# BL2 image.  It is deliberately separate from the fixed 128 KiB partition
# capacity.  The BL1 and BL2 Makefiles must receive the same value.
BL2_LOGICAL_SIZE ?= 0x3000
CFLAGS += -DBK7258_BL2_COPY_SIZE=$(BL2_LOGICAL_SIZE)
CFLAGS += -DBK7258_BL1_MANIFEST_ENFORCE=1u
BL1_MANIFEST_ARG := --manifest-primary $(BL1_MANIFEST_PRIMARY)
ifneq ($(strip $(BL1_MANIFEST_SECONDARY)),)
BL1_MANIFEST_ARG += --manifest-secondary $(BL1_MANIFEST_SECONDARY)
endif
endif

BL1_CORE_OBJS := start.o boot_main.o boot_clock.o boot_runtime.o \
		boot_sha256.o tinycrypt_ecc.o tinycrypt_ecc_dsa.o \
		boot_bl1_manifest.o boot_bl1_boot_flag_core.o \
		boot_bl1_handoff_core.o boot_bl1_policy.o \
		boot_bl1_manifest_key.o boot_flash.o boot_libc.o

OBJS := $(BL1_CORE_OBJS)

$(OBJS): $(PARTITION_HEADER) $(MEMORYMAP_HEADER)

.PHONY: all compile-only clean verify partition-check bl1-control FORCE

# Make does not track command-line CFLAGS.  These two outputs depend on the
# manifest enforcement setting and must therefore be refreshed when a caller
# switches BL1_MANIFEST_ENFORCE without first cleaning the directory.
FORCE:

all: partition-check bl.bin bl_crc.bin

# Host/executor compile-only boundary.  It intentionally stops at the raw
# ELF/BIN pair; CRC expansion, manifest generation and packaging remain
# separate authorized operations and are never prerequisites here.
compile-only: partition-check bl.elf bl.bin

$(OBJS) bootloader.tmp bl_crc.bin: | partition-check

$(OBJS): FORCE

# This target is deliberately opt-in.  The active v3.1.1.9 board layout still
# boots from the legacy boot slot; producing the official-shaped XIP control
# page must not silently change that default image.
bl1-control: $(BL1_CONTROL_GENERATOR)
	@test -f "$(BL2_IMAGE)" || { echo "missing BL2 image: $(BL2_IMAGE)" >&2; exit 1; }
	python3 $(BL1_CONTROL_GENERATOR) --bl2 "$(BL2_IMAGE)" --out "$(BL1_CONTROL_IMAGE)"

partition-check:
	python3 $(PARTITION_GENERATOR) $(PARTITION_ARGS) --check

bootloader.tmp: bootloader.ld $(PARTITION_HEADER) $(MEMORYMAP_HEADER)
	$(CC) -E -P -x c -D__LINKER__ \
		-I../chip/include $(PARTITION_INCLUDE_FLAGS) $< -o $@

start.o: start.S
	$(CC) $(ASFLAGS) -c $< -o $@

boot_main.o: boot_main.c boot_wdt.h boot_bl2_contract.h boot_bl1_manifest.h \
		boot_bl1_policy.h boot_bl1_boot_flag_core.h boot_bl1_handoff_core.h \
		boot_flash.h FORCE
	$(CC) $(CFLAGS) -c $< -o $@

boot_clock.o: boot_clock.c
	$(CC) $(CFLAGS) -c $< -o $@

boot_runtime.o: boot_runtime.c
	$(CC) $(CFLAGS) -c $< -o $@

boot_flash.o: boot_flash.c boot_flash.h boot_wdt.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_sha256.o: boot_sha256.c boot_sha256.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_bl1_manifest.o: boot_bl1_manifest.c boot_bl1_manifest.h \
		boot_bl1_policy.h boot_sha256.h
	$(CC) $(TINYCRYPT_CFLAGS) -c $< -o $@

boot_bl1_policy.o: boot_bl1_policy.c boot_bl1_policy.h boot_bl1_manifest.h \
		boot_bl1_boot_flag_core.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_bl1_boot_flag_core.o: boot_bl1_boot_flag_core.c boot_bl1_boot_flag_core.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_bl1_handoff_core.o: boot_bl1_handoff_core.c boot_bl1_handoff_core.h
	$(CC) $(CFLAGS) -c $< -o $@

tinycrypt_ecc.o: $(TINYCRYPT_ROOT)/source/ecc.c
	$(CC) $(TINYCRYPT_CFLAGS) -c $< -o $@

tinycrypt_ecc_dsa.o: $(TINYCRYPT_ROOT)/source/ecc_dsa.c
	$(CC) $(TINYCRYPT_CFLAGS) -c $< -o $@

boot_bl1_manifest_key.o: $(BL1_MANIFEST_KEY_SOURCE) FORCE
	$(CC) $(CFLAGS) -c $< -o $@

boot_libc.o: boot_libc.c
	$(CC) $(CFLAGS) -c $< -o $@

bl.elf: $(OBJS) bootloader.tmp
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

bl.bin: bl.elf
	$(OBJCOPY) -O binary $< $@

bl_crc.bin: bl.bin bk7236_pack_min_bootloader.py FORCE
	BK7258_PARTITION_LAYOUT_SOURCE="$(abspath $(PARTITION_CSV))" \
		BK7258_PARTITION_LAYOUT_ID="$(PARTITION_LAYOUT_ID)" \
		BK7258_PARTITION_LAYOUT_SHA256="$(PARTITION_LAYOUT_SHA256)" \
		python3 bk7236_pack_min_bootloader.py --in bl.bin --out bl_crc.bin $(BL1_MANIFEST_ARG)

verify: bl_crc.bin
	@for symbol in Reset_Handler c_main fal_partition_table \
		; do \
		$(NM) -n bl.elf | grep -qw "$$symbol" || { \
			echo "bootloader missing required symbol: $$symbol" >&2; \
			exit 1; \
		}; \
	done
ifeq ($(BL1_USE_BL2),1)
	@$(NM) -n bl.elf | grep -qw bk7258_bl1_handoff_vector_valid || { \
		echo 'BL2-handoff BL1 lacks its vector validator' >&2; \
		exit 1; \
	}
endif
ifeq ($(BL1_MANIFEST_ENFORCE),1)
	@$(NM) -n bl.elf | grep -qw bk7258_bl1_manifest_verify_at || { \
		echo 'Manifest-enforcing BL1 lacks its verifier' >&2; \
		exit 1; \
	}
endif
	@if $(NM) -n bl.elf | grep -Eq 'boot_ota_|boot_n17_|bk7258_ota_n17|nuttx_ecc'; then \
		echo 'BL1 unexpectedly links retired OTA/N17 lifecycle code' >&2; \
		exit 1; \
	fi
	$(OBJDUMP) -s -j .rodata bl.elf | sed -n '1,40p'

clean:
	rm -f *.o *.su bl.elf bl.bin bl.map bl_crc.bin bl_crc.json bootloader.tmp
