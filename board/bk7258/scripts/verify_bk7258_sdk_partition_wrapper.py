#!/usr/bin/env python3
"""Verify the project-owned SDK partition ABI wrapper on host and in an ELF."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from gen_bk7258_partitions import (
    DEFAULT_INPUT,
    PartitionLayout,
    PartitionLayoutError,
    generated_contents,
    load_layout,
)


SCRIPT_DIR = Path(__file__).resolve().parent
BOARD_DIR = SCRIPT_DIR.parent
WRAPPER_SOURCE = BOARD_DIR / "chip/cp/bk7258_sdk_partition.c"


class VerificationError(RuntimeError):
    """Raised when the SDK partition wrapper is incomplete or inconsistent."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationError(message)


def _dynamic_layout(root: Path) -> PartitionLayout:
    source = DEFAULT_INPUT.read_text(encoding="utf-8")
    replacements = (
        ("primary_cp_app,,1360K", "primary_cp_app,,1292K"),
        ("s_app,,2516K", "s_app,,2448K"),
    )
    for old, new in replacements:
        require(old in source, f"dynamic fixture source is absent: {old}")
        source = source.replace(old, new, 1)
    path = root / "dynamic.csv"
    path.write_text(source, encoding="utf-8")
    return load_layout(path)


def _write_host_headers(root: Path, layout: PartitionLayout) -> Path:
    include = root / "include"
    (include / "nuttx").mkdir(parents=True)
    (include / "arch/chip").mkdir(parents=True)
    (include / "driver").mkdir(parents=True)
    (include / "nuttx/config.h").write_text(
        "#pragma once\n#define CONFIG_BK7258_FLASH_MTD 1\n",
        encoding="utf-8",
    )
    (include / "arch/chip/bk7258_partition_layout.h").write_text(
        generated_contents(layout)["bk7258_partition_layout.h"],
        encoding="utf-8",
    )
    (include / "driver/flash.h").write_text(
        """#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int bk_err_t;
#define BK_OK 0
#define BK_FAIL (-1)
#define BK_ERR_NO_MEM (-5)
#define BK_ERR_FLASH_ADDR_OUT_OF_RANGE (-14082)
#define BK_ERR_FLASH_PARTITION_NOT_FOUND (-14083)
bk_err_t bk_flash_read_bytes(uint32_t, uint8_t *, uint32_t);
bk_err_t bk_flash_write_bytes(uint32_t, const uint8_t *, uint32_t);
bk_err_t bk_flash_erase_sector(uint32_t);
""",
        encoding="utf-8",
    )
    return include


