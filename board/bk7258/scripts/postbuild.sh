#!/usr/bin/env bash
#
# SPDX-License-Identifier: Apache-2.0
#
# Role-aware BK7258 post-build packaging.
#
#   CP build -> app.bin, app_crc.bin, legacy nuttx_crc.bin/all-app.bin
#   AP build -> app1.bin, app1_crc.bin
#
# The app/app1 names are the internal adapter contract.  The OpenVela-facing
# role aliases are emitted alongside them; a shared dual-role delivery root
# suppresses the ambiguous generic alias and publishes CP/AP names instead.
#
# The dual-image build script later combines these as sparse BKFIL segments;
# it never pads across the existing LittleFS partition during normal updates.
#
# BK7258_POSTBUILD_MODE defaults to "legacy" for the existing Make/CMake
# adapter.  The canonical role executor must opt into "isolated" and pass a
# complete, private artifact contract explicitly.

set -euo pipefail

TOPDIR="${1:-}"
BOARD_DIR="${2:-}"
ROLE="${3:-cp}"
POSTBUILD_MODE="${BK7258_POSTBUILD_MODE:-legacy}"

if [ -z "${TOPDIR}" ] || [ -z "${BOARD_DIR}" ]; then
    printf '%s\n' "postbuild.sh: ERROR: TOPDIR and BOARD_DIR are required" >&2
    exit 2
fi

case "${POSTBUILD_MODE}" in
    legacy)
        # Compatibility mode intentionally retains the historical source-tree
        # inputs and output names below.  Do not infer isolated inputs here.
        ;;
    isolated)
        ;;
    *)
        printf 'postbuild.sh: ERROR: unknown postbuild mode %s\n' \
               "${POSTBUILD_MODE}" >&2
        exit 2
        ;;
esac

# Isolated mode treats every path as a capability supplied by the executor.
# GNU stat without -L is lstat(2)-like: a symlink is reported as "symbolic
# link" instead of being followed.  readlink -f additionally rejects a
# symlinked parent and non-canonical spelling, so a path cannot escape the
# role root between validation and use.
lstat_kind()
{
    stat -c '%F' -- "$1" 2>/dev/null
}

require_isolated_path()
{
    local label="$1"
    local path="$2"
    local expected_kind="$3"
    local resolved
    local observed

    if [[ -z "${path}" || "${path}" != /* ]]; then
        printf 'postbuild.sh: ERROR: isolated %s must be an absolute path\n' \
               "${label}" >&2
        exit 3
    fi
    if [[ -L "${path}" ]]; then
        printf 'postbuild.sh: ERROR: isolated %s must not be a symlink: %s\n' \
               "${label}" "${path}" >&2
        exit 3
    fi
    if ! resolved="$(readlink -f -- "${path}" 2>/dev/null)" ||
       [[ "${resolved}" != "${path}" ]]; then
        printf 'postbuild.sh: ERROR: isolated %s path is not exact: %s\n' \
               "${label}" "${path}" >&2
        exit 3
    fi
    if ! observed="$(lstat_kind "${path}")" ||
       [[ "${observed}" != "${expected_kind}" ]]; then
        printf 'postbuild.sh: ERROR: isolated %s must be a %s: %s\n' \
               "${label}" "${expected_kind}" "${path}" >&2
        exit 3
    fi
}

path_is_below()
{
    case "$1" in
        "$2"|"$2"/*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

if [[ "${POSTBUILD_MODE}" == isolated ]]; then
    require_isolated_path "TOPDIR" "${TOPDIR}" directory
    require_isolated_path "BOARD_DIR" "${BOARD_DIR}" directory
    TOPDIR_CANONICAL="${TOPDIR}"
    BOARD_DIR_CANONICAL="${BOARD_DIR}"
    case "${TOPDIR_CANONICAL}" in
        "${BOARD_DIR_CANONICAL}"|"${BOARD_DIR_CANONICAL}"/*)
            printf '%s\n' \
                'postbuild.sh: ERROR: isolated TOPDIR must be outside BOARD_DIR source tree' >&2
            exit 3
            ;;
    esac
    if [[ "${TOPDIR_CANONICAL##*/}" != "${ROLE}" ]]; then
        printf 'postbuild.sh: ERROR: isolated TOPDIR must be the role-private /%s build root: %s\n' \
               "${ROLE}" "${TOPDIR}" >&2
        exit 3
    fi
    if [[ -n "${BK7258_POSTBUILD_ARTIFACT_ROOT:-}" ]]; then
        ARTIFACT_ROOT="${BK7258_POSTBUILD_ARTIFACT_ROOT}"
        require_isolated_path "artifact root" "${ARTIFACT_ROOT}" directory
        if path_is_below "${ARTIFACT_ROOT}" "${BOARD_DIR_CANONICAL}"; then
            printf '%s\n' \
                'postbuild.sh: ERROR: isolated artifact root must be outside BOARD_DIR source tree' >&2
            exit 3
        fi
    else
        ARTIFACT_ROOT="${TOPDIR_CANONICAL}"
    fi
else
    ARTIFACT_ROOT="${TOPDIR}"
fi

if [[ "${POSTBUILD_MODE}" == isolated ]]; then
    printf 'postbuild.sh: mode=isolated role=%s artifact_root=%s\n' \
           "${ROLE}" "${ARTIFACT_ROOT}"
else
    printf '%s\n' 'postbuild.sh: mode=legacy (compatibility)'
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

STANDARD_ROLE_NAME="vela_nuttx_${ROLE}.bin"
STANDARD_GENERIC_NAME="vela_nuttx.bin"
STANDARD_ROLE_MANIFEST_NAME="vela_nuttx_${ROLE}.json"
DUAL_ROLE_MODE="${BK7258_POSTBUILD_DUAL_ROLE:-0}"
case "${DUAL_ROLE_MODE}" in
    0|1)
        ;;
    *)
        printf 'postbuild.sh: ERROR: BK7258_POSTBUILD_DUAL_ROLE must be 0 or 1\n' \
               >&2
        exit 2
        ;;
