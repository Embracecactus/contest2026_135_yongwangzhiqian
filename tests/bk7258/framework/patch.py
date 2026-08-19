#!/usr/bin/env python3
"""patch.py - mechanical host-build patches for BK7258 firmware modules.

Applies minimal, mechanical edits to THROWAWAY COPIES only; the real sources
are never modified.  Two classes of change are made:

  1. MMIO macros are re-routed to mock_reg32_ref() so register traffic lands
     in the framework's RAM map (the register address is unchanged).
  2. Freestanding ARM barriers and the naked jump function in the MCUboot
     handoff path are replaced with host-neutral equivalents or stubs so an
     x86_64 compiler can assemble the file.

Usage: patch.py <profile> <input> <output>
"""

import re
import sys

REG32_LINE = re.compile(r"^#define\s+REG32\(addr\)[^\n]*$", re.M)

TWO_LINE_REG32 = re.compile(
    r"^#define\s+(BK7258_BL[12]_OTP_REG32\(addr\))\s*\\\s*\n"
    r"\s*\(.*\)[^\n]*$", re.M)

ARM_BARRIER_LINE = re.compile(
    r"^__asm__? volatile \([^\n]*\);\s*$", re.M)

NOP_BARRIER = "__asm volatile (\"nop\");"

NAKED_JUMP_START = (
    "static void __attribute__((naked, noinline, noreturn, section(\".data\")))")

NAKED_JUMP_STUB = """static void __attribute__((noinline, noreturn))
bk7258_mcuboot_jump(uint32_t msp, uint32_t reset)
{
  mock_boot_jump(msp, reset);
  for (;;)
    {
    }
}"""

INVALID_VECTOR_LOOP = re.compile(
    r"(\r?\n)([ \t]*)\{\r?\n[ \t]*for \(;;\)\r?\n[ \t]*\{\r?\n"
    r"[ \t]*\}\r?\n[ \t]*\}\r?\n")


def patch_naked_jump(src):
    """Replace the naked ASM jump function body with a host stub."""
    start = src.find(NAKED_JUMP_START)
    if start < 0:
        return src

    brace = src.find("{", start)
    if brace < 0:
        return src

    # The ASM body ends with "  );" followed by the closing brace.
    end = src.find("\n}", brace)
    if end < 0:
        return src

    head = src[:start]
    tail = src[end + 2:]
    return head + NAKED_JUMP_STUB + tail


def patch_invalid_vector_loop(src):
    """Replace the secondary-path "invalid vector" infinite loop with the
    jump hook (0xffffffff markers), so the host test can observe the
    hang.  Runs after patch_naked_jump so the stub loop never matches."""
    match = INVALID_VECTOR_LOOP.search(src)
    if match is None:
        return src

    newline = match.group(1)
    indent = match.group(2)
    hook = (newline + indent + "{\n" + indent + "  mock_boot_jump("
            "0xffffffffu, 0xffffffffu);\n" + indent + "}\n")
    return src[:match.start()] + hook + src[match.end():]


def patch_ram_sections(src):
    """Drop the board's RAM-resident section placement (.data) from the
    handoff functions: host x86_64 marks .data non-executable."""
    return src.replace(
        "static void __attribute__((noinline, noreturn, section(\".data\")))",
        "static void __attribute__((noinline, noreturn))")


def patch_arm_barriers(src):
    replace = ("__asm volatile (\"dmb sy\" ::: \"memory\");",
               "__asm volatile (\"dsb sy\" ::: \"memory\");",
               "__asm volatile (\"isb sy\" ::: \"memory\");",
               "__asm volatile (\"dsb sy; isb\" ::: \"memory\");",
               "__asm__ volatile (\"dsb 0xf\" ::: \"memory\");",
               "__asm__ volatile (\"isb 0xf\" ::: \"memory\");",
               "__asm volatile (\"cpsid i\" ::: \"memory\");",
               "__asm volatile (\"hexboot\" ::: \"memory\");")

    for line in replace:
        src = src.replace(line, "__asm__ volatile (\"\" ::: \"memory\");")

    return src.replace(NOP_BARRIER, "__asm__ volatile (\"\");")


def patch_reg32(src):
    """Route REG32/BL1_REG32/BK7258_REG32 macros to mock_reg32_ref()."""
    src = REG32_LINE.sub("#define REG32(addr)       (*mock_reg32_ref(addr))",
                         src)
    src = re.sub(r"^#define\s+BL1_REG32\(address\)[^\n]*$",
                 "#define BL1_REG32(address) (*mock_reg32_ref(address))",
                 src, flags=re.M)
    src = re.sub(r"^#define\s+BK7258_REG32\(address\)[^\n]*$",
                 "#define BK7258_REG32(address) (*mock_reg32_ref(address))",
                 src, flags=re.M)
    src = TWO_LINE_REG32.sub(
        r"#define \1 (*mock_reg32_ref(addr))", src)
    return src


