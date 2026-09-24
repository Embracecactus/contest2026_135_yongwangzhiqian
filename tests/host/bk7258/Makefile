############################################################################
# Host-side unit-test build for maintained BK7258 chip modules.
#
# The REAL implementation is compiled unmodified, except that the ARM-only
# "__asm volatile ("dmb sy" ::: "memory");" barrier is neutralized in a
# *throwaway copy* (the real source file is never touched).  Everything else
# is mocked under mocks/: the Beken SDK mailbox transport, the AMP boot
# state, and the NuttX sem/kthread/spinlock/clock APIs.  The transport is
# controllable from the test so the notify coalescing / -EAGAIN retry /
# lost-TX-complete paths are exercised deterministically on the host.
############################################################################

CC      ?= gcc
CXX     ?= g++
CFLAGS  := -std=gnu11 -pthread -Wall -Wextra -O2

MOCKS   := mocks
BUILD   := build

# Match the NuttX/OpenVela build convention: resolve the repository boundary
# once, then express cross-tree inputs from that named root.  This keeps the
# fixture independent of the absolute checkout path and avoids scattering
# directory-depth assumptions throughout the Makefile.

TEST_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPOSITORY_ROOT := $(abspath $(TEST_ROOT)/../../..)
OPENVELA_ROOT := $(abspath $(REPOSITORY_ROOT)/..)
BOARD_ROOT := $(REPOSITORY_ROOT)/boards/bk7258

# Real chip implementation, relative to tests/host/bk7258.  Keep host fixtures
# on the maintained chips/bk7258 ownership
# boundary; the retired board/bk7258 compatibility tree no longer exists.
CHIP_ROOT := $(REPOSITORY_ROOT)/chips/bk7258
BOARD_COMMON_SRC := $(BOARD_ROOT)/common/src
REAL_C    := $(CHIP_ROOT)/common/bk7258_rptun_mbox.c
REAL_H    := $(CHIP_ROOT)/common/bk7258_rptun_mbox.h
TOOLS_ROOT := $(REPOSITORY_ROOT)/tools/bk7258
PARTITION_CSV := $(BOARD_ROOT)/common/partitions/bk7258/bk7258_ab_agent_onchip_persistent.csv
PARTITION_DIR := $(BUILD)/layout
PARTITION_HEADER := $(PARTITION_DIR)/bk7258_partitions.h

TARGET  := $(BUILD)/test_bk7258_rptun_mbox
POLICY_TARGET := $(BUILD)/test_boot_bl1_policy
PM_ACTIVITY_TARGET := $(BUILD)/test_bk7258_pm_activity
RPTUN_CORE_CP_TARGET := $(BUILD)/test_bk7258_rptun_core_cp
RPTUN_CORE_AP_TARGET := $(BUILD)/test_bk7258_rptun_core_ap
STAGE_RUNNER_TARGET := $(BUILD)/test_bk7258_stage_runner
SDK_RUNTIME_TARGET := $(BUILD)/test_bk7258_sdk_runtime
CP_PLATFORM_SUCCESS_TARGET := $(BUILD)/test_bk7258_cp_platform_success
CP_PLATFORM_SDK_FAIL_TARGET := $(BUILD)/test_bk7258_cp_platform_sdk_fail
CP_PLATFORM_WDT_FAIL_TARGET := $(BUILD)/test_bk7258_cp_platform_wdt_fail
CP_PLATFORM_RESET_MARKER_FAIL_TARGET := $(BUILD)/test_bk7258_cp_platform_reset_marker_fail
CP_PLATFORM_OTA_LAYOUT_FAIL_TARGET := $(BUILD)/test_bk7258_cp_platform_ota_layout_fail
CP_PLATFORM_AP_START_FAIL_TARGET := $(BUILD)/test_bk7258_cp_platform_ap_start_fail
CP_PLATFORM_PSRAM_FAIL_TARGET := $(BUILD)/test_bk7258_cp_platform_psram_fail
CP_PLATFORM_OTA_POLICY_FAIL_TARGET := $(BUILD)/test_bk7258_cp_platform_ota_policy_fail
CP_PLATFORM_LEGACY_LEAVES_TARGET := $(BUILD)/test_bk7258_cp_platform_legacy_leaves
CP_PLATFORM_OPTIONAL_POLICY_TARGET := $(BUILD)/test_bk7258_cp_platform_optional_policy
CP_PLATFORM_RADIO_FAIL_TARGET := $(BUILD)/test_bk7258_cp_platform_radio_fail
CP_PLATFORM_HEAP_ORDER_TARGET := $(BUILD)/test_bk7258_cp_platform_heap_order
OTA_CONFIRM_NO_SUPERVISOR_TARGET := $(BUILD)/test_bk7258_ota_confirmation_no_supervisor
OTA_CONFIRM_SUPERVISOR_TARGET := $(BUILD)/test_bk7258_ota_confirmation_supervisor
OTA_CONFIRMATION_TARGETS := $(OTA_CONFIRM_NO_SUPERVISOR_TARGET) \
	$(OTA_CONFIRM_SUPERVISOR_TARGET)
AP_PLATFORM_SUCCESS_TARGET := $(BUILD)/test_bk7258_ap_platform_success
AP_PLATFORM_SDK_FAIL_TARGET := $(BUILD)/test_bk7258_ap_platform_sdk_fail
AP_PLATFORM_PSRAM_FAIL_TARGET := $(BUILD)/test_bk7258_ap_platform_psram_fail
AP_PLATFORM_PM_FAIL_TARGET := $(BUILD)/test_bk7258_ap_platform_pm_fail
AP_ENTRY_TARGET := $(BUILD)/test_bk7258_ap_entry
STORAGE_UNCONFIGURED_TARGET := $(BUILD)/test_bk7258_storage_unbound
STORAGE_CONFIG_TARGET := $(BUILD)/test_bk7258_storage_contract
STORAGE_LAYOUT_TARGET := $(BUILD)/test_bk7258_storage_layout
STORAGE_LOCKING_TARGET := $(BUILD)/test_bk7258_storage_locking
RADIO_STORAGE_TARGET := $(BUILD)/test_bk7258_radio_storage
BOOT_SLOT_TARGET := $(BUILD)/test_bk7258_boot_slot
STORAGE_CONTRACT_TARGETS := $(STORAGE_UNCONFIGURED_TARGET) \
	$(STORAGE_CONFIG_TARGET) $(STORAGE_LAYOUT_TARGET) \
	$(STORAGE_LOCKING_TARGET) $(RADIO_STORAGE_TARGET)
OTA_CONTRACT_TARGETS := $(STORAGE_CONTRACT_TARGETS) $(BOOT_SLOT_TARGET)
ACTIVE_IMAGE_TARGET := $(BUILD)/test_bk7258_active_image
OTA_MANAGER_TARGET := $(BUILD)/test_bk7258_ota_manager
ACTIVE_IMAGE_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2 \
	-DCONFIG_BK7258_OTA_MANAGER
OTA_CONTRACT_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -pthread
OTA_CONFIRM_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2 -pthread \
	-DCONFIG_BK7258_OTA
OTA_CONTRACT_INC := -I $(MOCKS) -I $(CHIP_ROOT)/include -I $(CHIP_ROOT)/cp
RADIO_STORAGE_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -pthread \
	-DCONFIG_BK7258_BT_IPC
RESET_MARKER_UNBOUND_TARGET := $(BUILD)/test_bk7258_reset_marker_unbound
RESET_MARKER_BINDING_TARGET := $(BUILD)/test_bk7258_reset_marker_binding
RESET_MARKER_CAPTURE_TARGET := $(BUILD)/test_bk7258_reset_marker_capture
RESET_MARKER_EMPTY_TARGET := $(BUILD)/test_bk7258_reset_marker_empty
RESET_MARKER_TORN_TARGET := $(BUILD)/test_bk7258_reset_marker_torn
RESET_MARKER_OPERATIONS_TARGET := $(BUILD)/test_bk7258_reset_marker_operations
RESET_CAUSE_TARGET := $(BUILD)/test_bk7258_reset_cause
SYSTEM_RESET_TARGET := $(BUILD)/test_bk7258_system_reset
FLASH_TARGET := $(BUILD)/test_bk7258_flash
SDK_PARTITION_TARGET := $(BUILD)/test_bk7258_sdk_partition
RESET_MARKER_TARGETS := $(RESET_MARKER_UNBOUND_TARGET) \
	$(RESET_MARKER_BINDING_TARGET) $(RESET_MARKER_CAPTURE_TARGET) \
	$(RESET_MARKER_EMPTY_TARGET) $(RESET_MARKER_TORN_TARGET) \
	$(RESET_MARKER_OPERATIONS_TARGET) $(RESET_CAUSE_TARGET) \
	$(SYSTEM_RESET_TARGET)
RESET_MARKER_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -pthread
RESET_MARKER_INC := -I $(MOCKS) -I $(CHIP_ROOT)/include \
	-I $(CHIP_ROOT)/common -I $(CHIP_ROOT)/cp
FLASH_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2 -pthread \
	-DCONFIG_BK7258_FLASH_MTD -Dferr=_err
FLASH_INC := -I $(MOCKS) -I $(CHIP_ROOT)/include -I $(CHIP_ROOT)/common \
	-I $(CHIP_ROOT)/cp
RESET_MARKER_TEST_SRCS := test_bk7258_reset_marker.c \
	$(CHIP_ROOT)/cp/bk7258_reset_marker.c \
	$(CHIP_ROOT)/cp/bk7258_storage.c $(MOCKS)/mock_nuttx_ap.c
POLICY_CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g \
	-fsanitize=address,undefined -fno-omit-frame-pointer
PM_ACTIVITY_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2
RPTUN_CORE_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 \
	-DTEST_BK7258_RPTUN_CORE
STAGE_RUNNER_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -pthread
PLATFORM_TEST_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2 -pthread \
	-DCONFIG_BK7258_SDK_IPC_RUNTIME -DCONFIG_BK7258_PSRAM \
	-DCONFIG_BK7258_PSRAM_SYSTEM_HEAP
CP_PLATFORM_CFLAGS := $(PLATFORM_TEST_CFLAGS) \
	-DCONFIG_BK7258_AP_CONTROL -DCONFIG_BK7258_AP_AUTOSTART \
	-DCONFIG_BK7258_AP_AUTOSTART_TIMEOUT_MS=42u \
	-DCONFIG_BK7258_OTA -DCONFIG_BK7258_OTA_AUTO_CONFIRM \
	-DCONFIG_BK7258_WDT -DCONFIG_BK7258_WDT_PRETIMEOUT_PANIC \
	-DCONFIG_BK7258_IRDA -DCONFIG_BK7258_PSRAM_SYSTEM_HEAP_SIZE=4096u
CP_PLATFORM_LEGACY_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2 \
	-pthread -DCONFIG_BK7258_SDK_IPC_RUNTIME -DCONFIG_BK7258_SWD_DEBUG \
	-DCONFIG_BK7258_IRDA -DCONFIG_BK7258_GPIO_LOWERHALF \
	-DCONFIG_BK7258_TOUCH
CP_PLATFORM_OPTIONAL_POLICY_CFLAGS := -std=gnu11 -Wall -Wextra -Werror \
	-O2 -pthread -DCONFIG_BK7258_OTA
CP_PLATFORM_RADIO_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2 \
	-pthread -DCONFIG_BK7258_BT_IPC
CP_PLATFORM_TEST_SRCS := test_bk7258_cp_platform.c \
	$(CHIP_ROOT)/cp/bk7258_cp_platform.c \
	$(BOARD_COMMON_SRC)/bk7258_cp_bringup.c \
	$(CHIP_ROOT)/common/bk7258_stage_runner.c mocks/mock_nuttx_ap.c
AP_PLATFORM_CFLAGS := $(PLATFORM_TEST_CFLAGS) -DCONFIG_BK7258_AP_CORE \
	-DCONFIG_BK7258_PM_CLOCK -DCONFIG_BK7258_TEMPERATURE \
	-DCONFIG_BK7258_PSRAM_SYSTEM_HEAP_SIZE=2048u
AP_ENTRY_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2 \
	-DCONFIG_BK7258_AP_CORE -DCONFIG_BK7258_AP_APPLICATION_LIFECYCLE \
	-DCONFIG_BK7258_BOARD_DEFERRED_INIT
PLATFORM_TEST_INC := -I $(MOCKS) -I $(CHIP_ROOT)/include \
	-I $(CHIP_ROOT)/common -I $(CHIP_ROOT)/cp -I $(BOARD_COMMON_SRC)
HEADER_PUBLIC_HEADERS := $(notdir \
	$(wildcard $(CHIP_ROOT)/include/bk7258_*.h))
HEADER_PRIVATE_HEADERS := bk7258_board.h bk7258_ota_trial.h bk7258_wdt.h \
	bk7258_storage_configure.h bk7258_storage_internal.h \
	bk7258_radio_storage.h bk7258_ota_engine_internal.h \
	bk7258_ota_flash_internal.h bk7258_ota_image.h \
	bk7258_reset_marker_internal.h bk7258_internal.h
HEADER_CHECK_HEADERS := $(sort $(HEADER_PUBLIC_HEADERS) \
	$(HEADER_PRIVATE_HEADERS))