esac

PARTITION_GENERATOR="${BOARD_DIR}/scripts/gen_bk7258_partitions.py"
if [ ! -f "${PARTITION_GENERATOR}" ]; then
    printf 'postbuild.sh: ERROR: %s not found\n' "${PARTITION_GENERATOR}" >&2
    exit 3
fi
if [[ "${POSTBUILD_MODE}" == isolated ]]; then
    require_isolated_path "partition generator" \
        "${PARTITION_GENERATOR}" "regular file"
fi

PARTITION_CONTRACT_ROOT="${BK7258_PARTITION_CONTRACT_ROOT:-}"
PARTITION_EXPLICIT_INPUT="${BK7258_PARTITION_LAYOUT_SOURCE:-}"
PARTITION_INPUT="${PARTITION_EXPLICIT_INPUT:-${BOARD_DIR}/partitions/bk7258/auto_partitions.csv}"
PARTITION_EXPECTED_ID="${BK7258_PARTITION_LAYOUT_ID:-}"
PARTITION_EXPECTED_SHA256="${BK7258_PARTITION_LAYOUT_SHA256:-}"
if [[ "${POSTBUILD_MODE}" == isolated ]]; then
    if [[ -z "${PARTITION_CONTRACT_ROOT}" ||
          -z "${PARTITION_EXPLICIT_INPUT}" ||
          -z "${PARTITION_EXPECTED_ID}" ||
          -z "${PARTITION_EXPECTED_SHA256}" ]]; then
        printf '%s\n' \
            'postbuild.sh: ERROR: isolated partition contract requires root, source, ID and SHA-256' >&2
        exit 3
    fi
    require_isolated_path "partition contract root" \
        "${PARTITION_CONTRACT_ROOT}" directory
    require_isolated_path "partition source" \
        "${PARTITION_EXPLICIT_INPUT}" "regular file"
    if [[ ! "${PARTITION_EXPECTED_ID}" =~ ^[A-Za-z0-9._-]+$ ]]; then
        printf '%s\n' \
            'postbuild.sh: ERROR: isolated partition layout ID has invalid characters' >&2
        exit 3
    fi
    if [[ ! "${PARTITION_EXPECTED_SHA256}" =~ ^[[:xdigit:]]{64}$ ]]; then
        printf '%s\n' \
            'postbuild.sh: ERROR: isolated partition layout SHA-256 must be 64 hex digits' >&2
        exit 3
    fi
    PARTITION_INPUT="${PARTITION_EXPLICIT_INPUT}"
    PARTITION_HEADER="${PARTITION_CONTRACT_ROOT}/include/arch/board/bk7258_partition_layout.h"
    PARTITION_OUTPUT_DIR="${PARTITION_CONTRACT_ROOT}/generated"
    require_isolated_path "partition header" "${PARTITION_HEADER}" "regular file"
    require_isolated_path "partition generated directory" \
        "${PARTITION_OUTPUT_DIR}" directory
    if ! path_is_below "${PARTITION_CONTRACT_ROOT}" "${TOPDIR_CANONICAL}"; then
        printf '%s\n' \
            'postbuild.sh: ERROR: isolated partition contract root must be below TOPDIR' >&2
        exit 3
    fi
