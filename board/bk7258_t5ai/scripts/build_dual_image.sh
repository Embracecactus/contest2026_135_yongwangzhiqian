#!/usr/bin/env bash
# Build CP app.bin and CPU1 AP app1.bin, then restore the CP build tree.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONTEST_DIR="$(cd "${BOARD_DIR}/../.." && pwd)"
WORKSPACE="$(cd "${CONTEST_DIR}/.." && pwd)"
TOPDIR="${WORKSPACE}/nuttx"
BUILD="${WORKSPACE}/build.sh"
CP_CONFIG_NAME="${CP_CONFIG_NAME:-cp_nsh}"
case "${CP_CONFIG_NAME}" in
    cp_nsh|cp_nsh_manual|cp_nsh_rptun|cp_nsh_btipc)
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
    ap_up|ap_smp|ap_smp_online|ap_smp_affinity|ap_smp_semwake|ap_smp_semwake_loop|ap_smp_bidir|ap_smp_dualtask|ap_smp_migration|ap_smp_timedwait|ap_smp_lifecycle|ap_smp_rptun|ap_smp_btipc)
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
JOBS="${JOBS:-8}"
OUTPUT="${TOPDIR}/bk7258-dual"
BK7258_SDK_BUNDLE_VERSION="${BK7258_SDK_BUNDLE_VERSION:-v3.1.1.9}"

case "${BK7258_SDK_BUNDLE_VERSION}" in
    legacy|v3.1.1.9)
        ;;
    *)
        printf "build_dual_image: unsupported SDK bundle version '%s'\n" \
            "${BK7258_SDK_BUNDLE_VERSION}" >&2
        exit 2
        ;;
esac

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
    if [ -f "${TOPDIR}/nuttx.map" ]; then
        cp "${TOPDIR}/nuttx.map" "${TMPDIR}/nuttx-${role}.map"
    fi
}

printf 'build_dual_image: SDK bundle version: %s\n' \
    "${BK7258_SDK_BUNDLE_VERSION}"
printf '%s\n' "build_dual_image: rebuilding Tier-1 bootloader"
make -C "${BOARD_DIR}/bootloader" clean all

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

rm -rf "${OUTPUT}"
mkdir -p "${OUTPUT}"
cp "${TMPDIR}"/nuttx-*.elf "${OUTPUT}/"
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
EOF

cp "${OUTPUT}/app.bin" "${TOPDIR}/app.bin"
cp "${OUTPUT}/app_crc.bin" "${TOPDIR}/app_crc.bin"
cp "${OUTPUT}/app1.bin" "${TOPDIR}/app1.bin"
cp "${OUTPUT}/app1_crc.bin" "${TOPDIR}/app1_crc.bin"
cp "${OUTPUT}/bk7258-dual-image.json" "${TOPDIR}/"

if [[ "${CP_CONFIG_NAME}" == "cp_nsh_rptun" ||
      "${CP_CONFIG_NAME}" == "cp_nsh_btipc" ]]; then
    python3 "${SCRIPT_DIR}/verify_bk7258_rptun_layout.py" \
        --cp-elf "${OUTPUT}/nuttx-cp.elf" \
        --cp-map "${OUTPUT}/nuttx-cp.map" \
        --ap-elf "${OUTPUT}/nuttx-ap.elf" \
        --ap-map "${OUTPUT}/nuttx-ap.map" \
        --json "${OUTPUT}/bk7258-rptun-layout.json"
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
printf '%s\n' "build_dual_image: all-app-factory.bin erases/pads the LittleFS region"
