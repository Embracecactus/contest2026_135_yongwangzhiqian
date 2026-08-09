#!/usr/bin/env python3
"""Fail-closed verify the BK7258 N15 SRAM Flash-engine closure.

The verifier inspects a linked Tier-1 ELF, its load image, disassembly, map,
stack-usage evidence, the repository ABI, and (when supplied) the exact
official Beken v3.1.1.9 Flash sources.  It never connects to or writes a board.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import bk7258_ota_metadata as metadata


SDK_RELEASE = "v3.1.1.9"
FLASH_VMA_START = 0x02000000
FLASH_VMA_END = 0x02010000
SRAM_VMA_START = 0x28000000
SRAM_VMA_END = 0x28002000
MAX_CLOSURE_STACK = 512

REQUIRED_SRAM_SYMBOLS = {
    "boot_ota_sram_wdt_feed",
    "boot_ota_sram_fail_reset",
    "boot_ota_sram_wait_idle",
    "boot_ota_sram_trigger",
    "boot_ota_sram_prepare_controller",
    "boot_ota_sram_read_id",
    "boot_ota_sram_read_status",
    "boot_ota_sram_write_status",
    "boot_ota_sram_unprotect",
    "boot_ota_sram_read_chunk",
    "boot_ota_sram_program_chunk",
    "boot_ota_sram_erase_sector",
    "boot_ota_sram_copy_sector",
    "boot_ota_sram_erase_journal",
    "boot_ota_sram_program_journal",
    "boot_ota_sram_restore_protection",
    "boot_ota_sram_hold_secondaries",
    "boot_ota_sram_check_environment",
    "boot_ota_sram_entry",
    "g_bk7258_ota_write_gate",
}


class VerificationError(RuntimeError):
    """Raised when the closure is not completely proven."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise VerificationError(message)


def run(command: list[str]) -> str:
    try:
        result = subprocess.run(
            command,
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        output = getattr(error, "stdout", "")
        raise VerificationError(
            f"command failed: {' '.join(command)}\n{output}"
        ) from error
    return result.stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


@dataclass(frozen=True)
class Section:
    name: str
    size: int
    vma: int
    lma: int
    flags: str


@dataclass(frozen=True)
class Symbol:
    name: str
    address: int
    size: int
    kind: str


def parse_sections(output: str) -> dict[str, Section]:
    lines = output.splitlines()
    sections: dict[str, Section] = {}
    pattern = re.compile(
        r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+" r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"
    )
    for index, line in enumerate(lines):
        match = pattern.match(line)
        if not match:
            continue
        flags = lines[index + 1].strip() if index + 1 < len(lines) else ""
        name, size, vma, lma = match.groups()
        sections[name] = Section(
            name=name,
            size=int(size, 16),
            vma=int(vma, 16),
            lma=int(lma, 16),
            flags=flags,
        )
    return sections


def parse_symbols(output: str) -> dict[str, Symbol]:
    symbols: dict[str, Symbol] = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) == 4:
            address, size, kind, name = fields
        elif len(fields) == 3:
            address, kind, name = fields
            size = "0"
        else:
            continue
        if not re.fullmatch(r"[0-9a-fA-F]+", address):
            continue
        symbols[name] = Symbol(name, int(address, 16), int(size, 16), kind)
    return symbols


def read_section(elf: Path, objcopy: str, name: str) -> bytes:
    with tempfile.TemporaryDirectory(prefix="bk7258-ota-sram-") as directory:
        output = Path(directory) / "section.bin"
        run([objcopy, "--dump-section", f"{name}={output}", str(elf)])
        return output.read_bytes()


def parse_stack_usage(paths: list[Path]) -> dict[str, int]:
    usage: dict[str, int] = {}
    for path in paths:
        require(path.is_file(), f"missing stack-usage evidence: {path}")
        for line in path.read_text(encoding="utf-8").splitlines():
            fields = line.rsplit("\t", 2)
            require(len(fields) == 3, f"malformed stack-usage line: {line}")
            location, size, kind = fields
            name = location.rsplit(":", 1)[-1]
            require(kind == "static", f"dynamic stack use in {name}: {kind}")
            usage[name] = int(size)
    return usage


