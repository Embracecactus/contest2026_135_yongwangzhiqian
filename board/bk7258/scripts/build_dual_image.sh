#!/usr/bin/env bash
# Build CP app.bin and CPU1 AP app1.bin, then restore the CP build tree.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONTEST_DIR="$(cd "${BOARD_DIR}/../.." && pwd)"
WORKSPACE="$(cd "${CONTEST_DIR}/.." && pwd)"
TOPDIR="${WORKSPACE}/nuttx"
BUILD="${WORKSPACE}/build.sh"
PARTITION_GENERATOR="${SCRIPT_DIR}/gen_bk7258_partitions.py"
# A worktree validation can point this at a temporary config mirror whose
# custom board/chip paths resolve to that worktree.  Normal builds keep using
# the established vendor board-config mirror.
CONFIG_ROOT="${BK7258_CONFIG_ROOT:-${WORKSPACE}/vendor/openvela/boards/contest2026_135_bk7258/configs}"
if [[ ! -d "${CONFIG_ROOT}" ]]; then
    printf 'build_dual_image: missing BK7258 config root: %s\n' \
        "${CONFIG_ROOT}" >&2
    exit 2
fi
CP_CONFIG_NAME="${CP_CONFIG_NAME:-cp_nsh}"
case "${CP_CONFIG_NAME}" in
    cp_nsh|cp_nsh_manual|cp_nsh_rptun|cp_nsh_btipc|cp_nsh_ble_gatt|cp_nsh_psram|cp_nsh_wifi|cp_nsh_mcuboot|cp_nsh_drivercheck|cp_nsh_drivercheck_mcuboot)
        ;;
    *)
        printf 'build_dual_image: unsupported CP_CONFIG_NAME=%s\n' \
            "${CP_CONFIG_NAME}" >&2
        exit 2
        ;;
esac
CP_CONFIG="${CONFIG_ROOT}/${CP_CONFIG_NAME}"
AP_CONFIG_NAME="${AP_CONFIG_NAME:-ap_smp}"
case "${AP_CONFIG_NAME}" in
    ap_up|ap_smp|ap_smp_online|ap_smp_affinity|ap_smp_semwake|ap_smp_semwake_loop|ap_smp_bidir|ap_smp_dualtask|ap_smp_migration|ap_smp_timedwait|ap_smp_lifecycle|ap_smp_rptun|ap_smp_btipc|ap_smp_ble_gatt|ap_smp_psram|ap_smp_wifi|ap_smp_mcuboot|ap_smp_drivercheck|ap_smp_drivercheck_mcuboot)
        ;;
    *)
        printf 'build_dual_image: unsupported AP_CONFIG_NAME=%s\n' \
            "${AP_CONFIG_NAME}" >&2
        exit 2
        ;;
esac
AP_CONFIG="${CONFIG_ROOT}/${AP_CONFIG_NAME}"

if [[ "${CP_CONFIG_NAME}" == *_mcuboot ||
      "${AP_CONFIG_NAME}" == *_mcuboot ]]; then
    if [[ "${CP_CONFIG_NAME}" != *_mcuboot ||
          "${AP_CONFIG_NAME}" != *_mcuboot ]]; then
        printf '%s\n' \
            'build_dual_image: MCUboot AP-start configs must be selected as a pair' \
            >&2
        exit 2
    fi
fi

if [[ "${CP_CONFIG_NAME}" == cp_nsh_drivercheck* ||
      "${AP_CONFIG_NAME}" == ap_smp_drivercheck* ]]; then
    case "${CP_CONFIG_NAME}:${AP_CONFIG_NAME}" in
        cp_nsh_drivercheck:ap_smp_drivercheck|\
        cp_nsh_drivercheck_mcuboot:ap_smp_drivercheck_mcuboot)
            ;;
        *)
            printf '%s\n' \
                'build_dual_image: matching AP driver-check configs must be selected as a pair' \
                >&2
            exit 2
            ;;
    esac
fi

