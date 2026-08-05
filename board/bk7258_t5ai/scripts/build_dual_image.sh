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
CP_CONFIG_NAME="${CP_CONFIG_NAME:-cp_nsh}"
case "${CP_CONFIG_NAME}" in
    cp_nsh|cp_nsh_manual|cp_nsh_rptun|cp_nsh_btipc|cp_nsh_ble_gatt|cp_nsh_psram|cp_nsh_ota|cp_nsh_wifi)
        ;;
    *)
        printf 'build_dual_image: unsupported CP_CONFIG_NAME=%s\n' \
            "${CP_CONFIG_NAME}" >&2
        exit 2
        ;;
esac
CP_CONFIG="vendor/openvela/boards/contest2026_135_bk7258/configs/${CP_CONFIG_NAME}"
AP_CONFIG_NAME="${AP_CONFIG_NAME:-ap_smp}"
case "${AP_CONFIG_NAME}" in
    ap_up|ap_smp|ap_smp_online|ap_smp_affinity|ap_smp_semwake|ap_smp_semwake_loop|ap_smp_bidir|ap_smp_dualtask|ap_smp_migration|ap_smp_timedwait|ap_smp_lifecycle|ap_smp_rptun|ap_smp_btipc|ap_smp_ble_gatt|ap_smp_psram|ap_smp_wifi)
        ;;
    *)
        printf 'build_dual_image: unsupported AP_CONFIG_NAME=%s\n' \
            "${AP_CONFIG_NAME}" >&2
        exit 2
        ;;
esac
AP_CONFIG="vendor/openvela/boards/contest2026_135_bk7258/configs/${AP_CONFIG_NAME}"

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
      "${CP_CONFIG_NAME}" == "cp_nsh_ota" ||
      "${AP_CONFIG_NAME}" == "ap_smp_psram" ]]; then
    if [[ ("${CP_CONFIG_NAME}" != "cp_nsh_psram" &&
           "${CP_CONFIG_NAME}" != "cp_nsh_ota") ||
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
    printf "build_dual_image: N15 requires official SDK v3.1.1.9, got '%s'\n" \
        "${BK7258_SDK_BUNDLE_VERSION}" >&2
    exit 2
fi

# A normal dual build never arms OTA.  A deterministic host-only candidate
# bundle is emitted only when the caller supplies the complete four-field
# identity.  Partial identities fail closed instead of inventing versions.

N15_OTA_GENERATION="${N15_OTA_GENERATION:-}"
N15_OTA_VERSION="${N15_OTA_VERSION:-}"
N15_OTA_BASE_VERSION="${N15_OTA_BASE_VERSION:-}"
N15_OTA_TIMESTAMP="${N15_OTA_TIMESTAMP:-}"
N15_OTA_HOST_BUNDLE_ENABLED=false
if [[ -n "${N15_OTA_GENERATION}${N15_OTA_VERSION}${N15_OTA_BASE_VERSION}${N15_OTA_TIMESTAMP}" ]]; then
    for field in N15_OTA_GENERATION N15_OTA_VERSION \
        N15_OTA_BASE_VERSION N15_OTA_TIMESTAMP; do
        if [[ -z "${!field}" ]]; then
            printf 'build_dual_image: host OTA bundle requires %s\n' \
                "${field}" >&2
            exit 2
        fi
    done
    N15_OTA_HOST_BUNDLE_ENABLED=true
fi

# Building a validation package is separate from authorizing any board write.
# The explicit value prevents a typo or merely selecting cp_nsh_ota from
# silently producing a bootloader with live selector/remap/trial gates.

N15_OTA_VALIDATION="${N15_OTA_VALIDATION:-NO}"
N15_OTA_VALIDATION_ENABLED=false
BOOT_GATE_VALUE=0
case "${N15_OTA_VALIDATION}" in
    NO)
        if [[ "${CP_CONFIG_NAME}" == "cp_nsh_ota" ]]; then
            printf '%s\n' \
                'build_dual_image: cp_nsh_ota requires N15_OTA_VALIDATION=YES' \
                >&2
            exit 2
        fi
        ;;
    YES)
        if [[ "${CP_CONFIG_NAME}" != "cp_nsh_ota" ||
              "${AP_CONFIG_NAME}" != "ap_smp_psram" ]]; then
            printf '%s\n' \
                'build_dual_image: validation requires cp_nsh_ota + ap_smp_psram' \
                >&2
            exit 2
        fi
        if [[ "${N15_OTA_HOST_BUNDLE_ENABLED}" != "true" ]]; then
            printf '%s\n' \
                'build_dual_image: validation requires a complete N15 OTA identity' \
                >&2
            exit 2
        fi
        N15_OTA_VALIDATION_ENABLED=true
        BOOT_GATE_VALUE=1
        ;;
    *)
        printf "build_dual_image: N15_OTA_VALIDATION must be YES or NO, got '%s'\n" \
            "${N15_OTA_VALIDATION}" >&2
        exit 2
        ;;