def patch_scale1_threshold_ref(src):
    """bk7258_scale_rotate.c programs the SDK-missing Scale1 write-burst
    field through a direct volatile pointer.  Route that one poke through
    mock_reg32_ref() (address unchanged; lands in the mock RAM window)."""
    return src.replace(
        "(volatile uint32_t *)(uintptr_t)BK7258_SCALE1_WRITE_THRESHOLD_REG",
        "mock_reg32_ref(BK7258_SCALE1_WRITE_THRESHOLD_REG)")


def patch_scale_rotate_board_include(src):
    """The patched copy lives in build/; re-point the relative board
    include at the real header via -I CHIP_INC and declare the mock_reg32
    accessor used by the rerouted Scale1 poke."""
    src = src.replace('#include "../include/bk7258_scale_rotate.h"',
                      '#include "bk7258_scale_rotate.h"')
    return src.replace("#include <nuttx/spinlock.h>",
                       "#include <nuttx/spinlock.h>\n"
                       "#include \"mock_reg32.h\"")


def patch_irda_reg(src):
    """bk7258_irda.c accesses the BK7258 IRDA registers through direct
    volatile pointers.  Route those two accessors through mock_reg32 so the
    host tests can preset and observe the NEC decoder registers (address
    unchanged; lands in the mock RAM window)."""
    src = src.replace(
        "return *(FAR volatile uint32_t *)reg;",
        "return mock_reg32_read(reg);")
    src = src.replace(
        "*(FAR volatile uint32_t *)reg = value;",
        "mock_reg32_write(reg, value);")
    src = src.replace('#include <arch/chip/bk7258_irda.h>',
                       '#include <arch/chip/bk7258_irda.h>\n'
                       '#include "mock_reg32.h"')
    # Expose the character-device callbacks so the host suite can drive
    # open/close/read/write/ioctl directly instead of through a mock vfs.
    for fn in ("bk7258_irda_open", "bk7258_irda_close",
               "bk7258_irda_read", "bk7258_irda_write", "bk7258_irda_ioctl"):
        src = src.replace("static int " + fn, "int " + fn)
        src = src.replace("static ssize_t " + fn, "ssize_t " + fn)
    # Expose a reset hook so each host test starts with a clean driver
    # singleton (the static g_bk7258_irda keeps state across tests).
    src = src.replace("static struct bk7258_irda_priv_s g_bk7258_irda =",
                      "struct bk7258_irda_priv_s g_bk7258_irda =", 1)
    hook = ("\nvoid bk7258_irda_test_reset(void)\n"
            "{\n"
            "  memset(&g_bk7258_irda, 0, sizeof(g_bk7258_irda));\n"
            "  pthread_mutex_init(&g_bk7258_irda.lock, NULL);\n"
            "  sem_init(&g_bk7258_irda.keysem, 0, 0);\n"
            "}\n")
    src = src.replace("static inline uint32_t bk7258_irda_reg_read",
                      hook + "\nstatic inline uint32_t bk7258_irda_reg_read",
                      1)
    return src


def patch_wdt_include(src):
    """Point ../boot_wdt.h at the patched copy in the build dir."""
    return src.replace('#include "../boot_wdt.h"', '#include "boot_wdt.h"')


def patch_flash_fifo(src):
    """Route the auto-increment flash data window through the FIFO mock."""
    return src.replace(
        "BL1_REG32(FLASH_DATA_FLASH_TO_SW)",
        "(*mock_flash_fifo_ref())")


PROFILES = {
    "boot_wdt": [patch_reg32, patch_arm_barriers],
    "boot_flash": [patch_reg32, patch_wdt_include, patch_flash_fifo],
    "boot_clock": [patch_reg32, patch_arm_barriers],
    "boot_runtime": [patch_reg32, patch_arm_barriers],
    "bl1_manifest": [patch_reg32],
    "bl2_security_cnt": [patch_reg32],
    "bl2_flash_map": [patch_wdt_include],
    "bl2_mcuboot_boot": [patch_reg32, patch_arm_barriers,
                         patch_naked_jump, patch_invalid_vector_loop,
                         patch_ram_sections],
    "scale_rotate": [patch_arm_barriers, patch_scale1_threshold_ref,
                     patch_scale_rotate_board_include],
    "irda": [patch_irda_reg],
}


def main():
    if len(sys.argv) != 4:
        print("usage: patch.py <profile> <input> <output>", file=sys.stderr)
        return 1

    profile, src_path, dst_path = sys.argv[1:]
    with open(src_path, "r", encoding="utf-8") as stream:
        text = stream.read()

    for step in PROFILES.get(profile, []):
        text = step(text)

    with open(dst_path, "w", encoding="utf-8") as stream:
        stream.write(text)

    return 0


if __name__ == "__main__":
    sys.exit(main())