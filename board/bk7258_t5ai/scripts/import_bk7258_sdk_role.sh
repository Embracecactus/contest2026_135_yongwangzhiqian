#!/usr/bin/env bash
# Build or import one BK7258 SDK role and patch its UART archive for NuttX.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SDK_DIR="${ARMINO_SDK_DIR:-/home/lijian/project/armino/bk_avdk_smp}"
ROLE=""
JOBS="${JOBS:-8}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-}"
BUILD_SDK=false
FORCE=false

usage()
{
  cat <<EOF
Usage: $(basename "$0") --role cp|ap [options]

Options:
  --sdk-dir DIR       Authorized bk_avdk_smp source tree
  --toolchain-dir DIR Directory containing arm-none-eabi-gcc
  --jobs N            SDK build jobs (default: ${JOBS})
  --build             Build the selected SDK role before importing
  --force             Replace an existing local role bundle
  -h, --help          Show this help

The imported bundle is written to:
  ${BOARD_DIR}/bk_idk/armino_as_lib/<role>
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --role)
      ROLE="$2"
      shift 2
      ;;
    --sdk-dir)
      SDK_DIR="$2"
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
    --force)
      FORCE=true
      shift
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

if [ -z "$TOOLCHAIN_DIR" ]; then
  CC_PATH="$(command -v arm-none-eabi-gcc || true)"
  if [ -z "$CC_PATH" ]; then
    printf 'error: arm-none-eabi-gcc not found\n' >&2
    exit 1
  fi

  TOOLCHAIN_DIR="$(dirname "$CC_PATH")"
fi

BUILD_DIR="${SDK_DIR}/build/bk7258/app/${BUILD_NAME}"
EXPORT_DIR="${BUILD_DIR}/armino_as_lib"
ROLE_EXPORT="${EXPORT_DIR}/${BUILD_NAME}"
COMPILE_DB="${BUILD_DIR}/compile_commands.json"
DEST="${BOARD_DIR}/bk_idk/armino_as_lib/${ROLE}"
TMP_DEST="${DEST}.tmp.$$"
TMP_OBJ="/tmp/bk7258-${ROLE}-uart-driver.$$/uart_driver.c.obj"

cleanup()
{
  rm -rf "$TMP_DEST" "$(dirname "$TMP_OBJ")"
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

if [ -e "$DEST" ] && ! $FORCE; then
  printf 'error: destination exists: %s (use --force to replace)\n' "$DEST" >&2
  exit 1
fi

mkdir -p "$(dirname "$TMP_OBJ")" "$TMP_DEST"

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

cp -a "$EXPORT_DIR/include" "$TMP_DEST/include"
cp -a "$ROLE_EXPORT/config" "$TMP_DEST/config"
cp -a "$ROLE_EXPORT/libs" "$TMP_DEST/libs"
arm-none-eabi-ar r "$TMP_DEST/libs/libdriver.a" "$TMP_OBJ"

arm-none-eabi-ar p "$TMP_DEST/libs/libdriver.a" uart_driver.c.obj \
  > "$(dirname "$TMP_OBJ")/verify.o"
if arm-none-eabi-nm -u "$(dirname "$TMP_OBJ")/verify.o" |
   grep -q 'bk_printf_init'; then
  printf 'error: patched UART object still references bk_printf_init\n' >&2
  exit 1
fi

if [ -e "$DEST" ]; then
  rm -rf "$DEST"
fi
mv "$TMP_DEST" "$DEST"
printf 'installed BK7258 %s SDK bundle: %s\n' "$ROLE" "$DEST"
