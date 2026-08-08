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
BL1_CONTROL_GENERATOR := make_bk7258_bl1_control.py
BL2_IMAGE ?= bl2/bl2.bin
BL1_CONTROL_IMAGE ?= bl1_control.bin
NUTTX_ROOT ?= ../../../../nuttx
N17_ECC_SOURCE := $(NUTTX_ROOT)/crypto/ecc.c
MCUBOOT_ROOT := $(NUTTX_ROOT)/../apps/boot/mcuboot/mcuboot
TINYCRYPT_ROOT := $(MCUBOOT_ROOT)/ext/tinycrypt/lib
CRYPTO_CFLAGS := $(CFLAGS) -I$(NUTTX_ROOT)/include -DFAR=
TINYCRYPT_CFLAGS := $(CFLAGS) -I$(TINYCRYPT_ROOT)/include

# The normal image is immutable and fail-closed.  Only the separately named
# N15 validation package may build a selector whose six const gates are one.
# This variable affects compilation only; it does not flash a board.

N15_OTA_VALIDATION ?= 0
ifneq ($(filter $(N15_OTA_VALIDATION),0 1),$(N15_OTA_VALIDATION))
$(error N15_OTA_VALIDATION must be exactly 0 or 1)
endif

# This development-only probe executes the N17 reader while leaving the
# older selector, Flash remap and all programming paths closed.  It is useful
# on an erased/format-2 board to prove the target-side parser returns to the
# primary handoff; it is not an authorization to arm N17 metadata.
N17_READ_PROBE ?= 0
ifneq ($(filter $(N17_READ_PROBE),0 1),$(N17_READ_PROBE))
$(error N17_READ_PROBE must be exactly 0 or 1)
endif

# In the BL1 -> SRAM MCUboot BL2 profile, BL1 consumes the existing lifecycle
# journal and publishes only an ordered slot policy.  BL2 remains the sole
# image validator/launcher.  Keep this opt-in for direct bootloader builds;
# build_dual_image.sh enables it for the MCUboot package.
BL2_BOOT_POLICY ?= 0
ifneq ($(filter $(BL2_BOOT_POLICY),0 1),$(BL2_BOOT_POLICY))
$(error BL2_BOOT_POLICY must be exactly 0 or 1)
endif
ifeq ($(BL2_BOOT_POLICY),1)
CFLAGS += -DBK7258_BL2_BOOT_POLICY_COMPILE_GATE=1u \
          -DBK7258_BL2_BOOT_POLICY_RUNTIME_GATE=1u
endif

# The production MCUboot chain uses a deliberately small, read-only BL1:
# fixed Primary -> Secondary ordering, Manifest authentication, BL2 copy and
# handoff.  Historical N15/N17 lifecycle readers and the Flash writer remain
# available only to their explicit validation profiles.
BL1_MINIMAL ?= 0
ifneq ($(filter $(BL1_MINIMAL),0 1),$(BL1_MINIMAL))
$(error BL1_MINIMAL must be exactly 0 or 1)
endif
CFLAGS += -DBK7258_BL1_MINIMAL=$(BL1_MINIMAL)u

ifeq ($(N15_OTA_VALIDATION),1)
CFLAGS += -DBK7258_BOOT_OTA_SELECT_COMPILE_GATE=1u \
          -DBK7258_BOOT_OTA_SELECT_RUNTIME_GATE=1u \
          -DBK7258_BOOT_OTA_REMAP_COMPILE_GATE=1u \
          -DBK7258_BOOT_OTA_REMAP_RUNTIME_GATE=1u \
          -DBK7258_BOOT_OTA_TRIAL_COMPILE_GATE=1u \
          -DBK7258_BOOT_OTA_TRIAL_RUNTIME_GATE=1u
endif

ifeq ($(N17_READ_PROBE),1)
CFLAGS += -DBK7258_BOOT_N17_SELECT_COMPILE_GATE=1u \
          -DBK7258_BOOT_N17_SELECT_RUNTIME_GATE=1u \
          -DBK7258_BOOT_N17_ECC_VECTOR_SELFTEST=1u
endif

# The self-owned BL1 Manifest is opt-in while the board remains on a
# recoverable development chain.  Enabling it requires an externally signed
# 256-byte record; neither SDK/NuttX sources nor OTP/eFuse are modified.
BL1_MANIFEST_ENFORCE ?= 0
ifneq ($(filter $(BL1_MANIFEST_ENFORCE),0 1),$(BL1_MANIFEST_ENFORCE))
$(error BL1_MANIFEST_ENFORCE must be exactly 0 or 1)
endif

