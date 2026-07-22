#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# setup_bk7258_sdk.sh -- validate or install the BK7258 T5-AI SDK bundle.
#
# This script manages the local Beken SDK prebuilt library bundle that lives
# under board/bk7258_t5ai/bk_idk/armino_as_lib/cp/.  The bundle is a local
# prerequisite and is NOT committed to this repository because it contains
# Beken proprietary/restricted prebuilts.
#
# Usage:
#   setup_bk7258_sdk.sh --check  [cp-dir]   Validate bundle structure and checksums
#   setup_bk7258_sdk.sh --install <cp-dir>   Install from authorized SDK source
#   setup_bk7258_sdk.sh --help               Show this help
#
# The script performs no network download and no privilege escalation.
# The source for --install must be an authorized Beken/Tuya SDK
# armino_as_lib/cp bundle obtained separately.

set -euo pipefail

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
MANIFEST="${SCRIPT_DIR}/bk7258_sdk_manifest.sha256"
DEFAULT_TARGET="${BOARD_DIR}/bk_idk/armino_as_lib/cp"

PROG="$(basename "$0")"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

die() {
    printf '%s: error: %s\n' "$PROG" "$*" >&2
    exit 1
}

info() {
    printf '%s: %s\n' "$PROG" "$*" >&2
}

usage() {
    cat <<EOF
Usage:
  $PROG --check  [cp-dir]   Validate bundle structure and checksums
  $PROG --install <cp-dir>   Install from authorized SDK source
  $PROG --help               Show this help

Options:
  --check [cp-dir]   Validate that the directory has the expected structure
                     (include/, config/, libs/) and that all entries in the
                     tracked checksum manifest pass sha256sum -c.
                     Defaults to the standard installed location if omitted.

  --install <cp-dir> Copy an authorized Beken/Tuya SDK cp bundle into the
                     standard installed location.  Refuses if the destination
                     already exists.  Uses atomic rename: copies to a temp
                     sibling directory, validates checksums, then renames.
                     Cleans only its own temp directory on failure.

  --help             Show this help message and exit.

No network download is performed.  The source must be an authorized
Beken/Tuya SDK armino_as_lib/cp bundle obtained separately.
EOF
}

# validate_structure <dir>
#   Check that the given directory contains the expected subdirectories.
validate_structure() {
    local dir="$1"
    local ok=true

    for subdir in include config libs; do
        if [[ ! -d "${dir}/${subdir}" ]]; then
            printf '  missing: %s/%s\n' "$dir" "$subdir" >&2
            ok=false
        fi
    done

    if [[ "$ok" == "false" ]]; then
        return 1
    fi
    return 0
}

# validate_manifest <cp-dir>
#   Run sha256sum -c against the tracked manifest from the given cp root.
#   Fail-closed: any nonzero exit from sha256sum (missing files, read errors,
#   or hash mismatches) triggers an immediate failure.  Useful diagnostics are
#   preserved on stderr.
validate_manifest() {
    local cp_dir="$1"

    if [[ ! -f "$MANIFEST" ]]; then
        die "tracked manifest not found: ${MANIFEST}"
    fi

    info "validating checksums against ${MANIFEST} ..."
    local output
    output=$(cd "$cp_dir" && sha256sum -c "$MANIFEST" 2>&1) || {
        echo "$output" | grep -v ": OK$" | head -30 >&2
        die "checksum validation failed (see diagnostics above)"
    }

    info "all checksums OK"
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

MODE=""
CP_DIR=""

if [[ $# -lt 1 ]]; then
    usage >&2
    exit 1
fi

case "$1" in
    --check)
        MODE="check"
        CP_DIR="${2:-${DEFAULT_TARGET}}"
        ;;
    --install)
        MODE="install"
        if [[ $# -lt 2 ]]; then
            die "--install requires a <cp-dir> argument"
        fi
        CP_DIR="$2"
        ;;
    --help|-h)
        usage
        exit 0
        ;;
    *)
        die "unknown option: $1 (try --help)"
        ;;
esac

# ---------------------------------------------------------------------------
# --check mode
# ---------------------------------------------------------------------------

do_check() {
    local cp_dir="$1"

    info "checking SDK bundle at: ${cp_dir}"

    if [[ ! -d "$cp_dir" ]]; then
        die "directory does not exist: ${cp_dir}"
    fi

    info "validating directory structure ..."
    if ! validate_structure "$cp_dir"; then
        die "directory structure validation failed"
    fi
    info "directory structure OK"

    validate_manifest "$cp_dir"

    info "check PASSED"
}

# ---------------------------------------------------------------------------
# --install mode
# ---------------------------------------------------------------------------

do_install() {
    local src_dir="$1"
    local dst_dir="$DEFAULT_TARGET"
    local tmp_dir="${dst_dir}.tmp.$$"

    info "source:      ${src_dir}"
    info "destination: ${dst_dir}"

    # Refuse if destination already exists
    if [[ -e "$dst_dir" ]]; then
        die "destination already exists: ${dst_dir} -- will not overwrite or delete.  Remove it manually first if you intend to replace it."
    fi

    # Validate source structure
    info "validating source structure ..."
    if ! validate_structure "$src_dir"; then
        die "source directory structure validation failed"
    fi
    info "source structure OK"

    # Ensure destination parent exists (cp -a needs it for the temp sibling)
    mkdir -p "$(dirname "$dst_dir")"

    # Clean up temp directory if it exists from a previous failed run
    if [[ -e "$tmp_dir" ]]; then
        info "removing leftover temp directory: ${tmp_dir}"
        rm -rf "$tmp_dir"
    fi

    # Ensure cleanup on failure
    cleanup() {
        if [[ -e "$tmp_dir" ]]; then
            info "cleaning up temp directory: ${tmp_dir}"
            rm -rf "$tmp_dir"
        fi
    }
    trap cleanup ERR

    # Copy to temp directory
    info "copying to temporary directory ..."
    cp -a "$src_dir" "$tmp_dir"

    # Validate checksums from the temp copy
    info "validating checksums on copied bundle ..."
    (cd "$tmp_dir" && sha256sum -c "$MANIFEST" --quiet) || {
        cleanup
        die "checksum validation failed on copied bundle"
    }
    info "checksums OK on copied bundle"

    # Atomic rename
    info "renaming into place ..."
    mv "$tmp_dir" "$dst_dir"

    # Disable cleanup trap (success)
    trap - ERR

    info "install PASSED -- SDK bundle at: ${dst_dir}"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

case "$MODE" in
    check)
        do_check "$CP_DIR"
        ;;
    install)
        do_install "$CP_DIR"
        ;;
esac
