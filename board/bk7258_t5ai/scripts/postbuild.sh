#!/usr/bin/env bash
#
# contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/postbuild.sh
#
# SPDX-License-Identifier: Apache-2.0
#
# Stage N1 post-build step, invoked from scripts/Make.defs as POSTBUILD.
# Produces all-app.bin = bl_crc.bin (Tier-1 bootloader, 0x11000 bytes) +
# nuttx_crc.bin (the CRC-expanded NuttX app image) so a single artifact can
# be flashed at physical flash offset 0x0.
#
# Layout of all-app.bin (matches the board-verified probe/bootloader flow):
#
#   physical 0x00000 .. 0x10fff  bl_crc.bin     (Tier-1 bootloader)
#   physical 0x11000 ..          nuttx_crc.bin  (app; BK7236 magic @ 0x11110)
#
# The app image's internal BK7236 magic sits at nuttx.bin offset 0x100; the
# CRC expander tool inserts a 0x10-byte header before each 0x100-page, so in
# nuttx_crc.bin the magic moves to offset 0x110.  In the concatenated
# all-app.bin it sits at bl_crc.bin size (0x11000) + 0x110 = 0x11110,
# exactly where the flashing/tooling flow expects it.
#
# Args (passed by the POSTBUILD define in Make.defs):
#   $1 = TOPDIR  (nuttx build dir, holds nuttx.bin after the link)
#   $2 = BOARD_DIR (absolute path to board/bk7258_t5ai)
#
# Exit status: 0 on success, non-zero aborts the build.

set -euo pipefail

TOPDIR="${1:-}"
BOARD_DIR="${2:-}"

if [ -z "${TOPDIR}" ] || [ -z "${BOARD_DIR}" ]; then
    echo "postbuild.sh: ERROR: TOPDIR and BOARD_DIR are required" >&2
    echo "usage: $0 <TOPDIR> <BOARD_DIR>" >&2
    exit 2
fi

# Resolve the openvela workspace root: TOPDIR is <WS>/nuttx, so the
# workspace (which holds the contest repo and the packer tool) is one
# level up.
WS_DIR="$(cd "${TOPDIR}/.." && pwd)"
CONTEST_DIR="${WS_DIR}/contest2026_135_yongwangzhiqian"

NUTTX_BIN="${TOPDIR}/nuttx.bin"
if [ ! -f "${NUTTX_BIN}" ]; then
    echo "postbuild.sh: ERROR: ${NUTTX_BIN} not found (link step failed?)" >&2
    exit 3
fi

BL_CRC_BIN="${BOARD_DIR}/bootloader/bl_crc.bin"
if [ ! -f "${BL_CRC_BIN}" ]; then
    echo "postbuild.sh: ERROR: ${BL_CRC_BIN} not found." >&2
    echo "  Run: make -C ${BOARD_DIR}/bootloader" >&2
    exit 4
fi

# Packer: open-source BK7258 CRC expander (verified equivalent to the
# official cmake_encrypt_crc; see docs/bk7258-t5ai and contest task #17).
PACKER="${WS_DIR}/../TuyaOpen/zephyr-bk7258-port/tools/bk7258_crc_expand_app.py"
if [ ! -f "${PACKER}" ]; then
    # Fallback: the packer also lives inside the contest repo's tool dir.
    PACKER="${CONTEST_DIR}/board/bk7258_t5ai/bootloader/bk7236_pack_min_bootloader.py"
fi
if [ ! -f "${PACKER}" ]; then
    echo "postbuild.sh: ERROR: bk7258_crc_expand_app.py not found" >&2
    exit 5
fi

NUTTX_CRC_BIN="${TOPDIR}/nuttx_crc.bin"
ALL_APP_BIN="${TOPDIR}/all-app.bin"

echo "postbuild.sh: CRC-expanding ${NUTTX_BIN##*/} -> ${NUTTX_CRC_BIN##*/}"
python3 "${PACKER}" --in  "${NUTTX_BIN}" \
                    --out "${NUTTX_CRC_BIN}"

echo "postbuild.sh: concatenating bl_crc.bin + ${NUTTX_CRC_BIN##*/} -> ${ALL_APP_BIN##*/}"
cat "${BL_CRC_BIN}" "${NUTTX_CRC_BIN}" > "${ALL_APP_BIN}"

# Report sizes so the build log self-documents the artifact layout.
BL_SIZE=$(stat -c '%s' "${BL_CRC_BIN}")
APP_SIZE=$(stat -c '%s' "${NUTTX_CRC_BIN}")
ALL_SIZE=$(stat -c '%s' "${ALL_APP_BIN}")
NUTTX_SIZE=$(stat -c '%s' "${NUTTX_BIN}")

echo "postbuild.sh: nuttx.bin       = ${NUTTX_SIZE} bytes"
echo "postbuild.sh: nuttx_crc.bin   = ${APP_SIZE} bytes"
echo "postbuild.sh: bl_crc.bin      = ${BL_SIZE} bytes (physical 0x0 .. 0x$(printf '%x' $((BL_SIZE - 1))))"
echo "postbuild.sh: all-app.bin     = ${ALL_SIZE} bytes (= bl_crc.bin + nuttx_crc.bin)"
echo "postbuild.sh: app magic in nuttx.bin      @ 0x100"
echo "postbuild.sh: app magic in all-app.bin    @ 0x$(printf '%x' $((BL_SIZE + 0x110))) (physical)"

exit 0