# MCUboot is an explicit, signed build profile.  Do not silently turn a
# cp_nsh_mcuboot/ap_smp_mcuboot build into the unsigned raw-image pipeline:
# the signing key and BL1 authorization key must be supplied from outside the
# repository (normally from /tmp or a developer-owned secure key store).
MCUBOOT_PROFILE=false
MCUBOOT_SIGNING_KEY="${MCUBOOT_SIGNING_KEY:-}"
MCUBOOT_VERSION="${MCUBOOT_VERSION:-}"
MCUBOOT_SECURITY_COUNTER="${MCUBOOT_SECURITY_COUNTER:-auto}"
MCUBOOT_OFFICIAL_PIPELINE="${MCUBOOT_OFFICIAL_PIPELINE:-YES}"
SECUREBOOT_AES_TOOL="${SECUREBOOT_AES_TOOL:-}"
SECUREBOOT_AES_KEY_FILE="${SECUREBOOT_AES_KEY_FILE:-}"
BL1_MANIFEST_KEY="${BL1_MANIFEST_KEY:-}"
BL1_MANIFEST_FORMAT="${BL1_MANIFEST_FORMAT:-beken-candidate-v1}"
BL1_MANIFEST_RAW_PAGE="${BL1_MANIFEST_RAW_PAGE:-false}"
BL2_LOGICAL_SIZE="${BL2_LOGICAL_SIZE:-0x3000}"
BL2_SECURITY_COUNTER_FLOOR="${BL2_SECURITY_COUNTER_FLOOR:-0}"
BL2_XIP_ADDRESS="${BL2_XIP_ADDRESS:-0x024d0000}"
BL2_SECONDARY_XIP_ADDRESS="${BL2_SECONDARY_XIP_ADDRESS:-0x024f0000}"
BL2_LOAD_ADDRESS="${BL2_LOAD_ADDRESS:-0x28020000}"
MCUBOOT_BL2_FLASH_SEGMENT=
case "${BL1_MANIFEST_RAW_PAGE}" in
    false)
        BL1_MANIFEST_RAW_PAGE_VALUE=0
        ;;
    true)
        BL1_MANIFEST_RAW_PAGE_VALUE=1
        ;;
    *)
        printf "build_dual_image: BL1_MANIFEST_RAW_PAGE must be false or true, got '%s'\n" \
            "${BL1_MANIFEST_RAW_PAGE}" >&2
        exit 2
        ;;
esac
if [[ "${CP_CONFIG_NAME}" == *_mcuboot ]]; then
    MCUBOOT_PROFILE=true
    MCUBOOT_BL2_FLASH_SEGMENT="bl2_crc.bin@0x51d000,bl2_secondary_crc.bin@0x53f000"
    if [[ -z "${MCUBOOT_SIGNING_KEY}" || ! -f "${MCUBOOT_SIGNING_KEY}" ]]; then
        printf '%s\n' \
            'build_dual_image: a *_mcuboot CP config requires an external MCUBOOT_SIGNING_KEY' \
            >&2
        exit 2
    fi
    if [[ -z "${MCUBOOT_VERSION}" ]]; then
        printf '%s\n' \
            'build_dual_image: a *_mcuboot CP config requires MCUBOOT_VERSION' \
            >&2
        exit 2
    fi
    if [[ -z "${BL1_MANIFEST_KEY}" || ! -f "${BL1_MANIFEST_KEY}" ]]; then
        printf '%s\n' \
            'build_dual_image: a *_mcuboot CP config requires an external BL1_MANIFEST_KEY' \
            >&2
        exit 2
    fi
    case "${BL1_MANIFEST_FORMAT}" in
        custom-v2|beken-candidate-v1)
            ;;
        *)
            printf 'build_dual_image: unsupported BL1_MANIFEST_FORMAT=%s\n' \
                "${BL1_MANIFEST_FORMAT}" >&2
            exit 2
            ;;
    esac
    if [[ "${BL1_MANIFEST_RAW_PAGE}" == "true" &&
          "${BL1_MANIFEST_FORMAT}" != "beken-candidate-v1" ]]; then
        printf '%s\n' \
            'build_dual_image: BL1_MANIFEST_RAW_PAGE requires beken-candidate-v1' \
            >&2
        exit 2
    fi
    if ! [[ "${BL2_SECURITY_COUNTER_FLOOR}" =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]]; then
        printf "build_dual_image: BL2_SECURITY_COUNTER_FLOOR must be an integer, got '%s'\n" \
            "${BL2_SECURITY_COUNTER_FLOOR}" >&2
        exit 2
    fi
    case "${MCUBOOT_OFFICIAL_PIPELINE}" in
        NO|YES)
            ;;
        *)
            printf "build_dual_image: MCUBOOT_OFFICIAL_PIPELINE must be YES or NO, got '%s'\n" \
                "${MCUBOOT_OFFICIAL_PIPELINE}" >&2
            exit 2
            ;;
    esac
    if [[ -n "${SECUREBOOT_AES_TOOL}" || -n "${SECUREBOOT_AES_KEY_FILE}" ]]; then
        if [[ "${MCUBOOT_OFFICIAL_PIPELINE}" != "YES" ]]; then
            printf '%s\n' \
                'build_dual_image: SECUREBOOT_AES_* requires MCUBOOT_OFFICIAL_PIPELINE=YES' \
                >&2
            exit 2
        fi
        if [[ -z "${SECUREBOOT_AES_TOOL}" ||
              ! -f "${SECUREBOOT_AES_TOOL}" ||
              -z "${SECUREBOOT_AES_KEY_FILE}" ||
              ! -f "${SECUREBOOT_AES_KEY_FILE}" ]]; then
            printf '%s\n' \
                'build_dual_image: SECUREBOOT_AES_TOOL and SECUREBOOT_AES_KEY_FILE must both name existing files' \
                >&2
            exit 2
        fi
    fi
