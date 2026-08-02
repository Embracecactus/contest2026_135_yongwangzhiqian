#!/usr/bin/env python3
"""Calculate and verify the BK7258 N9 RPTUN shared-memory layout."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


class VerificationError(RuntimeError):
    """Raised when an N9 layout gate is not satisfied."""


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


def parse_probe(
    workspace: Path, board: Path, cc: str, cmake: str
) -> dict[str, int]:
    """Compile/run a host probe against the exact checked-out C headers."""

    nuttx = workspace / "nuttx"
    openamp = nuttx / "openamp" / "open-amp"
    libmetal = nuttx / "openamp" / "libmetal"

    with tempfile.TemporaryDirectory(prefix="bk7258-rptun-layout-") as tmp:
        tmpdir = Path(tmp)
        metal_build = tmpdir / "metal-build"
        chip_include = tmpdir / "include" / "arch" / "chip"
        chip_include.mkdir(parents=True)

        for name in ("bk7258_amp.h", "bk7258_rptun.h"):
            shutil.copy2(board / "chip" / "include" / name, chip_include / name)

        run(
            cmake,
            "-S",
            str(libmetal),
            "-B",
            str(metal_build),
            "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
            "-DCMAKE_SYSTEM_NAME=Generic",
            "-DCMAKE_SYSTEM_PROCESSOR=generic",
            "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
            "-DMACHINE=template",
            "-DWITH_DEFAULT_LOGGER=OFF",
            "-DWITH_DOC=OFF",
            "-DWITH_TESTS=OFF",
        )

        source = tmpdir / "probe.c"
        executable = tmpdir / "probe"
        source.write_text(
            r"""
#include <stddef.h>
#include <stdio.h>
#include <nuttx/rptun/rptun.h>
#include <openamp/virtio_ring.h>
#include <arch/chip/bk7258_rptun.h>