def parse_call_graph(disassembly: str) -> dict[str, set[str]]:
    graph: dict[str, set[str]] = {}
    current: str | None = None
    symbol_line = re.compile(r"^[0-9a-fA-F]+\s+<([^>]+)>:$")
    call_line = re.compile(
        r"\bblx?(?:\.[a-z]+)?\s+[0-9a-fA-F]+\s+<([^>+]+)(?:\+[^>]*)?>"
    )
    for raw in disassembly.splitlines():
        line = raw.strip()
        symbol_match = symbol_line.match(line)
        if symbol_match:
            current = symbol_match.group(1)
            graph.setdefault(current, set())
            continue
        if current is None:
            continue
        call_match = call_line.search(line)
        if call_match:
            graph[current].add(call_match.group(1))
    return graph


def stack_upper_bound(
    name: str,
    graph: dict[str, set[str]],
    usage: dict[str, int],
    visiting: set[str] | None = None,
) -> int:
    visiting = set() if visiting is None else set(visiting)
    require(name not in visiting, f"recursive SRAM call graph at {name}")
    visiting.add(name)
    children = [
        stack_upper_bound(child, graph, usage, visiting)
        for child in graph.get(name, set())
        if child in usage
    ]
    return usage.get(name, 0) + (max(children) if children else 0)


