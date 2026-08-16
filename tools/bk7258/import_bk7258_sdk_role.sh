#!/usr/bin/env bash
# Build or import one BK7258 SDK role and patch its UART archive for NuttX.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LAYOUT_PATHS="$(python3 -c '
import sys
sys.path.insert(0, sys.argv[1])
from bk7258_paths import Bk7258Layout
layout = Bk7258Layout()
print(layout.board_dir)
print(layout.tools_dir)
' "$SCRIPT_DIR")" || {
  printf '%s\n' 'error: cannot resolve BK7258 layout' >&2
  exit 1
}
BOARD_DIR="${LAYOUT_PATHS%%$'\n'*}"
TOOLS_DIR="${LAYOUT_PATHS#*$'\n'}"
if [[ "$BOARD_DIR" == "$LAYOUT_PATHS" || -z "$BOARD_DIR" || -z "$TOOLS_DIR" ]]; then
  printf '%s\n' 'error: malformed BK7258 layout result' >&2
  exit 1
fi
SDK_DIR="${BK7258_SDK_SOURCE:-${ARMINO_SDK_DIR:-}}"
ROLE=""
VERSION="v3.1.1.9"
JOBS="${JOBS:-8}"
TOOLCHAIN_DIR="${TOOLCHAIN_DIR:-}"
SOURCE_ARCHIVE=""
BUILD_SDK=false
REPLACE=false
PROFILE="base"

usage()
{
  cat <<EOF
Usage: $(basename "$0") --role cp|ap [options]

Options:
  --bundle-version V Version label: legacy, v3.1.1.9, or
                     v3.1.1.9-sdio4 (default: v3.1.1.9)
  --sdk-dir DIR       Authorized bk_avdk_smp source tree (required unless
                      BK7258_SDK_SOURCE or legacy ARMINO_SDK_DIR is set)
  --source-archive F  Original SDK archive recorded in provenance
  --toolchain-dir DIR Directory containing arm-none-eabi-gcc
  --jobs N            SDK build jobs (default: ${JOBS})
  --profile NAME      Build profile: base, ap-peripherals-r2, or
                     ap-peripherals-r2-sdio4
  --build             Build the selected SDK role before importing
  --replace           Atomically replace an existing non-legacy bundle
  -h, --help          Show this help

The legacy bundle is never replaceable through this script.  Destinations:
  ${BOARD_DIR}/bk_idk/armino_as_lib/versions/<version>/<role>

No developer-specific SDK source path is assumed.
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
    --profile)
      PROFILE="$2"
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
  legacy|v3.1.1.9|v3.1.1.9-sdio4)
    ;;
  *)
    printf "error: unsupported bundle version '%s' (supported: legacy v3.1.1.9 v3.1.1.9-sdio4)\n" \
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

PROFILE_FILES=()
case "$PROFILE" in
  base)
    ;;
  ap-peripherals-r2)
    if [[ "$ROLE" != "ap" ]]; then
      printf '%s\n' \
        'error: ap-peripherals-r2 is only valid for the AP role' >&2
      exit 2
    fi
    PROFILE_FILES+=(
      "${BOARD_DIR}/bk_idk/sdk-profiles/v3.1.1.9/ap-peripherals-r2.config"
    )
    ;;
  ap-peripherals-r2-sdio4)
    if [[ "$ROLE" != "ap" ]]; then
      printf '%s\n' \
        'error: ap-peripherals-r2-sdio4 is only valid for the AP role' >&2
      exit 2
    fi
    PROFILE_FILES+=(
      "${BOARD_DIR}/bk_idk/sdk-profiles/v3.1.1.9/ap-peripherals-r2.config"
      "${BOARD_DIR}/bk_idk/sdk-profiles/v3.1.1.9/ap-sdio4.config"
    )
    ;;
  *)
    printf "error: unsupported SDK build profile '%s'\n" "$PROFILE" >&2
    exit 2
    ;;
esac

