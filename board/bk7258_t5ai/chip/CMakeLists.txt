############################################################################
# contest2026_135_yongwangzhiqian/board/bk7258_t5ai/chip/CMakeLists.txt
#
# SPDX-License-Identifier: Apache-2.0
#
# Mirror of chip/Make.defs for the CMake backend.  Stage N1 is validated
# against the classic Make backend (matching the RV1126B precedent); this
# file exists so a CMake-driven build does not silently pull in an
# inconsistent source set.
############################################################################

# Sources mirror chip/Make.defs CHIP_CSRCS.
set(SRCS
    bk7258_vectors.c
    bk7258_start.c
    bk7258_lowputc.c
    bk7258_serial.c
    bk7258_irq.c
    bk7258_timerisr.c
    bk7258_allocateheap.c
)

# NOTE: The common armv8-m CMake path still adds arm_vectors.c.  When this
# chip is built under CMake, that file would collide with bk7258_vectors.c
# (duplicate _vectors symbol).  We do not exercise CMake for N1; if a later
# stage switches to CMake, filter arm_vectors.c out of the arch target here.

target_sources(chip PRIVATE ${SRCS})
