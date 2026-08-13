#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Verify ownership and dispatch invariants for the BK7258 SDK IRQ bridge."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
import tempfile


PUBLIC_SYMBOLS = (
    "bk_int_isr_register",
    "bk_int_isr_unregister",
    "bk_int_set_priority",
    "interrupt_init",
    "interrupt_deinit",
)

SDK_DIRECT_SYMBOLS = (
    "arch_interrupt_register_int",
    "arch_interrupt_unregister_int",
    "arch_interrupt_set_priority",
    "icu_int_map_table",
)


class Verifier:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0

    def check(self, condition: bool, name: str, detail: str) -> None:
        if condition:
            self.passed += 1
            print(f"PASS {name}: {detail}")
        else:
            self.failed += 1
            print(f"FAIL {name}: {detail}")


def run(*args: str) -> str:
    result = subprocess.run(
        args,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {' '.join(args)}\n"
            f"{result.stdout}"
        )

    return result.stdout


def read(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        return ""


def symbol_counts(nm_output: str) -> dict[str, int]:
    counts: dict[str, int] = {}
    for line in nm_output.splitlines():
        match = re.search(r"\b[TRWBD]\s+(\S+)$", line)
        if match:
            name = match.group(1)
            counts[name] = counts.get(name, 0) + 1

    return counts


def map_owner(map_text: str, symbol: str) -> str | None:
    match = re.search(
        rf"^{re.escape(symbol)}\s+(\S.*)$", map_text, re.MULTILINE
    )
    return match.group(1).strip() if match else None


def disassemble(objdump: str, obj: pathlib.Path, symbol: str) -> str:
    return run(objdump, "-dr", str(obj), f"--disassemble={symbol}")


def has_mapped_default_priority(
    disassembly: str, normal_priority: int, priority_shift: int
) -> bool:
    """Recognize the compiled LCD-0 / configured-normal priority selection."""

    if re.search(r"\bcmp(?:\.w)?\s+r\d+,\s*#27\b", disassembly) is None:
        return False

    normal = re.search(
        rf"\bmovne(?:s?\.w)?\s+(?P<reg>r\d+),\s*#{normal_priority}\b",
        disassembly,
    )
    if normal is None:
        return False

    priority_reg = re.escape(normal.group("reg"))
    if re.search(
        rf"\bmoveq(?:s?\.w)?\s+{priority_reg},\s*#0\b",
        disassembly,
    ) is None:
        return False

    fused_argument = re.search(
        rf"\bmov(?:s?\.w)?\s+r1,\s*{priority_reg},\s*lsl\s+"
        rf"#{priority_shift}\b",
        disassembly,
    )
    shifted = re.search(
        rf"\bmov(?:s?\.w)?\s+{priority_reg},\s*{priority_reg},"
        rf"\s*lsl\s+#{priority_shift}\b",
        disassembly,
    ) or re.search(
        rf"\blsl(?:s?\.w)?\s+{priority_reg},\s*{priority_reg},"
        rf"\s*#{priority_shift}\b",
        disassembly,
    ) or fused_argument
    if shifted is None:
        return False

    priority_argument = fused_argument is not None or \
        normal.group("reg") == "r1" or re.search(
            rf"\bmov(?:s?\.w)?\s+r1,\s*{priority_reg}\b",
            disassembly,
        ) is not None

    return priority_argument and "up_prioritize_irq" in disassembly


def has_flat_default_priority(
    disassembly: str, normal_priority: int, priority_shift: int
) -> bool:
    """Recognize the non-reentrant dispatcher safety priority."""

    encoded = normal_priority << priority_shift
    loaded = re.search(
        rf"\bmov(?:s?\.w)?\s+(?P<reg>r\d+),\s*#{encoded}\b",
        disassembly,
    )
    if loaded is None:
        return False

    priority_reg = re.escape(loaded.group("reg"))
    return (
        re.search(
            rf"\bmov(?:s?\.w)?\s+r1,\s*{priority_reg}\b",
            disassembly,
        )
        is not None
        and "up_prioritize_irq" in disassembly
    )


def has_irq_serialization(disassembly: str) -> bool:
    """Recognize NuttX local interrupt save/mask/restore semantics."""

    lowered = disassembly.lower()
    primask = (
        re.search(r"\bmrs\s+r\d+,\s*primask\b", lowered) is not None
        and "cpsid" in lowered
        and re.search(r"\bmsr\s+primask\b", lowered) is not None
    )
    basepri = (
        re.search(r"\bmrs\s+r\d+,\s*basepri\b", lowered) is not None
        and len(re.findall(r"\bmsr\s+basepri\b", lowered)) >= 2
    )
    external = (
        "up_irq_save" in lowered and "up_irq_restore" in lowered
    )
    return primask or basepri or external


def calls_direct_or_via_unregister_helper(
    callee: str, lifecycle_asm: str, helper_asm: str
) -> bool:
    """Accept a lifecycle call directly or through the locked teardown helper."""

    return callee in lifecycle_asm or (
        "bk7258_sdk_irq_unregister_locked" in lifecycle_asm
        and callee in helper_asm
    )


def extract_member(ar: str, archive: pathlib.Path, member: str,
                   output: pathlib.Path) -> None:
    result = subprocess.run(
        (ar, "p", str(archive), member),
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0 or not result.stdout:
        detail = result.stderr.decode(errors="replace").strip()
        raise RuntimeError(
            f"failed to extract {member} from {archive}"
            f"{': ' + detail if detail else ': member is absent'}"
        )

    output.write_bytes(result.stdout)


def main() -> int:
    script = pathlib.Path(__file__).resolve()
    board = script.parent.parent
    contest = board.parents[1]
    workspace = contest.parent

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=pathlib.Path,
                        default=workspace / "nuttx" / "nuttx")
    parser.add_argument("--map", type=pathlib.Path,
                        default=workspace / "nuttx" / "nuttx.map")
    parser.add_argument("--archive", type=pathlib.Path,
                        default=workspace / "nuttx" / "staging" / "libarch.a")
    parser.add_argument("--ar", default="arm-none-eabi-ar")
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    parser.add_argument("--objdump", default="arm-none-eabi-objdump")
    args = parser.parse_args()

    bridge_c = board / "chip" / "common" / "bk7258_sdk_irq.c"
    bridge_h = board / "chip" / "common" / "bk7258_sdk_irq.h"
    make_defs = board / "chip" / "Make.defs"
    cmake = board / "chip" / "CMakeLists.txt"
    kconfig = board / "chip" / "Kconfig"
    defconfig = board / "configs" / "t5ai_core_cp_base" / "defconfig"
    ldscript = board / "scripts" / "ld.script"
    build_config = workspace / "nuttx" / ".config"
    stubs = board / "chip" / "common" / "bk7258_sdk_stubs.c"

    verifier = Verifier()

    bridge_source = read(bridge_c)
    bridge_header = read(bridge_h)
    make_text = read(make_defs)
    cmake_text = read(cmake)
    kconfig_text = read(kconfig)
    defconfig_text = read(defconfig)
    ldscript_text = read(ldscript)
    build_config_text = read(build_config)
    stubs_text = read(stubs)
    default_priority_match = re.search(
        r"^#define\s+BK7258_SDK_IRQ_DEFAULT_PRIORITY\s+(\d+)",
        bridge_header,
        re.MULTILINE,
    )
    priority_bits_match = re.search(
        r"^#define\s+BK7258_SDK_IRQ_PRIORITY_BITS\s+(\d+)",
        bridge_header,
        re.MULTILINE,
    )
    default_priority = (
        int(default_priority_match.group(1))
        if default_priority_match is not None
        else -1
    )
    priority_shift = (
        8 - int(priority_bits_match.group(1))
        if priority_bits_match is not None
        else -1
    )

    verifier.check(bridge_c.is_file(), "S01", "dedicated bridge source exists")
    verifier.check(bridge_h.is_file(), "S02", "private bridge header exists")
    verifier.check(
        "config BK7258_SDK_IRQ_BRIDGE" in kconfig_text,
        "S03",
        "Kconfig owns a bridge gate",
    )
    verifier.check(
        "CONFIG_BK7258_SDK_IRQ_BRIDGE" in make_text
        and "bk7258_sdk_irq.c" in make_text,
        "S04",
        "classic Make backend gates the bridge source",
    )
    verifier.check(
        "CONFIG_BK7258_SDK_IRQ_BRIDGE" in cmake_text
        and "bk7258_sdk_irq.c" in cmake_text,
        "S05",
        "CMake backend mirrors bridge source selection",
    )
    verifier.check(
        "CONFIG_BK7258_SDK_IRQ_BRIDGE=y" in defconfig_text,
        "S06",
        "Stage B defconfig enables the bridge",
    )
    verifier.check(
        "INT_SRC_NONE == BK7258_EXTERNAL_IRQS" in bridge_source
        and "BK7258_SDK_IRQ_PRIORITY_SHIFT" in bridge_header,
        "S07",
        "source gates 64 SDK sources and priority encoding",
    )
    verifier.check(
        all(symbol not in stubs_text for symbol in PUBLIC_SYMBOLS),
        "S08",
        "production bridge is not implemented in sdk stubs",
    )
    verifier.check(
        "BK7258_SDK_IRQ_FIRST" in bridge_header
        and "BK7258_SDK_IRQ_COUNT" in bridge_header,
        "S09",
        "private header exposes bridge mapping constants",
    )
    verifier.check(
        "CONFIG_BK7258_SDK_IRQ_BRIDGE" in ldscript_text
        and "EXTERN(bk_int_isr_register)" in ldscript_text,
        "S10",
        "linker script forces overlay bridge extraction",
    )
    verifier.check(
        "INT_SRC_LCD == 27" in bridge_source
        and "BK7258_SDK_IRQ_LCD_PRIORITY" in bridge_header
        and "bk7258_sdk_irq_default_priority(source)" in bridge_source,
        "S11",
        "mapped defaults preserve the LCD source-27 priority-zero exception",
    )
    verifier.check(
        "select ARCH_IRQPRIO" in kconfig_text,
        "S12",
        "bridge Kconfig selects the priority API declaration",
    )
    verifier.check(
        "CONFIG_BK7258_SDK_IRQ_BRIDGE=y" in build_config_text
        and "CONFIG_ARCH_IRQPRIO=y" in build_config_text,
        "S13",
        "generated build configuration enables bridge and IRQ priorities",
    )
    verifier.check(
        "return BK7258_SDK_IRQ_FIRST + (int)index;" in bridge_source
        and "handler();" in bridge_source
        and "(void)arg;" in bridge_source,
        "S14",
        "source mapping and no-argument callback semantics are explicit",
    )
    verifier.check(
        "spinlock_t g_bk7258_sdk_irq_lock" in bridge_source
        and bridge_source.count(
            "spin_lock_irqsave(&g_bk7258_sdk_irq_lock)") >= 4
        and bridge_source.count(
            "spin_unlock_irqrestore(&g_bk7258_sdk_irq_lock") >= 4
        and "bk7258_sdk_irq_unregister_locked" in bridge_source,
        "S15",
        "bridge-local lock serializes lifecycle transactions",
    )

    if (not args.elf.is_file() or not args.map.is_file()
            or not args.archive.is_file()):
        verifier.check(False, "E00",
                       "ELF, link map, and libarch archive all exist")
        print(f"\nRESULT: {verifier.passed} passed, {verifier.failed} failed")
        return 1

    try:
        nm_output = run(args.nm, "-A", "--defined-only", str(args.elf))
        archive_nm = run(args.nm, "-A", "--defined-only",
                         str(args.archive))
        map_text = read(args.map)
        counts = symbol_counts(nm_output)
        archive_counts = symbol_counts(archive_nm)

        verifier.check(True, "E00",
                       "ELF, link map, and libarch archive are readable")

        for symbol in PUBLIC_SYMBOLS:
            verifier.check(
                archive_counts.get(symbol, 0) == 1,
                "E01",
                f"{symbol} has exactly one overlay archive definition",
            )

            owner = map_owner(map_text, symbol)
            verifier.check(
                owner is not None
                and "libarch.a(bk7258_sdk_irq.o)" in owner,
                "E02",
                f"{symbol} owner is overlay bk7258_sdk_irq.o"
                f" (observed: {owner or 'missing'})",
            )

        verifier.check(
            counts.get("bk_int_isr_register", 0) == 1,
            "E01A",
            "currently used bk_int_isr_register has one final ELF definition",
        )
        verifier.check(
            all(counts.get(symbol, 0) <= 1 for symbol in PUBLIC_SYMBOLS),
            "E01B",
            "garbage collection leaves no duplicate final definitions",
        )

        verifier.check(
            "libdriver.a(interrupt_base.c.obj)" not in map_text,
            "E03",
            "SDK interrupt_base object is not extracted",
        )

        for symbol in SDK_DIRECT_SYMBOLS:
            verifier.check(
                counts.get(symbol, 0) == 0,
                "E04",
                f"obsolete SDK direct-dispatch symbol {symbol} is absent",
            )

        with tempfile.TemporaryDirectory(prefix="bk7258-sdk-irq-") as tmp:
            bridge_obj = pathlib.Path(tmp) / "bk7258_sdk_irq.o"
            extract_member(args.ar, args.archive, "bk7258_sdk_irq.o",
                           bridge_obj)
            unregister_helper_asm = disassemble(
                args.objdump, bridge_obj,
                "bk7258_sdk_irq_unregister_locked",
            )
            register_asm = disassemble(args.objdump, bridge_obj,
                                       "bk_int_isr_register")
            unregister_asm = disassemble(args.objdump, bridge_obj,
                                         "bk_int_isr_unregister")
            priority_asm = disassemble(args.objdump, bridge_obj,
                                       "bk_int_set_priority")
            deinit_asm = disassemble(args.objdump, bridge_obj,
                                     "interrupt_deinit")

        for lifecycle, lifecycle_asm in (
            ("register", register_asm),
            ("unregister", unregister_asm),
            ("set-priority", priority_asm),
            ("deinit", deinit_asm),
        ):
            verifier.check(
                has_irq_serialization(lifecycle_asm),
                "E09",
                f"{lifecycle} path saves, masks, and restores local IRQs",
            )

        verifier.check(
            all(
                "bk7258_sdk_irq_unregister_locked" in lifecycle_asm
                for lifecycle_asm in (register_asm, unregister_asm, deinit_asm)
            )
            and all(
                callee in unregister_helper_asm
                for callee in (
                    "irq_attach",
                    "up_disable_irq",
                    "bk7258_clear_pending_irq",
                )
            ),
            "E10",
            "register, unregister, and deinit share the locked teardown helper",
        )

        for callee in (
            "irq_attach",
            "up_disable_irq",
            "bk7258_clear_pending_irq",
            "up_prioritize_irq",
            "up_enable_irq",
        ):
            verifier.check(
                calls_direct_or_via_unregister_helper(
                    callee, register_asm, unregister_helper_asm
                ),
                "E05",
                f"register path calls {callee} directly or via locked helper",
            )

        high_priority_dispatch = (
            "CONFIG_ARCH_HIPRI_INTERRUPT=y" in build_config_text
        )
        verifier.check(
            default_priority >= 0
            and priority_shift >= 0
            and (
                has_mapped_default_priority(
                    register_asm, default_priority, priority_shift
                )
                if high_priority_dispatch
                else has_flat_default_priority(
                    register_asm, default_priority, priority_shift
                )
            ),
            "E08",
            "register object applies the configured dispatcher-safe priority",
        )

        for callee in (
            "irq_attach",
            "up_disable_irq",
            "bk7258_clear_pending_irq",
        ):
            verifier.check(
                calls_direct_or_via_unregister_helper(
                    callee, unregister_asm, unregister_helper_asm
                ),
                "E06",
                f"unregister path calls {callee} via locked helper",
            )

        verifier.check(
            "up_prioritize_irq" in priority_asm,
            "E07",
            "custom-priority path calls up_prioritize_irq",
        )

    except (OSError, RuntimeError) as error:
        verifier.check(False, "E99", str(error))

    print(f"\nRESULT: {verifier.passed} passed, {verifier.failed} failed")
    return 0 if verifier.failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