fi

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_rptun" ||
      "${AP_CONFIG_NAME}" == "ap_smp_rptun" ]]; then
    if [[ "${CP_CONFIG_NAME}" != "cp_nsh_rptun" ||
          "${AP_CONFIG_NAME}" != "ap_smp_rptun" ]]; then
        printf '%s\n' \
            'build_dual_image: N9 RPTUN layout configs must be selected as a pair' \
            >&2
        exit 2
    fi
fi

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_btipc" ||
      "${AP_CONFIG_NAME}" == "ap_smp_btipc" ]]; then
    if [[ "${CP_CONFIG_NAME}" != "cp_nsh_btipc" ||
          "${AP_CONFIG_NAME}" != "ap_smp_btipc" ]]; then
        printf '%s\n' \
            'build_dual_image: N12 Bluetooth IPC configs must be selected as a pair' \
            >&2
        exit 2
    fi
fi

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_ble_gatt" ||
      "${AP_CONFIG_NAME}" == "ap_smp_ble_gatt" ]]; then
    if [[ "${CP_CONFIG_NAME}" != "cp_nsh_ble_gatt" ||
          "${AP_CONFIG_NAME}" != "ap_smp_ble_gatt" ]]; then
        printf '%s\n' \
            'build_dual_image: N13 BLE GATT configs must be selected as a pair' \
            >&2
        exit 2
    fi
fi

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_wifi" ||
      "${AP_CONFIG_NAME}" == "ap_smp_wifi" ]]; then
    if [[ "${CP_CONFIG_NAME}" != "cp_nsh_wifi" ||
          "${AP_CONFIG_NAME}" != "ap_smp_wifi" ]]; then
        printf '%s\n' \
            'build_dual_image: N16 Wi-Fi configs must be selected as a pair' \
            >&2
        exit 2
    fi
fi

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_psram" ||
      "${AP_CONFIG_NAME}" == "ap_smp_psram" ]]; then
    if [[ "${CP_CONFIG_NAME}" != "cp_nsh_psram" ||
          "${AP_CONFIG_NAME}" != "ap_smp_psram" ]]; then
        printf '%s\n' \
            'build_dual_image: N14 PSRAM configs must be selected as a pair' \
            >&2
        exit 2
    fi
fi
JOBS="${JOBS:-8}"
OUTPUT="${TOPDIR}/bk7258-dual"
BK7258_SDK_BUNDLE_VERSION="${BK7258_SDK_BUNDLE_VERSION:-v3.1.1.9}"

if [[ "${BK7258_SDK_BUNDLE_VERSION}" != "v3.1.1.9" ]]; then
    printf "build_dual_image: project requires official SDK v3.1.1.9, got '%s'\n" \
        "${BK7258_SDK_BUNDLE_VERSION}" >&2
    exit 2
fi

SDK_BUNDLE_BASE="${BOARD_DIR}/bk_idk/armino_as_lib"
SDK_BUNDLE_ROOT="${SDK_BUNDLE_BASE}/versions/${BK7258_SDK_BUNDLE_VERSION}"
SDK_MANIFEST_DIR="${SCRIPT_DIR}/sdk-manifests/${BK7258_SDK_BUNDLE_VERSION}"
export BK7258_SDK_BUNDLE_VERSION
TMPDIR="$(mktemp -d)"

cleanup()
{
    rm -rf "${TMPDIR}"
}
trap cleanup EXIT

for role in cp ap; do
    "${SCRIPT_DIR}/setup_bk7258_sdk.sh" --check \
        --version "${BK7258_SDK_BUNDLE_VERSION}" --role "${role}"
done

CP_SDK_ROLE_DIR="$(readlink -f "${SDK_BUNDLE_ROOT}/cp")"
AP_SDK_ROLE_DIR="$(readlink -f "${SDK_BUNDLE_ROOT}/ap")"
CP_SDK_MANIFEST="${SDK_MANIFEST_DIR}/cp.sha256"
AP_SDK_MANIFEST="${SDK_MANIFEST_DIR}/ap.sha256"
CP_SDK_MANIFEST_SHA256="$(sha256sum "${CP_SDK_MANIFEST}" | awk '{print $1}')"
AP_SDK_MANIFEST_SHA256="$(sha256sum "${AP_SDK_MANIFEST}" | awk '{print $1}')"

file_sha256_or_missing()
{
    local path="$1"

    if [[ -f "${path}" ]]; then
        sha256sum "${path}" | awk '{print $1}'
    else
        printf 'missing\n'
    fi
}

CP_SDK_PROVENANCE_SHA256="$(
    file_sha256_or_missing "${SDK_MANIFEST_DIR}/cp.provenance"
)"
AP_SDK_PROVENANCE_SHA256="$(
    file_sha256_or_missing "${SDK_MANIFEST_DIR}/ap.provenance"
)"

