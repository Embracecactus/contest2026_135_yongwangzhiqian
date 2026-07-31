#!/usr/bin/env bash
# Build or import one BK7258 SDK role and patch its UART archive for NuttX.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SDK_DIR="${ARMINO_SDK_DIR:-/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9}"
ROLE=""
VERSION="v3.1.1.9"
JOBS="${JOBS:-8}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-}"
SOURCE_ARCHIVE=""
BUILD_SDK=false
REPLACE=false

usage()
{
  cat <<EOF
Usage: $(basename "$0") --role cp|ap [options]

Options:
  --bundle-version V Version label: legacy or v3.1.1.9 (default: v3.1.1.9)
  --sdk-dir DIR       Authorized bk_avdk_smp source tree
  --source-archive F  Original SDK archive recorded in provenance
  --toolchain-dir DIR Directory containing arm-none-eabi-gcc
  --jobs N            SDK build jobs (default: ${JOBS})
  --build             Build the selected SDK role before importing
  --replace           Atomically replace an existing non-legacy bundle
  -h, --help          Show this help

The legacy bundle is never replaceable through this script.  Destinations:
  ${BOARD_DIR}/bk_idk/armino_as_lib/versions/<version>/<role>
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --role)
      ROLE="$2"
      shift 2
      ;;
    --bundle-version)
      VERSION="$2"
      shift 2
      ;;
    --sdk-dir)
      SDK_DIR="$2"
      shift 2
      ;;
    --source-archive)
      SOURCE_ARCHIVE="$2"
      shift 2
      ;;
    --toolchain-dir)
      TOOLCHAIN_DIR="$2"
      shift 2
      ;;
    --jobs)
      JOBS="$2"
      shift 2
      ;;
    --build)
      BUILD_SDK=true
      shift
      ;;
    --replace)
      REPLACE=true
      shift
      ;;
    --force)
      printf '%s\n' \
        'error: --force was removed; use --replace for a non-legacy version' >&2
      exit 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      printf 'error: unknown argument: %s\n' "$1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

case "$VERSION" in
  legacy|v3.1.1.9)
    ;;
  *)
    printf "error: unsupported bundle version '%s' (supported: legacy v3.1.1.9)\n" \
      "$VERSION" >&2
    exit 2
    ;;
esac

case "$ROLE" in
  cp)
    SDK_TARGET="bk7258_cp"
    BUILD_NAME="bk7258"
    SOURCE_ROLE="cp"
    ;;
  ap)
    SDK_TARGET="bk7258_ap"
    BUILD_NAME="bk7258_ap"
    SOURCE_ROLE="ap"
    ;;
  *)
    printf 'error: --role must be cp or ap\n' >&2
    exit 2
    ;;
esac

if [[ "$VERSION" == "legacy" && "$REPLACE" == "true" ]]; then
  printf '%s\n' \
    'error: the legacy SDK bundle is immutable; import a versioned bundle' >&2
  exit 2
fi

if [ -z "$TOOLCHAIN_DIR" ]; then
  CC_PATH="$(command -v arm-none-eabi-gcc || true)"
  if [ -z "$CC_PATH" ]; then
    printf 'error: arm-none-eabi-gcc not found\n' >&2
    exit 1
  fi
  TOOLCHAIN_DIR="$(dirname "$CC_PATH")"
fi

for tool in arm-none-eabi-gcc arm-none-eabi-ar arm-none-eabi-nm; do
  if [[ ! -x "${TOOLCHAIN_DIR}/${tool}" ]]; then
    printf 'error: required tool is missing: %s/%s\n' \
      "$TOOLCHAIN_DIR" "$tool" >&2
    exit 1
  fi
done

if [[ ! -d "$SDK_DIR" ]]; then
  printf 'error: SDK source tree does not exist: %s\n' "$SDK_DIR" >&2
  exit 1
fi
SDK_DIR="$(cd "$SDK_DIR" && pwd)"