for profile_file in "${PROFILE_FILES[@]}"; do
  if [[ ! -f "$profile_file" ]]; then
    printf 'error: SDK build profile does not exist: %s\n' \
      "$profile_file" >&2
    exit 1
  fi
done

if [[ "$VERSION" == "v3.1.1.9-sdio4" ]]; then
  if [[ "$ROLE" != "ap" || "$PROFILE" != "ap-peripherals-r2-sdio4" ]]; then
    printf '%s\n' \
      'error: v3.1.1.9-sdio4 requires AP role and ap-peripherals-r2-sdio4 profile' >&2
    exit 2
  fi
elif [[ "$PROFILE" == "ap-peripherals-r2-sdio4" ]]; then
  printf '%s\n' \
    'error: ap-peripherals-r2-sdio4 must use bundle version v3.1.1.9-sdio4' >&2
  exit 2
fi

if [[ "$PROFILE" != "base" && "$BUILD_SDK" != "true" ]]; then
  printf '%s\n' \
    'error: a non-base SDK profile requires --build' >&2
  exit 2
fi

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

if [[ -z "$SDK_DIR" ]]; then
  printf '%s\n' \
    'error: provide --sdk-dir or set BK7258_SDK_SOURCE' >&2
  exit 2
fi

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

TMP_WORK="$(mktemp -d)"
TMP_DEST=""
TMP_MANIFEST=""
TMP_PROVENANCE=""
DEST=""
MANIFEST=""
PROVENANCE=""
DEST_BACKUP=""
MANIFEST_BACKUP=""
PROVENANCE_BACKUP=""
DEST_BACKED_UP=false
MANIFEST_BACKED_UP=false
PROVENANCE_BACKED_UP=false
DEST_INSTALLED=false
MANIFEST_INSTALLED=false
PROVENANCE_INSTALLED=false
INSTALL_COMMITTED=false