build_config()
{
    local config="$1"
    "${BUILD}" "${config}" distclean
    "${BUILD}" "${config}" "-j${JOBS}"
}

save_role()
{
    local role="$1"
    local raw="$2"
    local crc="$3"

    cp "${TOPDIR}/${raw}" "${TMPDIR}/${raw}"
    cp "${TOPDIR}/${crc}" "${TMPDIR}/${crc}"
    cp "${TOPDIR}/nuttx" "${TMPDIR}/nuttx-${role}.elf"
    cp "${TOPDIR}/.config" "${TMPDIR}/nuttx-${role}.config"
    if [ -f "${TOPDIR}/nuttx.map" ]; then
        cp "${TOPDIR}/nuttx.map" "${TMPDIR}/nuttx-${role}.map"
    fi
}

printf 'build_dual_image: SDK bundle version: %s\n' \
    "${BK7258_SDK_BUNDLE_VERSION}"
PARTITION_GENERATE_ARGS=(--check)
PARTITION_VERIFY_ARGS=(
    --output "${TMPDIR}/bk7258-partitions.json"
)
if [[ -n "${BK7258_SDK_SOURCE:-}" ]]; then
    PARTITION_GENERATE_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
    PARTITION_VERIFY_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
fi
python3 "${PARTITION_GENERATOR}" \
    "${PARTITION_GENERATE_ARGS[@]}"
python3 "${SCRIPT_DIR}/verify_bk7258_partitions.py" \
    "${PARTITION_VERIFY_ARGS[@]}"
python3 "${SCRIPT_DIR}/verify_bk7258_sdk_partition_wrapper.py"
printf '%s\n' "build_dual_image: rebuilding Tier-1 bootloader"

# Build the SRAM MCUboot BL2 and its two board-owned BL1 authorization records
# before linking Tier-1.  The records occupy the fixed bootloader tail; the
# primary and secondary BL2 copies are separate CRC streams in the
# pre-LittleFS gap.  Neither step writes OTP/eFuse.
BL1_BOOT_ARGS=(
    "BL1_MANIFEST_RAW_PAGE=${BL1_MANIFEST_RAW_PAGE_VALUE}"
)
if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    printf '%s\n' "build_dual_image: building MCUboot BL2"
    BL2_KEY_SOURCE="${TMPDIR}/bk7258_bl2_keys.c"
    python3 "${WORKSPACE}/apps/boot/mcuboot/mcuboot/scripts/imgtool.py" \
        getpub --key "${MCUBOOT_SIGNING_KEY}" --encoding lang-c \
        > "${BL2_KEY_SOURCE}"
    printf '%s\n' \
        '#include <bootutil/sign_key.h>' \
        'const struct bootutil_key bootutil_keys[] =' \
        '{' \
        '  { .key = ecdsa_pub_key, .len = &ecdsa_pub_key_len },' \
        '};' \
        'const int bootutil_key_cnt = 1;' \
        >> "${BL2_KEY_SOURCE}"
    make -C "${BOARD_DIR}/bootloader/bl2" clean all \
        "BL2_LOGICAL_SIZE=${BL2_LOGICAL_SIZE}" \
        "BL2_SECURITY_COUNTER_FLOOR=${BL2_SECURITY_COUNTER_FLOOR}" \
        "BL2_KEY_SOURCE=${BL2_KEY_SOURCE}"
    BL1_MANIFEST_CONTAINER_ARGS=()
    if [[ "${BL1_MANIFEST_RAW_PAGE}" == "true" ]]; then
        BL1_MANIFEST_CONTAINER_ARGS+=(--container-size 0x1000)
    fi
    python3 "${BOARD_DIR}/bootloader/make_bl1_manifest.py" \
        --format "${BL1_MANIFEST_FORMAT}" \
        --bl2 "${BOARD_DIR}/bootloader/bl2/bl2.bin" \
        --private-key "${BL1_MANIFEST_KEY}" \
        --generated-root-c "${TMPDIR}/boot_bl1_manifest_key.c" \
        --bl2-xip "${BL2_XIP_ADDRESS}" \
        --bl2-size "${BL2_LOGICAL_SIZE}" \
        --bl2-load "${BL2_LOAD_ADDRESS}" \
        "${BL1_MANIFEST_CONTAINER_ARGS[@]}" \
        --out "${TMPDIR}/bl1-manifest-primary.bin"
    python3 "${BOARD_DIR}/bootloader/make_bl1_manifest.py" \
        --format "${BL1_MANIFEST_FORMAT}" \
        --bl2 "${BOARD_DIR}/bootloader/bl2/bl2.bin" \
        --private-key "${BL1_MANIFEST_KEY}" \
        --generated-root-c "${TMPDIR}/boot_bl1_manifest_key.c" \
        --bl2-xip "${BL2_SECONDARY_XIP_ADDRESS}" \
        --bl2-size "${BL2_LOGICAL_SIZE}" \
        --bl2-load "${BL2_LOAD_ADDRESS}" \
        "${BL1_MANIFEST_CONTAINER_ARGS[@]}" \
        --out "${TMPDIR}/bl1-manifest-secondary.bin"
    cp "${TMPDIR}/bl1-manifest-primary.bin" "${TMPDIR}/bl1-manifest.bin"
    BL1_BOOT_ARGS+=(
        'BL1_MANIFEST_ENFORCE=1'
        "BL1_MANIFEST_PRIMARY=${TMPDIR}/bl1-manifest-primary.bin"
        "BL1_MANIFEST_SECONDARY=${TMPDIR}/bl1-manifest-secondary.bin"
        "BL1_MANIFEST_KEY_SOURCE=${TMPDIR}/boot_bl1_manifest_key.c"
        "BL2_LOGICAL_SIZE=${BL2_LOGICAL_SIZE}"
    )