else
    if [[ -n "${PARTITION_CONTRACT_ROOT}" ||
          -n "${PARTITION_EXPLICIT_INPUT}" ||
          -n "${PARTITION_EXPECTED_ID}" ||
          -n "${PARTITION_EXPECTED_SHA256}" ]]; then
        if [[ -z "${PARTITION_CONTRACT_ROOT}" ||
              -z "${PARTITION_EXPLICIT_INPUT}" ||
              -z "${PARTITION_EXPECTED_ID}" ||
              -z "${PARTITION_EXPECTED_SHA256}" ]]; then
            printf '%s\n' \
                'postbuild.sh: ERROR: partition contract root, source, ID and SHA-256 must be supplied together' >&2
            exit 3
        fi
        PARTITION_HEADER="${PARTITION_CONTRACT_ROOT}/include/arch/board/bk7258_partition_layout.h"
        PARTITION_OUTPUT_DIR="${PARTITION_CONTRACT_ROOT}/generated"
        if [[ ! -f "${PARTITION_HEADER}" ||
              ! -d "${PARTITION_OUTPUT_DIR}" ]]; then
            printf 'postbuild.sh: ERROR: incomplete private partition contract: %s\n' \
                "${PARTITION_CONTRACT_ROOT}" >&2
            exit 3
        fi
    else
        PARTITION_HEADER="${BOARD_DIR}/include/bk7258_partition_layout.h"
        PARTITION_OUTPUT_DIR="${BOARD_DIR}/partitions/generated"
    fi
fi
if { [[ -n "${PARTITION_EXPECTED_ID}" ]] &&
     [[ -z "${PARTITION_EXPECTED_SHA256}" ]]; } ||
   { [[ -z "${PARTITION_EXPECTED_ID}" ]] &&
     [[ -n "${PARTITION_EXPECTED_SHA256}" ]]; }; then
    printf '%s\n' \
        'postbuild.sh: ERROR: partition layout ID and SHA-256 must be supplied together' >&2
    exit 3
fi
PARTITION_ARGS=(
    --input "${PARTITION_INPUT}"
    --header "${PARTITION_HEADER}"
    --output-dir "${PARTITION_OUTPUT_DIR}"
)
if [[ -n "${PARTITION_EXPECTED_ID}" ]]; then
    PARTITION_ARGS+=(
        --expect-layout-id "${PARTITION_EXPECTED_ID}"
        --expect-layout-sha256 "${PARTITION_EXPECTED_SHA256}"
    )
fi

python3 "${PARTITION_GENERATOR}" "${PARTITION_ARGS[@]}" --check

# BL2 is stored in its own sparse partition, but BL1 loads its CRC-decoded
# logical bytes into the official RAM execution window before entering it.
# It cannot be concatenated after BL1: the two physical segments are apart.
IS_BL2=0
EXECUTION_BASE_ARG=""
CONFIG_PATH="${TOPDIR}/.config"
if [[ "${POSTBUILD_MODE}" == isolated ]]; then
    require_isolated_path "role config" "${CONFIG_PATH}" "regular file"
fi
if [ "${ROLE}" = "cp" ] && grep -qx 'CONFIG_BK7258_BL2_IMAGE=y' "${CONFIG_PATH}"; then
    PARTITION_ROLE="bl2"
    IS_BL2=1
    EXECUTION_BASE_ARG="--execution-base 0x28020000"
fi

XIP_BASE="$(python3 "${PARTITION_GENERATOR}" \
    "${PARTITION_ARGS[@]}" --get "${PARTITION_ROLE}.xip_start")"
MAX_SIZE="$(python3 "${PARTITION_GENERATOR}" \
    "${PARTITION_ARGS[@]}" --get "${PARTITION_ROLE}.logical_size")"
PHYSICAL_OFFSET="$(python3 "${PARTITION_GENERATOR}" \
    "${PARTITION_ARGS[@]}" --get "${PARTITION_ROLE}.offset")"

