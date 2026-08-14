############################################################################
# SPDX-License-Identifier: Apache-2.0
#
# BK7258 SDK bundle selector shared by the classic Make build.
#
# Every SDK bundle, including the preserved legacy baseline, lives below:
#   armino_as_lib/versions/<version>/{cp,ap}
############################################################################

BK7258_SDK_SUPPORTED_BUNDLE_VERSIONS := legacy v3.1.1.9 \
	v3.1.1.9-sdio4
BK7258_SDK_BUNDLE_VERSION ?= v3.1.1.9
BK7258_SDK_DEFAULT_BUNDLE_VERSION := $(BK7258_SDK_BUNDLE_VERSION)
BK7258_CP_SDK_BUNDLE_VERSION ?= $(BK7258_SDK_DEFAULT_BUNDLE_VERSION)
BK7258_AP_SDK_BUNDLE_VERSION ?= $(BK7258_SDK_DEFAULT_BUNDLE_VERSION)

ifeq ($(CONFIG_BK7258_AP_CORE),y)
BK7258_SDK_BUNDLE_VERSION := $(BK7258_AP_SDK_BUNDLE_VERSION)
else
BK7258_SDK_BUNDLE_VERSION := $(BK7258_CP_SDK_BUNDLE_VERSION)
endif

ifeq ($(filter $(BK7258_SDK_BUNDLE_VERSION),$(BK7258_SDK_SUPPORTED_BUNDLE_VERSIONS)),)
$(error Unsupported BK7258_SDK_BUNDLE_VERSION='$(BK7258_SDK_BUNDLE_VERSION)'; supported: $(BK7258_SDK_SUPPORTED_BUNDLE_VERSIONS))
endif

BK7258_SDK_BUNDLE_BASE := $(BOARD_DIR)$(DELIM)bk_idk$(DELIM)armino_as_lib
BK7258_SDK_BUNDLE_ROOT := $(BK7258_SDK_BUNDLE_BASE)$(DELIM)versions$(DELIM)$(BK7258_SDK_BUNDLE_VERSION)

ifeq ($(BK7258_SDK_BUNDLE_VERSION),v3.1.1.9-sdio4)
ifneq ($(CONFIG_BK7258_AP_CORE),y)
$(error BK7258 SDK bundle 'v3.1.1.9-sdio4' is AP-only)
endif
endif

ifeq ($(CONFIG_BK7258_AP_CORE),y)
ifeq ($(CONFIG_BK7258_SDIO_4BIT),y)
ifneq ($(BK7258_SDK_BUNDLE_VERSION),v3.1.1.9-sdio4)
$(error BK7258 four-bit SDIO requires AP SDK bundle 'v3.1.1.9-sdio4')
endif
else ifeq ($(BK7258_SDK_BUNDLE_VERSION),v3.1.1.9-sdio4)
$(error BK7258 AP SDK bundle 'v3.1.1.9-sdio4' requires CONFIG_BK7258_SDIO_4BIT=y)
endif
endif