fi
BOOT_MAKE_TARGETS=(clean all verify)
make -C "${BOARD_DIR}/bootloader" "${BOOT_MAKE_TARGETS[@]}" \
    "${BL1_BOOT_ARGS[@]}"
cp "${BOARD_DIR}/bootloader/bl.elf" "${TMPDIR}/bootloader.elf"
cp "${BOARD_DIR}/bootloader/bl.bin" "${TMPDIR}/bootloader.bin"
cp "${BOARD_DIR}/bootloader/bl.map" "${TMPDIR}/bootloader.map"
if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    cp "${BOARD_DIR}/bootloader/bl_crc.bin" "${TMPDIR}/bootloader_crc.bin"
    cp "${BOARD_DIR}/bootloader/bl_crc.json" "${TMPDIR}/bootloader_crc.json"
    cp "${BOARD_DIR}/bootloader/bl2/bl2.bin" "${TMPDIR}/bl2.bin"
    cp "${BOARD_DIR}/bootloader/bl2/bl2.elf" "${TMPDIR}/bl2.elf"
    cp "${BOARD_DIR}/bootloader/bl2/bl2.map" "${TMPDIR}/bl2.map"
    cp "${BOARD_DIR}/bootloader/bl2/bl2_crc.bin" "${TMPDIR}/bl2_crc.bin"
    cp "${BOARD_DIR}/bootloader/bl2/bl2_crc.bin" "${TMPDIR}/bl2_secondary_crc.bin"
    cp "${BOARD_DIR}/bootloader/bl2/bl2_crc.bin.json" "${TMPDIR}/bl2_crc.bin.json"
fi

printf 'build_dual_image: building CPU0/CP (%s)\n' "${CP_CONFIG_NAME}"
build_config "${CP_CONFIG}"
save_role cp app.bin app_crc.bin

printf 'build_dual_image: building physical CPU1/AP (%s)\n' \
    "${AP_CONFIG_NAME}"
build_config "${AP_CONFIG}"
save_role ap app1.bin app1_crc.bin

printf '%s\n' "build_dual_image: restoring CPU0/CP build tree"
build_config "${CP_CONFIG}"

# The restored CP build is authoritative for both the normal build tree and
# the dual-image package.  Overwrite the first CP snapshot so app.bin,
# app_crc.bin, nuttx_crc.bin, all-app.bin, the saved CP ELF and the manifest
# cannot describe two timestamp-distinct CP builds.

save_role cp app.bin app_crc.bin

