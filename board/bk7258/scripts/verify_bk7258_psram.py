#!/usr/bin/env python3
"""Verify the retained source/config/SDK/ELF contract of BK7258 N14 PSRAM."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

class VerificationError(RuntimeError):
    """Raised when an N14 build gate is not satisfied."""


EXPECTED_LAYOUT = {
    "BK7258_PSRAM_BASE": 0x60000000,
    "BK7258_PSRAM_8M_SIZE": 0x00800000,
    "BK7258_PSRAM_16M_SIZE": 0x01000000,
    "BK7258_PSRAM_CP_HEAP_BASE": 0x60700000,
    "BK7258_PSRAM_CP_HEAP_SIZE": 0x00020000,
    "BK7258_PSRAM_AP_HEAP_BASE": 0x60720000,
    "BK7258_PSRAM_AP_HEAP_SIZE": 0x000A0000,
    "BK7258_PSRAM_AP_SECTION_BASE": 0x607C0000,
    "BK7258_PSRAM_AP_SECTION_SIZE": 0x00040000,
}

EXPECTED_IDS = {
    "BK7258_PSRAM_ID_APS6408L": 0x8D09,
    "BK7258_PSRAM_ID_APS128XXO": 0x8D08,
    "BK7258_PSRAM_ID_W955D8_1C": 0x1C8F,
    "BK7258_PSRAM_ID_W955D8_1F": 0x1F8F,
}

EXPECTED_CONFIG_VALUES = {
    "BK7258_PSRAM_CONFIG_APS6408L": 0x8D13,
    "BK7258_PSRAM_CONFIG_APS128XXO": 0x8D1A,
    "BK7258_PSRAM_W955D8_ID_ADDR": 0x01000000,
}

REQUIRED_COMMON_SYMBOLS = {
    "bk7258_psram_address",
    "bk7258_psram_free",
    "bk7258_psram_free_size",
    "bk7258_psram_heap_contains",
    "bk7258_psram_heap_test",
    "bk7258_psram_initialize",
    "bk7258_psram_malloc",
    "bk7258_psram_mpu_valid",
    "bk7258_psram_ready",
    "bk7258_psram_realloc",
    "bk7258_psram_zalloc",
}

REQUIRED_CP_SYMBOLS = {
    "bk7258_psram_boot_test",
    "bk7258_psram_early_initialize",
    "bk7258_psram_get_info",
    "bk7258_psram_minimum_free_size",
    "bk7258_psram_run_boot_test",
    "bk7258_psram_total_size",
    "bk7258_sdk_timer_selftest",
    "bk_printf_static_block",
    "bk_pm_module_vote_power_ctrl",
    "bk_pm_module_vote_psram_ctrl",
    "bk_psram_init",
    "bkpsramtest_main",
    "bktimertest_main",
    "psram_hal_cmd_read",
}

FORBIDDEN_AP_SYMBOLS = {
    "bk_printf_static_block",
    "bk_psram_init",
    "psram_hal_cmd_read",
}


def run(*args: str) -> str:
    result = subprocess.run(
        args,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode != 0:
        raise VerificationError(
            f"command failed ({result.returncode}): {' '.join(args)}\n"
            f"{result.stdout}{result.stderr}"
        )

    return result.stdout


def read_text(path: Path) -> str:
    if not path.is_file():
        raise VerificationError(f"required file is missing: {path}")

    return path.read_text(encoding="utf-8", errors="replace")


def require_tokens(text: str, tokens: list[str], description: str) -> None:
    for token in tokens:
        if token not in text:
            raise VerificationError(f"{description} is missing: {token}")


def parse_u32_macros(text: str, names: set[str]) -> dict[str, int]:
    values: dict[str, int] = {}
    for name in names:
        match = re.search(
            rf"^#define\s+{re.escape(name)}\s+(0x[0-9a-fA-F]+|[0-9]+)u?$",
            text,
            re.MULTILINE,
        )
        if match is None:
            raise VerificationError(f"numeric macro is missing: {name}")
        values[name] = int(match.group(1), 0)

    return values


def remove_config_lines(text: str, lines: list[str], description: str) -> str:
    for line in lines:
        exact = line + "\n"
        if text.count(exact) != 1:
            raise VerificationError(
                f"{description} must contain exactly one line: {line}"
            )
        text = text.replace(exact, "", 1)

    return text


def verify_profiles(board: Path) -> dict[str, object]:
    cp_n13 = read_text(board / "configs/cp_nsh_ble_gatt/defconfig")
    cp_n14 = read_text(board / "configs/cp_nsh_psram/defconfig")
    cp_psram_lines = [
        "CONFIG_BK7258_AP_AUTOSTART_TIMEOUT_MS=60000",
        "CONFIG_BK7258_PSRAM=y",
        "CONFIG_BK7258_PSRAM_BOOT_TEST=y",
        "CONFIG_BK7258_PSRAM_TEST=y",
        "CONFIG_BK7258_SDK_TIMER_SELFTEST=y",
    ]
    cp_normalized = remove_config_lines(
        cp_n14, cp_psram_lines, "N14 CP defconfig"
    )
    if cp_normalized != cp_n13:
        raise VerificationError(
            "CP PSRAM profile differs from N13 outside retained N14 gates"
        )

    ap_n13 = read_text(board / "configs/ap_smp_ble_gatt/defconfig")
    ap_n14 = read_text(board / "configs/ap_smp_psram/defconfig")
    ap_psram_lines = [
        "CONFIG_BK7258_PSRAM=y",
        "CONFIG_BK7258_PSRAM_TEST=y",
        "CONFIG_BK7258_PSRAM_TEST_ITERATIONS=16",
    ]
    ap_normalized = remove_config_lines(
        ap_n14, ap_psram_lines, "N14 AP defconfig"
    )
    ap_normalized = ap_normalized.replace(
        'CONFIG_DEVICE_LOCAL_NAME="BK7258-N14"',
        'CONFIG_DEVICE_LOCAL_NAME="BK7258-N13"',
    ).replace(
        'CONFIG_DEVICE_NAME="BK7258 N14"',
        'CONFIG_DEVICE_NAME="BK7258 N13"',
    )
    if ap_normalized != ap_n13:
        raise VerificationError(
            "N14 AP defconfig may differ from N13 only by PSRAM gates/name"
        )

    return {
        "cp_base": "cp_nsh_ble_gatt",
        "ap_base": "ap_smp_ble_gatt",
        "cp_added": cp_psram_lines,
        "cp_later_stage_added": cp_later_stage_lines,
        "ap_added": ap_psram_lines,
        "ap_name": "BK7258-N14",
    }


def verify_layout(board: Path) -> dict[str, object]:
    header = read_text(board / "chip/include/bk7258_psram.h")
    layout = parse_u32_macros(header, set(EXPECTED_LAYOUT))
    if layout != EXPECTED_LAYOUT:
        raise VerificationError(
            f"N14 PSRAM layout changed: expected {EXPECTED_LAYOUT}, got {layout}"
        )

    cp_begin = layout["BK7258_PSRAM_CP_HEAP_BASE"]
    cp_end = cp_begin + layout["BK7258_PSRAM_CP_HEAP_SIZE"]
    ap_begin = layout["BK7258_PSRAM_AP_HEAP_BASE"]
    ap_end = ap_begin + layout["BK7258_PSRAM_AP_HEAP_SIZE"]
    section_begin = layout["BK7258_PSRAM_AP_SECTION_BASE"]
    section_end = section_begin + layout["BK7258_PSRAM_AP_SECTION_SIZE"]
    lower_end = layout["BK7258_PSRAM_BASE"] + layout["BK7258_PSRAM_8M_SIZE"]
    physical_end = (
        layout["BK7258_PSRAM_BASE"] + layout["BK7258_PSRAM_16M_SIZE"]
    )
    if not (
        cp_begin == 0x60700000
        and cp_end == ap_begin
        and ap_end == section_begin
        and section_end == lower_end
        and lower_end == 0x60800000
        and physical_end == 0x61000000
    ):
        raise VerificationError("N14 PSRAM heap/section boundaries overlap or gap")

    return {
        "physical_window": [layout["BK7258_PSRAM_BASE"], physical_end],
        "sdk_abi_window": [layout["BK7258_PSRAM_BASE"], lower_end],
        "cp_heap": [cp_begin, cp_end],
        "ap_heap": [ap_begin, ap_end],
        "ap_section": [section_begin, section_end],
        "upper_8m_policy": "boot-tested-unallocated",
    }


def verify_source_contract(board: Path) -> dict[str, object]:
    source = read_text(board / "chip/common/bk7258_psram.c")
    ids = parse_u32_macros(source, set(EXPECTED_IDS))
    if ids != EXPECTED_IDS:
        raise VerificationError(
            f"N14 PSRAM device IDs changed: expected {EXPECTED_IDS}, got {ids}"
        )

    config_values = parse_u32_macros(source, set(EXPECTED_CONFIG_VALUES))
    if config_values != EXPECTED_CONFIG_VALUES:
        raise VerificationError(
            "N14 post-init PSRAM config signatures changed: "
            f"expected {EXPECTED_CONFIG_VALUES}, got {config_values}"
        )

    require_tokens(
        source,
        [
            "#define BK7258_MPU_PSRAM_REGION        6u",
            "#define BK7258_MPU_PSRAM_RBAR          0x60000002u",
            "#define BK7258_MPU_PSRAM_RLAR          0x63ffffe3u",
            "extern uint32_t psram_hal_cmd_read(uint32_t addr);",
            "int bk7258_psram_early_initialize(void)",
            "g_bk7258_psram_hardware_attempted = true;",
            "#  define BK7258_SDK_PM_PSRAM_AS_MEM 10",
            "#  define BK7258_SDK_PM_POWER_ON      0",
            "extern bk_err_t bk_pm_module_vote_psram_ctrl(int module, int power_state);",
            "bk_set_printf_enable(0);",
            "ret = bk_pm_module_vote_psram_ctrl(",
            "BK7258_SDK_PM_PSRAM_AS_MEM, BK7258_SDK_PM_POWER_ON",
            "bk_set_printf_enable(1);",
            "psram_hal_cmd_read(0) & 0xffffu",
            "case BK7258_PSRAM_CONFIG_APS128XXO:",
            "case BK7258_PSRAM_ID_APS128XXO:",
            "g_bk7258_psram_info.capacity = BK7258_PSRAM_16M_SIZE;",
            "ret = bk7258_psram_verify_16m_not_aliased();",
            "case BK7258_PSRAM_ID_APS6408L:",
            "case BK7258_PSRAM_CONFIG_APS6408L:",
            "g_bk7258_psram_info.capacity = BK7258_PSRAM_8M_SIZE;",
            "psram_hal_cmd_read(BK7258_PSRAM_W955D8_ID_ADDR) & 0xffffu",
            '__attribute__((noinline, noclone, used, section(".psram_boot_text")))',
            "#  define BK7258_PSRAM_BOOT_TEST_GATE 0x72580001u",
            "#  define BK7258_PSRAM_BOOT_TEST_GATE 0x72580000u",
            "g_bk7258_psram_boot_test_gate & 1u",
            "bk7258_psram_run_boot_test(uint32_t capacity)",
            "return bk7258_psram_boot_test(capacity);",
            "ret = bk7258_psram_run_boot_test(g_bk7258_psram_info.capacity);",
            "for (bit = 1u; bit != 0; bit <<= 1)",
            "for (offset = 1u; offset < words; offset <<= 1)",
            "base[offset] = 0xa5a50000u ^ offset;",
            "pattern = ~(0xa5a50000u ^ offset);",
            "base[offset] = 0;",
            "struct mm_heap_config_s config;",
            "config.start = (void *)(uintptr_t)BK7258_PSRAM_LOCAL_HEAP_BASE;",
            "config.size = BK7258_PSRAM_LOCAL_HEAP_SIZE;",
            "config.allocheap = true;",
            "mm_initialize_heap(&config, &g_bk7258_psram_heap);",
            "bk_psram_heap_init_flag_set(true)",
            "static spinlock_t g_bk7258_psram_lock = SP_UNLOCKED;",
            "The official",
            "BK7258 AP SMP heap instead serializes allocator metadata",
            "flags = spin_lock_irqsave(&g_bk7258_psram_lock);",
            "spin_unlock_irqrestore(&g_bk7258_psram_lock, flags);",
            "Match the official BK7258 AP psram_realloc() wrapper",
            "size = mm_malloc_size(g_bk7258_psram_heap, ptr);",
            "copy_size = bk7258_psram_allocation_size(ptr);",
            "replacement = bk7258_psram_malloc(size);",
            "if (copy_size > size)",
            "memcpy(replacement, ptr, copy_size);",
            "bk7258_psram_free(ptr);",
            "return g_bk7258_psram_info.heap_size;",
            "free_size = bk7258_psram_free_size();",
            "g_bk7258_psram_info.heap_size - free_size",
            "pthread_attr_setaffinity_np(",
            "pthread_join(threads[index], NULL)",
            "BK7258_PSRAM_TEST_STAGE_REALLOC_ENTER",
            "BK7258_PSRAM_TEST_STAGE_FREE_ENTER",
            "result->active_iteration[index]",
            "result->stage[index]",
            "result->free_before != result->free_after",
        ],
        "N14 PSRAM wrapper",
    )

    if "g_bk7258_psram_heap = mm_initialize(" in source:
        raise VerificationError(
            "PSRAM must not hold mm_heap_s: BK7258 exclusive stores do not complete"
        )

    early_start = source.index("int bk7258_psram_early_initialize(void)")
    early_end = source.index(
        "#endif /* !CONFIG_BK7258_AP_CORE */", early_start
    )
    early_source = source[early_start:early_end]
    if early_source.count("bk_pm_module_vote_psram_ctrl(") != 1:
        raise VerificationError(
            "CP hardware phase must take the official PSRAM PM vote once"
        )
    if early_source.index("bk_pm_module_vote_psram_ctrl(") > early_source.index(
        "bk7258_psram_run_boot_test("
    ):
        raise VerificationError("PSRAM boot test must follow the SDK PM vote")

    for script_name in ("ld.script", "ld_ap.script"):
        linker = read_text(board / "scripts" / script_name)
        require_tokens(
            linker,
            ["KEEP(*(.psram_boot_text))", "_etext = ABSOLUTE(.);"],
            f"N14 {script_name} PSRAM boot-test tail",
        )
        if linker.index("KEEP(*(.psram_boot_text))") > linker.index(
            "_etext = ABSOLUTE(.);"
        ):
            raise VerificationError(
                f"{script_name} must place PSRAM boot-test code before _etext"
            )

    init_start = source.index("int bk7258_psram_initialize(void)")
    init_end = source.index("bool bk7258_psram_ready(void)", init_start)
    init_source = source[init_start:init_end]
    role_if = init_source.index("#ifdef CONFIG_BK7258_AP_CORE")
    role_else = init_source.index("#else", role_if)
    role_end = init_source.index(
        "#endif /* CONFIG_BK7258_AP_CORE */", role_else
    )
    if ("bk_psram_init" in init_source[role_if:role_else] or
        "bk_pm_module_vote_psram_ctrl" in init_source[role_if:role_else]):
        raise VerificationError("AP branch must not initialize PSRAM hardware")
    if "bk_psram_init" in init_source[role_else:role_end]:
        raise VerificationError(
            "CP scheduled phase must not initialize PSRAM hardware"
        )
    if init_source[role_else:role_end].count(
        "bk7258_psram_early_initialize();"
    ) != 1:
        raise VerificationError("CP scheduled phase must retain early fallback")

    cp_start = read_text(board / "chip/cp/bk7258_start.c")
    if "bk7258_psram_early_initialize" in cp_start or \
       "bk7258_psram.h" in cp_start:
        raise VerificationError(
            "CP PSRAM hardware/test phase must not run before nx_start"
        )

    os_adapt = read_text(board / "chip/common/bk7258_os_adapt.c")
    require_tokens(
        os_adapt,
        [
            '#define BK7258_TIMER_SERVICE_NAME       "bk-sdk-timer"',
            "static int bk7258_timer_service(int argc, char *argv[])",
            "bk7258_timer_queue_locked(timer_apt, true);",
            "timer->handle = NULL;",
            "bk7258_timer_delete_locked(timer_apt);",
            "the queued delete entry owns the final free",
            "if (!timer_apt->queued)",
            "if (bk7258_psram_address(ptr))",
            "if (bk7258_psram_heap_contains(ptr))",
            "Refusing foreign PSRAM free",
            "return bk7258_psram_realloc(ptr, size);",
            "return bk7258_psram_malloc(size);",
            "return bk7258_psram_zalloc(size);",
            "bk7258_psram_used_size()",
            "return bk7258_psram_total_size();",
            "return bk7258_psram_free_size();",
            "void bk_printf_static_block(int level, char *tag, const char *fmt, ...)"
        ],
        "N14 SDK allocator/log wrapper",
    )

    timer_test = read_text(board / "chip/cp/bk7258_sdk_timer_selftest.c")
    require_tokens(
        timer_test,
        [
            "bk7258_sdk_timer_test_callback(void *arg)",
            "context->interrupt_context = rtos_is_in_interrupt_context();",
            "context->deinit_status = rtos_deinit_timer(&context->timer);",
            "BK7258_SDK_TIMER_TEST_LONG_CALLBACK_US",
            'phase = "queued-self-delete";',
            '" handle=0 long_callback_us=%u queued_delete=1\\n"',
            "nxsem_tickwait_uninterruptible(",
            "context.callback_pid == caller_pid",
            'printf("BTMR PASS iterations=%lu callbacks=%lu',
        ],
        "N14 SDK software-timer lifecycle regression",
    )

    ap_start = read_text(board / "chip/ap/bk7258_ap_start.c")
    require_tokens(
        ap_start,
        [
            "#  define BK7258_MPU_PSRAM_REGION 6u",
            "#  define BK7258_MPU_PSRAM_RBAR   0x60000002u",
            "#  define BK7258_MPU_PSRAM_RLAR   0x63ffffe3u",
            '"movs r1, %c[psram_region]\\n"',
            "bk7258_ap_smp_memory_initialize();",
        ],
        "N14 AP primary MPU startup",
    )
    ap_smp = read_text(board / "chip/ap/bk7258_ap_smp.c")
    if ap_smp.count("bk7258_ap_smp_memory_initialize();") != 1:
        raise VerificationError(
            "N14 AP secondary bootstrap must install the MPU contract once"
        )

    ap_main = read_text(board / "chip/ap/bk7258_ap_main.c")
    ap_init = ap_main.index("ret = bk7258_psram_initialize();")
    ap_test = ap_main.index("ret = bk7258_psram_heap_test(", ap_init)
    rptun_init = ap_main.index("ret = bk7258_rptun_initialize(", ap_test)
    bt_init = ap_main.index("ret = bk7258_bt_hci_initialize();", rptun_init)
    if not ap_init < ap_test < rptun_init < bt_init:
        raise VerificationError("N14 AP PSRAM gates must precede RPTUN/Bluetooth")
    require_tokens(
        ap_main,
        [
            "BK7258_PSRAM_AP_HEAP_READY",
            "BK7258_PSRAM_AP_RESULT_READY",
            "(uint32_t)(uintptr_t)&psram_test",
            "BK7258_PSRAM_AP_TEST_PASSED",
            "psram_test.observed_cpu[0] != 0u",
            "psram_test.observed_cpu[1] != 1u",
            "bk7258_ap_publish_failure(BK7258_AP_ERROR_PSRAM);",
        ],
        "N14 AP SMP heap gate",
    )

    bringup = read_text(board / "src/bk7258_bringup.c")
    require_tokens(
        bringup,
        [
            "Match the official CP startup order",
            "AP is still held in reset",
            "do not move this back to __start()",
            "if (apret >= 0 && psramret < 0)",
            "apret = psramret;",
        ],
        "N14 CP post-calibration PSRAM gate",
    )
    control_init = bringup.index("apret = bk7258_ap_control_initialize();")
    bt_ipc_init = bringup.index("apret = bk7258_bt_controller_ipc_initialize();")
    cp_init = bringup.index("psramret = bk7258_psram_initialize();")
    psram_gate = bringup.index("if (apret >= 0 && psramret < 0)", cp_init)
    supervisor_init = bringup.index("apret = bk7258_ap_supervisor_initialize();")
    ap_release = bringup.index("apret = bk7258_ap_start(")
    if not (
        control_init
        < bt_ipc_init
        < cp_init
        < psram_gate
        < supervisor_init
        < ap_release
    ):
        raise VerificationError(
            "N14 CP order must be AP control, calibration/BT IPC, PSRAM, "
            "supervisor, then AP release"
        )

    ap_control = read_text(board / "chip/cp/bk7258_ap_control.c")
    require_tokens(
        ap_control,
        [
            "if (!bk7258_psram_ready())",
            "return -ENODEV;",
            "#define BK7258_SDK_PM_POWER_MODULE_CPU1 17u",
            "#define BK7258_SDK_PM_POWER_ON          0u",
            "extern int bk_pm_module_vote_power_ctrl(unsigned int module,",
            "ret = bk_pm_module_vote_power_ctrl(",
            "BK7258_SDK_PM_POWER_MODULE_CPU1,",
            "BK7258_SDK_PM_POWER_ON);",
            "it also clears the",
            "CPU1 halt bit, restores its clock",
        ],
        "N14 AP release power/PSRAM gate",
    )
    ap_start = ap_control.index("static int bk7258_ap_start_locked(")
    ap_start_end = ap_control.index(
        "static int bk7258_ap_stop_locked(", ap_start
    )
    ap_start_source = ap_control[ap_start:ap_start_end]
    power_vote = ap_start_source.index("ret = bk_pm_module_vote_power_ctrl(")
    boot_address = ap_start_source.index(
        "sys_drv_set_cpu1_boot_address_offset(", power_vote
    )
    reset_release = ap_start_source.index(
        "sys_drv_set_cpu1_reset(1);", boot_address
    )
    if not power_vote < boot_address < reset_release:
        raise VerificationError(
            "official CPU1 PM vote must precede AP boot address/reset release"
        )

    amp = read_text(board / "chip/include/bk7258_amp.h")
    require_tokens(
        amp,
        [
            "#ifdef CONFIG_BK7258_PSRAM_TEST",
            "#  define BK7258_AP_DEFAULT_TIMEOUT_MS   60000u",
            "#  define BK7258_AP_DEFAULT_TIMEOUT_MS   15000u",
        ],
        "N14 AP lifecycle timeout budget",
    )

    test_app = read_text(
        board.parent.parent / "app/hello_app/bk7258_psram_test_main.c"
    )
    require_tokens(
        test_app,
        [
            '"  bkpsramtest info\\n"',
            '"  bkpsramtest heap [iterations=%u]\\n"',
            '"  bkpsramtest all  [iterations=%u]\\n"',
            '"The destructive raw-capacity test is boot-only.\\n"',
            '"BPSR APTEST status=%" PRId32',
            '" active=%" PRIu32 "/%" PRIu32',
            '" stage=%" PRIu32 "/%" PRIu32',
            "BK7258_PSRAM_AP_RESERVED_RESULT",
            "BK7258_PSRAM_AP_RESULT_READY",
            'printf("BPSR INFO PASS\\n");',
            'printf("BPSR HEAP PASS iterations=%" PRIu32 "\\n", iterations);',
        ],
        "N14 bkpsramtest",
    )

    build_script = read_text(board / "scripts/build_dual_image.sh")
    require_tokens(
        build_script,
        [
            "cp_nsh_psram",
            "ap_smp_psram",
            "N14 PSRAM configs must be selected as a pair",
            'if [[ "${CP_CONFIG_NAME}" == "cp_nsh_psram" ||',
            'verify_bk7258_psram.py"',
            'BK7258_SDK_SOURCE',
        ],
        "N14 dual-image build wrapper",
    )

    return {
        "hardware_owner": "cp-only",
        "allocator": "role-local-nuttx-mm",
        "cache_policy": "official-mpu-region-6-nonshareable-noncacheable",
        "capacity_ids": ids,
        "post_init_config_values": config_values,
        "boot_test": (
            "full-detected-capacity-post-calibration-before-role-heaps-and-ap"
        ),
        "ap_smp_test": "cpu0-cpu1-concurrent-before-rptun",
        "runtime_raw_test": "not-exposed",
    }


def parse_ram_regions(path: Path) -> tuple[str, dict[str, tuple[int, int]]]:
    text = read_text(path)
    capacity_match = re.search(r"^PSRAM_CAPCAITY_SIZE=(\S+)$", text, re.MULTILINE)
    if capacity_match is None:
        raise VerificationError(f"PSRAM capacity is missing from {path}")

    regions: dict[str, tuple[int, int]] = {}
    cursor = 0x60000000
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        columns = [column.strip() for column in line.split(",")]
        if len(columns) != 4 or columns[1] != "PSRAM":
            continue
        name, _, offset_text, size_text = columns
        if offset_text:
            cursor = int(offset_text, 0)
        size = int(size_text, 0)
        regions[name] = (cursor, size)
        cursor += size

    return capacity_match.group(1), regions


def verify_sdk_source(sdk: Path) -> dict[str, object]:
    cp_driver = read_text(sdk / "cp/middleware/driver/psram/psram_driver.c")
    ap_driver = read_text(sdk / "ap/middleware/driver/psram/psram_driver.c")
    cp_hal = read_text(sdk / "cp/middleware/soc/common/hal/psram_hal.c")
    cp_pm_header = read_text(sdk / "cp/include/modules/pm.h")
    cp_pwr_clk_header = read_text(sdk / "cp/include/driver/pwr_clk.h")
    cp_pm_driver = read_text(
        sdk / "cp/middleware/driver/pwr_clk/pwr_clk.c"
    )
    cp_smp_startup = read_text(
        sdk
        / "cp/components/cmsis/CMSIS_5/Device/Beken/bk7236xx/Source/smp/startup_cpu1.c"
    )
    ap_mem_wrapper = read_text(sdk / "ap/components/bk_rtos/freertos/mem_arch.c")
    ap_heap = read_text(
        sdk
        / "ap/components/os_source/freertos_smp_v2p0/FreeRTOS-Kernel/portable/MemMang/heap_4.c"
    )

    require_tokens(
        cp_driver,
        [
            "bk_err_t bk_psram_init(void)",
            "if (!rtos_is_scheduler_started())",
            "is_skip_mutex = 1;",
            "goto start_init;",
            "actual_id = psram_hal_config_init(chip_id);",
        ],
        "official CP PSRAM driver",
    )
    ap_init_start = ap_driver.index("bk_err_t bk_psram_init(void)")
    ap_init_end = ap_driver.index("bk_err_t bk_psram_deinit(void)", ap_init_start)
    ap_init = ap_driver[ap_init_start:ap_init_end]
    if "psram only init in cp" not in ap_init or "return BK_FAIL;" not in ap_init:
        raise VerificationError("official AP PSRAM init ownership changed")
    if "psram_hal_cmd_read(0x00000000)" not in cp_hal:
        raise VerificationError("official CP PSRAM ID read convention changed")
    require_tokens(
        cp_hal,
        [
            "val = (val & ~(0x1F)) | (0x4 << 2) | 0x3;",
            "val = (val & ~(0x1F)) | (0x6 << 2) | 0x2;",
            "psram_hal_cmd_write(0x01000000, 0x1c8f);",
        ],
        "official post-detect PSRAM configuration",
    )
    require_tokens(
        cp_pm_header,
        [
            "PM_POWER_MODULE_STATE_ON = 0,",
            "PM_POWER_MODULE_NAME_CPU1",
            "//17",
            "bk_err_t bk_pm_module_vote_power_ctrl(",
        ],
        "official PM power-control ABI",
    )
    require_tokens(
        cp_pwr_clk_header,
        [
            "PM_POWER_PSRAM_MODULE_NAME_AS_MEM       ,// 10",
            "bk_err_t bk_pm_module_vote_psram_ctrl(",
            "pm_power_psram_module_name_e module,pm_power_module_state_e power_state);",
        ],
        "official PSRAM PM vote ABI",
    )
    require_tokens(
        cp_pm_driver,
        [
            "bk_pm_module_vote_power_ctrl(PM_POWER_MODULE_NAME_CPU1, PM_POWER_MODULE_STATE_ON);",
            "start_cpu1_core();",
        ],
        "official CPU1 PM boot sequence",
    )
    require_tokens(
        cp_smp_startup,
        [
            "bk_pm_module_vote_power_ctrl(PM_POWER_MODULE_NAME_CPU1, PM_POWER_MODULE_STATE_ON);",
            "reset_cpu1_core((uint32_t)&__vector_core1_table, 1);",
        ],
        "official CPU1 SMP release sequence",
    )
    require_tokens(
        ap_mem_wrapper,
        [
            "void *psram_realloc(void *ptr, size_t size)",
            "tmp = psram_malloc(size);",
            "os_memcpy_word((uint32_t *)tmp, (uint32_t *)ptr, size);",
            "os_free((void *)ptr);",
        ],
        "official AP PSRAM realloc wrapper",
    )
    require_tokens(
        ap_heap,
        [
            "static SPINLOCK_SECTION spinlock_t s_spinlock_heap = SPIN_LOCK_ACQUIRE_INIT;",
            "#define HeapEnterCritical() {vPortEnterCritical(&s_spinlock_heap);}",
            "#define HeapExitCritical() {vPortExitCritical(&s_spinlock_heap);}",
        ],
        "official AP SMP heap serialization",
    )

    app_csv = sdk / "projects/app/partitions/bk7258/ram_regions.csv"
    capacity, regions = parse_ram_regions(app_csv)
    expected = {
        "CP_PSRAM_HEAP": (0x60700000, 0x00020000),
        "AP_PSRAM_HEAP": (0x60720000, 0x000A0000),
        "AP_PSRAM_SECTION": (0x607C0000, 0x00040000),
    }
    if capacity != "8M" or any(regions.get(name) != value for name, value in expected.items()):
        raise VerificationError("official projects/app 8 MiB PSRAM ABI changed")

    templates_16m: list[str] = []
    for candidate in sorted(
        (sdk / "projects").glob("**/partitions/bk7258/ram_regions.csv")
    ):
        candidate_capacity, candidate_regions = parse_ram_regions(candidate)
        if candidate_capacity != "16M":
            continue
        final_end = max(
            (base + size for base, size in candidate_regions.values()),
            default=0,
        )
        if final_end == 0x61000000:
            templates_16m.append(str(candidate.relative_to(sdk)))

    if not templates_16m:
        raise VerificationError("official SDK has no complete 16 MiB layout template")

    return {
        "root": str(sdk.resolve()),
        "cp_init": "real-hardware-owner",
        "ap_init": "returns-BK_FAIL",
        "default_layout": str(app_csv.relative_to(sdk)),
        "default_capacity": capacity,
        "default_regions": expected,
        "complete_16m_templates": templates_16m,
        "ap_psram_realloc": "allocate-copy-free",
        "ap_smp_heap_lock": "internal-spinlock-critical-section",
    }


def parse_symbols(nm: str, elf: Path) -> set[str]:
    symbols: set[str] = set()
    for line in run(nm, "--defined-only", str(elf)).splitlines():
        match = re.match(r"^[0-9a-fA-F]+\s+\S\s+(\S+)$", line)
        if match:
            symbols.add(match.group(1))

    return symbols


def parse_symbol_addresses(nm: str, elf: Path) -> dict[str, int]:
    symbols: dict[str, int] = {}
    for line in run(nm, "--defined-only", str(elf)).splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+\S\s+(\S+)$", line)
        if match:
            symbols[match.group(2)] = int(match.group(1), 16)

    return symbols


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def map_bundle(map_text: str, role: str) -> str:
    matches = set(
        re.findall(rf"/versions/([^/]+)/{role}/libs/", map_text)
    )
    if len(matches) != 1:
        raise VerificationError(
            f"{role.upper()} map must reference exactly one SDK bundle: {matches}"
        )
    return matches.pop()


def verify_elf(
    cp_elf: Path,
    ap_elf: Path,
    cp_map: Path,
    ap_map: Path,
    nm: str,
    expected_bundle: str | None,
) -> dict[str, object]:
    for artifact in (cp_elf, ap_elf, cp_map, ap_map):
        if not artifact.is_file():
            raise VerificationError(f"build artifact is missing: {artifact}")

    cp_symbols = parse_symbols(nm, cp_elf)
    ap_symbols = parse_symbols(nm, ap_elf)
    cp_symbol_addresses = parse_symbol_addresses(nm, cp_elf)
    missing_cp = sorted((REQUIRED_COMMON_SYMBOLS | REQUIRED_CP_SYMBOLS) - cp_symbols)
    missing_ap = sorted(REQUIRED_COMMON_SYMBOLS - ap_symbols)
    if missing_cp:
        raise VerificationError(
            "N14 CP ELF is missing symbols: " + ", ".join(missing_cp)
        )
    if missing_ap:
        raise VerificationError(
            "N14 AP ELF is missing symbols: " + ", ".join(missing_ap)
        )
    leaked = sorted(FORBIDDEN_AP_SYMBOLS & ap_symbols)
    if leaked:
        raise VerificationError(
            "N14 CP-only hardware symbols leaked into AP ELF: " + ", ".join(leaked)
        )

    cp_map_text = read_text(cp_map)
    ap_map_text = read_text(ap_map)
    for role, map_text in (("CP", cp_map_text), ("AP", ap_map_text)):
        if "bk7258_psram.o" not in map_text:
            raise VerificationError(f"N14 {role} map lacks bk7258_psram.o")
    require_tokens(
        cp_map_text,
        [
            "libcommon.a(psram_hal.c.obj)",
            "libdriver.a(psram_driver.c.obj)",
            ".text.bk_psram_init",
            ".text.psram_hal_cmd_read",
            "*(.psram_boot_text)",
            ".psram_boot_text",
        ],
        "N14 CP link map",
    )

    tail = re.search(
        r"\n \.psram_boot_text\s*\n\s+"
        r"0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+",
        cp_map_text,
    )
    etext = re.search(
        r"\n\s+0x([0-9a-fA-F]+)\s+_etext = ABSOLUTE \(\.\)",
        cp_map_text,
    )
    if tail is None or etext is None:
        raise VerificationError("N14 CP map lacks a measurable PSRAM boot-test tail")
    tail_start = int(tail.group(1), 16)
    tail_end = tail_start + int(tail.group(2), 16)
    etext_address = int(etext.group(1), 16)
    if tail_end > etext_address or etext_address - tail_end > 7:
        raise VerificationError("PSRAM boot-test code is not the final CP .text input")
    for symbol in ("bk7258_psram_boot_test", "bk7258_psram_run_boot_test"):
        address = cp_symbol_addresses[symbol]
        if not tail_start <= address < tail_end:
            raise VerificationError(f"{symbol} escaped the PSRAM boot-test tail")

    cp_bundle = map_bundle(cp_map_text, "cp")
    ap_bundle = map_bundle(ap_map_text, "ap")
    if cp_bundle != ap_bundle:
        raise VerificationError(
            f"CP/AP SDK bundle mismatch: CP={cp_bundle}, AP={ap_bundle}"
        )
    if expected_bundle is not None and cp_bundle != expected_bundle:
        raise VerificationError(
            f"expected SDK bundle {expected_bundle}, linked {cp_bundle}"
        )

    cp_bytes = cp_elf.read_bytes()
    ap_bytes = ap_elf.read_bytes()
    for value in (
        b"BPSR BOOT PASS",
        b"BPSR INFO PASS",
        b"BPSR HEAP PASS",
        b"bk7258-cp-psram",
    ):
        if value not in cp_bytes:
            raise VerificationError(f"N14 CP ELF is missing payload {value!r}")
    for value in (b"BK7258 N14", b"BK7258-N14", b"bk7258-ap-psram"):
        if value not in ap_bytes:
            raise VerificationError(f"N14 AP ELF is missing payload {value!r}")

    return {
        "bundle": cp_bundle,
        "cp": {
            "path": str(cp_elf.resolve()),
            "size": cp_elf.stat().st_size,
            "sha256": sha256(cp_elf),
            "required_symbols": sorted(REQUIRED_COMMON_SYMBOLS | REQUIRED_CP_SYMBOLS),
        },
        "ap": {
            "path": str(ap_elf.resolve()),
            "size": ap_elf.stat().st_size,
            "sha256": sha256(ap_elf),
            "required_symbols": sorted(REQUIRED_COMMON_SYMBOLS),
            "forbidden_symbols_present": leaked,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cp-elf", required=True, type=Path)
    parser.add_argument("--ap-elf", required=True, type=Path)
    parser.add_argument("--cp-map", required=True, type=Path)
    parser.add_argument("--ap-map", required=True, type=Path)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--expected-bundle")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    args = parser.parse_args()

    board = Path(__file__).resolve().parent.parent
    result: dict[str, object] = {
        "format": 1,
        "profiles": verify_profiles(board),
        "layout": verify_layout(board),
        "source": verify_source_contract(board),
        "sdk_source": (
            verify_sdk_source(args.sdk_source)
            if args.sdk_source is not None
            else {"status": "not-requested"}
        ),
        "elf": verify_elf(
            args.cp_elf,
            args.ap_elf,
            args.cp_map,
            args.ap_map,
            args.nm,
            args.expected_bundle,
        ),
    }

    if args.json is not None:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    upper_policy = result["layout"]["upper_8m_policy"]
    print(
        "PASS bk7258-psram: "
        f"bundle={result['elf']['bundle']} owner=cp-only capacity=8m-or-16m "
        f"sdk-abi=lower-8m upper-8m={upper_policy} ap-smp=cpu0+cpu1"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except VerificationError as error:
        print(f"FAIL bk7258-psram: {error}")
        raise SystemExit(1) from error