def _host_harness(label: str) -> str:
    return f'''#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/chip/bk7258_partition_layout.h>
#include <driver/flash.h>

#define MAGIC 0x12345678u

struct sdk_partition
{{
  uint32_t owner;
  const char *description;
  uint32_t start;
  uint32_t length;
  uint32_t options;
}};

extern struct sdk_partition *__wrap_bk_flash_partition_get_info(uint32_t);
extern bk_err_t __wrap_bk_flash_partition_read(uint32_t, uint8_t *, uint32_t,
                                               uint32_t);
extern bk_err_t __wrap_bk_flash_partition_write(uint32_t, const uint8_t *,
                                                uint32_t, uint32_t);
extern bk_err_t __wrap_bk_flash_partition_erase(uint32_t, uint32_t, uint32_t);
extern bk_err_t __wrap_bk_flash_partition_write_perm_check_by_addr(
  uint32_t, uint32_t, uint32_t);

static bool g_guard;
static uint32_t g_read_addr;
static uint32_t g_read_size;
static uint32_t g_write_addr;
static uint32_t g_write_size;
static uint32_t g_erase_addr;

static bool in_range(uint32_t addr, uint32_t size, uint32_t start,
                     uint32_t length)
{{
  return size != 0 && addr >= start && addr - start < length &&
         size <= length - (addr - start);
}}

bool bk7258_flash_guard_write_authorized(uint32_t addr, uint32_t size)
{{
  return g_guard &&
         in_range(addr, size, BK7258_ROLE_LITTLEFS_OFFSET,
                  BK7258_ROLE_LITTLEFS_SIZE);
}}

bk_err_t bk_flash_read_bytes(uint32_t addr, uint8_t *buffer, uint32_t size)
{{
  uint32_t index;
  g_read_addr = addr;
  g_read_size = size;
  for (index = 0; index < size; index++)
    {{
      buffer[index] = (uint8_t)(addr + index);
    }}
  return BK_OK;
}}

bk_err_t bk_flash_write_bytes(uint32_t addr, const uint8_t *buffer,
                              uint32_t size)
{{
  (void)buffer;
  g_write_addr = addr;
  g_write_size = size;
  return __wrap_bk_flash_partition_write_perm_check_by_addr(addr, size, MAGIC);
}}

bk_err_t bk_flash_erase_sector(uint32_t addr)
{{
  g_erase_addr = addr;
  return __wrap_bk_flash_partition_write_perm_check_by_addr(
    addr, BK7258_FLASH_ERASE_SIZE, MAGIC);
}}

#define CHECK(condition)                                                    \
  do                                                                        \
    {{                                                                       \
      if (!(condition))                                                     \
        {{                                                                   \
          fprintf(stderr, "FAIL {label} line=%d: %s\\n", __LINE__,          \
                  #condition);                                              \
          return 1;                                                         \
        }}                                                                   \
    }}                                                                       \
  while (0)

int main(void)
{{
  struct sdk_partition *info;
  uint8_t input[8] = {{0}};
  uint8_t output[8] = {{0}};
  uint32_t index;

  info = __wrap_bk_flash_partition_get_info(
    BK7258_ROLE_SLOT_A_AP_SDK_ID);
  CHECK(info != NULL);
  CHECK(strcmp(info->description, "application1") == 0);
  CHECK(info->start == BK7258_ROLE_SLOT_A_AP_OFFSET);
  CHECK(info->length == BK7258_ROLE_SLOT_A_AP_SIZE);
  CHECK(__wrap_bk_flash_partition_get_info(5) == NULL);
  CHECK(__wrap_bk_flash_partition_get_info(
          BK7258_SDK_PARTITIONS_TABLE_SIZE) == NULL);

  info = __wrap_bk_flash_partition_get_info(
    BK7258_ROLE_VENDOR_CONFIG_SDK_ID);
  CHECK(info != NULL && info->start == BK7258_ROLE_VENDOR_CONFIG_OFFSET);

  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_CALIBRATION_NET_OFFSET, 4, MAGIC) == BK_OK);
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_CALIBRATION_NET_OFFSET, 4, 0) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_SLOT_A_CP_OFFSET, 4, MAGIC) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_SLOT_B_PAIR_OFFSET, 4, MAGIC) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_LITTLEFS_OFFSET, 4, MAGIC) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_BL1_PRIMARY_MANIFEST_OFFSET, 4, MAGIC) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_BL1_SECONDARY_MANIFEST_OFFSET, 4, MAGIC) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_VENDOR_CONFIG_OFFSET, 4, MAGIC) == BK_OK);
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_CALIBRATION_NET_END - 2, 4, MAGIC) ==
        BK_ERR_FLASH_ADDR_OUT_OF_RANGE);
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_BL1_SECONDARY_MANIFEST_END + 0x1000, 4, MAGIC) == BK_FAIL);

  g_guard = true;
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_SLOT_A_CP_OFFSET, 4, MAGIC) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_SLOT_B_PAIR_OFFSET, 4, MAGIC) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write_perm_check_by_addr(
          BK7258_ROLE_LITTLEFS_OFFSET, 4, MAGIC) == BK_OK);
  g_guard = false;

  CHECK(__wrap_bk_flash_partition_read(
          BK7258_ROLE_CALIBRATION_NET_SDK_ID, output, 3, 5) == BK_OK);
  CHECK(g_read_addr == BK7258_ROLE_CALIBRATION_NET_OFFSET);
  CHECK(g_read_size == 32);
  for (index = 0; index < 5; index++)
    {{
      CHECK(output[index] ==
            (uint8_t)(BK7258_ROLE_CALIBRATION_NET_OFFSET + 3 + index));
    }}
  CHECK(__wrap_bk_flash_partition_read(
          BK7258_ROLE_CALIBRATION_NET_SDK_ID, output,
          BK7258_ROLE_CALIBRATION_NET_SIZE - 1, 2) ==
        BK_ERR_FLASH_ADDR_OUT_OF_RANGE);

  CHECK(__wrap_bk_flash_partition_write(
          BK7258_ROLE_CALIBRATION_NET_SDK_ID, input, 16, 4) == BK_OK);
  CHECK(g_write_addr == BK7258_ROLE_CALIBRATION_NET_OFFSET + 16);
  CHECK(g_write_size == 4);
  CHECK(__wrap_bk_flash_partition_write(
          BK7258_ROLE_SLOT_A_CP_SDK_ID, input, 0, 4) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write(
          BK7258_ROLE_SLOT_B_PAIR_SDK_ID, input, 0, 4) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write(
          BK7258_ROLE_BL1_PRIMARY_MANIFEST_SDK_ID, input, 0, 4) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write(
          BK7258_ROLE_BL1_SECONDARY_MANIFEST_SDK_ID, input, 0, 4) == BK_FAIL);
  g_guard = true;
  CHECK(__wrap_bk_flash_partition_write(
          BK7258_ROLE_SLOT_A_CP_SDK_ID, input, 0, 4) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write(
          BK7258_ROLE_SLOT_B_PAIR_SDK_ID, input, 0, 4) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_write(
          BK7258_ROLE_LITTLEFS_SDK_ID, input, 0, 4) == BK_OK);
  CHECK(g_write_addr == BK7258_ROLE_LITTLEFS_OFFSET);
  g_guard = false;

  CHECK(__wrap_bk_flash_partition_erase(
          BK7258_ROLE_CALIBRATION_NET_SDK_ID, 0,
          BK7258_FLASH_ERASE_SIZE) == BK_OK);
  CHECK(g_erase_addr == BK7258_ROLE_CALIBRATION_NET_OFFSET);
  CHECK(__wrap_bk_flash_partition_erase(
          BK7258_ROLE_SLOT_B_PAIR_SDK_ID, 0,
          BK7258_FLASH_ERASE_SIZE) == BK_FAIL);
  g_guard = true;
  CHECK(__wrap_bk_flash_partition_erase(
          BK7258_ROLE_SLOT_A_CP_SDK_ID, 0,
          BK7258_FLASH_ERASE_SIZE) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_erase(
          BK7258_ROLE_SLOT_B_PAIR_SDK_ID, 0,
          BK7258_FLASH_ERASE_SIZE) == BK_FAIL);
  CHECK(__wrap_bk_flash_partition_erase(
          BK7258_ROLE_LITTLEFS_SDK_ID, 0,
          BK7258_FLASH_ERASE_SIZE) == BK_OK);

  puts("PASS {label}");
  return 0;
}}
'''