# A payload is signed later with a 0x200-byte MCUboot header.  That size keeps
# the 80-entry Cortex-M vector table VTOR-aligned.  Its raw NuttX binary
# therefore begins at slot base + 0x200 and intentionally carries no
# direct-BL1 BK7236 magic at raw byte 0x100.
if grep -qx 'CONFIG_BK7258_MCUBOOT_IMAGE=y' "${CONFIG_PATH}"; then
    XIP_BASE=$(printf '0x%x' "$((XIP_BASE + 0x200))")
    MAX_SIZE=$((MAX_SIZE - 0x200))
    MAGIC_ARG=""
fi

NUTTX_BIN="${TOPDIR}/nuttx.bin"
RAW_BIN="${ARTIFACT_ROOT}/${RAW_NAME}"
CRC_BIN="${ARTIFACT_ROOT}/${CRC_NAME}"
PACKER="${BOARD_DIR}/scripts/bk7258_crc_expand.py"

if [[ "${POSTBUILD_MODE}" == isolated ]]; then
    require_isolated_path "nuttx.bin" "${NUTTX_BIN}" "regular file"
fi
if [ ! -f "${NUTTX_BIN}" ]; then
    printf 'postbuild.sh: ERROR: %s not found\n' "${NUTTX_BIN}" >&2
    exit 4
fi

if [[ "${POSTBUILD_MODE}" == isolated ]]; then
    require_isolated_path "CRC packer" "${PACKER}" "regular file"
fi
if [ ! -f "${PACKER}" ]; then
    printf 'postbuild.sh: ERROR: %s not found\n' "${PACKER}" >&2
    exit 5
fi

ensure_isolated_output()
{
    local label="$1"
    local path="$2"
    local observed

    if [[ -L "${path}" ]]; then
        printf 'postbuild.sh: ERROR: isolated %s output must not be a symlink: %s\n' \
               "${label}" "${path}" >&2
        exit 3
    fi
    if [[ -e "${path}" ]]; then
        if ! observed="$(lstat_kind "${path}")" ||
           [[ "${observed}" != "regular file" ]]; then
            printf 'postbuild.sh: ERROR: isolated %s output must be a regular file: %s\n' \
                   "${label}" "${path}" >&2
            exit 3
        fi
    fi
}

if [[ "${POSTBUILD_MODE}" == isolated ]]; then
    ensure_isolated_output "raw image" "${RAW_BIN}"
    ensure_isolated_output "CRC image" "${CRC_BIN}"
    ensure_isolated_output "CRC manifest" "${CRC_BIN}.json"
    ensure_isolated_output "OpenVela role alias" \
        "${ARTIFACT_ROOT}/${STANDARD_ROLE_NAME}"
    ensure_isolated_output "OpenVela role manifest" \
        "${ARTIFACT_ROOT}/${STANDARD_ROLE_MANIFEST_NAME}"
    ensure_isolated_output "OpenVela generic alias" \
        "${ARTIFACT_ROOT}/${STANDARD_GENERIC_NAME}"
fi

BL_CRC_BIN=""
if [[ "${POSTBUILD_MODE}" == isolated &&
      "${ROLE}" == cp && "${IS_BL2}" = 0 ]]; then
    BL_CRC_BIN="${BK7258_BL1_CRC_BIN:-}"
    if [[ -z "${BL_CRC_BIN}" ]]; then
        printf '%s\n' \
            'postbuild.sh: ERROR: isolated CP requires explicit BK7258_BL1_CRC_BIN' >&2
        exit 6
    fi
    require_isolated_path "BL1 CRC input" "${BL_CRC_BIN}" "regular file"
    if [[ "${BL_CRC_BIN}" == "${BOARD_DIR_CANONICAL}/bootloader/bl_crc.bin" ]]; then
        printf '%s\n' \
            'postbuild.sh: ERROR: isolated CP cannot use the legacy board bootloader/bl_crc.bin fallback' >&2
        exit 6
    fi
    ensure_isolated_output "nuttx CRC image" "${ARTIFACT_ROOT}/nuttx_crc.bin"
    ensure_isolated_output "combined image" "${ARTIFACT_ROOT}/all-app.bin"
fi

cp "${NUTTX_BIN}" "${RAW_BIN}"
cp "${RAW_BIN}" "${ARTIFACT_ROOT}/${STANDARD_ROLE_NAME}"
if [[ "${DUAL_ROLE_MODE}" == 0 ]]; then
    cp "${RAW_BIN}" "${ARTIFACT_ROOT}/${STANDARD_GENERIC_NAME}"
