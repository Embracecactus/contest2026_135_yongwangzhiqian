#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise the real CP fault-frame predicate without dereferencing target RAM."""
import re
import subprocess
import tempfile
from pathlib import Path
from test_mfrc522_read_errors import extract_function, REPOSITORY

source = (REPOSITORY / "chips/bk7258/cp/bk7258_vectors.c").read_text()
function = extract_function(source, "static bool bk7258_fault_frame_readable(")
headers = [
    REPOSITORY / "chips/bk7258/include/bk7258_amp.h",
    REPOSITORY / "chips/bk7258/include/bk7258_psram.h",
    REPOSITORY.parent / "nuttx/arch/arm/src/arm_m/nvic.h",
]
names = set(re.findall(r"\b(?:BK7258|NVIC)_[A-Z0-9_]+\b", function))
defs = []
for header in headers:
    for line in header.read_text().splitlines():
        if line.startswith("#define ") and line.split()[1] in names:
            defs.append(line)
prefix = """#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#define BK7258_EXCEPTION_FRAME_WORDS 8u
#ifdef CONFIG_BK7258_PSRAM
static bool ready;
static bool bk7258_psram_ready(void) { return ready; }
#endif
"""
main = """
int main(void) {
 assert(bk7258_fault_frame_readable(BK7258_CP_RAM_BASE,0));
 assert(bk7258_fault_frame_readable(BK7258_CP_RAM_BASE+BK7258_CP_RAM_SIZE-32,0));
 assert(!bk7258_fault_frame_readable(BK7258_CP_RAM_BASE-4,0));
 assert(!bk7258_fault_frame_readable(BK7258_CP_RAM_BASE+BK7258_CP_RAM_SIZE-28,0));
 assert(!bk7258_fault_frame_readable(0xffffffffu,0));
 assert(!bk7258_fault_frame_readable(0x60720000u,0));
 assert(!bk7258_fault_frame_readable(0x60704038u,0x100));
#ifdef CONFIG_BK7258_PSRAM
 ready=true;
 assert(bk7258_fault_frame_readable(0x60704038u,0x100));
 assert(!bk7258_fault_frame_readable(0x60704039u,0x100));
 assert(bk7258_fault_frame_readable(0x6071ffe0u,0));
 assert(!bk7258_fault_frame_readable(0x6071ffe4u,0));
 unsigned errors[]={NVIC_CFAULTS_MUNSTKERR,NVIC_CFAULTS_MSTKERR,
 NVIC_CFAULTS_MLSPERR,NVIC_CFAULTS_UNSTKERR,NVIC_CFAULTS_STKERR,
 NVIC_CFAULTS_LSPERR,NVIC_CFAULTS_STKOF};
 for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
  assert(!bk7258_fault_frame_readable(0x60704038u,errors[i]));
  assert(!bk7258_fault_frame_readable(BK7258_CP_RAM_BASE,errors[i]));
 }
#endif
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="cp-fault-frame-") as tmp:
    p = Path(tmp)
    (p / "test.c").write_text(prefix + "\n".join(defs) + "\n" + function + main)
    for enabled in (False, True):
        flags = ["-DCONFIG_BK7258_PSRAM"] if enabled else []
        subprocess.run(
            ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined"]
            + flags
            + [str(p / "test.c"), "-o", str(p / "test")],
            check=True,
        )
        subprocess.run([str(p / "test")], check=True)
print("PASS: CP fault-frame boundaries, PSRAM readiness and stacking-fault rejection")