if [[ -n "$SOURCE_ARCHIVE" ]]; then
  if [[ ! -f "$SOURCE_ARCHIVE" ]]; then
    printf 'error: SDK source archive does not exist: %s\n' \
      "$SOURCE_ARCHIVE" >&2
    exit 1
  fi
  SOURCE_ARCHIVE="$(cd "$(dirname "$SOURCE_ARCHIVE")" && pwd)/$(basename "$SOURCE_ARCHIVE")"
  SOURCE_ARCHIVE_SHA256="$(sha256sum "$SOURCE_ARCHIVE" | awk '{print $1}')"
else
  SOURCE_ARCHIVE="not-provided"
  SOURCE_ARCHIVE_SHA256="not-provided"
fi

BUILD_DIR="${SDK_DIR}/build/bk7258/app/${BUILD_NAME}"
EXPORT_DIR="${BUILD_DIR}/armino_as_lib"
ROLE_EXPORT="${EXPORT_DIR}/${BUILD_NAME}"
COMPILE_DB="${BUILD_DIR}/compile_commands.json"
BUNDLE_BASE="${BOARD_DIR}/bk_idk/armino_as_lib"
DEST="${BUNDLE_BASE}/versions/${VERSION}/${ROLE}"
MANIFEST_DIR="${SCRIPT_DIR}/sdk-manifests/${VERSION}"
MANIFEST="${MANIFEST_DIR}/${ROLE}.sha256"
PROVENANCE="${MANIFEST_DIR}/${ROLE}.provenance"
TMP_DEST="${DEST}.tmp.$$"
TMP_WORK="$(mktemp -d)"
TMP_OBJ="${TMP_WORK}/uart_driver.c.obj"
TMP_MANIFEST="${TMP_WORK}/${ROLE}.sha256"
TMP_PROVENANCE="${TMP_WORK}/${ROLE}.provenance"
BACKUP=""

cleanup()
{
  rm -rf "$TMP_DEST" "$TMP_WORK"
  if [[ -n "$BACKUP" && -e "$BACKUP" ]]; then
    if [[ -e "$DEST" ]]; then
      rm -rf "$DEST"
    fi
    mv "$BACKUP" "$DEST"
  fi
}
trap cleanup EXIT

if $BUILD_SDK; then
  make -C "$SDK_DIR" "$SDK_TARGET" PROJECT=app \
    COMPILER_TOOLCHAIN_PATH="$TOOLCHAIN_DIR" "-j${JOBS}"
fi

for path in "$COMPILE_DB" "$EXPORT_DIR/include" \
            "$ROLE_EXPORT/config" "$ROLE_EXPORT/libs/libdriver.a"; do
  if [ ! -e "$path" ]; then
    printf 'error: required SDK build output missing: %s\n' "$path" >&2
    printf 'hint: rerun with --build\n' >&2
    exit 1
  fi
done

if [[ -e "$DEST" && "$REPLACE" != "true" ]]; then
  printf 'error: destination exists: %s\n' "$DEST" >&2
  printf '%s\n' \
    'hint: versioned bundles may be replaced explicitly with --replace' >&2
  exit 1
fi

mkdir -p "$TMP_DEST"

python3 - "$COMPILE_DB" "$SOURCE_ROLE" "$TMP_OBJ" <<'PY'
import json
import shlex
import subprocess
import sys

compile_db, role, output = sys.argv[1:]
with open(compile_db, encoding="utf-8") as stream:
    entries = json.load(stream)

suffix = f"/{role}/middleware/driver/uart/uart_driver.c"
entry = next(
    item for item in entries
    if item["file"].replace("\\", "/").endswith(suffix)
)
args = shlex.split(entry["command"])
if "-DCONFIG_BK_PRINTF_DISABLE" not in args:
    index = args.index("-DCONFIG_CMAKE=1") + 1
    args.insert(index, "-DCONFIG_BK_PRINTF_DISABLE")
args[args.index("-o") + 1] = output
subprocess.run(args, cwd=entry["directory"], check=True)
PY

