############################################################################
# SPDX-License-Identifier: Apache-2.0
#
# BK7258 SDK bundle selector shared by the classic Make build.
#
# Every SDK bundle, including the preserved legacy baseline, lives below:
#   armino_as_lib/versions/<version>/{cp,ap}
############################################################################

BK7258_SDK_SUPPORTED_BUNDLE_VERSIONS := legacy v3.1.1.9
BK7258_SDK_BUNDLE_VERSION ?= v3.1.1.9

ifeq ($(filter $(BK7258_SDK_BUNDLE_VERSION),$(BK7258_SDK_SUPPORTED_BUNDLE_VERSIONS)),)
$(error Unsupported BK7258_SDK_BUNDLE_VERSION='$(BK7258_SDK_BUNDLE_VERSION)'; supported: $(BK7258_SDK_SUPPORTED_BUNDLE_VERSIONS))
endif

BK7258_SDK_BUNDLE_BASE := $(BOARD_DIR)$(DELIM)bk_idk$(DELIM)armino_as_lib
BK7258_SDK_BUNDLE_ROOT := $(BK7258_SDK_BUNDLE_BASE)$(DELIM)versions$(DELIM)$(BK7258_SDK_BUNDLE_VERSION)
