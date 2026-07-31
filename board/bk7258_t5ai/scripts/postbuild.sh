#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Role-aware BK7258 post-build packaging.
#
#   CP build -> app.bin, app_crc.bin, legacy nuttx_crc.bin/all-app.bin
#   AP build -> app1.bin, app1_crc.bin
#
# The dual-image build script later combines these as sparse BKFIL segments;
# it never pads across the existing LittleFS partition during normal updates.

set -euo pipefail

TOPDIR="${1:-}"
BOARD_DIR="${2:-}"
ROLE="${3:-cp}"

if [ -z "${TOPDIR}" ] || [ -z "${BOARD_DIR}" ]; then
    printf '%s\n' "postbuild.sh: ERROR: TOPDIR and BOARD_DIR are required" >&2
    exit 2
fi

case "${ROLE}" in
    cp)
        RAW_NAME="app.bin"
        CRC_NAME="app_crc.bin"
        XIP_BASE="0x02010000"
        MAX_SIZE="0x000f0000"
        MAGIC_ARG="--require-magic"
        PHYSICAL_OFFSET="0x00011000"
        ;;
    ap)
        RAW_NAME="app1.bin"
        CRC_NAME="app1_crc.bin"
        XIP_BASE="0x02200000"
        MAX_SIZE="0x00200000"
        MAGIC_ARG=""
        PHYSICAL_OFFSET="0x00220000"
        ;;
    *)
        printf 'postbuild.sh: ERROR: unknown role %s\n' "${ROLE}" >&2
        exit 2
        ;;
esac

NUTTX_BIN="${TOPDIR}/nuttx.bin"
RAW_BIN="${TOPDIR}/${RAW_NAME}"
CRC_BIN="${TOPDIR}/${CRC_NAME}"
PACKER="${BOARD_DIR}/scripts/bk7258_crc_expand.py"

if [ ! -f "${NUTTX_BIN}" ]; then
    printf 'postbuild.sh: ERROR: %s not found\n' "${NUTTX_BIN}" >&2
    exit 3
fi

if [ ! -f "${PACKER}" ]; then
    printf 'postbuild.sh: ERROR: %s not found\n' "${PACKER}" >&2
    exit 4
fi

cp "${NUTTX_BIN}" "${RAW_BIN}"
python3 "${PACKER}" \
    --in "${RAW_BIN}" \
    --out "${CRC_BIN}" \
    --xip-base "${XIP_BASE}" \
    --max-size "${MAX_SIZE}" \
    ${MAGIC_ARG}

RAW_SIZE=$(stat -c '%s' "${RAW_BIN}")
CRC_SIZE=$(stat -c '%s' "${CRC_BIN}")

printf 'postbuild.sh: role=%s %s=%s bytes %s=%s bytes\n' \
       "${ROLE}" "${RAW_NAME}" "${RAW_SIZE}" "${CRC_NAME}" "${CRC_SIZE}"
printf 'postbuild.sh: %s physical flash segment @ %s length 0x%x\n' \
       "${CRC_NAME}" "${PHYSICAL_OFFSET}" "${CRC_SIZE}"

if [ "${ROLE}" = "cp" ]; then
    BL_CRC_BIN="${BOARD_DIR}/bootloader/bl_crc.bin"
    if [ ! -f "${BL_CRC_BIN}" ]; then
        printf 'postbuild.sh: ERROR: %s not found; rebuild bootloader\n' \
               "${BL_CRC_BIN}" >&2
        exit 5
    fi

    cp "${CRC_BIN}" "${TOPDIR}/nuttx_crc.bin"
    cat "${BL_CRC_BIN}" "${CRC_BIN}" > "${TOPDIR}/all-app.bin"
    printf 'postbuild.sh: CP-only all-app.bin=%s bytes\n' \
           "$(stat -c '%s' "${TOPDIR}/all-app.bin")"
else
    rm -f "${TOPDIR}/all-app.bin" "${TOPDIR}/nuttx_crc.bin"
fi