if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    printf '%s\n' "build_dual_image: signing CP/AP with pinned NuttX MCUboot imgtool"
    cp "${TMPDIR}/app.bin" "${TMPDIR}/cp-raw.bin"
    cp "${TMPDIR}/app_crc.bin" "${TMPDIR}/cp-raw-crc.bin"
    cp "${TMPDIR}/app1.bin" "${TMPDIR}/ap-raw.bin"
    cp "${TMPDIR}/app1_crc.bin" "${TMPDIR}/ap-raw-crc.bin"
    MCUBOOT_PAIR_OUTPUT="${TMPDIR}/mcuboot-pair"
    python3 "${SCRIPT_DIR}/pack_bk7258_mcuboot_pair.py" \
        --cp-raw "${TMPDIR}/cp-raw.bin" \
        --ap-raw "${TMPDIR}/ap-raw.bin" \
        --key "${MCUBOOT_SIGNING_KEY}" \
        --output "${MCUBOOT_PAIR_OUTPUT}" \
        --version "${MCUBOOT_VERSION}" \
        --security-counter "${MCUBOOT_SECURITY_COUNTER}"
    cp "${MCUBOOT_PAIR_OUTPUT}/cp_signed.bin" "${TMPDIR}/app.bin"
    cp "${MCUBOOT_PAIR_OUTPUT}/cp_signed_crc.bin" "${TMPDIR}/app_crc.bin"
    cp "${MCUBOOT_PAIR_OUTPUT}/ap_signed.bin" "${TMPDIR}/app1.bin"
    cp "${MCUBOOT_PAIR_OUTPUT}/ap_signed_crc.bin" "${TMPDIR}/app1_crc.bin"
    if [[ "${MCUBOOT_OFFICIAL_PIPELINE}" == "YES" ]]; then
        printf '%s\n' \
            "build_dual_image: emitting v3.1.1.9 merge/sign/AES/CRC reference"
        SECUREBOOT_PIPELINE_OUTPUT="${TMPDIR}/secureboot-pipeline"
        SECUREBOOT_AES_ARGS=()
        if [[ -n "${SECUREBOOT_AES_TOOL}" ]]; then
            SECUREBOOT_AES_ARGS+=(
                --aes-tool "${SECUREBOOT_AES_TOOL}"
                --aes-key-file "${SECUREBOOT_AES_KEY_FILE}"
            )
        fi
        python3 "${SCRIPT_DIR}/pack_bk7258_secureboot.py" \
            --cp-raw "${TMPDIR}/cp-raw.bin" \
            --ap-raw "${TMPDIR}/ap-raw.bin" \
            --key "${MCUBOOT_SIGNING_KEY}" \
            --imgtool "${WORKSPACE}/apps/boot/mcuboot/mcuboot/scripts/imgtool.py" \
            --output "${SECUREBOOT_PIPELINE_OUTPUT}" \
            --version "${MCUBOOT_VERSION}" \
            --security-counter "${MCUBOOT_SECURITY_COUNTER}" \
            "${SECUREBOOT_AES_ARGS[@]}" \
            > "${TMPDIR}/secureboot-pipeline.json"
    fi
fi

rm -rf "${OUTPUT}"
mkdir -p "${OUTPUT}"
cp "${TMPDIR}"/nuttx-*.elf "${OUTPUT}/"
cp "${TMPDIR}"/nuttx-*.config "${OUTPUT}/"
cp "${TMPDIR}"/bootloader.elf "${OUTPUT}/"
cp "${TMPDIR}"/bootloader.bin "${OUTPUT}/"
cp "${TMPDIR}"/bootloader.map "${OUTPUT}/"
cp "${TMPDIR}/bk7258-partitions.json" "${OUTPUT}/"
for map in "${TMPDIR}"/nuttx-*.map; do
    if [ -f "${map}" ]; then
        cp "${map}" "${OUTPUT}/"
    fi