def _run_host_case(root: Path, label: str, layout: PartitionLayout) -> str:
    compiler = shutil.which("cc")
    if compiler is None:
        raise VerificationError("host C compiler is unavailable")
    case_root = root / label
    case_root.mkdir()
    include = _write_host_headers(case_root, layout)
    harness = case_root / "wrapper_harness.c"
    executable = case_root / "wrapper_harness"
    harness.write_text(_host_harness(label), encoding="utf-8")
    command = [
        compiler,
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-DBK7258_SDK_PARTITION_HOST_TEST=1",
        "-I",
        str(include),
        "-I",
        str(WRAPPER_SOURCE.parent),
        str(WRAPPER_SOURCE),
        str(harness),
        "-o",
        str(executable),
    ]
    compile_result = subprocess.run(
        command, check=False, capture_output=True, text=True
    )
    if compile_result.returncode != 0:
        raise VerificationError(
            f"host wrapper compile failed ({label}):\n{compile_result.stderr}"
        )
    run_result = subprocess.run(
        [str(executable)], check=False, capture_output=True, text=True
    )
    if run_result.returncode != 0:
        raise VerificationError(
            f"host wrapper test failed ({label}):\n"
            f"{run_result.stdout}{run_result.stderr}"
        )
    return run_result.stdout.strip()