cleanup()
{
  local exit_status=$?
  local cleanup_failed=false

  trap - EXIT INT TERM HUP
  set +e

  if [[ "$INSTALL_COMMITTED" != "true" ]]; then
    if [[ "$DEST_INSTALLED" == "true" && -e "$DEST" ]]; then
      if ! rm -rf "$DEST"; then
        printf 'error: failed to remove new SDK bundle during rollback: %s\n' \
          "$DEST" >&2
        cleanup_failed=true
      fi
    fi
    if [[ "$MANIFEST_INSTALLED" == "true" && -e "$MANIFEST" ]]; then
      if ! rm -f "$MANIFEST"; then
        printf 'error: failed to remove new manifest during rollback: %s\n' \
          "$MANIFEST" >&2
        cleanup_failed=true
      fi
    fi
    if [[ "$PROVENANCE_INSTALLED" == "true" && -e "$PROVENANCE" ]]; then
      if ! rm -f "$PROVENANCE"; then
        printf 'error: failed to remove new provenance during rollback: %s\n' \
          "$PROVENANCE" >&2
        cleanup_failed=true
      fi
    fi

    if [[ "$DEST_BACKED_UP" == "true" && -e "$DEST_BACKUP" ]]; then
      if [[ -e "$DEST" ]]; then
        printf 'error: SDK bundle rollback target is still occupied; backup kept at %s\n' \
          "$DEST_BACKUP" >&2
        cleanup_failed=true
      elif ! mv "$DEST_BACKUP" "$DEST"; then
        printf 'error: failed to restore SDK bundle; backup kept at %s\n' \
          "$DEST_BACKUP" >&2
        cleanup_failed=true
      fi
    fi
    if [[ "$MANIFEST_BACKED_UP" == "true" && -e "$MANIFEST_BACKUP" ]]; then
      if [[ -e "$MANIFEST" ]]; then
        printf 'error: manifest rollback target is still occupied; backup kept at %s\n' \
          "$MANIFEST_BACKUP" >&2
        cleanup_failed=true
      elif ! mv "$MANIFEST_BACKUP" "$MANIFEST"; then
        printf 'error: failed to restore manifest; backup kept at %s\n' \
          "$MANIFEST_BACKUP" >&2
        cleanup_failed=true
      fi
    fi
    if [[ "$PROVENANCE_BACKED_UP" == "true" &&
          -e "$PROVENANCE_BACKUP" ]]; then
      if [[ -e "$PROVENANCE" ]]; then
        printf 'error: provenance rollback target is still occupied; backup kept at %s\n' \
          "$PROVENANCE_BACKUP" >&2
        cleanup_failed=true
      elif ! mv "$PROVENANCE_BACKUP" "$PROVENANCE"; then
        printf 'error: failed to restore provenance; backup kept at %s\n' \
          "$PROVENANCE_BACKUP" >&2
        cleanup_failed=true
      fi
    fi
  fi

  if [[ -n "$TMP_DEST" ]]; then
    if ! rm -rf "$TMP_DEST"; then
      printf 'error: failed to remove temporary SDK bundle: %s\n' \
        "$TMP_DEST" >&2
      cleanup_failed=true
    fi
  fi
  if [[ -n "$TMP_MANIFEST" ]]; then
    if ! rm -f "$TMP_MANIFEST"; then
      printf 'error: failed to remove temporary manifest: %s\n' \
        "$TMP_MANIFEST" >&2
      cleanup_failed=true
    fi
  fi
  if [[ -n "$TMP_PROVENANCE" ]]; then
    if ! rm -f "$TMP_PROVENANCE"; then
      printf 'error: failed to remove temporary provenance: %s\n' \
        "$TMP_PROVENANCE" >&2
      cleanup_failed=true
    fi
  fi

  if ! rm -rf "$TMP_WORK"; then
    printf 'error: failed to remove temporary work tree: %s\n' \
      "$TMP_WORK" >&2
    cleanup_failed=true
  fi
  if [[ "$cleanup_failed" == "true" ]]; then
    printf '%s\n' \
      'error: SDK import rollback was incomplete; preserved backup paths are listed above' >&2
    exit 125
  fi
  exit "$exit_status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

PROJECT_DIR="${SDK_DIR}/projects/app"
MAKE_BUILD_ROOT="${SDK_DIR}/build"

if ((${#PROFILE_FILES[@]} != 0)); then
  PROJECT_DIR="${TMP_WORK}/app"
  MAKE_BUILD_ROOT="${TMP_WORK}/build"
  cp -a "${SDK_DIR}/projects/app" "$PROJECT_DIR"

  python3 - "$PROJECT_DIR/ap/config/bk7258_ap/config" \
    "${PROFILE_FILES[@]}" <<'PY'
import re
import sys
from pathlib import Path

base_path = Path(sys.argv[1])
profile_paths = [Path(item) for item in sys.argv[2:]]
config_re = re.compile(r"^(?:# )?(CONFIG_[A-Za-z0-9_]+)(?:=.*| is not set)$")

base = base_path.read_text(encoding="utf-8").splitlines()
overrides = {}
order = []
for profile_path in profile_paths:
    for line in profile_path.read_text(encoding="utf-8").splitlines():
        match = config_re.match(line)
        if match:
            key = match.group(1)
            if key not in overrides:
                order.append(key)
            overrides[key] = line

seen = set()
merged = []
for line in base:
    match = config_re.match(line)
    if match and match.group(1) in overrides:
        key = match.group(1)
        if key not in seen:
            merged.append(overrides[key])
            seen.add(key)
        continue
    merged.append(line)

missing = [key for key in order if key not in seen]
if missing:
    merged.extend(["", "# BK7258 board-owned SDK bundle profile"])
    merged.extend(overrides[key] for key in missing)

base_path.write_text("\n".join(merged) + "\n", encoding="utf-8")
PY
fi

BUILD_DIR="${MAKE_BUILD_ROOT}/bk7258/app/${BUILD_NAME}"
EXPORT_DIR="${BUILD_DIR}/armino_as_lib"
ROLE_EXPORT="${EXPORT_DIR}/${BUILD_NAME}"
COMPILE_DB="${BUILD_DIR}/compile_commands.json"
BUNDLE_BASE="${BOARD_DIR}/bk_idk/armino_as_lib"
DEST="${BUNDLE_BASE}/versions/${VERSION}/${ROLE}"
MANIFEST_DIR="${BOARD_DIR}/bk_idk/manifests/${VERSION}"
MANIFEST="${MANIFEST_DIR}/${ROLE}.sha256"
PROVENANCE="${MANIFEST_DIR}/${ROLE}.provenance"
TMP_DEST="${DEST}.tmp.$$"
TMP_OBJ="${TMP_WORK}/uart_driver.c.obj"
TMP_MANIFEST="${MANIFEST}.tmp.$$"
TMP_PROVENANCE="${PROVENANCE}.tmp.$$"

if $BUILD_SDK; then
  make -C "$SDK_DIR" "$SDK_TARGET" PROJECT=app \
    PROJECT_DIR="$PROJECT_DIR" BUILD_DIR="$MAKE_BUILD_ROOT" \
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

mkdir -p "$(dirname "$DEST")" "$MANIFEST_DIR"
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
  printf 'bundle_profile=%s\n' "$PROFILE"
  printf 'role=%s\n' "$ROLE"
  printf 'sdk_target=%s\n' "$SDK_TARGET"
  printf 'sdk_source_tree=%s\n' "$SDK_DIR"
  printf 'sdk_git_commit=%s\n' "$SDK_GIT_COMMIT"
  printf 'source_archive=%s\n' "$SOURCE_ARCHIVE"
  printf 'source_archive_sha256=%s\n' "$SOURCE_ARCHIVE_SHA256"
  if ((${#PROFILE_FILES[@]} != 0)); then
    printf 'profile_sha256=%s\n' \
      "$(cat "${PROFILE_FILES[@]}" | sha256sum | awk '{print $1}')"
    printf 'profile_components='
    profile_separator=""
    for profile_file in "${PROFILE_FILES[@]}"; do
      printf '%s%s' "$profile_separator" "$(basename "$profile_file")"
      profile_separator=,
    done
    printf '\n'
  else
    printf 'profile_sha256=not-applicable\n'
  fi
  printf 'compiler=%s\n' "$COMPILER_VERSION"
  printf 'uart_patch_define=CONFIG_BK_PRINTF_DISABLE\n'
  printf 'uart_patched_object_sha256=%s\n' "$PATCHED_OBJECT_SHA256"
  printf 'libdriver_original_sha256=%s\n' "$ORIGINAL_LIBDRIVER_SHA256"
  printf 'libdriver_final_sha256=%s\n' "$FINAL_LIBDRIVER_SHA256"
} > "$TMP_PROVENANCE"

python3 "${TOOLS_DIR}/generate_bk7258_sdk_manifest.py" \
  --bundle-dir "$TMP_DEST" --output "$TMP_MANIFEST"
(cd "$TMP_DEST" && sha256sum -c "$TMP_MANIFEST" --quiet)
printf 'final_manifest_sha256=%s\n' \
  "$(sha256sum "$TMP_MANIFEST" | awk '{print $1}')" >> "$TMP_PROVENANCE"

if ! command -v flock >/dev/null 2>&1; then
  printf '%s\n' 'error: flock is required for SDK bundle replacement' >&2
  exit 1
fi
BK7258_BUILD_LOCK_TIMEOUT_SECONDS="${BK7258_BUILD_LOCK_TIMEOUT_SECONDS:-600}"
if ! [[ "$BK7258_BUILD_LOCK_TIMEOUT_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
  printf 'error: invalid build-lock timeout: %s\n' \
    "$BK7258_BUILD_LOCK_TIMEOUT_SECONDS" >&2
  exit 2
fi
BK7258_BUILD_LOCK="/tmp/openvela-bk7258-build-${UID}.lock"
exec {BK7258_BUILD_LOCK_FD}>"$BK7258_BUILD_LOCK"
if ! flock -w "$BK7258_BUILD_LOCK_TIMEOUT_SECONDS" \
     "$BK7258_BUILD_LOCK_FD"; then
  printf 'error: timed out waiting %ss for build lock: %s\n' \
    "$BK7258_BUILD_LOCK_TIMEOUT_SECONDS" "$BK7258_BUILD_LOCK" >&2
  exit 1
fi

DEST_EXISTS=false
MANIFEST_EXISTS=false
PROVENANCE_EXISTS=false
[[ -e "$DEST" ]] && DEST_EXISTS=true
[[ -e "$MANIFEST" ]] && MANIFEST_EXISTS=true
[[ -e "$PROVENANCE" ]] && PROVENANCE_EXISTS=true

if [[ "$DEST_EXISTS" == "true" &&
      "$MANIFEST_EXISTS" == "true" &&
      "$PROVENANCE_EXISTS" == "true" ]]; then
  :
elif [[ "$DEST_EXISTS" == "false" &&
        "$MANIFEST_EXISTS" == "true" &&
        "$PROVENANCE_EXISTS" == "true" ]]; then
  # Normal fresh checkout: restricted binaries are ignored, while their
  # tracked manifest and provenance are already present.
  :
elif [[ "$DEST_EXISTS" == "false" &&
        "$MANIFEST_EXISTS" == "false" &&
        "$PROVENANCE_EXISTS" == "false" ]]; then
  :
else
  printf '%s\n' \
    'error: refusing to replace an incomplete bundle/manifest/provenance set' >&2
  exit 1
fi
if [[ "$REPLACE" != "true" &&
      ( "$DEST_EXISTS" == "true" ||
        "$MANIFEST_EXISTS" == "true" ||
        "$PROVENANCE_EXISTS" == "true" ) ]]; then
  printf '%s\n' \
    'error: existing bundle metadata requires explicit --replace' >&2
  exit 1
fi

if [[ -e "$DEST" ]]; then
  DEST_BACKUP="${DEST}.backup.$$"
  DEST_BACKED_UP=true
  mv "$DEST" "$DEST_BACKUP"
fi
if [[ -e "$MANIFEST" ]]; then
  MANIFEST_BACKUP="${MANIFEST}.backup.$$"
  MANIFEST_BACKED_UP=true
  mv "$MANIFEST" "$MANIFEST_BACKUP"
fi
if [[ -e "$PROVENANCE" ]]; then
  PROVENANCE_BACKUP="${PROVENANCE}.backup.$$"
  PROVENANCE_BACKED_UP=true
  mv "$PROVENANCE" "$PROVENANCE_BACKUP"
fi
DEST_INSTALLED=true
mv "$TMP_DEST" "$DEST"
TMP_DEST=""
MANIFEST_INSTALLED=true
mv "$TMP_MANIFEST" "$MANIFEST"
TMP_MANIFEST=""
PROVENANCE_INSTALLED=true
mv "$TMP_PROVENANCE" "$PROVENANCE"
TMP_PROVENANCE=""

"${TOOLS_DIR}/setup_bk7258_sdk.sh" --check "$DEST" \
  --version "$VERSION" --role "$ROLE"
INSTALL_COMMITTED=true

if [[ "$DEST_BACKED_UP" == "true" ]]; then
  rm -rf "$DEST_BACKUP"
  DEST_BACKUP=""
fi
if [[ "$MANIFEST_BACKED_UP" == "true" ]]; then
  rm -f "$MANIFEST_BACKUP"
  MANIFEST_BACKUP=""
fi
if [[ "$PROVENANCE_BACKED_UP" == "true" ]]; then
  rm -f "$PROVENANCE_BACKUP"
  PROVENANCE_BACKUP=""
fi

trap - EXIT INT TERM HUP
rm -rf "$TMP_WORK"
printf 'installed BK7258 %s SDK bundle %s: %s\n' \
  "$ROLE" "$VERSION" "$DEST"
printf 'manifest: %s\nprovenance: %s\n' "$MANIFEST" "$PROVENANCE"