done
if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    cp "${TMPDIR}/cp-raw.bin" "${OUTPUT}/cp-raw.bin"
    cp "${TMPDIR}/cp-raw-crc.bin" "${OUTPUT}/cp-raw-crc.bin"
    cp "${TMPDIR}/ap-raw.bin" "${OUTPUT}/ap-raw.bin"
    cp "${TMPDIR}/ap-raw-crc.bin" "${OUTPUT}/ap-raw-crc.bin"
    cp "${TMPDIR}/bl1-manifest.bin" "${OUTPUT}/bl1-manifest.bin"
    cp "${TMPDIR}/bl1-manifest-primary.bin" "${OUTPUT}/bl1-manifest-primary.bin"
    cp "${TMPDIR}/bl1-manifest-secondary.bin" "${OUTPUT}/bl1-manifest-secondary.bin"
    cp "${TMPDIR}/boot_bl1_manifest_key.c" "${OUTPUT}/boot_bl1_manifest_key.c"
    cp "${TMPDIR}/bl2.bin" "${OUTPUT}/bl2.bin"
    cp "${TMPDIR}/bl2.elf" "${OUTPUT}/bl2.elf"
    cp "${TMPDIR}/bl2.map" "${OUTPUT}/bl2.map"
    cp "${TMPDIR}/bl2_crc.bin" "${OUTPUT}/bl2_crc.bin"
    cp "${TMPDIR}/bl2_secondary_crc.bin" "${OUTPUT}/bl2_secondary_crc.bin"
    cp "${TMPDIR}/bl2_crc.bin.json" "${OUTPUT}/bl2_crc.bin.json"
    cp "${TMPDIR}/bootloader_crc.json" "${OUTPUT}/bootloader_crc.json"
    cp "${TMPDIR}/mcuboot-pair"/* "${OUTPUT}/"
    if [[ "${MCUBOOT_OFFICIAL_PIPELINE}" == "YES" ]]; then
        cp "${TMPDIR}/secureboot-pipeline"/* "${OUTPUT}/"
    fi
fi

PACK_DUAL_ARGS=(
    --boot "${BOARD_DIR}/bootloader/bl_crc.bin"
    --cp-raw "${TMPDIR}/app.bin"
    --cp-crc "${TMPDIR}/app_crc.bin"
    --ap-raw "${TMPDIR}/app1.bin"
    --ap-crc "${TMPDIR}/app1_crc.bin"
    --output "${OUTPUT}"
)
if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    PACK_DUAL_ARGS+=(
        --bl2-primary-crc "${TMPDIR}/bl2_crc.bin"
        --bl2-secondary-crc "${TMPDIR}/bl2_secondary_crc.bin"
    )
fi
python3 "${SCRIPT_DIR}/pack_dual_image.py" "${PACK_DUAL_ARGS[@]}"

if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    # Keep the root NuttX artifacts consistent with the signed package.  The
    # CP/AP ELF and .config files remain the unsigned build inputs; only the
    # flash-facing binaries are replaced by the deterministic imgtool output.
    cp "${TMPDIR}/app.bin" "${TOPDIR}/app.bin"
    cp "${TMPDIR}/app_crc.bin" "${TOPDIR}/app_crc.bin"
    cp "${TMPDIR}/app1.bin" "${TOPDIR}/app1.bin"
    cp "${TMPDIR}/app1_crc.bin" "${TOPDIR}/app1_crc.bin"
    cp "${TMPDIR}/app_crc.bin" "${TOPDIR}/nuttx_crc.bin"
    cat "${BOARD_DIR}/bootloader/bl_crc.bin" "${TOPDIR}/app_crc.bin" \
        > "${TOPDIR}/all-app.bin"
fi

python3 "${SCRIPT_DIR}/verify_bk7258_factory_layout.py" \
    --package "${OUTPUT}" \
    --json "${OUTPUT}/bk7258-factory-layout.json"

cat > "${OUTPUT}/build-profile.txt" <<EOF
CP_CONFIG_NAME=${CP_CONFIG_NAME}
AP_CONFIG_NAME=${AP_CONFIG_NAME}
CP_CONFIG=${CP_CONFIG}
AP_CONFIG=${AP_CONFIG}
BK7258_SDK_BUNDLE_VERSION=${BK7258_SDK_BUNDLE_VERSION}
BK7258_SDK_BUNDLE_ROOT=${SDK_BUNDLE_ROOT}
CP_SDK_ROLE_DIR=${CP_SDK_ROLE_DIR}
AP_SDK_ROLE_DIR=${AP_SDK_ROLE_DIR}
CP_SDK_MANIFEST=${CP_SDK_MANIFEST}
AP_SDK_MANIFEST=${AP_SDK_MANIFEST}
CP_SDK_MANIFEST_SHA256=${CP_SDK_MANIFEST_SHA256}
AP_SDK_MANIFEST_SHA256=${AP_SDK_MANIFEST_SHA256}
CP_SDK_PROVENANCE_SHA256=${CP_SDK_PROVENANCE_SHA256}
AP_SDK_PROVENANCE_SHA256=${AP_SDK_PROVENANCE_SHA256}
MCUBOOT_PROFILE=${MCUBOOT_PROFILE}
MCUBOOT_VERSION=${MCUBOOT_VERSION}
MCUBOOT_SECURITY_COUNTER=${MCUBOOT_SECURITY_COUNTER}
MCUBOOT_OFFICIAL_PIPELINE=${MCUBOOT_OFFICIAL_PIPELINE}
MCUBOOT_SIGNING_KEY_REQUIRED=${MCUBOOT_PROFILE}
BL1_MANIFEST_ENABLED=${MCUBOOT_PROFILE}
BL1_MANIFEST_FORMAT=${BL1_MANIFEST_FORMAT}
BL1_MANIFEST_RAW_PAGE=${BL1_MANIFEST_RAW_PAGE}
BL2_LOGICAL_SIZE=${BL2_LOGICAL_SIZE}
BL2_SECURITY_COUNTER_FLOOR=${BL2_SECURITY_COUNTER_FLOOR}
BL2_BOOT_POLICY_ENABLED=${MCUBOOT_PROFILE}
BL1_BOOT_POLICY=primary-then-secondary
BL2_XIP_ADDRESS=${BL2_XIP_ADDRESS}
BL2_SECONDARY_XIP_ADDRESS=${BL2_SECONDARY_XIP_ADDRESS}
BL2_LOAD_ADDRESS=${BL2_LOAD_ADDRESS}
BL2_FLASH_SEGMENT=${MCUBOOT_BL2_FLASH_SEGMENT}
EOF

cp "${OUTPUT}/app.bin" "${TOPDIR}/app.bin"
cp "${OUTPUT}/app_crc.bin" "${TOPDIR}/app_crc.bin"
cp "${OUTPUT}/app1.bin" "${TOPDIR}/app1.bin"
cp "${OUTPUT}/app1_crc.bin" "${TOPDIR}/app1_crc.bin"
cp "${OUTPUT}/bk7258-dual-image.json" "${TOPDIR}/"

if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    # The MCUboot CP profile has no runtime SDK partition call site in its
    # minimal handoff path, so --gc-sections legitimately removes unused
    # wrapper entry points.  The host ABI test above still runs; defer the
    # ELF call-site assertion to a runtime profile that exercises the SDK API.
    printf '%s\n' \
        "build_dual_image: skipping unused SDK wrapper ELF check for MCUboot"
else
    python3 "${SCRIPT_DIR}/verify_bk7258_sdk_partition_wrapper.py" \
        --elf "${OUTPUT}/nuttx-cp.elf" \
        --map "${OUTPUT}/nuttx-cp.map" \
        --output "${OUTPUT}/bk7258-sdk-partition-wrapper.json"
fi

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_rptun" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_btipc" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_ble_gatt" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_psram" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_wifi" ]]; then
    python3 "${SCRIPT_DIR}/verify_bk7258_rptun_layout.py" \
        --cp-elf "${OUTPUT}/nuttx-cp.elf" \
        --cp-map "${OUTPUT}/nuttx-cp.map" \
        --ap-elf "${OUTPUT}/nuttx-ap.elf" \
        --ap-map "${OUTPUT}/nuttx-ap.map" \
        --json "${OUTPUT}/bk7258-rptun-layout.json"
fi

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_ble_gatt" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_psram" ]]; then
    BLE_VERIFY_IDENTITY_ARGS=()
    if [[ "${CP_CONFIG_NAME}" == "cp_nsh_psram" ]]; then
        BLE_VERIFY_IDENTITY_ARGS+=(
            --expected-device-name "BK7258 N14"
            --expected-local-name "BK7258-N14"
        )
    fi

    python3 "${SCRIPT_DIR}/verify_bk7258_ble_gatt.py" \
        --cp-elf "${OUTPUT}/nuttx-cp.elf" \
        --ap-elf "${OUTPUT}/nuttx-ap.elf" \
        --ap-map "${OUTPUT}/nuttx-ap.map" \
        --json "${OUTPUT}/bk7258-ble-gatt.json" \
        "${BLE_VERIFY_IDENTITY_ARGS[@]}"
fi

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_psram" ]]; then
    PSRAM_VERIFY_ARGS=(
        --cp-elf "${OUTPUT}/nuttx-cp.elf"
        --cp-map "${OUTPUT}/nuttx-cp.map"
        --ap-elf "${OUTPUT}/nuttx-ap.elf"
        --ap-map "${OUTPUT}/nuttx-ap.map"
        --expected-bundle "${BK7258_SDK_BUNDLE_VERSION}"
        --json "${OUTPUT}/bk7258-psram.json"
    )

    # Source verification is deliberately opt-in because the immutable SDK
    # bundle is sufficient to build, while the matching full SDK source tree
    # normally lives outside this repository.  CI and local acceptance can
    # add BK7258_SDK_SOURCE without embedding a developer-specific path.

    if [[ -n "${BK7258_SDK_SOURCE:-}" ]]; then
        PSRAM_VERIFY_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
    fi
    python3 "${SCRIPT_DIR}/verify_bk7258_psram.py" \
        "${PSRAM_VERIFY_ARGS[@]}"
fi

verify_equal()
{
    local expected="$1"
    local actual="$2"

    if ! cmp -s "${expected}" "${actual}"; then
        printf 'build_dual_image: artifact mismatch: %s != %s\n' \
            "${expected}" "${actual}" >&2
        exit 1
    fi
}

cat "${BOARD_DIR}/bootloader/bl_crc.bin" "${TOPDIR}/app_crc.bin" \
    > "${TMPDIR}/all-app-expected.bin"
verify_equal "${OUTPUT}/app.bin" "${TOPDIR}/app.bin"
verify_equal "${OUTPUT}/app_crc.bin" "${TOPDIR}/app_crc.bin"
verify_equal "${TOPDIR}/app_crc.bin" "${TOPDIR}/nuttx_crc.bin"
verify_equal "${TMPDIR}/all-app-expected.bin" "${TOPDIR}/all-app.bin"

printf 'build_dual_image: artifacts: %s\n' "${OUTPUT}"
printf '%s\n' "build_dual_image: root CP artifacts match the manifest CP image"
printf '%s\n' "build_dual_image: root all-app.bin remains CP-only (bootloader + CP)"
printf '%s\n' "build_dual_image: normal split updates preserve LittleFS;"
printf '%s\n' "  use the offset-length segments in bk7258-dual-image.json"
printf '%s\n' "build_dual_image: destructive factory rewrite requires fresh owner authorization"
printf '%s\n' "build_dual_image: factory ranges preserve usr_config/reserved/tail"
printf '  all-app-factory.bin@%s-%s + littlefs_factory_clear.bin@%s-%s\n' \
    "$(python3 "${PARTITION_GENERATOR}" --get boot.offset)" \
    "$(python3 "${PARTITION_GENERATOR}" --get slot_b_pair.end)" \
    "$(python3 "${PARTITION_GENERATOR}" --get littlefs.offset)" \
    "$(python3 "${PARTITION_GENERATOR}" --get littlefs.size)"
