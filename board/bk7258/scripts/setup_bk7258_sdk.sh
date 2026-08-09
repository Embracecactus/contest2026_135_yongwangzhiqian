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

Installation is local-only, atomic, and always refuses to overwrite an
existing destination.
EOF
}

validate_version()
{
    case "$1" in
        legacy|v3.1.1.9)
            ;;
        *)
            die "unsupported SDK bundle version '$1' (supported: legacy v3.1.1.9)"
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

validate_structure()
{
    local dir="$1"
    local ok=true

    for subdir in include config libs; do
        if [[ ! -d "${dir}/${subdir}" ]]; then
            printf '  missing: %s/%s\n' "$dir" "$subdir" >&2
            ok=false
        fi
    done

    [[ "$ok" == "true" ]]
}

validate_manifest()
{
    local bundle_dir="$1"
    local manifest
    local output

    manifest="$(manifest_path)"
    [[ -f "$manifest" ]] || die "tracked manifest not found: ${manifest}"

    info "validating checksums against ${manifest} ..."
    output="$(cd "$bundle_dir" && sha256sum -c "$manifest" 2>&1)" || {
        printf '%s\n' "$output" | grep -v ': OK$' | head -30 >&2
        die "checksum validation failed"
    }
    info "all checksums OK"
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

if [[ "$MODE" == "check" ]]; then
    if [[ -z "$BUNDLE_DIR" ]]; then
        BUNDLE_DIR="$(default_target)"
    fi

    info "checking SDK ${VERSION}/${ROLE} bundle at: ${BUNDLE_DIR}"
    [[ -d "$BUNDLE_DIR" ]] || die "directory does not exist: ${BUNDLE_DIR}"
    validate_structure "$BUNDLE_DIR" ||
        die "directory structure validation failed"
    validate_manifest "$BUNDLE_DIR"
    info "check PASSED"
    exit 0
fi

SOURCE_DIR="$BUNDLE_DIR"
DEST_DIR="$(default_target)"
TMP_DIR="${DEST_DIR}.tmp.$$"

info "source:      ${SOURCE_DIR}"
info "destination: ${DEST_DIR}"
[[ -d "$SOURCE_DIR" ]] || die "source directory does not exist: ${SOURCE_DIR}"
validate_structure "$SOURCE_DIR" || die "source directory structure validation failed"
[[ ! -e "$DEST_DIR" ]] ||
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
mv "$TMP_DIR" "$DEST_DIR"
trap - EXIT
info "install PASSED -- SDK bundle at: ${DEST_DIR}"
