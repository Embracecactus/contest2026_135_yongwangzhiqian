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
        PARTITION_ROLE="slot_a_cp"
        MAGIC_ARG="--require-magic"
        ;;
    ap)
        RAW_NAME="app1.bin"
        CRC_NAME="app1_crc.bin"
        PARTITION_ROLE="slot_a_ap"
        MAGIC_ARG=""
        ;;
    *)
        printf 'postbuild.sh: ERROR: unknown role %s\n' "${ROLE}" >&2
        exit 2
        ;;
esac

PARTITION_GENERATOR="${BOARD_DIR}/scripts/gen_bk7258_partitions.py"
if [ ! -f "${PARTITION_GENERATOR}" ]; then
    printf 'postbuild.sh: ERROR: %s not found\n' "${PARTITION_GENERATOR}" >&2
    exit 3
fi

python3 "${PARTITION_GENERATOR}" --check

# BL2 is stored in its own sparse partition, but BL1 loads its CRC-decoded
# logical bytes into the official RAM execution window before entering it.
# It cannot be concatenated after BL1: the two physical segments are apart.
IS_BL2=0
EXECUTION_BASE_ARG=""
if [ "${ROLE}" = "cp" ] && grep -qx 'CONFIG_BK7258_BL2_IMAGE=y' "${TOPDIR}/.config"; then
    PARTITION_ROLE="bl2"
    IS_BL2=1
    EXECUTION_BASE_ARG="--execution-base 0x28020000"
fi

XIP_BASE="$(python3 "${PARTITION_GENERATOR}" --get "${PARTITION_ROLE}.xip_start")"
MAX_SIZE="$(python3 "${PARTITION_GENERATOR}" --get "${PARTITION_ROLE}.logical_size")"
PHYSICAL_OFFSET="$(python3 "${PARTITION_GENERATOR}" --get "${PARTITION_ROLE}.offset")"

# A payload is signed later with a 0x200-byte MCUboot header.  That size keeps
# the 80-entry Cortex-M vector table VTOR-aligned.  Its raw NuttX binary
# therefore begins at slot base + 0x200 and intentionally carries no
# direct-BL1 BK7236 magic at raw byte 0x100.
if grep -qx 'CONFIG_BK7258_MCUBOOT_IMAGE=y' "${TOPDIR}/.config"; then
    XIP_BASE=$(printf '0x%x' "$((XIP_BASE + 0x200))")
    MAX_SIZE=$((MAX_SIZE - 0x200))
    MAGIC_ARG=""
fi

NUTTX_BIN="${TOPDIR}/nuttx.bin"
RAW_BIN="${TOPDIR}/${RAW_NAME}"
CRC_BIN="${TOPDIR}/${CRC_NAME}"
PACKER="${BOARD_DIR}/scripts/bk7258_crc_expand.py"

if [ ! -f "${NUTTX_BIN}" ]; then
    printf 'postbuild.sh: ERROR: %s not found\n' "${NUTTX_BIN}" >&2
    exit 4
fi

if [ ! -f "${PACKER}" ]; then
    printf 'postbuild.sh: ERROR: %s not found\n' "${PACKER}" >&2
    exit 5
fi

cp "${NUTTX_BIN}" "${RAW_BIN}"
python3 "${PACKER}" \
    --in "${RAW_BIN}" \
    --out "${CRC_BIN}" \
    --xip-base "${XIP_BASE}" \
    --max-size "${MAX_SIZE}" \
    ${EXECUTION_BASE_ARG} \
    ${MAGIC_ARG}

RAW_SIZE=$(stat -c '%s' "${RAW_BIN}")
CRC_SIZE=$(stat -c '%s' "${CRC_BIN}")

printf 'postbuild.sh: role=%s %s=%s bytes %s=%s bytes\n' \
       "${ROLE}" "${RAW_NAME}" "${RAW_SIZE}" "${CRC_NAME}" "${CRC_SIZE}"
printf 'postbuild.sh: %s physical flash segment @ %s length 0x%x\n' \
       "${CRC_NAME}" "${PHYSICAL_OFFSET}" "${CRC_SIZE}"

if [ "${ROLE}" = "cp" ] && [ "${IS_BL2}" = 0 ]; then
    BL_CRC_BIN="${BOARD_DIR}/bootloader/bl_crc.bin"
    if [ ! -f "${BL_CRC_BIN}" ]; then
        printf 'postbuild.sh: ERROR: %s not found; rebuild bootloader\n' \
               "${BL_CRC_BIN}" >&2
        exit 6
    fi

    cp "${CRC_BIN}" "${TOPDIR}/nuttx_crc.bin"
    cat "${BL_CRC_BIN}" "${CRC_BIN}" > "${TOPDIR}/all-app.bin"
    printf 'postbuild.sh: CP-only all-app.bin=%s bytes\n' \
           "$(stat -c '%s' "${TOPDIR}/all-app.bin")"
else
    rm -f "${TOPDIR}/all-app.bin" "${TOPDIR}/nuttx_crc.bin"
fi