def parse_numeric_defines(path: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    pattern = re.compile(
        r"^\s*#define\s+(BK7258_OTA_[A-Z0-9_]+)\s+"
        r"(0x[0-9a-fA-F]+|[0-9]+)u?\s*(?:/\*.*)?$"
    )
    for line in path.read_text(encoding="utf-8").splitlines():
        match = pattern.match(line)
        if match:
            result[match.group(1)] = int(match.group(2), 0)
    return result


def verify_header_contract(header: Path) -> dict[str, int]:
    defines = parse_numeric_defines(header)
    expected = {
        "BK7258_OTA_FLASH_ID": metadata.FLASH_ID,
        "BK7258_OTA_FLASH_SIZE": 0x800000,
        "BK7258_OTA_ERASE_SIZE": metadata.ERASE_SIZE,
        "BK7258_OTA_WRITE_CHUNK_SIZE": metadata.WRITE_CHUNK_SIZE,
        "BK7258_OTA_CP_ACTIVE_START": metadata.CP_ACTIVE_START,
        "BK7258_OTA_CP_STAGING_START": metadata.CP_STAGING_START,
        "BK7258_OTA_CP_SLOT_SIZE": metadata.CP_SLOT_SIZE,
        "BK7258_OTA_AP_ACTIVE_START": metadata.AP_ACTIVE_START,
        "BK7258_OTA_AP_STAGING_START": metadata.AP_STAGING_START,
        "BK7258_OTA_AP_SLOT_SIZE": metadata.AP_SLOT_SIZE,
        "BK7258_OTA_JOURNAL_COPY_SIZE": metadata.JOURNAL_COPY_SIZE,
        "BK7258_OTA_SCRATCH_START": metadata.SCRATCH_START,
        "BK7258_OTA_PAIR_SECTORS": metadata.PAIR_SECTORS,
        "BK7258_OTA_PHASES_PER_SECTOR": metadata.PHASES_PER_SECTOR,
        "BK7258_OTA_PHASE_MARKERS": metadata.PHASE_MARKERS,
        "BK7258_OTA_HEADER_SIZE": metadata.HEADER_SIZE,
        "BK7258_OTA_CONTROL_SIZE": metadata.CONTROL_SIZE,
        "BK7258_OTA_PHASE_OFFSET": metadata.PHASE_OFFSET,
        "BK7258_OTA_HEADER_MAGIC": metadata.HEADER_MAGIC,
        "BK7258_OTA_MARKER_MAGIC": metadata.MARKER_MAGIC,
        "BK7258_OTA_FORMAT_VERSION": metadata.FORMAT_VERSION,
    }
    for name, value in expected.items():
        require(defines.get(name) == value, f"C/Python ABI drift: {name}")
    text = header.read_text(encoding="utf-8")
    require(
        "sizeof(struct bk7258_ota_journal_header_v1) ==" in text,
        "C header-size static assertion is missing",
    )
    require(
        "sizeof(struct bk7258_ota_journal_marker_v1) ==" in text,
        "C marker-size static assertion is missing",
    )
    return expected


def verify_sdk_source(sdk: Path) -> dict[str, Any]:
    require(SDK_RELEASE in sdk.name, f"SDK source is not the pinned {SDK_RELEASE}")
    files = {
        "driver": sdk / "cp/middleware/driver/flash/flash_driver.c",
        "driver_header": sdk / "cp/middleware/driver/flash/flash_driver.h",
        "ll": sdk / "cp/middleware/soc/bk7258/hal/flash_ll.h",
        "flash_struct": sdk / "cp/middleware/soc/bk7258/soc/flash_struct.h",
        "sys_struct": sdk / "cp/middleware/soc/bk7258/soc/sys_struct.h",
        "wdt_struct": sdk / "cp/middleware/soc/bk7258/soc/wdt_struct.h",
        "types": sdk / "cp/include/driver/hal/hal_flash_types.h",
        "registers": sdk / "cp/include/soc/bk7258/reg_base.h",
        "config": sdk / "cp/components/bk_libs/bk7258/config/sdkconfig",
    }
    for name, path in files.items():
        require(path.is_file(), f"official SDK {name} source is missing: {path}")

    driver = files["driver"].read_text(encoding="utf-8")
    driver_header = files["driver_header"].read_text(encoding="utf-8")
    ll = files["ll"].read_text(encoding="utf-8")
    flash_struct = files["flash_struct"].read_text(encoding="utf-8")
    sys_struct = files["sys_struct"].read_text(encoding="utf-8")
    wdt_struct = files["wdt_struct"].read_text(encoding="utf-8")
    types = files["types"].read_text(encoding="utf-8")
    registers = files["registers"].read_text(encoding="utf-8")
    config = files["config"].read_text(encoding="utf-8")

    fragments = {
        "C86517 8 MiB/2-status-byte identity": "{0xC86517,\t FLASH_SIZE_8M,   2,"
        in driver,
        "32-byte SDK transaction": "#define FLASH_BYTES_CNT                  32"
        in driver_header,
        "eight controller data words": "#define FLASH_BUFFER_LEN                 8"
        in driver_header,
        "4 KiB erase sector": "#define FLASH_SECTOR_SIZE                0x1000"
        in driver_header,
        "raw read command": "FLASH_OP_CMD_READ  = 5" in types,
        "page-program command": "FLASH_OP_CMD_PP    = 12" in types,
        "sector-erase command": "FLASH_OP_CMD_SE    = 13" in types,
        "controller waits after program": "flash_ll_set_op_cmd_write" in ll
        and "while (flash_ll_is_busy(hw));" in ll,
        "controller register layout": all(
            token in flash_struct
            for token in (
                "uint32_t op_sw:",
                "uint32_t wp_value:",
                "uint32_t busy_sw:",
                "uint32_t data_sw_flash",
                "uint32_t data_flash_sw",
                "uint32_t mode_sel:",
                "uint32_t wrsr_data:",
                "uint32_t addr_sw_reg:",
                "uint32_t op_type_sw:",
            )
        ),
        "controller/SYS/WDT bases": all(
            token in registers
            for token in (
                "SOC_SYS_REG_BASE         (0x44010000",
                "SOC_AON_WDT_REG_BASE     (0x44000600",
                "SOC_FLASH_REG_BASE       (0x44030000",
                "SOC_WDT_REG_BASE         (0x44800000",
            )
        ),
        "secondary-core control/status bits": all(
            token in sys_struct
            for token in (
                "cpu1_sw_rst",
                "cpu1_pwr_dw",
                "cpu2_sw_rst",
                "cpu2_pwr_dw",
                "cpu1_pwr_dw_state",
                "cpu2_pwr_dw_state",
            )
        ),
        "watchdog key/period register": "uint32_t period:   16" in wdt_struct
        and "uint32_t key:      8" in wdt_struct,
        "BK7236XX path": "CONFIG_SOC_BK7236XX=y" in config,
        "quad disabled": "# CONFIG_FLASH_QUAD_ENABLE is not set" in config,
        "volatile status disabled": "# CONFIG_FLASH_WRITE_STATUS_VOLATILE is not set"
        in config,
        "mailbox Flash lock disabled": "# CONFIG_FLASH_MB is not set" in config,
    }
    for description, passed in fragments.items():
        require(passed, f"official SDK contract drift: {description}")

    return {
        "release": SDK_RELEASE,
        "source": str(sdk),
        "files": {name: sha256(path) for name, path in files.items()},
        "contracts": sorted(fragments),
    }


def verify(args: argparse.Namespace) -> dict[str, Any]:
    elf = args.elf.resolve()
    map_path = args.map.resolve()
    require(elf.is_file(), f"ELF is missing: {elf}")
    require(map_path.is_file(), f"map is missing: {map_path}")

    boot_dir = elf.parent
    abi_header = boot_dir / "boot_ota_abi.h"
    engine_header = boot_dir / "boot_ota_engine.h"
    engine_source = boot_dir / "boot_ota_engine.c"
    loader_source = boot_dir / "boot_ota_loader.c"
    for path in (abi_header, engine_header, engine_source, loader_source):
        require(path.is_file(), f"required source is missing: {path}")

    sections = parse_sections(run([args.objdump, "-h", str(elf)]))
    symbols = parse_symbols(run([args.nm, "-n", "-S", "--defined-only", str(elf)]))
    disassembly = run([args.objdump, "-d", "-j", ".ota_sram", str(elf)])
    full_disassembly = run([args.objdump, "-d", str(elf)])
    undefined = run([args.nm, "-u", str(elf)]).strip()
    require(not undefined, f"bootloader has undefined symbols:\n{undefined}")

    section = sections.get(".ota_sram")
    require(section is not None, ".ota_sram section is missing")
    require(section.vma == SRAM_VMA_START, ".ota_sram VMA drifted")
    require(
        0 < section.size <= SRAM_VMA_END - SRAM_VMA_START, ".ota_sram size is invalid"
    )
    require(
        FLASH_VMA_START <= section.lma < FLASH_VMA_END
        and section.lma + section.size <= FLASH_VMA_END,
        ".ota_sram load image is outside the Tier-1 slot",
    )
    require(
        "ALLOC" in section.flags and "LOAD" in section.flags,
        ".ota_sram is not loadable",
    )
    require("CODE" in section.flags, ".ota_sram is not executable")
    require(section.vma != section.lma, ".ota_sram has no distinct SRAM VMA")
    require(
        sections.get(".data") is None or sections[".data"].size == 0,
        ".data is non-empty",
    )
    require(
        sections.get(".bss") is None or sections[".bss"].size == 0, ".bss is non-empty"
    )

    missing = REQUIRED_SRAM_SYMBOLS - symbols.keys()
    require(not missing, f"SRAM closure symbols are missing: {sorted(missing)}")
    for name in REQUIRED_SRAM_SYMBOLS:
        symbol = symbols[name]
        require(
            section.vma <= symbol.address < section.vma + section.size,
            f"{name} is not resident in .ota_sram",
        )

    closure_symbols = {
        name
        for name, symbol in symbols.items()
        if section.vma <= symbol.address < section.vma + section.size
    }

    for name in (
        "boot_ota_engine_install",
        "boot_ota_engine_call",
        "boot_prepare_ota_execution",
    ):
        symbol = symbols.get(name)
        require(symbol is not None, f"XIP loader symbol is missing: {name}")
        require(
            FLASH_VMA_START <= symbol.address < FLASH_VMA_END,
            f"XIP loader symbol is outside Tier-1: {name}",
        )

    # The R2 implementation is retained for static closure verification but
    # must not be reachable from Reset_Handler/c_main.  Its installer is
    # referenced only by the equally inactive boot_ota_engine_call wrapper.

    full_call_graph = parse_call_graph(full_disassembly)
    engine_call_callers = sorted(
        caller
        for caller, targets in full_call_graph.items()
        if "boot_ota_engine_call" in targets
    )
    install_callers = sorted(
        caller
        for caller, targets in full_call_graph.items()
        if "boot_ota_engine_install" in targets
    )
    require(
        not engine_call_callers,
        f"normal Tier-1 path reaches disabled OTA wrapper: {engine_call_callers}",
    )
    require(
        install_callers == ["boot_ota_engine_call"],
        f"SRAM installer has unexpected callers: {install_callers}",
    )

    image = read_section(elf, args.objcopy, ".ota_sram")
    require(len(image) == section.size, "extracted SRAM section size drifted")
    gate = symbols["g_bk7258_ota_write_gate"]
    gate_offset = gate.address - section.vma
    require(gate_offset + 4 <= len(image), "write gate lies outside SRAM image")
    gate_value = struct.unpack_from("<I", image, gate_offset)[0]
    require(gate_value == 0, "N15-R2 Flash write gate is enabled")

    # Raw instruction words can numerically resemble 0x02xxxxxx, so inspect
    # only literal-pool words identified by objdump rather than scanning every
    # four bytes of executable code.

    xip_literals: list[tuple[int, int]] = []
    literal_pattern = re.compile(r"^\s*([0-9a-fA-F]+):.*\.word\s+0x([0-9a-fA-F]+)\s*$")
    for line in disassembly.splitlines():
        match = literal_pattern.match(line)
        if match:
            address = int(match.group(1), 16)
            value = int(match.group(2), 16)
            if 0x02000000 <= value < 0x02800000:
                xip_literals.append((address - section.vma, value))
    require(not xip_literals, f"SRAM closure embeds XIP pointers: {xip_literals}")

    call_graph = parse_call_graph(disassembly)
    external_calls = {
        caller: sorted(target for target in targets if target not in closure_symbols)
        for caller, targets in call_graph.items()
        if caller in closure_symbols
        and any(target not in closure_symbols for target in targets)
    }
    require(not external_calls, f"SRAM closure calls outside itself: {external_calls}")

    stack_paths = [boot_dir / "boot_ota_engine.su"]
    usage = parse_stack_usage(stack_paths)
    missing_stack = {
        name
        for name in REQUIRED_SRAM_SYMBOLS
        if name.startswith("boot_ota_sram_") and name not in usage
    }
    require(not missing_stack, f"stack evidence is missing: {sorted(missing_stack)}")
    entry_stack = stack_upper_bound("boot_ota_sram_entry", call_graph, usage)
    require(
        entry_stack <= MAX_CLOSURE_STACK,
        f"SRAM closure stack {entry_stack} exceeds {MAX_CLOSURE_STACK}",
    )

    binary = boot_dir / "bl.bin"
    require(binary.is_file(), f"logical boot image is missing: {binary}")
    binary_data = binary.read_bytes()
    load_offset = section.lma - FLASH_VMA_START
    require(
        binary_data[load_offset : load_offset + section.size] == image,
        "bl.bin does not contain the exact SRAM load image",
    )
    require(
        len(binary_data) <= FLASH_VMA_END - FLASH_VMA_START,
        "Tier-1 image exceeds its slot",
    )

    map_text = map_path.read_text(encoding="utf-8")
    for token in (
        ".ota_sram",
        "__ota_sram_start",
        "__ota_sram_end",
        "__ota_sram_load_start",
        "boot_ota_engine.o",
    ):
        require(token in map_text, f"link map evidence is missing: {token}")

    engine_header_text = engine_header.read_text(encoding="utf-8")
    require(
        re.search(
            r"#define\s+BK7258_OTA_ENGINE_WRITE_GATE\s+0u\b",
            engine_header_text,
        )
        is not None,
        "source write gate is not exactly zero",
    )
    engine_text = engine_source.read_text(encoding="utf-8")
    for forbidden in (
        r"\bbk_flash_[A-Za-z0-9_]*\s*\(",
        r"\brtos_[A-Za-z0-9_]*\s*\(",
        r"\bmem(?:cpy|set|move)\s*\(",
        r"\b(?:printf|puts|uart_[A-Za-z0-9_]*)\s*\(",
        r"\b(?:malloc|free)\s*\(",
    ):
        require(
            re.search(forbidden, engine_text) is None,
            f"forbidden SRAM dependency: {forbidden}",
        )
    for required in (
        "mrs %0, primask",
        "SCB_DCACHE_ENABLE",
        "MPU_CTRL",
        "SYS_CPU1_POWERED_DOWN",
        "SYS_CPU2_POWERED_DOWN",
        "boot_ota_sram_wdt_feed",
        "boot_ota_sram_fail_reset",
        "g_bk7258_ota_write_gate",
    ):
        require(
            required in engine_text, f"SRAM safety source gate is missing: {required}"
        )

    abi_contract = verify_header_contract(abi_header)
    metadata_result = metadata.run_self_test()
    sdk_result = (
        verify_sdk_source(args.sdk_source.resolve())
        if args.sdk_source is not None
        else None
    )

    return {
        "status": "pass-read-only-sram-closure",
        "writes_enabled": False,
        "elf": str(elf),
        "elf_sha256": sha256(elf),
        "logical_boot_image_sha256": sha256(binary),
        "sram": {
            "vma": section.vma,
            "lma": section.lma,
            "size": section.size,
            "limit": SRAM_VMA_END - SRAM_VMA_START,
            "write_gate_value": gate_value,
            "xip_pointer_count": len(xip_literals),
            "external_call_count": sum(len(value) for value in external_calls.values()),
            "entry_stack_upper_bound": entry_stack,
            "stack_limit": MAX_CLOSURE_STACK,
        },
        "environment": {
            "interrupts": "PRIMASK=1 required",
            "cache": "D-cache off and MPU disabled required",
            "secondaries": "CPU1+CPU2 reset/power-down required",
            "watchdog": "SRAM feed; stuck controller stops feeding and resets",
            "read_back": "every erase/program/copy is checked",
        },
        "runtime_integration": {
            "normal_boot_path_callers": engine_call_callers,
            "installer_callers": install_callers,
            "state": "linked-for-static-verification-only",
        },
        "abi_contract": abi_contract,
        "metadata_abi": metadata_result,
        "sdk_source": sdk_result,
        "authenticated": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--sdk-source", type=Path)
    parser.add_argument("--objdump", default="arm-none-eabi-objdump")
    parser.add_argument("--objcopy", default="arm-none-eabi-objcopy")
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--json", action="store_true", help="emit JSON only")
    parser.add_argument("--output", type=Path, help="also write the JSON report")
    args = parser.parse_args()

    try:
        result = verify(args)
    except (OSError, UnicodeError, VerificationError, metadata.MetadataError) as error:
        print(f"BK7258 N15 OTA SRAM closure FAIL: {error}")
        return 1

    encoded = json.dumps(result, indent=2, sort_keys=True)
    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded + "\n", encoding="utf-8")
    if not args.json:
        print(
            "BK7258 N15 OTA SRAM closure PASS: writes_enabled=false "
            f"size=0x{result['sram']['size']:x} "
            f"stack={result['sram']['entry_stack_upper_bound']}"
        )
    else:
        print(encoded)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