int main(void)
{
  printf(
    "{"
    "\"control_size\":%zu,"
    "\"rptun_rsc_size\":%zu,"
    "\"rptun_rsc_vdev_offset\":%zu,"
    "\"rptun_rsc_carveout_offset\":%zu,"
    "\"vring_raw_size\":%zu,"
    "\"shmem_base\":%u,"
    "\"shmem_size\":%u,"
    "\"control_offset\":%u,"
    "\"resource_offset\":%u,"
    "\"resource_size\":%u,"
    "\"carveout_offset\":%u,"
    "\"carveout_size\":%u,"
    "\"vring_span\":%u,"
    "\"buffer_bytes\":%u,"
    "\"minimum_carveout\":%u,"
    "\"layout_spare\":%u"
    "}\n",
    sizeof(struct bk7258_rptun_control_s),
    sizeof(struct rptun_rsc_s),
    offsetof(struct rptun_rsc_s, rpmsg_vdev),
    offsetof(struct rptun_rsc_s, carveout),
    (size_t)vring_size(BK7258_RPTUN_VRING_NUM,
                       BK7258_RPTUN_VRING_ALIGN),
    BK7258_RPTUN_SHMEM_BASE,
    BK7258_RPTUN_SHMEM_SIZE,
    BK7258_RPTUN_CONTROL_OFFSET,
    BK7258_RPTUN_RESOURCE_OFFSET,
    BK7258_RPTUN_RESOURCE_SIZE,
    BK7258_RPTUN_CARVEOUT_OFFSET,
    BK7258_RPTUN_CARVEOUT_SIZE,
    BK7258_RPTUN_VRING_SPAN,
    BK7258_RPTUN_BUFFER_BYTES,
    BK7258_RPTUN_MIN_CARVEOUT,
    BK7258_RPTUN_LAYOUT_SPARE);
  return 0;
}
""".lstrip(),
            encoding="utf-8",
        )

        run(
            cc,
            "-std=gnu11",
            "-DCONFIG_RPTUN=1",
            "-DCONFIG_BK7258_RPTUN_LAYOUT=1",
            "-D__METAL_METAL_TRACE__H__=1",
            f"-I{tmpdir / 'include'}",
            f"-I{nuttx / 'include'}",
            f"-I{openamp / 'lib' / 'include'}",
            f"-I{metal_build / 'lib' / 'include'}",
            str(source),
            "-o",
            str(executable),
        )
        return {key: int(value) for key, value in json.loads(
            run(str(executable))
        ).items()}


def parse_int(value: str) -> int:
    return int(value, 0)


def parse_nm(nm: str, elf: Path) -> dict[str, int]:
    symbols: dict[str, int] = {}
    for line in run(nm, "-n", "--defined-only", str(elf)).splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+\S\s+(\S+)$", line)
        if match:
            symbols[match.group(2)] = int(match.group(1), 16)

    return symbols


def parse_alloc_sections(objdump: str, elf: Path) -> list[dict[str, int | str]]:
    lines = run(objdump, "-h", str(elf)).splitlines()
    sections: list[dict[str, int | str]] = []
    pattern = re.compile(
        r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+"
        r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"
        r"([0-9a-fA-F]+)\s+2\*\*\d+"
    )
    for index, line in enumerate(lines):
        match = pattern.match(line)
        if not match:
            continue

        flags = lines[index + 1] if index + 1 < len(lines) else ""
        if "ALLOC" not in flags:
            continue

        sections.append(
            {
                "name": match.group(1),
                "size": int(match.group(2), 16),
                "vma": int(match.group(3), 16),
                "lma": int(match.group(4), 16),
            }
        )

    return sections


def in_ranges(start: int, end: int, ranges: list[tuple[int, int]]) -> bool:
    return any(start >= low and end <= high for low, high in ranges)


def verify_sections(
    role: str,
    sections: list[dict[str, int | str]],
    ranges: list[tuple[int, int]],
) -> None:
    for section in sections:
        start = int(section["vma"])
        end = start + int(section["size"])
        if start == end:
            continue

        if not in_ranges(start, end, ranges):
            raise VerificationError(
                f"{role} allocated section {section['name']} "
                f"0x{start:08x}..0x{end:08x} is outside role-owned memory"
            )

        if section["name"] in {
            ".swap_data",
            ".dtcm_sec_data",
            ".sram_spinlock_section",
        }:
            raise VerificationError(
                f"{role} vendor section {section['name']} remains an "
                "orphan output section"
            )


def require_symbols(
    role: str, symbols: dict[str, int], expected: dict[str, int]
) -> None:
    for name, value in expected.items():
        observed = symbols.get(name)
        if observed != value:
            detail = "missing" if observed is None else f"0x{observed:08x}"
            raise VerificationError(
                f"{role} symbol {name}: expected 0x{value:08x}, got {detail}"
            )


def verify_source_contract(board: Path, compatibility: dict[str, object]) -> None:
    cp_defconfig = (board / "configs" / "cp_nsh_rptun" / "defconfig").read_text()
    ap_defconfig = (board / "configs" / "ap_smp_rptun" / "defconfig").read_text()
    if "CONFIG_BK7258_RPTUN_LAYOUT=y" not in cp_defconfig:
        raise VerificationError("CP N9 defconfig does not reserve RPTUN memory")
    if "CONFIG_BK7258_RPTUN_LAYOUT=y" not in ap_defconfig:
        raise VerificationError("AP N9 defconfig does not reserve RPTUN memory")
    if "CONFIG_SMP_DEFAULT_CPUSET=0x1" not in ap_defconfig:
        raise VerificationError("AP N9 default CPU set is not pinned to CPU0")

    for script_name in ("ld.script", "ld_ap.script"):
        linker = (board / "scripts" / script_name).read_text()
        for section in compatibility["vendor_input_sections"]:
            if f"*({section})" not in linker:
                raise VerificationError(
                    f"{script_name} does not explicitly place {section}"
                )

    ap_linker = (board / "scripts" / "ld_ap.script").read_text()
    for input_pattern in (
        "*(.data.g_bk7258_sdk_critical_lock)",
        "*(.bss.g_*lock*)",
        "*(.bss.g_bk7258_ap_smp_pending)",
    ):
        if input_pattern not in ap_linker:
            raise VerificationError(
                f"ld_ap.script does not collect {input_pattern} into the "
                "exclusive-state region"
            )


def verify_layout_values(
    layout: dict[str, int], compatibility: dict[str, object]
) -> None:
    expected = {
        "control_size": 0x40,
        "rptun_rsc_size": 0x108,
        "vring_raw_size": 222,
        "shmem_base": 0x28097000,
        "shmem_size": 0x8000,
        "control_offset": 0,
        "resource_offset": 0x40,
        "resource_size": 0x108,
        "carveout_offset": 0x180,
        "carveout_size": 0x7E80,
        "vring_span": 224,
        "buffer_bytes": 0x2000,
        "minimum_carveout": 0x31C0,
        "layout_spare": 0x4CC0,
    }
    for name, value in expected.items():
        if layout.get(name) != value:
            raise VerificationError(
                f"header layout {name}: expected {value}, "
                f"got {layout.get(name)}"
            )

    sram = compatibility["sram"]
    if [parse_int(item) for item in sram["ap_spinlock"]] != [
        0x28000000,
        0x28010000,
    ]:
        raise VerificationError("compatibility JSON AP spinlock range drift")
    if [parse_int(item) for item in sram["cp_owned"]] != [
        0x28010000,
        0x28050000,
    ]:
        raise VerificationError("compatibility JSON CP range drift")
    if [parse_int(item) for item in sram["rptun_shared"]] != [
        layout["shmem_base"],
        layout["shmem_base"] + layout["shmem_size"],
    ]:
        raise VerificationError("compatibility JSON RPTUN range drift")
    if parse_int(sram["team_telemetry"][0]) != (
        layout["shmem_base"] + layout["shmem_size"]
    ):
        raise VerificationError("RPTUN range does not end at telemetry")

    mailbox = compatibility["mailbox"]
    if mailbox["base"] != "0x41000000":
        raise VerificationError("MBOX0 compatibility base drift")
    if mailbox["channel_lengths"] != [2, 3, 3]:
        raise VerificationError("MBOX0 FIFO split drift")
    if mailbox["smp_raw_data1"] != 0 or mailbox["cp_ap_raw_data1"] != 16:
        raise VerificationError("MBOX0 zero/nonzero discriminator drift")
    if (
        mailbox["sdk_wrapper"] != "mb_chnl"
        or mailbox["sdk_logical_channel"] != "MB_CHNL_LOG"
        or mailbox["sdk_logical_index"] != 14
    ):
        raise VerificationError("RPTUN SDK logical mailbox channel drift")


def main() -> int:
    script = Path(__file__).resolve()
    board = script.parent.parent
    contest = board.parents[1]
    workspace = contest.parent

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--headers-only", action="store_true")
    parser.add_argument("--cp-elf", type=Path)
    parser.add_argument("--cp-map", type=Path)
    parser.add_argument("--ap-elf", type=Path)
    parser.add_argument("--ap-map", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--cc", default=os.environ.get("HOSTCC", "gcc"))
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--objdump", default="arm-none-eabi-objdump")
    args = parser.parse_args()

    compatibility_path = script.parent / "bk7258-rptun-compatibility.json"
    compatibility = json.loads(compatibility_path.read_text(encoding="utf-8"))

    layout = parse_probe(workspace, board, args.cc, args.cmake)
    verify_layout_values(layout, compatibility)
    verify_source_contract(board, compatibility)

    result: dict[str, object] = {
        "format": 1,
        "status": "headers-verified",
        "layout": layout,
        "compatibility": compatibility_path.name,
        "versions": {
            "nuttx": run("git", "-C", str(workspace / "nuttx"),
                         "rev-parse", "HEAD").strip(),
            "openamp": run("git", "-C",
                           str(workspace / "nuttx/openamp/open-amp"),
                           "rev-parse", "HEAD").strip(),
            "libmetal": run("git", "-C",
                            str(workspace / "nuttx/openamp/libmetal"),
                            "rev-parse", "HEAD").strip(),
        },
    }

    if not args.headers_only:
        required = (args.cp_elf, args.cp_map, args.ap_elf, args.ap_map)
        if any(path is None for path in required):
            parser.error(
                "--cp-elf/--cp-map/--ap-elf/--ap-map are required "
                "unless --headers-only is used"
            )
        for path in required:
            if path is None or not path.is_file():
                raise VerificationError(f"missing build artifact: {path}")

        shared = layout["shmem_base"]
        shared_end = shared + layout["shmem_size"]
        common_symbols = {
            "_bk7258_rptun_shmem_start": shared,
            "_bk7258_rptun_shmem_end": shared_end,
            "_bk7258_rptun_control": shared + layout["control_offset"],
            "_bk7258_rptun_resource": shared + layout["resource_offset"],
            "_bk7258_rptun_carveout_start":
                shared + layout["carveout_offset"],
            "_bk7258_rptun_carveout_end": shared_end,
        }

        cp_symbols = parse_nm(args.nm, args.cp_elf)
        ap_symbols = parse_nm(args.nm, args.ap_elf)
        require_symbols("CP", cp_symbols, common_symbols)
        require_symbols("AP", ap_symbols, common_symbols)
        require_symbols(
            "AP",
            ap_symbols,
            {
                "_bk7258_cpu2_probe_stack_top": shared,
                "_bk7258_cpu2_probe_stack_base": shared - 0x800,
                "_eheap": shared - 0x804,
            },
        )
        if cp_symbols.get("_eheap", 0xFFFFFFFF) >= 0x28050000:
            raise VerificationError("CP heap reaches the AP-owned SRAM")
        if ap_symbols.get("_ebss", 0xFFFFFFFF) >= shared - 0x800:
            raise VerificationError("AP BSS reaches the CPU2 boot stack")

        spinlock_range = tuple(
            parse_int(item) for item in compatibility["sram"]["ap_spinlock"]
        )
        cp_ram_range = tuple(
            parse_int(item) for item in compatibility["sram"]["cp_owned"]
        )
        ap_ram_range = tuple(
            parse_int(item) for item in compatibility["sram"]["ap_owned_n9"]
        )
        spinlock_start, spinlock_end = spinlock_range
        if ap_symbols.get("_sspinlock_data") != spinlock_start:
            raise VerificationError(
                "AP initialized spinlocks do not start at 0x28000000"
            )
        for symbol in ("_espinlock_data", "_sspinlock_bss",
                       "_espinlock_bss"):
            value = ap_symbols.get(symbol)
            if value is None or not spinlock_start <= value <= spinlock_end:
                raise VerificationError(
                    f"AP symbol {symbol} is outside the spinlock region"
                )

        exclusive_state = compatibility["ap_exclusive_state"]
        exclusive_symbols: dict[str, int] = {}
        for group, section_start, section_end in (
            ("initialized", "_sspinlock_data", "_espinlock_data"),
            ("zero_initialized", "_sspinlock_bss", "_espinlock_bss"),
        ):
            low = ap_symbols[section_start]
            high = ap_symbols[section_end]
            if low > high:
                raise VerificationError(
                    f"AP exclusive-state section {group} has inverted bounds"
                )
            for symbol in exclusive_state[group]:
                value = ap_symbols.get(symbol)
                if value is None:
                    raise VerificationError(
                        f"AP required exclusive-state symbol {symbol} is missing"
                    )
                if not low <= value < high:
                    raise VerificationError(
                        f"AP symbol {symbol} is outside {group} "
                        f"exclusive-state section"
                    )
                exclusive_symbols[symbol] = value

        cp_sections = parse_alloc_sections(args.objdump, args.cp_elf)
        ap_sections = parse_alloc_sections(args.objdump, args.ap_elf)
        verify_sections(
            "CP",
            cp_sections,
            [(0x02010000, 0x02100000), cp_ram_range],
        )
        verify_sections(
            "AP",
            ap_sections,
            [(0x02200000, 0x02400000), spinlock_range, ap_ram_range],
        )

        for role, map_path in (("CP", args.cp_map), ("AP", args.ap_map)):
            map_text = map_path.read_text(encoding="utf-8", errors="replace")
            orphan = re.search(
                r"^\.(swap_data|dtcm_sec_data|sram_spinlock_section)\s",
                map_text,
                re.MULTILINE,
            )
            if orphan:
                raise VerificationError(
                    f"{role} map retains orphan output section "
                    f".{orphan.group(1)}"
                )

        result.update(
            {
                "status": "elf-verified",
                "symbols": {
                    "cp_eheap": cp_symbols["_eheap"],
                    "ap_spinlock_start": spinlock_start,
                    "ap_spinlock_end": spinlock_end,
                    "ap_g_schedlock": ap_symbols["g_schedlock"],
                    "ap_exclusive_symbols": exclusive_symbols,
                    "ap_ebss": ap_symbols["_ebss"],
                    "ap_eheap": ap_symbols["_eheap"],
                    "cpu2_stack_base":
                        ap_symbols["_bk7258_cpu2_probe_stack_base"],
                    "cpu2_stack_top":
                        ap_symbols["_bk7258_cpu2_probe_stack_top"],
                },
                "allocated_sections": {
                    "cp": len(cp_sections),
                    "ap": len(ap_sections),
                },
            }
        )

    output = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json is not None:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(output, encoding="utf-8")

    print(
        "PASS bk7258-rptun-layout: "
        f"rsc={layout['rptun_rsc_size']} "
        f"vring={layout['vring_raw_size']}/{layout['vring_span']} "
        f"carveout=0x{layout['carveout_size']:x} "
        f"spare=0x{layout['layout_spare']:x} "
        f"status={result['status']}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except VerificationError as error:
        raise SystemExit(f"FAIL bk7258-rptun-layout: {error}") from error
