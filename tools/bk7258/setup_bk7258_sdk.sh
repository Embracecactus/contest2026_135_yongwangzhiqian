#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Validate or install one versioned BK7258 AP/CP SDK role bundle.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUNDLE_BASE="${BOARD_DIR}/bk_idk/armino_as_lib"
PROG="$(basename "$0")"

MODE=""
ROLE="cp"
VERSION="v3.1.1.9"
BUNDLE_DIR=""

die()
{
    printf '%s: error: %s\n' "$PROG" "$*" >&2
    exit 1
}

info()
{
    printf '%s: %s\n' "$PROG" "$*" >&2
}

usage()
{
    cat <<EOF
Usage:
  $PROG --check [bundle-dir] [--version VERSION] [--role cp|ap]
  $PROG --install SOURCE_DIR [--version VERSION] [--role cp|ap]
  $PROG --help

The default is --version v3.1.1.9 --role cp.  All versions map to:
  ${BUNDLE_BASE}/versions/<version>/{cp,ap}

Checksums are read from:
  ${SCRIPT_DIR}/sdk-manifests/<version>/<role>.sha256

Bundle identity and manifest binding are read from:
  ${SCRIPT_DIR}/sdk-manifests/<version>/<role>.provenance

Installation is local-only, atomic, and always refuses to overwrite an
existing destination.
EOF
}

validate_version()
{
    case "$1" in
        legacy|v3.1.1.9|v3.1.1.9-sdio4)
            ;;
        *)
            die "unsupported SDK bundle version '$1' (supported: legacy v3.1.1.9 v3.1.1.9-sdio4)"
            ;;
    esac
}

validate_role()
{
    case "$1" in
        cp|ap)
            ;;
        *)
            die "--role must be cp or ap"
            ;;
    esac
}

default_target()
{
    printf '%s/versions/%s/%s\n' "$BUNDLE_BASE" "$VERSION" "$ROLE"
}

manifest_path()
{
    printf '%s/sdk-manifests/%s/%s.sha256\n' \
        "$SCRIPT_DIR" "$VERSION" "$ROLE"
}

provenance_path()
{
    printf '%s/sdk-manifests/%s/%s.provenance\n' \
        "$SCRIPT_DIR" "$VERSION" "$ROLE"
}

validate_structure()
{
    local dir="$1"
    local actual_top
    local ok=true

    [[ ! -L "$dir" ]] || {
        printf '  symlink bundle root is forbidden: %s\n' "$dir" >&2
        return 1
    }

    actual_top="$(
        find "$dir" -mindepth 1 -maxdepth 1 -printf '%f\n' |
            LC_ALL=C sort
    )"
    if [[ "$actual_top" != $'config\ninclude\nlibs' ]]; then
        printf '  bundle roots must be exactly: config include libs\n' >&2
        ok=false
    fi

    for subdir in include config libs; do
        if [[ ! -d "${dir}/${subdir}" ]]; then
            printf '  missing: %s/%s\n' "$dir" "$subdir" >&2
            ok=false
        fi
    done

    if [[ -n "$(find "$dir" -type l -print -quit)" ]]; then
        printf '  SDK bundle contains a symlink: %s\n' \
            "$(find "$dir" -type l -print -quit)" >&2
        ok=false
    fi

    if [[ -n "$(find "$dir" -mindepth 1 ! -type d ! -type f ! -type l -print -quit)" ]]; then
        printf '  SDK bundle contains a special file: %s\n' \
            "$(find "$dir" -mindepth 1 ! -type d ! -type f ! -type l -print -quit)" >&2
        ok=false
    fi

    [[ "$ok" == "true" ]]
}

