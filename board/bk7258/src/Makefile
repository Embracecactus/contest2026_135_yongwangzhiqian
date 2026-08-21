############################################################################
# contest2026_135_yongwangzhiqian/board/bk7258/src/Makefile
#
# SPDX-License-Identifier: Apache-2.0
############################################################################

include $(TOPDIR)/Make.defs

BK7258_CHIP_DIR := $(TOPDIR)$(DELIM)arch$(DELIM)$(CONFIG_ARCH)$(DELIM)src$(DELIM)chip
ifeq ($(CONFIG_BK7258_AP_CORE),y)
BK7258_BOARD_SDK_ROLE := ap
else
BK7258_BOARD_SDK_ROLE := cp
endif
BK7258_BOARD_SDK_DIR := $(BK7258_SDK_BUNDLE_ROOT)$(DELIM)$(BK7258_BOARD_SDK_ROLE)

CFLAGS += ${INCDIR_PREFIX}$(BK7258_CHIP_DIR)$(DELIM)common
CFLAGS += ${INCDIR_PREFIX}$(BK7258_CHIP_DIR)$(DELIM)cp
CFLAGS += ${INCDIR_PREFIX}$(BK7258_BOARD_SDK_DIR)$(DELIM)include
CFLAGS += ${INCDIR_PREFIX}$(BK7258_BOARD_SDK_DIR)$(DELIM)config
CFLAGS += -DCONFIG_FREERTOS=0 -DCONFIG_FREERTOS_SMP=0
CFLAGS += -DCONFIG_SYSTEM_CTRL=1 -DCONFIG_NMI_WDT_EN=1
CFLAGS += -DCONFIG_INT_WDT=0 -DCONFIG_TASK_WDT=0
CFLAGS += -DCONFIG_INT_AON_WDT=0 -DCONFIG_DEBUG_VERSION=0
CFLAGS += -DCONFIG_GPIO_RETENTION_SUPPORT=0
CFLAGS += -DCONFIG_AON_RTC=0 -DCONFIG_AON_RTC_64BIT=0
CFLAGS += -DCONFIG_SOC_BK7256XX=0 -DCONFIG_SOC_BK7236XX=0
CFLAGS += -DCONFIG_SOC_BK7239XX=0
VPATH += $(BK7258_BOARD_VARIANT_DIR)$(DELIM)src
DEPPATH += --dep-path $(BK7258_BOARD_VARIANT_DIR)$(DELIM)src

CSRCS = bk7258_appinit.c bk7258_boot.c bk7258_bringup.c
CSRCS += bk7258_platform.c bk7258_board_bringup.c

# The product Flash layout and its NuttX MTD composition belong to the
# logical board.  The chip/SDK layer supplies only the raw Flash mechanism.
# CP remains the sole Flash owner, so AP never links this policy wrapper.

ifneq ($(CONFIG_BK7258_AP_CORE),y)
CSRCS += bk7258_boot_slot.c bk7258_sdk_partition.c
ifneq ($(filter y,$(CONFIG_BK7258_BT_IPC) $(CONFIG_BK7258_WIFI_VNET)),)
CSRCS += bk7258_mac_storage.c
endif
ifeq ($(CONFIG_BK7258_FLASH_MTD),y)
CSRCS += bk7258_flash_mtd.c bk7258_flash_guard.c
endif
ifeq ($(CONFIG_BK7258_OTA),y)
CSRCS += bk7258_ota.c
ifneq ($(CONFIG_BK7258_FLASH_MTD),y)
CSRCS += bk7258_flash_guard.c
endif
endif
endif

# The immutable physical-board descriptor is linked into both roles.  AP
# lower halves consume MIC/SDIO/audio bindings, while CP GPIO consumers use
# the same selected board's LED/button binding.  Unavailable capabilities are
# rejected by Kconfig before their accessor can be referenced.

# Physical-device bindings belong to the selected board archive.  Chip
# lower-halves expose reusable controller APIs and never select a PCB source.

ifeq ($(CONFIG_BK7258_AUD),y)
ifeq ($(CONFIG_BK7258_BOARD_T5_BOARD),y)
CSRCS += bk7258_t5_board_audio.c
else ifeq ($(CONFIG_BK7258_BOARD_T5AI_CORE),y)
CSRCS += bk7258_t5ai_core_audio.c
else ifeq ($(CONFIG_BK7258_BOARD_AIDK_AI_TOY),y)
CSRCS += bk7258_aidk_board_audio.c
else
$(error BK7258_AUD requires a physical-board audio binding)
endif
endif

ifeq ($(CONFIG_BK7258_AIDK_SD_NAND),y)
CSRCS += bk7258_aidk_board_sdio.c
endif

ifeq ($(CONFIG_BK7258_LCD),y)
VPATH += $(BOARD_DIR)$(DELIM)panels
DEPPATH += --dep-path $(BOARD_DIR)$(DELIM)panels
CFLAGS += ${INCDIR_PREFIX}$(BOARD_DIR)$(DELIM)panels
CSRCS += ili9488_rgb.c bk7258_t5_board_lcd.c
endif

ifeq ($(CONFIG_BK7258_GT1151),y)
CSRCS += bk7258_t5_board_gt1151.c
endif

ifeq ($(CONFIG_BK7258_T5_BOARD_RGB_LCD_PWM_VALIDATION),y)
CSRCS += bk7258_t5_board_rgb_lcd_backlight.c
endif

ifeq ($(CONFIG_BK7258_T5_BOARD_CAMERA),y)
CSRCS += bk7258_t5_board_camera.c bk7258_t5_board_camera_i2c.c
endif

ifeq ($(CONFIG_BK7258_T5_BOARD_TF_SLOT),y)
CSRCS += bk7258_t5_board_sdio.c
ifeq ($(CONFIG_BK7258_T5_BOARD_TF_VALIDATION),y)
CSRCS += bk7258_t5_board_tf_validation.c
endif
endif

include $(TOPDIR)/boards/Board.mk