else
    # A shared CP/AP root must never leave a role-ambiguous generic alias.
    rm -f -- "${ARTIFACT_ROOT}/${STANDARD_GENERIC_NAME}"
fi
python3 "${PACKER}" \
    --in "${RAW_BIN}" \
    --out "${CRC_BIN}" \
    --xip-base "${XIP_BASE}" \
    --max-size "${MAX_SIZE}" \
    ${EXECUTION_BASE_ARG} \
    ${MAGIC_ARG}

RAW_SIZE=$(stat -c '%s' "${RAW_BIN}")
CRC_SIZE=$(stat -c '%s' "${CRC_BIN}")

python3 - "${RAW_BIN}" \
    "${ARTIFACT_ROOT}/${STANDARD_ROLE_NAME}" \
    "${ARTIFACT_ROOT}/${STANDARD_GENERIC_NAME}" \
    "${ARTIFACT_ROOT}/${STANDARD_ROLE_MANIFEST_NAME}" \
    "${ROLE}" "${RAW_NAME}" "${DUAL_ROLE_MODE}" <<'PY'
import hashlib
import json
import pathlib
import sys

raw_path = pathlib.Path(sys.argv[1])
role_alias_path = pathlib.Path(sys.argv[2])
generic_path = pathlib.Path(sys.argv[3])
manifest_path = pathlib.Path(sys.argv[4])
role = sys.argv[5]
source_name = sys.argv[6]
dual_role = sys.argv[7] == "1"
raw = raw_path.read_bytes()
role_alias = role_alias_path.read_bytes()
if role_alias != raw:
    raise SystemExit("OpenVela role alias is not byte-exact")
generic_name = None
if not dual_role:
    if generic_path.read_bytes() != raw:
        raise SystemExit("OpenVela generic alias is not byte-exact")
    generic_name = generic_path.name
digest = hashlib.sha256(raw).hexdigest()
entry = {
    "schema": "openvela.nuttx-artifacts/1",
    "version": 1,
    "role": role,
    "dual_core": dual_role,
    "source_file": source_name,
    "role_alias": role_alias_path.name,
    "generic_alias": generic_name,
    "sha256": digest,
    "size": len(raw),
    "byte_exact": True,
}
manifest_path.write_text(
    json.dumps(entry, sort_keys=True, separators=(",", ":")) + "\n",
    encoding="utf-8",
)
PY

printf 'postbuild.sh: role=%s %s=%s bytes %s=%s bytes\n' \
       "${ROLE}" "${RAW_NAME}" "${RAW_SIZE}" "${CRC_NAME}" "${CRC_SIZE}"
printf 'postbuild.sh: %s physical flash segment @ %s length 0x%x\n' \
       "${CRC_NAME}" "${PHYSICAL_OFFSET}" "${CRC_SIZE}"

if [ "${ROLE}" = "cp" ] && [ "${IS_BL2}" = 0 ]; then
    if [[ "${POSTBUILD_MODE}" == isolated ]]; then
        : # BL_CRC_BIN was validated from the isolated artifact contract above.
    else
        BL_CRC_BIN="${BOARD_DIR}/bootloader/bl_crc.bin"
        if [ ! -f "${BL_CRC_BIN}" ]; then
            printf 'postbuild.sh: ERROR: %s not found; rebuild bootloader\n' \
                   "${BL_CRC_BIN}" >&2
            exit 6
        fi
    fi

    cp "${CRC_BIN}" "${ARTIFACT_ROOT}/nuttx_crc.bin"
    cat "${BL_CRC_BIN}" "${CRC_BIN}" > "${ARTIFACT_ROOT}/all-app.bin"
    printf 'postbuild.sh: CP-only all-app.bin=%s bytes\n' \
           "$(stat -c '%s' "${ARTIFACT_ROOT}/all-app.bin")"
else
    if [[ "${POSTBUILD_MODE}" == isolated ]]; then
        ensure_isolated_output "combined image" "${ARTIFACT_ROOT}/all-app.bin"
        ensure_isolated_output "nuttx CRC image" "${ARTIFACT_ROOT}/nuttx_crc.bin"
    fi
    rm -f "${ARTIFACT_ROOT}/all-app.bin" "${ARTIFACT_ROOT}/nuttx_crc.bin"
fi