validate_manifest()
{
    local bundle_dir="$1"
    local actual_files
    local expected_files
    local manifest
    local output

    manifest="$(manifest_path)"
    [[ -f "$manifest" ]] || die "tracked manifest not found: ${manifest}"

    expected_files="$(
        sed -n 's/^[0-9a-f]\{64\}  //p' "$manifest" |
            LC_ALL=C sort
    )"
    actual_files="$(
        cd "$bundle_dir"
        find config include libs -type f -printf '%p\n' | LC_ALL=C sort
    )"
    [[ -n "$expected_files" && "$actual_files" == "$expected_files" ]] ||
        die "SDK bundle file set differs from tracked manifest"

    info "validating checksums against ${manifest} ..."
    output="$(cd "$bundle_dir" && sha256sum -c "$manifest" 2>&1)" || {
        printf '%s\n' "$output" | grep -v ': OK$' | head -30 >&2
        die "checksum validation failed"
    }
    info "all checksums OK"
}

require_provenance_value()
{
    local provenance="$1"
    local key="$2"
    local expected="$3"
    local count
    local actual

    count="$(grep -c "^${key}=" "$provenance" || true)"
    [[ "$count" -eq 1 ]] ||
        die "provenance must contain exactly one '${key}' entry: ${provenance}"
    actual="$(grep "^${key}=" "$provenance" | cut -d= -f2-)"
    [[ "$actual" == "$expected" ]] ||
        die "provenance ${key} mismatch: expected '${expected}', got '${actual}'"
}

validate_provenance()
{
    local bundle_dir="$1"
    local manifest
    local provenance
    local manifest_sha256
    local libdriver="${bundle_dir}/libs/libdriver.a"
    local libdriver_sha256
    local profile_sha256

    manifest="$(manifest_path)"
    provenance="$(provenance_path)"
    [[ -f "$provenance" ]] ||
        die "tracked provenance not found: ${provenance}"
    [[ -f "$libdriver" ]] || die "SDK archive is missing: ${libdriver}"

    manifest_sha256="$(sha256sum "$manifest" | awk '{print $1}')"
    libdriver_sha256="$(sha256sum "$libdriver" | awk '{print $1}')"
    require_provenance_value "$provenance" bundle_version "$VERSION"
    require_provenance_value "$provenance" role "$ROLE"
    require_provenance_value "$provenance" \
        final_manifest_sha256 "$manifest_sha256"
    require_provenance_value "$provenance" \
        libdriver_final_sha256 "$libdriver_sha256"
    require_provenance_value "$provenance" \
        uart_patch_define CONFIG_BK_PRINTF_DISABLE

    if [[ "$VERSION" == "v3.1.1.9" && "$ROLE" == "ap" ]]; then
        require_provenance_value "$provenance" \
            bundle_profile ap-peripherals-r2
    elif [[ "$VERSION" == "v3.1.1.9-sdio4" ]]; then
        profile_sha256="$(
            cat \
                "${BOARD_DIR}/bk_idk/sdk-profiles/v3.1.1.9/ap-peripherals-r2.config" \
                "${BOARD_DIR}/bk_idk/sdk-profiles/v3.1.1.9/ap-sdio4.config" |
                sha256sum | awk '{print $1}'
        )"
        require_provenance_value "$provenance" \
            bundle_profile ap-peripherals-r2-sdio4
        require_provenance_value "$provenance" profile_components \
            ap-peripherals-r2.config,ap-sdio4.config
        require_provenance_value "$provenance" \
            profile_sha256 "$profile_sha256"
    fi
    info "provenance binding OK"
}

validate_bundle_policy()
{
    local bundle_dir="$1"
    local sdkconfig="${bundle_dir}/config/sdkconfig.h"

    [[ -f "$sdkconfig" ]] || die "SDK config is missing: ${sdkconfig}"
    if [[ "$VERSION" == "v3.1.1.9-sdio4" ]]; then
        grep -qx '#define CONFIG_SDCARD_BUSWIDTH_4LINE 1' "$sdkconfig" ||
            die "four-bit bundle does not enable CONFIG_SDCARD_BUSWIDTH_4LINE"
        if grep -qx '#define CONFIG_SDIO_4LINES_EN 1' "$sdkconfig"; then
            die "four-bit bundle must leave CONFIG_SDIO_4LINES_EN disabled"
        fi
    elif [[ "$VERSION" == "v3.1.1.9" && "$ROLE" == "ap" ]] &&
         grep -qx '#define CONFIG_SDCARD_BUSWIDTH_4LINE 1' "$sdkconfig"; then
        die "default AP bundle unexpectedly fixes data setup at four bits"
    fi
}