# Optional validation path for the SDK-shaped 4 KiB data partitions
# ota_manifest_a/b.  It is intentionally separate from the established
# executable XIP-tail records and is never enabled by a normal build.
BL1_MANIFEST_RAW_PAGE ?= 0
ifneq ($(filter $(BL1_MANIFEST_RAW_PAGE),0 1),$(BL1_MANIFEST_RAW_PAGE))
$(error BL1_MANIFEST_RAW_PAGE must be exactly 0 or 1)
endif
ifeq ($(BL1_MANIFEST_RAW_PAGE),1)
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
CFLAGS += -DBK7258_BL1_BOOT_CONTROL_STAGING=1u \
          -DBK7258_BL1_BOOT_CONTROL_RAW_OFFSET=0x1000u \
          -DBK7258_BL1_PRIMARY_MANIFEST_XIP=0x02003000u \
          -DBK7258_BL1_SECONDARY_MANIFEST_XIP=0x02004000u
endif

ifeq ($(BL1_MINIMAL),1)
ifneq ($(N15_OTA_VALIDATION),0)
$(error BL1_MINIMAL cannot link the N15 OTA validation profile)
endif
ifneq ($(N17_READ_PROBE),0)
$(error BL1_MINIMAL cannot link the N17 read probe)
endif
ifneq ($(BL1_MANIFEST_RAW_PAGE),0)
$(error BL1_MINIMAL requires the executable Manifest tail, not raw Flash reads)
endif
ifneq ($(BL1_BOOT_CONTROL_STAGING),0)
$(error BL1_MINIMAL cannot read the staging boot-control page)
endif
endif

ifeq ($(BL1_MANIFEST_ENFORCE),1)
ifeq ($(strip $(BL1_MANIFEST_PRIMARY)),)
$(error BL1_MANIFEST_PRIMARY must name a signed record when BL1_MANIFEST_ENFORCE=1)
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
		boot_bl1_manifest_key.o boot_libc.o

BL1_LIFECYCLE_OBJS := boot_ota_select.o boot_ota_select_core.o \
		boot_ota_trial_core.o boot_ota_rotation_core.o \
		boot_ota_rotation_select_core.o boot_ota_rotation_trial_core.o \
		boot_ota_flash_program.o boot_ota_staging_core.o \
		boot_n17_manifest_core.o boot_n17_journal_core.o \
		boot_n17_select.o boot_n17_ecc_wrapper.o \
		bk7258_ota_n17_release_keys.o nuttx_ecc.o

OBJS := $(BL1_CORE_OBJS)
ifeq ($(BL1_MINIMAL),0)
OBJS += $(BL1_LIFECYCLE_OBJS)
endif

$(OBJS): $(PARTITION_HEADER)

.PHONY: all clean verify partition-check bl1-control FORCE

# Make does not track command-line CFLAGS.  These two outputs depend on the
# manifest enforcement setting and must therefore be refreshed when a caller
# switches BL1_MANIFEST_ENFORCE without first cleaning the directory.
FORCE:

all: partition-check bl.bin bl_crc.bin

# This target is deliberately opt-in.  The active v3.1.1.9 board layout still
# boots from the legacy boot slot; producing the official-shaped XIP control
# page must not silently change that default image.
bl1-control: $(BL1_CONTROL_GENERATOR)
	@test -f "$(BL2_IMAGE)" || { echo "missing BL2 image: $(BL2_IMAGE)" >&2; exit 1; }
	python3 $(BL1_CONTROL_GENERATOR) --bl2 "$(BL2_IMAGE)" --out "$(BL1_CONTROL_IMAGE)"

partition-check:
	python3 $(PARTITION_GENERATOR) --check

bootloader.tmp: bootloader.ld $(PARTITION_HEADER)
	$(CC) -E -P -x c -D__LINKER__ \
		-DBK7258_BL1_MINIMAL=$(BL1_MINIMAL) -I../chip/include $< -o $@

start.o: start.S
	$(CC) $(ASFLAGS) -c $< -o $@

boot_main.o: boot_main.c boot_wdt.h boot_bl2_contract.h boot_bl1_manifest.h \
		boot_bl1_policy.h boot_bl1_boot_flag_core.h boot_bl1_handoff_core.h \
		boot_ota_select.h FORCE
	$(CC) $(CFLAGS) -c $< -o $@