ORIGINAL_LIBDRIVER_SHA256="$(
  sha256sum "$ROLE_EXPORT/libs/libdriver.a" | awk '{print $1}'
)"
cp -a "$EXPORT_DIR/include" "$TMP_DEST/include"
cp -a "$ROLE_EXPORT/config" "$TMP_DEST/config"
cp -a "$ROLE_EXPORT/libs" "$TMP_DEST/libs"
"${TOOLCHAIN_DIR}/arm-none-eabi-ar" r \
  "$TMP_DEST/libs/libdriver.a" "$TMP_OBJ"

"${TOOLCHAIN_DIR}/arm-none-eabi-ar" p \
  "$TMP_DEST/libs/libdriver.a" uart_driver.c.obj > "${TMP_WORK}/verify.o"
if "${TOOLCHAIN_DIR}/arm-none-eabi-nm" -u "${TMP_WORK}/verify.o" |
   grep -q 'bk_printf_init'; then
  printf 'error: patched UART object still references bk_printf_init\n' >&2
  exit 1
fi

PATCHED_OBJECT_SHA256="$(sha256sum "$TMP_OBJ" | awk '{print $1}')"
FINAL_LIBDRIVER_SHA256="$(
  sha256sum "$TMP_DEST/libs/libdriver.a" | awk '{print $1}'
)"
COMPILER_VERSION="$(
  "${TOOLCHAIN_DIR}/arm-none-eabi-gcc" --version | head -1
)"
SDK_GIT_COMMIT="$(
  git -C "$SDK_DIR" rev-parse HEAD 2>/dev/null || printf 'not-a-git-tree'
)"

{
  printf 'bundle_version=%s\n' "$VERSION"
  printf 'role=%s\n' "$ROLE"
  printf 'sdk_target=%s\n' "$SDK_TARGET"
  printf 'sdk_source_tree=%s\n' "$SDK_DIR"
  printf 'sdk_git_commit=%s\n' "$SDK_GIT_COMMIT"
  printf 'source_archive=%s\n' "$SOURCE_ARCHIVE"
  printf 'source_archive_sha256=%s\n' "$SOURCE_ARCHIVE_SHA256"
  printf 'compiler=%s\n' "$COMPILER_VERSION"
  printf 'uart_patch_define=CONFIG_BK_PRINTF_DISABLE\n'
  printf 'uart_patched_object_sha256=%s\n' "$PATCHED_OBJECT_SHA256"
  printf 'libdriver_original_sha256=%s\n' "$ORIGINAL_LIBDRIVER_SHA256"
  printf 'libdriver_final_sha256=%s\n' "$FINAL_LIBDRIVER_SHA256"
} > "$TMP_PROVENANCE"

python3 "${SCRIPT_DIR}/generate_bk7258_sdk_manifest.py" \
  --bundle-dir "$TMP_DEST" --output "$TMP_MANIFEST"
(cd "$TMP_DEST" && sha256sum -c "$TMP_MANIFEST" --quiet)
printf 'final_manifest_sha256=%s\n' \
  "$(sha256sum "$TMP_MANIFEST" | awk '{print $1}')" >> "$TMP_PROVENANCE"

mkdir -p "$(dirname "$DEST")" "$MANIFEST_DIR"
if [[ -e "$DEST" ]]; then
  BACKUP="${DEST}.backup.$$"
  mv "$DEST" "$BACKUP"
fi
mv "$TMP_DEST" "$DEST"
mv "$TMP_MANIFEST" "$MANIFEST"
mv "$TMP_PROVENANCE" "$PROVENANCE"
if [[ -n "$BACKUP" ]]; then
  rm -rf "$BACKUP"
  BACKUP=""
fi

trap - EXIT
rm -rf "$TMP_WORK"
printf 'installed BK7258 %s SDK bundle %s: %s\n' \
  "$ROLE" "$VERSION" "$DEST"
printf 'manifest: %s\nprovenance: %s\n' "$MANIFEST" "$PROVENANCE"