if [[ $# -lt 1 ]]; then
    usage >&2
    exit 1
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check)
            [[ -z "$MODE" ]] || die "choose exactly one mode"
            MODE="check"
            shift
            if [[ $# -gt 0 && "$1" != --* ]]; then
                BUNDLE_DIR="$1"
                shift
            fi
            ;;
        --install)
            [[ -z "$MODE" ]] || die "choose exactly one mode"
            [[ $# -ge 2 ]] || die "--install requires SOURCE_DIR"
            MODE="install"
            BUNDLE_DIR="$2"
            shift 2
            ;;
        --version)
            [[ $# -ge 2 ]] || die "--version requires a value"
            VERSION="$2"
            shift 2
            ;;
        --role)
            [[ $# -ge 2 ]] || die "--role requires cp or ap"
            ROLE="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1 (try --help)"
            ;;
    esac
done

[[ -n "$MODE" ]] || die "--check or --install is required"
validate_version "$VERSION"
validate_role "$ROLE"
if [[ "$VERSION" == "v3.1.1.9-sdio4" && "$ROLE" != "ap" ]]; then
    die "SDK bundle version '${VERSION}' is AP-only"
fi

if [[ "$MODE" == "check" ]]; then
    if [[ -z "$BUNDLE_DIR" ]]; then
        BUNDLE_DIR="$(default_target)"
    fi

    info "checking SDK ${VERSION}/${ROLE} bundle at: ${BUNDLE_DIR}"
    [[ ! -L "$BUNDLE_DIR" ]] || die "bundle root must not be a symlink: ${BUNDLE_DIR}"
    [[ -d "$BUNDLE_DIR" ]] || die "directory does not exist: ${BUNDLE_DIR}"
    validate_structure "$BUNDLE_DIR" ||
        die "directory structure validation failed"
    validate_manifest "$BUNDLE_DIR"
    validate_provenance "$BUNDLE_DIR"
    validate_bundle_policy "$BUNDLE_DIR"
    info "check PASSED"
    exit 0
fi

SOURCE_DIR="$BUNDLE_DIR"
DEST_DIR="$(default_target)"
TMP_DIR="${DEST_DIR}.tmp.$$"

info "source:      ${SOURCE_DIR}"
info "destination: ${DEST_DIR}"
[[ ! -L "$SOURCE_DIR" ]] || die "source directory must not be a symlink: ${SOURCE_DIR}"
[[ -d "$SOURCE_DIR" ]] || die "source directory does not exist: ${SOURCE_DIR}"
validate_structure "$SOURCE_DIR" || die "source directory structure validation failed"
[[ ! -e "$DEST_DIR" && ! -L "$DEST_DIR" ]] ||
    die "destination already exists: ${DEST_DIR} -- refusing to overwrite"

mkdir -p "$(dirname "$DEST_DIR")"
cleanup()
{
    if [[ -e "$TMP_DIR" ]]; then
        info "cleaning up temporary directory: ${TMP_DIR}"
        rm -rf "$TMP_DIR"
    fi
}
trap cleanup EXIT

cp -a "$SOURCE_DIR" "$TMP_DIR"
validate_manifest "$TMP_DIR"
validate_provenance "$TMP_DIR"
validate_bundle_policy "$TMP_DIR"
mv "$TMP_DIR" "$DEST_DIR"
trap - EXIT
info "install PASSED -- SDK bundle at: ${DEST_DIR}"