boot_clock.o: boot_clock.c
	$(CC) $(CFLAGS) -c $< -o $@

boot_runtime.o: boot_runtime.c
	$(CC) $(CFLAGS) -c $< -o $@

boot_ota_select.o: boot_ota_select.c boot_ota_select.h \
		boot_ota_select_core.h boot_ota_rotation_core.h \
		boot_ota_rotation_select_core.h \
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

boot_n17_manifest_core.o: boot_n17_manifest_core.c \
		boot_n17_manifest_core.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_n17_journal_core.o: boot_n17_journal_core.c \
		boot_n17_journal_core.h boot_n17_manifest_core.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_n17_select.o: boot_n17_select.c boot_n17_select.h \
		boot_n17_journal_core.h boot_n17_manifest_core.h boot_sha256.h \
		boot_n17_ecc_wrapper.h boot_ota_select_core.h
	$(CC) $(CFLAGS) -c $< -o $@

boot_n17_ecc_wrapper.o: boot_n17_ecc_wrapper.c boot_n17_ecc_wrapper.h \
		boot_sha256.h
	$(CC) $(CRYPTO_CFLAGS) -c $< -o $@

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

bk7258_ota_n17_release_keys.o: ../chip/common/bk7258_ota_n17_release_keys.c \
		../chip/include/bk7258_ota_n17_auth.h
	$(CC) $(CFLAGS) -c $< -o $@

nuttx_ecc.raw.o: $(N17_ECC_SOURCE)
	$(CC) $(CRYPTO_CFLAGS) -c $< -o $@

# NuttX ecc.c keeps the P-256 domain tables as mutable objects even though
# Tier-1 only reads them.  Relabel just those input sections while producing
# the repository-local wrapper object: this preserves NuttX source intact and
# prevents their writable flags from tainting the bootloader .rodata segment.
nuttx_ecc.o: nuttx_ecc.raw.o
	$(OBJCOPY) \
	  --rename-section .data.g_curve_p=.rodata.nuttx_ecc_curve_p,alloc,load,readonly,data,contents \
	  --rename-section .data.g_curve_b=.rodata.nuttx_ecc_curve_b,alloc,load,readonly,data,contents \
	  --rename-section .data.g_curve_n=.rodata.nuttx_ecc_curve_n,alloc,load,readonly,data,contents \
	  --rename-section .data.g_curve_g=.rodata.nuttx_ecc_curve_g,alloc,load,readonly,data,contents \
	  $< $@

boot_libc.o: boot_libc.c
	$(CC) $(CFLAGS) -c $< -o $@

bl.elf: $(OBJS) bootloader.tmp
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

bl.bin: bl.elf
	$(OBJCOPY) -O binary $< $@

bl_crc.bin: bl.bin bk7236_pack_min_bootloader.py FORCE
	python3 bk7236_pack_min_bootloader.py --in bl.bin --out bl_crc.bin $(BL1_MANIFEST_ARG)

verify: bl_crc.bin
	@for symbol in Reset_Handler c_main fal_partition_table \
		bk7258_bl1_handoff_vector_valid; do \
		$(NM) -n bl.elf | grep -qw "$$symbol" || { \
			echo "bootloader missing required symbol: $$symbol" >&2; \
			exit 1; \
		}; \
	done
ifeq ($(BL1_MANIFEST_ENFORCE),1)
	@$(NM) -n bl.elf | grep -qw bk7258_bl1_manifest_verify_at || { \
		echo 'Manifest-enforcing BL1 lacks its verifier' >&2; \
		exit 1; \
	}
endif
ifeq ($(BL1_MINIMAL),1)
	@if $(NM) -n bl.elf | grep -Eq 'boot_ota_|boot_n17_|bk7258_ota_n17|nuttx_ecc'; then \
		echo 'minimal BL1 unexpectedly links OTA/N17 lifecycle code' >&2; \
		exit 1; \
	fi
else
	$(NM) -n bl.elf | grep -E 'boot_ota_select_app|bk7258_boot_ota_select_core'
endif
	$(OBJDUMP) -s -j .rodata bl.elf | sed -n '1,40p'

clean:
	rm -f *.o *.su bl.elf bl.bin bl.map bl_crc.bin bl_crc.json bootloader.tmp