HEADER_CHECK_PATHS := $(sort \
	$(wildcard $(CHIP_ROOT)/include/bk7258_*.h) \
	$(wildcard $(CHIP_ROOT)/common/*.h) \
	$(wildcard $(CHIP_ROOT)/cp/*.h) \
	$(wildcard $(CHIP_ROOT)/ap/*.h) \
	$(wildcard $(CHIP_ROOT)/bootloader/*.h) \
	$(wildcard $(CHIP_ROOT)/bootloader/bl2/*.h) \
	$(wildcard $(CHIP_ROOT)/bootloader/bl2/include/*.h) \
	$(wildcard $(CHIP_ROOT)/bootloader/bl2/include/*/*.h) \
	$(wildcard $(BOARD_ROOT)/common/include/*.h) \
	$(wildcard $(BOARD_ROOT)/common/src/*.h) \
	$(wildcard $(BOARD_ROOT)/*/include/*.h))
HEADER_CHECK_FEATURE_FLAGS := -DCONFIG_BK7258_AUD \
	-DCONFIG_BK7258_AUD_DAC_EQ -DCONFIG_BK7258_CAN \
	-DCONFIG_BK7258_DMIC -DCONFIG_BK7258_ETH \
	-DCONFIG_BK7258_GPIOE -DCONFIG_BK7258_I2S \
	-DCONFIG_BK7258_LCD_RGB -DCONFIG_BK7258_LCD_8080 \
	-DCONFIG_BK7258_LCD_SPI -DCONFIG_BK7258_LIN \
	-DCONFIG_BK7258_MIC -DCONFIG_BK7258_SDIO \
	-DCONFIG_BK7258_SDIO_4BIT -DCONFIG_BK7258_SLCD \
	-DCONFIG_BK7258_SPI -DCONFIG_BK7258_SPI_BUS=0 \
	-DCONFIG_BK7258_QSPI -DCONFIG_BK7258_PWM \
	-DCONFIG_BK7258_RTC -DCONFIG_BK7258_SARADC \
	-DCONFIG_BK7258_SDMADC -DCONFIG_BK7258_TIMER \
	-DCONFIG_BK7258_I2C -DCONFIG_BK7258_TOUCH \
	-DCONFIG_BK7258_OTA -DCONFIG_BK7258_OTA_AUTO_CONFIRM \
	-DCONFIG_BK7258_OTA_MANAGER -DCONFIG_BK7258_OTA_RPMSG \
	-DCONFIG_BK7258_OTA_SOURCE_FILE \
	-DCONFIG_BK7258_OTA_SOURCE_HTTP -DCONFIG_BK7258_RPTUN \
	-DCONFIG_BK7258_WIFI_VNET \
	-DCONFIG_BK7258_TEMPERATURE_VALIDATION -DCONFIG_BK7258_WDT \
	-DCONFIG_BK7258_WDT_PRETIMEOUT_PANIC \
	-DCONFIG_BK7258_WDT_FAULT_INJECTION \
	-DCONFIG_BK7258_SARADC_SERVER \
	-DCONFIG_BK7258_SDK_IRQ_TIMER_TEST \
	-DCONFIG_BK7258_SDK_TIMER_SELFTEST \
	-DCONFIG_BK7258_GPIO_FOUNDATION_TEST \
	-DCONFIG_BK7258_GPIO_IRQ_TEST \
	-DCONFIG_BK7258_GPIO_LOWERHALF \
	-DCONFIG_BK7258_USBMODE -DCONFIG_BK7258_USBCDC \
	-DCONFIG_BK7258_USBMSC \
	-DCONFIG_BK7258_MCUBOOT_IMAGE -DCONFIG_BK7258_PSRAM_MEDIA \
	-DCONFIG_BK7258_AP_CONTROL
HEADER_CHECK_FLAGS := -Wall -Wextra -Werror -fsyntax-only \
	-I $(MOCKS) -I $(CHIP_ROOT)/include -I $(CHIP_ROOT)/common \
	-I $(CHIP_ROOT)/cp -I $(BOARD_ROOT)/common/include \
	-I $(BOARD_ROOT)/common/src \
	-I $(PARTITION_DIR)
VOICE_PACK_ROOT := $(REPOSITORY_ROOT)/app/bk7258
VOICE_PACK_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2
DISPLAY_PACK_TARGET := $(BUILD)/test_bk7258_display_pack
DISPLAY_PACK_FIXTURE := $(BUILD)/shaniu-default-v1.bkep
DISPLAY_PACK_SOURCE := $(VOICE_PACK_ROOT)/assets/display/shaniu-default-v1.json
DISPLAY_RPC_TARGET := $(BUILD)/test_bk7258_display_rpc
HEALTH_CORE_TARGET := $(BUILD)/test_bk7258_health_core
MOTION_CORE_TARGET := $(BUILD)/test_bk7258_motion_core
NFC_CORE_TARGET := $(BUILD)/test_bk7258_nfc_core
SENSOR_RPC_TARGETS := $(BUILD)/test_bk7258_motion_rpc \
	$(BUILD)/test_bk7258_nfc_rpc
SENSOR_RPC_SHIM := $(BUILD)/sensor-rpc-include
SENSOR_RPC_HEADERS := nuttx/config.h nuttx/irq.h nuttx/mutex.h \
	nuttx/semaphore.h nuttx/signal.h nuttx/spinlock.h nuttx/clock.h \
	nuttx/uorb.h nuttx/rpmsg/rpmsg.h nuttx/sensors/ioctl.h
VISION_CORE_TARGET := $(BUILD)/test_bk7258_vision_core
VISION_FEEDBACK_TARGET := $(BUILD)/test_bk7258_vision_feedback
USBMODE_LEASE_TARGET := $(BUILD)/test_bk7258_usbmode_lease
USBMODE_LEASE_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2 -pthread \
	-DCONFIG_BK7258_USBMODE -DCONFIG_BK7258_AP_CORE \
	-DCONFIG_BK7258_USBCDC -DCONFIG_BK7258_USBMSC \
	-DCONFIG_BK7258_USBMODE_BLOCKDEV=\"/dev/mmcsd0\" \
	-DCONFIG_BK7258_USBMODE_REENUM_DELAY_MS=1
VISION_RECORD_TARGET := $(BUILD)/test_bk7258_vision_record
MEDIA_VOLUME_TARGET := $(BUILD)/test_bk7258_media_volume
PRODUCT_KEYS_TARGET := $(BUILD)/test_bk7258_product_keys
VOICE_MEDIA_RECORDER_TARGET := $(BUILD)/test_bk7258_agent_media_recorder
MEDIA_INCLUDE := $(OPENVELA_ROOT)/apps/frameworks/multimedia/media/include

.PHONY: all check check-layers check-package-tools clean bl1 bl2 ap headers platform storage-contract flash provision active-image run-active-image \
	ota-contract reset-marker display-pack display-rpc health-core motion-core nfc-core vision-core vision-feedback usbmode-lease product-keys \
	vision-record media-volume voice-media-recorder run \
	run-core run-bl1 run-bl2 run-ap run-headers run-platform \
	run-storage-contract run-ota-contract run-ota-confirmation run-ota-manager \
		run-reset-marker run-flash run-display-pack run-display-rpc run-health-core run-motion-core run-nfc-core run-vision-core run-vision-feedback \
		run-vision-record run-media-volume run-usbmode-lease run-product-keys \
			run-voice-media-recorder \
		ota-confirmation run-provision

.PHONY: run-media-audio-session run-wifi-native-lease
.PHONY: run-motion-rpc run-nfc-rpc

check:
	@set -eu; \
	printf '%s\n' 'BK7258_HOST_TEST_BEGIN'; \
	printf 'git_commit=%s\n' "$$(git rev-parse --verify HEAD)"; \
	printf 'cc=%s\n' "$$($(CC) --version | sed -n '1p')"; \
	printf 'python=%s\n' "$$(python3 --version 2>&1)"; \
	printf 'cmocka=%s\n' "$$(pkg-config --modversion cmocka)"; \
	printf '%s\n' 'sanitizers=address,undefined:test_boot_bl1_policy'; \
	printf 'partition_csv_sha256=%s\n' \
	  "$$(sha256sum $(PARTITION_CSV) | awk '{print $$1}')"; \
	$(MAKE) clean >/dev/null; \
	$(MAKE) check-layers; \
	$(MAKE) check-package-tools; \
	ASAN_OPTIONS=$${ASAN_OPTIONS:-detect_leaks=0}; \
	export ASAN_OPTIONS; \
	$(MAKE) run; \
	printf '%s\n' 'BK7258_HOST_TEST_PASS'

check-layers:
	@python3 $(TOOLS_ROOT)/bk7258.py verify layers

check-package-tools:
	@python3 ./test_bk7258_layers.py
	@python3 ./test_bk7258_package_delivery.py
	@python3 ./test_bk7258_display_assets.py
	@python3 ./test_bk7258_aidk_hil_contract.py

all: $(TARGET) $(POLICY_TARGET) $(PM_ACTIVITY_TARGET) \
	$(RPTUN_CORE_CP_TARGET) $(RPTUN_CORE_AP_TARGET) \
	$(STAGE_RUNNER_TARGET) $(SDK_RUNTIME_TARGET) \
	$(CP_PLATFORM_SUCCESS_TARGET) \
	$(CP_PLATFORM_SDK_FAIL_TARGET) $(CP_PLATFORM_WDT_FAIL_TARGET) \
	$(CP_PLATFORM_RESET_MARKER_FAIL_TARGET) \
	$(CP_PLATFORM_OTA_LAYOUT_FAIL_TARGET) $(CP_PLATFORM_AP_START_FAIL_TARGET) \
	$(CP_PLATFORM_PSRAM_FAIL_TARGET) $(CP_PLATFORM_OTA_POLICY_FAIL_TARGET) \
	$(CP_PLATFORM_LEGACY_LEAVES_TARGET) $(CP_PLATFORM_OPTIONAL_POLICY_TARGET) \
	$(CP_PLATFORM_RADIO_FAIL_TARGET) \
	$(AP_PLATFORM_SUCCESS_TARGET) $(AP_PLATFORM_SDK_FAIL_TARGET) \
	$(AP_PLATFORM_PSRAM_FAIL_TARGET) $(AP_PLATFORM_PM_FAIL_TARGET) \
		$(AP_ENTRY_TARGET) \
		$(OTA_CONTRACT_TARGETS) $(OTA_CONFIRMATION_TARGETS) $(OTA_MANAGER_TARGET) \
		$(ACTIVE_IMAGE_TARGET) \
	$(RESET_MARKER_TARGETS) $(FLASH_TARGET) $(SDK_PARTITION_TARGET) \
		$(DISPLAY_PACK_TARGET) $(DISPLAY_RPC_TARGET) \
		$(HEALTH_CORE_TARGET) \
		$(MOTION_CORE_TARGET) \
		$(NFC_CORE_TARGET) \
		$(VISION_CORE_TARGET) \
		$(VISION_FEEDBACK_TARGET) \
		$(USBMODE_LEASE_TARGET) \
	$(PRODUCT_KEYS_TARGET) \
	$(VOICE_MEDIA_RECORDER_TARGET) \
	bl1 bl2 ap

run: run-motion-rpc run-nfc-rpc run-wifi-native-lease run-camera-storage run-vision-record run-media-volume run-headers run-core run-platform run-flash run-ota-confirmation run-ota-manager run-active-image \
	run-bl1 run-bl2 run-ap run-display-pack run-display-rpc \
	run-health-core run-motion-core run-nfc-core run-vision-core run-vision-feedback \
	run-usbmode-lease run-product-keys \
	run-voice-media-recorder run-voice-media-player

$(DISPLAY_PACK_FIXTURE): $(DISPLAY_PACK_SOURCE) \
	$(TOOLS_ROOT)/bk7258.py $(TOOLS_ROOT)/_lib/display_assets.py | $(BUILD)
	python3 $(TOOLS_ROOT)/bk7258.py package eye-pack \
		--source $(DISPLAY_PACK_SOURCE) --output $@

$(DISPLAY_PACK_TARGET): test_bk7258_display_pack.c \
	$(VOICE_PACK_ROOT)/bk7258_display_pack.c \
	$(VOICE_PACK_ROOT)/bk7258_display_pack.h \
	$(VOICE_PACK_ROOT)/bk7258_display_store.c \
	$(VOICE_PACK_ROOT)/bk7258_display_store.h \
	$(DISPLAY_PACK_FIXTURE) | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) test_bk7258_display_pack.c \
		$(VOICE_PACK_ROOT)/bk7258_display_pack.c \
		$(VOICE_PACK_ROOT)/bk7258_display_store.c \
		-I $(VOICE_PACK_ROOT) -o $@

display-pack: $(DISPLAY_PACK_TARGET)

run-display-pack: display-pack
	./$(DISPLAY_PACK_TARGET) $(DISPLAY_PACK_FIXTURE)

$(DISPLAY_RPC_TARGET): test_bk7258_display_rpc.c \
	$(VOICE_PACK_ROOT)/bk7258_display_rpc_core.c \
	$(VOICE_PACK_ROOT)/bk7258_display_rpc_core.h \
	$(VOICE_PACK_ROOT)/bk7258_display_protocol.h \
	$(VOICE_PACK_ROOT)/bk7258_display_service.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) test_bk7258_display_rpc.c \
		$(VOICE_PACK_ROOT)/bk7258_display_rpc_core.c \
		-I $(VOICE_PACK_ROOT) -o $@

display-rpc: $(DISPLAY_RPC_TARGET)

run-display-rpc: display-rpc
	./$(DISPLAY_RPC_TARGET)

$(HEALTH_CORE_TARGET): test_bk7258_health_core.c \
	$(VOICE_PACK_ROOT)/bk7258_health_core.c \
	$(VOICE_PACK_ROOT)/bk7258_health_core.h \
	$(VOICE_PACK_ROOT)/bk7258_health_protocol.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) test_bk7258_health_core.c \
		$(VOICE_PACK_ROOT)/bk7258_health_core.c \
		-I $(VOICE_PACK_ROOT) -o $@

health-core: $(HEALTH_CORE_TARGET)

run-health-core: health-core
	./$(HEALTH_CORE_TARGET)

$(MOTION_CORE_TARGET): test_bk7258_motion_core.c \
	$(VOICE_PACK_ROOT)/bk7258_motion_core.c \
	$(VOICE_PACK_ROOT)/bk7258_motion_core.h \
	$(VOICE_PACK_ROOT)/bk7258_motion_protocol.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) test_bk7258_motion_core.c \
		$(VOICE_PACK_ROOT)/bk7258_motion_core.c \
		-I $(VOICE_PACK_ROOT) -o $@

motion-core: $(MOTION_CORE_TARGET)

run-motion-core: motion-core
	./$(MOTION_CORE_TARGET)

$(NFC_CORE_TARGET): test_bk7258_nfc_core.c \
	$(VOICE_PACK_ROOT)/bk7258_nfc_core.c \
	$(VOICE_PACK_ROOT)/bk7258_nfc_core.h \
	$(VOICE_PACK_ROOT)/bk7258_nfc_protocol.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) test_bk7258_nfc_core.c \
		$(VOICE_PACK_ROOT)/bk7258_nfc_core.c \
		-I $(VOICE_PACK_ROOT) -o $@

nfc-core: $(NFC_CORE_TARGET)

run-nfc-core: nfc-core
	./$(NFC_CORE_TARGET)

# These tests include the real client, service and core. The explicit empty
# headers isolate their deterministic RPC/VFS mocks from a target .config.
$(SENSOR_RPC_SHIM)/.ready: | $(BUILD)
	mkdir -p $(sort $(dir $(addprefix $(SENSOR_RPC_SHIM)/,$(SENSOR_RPC_HEADERS))))
	@for header in $(SENSOR_RPC_HEADERS); do \
		touch "$(SENSOR_RPC_SHIM)/$$header"; \
	done
	touch $@

$(SENSOR_RPC_TARGETS): $(BUILD)/test_bk7258_%_rpc: test_bk7258_%_rpc.c \
	bk7258_sensor_rpc_test.h \
	$(wildcard $(VOICE_PACK_ROOT)/bk7258_motion_*.c) \
	$(wildcard $(VOICE_PACK_ROOT)/bk7258_motion_*.h) \
	$(wildcard $(VOICE_PACK_ROOT)/bk7258_nfc_*.c) \
	$(wildcard $(VOICE_PACK_ROOT)/bk7258_nfc_*.h) \
	$(SENSOR_RPC_SHIM)/.ready
	$(CC) -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
		-fsanitize=undefined -fno-sanitize-recover=all \
		-I $(SENSOR_RPC_SHIM) -I $(VOICE_PACK_ROOT) $< -o $@

run-motion-rpc: $(BUILD)/test_bk7258_motion_rpc
	./$(BUILD)/test_bk7258_motion_rpc

run-nfc-rpc: $(BUILD)/test_bk7258_nfc_rpc
	./$(BUILD)/test_bk7258_nfc_rpc

run-wifi-native-lease:
	python3 test_wifi_native_lease_drift.py

$(VISION_CORE_TARGET): test_bk7258_vision_core.c \
	$(VOICE_PACK_ROOT)/bk7258_vision_core.c \
	$(VOICE_PACK_ROOT)/bk7258_vision_core.h \
	$(VOICE_PACK_ROOT)/bk7258_vision_protocol.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) test_bk7258_vision_core.c \
		$(VOICE_PACK_ROOT)/bk7258_vision_core.c \
		-I $(VOICE_PACK_ROOT) -o $@

vision-core: $(VISION_CORE_TARGET)

run-vision-core: vision-core
	./$(VISION_CORE_TARGET)

$(VISION_FEEDBACK_TARGET): test_bk7258_vision_feedback.c \
	$(VOICE_PACK_ROOT)/bk7258_vision_feedback.c \
	$(VOICE_PACK_ROOT)/bk7258_vision_feedback.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) test_bk7258_vision_feedback.c \
		$(VOICE_PACK_ROOT)/bk7258_vision_feedback.c \
		-I $(VOICE_PACK_ROOT) -o $@

vision-feedback: $(VISION_FEEDBACK_TARGET)

run-vision-feedback: vision-feedback
	./$(VISION_FEEDBACK_TARGET)

$(USBMODE_LEASE_TARGET): test_bk7258_usbmode_lease.c \
	$(CHIP_ROOT)/ap/bk7258_usbmode.c \
	$(CHIP_ROOT)/include/bk7258_usbmode.h \
	$(MOCKS)/arch/chip/bk7258_usbcdc.h \
	$(MOCKS)/arch/chip/bk7258_usbmsc.h \
	$(MOCKS)/mock_nuttx_ap.c | $(BUILD)
	$(CC) $(USBMODE_LEASE_CFLAGS) test_bk7258_usbmode_lease.c \
		$(CHIP_ROOT)/ap/bk7258_usbmode.c $(MOCKS)/mock_nuttx_ap.c \
		-I $(MOCKS) -I $(CHIP_ROOT)/include -o $@

usbmode-lease: $(USBMODE_LEASE_TARGET)

run-usbmode-lease: usbmode-lease
	./$(USBMODE_LEASE_TARGET)

$(PRODUCT_KEYS_TARGET): test_bk7258_product_keys.c \
	$(VOICE_PACK_ROOT)/bk7258_product_keys.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) test_bk7258_product_keys.c \
		-I $(VOICE_PACK_ROOT) -o $@

product-keys: $(PRODUCT_KEYS_TARGET)

run-product-keys: product-keys
	./$(PRODUCT_KEYS_TARGET)

# These explicit targets use the manifest workspace's mbedTLS headers/source.
# The interop target additionally needs OpenSSL, cmake and Python websockets;
# it creates only temporary loopback listeners and ephemeral test credentials.

.PHONY: run-voice-tls-concurrency

# Provisioning tests compile real app sources into temporary directories.
# Keep them outside all/check/run until their CMake/OpenSSL requirements are
# intentionally added to those broader host-test contracts.
provision: test_provision_gatt.py \
	test_provision_tls.py

run-provision: provision
	python3 test_provision_gatt.py
	python3 test_provision_tls.py

$(BUILD)/test_bk7258_voice_tls_concurrency: test_bk7258_voice_tls_concurrency.c \
	$(VOICE_PACK_ROOT)/bk7258_voice_tls.c \
	$(VOICE_PACK_ROOT)/bk7258_voice_tls.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -D_GNU_SOURCE -DFAR= -pthread \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-I$(VOICE_PACK_ROOT) \
		-I$(OPENVELA_ROOT)/apps/crypto/mbedtls/mbedtls/include \
		test_bk7258_voice_tls_concurrency.c -o $@

run-voice-tls-concurrency: $(BUILD)/test_bk7258_voice_tls_concurrency
	./$(BUILD)/test_bk7258_voice_tls_concurrency

$(VOICE_MEDIA_RECORDER_TARGET): test_bk7258_agent_media_recorder.c \
	$(VOICE_PACK_ROOT)/bk7258_agent_media_recorder.c \
	$(MOCKS)/media_recorder.h $(MOCKS)/nuttx/audio/audio.h \
	$(MOCKS)/nuttx/mutex.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread \
		test_bk7258_agent_media_recorder.c -I $(MOCKS) -o $@

voice-media-recorder: $(VOICE_MEDIA_RECORDER_TARGET)

run-voice-media-recorder: voice-media-recorder
	./$(VOICE_MEDIA_RECORDER_TARGET)

$(BUILD)/test_bk7258_agent_capture: test_bk7258_agent_media_recorder.c \
	$(OPENVELA_ROOT)/packages/ai_agent/src/voice/audio_capture.c \
	$(OPENVELA_ROOT)/packages/ai_agent/include/voice/audio_capture.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread -DTEST_AGENT_CAPTURE $< \
		-I $(MOCKS) -I $(OPENVELA_ROOT)/packages/ai_agent/include \
		-I $(OPENVELA_ROOT)/packages/ai_agent/src -o $@

run-agent-capture: $(BUILD)/test_bk7258_agent_capture
	./$(BUILD)/test_bk7258_agent_capture

$(BUILD)/test_bk7258_agent_media_player: test_bk7258_agent_media_player.c \
	$(VOICE_PACK_ROOT)/bk7258_agent_media_player.c \
	$(MOCKS)/nuttx/audio/audio.h $(MOCKS)/nuttx/mutex.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread $< -I $(VOICE_PACK_ROOT) \
		-I $(MOCKS) -I $(MEDIA_INCLUDE) -o $@

.PHONY: run-voice-media-player
run-voice-media-player: $(BUILD)/test_bk7258_agent_media_player
	./$(BUILD)/test_bk7258_agent_media_player

$(BUILD)/test_agent_tts_queue: test_bk7258_agent_media_player.c \
	$(OPENVELA_ROOT)/packages/ai_agent/src/voice/voice_channel.c \
	$(OPENVELA_ROOT)/packages/ai_agent/src/llm/llm_stream.c \
	$(OPENVELA_ROOT)/packages/ai_agent/src/core/message_bus.c | $(BUILD)
	$(CC) $(CFLAGS) -DTEST_AGENT_TTS_QUEUE -DOK=0 -DERROR=-1 \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-I $(MOCKS) -I $(OPENVELA_ROOT)/packages/ai_agent/include \
		-I $(OPENVELA_ROOT)/packages/ai_agent/src \
		-I $(OPENVELA_ROOT)/apps/netutils/cjson/cJSON $< \
		$(OPENVELA_ROOT)/packages/ai_agent/src/llm/llm_stream.c \
		$(OPENVELA_ROOT)/packages/ai_agent/src/core/message_bus.c \
		$(OPENVELA_ROOT)/apps/netutils/cjson/cJSON/cJSON.c -o $@

.PHONY: run-agent-tts-queue
run-agent-tts-queue: $(BUILD)/test_agent_tts_queue
	./$(BUILD)/test_agent_tts_queue

$(BUILD)/test_agent_funasr: test_bk7258_agent_media_player.c \
	$(OPENVELA_ROOT)/packages/ai_agent/src/voice/funasr_asr.c | $(BUILD)
	$(CC) $(CFLAGS) -DTEST_AGENT_FUNASR \
		-I $(MOCKS) \
		-I $(OPENVELA_ROOT)/packages/ai_agent/include \
		-I $(OPENVELA_ROOT)/packages/ai_agent/src \
		-I $(OPENVELA_ROOT)/apps/netutils/cjson/cJSON \
		-I $(OPENVELA_ROOT)/apps/crypto/mbedtls/mbedtls/include \
		$< $(OPENVELA_ROOT)/apps/netutils/cjson/cJSON/cJSON.c \
		-lssl -lcrypto -o $@

.PHONY: run-agent-funasr
run-agent-funasr: $(BUILD)/test_agent_funasr
	./$(BUILD)/test_agent_funasr

# Optional real loopback probe after building: build/test_agent_funasr
# --live localhost PORT /asr CA_PEM PCM_S16LE_16K_MONO

$(VISION_RECORD_TARGET): test_bk7258_vision_record.c \
	$(VOICE_PACK_ROOT)/bk7258_vision_record.c \
	$(VOICE_PACK_ROOT)/bk7258_vision_record.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) test_bk7258_vision_record.c \
		$(VOICE_PACK_ROOT)/bk7258_vision_record.c \
		-Wl,--wrap=write \
		-I $(VOICE_PACK_ROOT) -o $@

vision-record: $(VISION_RECORD_TARGET)

run-vision-record: vision-record
	./$(VISION_RECORD_TARGET)

$(MEDIA_VOLUME_TARGET): test_bk7258_media_volume.c \
	$(VOICE_PACK_ROOT)/bk7258_media_volume.c \
	$(VOICE_PACK_ROOT)/bk7258_media_volume.h \
	$(CHIP_ROOT)/ap/bk7258_ota_source_file.c | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -DCONFIG_BK7258_USBMODE -DCONFIG_BK7258_AP_CORE \
		-DCONFIG_BK7258_OTA_SOURCE_FILE -DCONFIG_BK7258_OTA_MANAGER \
		-DCONFIG_BK7258_USBMODE_BLOCKDEV=\"/dev/mmcsd0\" \
		-I $(MOCKS) -I $(CHIP_ROOT)/include -I $(VOICE_PACK_ROOT) \
		-include $(CHIP_ROOT)/include/bk7258_usbmode.h \
		-include $(MOCKS)/bk7258_usbmode_lease.h \
		test_bk7258_media_volume.c $(VOICE_PACK_ROOT)/bk7258_media_volume.c \
		$(CHIP_ROOT)/ap/bk7258_ota_source_file.c -o $@

media-volume: $(MEDIA_VOLUME_TARGET)

run-media-volume: media-volume
	./$(MEDIA_VOLUME_TARGET)

.PHONY: board-layers

board-layers: check-layers
	@echo "BK7258_BOARD_LAYER_AUDIT_PASS boards=all implementation=verify-layers"

headers: $(PARTITION_HEADER)
	@set -eu; \
	for mode_flags in '' '$(HEADER_CHECK_FEATURE_FLAGS)' \
	  '$(HEADER_CHECK_FEATURE_FLAGS) -DCONFIG_BK7258_AP_CORE'; do \
	  for header_name in $(HEADER_CHECK_HEADERS); do \
	    printf '#include <%s>\n#include <%s>\nint main(void) { return 0; }\n' \
	      "$$header_name" "$$header_name" | \
	      $(CC) -std=c11 $(HEADER_CHECK_FLAGS) $$mode_flags -x c -; \
	    printf '#include <%s>\n#include <%s>\nint main() { return 0; }\n' \
	      "$$header_name" "$$header_name" | \
	      $(CXX) -std=c++17 $(HEADER_CHECK_FLAGS) $$mode_flags -x c++ -; \
	  done; \
	done; \
	seen_guards=' '; \
	for header_path in $(HEADER_CHECK_PATHS); do \
	  header_guard=$$(awk '$$1 == "#ifndef" { print $$2; exit }' \
	    "$$header_path"); \
	  if test -z "$$header_guard"; then \
	    echo "missing include guard: $$header_path" >&2; \
	    exit 1; \
	  fi; \
	  case "$$seen_guards" in \
	    *" $$header_guard "*) \
	      echo "duplicate include guard $$header_guard: $$header_path" >&2; \
	      exit 1 \
	      ;; \
	  esac; \
	  seen_guards="$$seen_guards$$header_guard "; \
	done; \
	command -v rg >/dev/null 2>&1 || \
	  { echo "rg is required for the BK7258 header path audit" >&2; exit 1; }; \
	if rg -n '#[[:space:]]*include[[:space:]]*[<"]\.\./' \
	  $(MOCKS)/arch/chip; then \
	  echo "deep relative chip-header forwarding is forbidden" >&2; \
	  exit 1; \
	fi; \
	echo "BK7258_HEADER_AUDIT_PASS count=$(words $(HEADER_CHECK_HEADERS)) modes=default,CP,AP languages=C11,C++17"

run-headers: headers

############################################################################
# BL1 framework: mock_reg32 MMIO windows, XIP flash window, tinycrypt mock.
############################################################################

FRAMEWORK := framework
BL1_MODULES := modules/bl1
BOOTLOADER := $(CHIP_ROOT)/bootloader
CHIP_INC   := $(CHIP_ROOT)/include
PATCH      := python3 $(FRAMEWORK)/patch.py
CMOCKA     := $(shell pkg-config --cflags --libs cmocka)
BL1_CONFIG := $(FRAMEWORK)/bk7258_bl1_config.h
BL1_CONFIG_FLAG := -include $(abspath $(BL1_CONFIG))

BL1_INC := -I $(FRAMEWORK) -I mocks -I mocks/tinycrypt \
	-I $(BL1_MODULES) -I $(BOOTLOADER) -I $(CHIP_INC) \
	-I $(PARTITION_DIR) -I $(BUILD)/bl1
BL1_CFLAGS := -std=gnu11 -Wall -Wextra -O2 -g $(BL1_CONFIG_FLAG)

# Framework objects shared by every BL1 test binary.
FRAMEWORK_OBJS := $(BUILD)/mock_reg32.o $(BUILD)/mock_flash.o \
	$(BUILD)/mock_tinycrypt.o

$(BUILD)/mock_reg32.o: $(FRAMEWORK)/mock_reg32.c $(FRAMEWORK)/mock_reg32.h
	$(CC) $(BL1_CFLAGS) -I $(FRAMEWORK) -c $< -o $@

$(BUILD)/mock_flash.o: $(FRAMEWORK)/mock_flash.c $(FRAMEWORK)/mock_flash.h
	$(CC) $(BL1_CFLAGS) -I $(FRAMEWORK) -c $< -o $@

$(BUILD)/mock_tinycrypt.o: $(FRAMEWORK)/mock_tinycrypt.c \
	$(FRAMEWORK)/mock_tinycrypt.h mocks/tinycrypt/ecc.h
	$(CC) $(BL1_CFLAGS) -I $(FRAMEWORK) -I mocks -c $< -o $@

# Throwaway patched copies of the real bootloader sources.  The real files
# are never modified.  -include mock_reg32.h injects the MMIO redirect into
# the patched translation unit.
$(BUILD)/bl1/%.c: $(BOOTLOADER)/%.c $(FRAMEWORK)/patch.py | $(BUILD)/bl1
	$(PATCH) $* $< $@

$(BUILD)/bl1/boot_bl1_manifest.c: $(BOOTLOADER)/boot_bl1_manifest.c \
	$(FRAMEWORK)/patch.py | $(BUILD)/bl1
	$(PATCH) bl1_manifest $< $@

$(BUILD)/bl1/boot_wdt.h: $(BOOTLOADER)/boot_wdt.h $(FRAMEWORK)/patch.py \
	| $(BUILD)/bl1
	$(PATCH) boot_wdt $< $@

$(PARTITION_DIR):
	mkdir -p $@

$(PARTITION_HEADER): $(PARTITION_CSV) $(TOOLS_ROOT)/_lib/layout.py \
	$(FRAMEWORK)/generate_layout.py | $(PARTITION_DIR)
	python3 $(FRAMEWORK)/generate_layout.py $(TOOLS_ROOT) \
	  $(PARTITION_CSV) $(PARTITION_DIR)

$(BUILD)/bl1: $(PARTITION_HEADER)
	mkdir -p $(BUILD)/bl1

BL1_PATCHED := $(BUILD)/bl1/boot_flash.c $(BUILD)/bl1/boot_clock.c \
	$(BUILD)/bl1/boot_runtime.c $(BUILD)/bl1/boot_bl1_manifest.c \
	$(BUILD)/bl1/boot_wdt.h

# boot_flash.c includes "../boot_wdt.h" - point it at the patched copy.
$(BUILD)/bl1/boot_flash.c: $(BOOTLOADER)/boot_flash.c $(FRAMEWORK)/patch.py \
	| $(BUILD)/bl1
	$(PATCH) boot_flash $< $@

# Helper: compile a patched .c with the framework headers injected.
define bl1_compile
	$(CC) $(BL1_CFLAGS) -include mock_reg32.h -include mock_flash.h \
	  $(BL1_INC) -c $< -o $@
endef

$(BUILD)/bl1/%.o: $(BUILD)/bl1/%.c
	$(bl1_compile)

# boot_runtime.c / boot_flash.c pull boot_wdt.h in by quote-include; make
# sure the patched header copy exists before compiling them.
$(BUILD)/bl1/boot_runtime.o: $(BUILD)/bl1/boot_wdt.h
$(BUILD)/bl1/boot_flash.o: $(BUILD)/bl1/boot_wdt.h

# Pure modules (no patch needed).
$(BUILD)/bl1/boot_libc.o: $(BOOTLOADER)/boot_libc.c | $(BUILD)/bl1
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

$(BUILD)/bl1/boot_sha256.o: $(BOOTLOADER)/boot_sha256.c \
	$(BOOTLOADER)/boot_sha256.h | $(BUILD)/bl1
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

$(BUILD)/bl1/boot_bl1_policy.o: $(BOOTLOADER)/boot_bl1_policy.c \
	$(BOOTLOADER)/boot_bl1_policy.h $(BOOTLOADER)/boot_bl1_boot_flag_core.h \
	| $(BUILD)/bl1
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

$(BUILD)/bl1/boot_bl1_boot_flag_core.o: \
	$(BOOTLOADER)/boot_bl1_boot_flag_core.c | $(BUILD)/bl1
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

$(BUILD)/bl1/mock_bl1_public_key.o: \
	$(FRAMEWORK)/mock_bl1_public_key.c | $(BUILD)/bl1
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

$(BUILD)/bl1/test_%.o: $(BL1_MODULES)/test_%.c | $(BUILD)/bl1
	$(CC) $(BL1_CFLAGS) $(BL1_INC) -c $< -o $@

# Test binaries.
$(BUILD)/test_boot_libc: $(BUILD)/bl1/boot_libc.o \
	$(BUILD)/bl1/test_boot_libc.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_boot_sha256: $(BUILD)/bl1/boot_sha256.o \
	$(BUILD)/bl1/test_boot_sha256.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_boot_flash: $(BUILD)/bl1/boot_flash.o \
	$(BUILD)/bl1/test_boot_flash.o $(BUILD)/mock_reg32.o \
	$(BUILD)/mock_flash.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_boot_clock: $(BUILD)/bl1/boot_clock.o \
	$(BUILD)/bl1/test_boot_clock.o $(BUILD)/mock_reg32.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_boot_runtime: $(BUILD)/bl1/boot_runtime.o \
	$(BUILD)/bl1/test_boot_runtime.o $(BUILD)/mock_reg32.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_bl1_manifest: $(BUILD)/bl1/boot_bl1_manifest.o \
	$(BUILD)/bl1/boot_bl1_policy.o \
	$(BUILD)/bl1/boot_bl1_boot_flag_core.o \
	$(BUILD)/bl1/mock_bl1_public_key.o $(BUILD)/bl1/boot_sha256.o \
	$(BUILD)/bl1/test_bl1_manifest.o $(BUILD)/mock_reg32.o \
	$(BUILD)/mock_flash.o $(BUILD)/mock_tinycrypt.o
	$(CC) $^ -o $@ $(CMOCKA)

BL1_TARGETS := $(BUILD)/test_boot_libc $(BUILD)/test_boot_sha256 \
	$(BUILD)/test_boot_flash $(BUILD)/test_boot_clock \
	$(BUILD)/test_boot_runtime $(BUILD)/test_bl1_manifest

bl1: $(BL1_TARGETS)

.PHONY: run-bl1
run-bl1: bl1
	./$(BUILD)/test_boot_libc
	./$(BUILD)/test_boot_sha256
	./$(BUILD)/test_boot_flash
	./$(BUILD)/test_boot_clock
	./$(BUILD)/test_boot_runtime
	./$(BUILD)/test_bl1_manifest

############################################################################
# BL2/MCUboot chip modules: security counter, flash-map and pair policy.
############################################################################

BL2_MODULES := modules/bl2
BL2_DIR     := $(BUILD)/bl2
BL2_SOURCE  := $(BOOTLOADER)/bl2
BL2_INC := -I $(BL2_DIR) -I $(BUILD) -I $(FRAMEWORK) -I mocks \
	-I $(BL2_MODULES) -I $(BOOTLOADER) -I $(BL2_SOURCE) \
	-I $(CHIP_INC) -I $(PARTITION_DIR) -iquote $(BL2_SOURCE)/include
BL2_CFLAGS := -std=gnu11 -Wall -Wextra -O2 -g \
	-DBK7258_BL2_SECURITY_COUNTER_FLOOR=7 \
	-DBK7258_BL2_COPY_SIZE=0x00010000u

# The board flash_map backend pulls ../boot_wdt.h in by quote-include; the
# patched header lives in $(BUILD) and is shared with BL1 modules.
$(BUILD)/boot_wdt.h: $(BOOTLOADER)/boot_wdt.h $(FRAMEWORK)/patch.py \
	| $(BUILD)
	$(PATCH) boot_wdt $< $@

$(BL2_DIR): $(PARTITION_HEADER)
	mkdir -p $(BL2_DIR)

$(BL2_DIR)/bk7258_bl2_security_cnt.c: \
	$(BL2_SOURCE)/bk7258_bl2_security_cnt.c $(FRAMEWORK)/patch.py \
	| $(BL2_DIR)
	$(PATCH) bl2_security_cnt $< $@

$(BL2_DIR)/bk7258_bl2_flash_map.c: \
	$(BL2_SOURCE)/bk7258_bl2_flash_map.c $(FRAMEWORK)/patch.py \
	| $(BL2_DIR)
	$(PATCH) bl2_flash_map $< $@

# Patched BL2 modules are compiled with the framework MMIO injected.
$(BL2_DIR)/%.o: $(BL2_DIR)/%.c | $(BUILD)/boot_wdt.h $(BL2_DIR)
	$(CC) $(BL2_CFLAGS) -include mock_reg32.h -include mock_flash.h \
	  $(BL2_INC) -c $< -o $@

# Pure pair-ordering policy: no MMIO and no source patch.
$(BL2_DIR)/bk7258_bl2_pair_policy.o: \
	$(BL2_SOURCE)/bk7258_bl2_pair_policy.c \
	$(BL2_SOURCE)/bk7258_bl2_pair_policy.h mocks/bootutil/image.h \
	| $(BL2_DIR)
	$(CC) $(BL2_CFLAGS) $(BL2_INC) -c $< -o $@

$(BL2_DIR)/test_%.o: $(BL2_MODULES)/test_%.c | $(BL2_DIR)
	$(CC) $(BL2_CFLAGS) $(BL2_INC) -c $< -o $@

$(BUILD)/mock_boot_flash.o: $(FRAMEWORK)/mock_boot_flash.c \
	$(FRAMEWORK)/mock_boot_flash.h $(PARTITION_HEADER) | $(BUILD)
	$(CC) $(BL2_CFLAGS) $(BL2_INC) -c $< -o $@

$(BUILD)/test_bl2_security_cnt: $(BL2_DIR)/bk7258_bl2_security_cnt.o \
	$(BL2_DIR)/test_bl2_security_cnt.o $(BUILD)/mock_reg32.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_bl2_flash_map: $(BL2_DIR)/bk7258_bl2_flash_map.o \
	$(BL2_DIR)/test_bl2_flash_map.o $(BUILD)/mock_reg32.o \
	$(BUILD)/mock_flash.o $(BUILD)/mock_boot_flash.o
	$(CC) $^ -o $@ $(CMOCKA)

$(BUILD)/test_bl2_pair_policy: $(BL2_DIR)/bk7258_bl2_pair_policy.o \
	$(BL2_DIR)/test_bl2_pair_policy.o
	$(CC) $^ -o $@ $(CMOCKA)

BL2_TARGETS := $(BUILD)/test_bl2_security_cnt $(BUILD)/test_bl2_flash_map \
	$(BUILD)/test_bl2_pair_policy

bl2: $(BL2_TARGETS)

.PHONY: run-bl2
run-bl2: bl2
	./$(BUILD)/test_bl2_security_cnt
	./$(BUILD)/test_bl2_flash_map
	./$(BUILD)/test_bl2_pair_policy

$(BUILD):
	mkdir -p $(BUILD)

# Throwaway copy of the REAL implementation with the ARM "dmb sy" barrier
# neutralized (host x86 has no such instruction).  The real file is NOT
# modified.
$(BUILD)/bk7258_rptun_mbox.c: $(REAL_C) | $(BUILD)
	cp $(REAL_C) $@
	sed -i 's|__asm volatile ("dmb sy".*|__asm__ volatile ("" ::: "memory");|g' $@

$(BUILD)/bk7258_rptun_mbox.h: $(REAL_H) | $(BUILD)
	cp $(REAL_H) $@

$(BUILD)/bk7258_rptun.c: $(CHIP_ROOT)/common/bk7258_rptun.c | $(BUILD)
	cp $< $@
	sed -i 's|__asm volatile ("dmb sy.*|__asm__ volatile ("" ::: "memory");|g' $@

$(TARGET): $(BUILD)/bk7258_rptun_mbox.c $(BUILD)/bk7258_rptun_mbox.h \
           test_bk7258_rptun_mbox.c mocks/mock_sdk.c
	$(CC) $(CFLAGS) \
	  test_bk7258_rptun_mbox.c \
	  mocks/mock_sdk.c \
	  $(BUILD)/bk7258_rptun_mbox.c \
	  -I $(MOCKS) -I $(BUILD) \
	  -o $@ -lpthread

$(POLICY_TARGET): test_boot_bl1_policy.c \
                 $(BL1_CONFIG) \
                 $(BOOTLOADER)/boot_bl1_boot_flag_core.c \
                 $(BOOTLOADER)/boot_bl1_boot_flag_core.h \
                 $(BOOTLOADER)/boot_bl1_handoff_core.c \
                 $(BOOTLOADER)/boot_bl1_handoff_core.h \
                 $(BOOTLOADER)/boot_bl1_policy.c \
                 $(BOOTLOADER)/boot_bl1_policy.h \
                 $(BOOTLOADER)/boot_bl1_manifest.h
	$(CC) $(POLICY_CFLAGS) $(BL1_CONFIG_FLAG) \
	  test_boot_bl1_policy.c \
	  $(BOOTLOADER)/boot_bl1_boot_flag_core.c \
	  $(BOOTLOADER)/boot_bl1_handoff_core.c \
	  $(BOOTLOADER)/boot_bl1_policy.c \
	  -I $(BOOTLOADER) \
	  -I $(CHIP_INC) \
	  -o $@

$(PM_ACTIVITY_TARGET): test_bk7258_pm_activity.c \
                      $(CHIP_ROOT)/common/bk7258_pm_activity.c \
                      $(CHIP_ROOT)/common/bk7258_pm_activity.h
	$(CC) $(PM_ACTIVITY_CFLAGS) \
	  test_bk7258_pm_activity.c \
	  $(CHIP_ROOT)/common/bk7258_pm_activity.c \
	  -I $(CHIP_ROOT)/common \
	  -o $@

$(RPTUN_CORE_CP_TARGET): $(BUILD)/bk7258_rptun.c \
                         test_bk7258_rptun_core.c
	$(CC) $(RPTUN_CORE_CFLAGS) \
	  test_bk7258_rptun_core.c \
	  $(BUILD)/bk7258_rptun.c \
	  -I $(MOCKS) -I $(CHIP_ROOT)/common \
	  -o $@

$(RPTUN_CORE_AP_TARGET): $(BUILD)/bk7258_rptun.c \
                         test_bk7258_rptun_core.c
	$(CC) $(RPTUN_CORE_CFLAGS) -DTEST_BK7258_RPTUN_AP \
	  test_bk7258_rptun_core.c \
	  $(BUILD)/bk7258_rptun.c \
	  -I $(MOCKS) -I $(CHIP_ROOT)/common \
	  -o $@

$(STAGE_RUNNER_TARGET): test_bk7258_stage_runner.c \
                        $(CHIP_ROOT)/common/bk7258_stage_runner.c \
                        $(CHIP_ROOT)/include/bk7258_stage_runner.h | $(BUILD)
	$(CC) $(STAGE_RUNNER_CFLAGS) \
	  test_bk7258_stage_runner.c \
	  $(CHIP_ROOT)/common/bk7258_stage_runner.c \
	  -I $(MOCKS) -I $(CHIP_ROOT)/include -I $(CHIP_ROOT)/common \
	  -o $@

$(SDK_RUNTIME_TARGET): test_bk7258_sdk_runtime.c \
			$(CHIP_ROOT)/common/bk7258_sdk_runtime.c \
			$(MOCKS)/driver/ckmn.h \
			$(MOCKS)/mock_nuttx_ap.c | $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -pthread \
	  test_bk7258_sdk_runtime.c \
	  $(CHIP_ROOT)/common/bk7258_sdk_runtime.c \
	  $(MOCKS)/mock_nuttx_ap.c \
	  -I $(MOCKS) -I $(CHIP_ROOT)/include -o $@

$(CP_PLATFORM_SUCCESS_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	$(CC) $(CP_PLATFORM_CFLAGS) -DTEST_CP_SCENARIO=1 \
	  $(CP_PLATFORM_TEST_SRCS) \
	  $(PLATFORM_TEST_INC) -o $@

$(CP_PLATFORM_HEAP_ORDER_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	mkdir -p $(BUILD)/platform-include/arch/chip
	ln -sf $(abspath $(CHIP_ROOT)/include/bk7258_wifi.h) \
	  $(BUILD)/platform-include/arch/chip/bk7258_wifi.h
	$(CC) $(CP_PLATFORM_CFLAGS) -DTEST_CP_SCENARIO=12 \
	  -DCONFIG_BK7258_WIFI_VNET -DCONFIG_BK7258_BT_IPC \
	  $(CP_PLATFORM_TEST_SRCS) $(PLATFORM_TEST_INC) \
	  -I $(BUILD)/platform-include -o $@

$(CP_PLATFORM_SDK_FAIL_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	$(CC) $(CP_PLATFORM_CFLAGS) -DTEST_CP_SCENARIO=2 \
	  $(CP_PLATFORM_TEST_SRCS) \
	  $(PLATFORM_TEST_INC) -o $@

$(CP_PLATFORM_WDT_FAIL_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	$(CC) $(CP_PLATFORM_CFLAGS) -DTEST_CP_SCENARIO=3 \
	  $(CP_PLATFORM_TEST_SRCS) \
	  $(PLATFORM_TEST_INC) -o $@

$(CP_PLATFORM_RESET_MARKER_FAIL_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	$(CC) $(CP_PLATFORM_CFLAGS) -DTEST_CP_SCENARIO=4 \
	  $(CP_PLATFORM_TEST_SRCS) \
	  $(PLATFORM_TEST_INC) -o $@

$(CP_PLATFORM_OTA_LAYOUT_FAIL_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	$(CC) $(CP_PLATFORM_CFLAGS) -DTEST_CP_SCENARIO=5 \
	  $(CP_PLATFORM_TEST_SRCS) \
	  $(PLATFORM_TEST_INC) -o $@

$(CP_PLATFORM_AP_START_FAIL_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	$(CC) $(CP_PLATFORM_CFLAGS) -DTEST_CP_SCENARIO=6 \
	  $(CP_PLATFORM_TEST_SRCS) \
	  $(PLATFORM_TEST_INC) -o $@

$(CP_PLATFORM_PSRAM_FAIL_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	$(CC) $(CP_PLATFORM_CFLAGS) -DTEST_CP_SCENARIO=7 \
	  $(CP_PLATFORM_TEST_SRCS) \
	  $(PLATFORM_TEST_INC) -o $@

$(CP_PLATFORM_OTA_POLICY_FAIL_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	$(CC) $(CP_PLATFORM_CFLAGS) -DTEST_CP_SCENARIO=8 \
	  $(CP_PLATFORM_TEST_SRCS) \
	  $(PLATFORM_TEST_INC) -o $@

$(CP_PLATFORM_LEGACY_LEAVES_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	$(CC) $(CP_PLATFORM_LEGACY_CFLAGS) -DTEST_CP_SCENARIO=9 \
	  $(CP_PLATFORM_TEST_SRCS) \
	  $(PLATFORM_TEST_INC) -o $@

$(CP_PLATFORM_OPTIONAL_POLICY_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	$(CC) $(CP_PLATFORM_OPTIONAL_POLICY_CFLAGS) -DTEST_CP_SCENARIO=10 \
	  $(CP_PLATFORM_TEST_SRCS) \
	  $(PLATFORM_TEST_INC) -o $@

$(CP_PLATFORM_RADIO_FAIL_TARGET): $(CP_PLATFORM_TEST_SRCS) | $(BUILD)
	$(CC) $(CP_PLATFORM_RADIO_CFLAGS) -DTEST_CP_SCENARIO=11 \
	  $(CP_PLATFORM_TEST_SRCS) \
	  $(PLATFORM_TEST_INC) -o $@

$(OTA_CONFIRM_NO_SUPERVISOR_TARGET): test_bk7258_ota_confirmation.c \
	$(CHIP_ROOT)/cp/bk7258_ota_boot_control.c | $(BUILD)
	$(CC) $(OTA_CONFIRM_CFLAGS) \
	  test_bk7258_ota_confirmation.c \
	  $(CHIP_ROOT)/cp/bk7258_ota_boot_control.c \
	  $(OTA_CONTRACT_INC) -o $@

$(OTA_CONFIRM_SUPERVISOR_TARGET): test_bk7258_ota_confirmation.c \
	$(CHIP_ROOT)/cp/bk7258_ota_boot_control.c | $(BUILD)
	$(CC) $(OTA_CONFIRM_CFLAGS) -fno-pie -no-pie \
	  -DCONFIG_BK7258_AP_SUPERVISOR \
	  test_bk7258_ota_confirmation.c \
	  $(CHIP_ROOT)/cp/bk7258_ota_boot_control.c \
	  $(OTA_CONTRACT_INC) -o $@

$(AP_PLATFORM_SUCCESS_TARGET): test_bk7258_ap_platform.c \
					$(CHIP_ROOT)/ap/bk7258_ap_platform.c \
					$(BOARD_COMMON_SRC)/bk7258_boot.c \
					$(CHIP_ROOT)/common/bk7258_stage_runner.c mocks/mock_nuttx_ap.c | $(BUILD)
	$(CC) $(AP_PLATFORM_CFLAGS) -DTEST_AP_SCENARIO=1 \
	  test_bk7258_ap_platform.c $(CHIP_ROOT)/ap/bk7258_ap_platform.c \
	  $(BOARD_COMMON_SRC)/bk7258_boot.c \
	  $(CHIP_ROOT)/common/bk7258_stage_runner.c mocks/mock_nuttx_ap.c \
	  $(PLATFORM_TEST_INC) -o $@

$(AP_PLATFORM_SDK_FAIL_TARGET): test_bk7258_ap_platform.c \
					$(CHIP_ROOT)/ap/bk7258_ap_platform.c \
					$(BOARD_COMMON_SRC)/bk7258_boot.c \
					$(CHIP_ROOT)/common/bk7258_stage_runner.c mocks/mock_nuttx_ap.c | $(BUILD)
	$(CC) $(AP_PLATFORM_CFLAGS) -DTEST_AP_SCENARIO=2 \
	  test_bk7258_ap_platform.c $(CHIP_ROOT)/ap/bk7258_ap_platform.c \
	  $(BOARD_COMMON_SRC)/bk7258_boot.c \
	  $(CHIP_ROOT)/common/bk7258_stage_runner.c mocks/mock_nuttx_ap.c \
	  $(PLATFORM_TEST_INC) -o $@

$(AP_PLATFORM_PSRAM_FAIL_TARGET): test_bk7258_ap_platform.c \
					$(CHIP_ROOT)/ap/bk7258_ap_platform.c \
					$(BOARD_COMMON_SRC)/bk7258_boot.c \
					$(CHIP_ROOT)/common/bk7258_stage_runner.c mocks/mock_nuttx_ap.c | $(BUILD)
	$(CC) $(AP_PLATFORM_CFLAGS) -DTEST_AP_SCENARIO=3 \
	  test_bk7258_ap_platform.c $(CHIP_ROOT)/ap/bk7258_ap_platform.c \
	  $(BOARD_COMMON_SRC)/bk7258_boot.c \
	  $(CHIP_ROOT)/common/bk7258_stage_runner.c mocks/mock_nuttx_ap.c \
	  $(PLATFORM_TEST_INC) -o $@

$(AP_PLATFORM_PM_FAIL_TARGET): test_bk7258_ap_platform.c \
					$(CHIP_ROOT)/ap/bk7258_ap_platform.c \
					$(BOARD_COMMON_SRC)/bk7258_boot.c \
					$(CHIP_ROOT)/common/bk7258_stage_runner.c mocks/mock_nuttx_ap.c | $(BUILD)
	$(CC) $(AP_PLATFORM_CFLAGS) -DTEST_AP_SCENARIO=4 \
	  test_bk7258_ap_platform.c $(CHIP_ROOT)/ap/bk7258_ap_platform.c \
	  $(BOARD_COMMON_SRC)/bk7258_boot.c \
	  $(CHIP_ROOT)/common/bk7258_stage_runner.c mocks/mock_nuttx_ap.c \
	  $(PLATFORM_TEST_INC) -o $@

$(AP_ENTRY_TARGET): test_bk7258_ap_entry.c \
			$(BOARD_COMMON_SRC)/bk7258_ap_entry.c | $(BUILD)
	$(CC) $(AP_ENTRY_CFLAGS) test_bk7258_ap_entry.c \
	  $(BOARD_COMMON_SRC)/bk7258_ap_entry.c \
	  -I $(MOCKS) -I $(CHIP_ROOT)/include -o $@

platform: $(STAGE_RUNNER_TARGET) $(SDK_RUNTIME_TARGET) \
	$(CP_PLATFORM_SUCCESS_TARGET) \
	$(CP_PLATFORM_SDK_FAIL_TARGET) $(CP_PLATFORM_WDT_FAIL_TARGET) \
	$(CP_PLATFORM_RESET_MARKER_FAIL_TARGET) \
	$(CP_PLATFORM_OTA_LAYOUT_FAIL_TARGET) $(CP_PLATFORM_AP_START_FAIL_TARGET) \
	$(CP_PLATFORM_PSRAM_FAIL_TARGET) $(CP_PLATFORM_OTA_POLICY_FAIL_TARGET) \
	$(CP_PLATFORM_LEGACY_LEAVES_TARGET) $(CP_PLATFORM_OPTIONAL_POLICY_TARGET) \
	$(CP_PLATFORM_RADIO_FAIL_TARGET) \
	$(AP_PLATFORM_SUCCESS_TARGET) $(AP_PLATFORM_SDK_FAIL_TARGET) \
	$(AP_PLATFORM_PSRAM_FAIL_TARGET) $(AP_PLATFORM_PM_FAIL_TARGET) \
	$(AP_ENTRY_TARGET)

$(STORAGE_UNCONFIGURED_TARGET): test_bk7258_storage.c \
					$(CHIP_ROOT)/cp/bk7258_storage.c \
					$(MOCKS)/mock_nuttx_ap.c | $(BUILD)
	$(CC) $(OTA_CONTRACT_CFLAGS) -DTEST_STORAGE_SCENARIO=1 \
	  test_bk7258_storage.c $(CHIP_ROOT)/cp/bk7258_storage.c \
	  $(MOCKS)/mock_nuttx_ap.c \
	  $(OTA_CONTRACT_INC) -o $@

$(STORAGE_CONFIG_TARGET): test_bk7258_storage.c \
					$(CHIP_ROOT)/cp/bk7258_storage.c \
					$(MOCKS)/mock_nuttx_ap.c | $(BUILD)
	$(CC) $(OTA_CONTRACT_CFLAGS) -DTEST_STORAGE_SCENARIO=2 \
	  test_bk7258_storage.c $(CHIP_ROOT)/cp/bk7258_storage.c \
	  $(MOCKS)/mock_nuttx_ap.c \
	  $(OTA_CONTRACT_INC) -o $@

$(STORAGE_LAYOUT_TARGET): test_bk7258_storage.c \
					$(CHIP_ROOT)/cp/bk7258_storage.c \
					$(MOCKS)/mock_nuttx_ap.c | $(BUILD)
	$(CC) $(OTA_CONTRACT_CFLAGS) -DTEST_STORAGE_SCENARIO=3 \
	  test_bk7258_storage.c $(CHIP_ROOT)/cp/bk7258_storage.c \
	  $(MOCKS)/mock_nuttx_ap.c \
	  $(OTA_CONTRACT_INC) -o $@

$(STORAGE_LOCKING_TARGET): test_bk7258_storage.c \
					$(CHIP_ROOT)/cp/bk7258_storage.c \
					$(MOCKS)/mock_nuttx_ap.c | $(BUILD)
	$(CC) $(OTA_CONTRACT_CFLAGS) -DTEST_STORAGE_SCENARIO=4 \
	  test_bk7258_storage.c $(CHIP_ROOT)/cp/bk7258_storage.c \
	  $(MOCKS)/mock_nuttx_ap.c \
	  $(OTA_CONTRACT_INC) -o $@

$(RADIO_STORAGE_TARGET): test_bk7258_radio_storage.c \
				$(CHIP_ROOT)/cp/bk7258_radio_storage.c \
				$(CHIP_ROOT)/cp/bk7258_radio_storage.h \
				$(MOCKS)/mock_nuttx_ap.c | $(BUILD)
	$(CC) $(RADIO_STORAGE_CFLAGS) test_bk7258_radio_storage.c \
	  $(CHIP_ROOT)/cp/bk7258_radio_storage.c \
	  $(MOCKS)/mock_nuttx_ap.c $(OTA_CONTRACT_INC) -o $@

$(BOOT_SLOT_TARGET): test_bk7258_boot_slot.c \
	$(CHIP_ROOT)/common/bk7258_boot_slot.c | $(BUILD)
	$(CC) $(OTA_CONTRACT_CFLAGS) test_bk7258_boot_slot.c \
	  $(CHIP_ROOT)/common/bk7258_boot_slot.c $(OTA_CONTRACT_INC) -o $@

$(OTA_MANAGER_TARGET): test_bk7258_ota_manager.c \
	$(CHIP_ROOT)/ap/bk7258_ota_manager.c \
	$(CHIP_ROOT)/include/bk7258_ota_manager.h \
	$(MOCKS)/mock_nuttx_ap.c | $(BUILD)
	$(CC) $(OTA_CONTRACT_CFLAGS) -DCONFIG_BK7258_OTA_MANAGER \
	  -DCONFIG_BK7258_OTA_RPMSG -DCONFIG_BK7258_AP_CORE \
	  test_bk7258_ota_manager.c \
	  $(CHIP_ROOT)/ap/bk7258_ota_manager.c \
	  $(MOCKS)/mock_nuttx_ap.c \
	  $(OTA_CONTRACT_INC) -o $@

run-ota-manager: $(OTA_MANAGER_TARGET)
	./$(OTA_MANAGER_TARGET)

$(RESET_MARKER_UNBOUND_TARGET): $(RESET_MARKER_TEST_SRCS) | $(BUILD)
	$(CC) $(RESET_MARKER_CFLAGS) -DTEST_RESET_MARKER_SCENARIO=1 \
	  $(RESET_MARKER_TEST_SRCS) \
	  $(RESET_MARKER_INC) -o $@

$(RESET_MARKER_BINDING_TARGET): $(RESET_MARKER_TEST_SRCS) | $(BUILD)
	$(CC) $(RESET_MARKER_CFLAGS) -DTEST_RESET_MARKER_SCENARIO=2 \
	  $(RESET_MARKER_TEST_SRCS) \
	  $(RESET_MARKER_INC) -o $@

$(RESET_MARKER_CAPTURE_TARGET): $(RESET_MARKER_TEST_SRCS) | $(BUILD)
	$(CC) $(RESET_MARKER_CFLAGS) -DTEST_RESET_MARKER_SCENARIO=3 \
	  $(RESET_MARKER_TEST_SRCS) \
	  $(RESET_MARKER_INC) -o $@

$(RESET_MARKER_EMPTY_TARGET): $(RESET_MARKER_TEST_SRCS) | $(BUILD)
	$(CC) $(RESET_MARKER_CFLAGS) -DTEST_RESET_MARKER_SCENARIO=4 \
	  $(RESET_MARKER_TEST_SRCS) \
	  $(RESET_MARKER_INC) -o $@

$(RESET_MARKER_TORN_TARGET): $(RESET_MARKER_TEST_SRCS) | $(BUILD)
	$(CC) $(RESET_MARKER_CFLAGS) -DTEST_RESET_MARKER_SCENARIO=5 \
	  $(RESET_MARKER_TEST_SRCS) \
	  $(RESET_MARKER_INC) -o $@

$(RESET_MARKER_OPERATIONS_TARGET): $(RESET_MARKER_TEST_SRCS) | $(BUILD)
	$(CC) $(RESET_MARKER_CFLAGS) -DTEST_RESET_MARKER_SCENARIO=6 \
	  $(RESET_MARKER_TEST_SRCS) \
	  $(RESET_MARKER_INC) -o $@

$(RESET_CAUSE_TARGET): test_bk7258_reset_cause.c \
	$(CHIP_ROOT)/common/bk7258_reset_cause.c \
	$(MOCKS)/mock_reset_cause_mmio.h | $(BUILD)
	$(CC) $(RESET_MARKER_CFLAGS) -DCONFIG_BK7258_WDT \
	  -DCONFIG_BK7258_WDT_PRETIMEOUT_PANIC \
	  -include $(MOCKS)/mock_reset_cause_mmio.h \
	  test_bk7258_reset_cause.c $(CHIP_ROOT)/common/bk7258_reset_cause.c \
	  $(RESET_MARKER_INC) -o $@

$(SYSTEM_RESET_TARGET): test_bk7258_system_reset.c \
	$(CHIP_ROOT)/cp/bk7258_system_reset.c \
	$(MOCKS)/mock_system_reset.h $(MOCKS)/driver/aon_wdt.h | $(BUILD)
	$(CC) $(RESET_MARKER_CFLAGS) \
	  -include $(MOCKS)/mock_system_reset.h \
	  test_bk7258_system_reset.c $(CHIP_ROOT)/cp/bk7258_system_reset.c \
	  $(RESET_MARKER_INC) -o $@

$(FLASH_TARGET): test_bk7258_flash.c $(CHIP_ROOT)/cp/bk7258_flash.c \
	$(CHIP_ROOT)/cp/bk7258_flash_mtd.c \
	$(MOCKS)/driver/flash.h $(MOCKS)/driver/flash_partition.h \
	$(MOCKS)/nuttx/mtd/mtd.h \
	$(MOCKS)/mock_nuttx_ap.c $(PARTITION_HEADER) | $(BUILD)
	$(CC) $(FLASH_CFLAGS) test_bk7258_flash.c \
	  $(CHIP_ROOT)/cp/bk7258_flash.c $(CHIP_ROOT)/cp/bk7258_flash_mtd.c \
	  $(MOCKS)/mock_nuttx_ap.c \
	  $(FLASH_INC) -I $(PARTITION_DIR) -o $@

$(SDK_PARTITION_TARGET): test_bk7258_sdk_partition.c \
	$(CHIP_ROOT)/common/bk7258_sdk_partition.c \
	$(MOCKS)/driver/flash_partition.h $(MOCKS)/driver/flash_types.h \
	$(PARTITION_HEADER) | $(BUILD)
	$(CC) -std=c11 -Wall -Wextra -Werror -O2 \
	  test_bk7258_sdk_partition.c \
	  $(CHIP_ROOT)/common/bk7258_sdk_partition.c \
	  -I $(MOCKS) -I $(CHIP_ROOT)/include -I $(CHIP_ROOT)/common \
	  -I $(PARTITION_DIR) -o $@

flash: $(FLASH_TARGET) $(SDK_PARTITION_TARGET)

run-flash: flash
	./$(FLASH_TARGET)
	./$(SDK_PARTITION_TARGET)

storage-contract: $(STORAGE_CONTRACT_TARGETS)

run-storage-contract: storage-contract
	./$(STORAGE_UNCONFIGURED_TARGET)
	./$(STORAGE_CONFIG_TARGET)
	./$(STORAGE_LAYOUT_TARGET)
	./$(STORAGE_LOCKING_TARGET)
	./$(RADIO_STORAGE_TARGET)

ota-contract: storage-contract $(BOOT_SLOT_TARGET)

run-ota-contract: run-storage-contract $(BOOT_SLOT_TARGET)
	./$(BOOT_SLOT_TARGET)

ota-confirmation: $(OTA_CONFIRMATION_TARGETS)

run-ota-confirmation: ota-confirmation
	./$(OTA_CONFIRM_NO_SUPERVISOR_TARGET)
	./$(OTA_CONFIRM_SUPERVISOR_TARGET)

$(ACTIVE_IMAGE_TARGET): test_bk7258_active_image.c \
		$(CHIP_ROOT)/ap/bk7258_active_image.c \
		$(CHIP_ROOT)/include/bk7258_active_image.h \
		$(CHIP_ROOT)/include/bk7258_mcuboot_format.h | $(BUILD)
	$(CC) $(ACTIVE_IMAGE_CFLAGS) test_bk7258_active_image.c \
		$(CHIP_ROOT)/ap/bk7258_active_image.c \
		-I $(MOCKS) -I $(CHIP_ROOT)/include -o $@

active-image: $(ACTIVE_IMAGE_TARGET)

run-active-image: active-image
	./$(ACTIVE_IMAGE_TARGET)

reset-marker: $(RESET_MARKER_TARGETS)

run-reset-marker: reset-marker
	./$(RESET_MARKER_UNBOUND_TARGET)
	./$(RESET_MARKER_BINDING_TARGET)
	./$(RESET_MARKER_CAPTURE_TARGET)
	./$(RESET_MARKER_EMPTY_TARGET)
	./$(RESET_MARKER_TORN_TARGET)
	./$(RESET_MARKER_OPERATIONS_TARGET)
	./$(RESET_CAUSE_TARGET)
	./$(SYSTEM_RESET_TARGET)

run-platform: platform $(CP_PLATFORM_HEAP_ORDER_TARGET)
	./$(STAGE_RUNNER_TARGET)
	./$(SDK_RUNTIME_TARGET)
	./$(CP_PLATFORM_SUCCESS_TARGET)
	./$(CP_PLATFORM_SDK_FAIL_TARGET)
	./$(CP_PLATFORM_WDT_FAIL_TARGET)
	./$(CP_PLATFORM_RESET_MARKER_FAIL_TARGET)
	./$(CP_PLATFORM_OTA_LAYOUT_FAIL_TARGET)
	./$(CP_PLATFORM_AP_START_FAIL_TARGET)
	./$(CP_PLATFORM_PSRAM_FAIL_TARGET)
	./$(CP_PLATFORM_OTA_POLICY_FAIL_TARGET)
	./$(CP_PLATFORM_LEGACY_LEAVES_TARGET)
	./$(CP_PLATFORM_OPTIONAL_POLICY_TARGET)
	./$(CP_PLATFORM_RADIO_FAIL_TARGET)
	./$(CP_PLATFORM_HEAP_ORDER_TARGET)
	./$(AP_PLATFORM_SUCCESS_TARGET)
	./$(AP_PLATFORM_SDK_FAIL_TARGET)
	./$(AP_PLATFORM_PSRAM_FAIL_TARGET)
	./$(AP_PLATFORM_PM_FAIL_TARGET)
	./$(AP_ENTRY_TARGET)

run-core: $(TARGET) $(POLICY_TARGET) $(PM_ACTIVITY_TARGET) \
	$(RPTUN_CORE_CP_TARGET) $(RPTUN_CORE_AP_TARGET) $(OTA_CONTRACT_TARGETS) \
	$(RESET_MARKER_TARGETS)
	./$(TARGET)
	./$(POLICY_TARGET)
	./$(PM_ACTIVITY_TARGET)
	./$(RPTUN_CORE_CP_TARGET)
	./$(RPTUN_CORE_AP_TARGET)
	./$(STORAGE_UNCONFIGURED_TARGET)
	./$(STORAGE_CONFIG_TARGET)
	./$(STORAGE_LAYOUT_TARGET)
	./$(STORAGE_LOCKING_TARGET)
	./$(BOOT_SLOT_TARGET)
	./$(RESET_MARKER_UNBOUND_TARGET)
	./$(RESET_MARKER_BINDING_TARGET)
	./$(RESET_MARKER_CAPTURE_TARGET)
	./$(RESET_MARKER_EMPTY_TARGET)
	./$(RESET_MARKER_TORN_TARGET)
	./$(RESET_MARKER_OPERATIONS_TARGET)
	./$(RESET_CAUSE_TARGET)
	./$(SYSTEM_RESET_TARGET)

############################################################################
# AP chip helpers.  bk7258_jpeg_decoder.c is compiled UNMODIFIED: it has no
# bare-metal MMIO or ARM barriers.  -no-pie keeps every static/allocated
# address below 4 GiB, which the driver requires (its SDK talks to 32-bit
# JPEG registers and rejects out-of-range caller buffers).
############################################################################

AP_MODULES := modules/ap
AP_DIR     := $(BUILD)/ap
AP_INC := -I $(FRAMEWORK) -I mocks -I $(AP_MODULES) -I $(CHIP_INC)
AP_CFLAGS := -std=gnu11 -Wall -Wextra -O2 -g -no-pie

$(AP_DIR):
	mkdir -p $(AP_DIR)

$(BUILD)/mock_nuttx_ap.o: mocks/mock_nuttx_ap.c mocks/nuttx/cache.h \
	mocks/nuttx/kmalloc.h mocks/nuttx/mutex.h
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(BUILD)/mock_sdk_jpeg.o: $(FRAMEWORK)/mock_sdk_jpeg.c \
	$(FRAMEWORK)/mock_sdk_jpeg.h
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(AP_DIR)/bk7258_jpeg_decoder.o: $(CHIP_ROOT)/ap/bk7258_jpeg_decoder.c \
	$(FRAMEWORK)/mock_sdk_jpeg.h mocks/mock_nuttx_ap.c | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(AP_DIR)/test_bk7258_jpeg_decoder.o: $(AP_MODULES)/test_bk7258_jpeg_decoder.c \
	$(FRAMEWORK)/mock_sdk_jpeg.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) -Wno-unused-parameter $(AP_INC) -c $< -o $@

$(BUILD)/test_bk7258_jpeg_decoder: $(AP_DIR)/bk7258_jpeg_decoder.o \
	$(AP_DIR)/test_bk7258_jpeg_decoder.o $(BUILD)/mock_nuttx_ap.o \
	$(BUILD)/mock_sdk_jpeg.o
	$(CC) $(AP_CFLAGS) $^ -o $@ $(CMOCKA) -lpthread

# bk7258_yuv_h264.c: same zero-patch template.  The suite mock TU
# implements the nxsem/clock shims, the CPU1 interrupt-route ABI and the
# media-root helper, and mirrors the DMA/H.264/YUV-BUF driver bundle.
# The yuv model needs the deterministic counting-semaphore + controllable
# clock variants of the nuttx shims; -I mocks/nuttx_yuv (before -I mocks)
# selects them for the three yuv TUs only, leaving the rptun/jpeg builds
# on the shared POSIX-sem based mocks.
MOCK_YUV_INC := -I mocks/nuttx_yuv

$(BUILD)/mock_sdk_yuv_h264.o: $(FRAMEWORK)/mock_sdk_yuv_h264.c \
	$(FRAMEWORK)/mock_sdk_yuv_h264.h mocks/driver/dma.h \
	mocks/driver/h264.h mocks/driver/yuv_buf.h mocks/nuttx_yuv/nuttx/semaphore.h \
	mocks/nuttx_yuv/nuttx/clock.h
	$(CC) $(AP_CFLAGS) $(MOCK_YUV_INC) $(AP_INC) -c $< -o $@

$(AP_DIR)/bk7258_yuv_h264.o: $(CHIP_ROOT)/ap/bk7258_yuv_h264.c \
	$(FRAMEWORK)/mock_sdk_yuv_h264.h mocks/driver/dma.h \
	mocks/driver/h264.h mocks/driver/yuv_buf.h mocks/nuttx_yuv/nuttx/semaphore.h \
	mocks/nuttx_yuv/nuttx/clock.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(MOCK_YUV_INC) $(AP_INC) -c $< -o $@

$(AP_DIR)/test_bk7258_yuv_h264.o: $(AP_MODULES)/test_bk7258_yuv_h264.c \
	$(FRAMEWORK)/mock_sdk_yuv_h264.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) -Wno-unused-parameter $(MOCK_YUV_INC) $(AP_INC) -c $< -o $@

$(BUILD)/test_bk7258_yuv_h264: $(AP_DIR)/bk7258_yuv_h264.o \
	$(AP_DIR)/test_bk7258_yuv_h264.o $(BUILD)/mock_nuttx_ap.o \
	$(BUILD)/mock_sdk_yuv_h264.o
	$(CC) $(AP_CFLAGS) $^ -o $@ $(CMOCKA) -lpthread

# bk7258_scale_rotate.c: same template.  The suite mock TU implements the
# nxsem/clock shims and mirrors the scale (hw_scale) + rotator (rott)
# driver bundle plus the CPU1 interrupt-route ABI.  Two mechanical patches
# apply to the throwaway copy (framework/patch.py): the ARM dsb barrier
# and the Scale1 write-burst register poke (routed to mock_reg32); the
# The arch/chip forwarding header resolves the real public interface through
# CHIP_INC; only MMIO/barrier instructions are patched in the throwaway copy.
$(BUILD)/bk7258_scale_rotate.c: $(CHIP_ROOT)/ap/bk7258_scale_rotate.c \
	framework/patch.py | $(BUILD)
	python3 framework/patch.py scale_rotate $< $@

$(BUILD)/mock_sdk_scale_rotate.o: $(FRAMEWORK)/mock_sdk_scale_rotate.c \
	$(FRAMEWORK)/mock_sdk_scale_rotate.h mocks/driver/hw_scale.h \
	mocks/driver/rott_driver.h mocks/nuttx_yuv/nuttx/semaphore.h \
	mocks/nuttx_yuv/nuttx/clock.h
	$(CC) $(AP_CFLAGS) $(MOCK_YUV_INC) $(AP_INC) -c $< -o $@

$(AP_DIR)/bk7258_scale_rotate.o: $(BUILD)/bk7258_scale_rotate.c \
	$(FRAMEWORK)/mock_sdk_scale_rotate.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(MOCK_YUV_INC) $(AP_INC) -I $(CHIP_INC) -c $< -o $@

$(AP_DIR)/test_bk7258_scale_rotate.o: $(AP_MODULES)/test_bk7258_scale_rotate.c \
	$(FRAMEWORK)/mock_sdk_scale_rotate.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) -Wno-unused-parameter $(MOCK_YUV_INC) $(AP_INC) \
		-I $(CHIP_INC) -c $< -o $@

$(BUILD)/test_bk7258_scale_rotate: $(AP_DIR)/bk7258_scale_rotate.o \
	$(AP_DIR)/test_bk7258_scale_rotate.o $(BUILD)/mock_nuttx_ap.o \
	$(BUILD)/mock_sdk_scale_rotate.o $(BUILD)/mock_reg32.o
	$(CC) $(AP_CFLAGS) $^ -o $@ $(CMOCKA) -lpthread

# bk7258_irda.c: chip-owned register-level NEC decoder (CP-owned).  The
# throwaway copy is patched by framework/patch.py so the direct IRDA
# register accessors route through mock_reg32; the SDK surface
# (gpio_dev_map, bk_int_isr_register, register_driver) is programmable via
# framework/mock_sdk_irda, and the key-debounce wdog fires explicitly via
# framework/mock_wdog.
$(BUILD)/bk7258_irda.c: $(CHIP_ROOT)/cp/bk7258_irda.c \
	framework/patch.py | $(BUILD)
	python3 framework/patch.py irda $< $@

$(BUILD)/mock_sdk_irda.o: $(FRAMEWORK)/mock_sdk_irda.c \
	$(FRAMEWORK)/mock_sdk_irda.h mocks/driver/gpio.h \
	mocks/driver/gpio_types.h mocks/driver/int.h mocks/driver/int_types.h \
	mocks/nuttx/fs/fs.h
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(BUILD)/mock_wdog.o: $(FRAMEWORK)/mock_wdog.c mocks/nuttx/wdog.h
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(BUILD)/mock_sem.o: $(FRAMEWORK)/mock_sem.c mocks/nuttx/semaphore.h
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(AP_DIR)/bk7258_irda.o: $(BUILD)/bk7258_irda.c \
	$(FRAMEWORK)/mock_sdk_irda.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(AP_INC) -I $(CHIP_INC) \
		-DCONFIG_BK7258_IRDA=1 -c $< -o $@

$(AP_DIR)/test_bk7258_irda.o: $(AP_MODULES)/test_bk7258_irda.c \
	$(FRAMEWORK)/mock_sdk_irda.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) -Wno-unused-parameter $(AP_INC) \
		-I $(CHIP_INC) -c $< -o $@

$(BUILD)/test_bk7258_irda: $(AP_DIR)/bk7258_irda.o \
	$(AP_DIR)/test_bk7258_irda.o $(BUILD)/mock_nuttx_ap.o \
	$(BUILD)/mock_sdk_irda.o $(BUILD)/mock_wdog.o $(BUILD)/mock_sem.o \
	$(BUILD)/mock_reg32.o
	$(CC) $(AP_CFLAGS) $^ -o $@ $(CMOCKA) -lpthread

# bk7258_pinmux.c: centralized SYS selector and pad ownership.  The host
# copy only redirects MMIO and the ARM barrier; the AMP/IRQ behavior is
# supplied and asserted by the test translation unit.
$(BUILD)/bk7258_pinmux.c: $(CHIP_ROOT)/ap/bk7258_pinmux.c \
	framework/patch.py | $(BUILD)
	python3 framework/patch.py pinmux $< $@

$(AP_DIR)/bk7258_pinmux.o: $(BUILD)/bk7258_pinmux.c \
	$(FRAMEWORK)/mock_reg32.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(AP_INC) -DCONFIG_BK7258_AP_CORE=1 -c $< -o $@

$(AP_DIR)/test_bk7258_pinmux.o: $(AP_MODULES)/test_bk7258_pinmux.c \
	$(FRAMEWORK)/mock_reg32.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(AP_INC) -c $< -o $@

$(BUILD)/test_bk7258_pinmux: $(AP_DIR)/bk7258_pinmux.o \
		$(AP_DIR)/test_bk7258_pinmux.o $(BUILD)/mock_reg32.o
	$(CC) $(AP_CFLAGS) $^ -o $@

# bk7258_media_root.c: verify the AP SMP session gate owns exactly one
# CP composite AUDIO reference and converges ambiguous RPMsg outcomes before
# allowing a later MIC/DAC owner.

$(AP_DIR)/bk7258_media_root.o: $(CHIP_ROOT)/ap/bk7258_media_root.c \
		$(CHIP_ROOT)/ap/bk7258_media_root.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(AP_INC) -I $(CHIP_ROOT)/ap \
		-DCONFIG_BK7258_AP_CORE -DCONFIG_BK7258_PM_CLOCK -c $< -o $@

$(AP_DIR)/test_bk7258_media_root.o: \
		$(AP_MODULES)/test_bk7258_media_root.c | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(AP_INC) -I $(CHIP_ROOT)/ap \
		-DCONFIG_BK7258_AP_CORE -DCONFIG_BK7258_PM_CLOCK -c $< -o $@

$(BUILD)/test_bk7258_media_root: $(AP_DIR)/bk7258_media_root.o \
		$(AP_DIR)/test_bk7258_media_root.o
	$(CC) $(AP_CFLAGS) $^ -o $@ -lpthread

run-media-audio-session: $(BUILD)/test_bk7258_media_root
	./$(BUILD)/test_bk7258_media_root

# bk7258_can.c: zero-patch template -- no bare-metal MMIO or ARM
# barriers.  The suite mock TU provides its own real-blocking nxsem_*
# (mocks/nuttx_can/nuttx/semaphore.h, selected by MOCK_CAN_INC) and the
# real-pthread kthread surface; the rx thread drains the SDK FIFO mock
# and the upper-half records.  The real arch/chip public headers are
# re-pointed at the mock variants via MOCKS first in AP_INC.
MOCK_CAN_INC := -I mocks/nuttx_can

$(BUILD)/mock_sdk_can.o: $(FRAMEWORK)/mock_sdk_can.c \
	$(FRAMEWORK)/mock_sdk_can.h mocks/nuttx_can/nuttx/semaphore.h \
	mocks/nuttx/can/can.h mocks/driver/hal/hal_can_types.h
	$(CC) $(AP_CFLAGS) $(MOCK_CAN_INC) $(AP_INC) -c $< -o $@

$(AP_DIR)/bk7258_can.o: $(CHIP_ROOT)/ap/bk7258_can.c \
	$(FRAMEWORK)/mock_sdk_can.h mocks/nuttx_can/nuttx/semaphore.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) $(MOCK_CAN_INC) $(AP_INC) -c $< -o $@

$(AP_DIR)/test_bk7258_can.o: $(AP_MODULES)/test_bk7258_can.c \
	$(FRAMEWORK)/mock_sdk_can.h mocks/nuttx_can/nuttx/semaphore.h | $(AP_DIR)
	$(CC) $(AP_CFLAGS) -Wno-unused-parameter $(MOCK_CAN_INC) $(AP_INC) \
		-c $< -o $@

$(BUILD)/test_bk7258_can: $(AP_DIR)/bk7258_can.o \
	$(AP_DIR)/test_bk7258_can.o $(BUILD)/mock_nuttx_ap.o \
	$(BUILD)/mock_sdk_can.o
	$(CC) $(AP_CFLAGS) $^ -o $@ $(CMOCKA) -lpthread

AP_TARGETS := $(BUILD)/test_bk7258_jpeg_decoder \
		$(BUILD)/test_bk7258_yuv_h264 \
		$(BUILD)/test_bk7258_scale_rotate \
		$(BUILD)/test_bk7258_can \
		$(BUILD)/test_bk7258_irda \
		$(BUILD)/test_bk7258_pinmux \
		$(BUILD)/test_bk7258_media_root

ap: $(AP_TARGETS)

.PHONY: run-ap
run-ap: ap
	./$(BUILD)/test_bk7258_jpeg_decoder
	./$(BUILD)/test_bk7258_yuv_h264
	./$(BUILD)/test_bk7258_scale_rotate
	./$(BUILD)/test_bk7258_can
	./$(BUILD)/test_bk7258_irda
	./$(BUILD)/test_bk7258_pinmux
	./$(BUILD)/test_bk7258_media_root

clean:
	rm -rf $(BUILD)

.PHONY: run-camera-storage
run-camera-storage:
	python3 test_sdio_tx_sram.py

.PHONY: run-voice-kws
include kws_frontend_sources.mk

TFLM_ROOT := $(OPENVELA_ROOT)/apps/mlearning/tflite-micro/tflite-micro
TFLM_MICROFRONTEND_ROOT := $(TFLM_ROOT)/tensorflow/lite/experimental/microfrontend/lib
KISSFFT_ROOT := $(OPENVELA_ROOT)/apps/math/kissfft/kissfft
VOICE_KWS_BUILD := $(BUILD)/voice-kws
VOICE_KWS_INC := -I$(VOICE_PACK_ROOT) -I$(TFLM_ROOT) -I$(KISSFFT_ROOT)
VOICE_KWS_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 $(VOICE_KWS_INC)
# TFLM marks this experimental library as having no default warning policy;
# retain warnings in the host build without promoting upstream-only warnings.
VOICE_KWS_CXXFLAGS := -std=c++17 -Wall -Wextra -O2 $(VOICE_KWS_INC)
VOICE_KWS_TFLM_C_OBJECTS := $(addprefix $(VOICE_KWS_BUILD)/, \
	$(VOICE_KWS_TFLM_C_SOURCES:.c=.o))
VOICE_KWS_TFLM_CXX_OBJECTS := $(addprefix $(VOICE_KWS_BUILD)/, \
	$(VOICE_KWS_TFLM_CXX_SOURCES:.cc=.o))
VOICE_KWS_OBJECTS := $(VOICE_KWS_BUILD)/test_bk7258_voice_kws_core.o \
	$(VOICE_KWS_BUILD)/bk7258_voice_kws.o \
	$(VOICE_KWS_BUILD)/bk7258_voice_kws_frontend.o \
	$(VOICE_KWS_TFLM_C_OBJECTS) $(VOICE_KWS_TFLM_CXX_OBJECTS)
VOICE_KWS_TARGET := $(BUILD)/test_bk7258_voice_kws_core

$(VOICE_KWS_BUILD):
	mkdir -p $@

$(VOICE_KWS_BUILD)/test_bk7258_voice_kws_core.o: test_bk7258_voice_kws_core.c \
	$(VOICE_PACK_ROOT)/bk7258_voice_kws.h | $(VOICE_KWS_BUILD)
	$(CC) $(VOICE_KWS_CFLAGS) -c $< -o $@

$(VOICE_KWS_BUILD)/bk7258_voice_kws.o: $(VOICE_PACK_ROOT)/bk7258_voice_kws.c \
	$(VOICE_PACK_ROOT)/bk7258_voice_kws.h | $(VOICE_KWS_BUILD)
	$(CC) $(VOICE_KWS_CFLAGS) -c $< -o $@

$(VOICE_KWS_BUILD)/bk7258_voice_kws_frontend.o: \
	$(VOICE_PACK_ROOT)/bk7258_voice_kws_frontend.c \
	$(VOICE_PACK_ROOT)/bk7258_voice_kws_frontend.h | $(VOICE_KWS_BUILD)
	$(CC) $(VOICE_KWS_CFLAGS) -c $< -o $@

$(VOICE_KWS_BUILD)/%.o: $(TFLM_MICROFRONTEND_ROOT)/%.c | $(VOICE_KWS_BUILD)
	$(CC) $(VOICE_KWS_CFLAGS) -c $< -o $@

$(VOICE_KWS_BUILD)/%.o: $(TFLM_MICROFRONTEND_ROOT)/%.cc | $(VOICE_KWS_BUILD)
	$(CXX) $(VOICE_KWS_CXXFLAGS) -c $< -o $@

$(VOICE_KWS_TARGET): $(VOICE_KWS_OBJECTS) | $(BUILD)
	$(CXX) $^ -lm -o $@

run-voice-kws: $(VOICE_KWS_TARGET)
	./$(VOICE_KWS_TARGET)
	python3 -m pytest -q test_bk7258_voice_kws.py test_bk7258_voice_kws_frontend.py

# Native upstream TFLM probe; isolated from the board's AP/CP build outputs.
# Generate probe.tflite/features.f32/scores.f32 with the existing Python KWS
# test and BKVOICE_STREAM_PROBE_DIR before running this target.
VOICE_STREAM_BUILD ?= $(OPENVELA_ROOT)/out/streaming-kws-host
VOICE_STREAM_KERNELS := conv conv_common depthwise_conv depthwise_conv_common \
	pooling pooling_common fully_connected fully_connected_common reshape \
	reshape_common softmax softmax_common concatenation strided_slice \
	strided_slice_common quantize quantize_common kernel_util
VOICE_STREAM_DIRS := tensorflow/lite/c tensorflow/lite/core/c tensorflow/lite/schema \
	tensorflow/lite/core/api tensorflow/lite/kernels \
	tensorflow/lite/kernels/internal tensorflow/lite/kernels/internal/reference \
	tensorflow/lite/kernels/internal/optimized tensorflow/lite/micro \
	tensorflow/lite/micro/arena_allocator tensorflow/lite/micro/memory_planner \
	tensorflow/lite/micro/tflite_bridge
VOICE_STREAM_SOURCES := $(foreach d,$(VOICE_STREAM_DIRS),$(wildcard $(TFLM_ROOT)/$(d)/*.cc))
VOICE_STREAM_SOURCES := $(filter-out %test.cc %test_common.cc %test_helpers.cc \
	%test_helper_custom_ops.cc %mock_micro_graph.cc %fake_micro_context.cc,$(VOICE_STREAM_SOURCES))
VOICE_STREAM_SOURCES += $(addprefix $(TFLM_ROOT)/tensorflow/lite/micro/kernels/,$(addsuffix .cc,$(VOICE_STREAM_KERNELS)))
VOICE_STREAM_OBJECTS := $(patsubst $(TFLM_ROOT)/%.cc,$(VOICE_STREAM_BUILD)/obj/%.o,$(VOICE_STREAM_SOURCES))
VOICE_STREAM_FLAGS := -std=c++17 -O2 -fno-exceptions -fno-rtti -DTF_LITE_STATIC_MEMORY \
	-DTF_LITE_DISABLE_X86_NEON -DTFLITE_WITH_STABLE_ABI=0 \
	-DTFLITE_USE_OPAQUE_DELEGATE=0 -DTFLITE_SINGLE_ROUNDING=0 \
	-I$(VOICE_PACK_ROOT) -I$(TFLM_ROOT) \
	-I$(OPENVELA_ROOT)/apps/math/gemmlowp/gemmlowp \
	-I$(OPENVELA_ROOT)/apps/math/ruy/ruy \
	-I$(OPENVELA_ROOT)/apps/system/flatbuffers/flatbuffers/include
VOICE_STREAM_PACKAGE_FLAGS := -I$(MOCKS) -I$(VOICE_PACK_ROOT) -I$(TFLM_ROOT) \
	-I$(OPENVELA_ROOT)/apps/crypto/mbedtls/mbedtls/include \
	-DBKVOICE_WAKE_PACKAGE_ROOT='"/proc/self/cwd/wake-models"' \
	-DCONFIG_BK7258_VOICE_KWS_ARENA_BYTES=524288
VOICE_STREAM_PACKAGE_OBJECTS := $(addprefix $(VOICE_STREAM_BUILD)/, \
	wake_package.o provision_store.o sha256.o platform_util.o constant_time.o)

$(VOICE_STREAM_BUILD)/wake_package.o: $(VOICE_PACK_ROOT)/bk7258_voice_wake_package.c \
	$(VOICE_PACK_ROOT)/bk7258_voice_wake_package.h
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -pthread $(VOICE_STREAM_PACKAGE_FLAGS) -c $< -o $@

$(VOICE_STREAM_BUILD)/provision_store.o: $(VOICE_PACK_ROOT)/bk7258_provision_store.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 $(VOICE_STREAM_PACKAGE_FLAGS) -c $< -o $@

$(VOICE_STREAM_BUILD)/%.o: $(OPENVELA_ROOT)/apps/crypto/mbedtls/mbedtls/library/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -ffunction-sections -fdata-sections $(VOICE_STREAM_PACKAGE_FLAGS) -c $< -o $@

$(VOICE_STREAM_BUILD)/obj/%.o: $(TFLM_ROOT)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(VOICE_STREAM_FLAGS) -c $< -o $@

$(VOICE_STREAM_BUILD)/test_model: test_bk7258_voice_kws_model.cc \
	$(VOICE_PACK_ROOT)/bk7258_voice_kws_model.cc \
	$(VOICE_PACK_ROOT)/bk7258_voice_kws_model.h $(VOICE_STREAM_OBJECTS) \
	$(VOICE_STREAM_PACKAGE_OBJECTS)
	$(CXX) $(VOICE_STREAM_FLAGS) $(VOICE_STREAM_PACKAGE_FLAGS) $< \
	    $(VOICE_PACK_ROOT)/bk7258_voice_kws_model.cc \
	    $(VOICE_STREAM_OBJECTS) $(VOICE_STREAM_PACKAGE_OBJECTS) -Wl,--gc-sections -pthread -lm -o $@

.PHONY: run-voice-kws-stream-model
run-voice-kws-stream-model: $(VOICE_STREAM_BUILD)/test_model
	$< $(VOICE_STREAM_BUILD)/probe.tflite $(VOICE_STREAM_BUILD)/features.f32 \
	    $(VOICE_STREAM_BUILD)/scores.f32

.PHONY: run-preferences
$(BUILD)/test_bk7258_preferences_storage: test_bk7258_preferences_storage.c \
	$(VOICE_PACK_ROOT)/bk7258_preferences_storage.c \
	$(VOICE_PACK_ROOT)/bk7258_preferences_storage.h \
	$(VOICE_PACK_ROOT)/bk7258_media_volume.h | $(BUILD)
	$(CC) -std=gnu11 -Wall -Wextra -Werror -O2 -pthread \
	    -DCONFIG_BK7258_PREFERENCES_BLOCKDEV='"/dev/mmcsd0"' \
	    -I mocks -I $(VOICE_PACK_ROOT) $< \
	    $(VOICE_PACK_ROOT)/bk7258_preferences_storage.c -o $@

$(BUILD)/test_bk7258_preferences: test_bk7258_preferences.c \
	$(VOICE_PACK_ROOT)/bk7258_preferences.c \
	$(VOICE_PACK_ROOT)/bk7258_preferences_storage.h \
	$(VOICE_PACK_ROOT)/bk7258_preferences.h mocks/kvdb.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread $< \
	    -I $(MOCKS) -I $(OPENVELA_ROOT)/external/unqlite/unqlite \
	    -I $(VOICE_PACK_ROOT) $(VOICE_PACK_ROOT)/bk7258_preferences.c -o $@

$(BUILD)/test_bk7258_preferences_voice_volume: test_bk7258_preferences_voice_volume.c \
	$(VOICE_PACK_ROOT)/bk7258_preferences.c | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread \
	    -DCONFIG_BK7258_VOICE_VOLUME_PERSISTENCE=1 \
	    -I $(MOCKS) -I $(OPENVELA_ROOT)/external/unqlite/unqlite \
	    -I $(VOICE_PACK_ROOT) $^ -o $@

run-preferences: $(BUILD)/test_bk7258_preferences \
	$(BUILD)/test_bk7258_preferences_voice_volume \
	$(BUILD)/test_bk7258_preferences_storage
	./$(BUILD)/test_bk7258_preferences
	./$(BUILD)/test_bk7258_preferences_voice_volume
	./$(BUILD)/test_bk7258_preferences_storage
.PHONY: run-voice-ota-store run-voice-ota-flow
VOICE_OTA_MBEDTLS_SOURCE := $(OPENVELA_ROOT)/apps/crypto/mbedtls/mbedtls
VOICE_OTA_MBEDTLS_BUILD := $(BUILD)/voice-ota-store-mbedtls
VOICE_OTA_MBEDCRYPTO := $(VOICE_OTA_MBEDTLS_BUILD)/library/libmbedcrypto.a

$(VOICE_OTA_MBEDCRYPTO): $(VOICE_OTA_MBEDTLS_SOURCE)/CMakeLists.txt | $(BUILD)
	cmake -S $(VOICE_OTA_MBEDTLS_SOURCE) -B $(VOICE_OTA_MBEDTLS_BUILD) \
		-DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF \
		-DCMAKE_C_FLAGS=-Wno-error=missing-prototypes -DCMAKE_BUILD_TYPE=Release
	cmake --build $(VOICE_OTA_MBEDTLS_BUILD) -j2

# Product-store integration: links the actual SCF1 implementation rather than
# a local double, and checks BVO2 codec/state and publication recovery paths.
$(BUILD)/test_bk7258_voice_ota_store: test_bk7258_voice_ota_store.c \
	$(VOICE_PACK_ROOT)/bk7258_voice_ota_store.c \
	$(VOICE_PACK_ROOT)/bk7258_voice_ota_store.h \
	$(VOICE_PACK_ROOT)/bk7258_provision_store.c \
	$(VOICE_PACK_ROOT)/bk7258_provision_store.h \
	$(VOICE_OTA_MBEDCRYPTO) | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread test_bk7258_voice_ota_store.c \
		$(VOICE_PACK_ROOT)/bk7258_voice_ota_store.c \
		$(VOICE_PACK_ROOT)/bk7258_provision_store.c \
		-I mocks -I $(VOICE_PACK_ROOT) -I $(CHIP_ROOT)/include \
		-I $(VOICE_OTA_MBEDTLS_SOURCE)/include \
		-Wl,--wrap=fsync -Wl,--wrap=rename $(VOICE_OTA_MBEDCRYPTO) -o $@

run-voice-ota-store: $(BUILD)/test_bk7258_voice_ota_store
	root=$$(mktemp -d "$(abspath $(BUILD))/voice-ota.XXXXXX"); \
	./$(BUILD)/test_bk7258_voice_ota_store "$$root"

$(BUILD)/test_bk7258_voice_ota_flow: test_bk7258_voice_ota_flow.c \
	$(VOICE_PACK_ROOT)/bk7258_voice_ota_flow.c \
	$(VOICE_PACK_ROOT)/bk7258_voice_ota_flow.h \
	$(VOICE_PACK_ROOT)/bk7258_voice_ota_store.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) test_bk7258_voice_ota_flow.c \
		$(VOICE_PACK_ROOT)/bk7258_voice_ota_flow.c \
		-I $(MOCKS) -I $(VOICE_PACK_ROOT) -I $(CHIP_ROOT)/include -o $@

run-voice-ota-flow: $(BUILD)/test_bk7258_voice_ota_flow
	./$(BUILD)/test_bk7258_voice_ota_flow

.PHONY: run-cloud-config run-cloud-request
$(BUILD)/test_bk7258_cloud_config: test_bk7258_cloud_config.c \
	$(VOICE_PACK_ROOT)/bk7258_cloud_config.c \
	$(VOICE_PACK_ROOT)/bk7258_cloud_config.h | $(BUILD)
	$(CC) $(CFLAGS) -Werror -I $(VOICE_PACK_ROOT) \
		test_bk7258_cloud_config.c $(VOICE_PACK_ROOT)/bk7258_cloud_config.c -o $@

run-cloud-config: $(BUILD)/test_bk7258_cloud_config
	./$(BUILD)/test_bk7258_cloud_config

run-cloud-request:
	python3 test_bk7258_cloud_request.py

$(BUILD)/test_agent_final_stream: test_bk7258_cloud_request.c \
	$(OPENVELA_ROOT)/packages/ai_agent/src/llm/llm_stream.c \
	$(OPENVELA_ROOT)/packages/ai_agent/src/llm/llm_proxy.c \
	$(OPENVELA_ROOT)/packages/ai_agent/src/llm/llm_parse.c \
	$(OPENVELA_ROOT)/packages/ai_agent/src/llm/llm_stream.h | $(BUILD)
	$(CC) $(CFLAGS) -Werror -Wno-error=stringop-truncation \
		-DTEST_AGENT_FINAL_STREAM -DOK=0 -DERROR=-1 \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-I $(MOCKS) -I $(OPENVELA_ROOT)/packages/ai_agent/include \
		-I $(OPENVELA_ROOT)/packages/ai_agent/src \
		-I $(OPENVELA_ROOT)/apps/netutils/cjson/cJSON $< \
		$(OPENVELA_ROOT)/packages/ai_agent/src/llm/llm_stream.c \
		$(OPENVELA_ROOT)/packages/ai_agent/src/llm/llm_proxy.c \
		$(OPENVELA_ROOT)/packages/ai_agent/src/llm/llm_parse.c \
		$(OPENVELA_ROOT)/apps/netutils/cjson/cJSON/cJSON.c -o $@

$(BUILD)/test_agent_endpoint: test_bk7258_cloud_request.c \
	$(OPENVELA_ROOT)/packages/ai_agent/src/voice/voice_endpoint.c \
	$(OPENVELA_ROOT)/packages/ai_agent/include/voice/voice_endpoint.h | $(BUILD)
	$(CC) $(CFLAGS) -Werror -DTEST_AGENT_ENDPOINT \
		-I $(OPENVELA_ROOT)/packages/ai_agent/include $< \
		$(OPENVELA_ROOT)/packages/ai_agent/src/voice/voice_endpoint.c -o $@

.PHONY: run-agent-final-stream run-agent-endpoint
run-agent-final-stream: $(BUILD)/test_agent_final_stream
	./$(BUILD)/test_agent_final_stream

run-agent-endpoint: $(BUILD)/test_agent_endpoint
	./$(BUILD)/test_agent_endpoint

.PHONY: run-cloud-http
run-cloud-http:
	python3 test_bk7258_cloud_http.py

.PHONY: test-control-session
.PHONY: test-control-ota-request
test-control-ota-request: $(BUILD)/test_control_ota_request
	$(BUILD)/test_control_ota_request

$(BUILD)/test_control_ota_request: $(REPOSITORY_ROOT)/app/bk7258/bk7258_control_ota_request.c \
                                 $(TEST_ROOT)/test_control_ota_request.c \
                                 $(REPOSITORY_ROOT)/app/bk7258/bk7258_control_ota_request.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Werror -I$(REPOSITORY_ROOT)/app/bk7258 $(filter %.c,$^) -o $@

test-control-session: $(BUILD)/test_control_session
	$(BUILD)/test_control_session

$(BUILD)/test_control_session: $(REPOSITORY_ROOT)/app/bk7258/bk7258_control_session.c \
                              $(TEST_ROOT)/test_control_session.c \
                              $(REPOSITORY_ROOT)/app/bk7258/bk7258_control_session.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(REPOSITORY_ROOT)/app/bk7258 $(filter %.c,$^) -o $@

.PHONY: test-control-pair
test-control-pair: $(BUILD)/test_control_pair
	$(BUILD)/test_control_pair

$(BUILD)/test_control_pair: $(REPOSITORY_ROOT)/app/bk7258/bk7258_control_session.c \
                           $(REPOSITORY_ROOT)/app/bk7258/bk7258_control_pair.c \
                           $(TEST_ROOT)/test_control_pair.c \
                           $(REPOSITORY_ROOT)/app/bk7258/bk7258_control_pair.h \
                           $(REPOSITORY_ROOT)/app/bk7258/bk7258_control_session.h \
                           $(REPOSITORY_ROOT)/app/bk7258/bk7258_provision_tls.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Werror -I$(REPOSITORY_ROOT)/app/bk7258 -I$(OPENVELA_ROOT)/apps/crypto/mbedtls/mbedtls/include $(filter %.c,$^) -o $@

# Requirement-level host contracts; each case is a separate process so an
# assertion cannot silently prevent later cases from being collected.
$(BUILD)/test_shaniu_key_contract: test_shaniu_key_contract.c $(VOICE_PACK_ROOT)/bk7258_product_keys.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) $< -I $(VOICE_PACK_ROOT) -o $@

$(BUILD)/test_shaniu_volume_contract: test_shaniu_volume_contract.c $(VOICE_PACK_ROOT)/bk7258_media_volume.c $(CHIP_ROOT)/ap/bk7258_usbmode.c | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread -DCONFIG_BK7258_USBMODE -DCONFIG_BK7258_AP_CORE \
		-DCONFIG_BK7258_USBCDC -DCONFIG_BK7258_USBMSC -DCONFIG_BK7258_OTA_SOURCE_FILE \
		-DCONFIG_BK7258_USBMODE_BLOCKDEV=\"/dev/mmcsd0\" -DCONFIG_BK7258_USBMODE_REENUM_DELAY_MS=1 \
		-I $(MOCKS) -I $(CHIP_ROOT)/include -I $(VOICE_PACK_ROOT) \
		$^ mocks/mock_nuttx_ap.c -o $@

.PHONY: run-shaniu-contracts
run-shaniu-contracts:
	python3 test_shaniu_contracts.py

$(BUILD)/test_shaniu_volume_transition: test_shaniu_volume_transition.c \
    $(VOICE_PACK_ROOT)/bk7258_media_volume.c $(VOICE_PACK_ROOT)/bk7258_media_volume.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -DCONFIG_BK7258_USBMODE \
	    -I $(MOCKS) -I $(CHIP_ROOT)/include -I $(VOICE_PACK_ROOT) \
	    -include $(CHIP_ROOT)/include/bk7258_usbmode.h \
	    -include $(MOCKS)/bk7258_usbmode_lease.h \
	    $< $(VOICE_PACK_ROOT)/bk7258_media_volume.c -o $@

$(BUILD)/test_shaniu_power_contract: test_shaniu_power_contract.c \
    $(VOICE_PACK_ROOT)/bk7258_agent_product_power.inc | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -I $(VOICE_PACK_ROOT) $< -o $@

$(BUILD)/test_shaniu_control_quiesce: test_shaniu_control_quiesce.c \
    $(VOICE_PACK_ROOT)/bk7258_control_session.c $(VOICE_PACK_ROOT)/bk7258_control_session.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -I $(VOICE_PACK_ROOT) $< \
	    $(VOICE_PACK_ROOT)/bk7258_control_session.c -o $@

OWNER_CONTRACT_FLAGS := -std=c11 -Wall -Wextra -Werror \
    -I $(MOCKS) -I $(OPENVELA_ROOT)/apps/crypto/mbedtls/mbedtls/include -I $(VOICE_PACK_ROOT)
OWNER_CONTRACT_SOURCES := $(VOICE_PACK_ROOT)/bk7258_provision_owner.c \
    $(VOICE_PACK_ROOT)/bk7258_control_session.c $(VOICE_PACK_ROOT)/bk7258_provision_scan.c
OWNER_CONTRACT_HEADERS := $(VOICE_PACK_ROOT)/bk7258_provision_owner.h \
    $(VOICE_PACK_ROOT)/bk7258_control_session.h
$(BUILD)/test_shaniu_owner: test_provision_owner.c $(OWNER_CONTRACT_SOURCES) $(OWNER_CONTRACT_HEADERS) | $(BUILD)
	$(CC) $(OWNER_CONTRACT_FLAGS) $< $(OWNER_CONTRACT_SOURCES) -o $@
$(BUILD)/shaniu_owner_peer.o: test_provision_owner.c $(OWNER_CONTRACT_HEADERS) | $(BUILD)
	$(CC) $(OWNER_CONTRACT_FLAGS) -DTEST_OWNER_LIBRARY -c $< -o $@
$(BUILD)/test_shaniu_power_owner: test_shaniu_power_contract.c $(BUILD)/shaniu_owner_peer.o \
    $(OWNER_CONTRACT_SOURCES) $(OWNER_CONTRACT_HEADERS) $(VOICE_PACK_ROOT)/bk7258_agent_product_power.inc | $(BUILD)
	$(CC) $(OWNER_CONTRACT_FLAGS) -DTEST_REAL_OWNER $< $(BUILD)/shaniu_owner_peer.o \
	    $(OWNER_CONTRACT_SOURCES) -o $@

$(BUILD)/test_shaniu_power_pixels: test_shaniu_power_pixels.c \
    $(VOICE_PACK_ROOT)/bk7258_display_power_pixels.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -I $(VOICE_PACK_ROOT) $< -o $@

$(BUILD)/test_shaniu_msc_stop: test_shaniu_msc_stop.c $(CHIP_ROOT)/ap/bk7258_usbmsc_start.inc $(CHIP_ROOT)/ap/bk7258_usbmsc_stop.inc | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -I $(CHIP_ROOT)/ap $< -o $@

$(BUILD)/test_shaniu_usb_cleanup: test_shaniu_usb_cleanup.c $(CHIP_ROOT)/ap/bk7258_usbmode.c | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread -DCONFIG_BK7258_USBMODE -DCONFIG_BK7258_AP_CORE \
		-DCONFIG_BK7258_USBCDC -DCONFIG_BK7258_USBMSC -DCONFIG_BK7258_OTA_SOURCE_FILE \
		-DCONFIG_BK7258_USBMODE_BLOCKDEV=\"/dev/mmcsd0\" -DCONFIG_BK7258_USBMODE_REENUM_DELAY_MS=1 \
		-I $(MOCKS) -I $(CHIP_ROOT)/include -I $(VOICE_PACK_ROOT) \
		$^ mocks/mock_nuttx_ap.c -o $@

$(BUILD)/test_shaniu_focus: test_shaniu_focus.c $(VOICE_PACK_ROOT)/bk7258_focus.c $(VOICE_PACK_ROOT)/bk7258_focus.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -I $(VOICE_PACK_ROOT) $(filter %.c,$^) -o $@

$(BUILD)/test_shaniu_focus_wire: test_shaniu_focus_wire.c $(VOICE_PACK_ROOT)/bk7258_focus.c $(VOICE_PACK_ROOT)/bk7258_control_session.c $(VOICE_PACK_ROOT)/bk7258_focus.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -I $(VOICE_PACK_ROOT) $(filter %.c,$^) -o $@

$(BUILD)/test_shaniu_focus_pixels: test_shaniu_focus_pixels.c $(VOICE_PACK_ROOT)/bk7258_focus_pixels.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -I $(VOICE_PACK_ROOT) $< -o $@

$(BUILD)/test_shaniu_focus_render: test_shaniu_focus_render.c $(VOICE_PACK_ROOT)/bk7258_focus_pixels.h $(VOICE_PACK_ROOT)/bk7258_display_focus.inc | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -I $(VOICE_PACK_ROOT) $< -o $@

$(BUILD)/test_shaniu_display_snapshot: test_shaniu_display_snapshot.c $(VOICE_PACK_ROOT)/bk7258_display_snapshot.inc $(VOICE_PACK_ROOT)/bk7258_display_service.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread -I $(MOCKS) -I $(VOICE_PACK_ROOT) $< -o $@

$(BUILD)/test_shaniu_display_intent: test_shaniu_display_intent.c $(VOICE_PACK_ROOT)/bk7258_display_intent.inc $(VOICE_PACK_ROOT)/bk7258_display_service.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread -I $(MOCKS) -I $(VOICE_PACK_ROOT) $< -o $@

$(BUILD)/test_shaniu_expression_cancel: test_shaniu_expression_cancel.c $(VOICE_PACK_ROOT)/bk7258_display_intent.inc $(VOICE_PACK_ROOT)/bk7258_agent_display_control.inc $(VOICE_PACK_ROOT)/bk7258_display_service.h | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread -I $(MOCKS) -I $(VOICE_PACK_ROOT) $< -o $@

$(BUILD)/test_shaniu_models_durability: test_shaniu_models_durability.c $(VOICE_PACK_ROOT)/bk7258_preferences.c $(VOICE_PACK_ROOT)/bk7258_cloud_config.c $(VOICE_PACK_ROOT)/bk7258_provision_store.c $(VOICE_OTA_MBEDCRYPTO) | $(BUILD)
	$(CC) $(VOICE_PACK_CFLAGS) -pthread -ffunction-sections -fdata-sections -DCONFIG_BK7258_PROVISION_GATT \
	    -I $(MOCKS) -I $(VOICE_PACK_ROOT) -I $(OPENVELA_ROOT)/external/unqlite/unqlite \
	    -I $(VOICE_OTA_MBEDTLS_SOURCE)/include $< $(VOICE_PACK_ROOT)/bk7258_preferences.c \
	    $(VOICE_PACK_ROOT)/bk7258_cloud_config.c $(VOICE_PACK_ROOT)/bk7258_provision_store.c \
	    -Wl,--gc-sections -Wl,--wrap=mkdir -Wl,--wrap=bkprov_store_open -Wl,--wrap=fsync \
	    -Wl,--wrap=lstat -Wl,--wrap=bkprov_store_check_filesystem $(VOICE_OTA_MBEDCRYPTO) -o $@