esac

N15_OTA_RUNTIME_PROFILE=false
if [[ "${CP_CONFIG_NAME}" == "cp_nsh_psram" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_ota" ]]; then
    N15_OTA_RUNTIME_PROFILE=true
fi

# Keep validation artifacts physically separate from the normal dual-image
# package.  A later normal build must not silently overwrite the candidate,
# and a validation build must not masquerade as the default package.

if [[ "${N15_OTA_VALIDATION_ENABLED}" == "true" ]]; then
    OUTPUT="${TOPDIR}/bk7258-dual-ota-validation"
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
LAYOUT_VERIFY_ARGS=(
    --output "${TMPDIR}/bk7258-ab-layout.json"
)
if [[ -n "${BK7258_SDK_SOURCE:-}" ]]; then
    LAYOUT_VERIFY_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
fi
python3 "${SCRIPT_DIR}/verify_bk7258_ota_layout.py" \
    "${LAYOUT_VERIFY_ARGS[@]}"
if [[ "${N15_OTA_RUNTIME_PROFILE}" == "true" ]]; then
    PAIR_SELF_TEST_ARGS=(--self-test)
    STAGING_SELF_TEST_ARGS=(--self-test)
    BOOT_SELF_TEST_ARGS=(--self-test)
    TRIAL_SELF_TEST_ARGS=(--self-test)
    FORMAT2_SDK_ARGS=()
    if [[ -n "${BK7258_SDK_SOURCE:-}" ]]; then
        PAIR_SELF_TEST_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
        STAGING_SELF_TEST_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
        BOOT_SELF_TEST_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
        TRIAL_SELF_TEST_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
        FORMAT2_SDK_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
    fi
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_pair.py" \
        "${PAIR_SELF_TEST_ARGS[@]}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_staging.py" \
        "${STAGING_SELF_TEST_ARGS[@]}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_boot.py" \
        "${BOOT_SELF_TEST_ARGS[@]}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_trial.py" \
        "${TRIAL_SELF_TEST_ARGS[@]}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_rotation.py" \
        --report "${TMPDIR}/bk7258-ota-rotation.json" \
        "${FORMAT2_SDK_ARGS[@]}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_rotation_select.py" \
        --report "${TMPDIR}/bk7258-ota-rotation-select.json" \
        "${FORMAT2_SDK_ARGS[@]}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_rotation_trial.py" \
        --report "${TMPDIR}/bk7258-ota-rotation-trial.json" \
        "${FORMAT2_SDK_ARGS[@]}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_rotation_publish.py" \
        --report "${TMPDIR}/bk7258-ota-publish.json" \
        "${FORMAT2_SDK_ARGS[@]}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_rotation_control.py" \
        --report "${TMPDIR}/bk7258-ota-rotation-control.json" \
        "${FORMAT2_SDK_ARGS[@]}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_rotation_health.py" \
        --report "${TMPDIR}/bk7258-ota-health.json" \
        "${FORMAT2_SDK_ARGS[@]}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_fault.py" \
        --self-test \
        --output "${TMPDIR}/bk7258-ota-fault.json"
fi
printf '%s\n' "build_dual_image: rebuilding Tier-1 bootloader"
make -C "${BOARD_DIR}/bootloader" clean all verify \
    N15_OTA_VALIDATION="${BOOT_GATE_VALUE}"
BOOT_ELF_VERIFY_ARGS=(
    --elf-only
    --boot-elf "${BOARD_DIR}/bootloader/bl.elf"
    --boot-bin "${BOARD_DIR}/bootloader/bl.bin"
    --boot-crc "${BOARD_DIR}/bootloader/bl_crc.bin"
    --expected-gate-value "${BOOT_GATE_VALUE}"
    --output "${TMPDIR}/bk7258-ota-boot.json"
)
if [[ -n "${BK7258_SDK_SOURCE:-}" ]]; then
    BOOT_ELF_VERIFY_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
fi
python3 "${SCRIPT_DIR}/verify_bk7258_ota_boot.py" \
    "${BOOT_ELF_VERIFY_ARGS[@]}"
cp "${BOARD_DIR}/bootloader/bl.elf" "${TMPDIR}/bootloader.elf"
cp "${BOARD_DIR}/bootloader/bl.bin" "${TMPDIR}/bootloader.bin"
cp "${BOARD_DIR}/bootloader/bl.map" "${TMPDIR}/bootloader.map"

printf 'build_dual_image: building CPU0/CP (%s)\n' "${CP_CONFIG_NAME}"
build_config "${CP_CONFIG}"
save_role cp app.bin app_crc.bin

printf 'build_dual_image: building physical CPU1/AP (%s)\n' \
    "${AP_CONFIG_NAME}"
build_config "${AP_CONFIG}"
save_role ap app1.bin app1_crc.bin

printf '%s\n' "build_dual_image: restoring CPU0/CP build tree"
build_config "${CP_CONFIG}"
if [[ "${N15_OTA_RUNTIME_PROFILE}" == "true" ]]; then
    STAGING_ELF_VERIFY_ARGS=(
        --elf-only
        --elf "${TOPDIR}/nuttx"
        --config "${TOPDIR}/.config"
    )
    if [[ "${N15_OTA_VALIDATION_ENABLED}" == "true" ]]; then
        STAGING_ELF_VERIFY_ARGS+=(--validation-profile)
    fi
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_staging.py" \
        "${STAGING_ELF_VERIFY_ARGS[@]}"
    TRIAL_ELF_VERIFY_ARGS=(
        --elf-only
        --boot-elf "${BOARD_DIR}/bootloader/bl.elf"
        --boot-bin "${BOARD_DIR}/bootloader/bl.bin"
        --boot-crc "${BOARD_DIR}/bootloader/bl_crc.bin"
        --cp-elf "${TOPDIR}/nuttx"
        --cp-config "${TOPDIR}/.config"
        --output "${TMPDIR}/bk7258-ota-trial.json"
    )
    if [[ -n "${BK7258_SDK_SOURCE:-}" ]]; then
        TRIAL_ELF_VERIFY_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
    fi
    if [[ "${N15_OTA_VALIDATION_ENABLED}" == "true" ]]; then
        TRIAL_ELF_VERIFY_ARGS+=(--validation-profile)
    fi
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_trial.py" \
        "${TRIAL_ELF_VERIFY_ARGS[@]}"
fi
if [[ "${N15_OTA_VALIDATION_ENABLED}" == "true" ]]; then
    VALIDATION_VERIFY_ARGS=(
        --boot-elf "${BOARD_DIR}/bootloader/bl.elf"
        --boot-bin "${BOARD_DIR}/bootloader/bl.bin"
        --boot-crc "${BOARD_DIR}/bootloader/bl_crc.bin"
        --cp-elf "${TOPDIR}/nuttx"
        --cp-config "${TOPDIR}/.config"
        --output "${TMPDIR}/bk7258-ota-validation.json"
    )
    if [[ -n "${BK7258_SDK_SOURCE:-}" ]]; then
        VALIDATION_VERIFY_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
    fi
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_validation.py" \
        "${VALIDATION_VERIFY_ARGS[@]}"
fi

# The restored CP build is authoritative for both the normal build tree and
# the dual-image package.  Overwrite the first CP snapshot so app.bin,
# app_crc.bin, nuttx_crc.bin, all-app.bin, the saved CP ELF and the manifest
# cannot describe two timestamp-distinct CP builds.

save_role cp app.bin app_crc.bin

rm -rf "${OUTPUT}"
mkdir -p "${OUTPUT}"
cp "${TMPDIR}"/nuttx-*.elf "${OUTPUT}/"
cp "${TMPDIR}"/nuttx-*.config "${OUTPUT}/"
cp "${TMPDIR}"/bootloader.elf "${OUTPUT}/"
cp "${TMPDIR}"/bootloader.bin "${OUTPUT}/"
cp "${TMPDIR}"/bootloader.map "${OUTPUT}/"
cp "${TMPDIR}/bk7258-partitions.json" "${OUTPUT}/"
cp "${TMPDIR}/bk7258-ab-layout.json" "${OUTPUT}/"
for report in "${TMPDIR}"/bk7258-ota-*.json; do
    if [[ -f "${report}" ]]; then
        cp "${report}" "${OUTPUT}/"
    fi
done
for map in "${TMPDIR}"/nuttx-*.map; do
    if [ -f "${map}" ]; then
        cp "${map}" "${OUTPUT}/"
    fi
done

python3 "${SCRIPT_DIR}/pack_dual_image.py" \
    --boot "${BOARD_DIR}/bootloader/bl_crc.bin" \
    --cp-raw "${TMPDIR}/app.bin" \
    --cp-crc "${TMPDIR}/app_crc.bin" \
    --ap-raw "${TMPDIR}/app1.bin" \
    --ap-crc "${TMPDIR}/app1_crc.bin" \
    --output "${OUTPUT}"

python3 "${SCRIPT_DIR}/verify_bk7258_factory_layout.py" \
    --package "${OUTPUT}" \
    --json "${OUTPUT}/bk7258-factory-layout.json"

if [[ "${N15_OTA_HOST_BUNDLE_ENABLED}" == "true" ]]; then
    N15_OTA_OUTPUT="${OUTPUT}/n15-ota-host-candidate"
    N15_OTA_SOURCE_ARGS=()
    if [[ -n "${BK7258_SDK_SOURCE:-}" ]]; then
        N15_OTA_SOURCE_ARGS+=(--sdk-source "${BK7258_SDK_SOURCE}")
    fi

    python3 "${SCRIPT_DIR}/pack_bk7258_ota_pair.py" \
        --cp-raw "${TMPDIR}/app.bin" \
        --ap-raw "${TMPDIR}/app1.bin" \
        --output "${N15_OTA_OUTPUT}" \
        --generation "${N15_OTA_GENERATION}" \
        --version "${N15_OTA_VERSION}" \
        --base-version "${N15_OTA_BASE_VERSION}" \
        --timestamp "${N15_OTA_TIMESTAMP}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_pair.py" \
        --bundle "${N15_OTA_OUTPUT}" \
        --expected-generation "${N15_OTA_GENERATION}" \
        --expected-version "${N15_OTA_VERSION}" \
        --expected-base-version "${N15_OTA_BASE_VERSION}" \
        --expected-timestamp "${N15_OTA_TIMESTAMP}" \
        "${N15_OTA_SOURCE_ARGS[@]}"
    python3 "${SCRIPT_DIR}/pack_bk7258_ota_rotation.py" \
        --bundle "${N15_OTA_OUTPUT}" \
        --base-cp-crc "${TMPDIR}/app_crc.bin" \
        --base-ap-crc "${TMPDIR}/app1_crc.bin" \
        --target-slot b \
        --bank 0 \
        --output "${N15_OTA_OUTPUT}/bk7258-ota-metadata.bin" \
        --record-output "${N15_OTA_OUTPUT}/bk7258-ota-pending-record.bin" \
        --descriptor-output "${N15_OTA_OUTPUT}/bk7258-ota-stage.bin" \
        --generation "${N15_OTA_GENERATION}" \
        --version "${N15_OTA_VERSION}" \
        --base-version "${N15_OTA_BASE_VERSION}" \
        --timestamp "${N15_OTA_TIMESTAMP}" \
        "${N15_OTA_SOURCE_ARGS[@]}"
    python3 "${SCRIPT_DIR}/verify_bk7258_ota_boot.py" \
        --bundle "${N15_OTA_OUTPUT}" \
        --cp-crc "${TMPDIR}/app_crc.bin" \
        --ap-crc "${TMPDIR}/app1_crc.bin" \
        --metadata "${N15_OTA_OUTPUT}/bk7258-ota-metadata.bin" \
        --expected-target-slot b \
        --expected-generation "${N15_OTA_GENERATION}" \
        --expected-version "${N15_OTA_VERSION}" \
        --expected-base-version "${N15_OTA_BASE_VERSION}" \
        --expected-timestamp "${N15_OTA_TIMESTAMP}" \
        --boot-elf "${BOARD_DIR}/bootloader/bl.elf" \
        --boot-bin "${BOARD_DIR}/bootloader/bl.bin" \
        --boot-crc "${BOARD_DIR}/bootloader/bl_crc.bin" \
        --expected-gate-value "${BOOT_GATE_VALUE}" \
        --output "${N15_OTA_OUTPUT}/bk7258-ota-boot-candidate.json" \
        "${N15_OTA_SOURCE_ARGS[@]}"
    if [[ "${N15_OTA_VALIDATION_ENABLED}" == "true" ]]; then
        python3 "${SCRIPT_DIR}/verify_bk7258_ota_transfer.py" \
            --package "${N15_OTA_OUTPUT}" \
            --expected-target-slot b \
            --expected-bank 0
    fi
fi

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
N15_OTA_HOST_BUNDLE_ENABLED=${N15_OTA_HOST_BUNDLE_ENABLED}
N15_OTA_GENERATION=${N15_OTA_GENERATION}
N15_OTA_VERSION=${N15_OTA_VERSION}
N15_OTA_BASE_VERSION=${N15_OTA_BASE_VERSION}
N15_OTA_TIMESTAMP=${N15_OTA_TIMESTAMP}
N15_OTA_VALIDATION_ENABLED=${N15_OTA_VALIDATION_ENABLED}
N15_OTA_BOOT_GATE_VALUE=${BOOT_GATE_VALUE}
N15_OTA_SELECTION_ENABLED=${N15_OTA_VALIDATION_ENABLED}
N15_OTA_REMAP_ENABLED=${N15_OTA_VALIDATION_ENABLED}
N15_OTA_TRIAL_METADATA_MUTATION_ENABLED=${N15_OTA_VALIDATION_ENABLED}
N15_OTA_CP_RUNTIME_GATES_INITIAL=false
N15_OTA_FAULT_INJECTION_ENABLED=${N15_OTA_VALIDATION_ENABLED}
N15_OTA_BOARD_WRITE_AUTHORIZED=false
EOF

cp "${OUTPUT}/app.bin" "${TOPDIR}/app.bin"
cp "${OUTPUT}/app_crc.bin" "${TOPDIR}/app_crc.bin"
cp "${OUTPUT}/app1.bin" "${TOPDIR}/app1.bin"
cp "${OUTPUT}/app1_crc.bin" "${TOPDIR}/app1_crc.bin"
cp "${OUTPUT}/bk7258-dual-image.json" "${TOPDIR}/"

python3 "${SCRIPT_DIR}/verify_bk7258_sdk_partition_wrapper.py" \
    --elf "${OUTPUT}/nuttx-cp.elf" \
    --map "${OUTPUT}/nuttx-cp.map" \
    --output "${OUTPUT}/bk7258-sdk-partition-wrapper.json"

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_rptun" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_btipc" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_ble_gatt" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_psram" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_ota" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_wifi" ]]; then
    python3 "${SCRIPT_DIR}/verify_bk7258_rptun_layout.py" \
        --cp-elf "${OUTPUT}/nuttx-cp.elf" \
        --cp-map "${OUTPUT}/nuttx-cp.map" \
        --ap-elf "${OUTPUT}/nuttx-ap.elf" \
        --ap-map "${OUTPUT}/nuttx-ap.map" \
        --json "${OUTPUT}/bk7258-rptun-layout.json"
fi

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_ble_gatt" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_psram" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_ota" ]]; then
    BLE_VERIFY_IDENTITY_ARGS=()
    if [[ "${CP_CONFIG_NAME}" == "cp_nsh_psram" ||
          "${CP_CONFIG_NAME}" == "cp_nsh_ota" ]]; then
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

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_psram" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_ota" ]]; then
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
    if [[ "${N15_OTA_VALIDATION_ENABLED}" == "true" ]]; then
        PSRAM_VERIFY_ARGS+=(--validation-profile)
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
    "$(python3 "${PARTITION_GENERATOR}" --get ota_metadata_primary.end)" \
    "$(python3 "${PARTITION_GENERATOR}" --get littlefs.offset)" \
    "$(python3 "${PARTITION_GENERATOR}" --get littlefs.size)"