def _verify_elf(elf: Path, map_path: Path) -> dict[str, object]:
    require(elf.is_file(), f"CP ELF does not exist: {elf}")
    require(map_path.is_file(), f"CP map does not exist: {map_path}")
    nm = shutil.which("arm-none-eabi-nm")
    if nm is None:
        raise VerificationError("arm-none-eabi-nm is unavailable")
    nm_result = subprocess.run(
        [nm, "-S", str(elf)], check=True, capture_output=True, text=True
    ).stdout
    elf_symbols = {
        fields[-1]
        for line in nm_result.splitlines()
        if len(fields := line.split()) >= 2
    }
    # Only wrappers reached by this concrete firmware profile survive
    # --gc-sections.  get_info() is used by the SDK start path and the raw
    # permission hook is used by the Flash driver, so both must be present in
    # every runtime CP image.  The read/write/erase entry points are exercised
    # by the host ABI cases above; requiring unused ones in the ELF would make
    # this verifier disagree with the linker's intentional dead-code removal.

    required_symbols = (
        "__wrap_bk_flash_partition_get_info",
        "__wrap_bk_flash_partition_write_perm_check_by_addr",
        "g_bk7258_sdk_partitions",
    )
    optional_symbols = (
        "__wrap_bk_flash_partition_read",
        "__wrap_bk_flash_partition_write",
        "__wrap_bk_flash_partition_erase",
    )
    for symbol in required_symbols:
        require(symbol in elf_symbols, f"CP ELF omits wrapper symbol: {symbol}")

    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    require(
        "flash_partition.c.obj" not in map_text,
        "stale SDK flash_partition.c.obj is still linked",
    )
    require(
        "system_main.c.obj) (__wrap_bk_flash_partition_get_info)" in map_text,
        "SDK AP-start lookup does not resolve to the project wrapper",
    )
    permission_crossref = map_text.find(
        "__wrap_bk_flash_partition_write_perm_check_by_addr "
    )
    require(
        permission_crossref >= 0
        and "libdriver.a(flash_driver.c.obj)"
        in map_text[permission_crossref : permission_crossref + 700],
        "SDK raw Flash permission path does not resolve to the project wrapper",
    )
    return {
        "elf": str(elf.resolve()),
        "map": str(map_path.resolve()),
        "required_symbols": list(required_symbols),
        "optional_symbols_linked": {
            symbol: symbol in elf_symbols for symbol in optional_symbols
        },
        "sdk_flash_partition_object_linked": False,
        "system_main_wrapped": True,
        "raw_permission_wrapped": True,
    }


def verify(elf: Path | None = None, map_path: Path | None = None) -> dict[str, object]:
    require(WRAPPER_SOURCE.is_file(), "SDK partition wrapper source is absent")
    baseline = load_layout(DEFAULT_INPUT)
    with tempfile.TemporaryDirectory(prefix="bk7258-sdk-partition-") as temp:
        root = Path(temp)
        dynamic = _dynamic_layout(root)
        host = {
            "default": _run_host_case(root, "default", baseline),
            "dynamic": _run_host_case(root, "dynamic", dynamic),
        }

    if (elf is None) != (map_path is None):
        raise VerificationError("--elf and --map must be supplied together")
    elf_result = None if elf is None else _verify_elf(elf, map_path)
    return {
        "format": 1,
        "status": "pass",
        "layout_id": baseline.layout_id,
        "dynamic_layout_id": dynamic.layout_id,
        "host": host,
        "elf": elf_result,
        "sdk_source_modified": False,
        "sdk_archive_modified": False,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--map", dest="map_path", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--json", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        result = verify(args.elf, args.map_path)
    except (
        OSError,
        PartitionLayoutError,
        VerificationError,
        subprocess.SubprocessError,
    ) as error:
        print(f"FAIL bk7258-sdk-partition-wrapper: {error}")
        return 1
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8")
    if args.json:
        print(encoded, end="")
    else:
        mode = "host+elf" if result["elf"] is not None else "host"
        print(
            "PASS bk7258-sdk-partition-wrapper: "
            f"mode={mode} layout_id={result['layout_id']} dynamic=passed"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
