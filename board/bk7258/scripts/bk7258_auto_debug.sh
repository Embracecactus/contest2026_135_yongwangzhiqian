#!/usr/bin/env bash
# Automate BK7258 build/download and optional Windows UART capture from WSL2.
# Physical RESET is authoritative when performed manually; J-Link RESETPIN is experimental until BClk is observed.

set -Eeuo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
OPENVELA_ROOT=$(cd "$SCRIPT_DIR/../../../.." && pwd)
BUILD_SCRIPT="$SCRIPT_DIR/build_dual_image.sh"
BKPACK_TOOL="$SCRIPT_DIR/bk7258_bkpack.py"
TRUST_CHAIN_TOOL="$SCRIPT_DIR/bk7258_trust_chain.py"
CAPTURE_PS1="$SCRIPT_DIR/capture_windows_serial.ps1"
PULSE_PS1="$SCRIPT_DIR/pulse_windows_serial.ps1"
LOADER_DIR_DEFAULT='/mnt/c/Users/lijian/Downloads/BEKEN_BKFIL_V2.1.11.15_20241114/BEKEN_BKFIL_V2.1.11.15_20241114'
LOADER_DIR=${BK_LOADER_DIR:-$LOADER_DIR_DEFAULT}
LOADER_EXE="$LOADER_DIR/bk_loader.exe"
JLINK_EXE_DEFAULT='/mnt/c/Program Files/SEGGER/JLink/JLink.exe'
JLINK_EXE=${JLINK_EXE:-$JLINK_EXE_DEFAULT}
DUAL_DIR="${BK7258_DUAL_DIR:-$OPENVELA_ROOT/nuttx/bk7258-dual}"
FIRMWARE="${BK7258_FIRMWARE:-}"
LOG_ROOT="$OPENVELA_ROOT/logs/bk7258-auto-debug"

CP_CONFIG_NAME=${CP_CONFIG_NAME:-t5ai_core_cp_base}
AP_CONFIG_NAME=${AP_CONFIG_NAME:-t5ai_core_ap_base}
DOWNLOAD_PORT=${BK_DOWNLOAD_PORT:-3}
CONSOLE_PORT=${BK_CONSOLE_PORT:-COM4}
CONSOLE_BAUD=${BK_CONSOLE_BAUD:-460800}
DOWNLOAD_BAUD=${BK_DOWNLOAD_BAUD:-6000000}
CAPTURE_SECONDS=${BK_CAPTURE_SECONDS:-25}

DO_BUILD=0
DO_FLASH=0
SPARSE_FLASH=0
BOOT_ONLY=0
APPLICATIONS_ONLY=0
COLD_CAPTURE=0
JLINK_RESET=0
RTS_RESET=0
ASSUME_YES=0
NO_CONSOLE=0
CP_CONFIG_EXPLICIT=0
AP_CONFIG_EXPLICIT=0

usage()
{
  cat <<USAGE
Usage: $(basename "$0") [options]

Actions:
  --build                 Build the selected CP_CONFIG_NAME/AP_CONFIG_NAME pair
  --flash                 Download through Windows bk_loader.exe
  --sparse-flash          With --flash, update boot/BL2/CP/AP when MCUboot profile is packaged; preserve data
  --boot-only             With --flash --sparse-flash, update only the boot segment
  --apps-only             With --flash --sparse-flash, update only CP/AP applications
  --cold-capture          Capture the selected UART and ask for a manual physical RESET
  --rts-reset             Capture UART, then pulse the selected download-port RTS
  --jlink-reset           Capture UART when enabled, then try J-Link RSetType 2
  --no-console            Do not open UART1 (required while P0/P1 own SWD/RTT)
  --yes                   Skip the destructive factory-rewrite confirmation

Options:
  --cp-config NAME        CP config (default: $CP_CONFIG_NAME)
  --ap-config NAME        AP config (default: $AP_CONFIG_NAME)
  --capture-seconds N     Serial capture duration (default: $CAPTURE_SECONDS)
  --download-port N       bk_loader port number (default: $DOWNLOAD_PORT / COM$DOWNLOAD_PORT)
  --console-port COMN     Windows console port (default: $CONSOLE_PORT)
  --console-baud N        Console baud (default: $CONSOLE_BAUD)
  --firmware PATH         Factory image path (default: PACKAGE/all-app-factory.bin)
  --package DIR           Dual-image directory containing verified firmware.bkpack (default: $DUAL_DIR)
  --log-root PATH         Output directory (default: $LOG_ROOT)
  -h, --help              Show this help

Examples:
  # Build the canonical T5-Board product, sparse-flash, and capture the warm path:
  BK7258_PRODUCT=t5_board_bringup $(basename "$0") --build --flash \
    --sparse-flash --no-console

  # Sparse-flash an already built image and capture:
  $(basename "$0") --flash --sparse-flash

  # Update only signed CP/AP after the automatic target trust preflight:
  $(basename "$0") --flash --sparse-flash --apps-only --no-console

  # Destructive factory rewrite; only with fresh owner authorization:
  $(basename "$0") --flash

  # Capture a physical RESET performed manually after the prompt:
  $(basename "$0") --cold-capture --capture-seconds 30

  # Pulse the selected downloader RTS only when that board route supports it:
  $(basename "$0") --rts-reset --capture-seconds 30

  # Experimentally request J-Link RESETPIN reset and capture; require BClk:
  $(basename "$0") --jlink-reset --capture-seconds 30

Port assignment on the current T5-Board:
  COM3 = downloader / bk_loader (-p 3)
  COM4 = UART1 console only when its DIP switch is enabled
  COM4 must remain unopened when P0/P1 are owned by SWD/RTT.

Every MCUboot flash action first compares the package's public BL1/BL2 trust
fingerprints with the running target through non-halting J-Link reads.  A
mismatch or unreadable target refuses the download; this path cannot rotate
trust roots. Flash also requires layout ID/SHA and every write range to match
the verified firmware.bkpack, dual manifest, build profile, and source files.
USAGE
}

while (($#)); do
  case "$1" in
    --build) DO_BUILD=1 ;;
    --flash) DO_FLASH=1 ;;
    --sparse-flash) SPARSE_FLASH=1 ;;
    --boot-only) BOOT_ONLY=1 ;;
    --apps-only) APPLICATIONS_ONLY=1 ;;
    --cold-capture) COLD_CAPTURE=1 ;;
    --rts-reset) RTS_RESET=1 ;;
    --jlink-reset) JLINK_RESET=1 ;;
    --no-console) NO_CONSOLE=1 ;;
    --yes) ASSUME_YES=1 ;;
    --cp-config) CP_CONFIG_NAME=${2:?missing value}; CP_CONFIG_EXPLICIT=1; shift ;;
    --ap-config) AP_CONFIG_NAME=${2:?missing value}; AP_CONFIG_EXPLICIT=1; shift ;;
    --capture-seconds) CAPTURE_SECONDS=${2:?missing value}; shift ;;
    --download-port) DOWNLOAD_PORT=${2:?missing value}; shift ;;
    --console-port) CONSOLE_PORT=${2:?missing value}; shift ;;
    --console-baud) CONSOLE_BAUD=${2:?missing value}; shift ;;
    --firmware) FIRMWARE=${2:?missing value}; shift ;;
    --package) DUAL_DIR=${2:?missing value}; shift ;;
    --log-root) LOG_ROOT=${2:?missing value}; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

if [[ -z "$FIRMWARE" ]]; then
  FIRMWARE="$DUAL_DIR/all-app-factory.bin"
fi

action_count=$((DO_FLASH + COLD_CAPTURE + RTS_RESET + JLINK_RESET))
if ((action_count != 1)); then
  echo "ERROR: choose exactly one of --flash, --cold-capture, --rts-reset, or --jlink-reset" >&2
  usage >&2
  exit 2
fi

if ((SPARSE_FLASH && !DO_FLASH)); then
  echo "ERROR: --sparse-flash requires --flash" >&2
  exit 2
fi

if ((BOOT_ONLY && (!DO_FLASH || !SPARSE_FLASH))); then
  echo "ERROR: --boot-only requires --flash --sparse-flash" >&2
  exit 2
fi

if ((APPLICATIONS_ONLY && (!DO_FLASH || !SPARSE_FLASH))); then
  echo "ERROR: --apps-only requires --flash --sparse-flash" >&2
  exit 2
fi

if ((BOOT_ONLY && APPLICATIONS_ONLY)); then
  echo "ERROR: --boot-only and --apps-only are mutually exclusive" >&2
  exit 2
fi

for n in "$CAPTURE_SECONDS" "$DOWNLOAD_PORT" "$CONSOLE_BAUD" "$DOWNLOAD_BAUD"; do
  [[ $n =~ ^[0-9]+$ ]] || { echo "ERROR: numeric option expected, got '$n'" >&2; exit 2; }
done

command -v powershell.exe >/dev/null 2>&1 || {
  echo "ERROR: WSL Windows interop is unavailable (powershell.exe not found)" >&2
  exit 1
}

[[ -f "$CAPTURE_PS1" ]] || { echo "ERROR: missing $CAPTURE_PS1" >&2; exit 1; }
if ((DO_FLASH)); then
  [[ -x "$LOADER_EXE" || -f "$LOADER_EXE" ]] || { echo "ERROR: missing $LOADER_EXE" >&2; exit 1; }
fi
if ((RTS_RESET)); then
  [[ -f "$PULSE_PS1" ]] || { echo "ERROR: missing $PULSE_PS1" >&2; exit 1; }
fi
if ((JLINK_RESET)); then
  [[ -x "$JLINK_EXE" || -f "$JLINK_EXE" ]] || { echo "ERROR: missing $JLINK_EXE" >&2; exit 1; }
fi

[[ -f "$BKPACK_TOOL" ]] || {
  echo "ERROR: missing payload verifier $BKPACK_TOOL" >&2
  exit 1
}
[[ -f "$TRUST_CHAIN_TOOL" ]] || {
  echo "ERROR: missing trust-chain tool $TRUST_CHAIN_TOOL" >&2
  exit 1
}

if ((DO_BUILD)); then
  echo "==> Building CP=$CP_CONFIG_NAME AP=$AP_CONFIG_NAME"
  CP_CONFIG_NAME="$CP_CONFIG_NAME" AP_CONFIG_NAME="$AP_CONFIG_NAME" "$BUILD_SCRIPT"
fi

[[ -f "$FIRMWARE" ]] || { echo "ERROR: missing firmware $FIRMWARE" >&2; exit 1; }

MANIFEST="$DUAL_DIR/bk7258-dual-image.json"
FIRMWARE_PACKAGE="$DUAL_DIR/firmware.bkpack"
BOOT_IMAGE="$DUAL_DIR/bl_crc.bin"
CP_IMAGE="$DUAL_DIR/app_crc_flash.bin"
AP_IMAGE="$DUAL_DIR/app1_crc_flash.bin"
BL2_IMAGE="$DUAL_DIR/bl2_crc.bin"
BL2_SECONDARY_IMAGE="$DUAL_DIR/bl2_secondary_crc.bin"
MANIFEST_PRIMARY_IMAGE="$DUAL_DIR/bl1-manifest-primary.bin"
MANIFEST_SECONDARY_IMAGE="$DUAL_DIR/bl1-manifest-secondary.bin"
LITTLEFS_CLEAR_IMAGE="$DUAL_DIR/littlefs_factory_clear.bin"

PROFILE_FILE="$DUAL_DIR/build-profile.txt"
TRUST_CHAIN_CONTRACT="$DUAL_DIR/bk7258-trust-chain.json"
PACKAGED_CP_CONFIG=unknown
PACKAGED_AP_CONFIG=unknown
PACKAGED_MCUBOOT_PROFILE=false
PACKAGED_BL1_MANIFEST_RAW_PAGE=false
if [[ -f "$PROFILE_FILE" ]]; then
  PACKAGED_CP_CONFIG=$(sed -n 's/^CP_CONFIG_NAME=//p' "$PROFILE_FILE" | head -1)
  PACKAGED_AP_CONFIG=$(sed -n 's/^AP_CONFIG_NAME=//p' "$PROFILE_FILE" | head -1)
  if grep -qx 'MCUBOOT_PROFILE=true' "$PROFILE_FILE"; then
    PACKAGED_MCUBOOT_PROFILE=true
  fi
  if grep -qx 'BL1_MANIFEST_RAW_PAGE=true' "$PROFILE_FILE"; then
    PACKAGED_BL1_MANIFEST_RAW_PAGE=true
  fi
fi

if ((DO_FLASH)); then
  [[ -f "$MANIFEST" ]] || { echo "ERROR: missing image manifest $MANIFEST" >&2; exit 1; }
  [[ -f "$PROFILE_FILE" ]] || { echo "ERROR: missing build profile $PROFILE_FILE" >&2; exit 1; }
  [[ -f "$FIRMWARE_PACKAGE" ]] || {
    echo "ERROR: Flash requires a verified firmware.bkpack with an explicit layout ID/SHA; legacy or raw directory packages are not download-authorized" >&2
    exit 1
  }
  FLASH_CONTRACT_JSON=$(python3 "$BKPACK_TOOL" flash-contract \
    --package "$FIRMWARE_PACKAGE" --source "$DUAL_DIR")
  FLASH_CONTRACT_FIELDS=$(python3 - "$FLASH_CONTRACT_JSON" <<'PY'
import json
import sys

contract = json.loads(sys.argv[1])
if contract.get("status") != "pass" or contract.get("source_verified") is not True:
    raise SystemExit("verified Flash contract is missing")
layout = contract.get("layout")
plans = contract.get("plans")
if not isinstance(layout, dict) or not isinstance(plans, dict):
    raise SystemExit("verified Flash contract layout/plans are missing")

expected = {
    "apps": {
        "primary_cp_app": "app_crc_flash.bin",
        "primary_ap_app": "app1_crc_flash.bin",
    },
    "normal": {
        "primary_bootloader": "bl_crc.bin",
        "primary_cp_app": "app_crc_flash.bin",
        "primary_ap_app": "app1_crc_flash.bin",
        "primary_bl2": "bl2_crc.bin",
        "secondary_bl2": "bl2_secondary_crc.bin",
    },
    "factory": {
        "factory_prefix": "all-app-factory.bin",
        "littlefs_clear": "littlefs_factory_clear.bin",
        "primary_bl2": "bl2_crc.bin",
        "secondary_bl2": "bl2_secondary_crc.bin",
    },
}
indexed = {}
for plan_name, names in expected.items():
    plan = plans.get(plan_name)
    if not isinstance(plan, dict):
        raise SystemExit(f"verified Flash plan is missing: {plan_name}")
    if (plan.get("layout_id") != layout.get("layout_id") or
            plan.get("layout_sha256") != layout.get("layout_sha256") or
            plan.get("flash_size") != layout.get("flash_size")):
        raise SystemExit(f"Flash plan layout binding drift: {plan_name}")
    rows = plan.get("segments")
    if not isinstance(rows, list):
        raise SystemExit(f"Flash plan segments are missing: {plan_name}")
    by_name = {row.get("name"): row for row in rows if isinstance(row, dict)}
    if set(by_name) != set(names):
        raise SystemExit(f"Flash plan segment set drift: {plan_name}")
    for name, member in names.items():
        row = by_name[name]
        if row.get("member") != member:
            raise SystemExit(f"Flash plan member drift: {plan_name}/{name}")
        if row.get("physical_end") != row.get("physical_offset") + row.get("length"):
            raise SystemExit(f"Flash plan range drift: {plan_name}/{name}")
    indexed[plan_name] = by_name

def emit(key, value):
    if not isinstance(value, (str, int)) or isinstance(value, bool):
        raise SystemExit(f"Flash contract value is invalid: {key}")
    print(f"{key}\t{value}")

emit("LAYOUT_ID", layout.get("layout_id"))
emit("LAYOUT_SHA256", layout.get("layout_sha256"))
emit("LAYOUT_SOURCE", layout.get("layout_source"))
emit("FLASH_SIZE", layout.get("flash_size"))
emit("PACKAGE_SHA256", contract.get("package_sha256"))
for prefix, plan_name, segment_name in (
    ("BOOT", "normal", "primary_bootloader"),
    ("CP", "apps", "primary_cp_app"),
    ("AP", "apps", "primary_ap_app"),
    ("BL2", "normal", "primary_bl2"),
    ("BL2_SECONDARY", "normal", "secondary_bl2"),
    ("FACTORY_PREFIX", "factory", "factory_prefix"),
    ("LITTLEFS", "factory", "littlefs_clear"),
):
    row = indexed[plan_name][segment_name]
    emit(prefix + "_OFFSET", f"0x{row['physical_offset']:x}")
    emit(prefix + "_SIZE", f"0x{row['length']:x}")
PY
  )
  declare -A VERIFIED_FLASH_CONTRACT=()
  while IFS=$'\t' read -r key value; do
    [[ -n "$key" && -n "$value" && -z ${VERIFIED_FLASH_CONTRACT[$key]+x} ]] || {
      echo "ERROR: malformed or duplicate verified Flash contract field" >&2
      exit 1
    }
    VERIFIED_FLASH_CONTRACT[$key]=$value
  done <<<"$FLASH_CONTRACT_FIELDS"
  EXPECTED_LAYOUT_ID=${VERIFIED_FLASH_CONTRACT[LAYOUT_ID]}
  EXPECTED_LAYOUT_SHA256=${VERIFIED_FLASH_CONTRACT[LAYOUT_SHA256]}
  EXPECTED_LAYOUT_SOURCE=${VERIFIED_FLASH_CONTRACT[LAYOUT_SOURCE]}
  VERIFIED_PACKAGE_SHA256=${VERIFIED_FLASH_CONTRACT[PACKAGE_SHA256]}
  BOOT_OFFSET=${VERIFIED_FLASH_CONTRACT[BOOT_OFFSET]}
  BOOT_SIZE=${VERIFIED_FLASH_CONTRACT[BOOT_SIZE]}
  CP_OFFSET=${VERIFIED_FLASH_CONTRACT[CP_OFFSET]}
  CP_SIZE=${VERIFIED_FLASH_CONTRACT[CP_SIZE]}
  AP_OFFSET=${VERIFIED_FLASH_CONTRACT[AP_OFFSET]}
  AP_SIZE=${VERIFIED_FLASH_CONTRACT[AP_SIZE]}
  BL2_OFFSET=${VERIFIED_FLASH_CONTRACT[BL2_OFFSET]}
  BL2_SIZE=${VERIFIED_FLASH_CONTRACT[BL2_SIZE]}
  BL2_SECONDARY_OFFSET=${VERIFIED_FLASH_CONTRACT[BL2_SECONDARY_OFFSET]}
  BL2_SECONDARY_SIZE=${VERIFIED_FLASH_CONTRACT[BL2_SECONDARY_SIZE]}
  FACTORY_PREFIX_OFFSET=${VERIFIED_FLASH_CONTRACT[FACTORY_PREFIX_OFFSET]}
  FACTORY_PREFIX_SIZE=${VERIFIED_FLASH_CONTRACT[FACTORY_PREFIX_SIZE]}
  LITTLEFS_OFFSET=${VERIFIED_FLASH_CONTRACT[LITTLEFS_OFFSET]}
  LITTLEFS_SIZE=${VERIFIED_FLASH_CONTRACT[LITTLEFS_SIZE]}
  printf -v BL2_SECONDARY_OFFSET_HEX '0x%x' "$BL2_SECONDARY_OFFSET"
  echo "==> Verified package layout: $EXPECTED_LAYOUT_ID ($EXPECTED_LAYOUT_SHA256)"
  MANIFEST_MCUBOOT_PROFILE=$(python3 - "$MANIFEST" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    manifest = json.load(stream)
print("true" if isinstance(manifest.get("secondary_pair"), dict) else "false")
PY
  )
  if [[ "$MANIFEST_MCUBOOT_PROFILE" != "$PACKAGED_MCUBOOT_PROFILE" ]]; then
    echo "ERROR: package manifest and build profile disagree on MCUboot" >&2
    exit 1
  fi
  if [[ "$PACKAGED_MCUBOOT_PROFILE" == true ]]; then
    [[ -f "$TRUST_CHAIN_CONTRACT" ]] || {
      echo "ERROR: missing MCUboot trust contract $TRUST_CHAIN_CONTRACT" >&2
      exit 1
    }
    grep -qx 'TRUST_CHAIN_PREFLIGHT_REQUIRED=true' "$PROFILE_FILE" || {
      echo "ERROR: packaged MCUboot profile does not require trust preflight" >&2
      exit 1
    }
    [[ -x "$JLINK_EXE" || -f "$JLINK_EXE" ]] || {
      echo "ERROR: MCUboot flash preflight requires $JLINK_EXE" >&2
      exit 1
    }
  fi
fi
if ((SPARSE_FLASH)); then
  if ((APPLICATIONS_ONLY)); then
    sparse_images=("$CP_IMAGE" "$AP_IMAGE")
  else
    sparse_images=("$BOOT_IMAGE")
  fi
  if ((!BOOT_ONLY && !APPLICATIONS_ONLY)); then
    sparse_images+=("$CP_IMAGE" "$AP_IMAGE")
    if [[ "$PACKAGED_MCUBOOT_PROFILE" == true ]]; then
      sparse_images+=("$BL2_IMAGE" "$BL2_SECONDARY_IMAGE")
      if [[ "$PACKAGED_BL1_MANIFEST_RAW_PAGE" == true ]]; then
        sparse_images+=("$MANIFEST_PRIMARY_IMAGE" "$MANIFEST_SECONDARY_IMAGE")
      fi
    fi
  fi

  for image in "${sparse_images[@]}"; do
    [[ -f "$image" ]] || { echo "ERROR: missing sparse image $image" >&2; exit 1; }
  done

  BOOT_IMAGE_SIZE=$(stat -c %s "$BOOT_IMAGE")
  CP_IMAGE_SIZE=$(stat -c %s "$CP_IMAGE")
  AP_IMAGE_SIZE=$(stat -c %s "$AP_IMAGE")
  BL2_IMAGE_SIZE=0
  BL2_SECONDARY_IMAGE_SIZE=0
  MANIFEST_PRIMARY_SIZE=0
  MANIFEST_SECONDARY_SIZE=0
  if [[ "$PACKAGED_MCUBOOT_PROFILE" == true && $BOOT_ONLY -eq 0 &&
        $APPLICATIONS_ONLY -eq 0 ]]; then
    BL2_IMAGE_SIZE=$(stat -c %s "$BL2_IMAGE")
    BL2_SECONDARY_IMAGE_SIZE=$(stat -c %s "$BL2_SECONDARY_IMAGE")
    if [[ "$PACKAGED_BL1_MANIFEST_RAW_PAGE" == true ]]; then
      MANIFEST_PRIMARY_SIZE=$(stat -c %s "$MANIFEST_PRIMARY_IMAGE")
      MANIFEST_SECONDARY_SIZE=$(stat -c %s "$MANIFEST_SECONDARY_IMAGE")
    fi
  fi

  # Refuse artifacts that differ from the package-verified Flash plan before
  # bk_loader can erase across a resolved-layout boundary.

  ((BOOT_IMAGE_SIZE == BOOT_SIZE)) || {
    echo "ERROR: sparse boot image length differs from the verified package plan" >&2
    exit 1
  }
  ((CP_IMAGE_SIZE == CP_SIZE)) || {
    echo "ERROR: sparse CP image length differs from the verified package plan" >&2
    exit 1
  }
  ((AP_IMAGE_SIZE == AP_SIZE)) || {
    echo "ERROR: sparse AP image length differs from the verified package plan" >&2
    exit 1
  }
  if [[ "$PACKAGED_MCUBOOT_PROFILE" == true && $BOOT_ONLY -eq 0 &&
        $APPLICATIONS_ONLY -eq 0 ]]; then
    ((BL2_IMAGE_SIZE == BL2_SIZE)) || {
      echo "ERROR: sparse BL2 image length differs from the verified package plan" >&2
      exit 1
    }
    ((BL2_SECONDARY_IMAGE_SIZE == BL2_SECONDARY_SIZE)) || {
      echo "ERROR: sparse secondary BL2 image length differs from the verified package plan" >&2
      exit 1
    }
    if [[ "$PACKAGED_BL1_MANIFEST_RAW_PAGE" == true ]]; then
      ((MANIFEST_PRIMARY_SIZE == MANIFEST_A_SIZE)) || {
        echo "ERROR: primary BL1 Manifest page must exactly fill its partition" >&2
        exit 1
      }
      ((MANIFEST_SECONDARY_SIZE == MANIFEST_B_SIZE)) || {
        echo "ERROR: secondary BL1 Manifest page must exactly fill its partition" >&2
        exit 1
      }
    fi
  fi
elif ((DO_FLASH)); then
  [[ $(readlink -f "$FIRMWARE") == $(readlink -f "$DUAL_DIR/all-app-factory.bin") ]] || {
    echo "ERROR: destructive factory rewrite requires the verified packaged prefix" >&2
    exit 1
  }
  [[ -f "$LITTLEFS_CLEAR_IMAGE" ]] || {
    echo "ERROR: missing LittleFS factory-clear segment $LITTLEFS_CLEAR_IMAGE" >&2
    exit 1
  }
  FIRMWARE_SIZE=$(stat -c %s "$FIRMWARE")
  LITTLEFS_CLEAR_SIZE=$(stat -c %s "$LITTLEFS_CLEAR_IMAGE")
  ((FIRMWARE_SIZE == FACTORY_PREFIX_SIZE && LITTLEFS_CLEAR_SIZE == LITTLEFS_SIZE)) || {
    echo "ERROR: factory segments differ from the verified package plan" >&2
    exit 1
  }
  if [[ "$PACKAGED_MCUBOOT_PROFILE" == true ]]; then
    [[ -f "$BL2_IMAGE" ]] || { echo "ERROR: missing MCUboot BL2 image $BL2_IMAGE" >&2; exit 1; }
    [[ -f "$BL2_SECONDARY_IMAGE" ]] || { echo "ERROR: missing secondary MCUboot BL2 image $BL2_SECONDARY_IMAGE" >&2; exit 1; }
    BL2_IMAGE_SIZE=$(stat -c %s "$BL2_IMAGE")
    BL2_SECONDARY_IMAGE_SIZE=$(stat -c %s "$BL2_SECONDARY_IMAGE")
    ((BL2_IMAGE_SIZE == BL2_SIZE)) || {
      echo "ERROR: factory BL2 image length differs from the verified package plan" >&2
      exit 1
    }
    ((BL2_SECONDARY_IMAGE_SIZE == BL2_SECONDARY_SIZE)) || {
      echo "ERROR: factory secondary BL2 image length differs from the verified package plan" >&2
      exit 1
    }
  fi
fi

if ((!DO_BUILD)); then
  if [[ $PACKAGED_CP_CONFIG == unknown || $PACKAGED_AP_CONFIG == unknown ]]; then
    echo "WARNING: packaged CP/AP profile is unknown; rebuild once with the current builder to create build-profile.txt" >&2
  else
    echo "==> Packaged profile: CP=$PACKAGED_CP_CONFIG AP=$PACKAGED_AP_CONFIG"
    if ((CP_CONFIG_EXPLICIT)) && [[ $CP_CONFIG_NAME != "$PACKAGED_CP_CONFIG" ]]; then
      echo "ERROR: expected CP=$CP_CONFIG_NAME but packaged CP=$PACKAGED_CP_CONFIG" >&2
      exit 1
    fi
    if ((AP_CONFIG_EXPLICIT)) && [[ $AP_CONFIG_NAME != "$PACKAGED_AP_CONFIG" ]]; then
      echo "ERROR: expected AP=$AP_CONFIG_NAME but packaged AP=$PACKAGED_AP_CONFIG" >&2
      exit 1
    fi
  fi
fi

# UART1/COM4 and the T5-Board SWD/RTT route share P0/P1.  Never open COM4 for
# a packaged SWD image, even if the caller omitted --no-console.  On the
# current board its UART1 DIP switch is physically off as an additional gate.

if [[ -f "$DUAL_DIR/nuttx-cp.config" ]] &&
   grep -qx 'CONFIG_BK7258_SWD_DEBUG=y' "$DUAL_DIR/nuttx-cp.config"; then
  NO_CONSOLE=1
fi

if ((DO_FLASH && !SPARSE_FLASH && !ASSUME_YES)); then
  echo "WARNING: factory download rewrites A/B/metadata and clears LittleFS."
  echo "WARNING: the one-time ADR-004 migration is complete; require fresh owner authorization."
  if [[ -t 0 ]]; then
    read -r -p "Type FLASH to continue: " answer
    [[ $answer == FLASH ]] || { echo "Cancelled"; exit 3; }
  else
    echo "ERROR: non-interactive factory download requires --yes" >&2
    exit 3
  fi
fi

# Verify the two independent CH342 ports before starting a destructive action.
PORTS=$(powershell.exe -NoProfile -Command '[System.IO.Ports.SerialPort]::GetPortNames()' | tr -d '\r')
grep -qx "COM${DOWNLOAD_PORT}" <<<"$PORTS" || {
  echo "ERROR: downloader COM${DOWNLOAD_PORT} is not present" >&2
  printf '%s\n' "$PORTS" >&2
  exit 1
}
if ((!NO_CONSOLE)); then
  grep -qx "$CONSOLE_PORT" <<<"$PORTS" || {
    echo "ERROR: console $CONSOLE_PORT is not present" >&2
    printf '%s\n' "$PORTS" >&2
    exit 1
  }
fi

STAMP=$(date +%Y%m%d-%H%M%S)
RUN_DIR="$LOG_ROOT/$STAMP"
mkdir -p "$RUN_DIR"
SERIAL_RAW="$RUN_DIR/serial.raw"
SERIAL_TEXT="$RUN_DIR/serial.txt"
SERIAL_STDOUT="$RUN_DIR/serial-capture.stdout.log"
READY_FILE="$RUN_DIR/serial.ready"
DOWNLOAD_LOG="$RUN_DIR/download.log"
SUMMARY_FILE="$RUN_DIR/summary.txt"
JLINK_LOG="$RUN_DIR/jlink-reset.log"
RESET_LOG="$RUN_DIR/serial-reset.log"
ARTIFACT_FILE="$RUN_DIR/artifacts.sha256"
TRUST_JLINK_COMMANDS="$RUN_DIR/trust-jlink.cmd"
TRUST_JLINK_LOG="$RUN_DIR/trust-jlink.log"
TRUST_PREFLIGHT_JSON="$RUN_DIR/trust-preflight.json"

# A normal MCUboot download may update only a target that already trusts the
# package's BL1 Manifest and MCUboot roots.  These are non-halting reads: no
# reset, register write, Flash write, or implicit root-rotation path exists.

if ((DO_FLASH)) && [[ "$PACKAGED_MCUBOOT_PROFILE" == true ]]; then
  echo "==> Reading BL1/BL2 public trust fingerprints through J-Link"
  python3 "$TRUST_CHAIN_TOOL" commands \
    --contract "$TRUST_CHAIN_CONTRACT" > "$TRUST_JLINK_COMMANDS"
  set +e
  "$JLINK_EXE" -device CORTEX-M33 -if SWD -speed 1000 -autoconnect 1 \
    < "$TRUST_JLINK_COMMANDS" 2>&1 | tee "$TRUST_JLINK_LOG"
  trust_jlink_rc=${PIPESTATUS[0]}
  set -e
  if ((trust_jlink_rc != 0)); then
    echo "ERROR: J-Link trust preflight exited with $trust_jlink_rc; refusing download" >&2
    exit "$trust_jlink_rc"
  fi
  if ! python3 "$TRUST_CHAIN_TOOL" verify \
      --contract "$TRUST_CHAIN_CONTRACT" \
      --jlink-log "$TRUST_JLINK_LOG" \
      --json "$TRUST_PREFLIGHT_JSON"; then
    echo "ERROR: target trust roots do not match this package; refusing download" >&2
    exit 1
  fi
fi

{
  printf 'ACTION_BUILD=%s\n' "$DO_BUILD"
  if ((DO_BUILD)); then
    printf 'REQUESTED_CP_CONFIG_NAME=%s\n' "$CP_CONFIG_NAME"
    printf 'REQUESTED_AP_CONFIG_NAME=%s\n' "$AP_CONFIG_NAME"
  fi
  printf 'PACKAGED_CP_CONFIG_NAME=%s\n' "$PACKAGED_CP_CONFIG"
  printf 'PACKAGED_AP_CONFIG_NAME=%s\n' "$PACKAGED_AP_CONFIG"
  if ((DO_FLASH)); then
    printf 'PARTITION_LAYOUT_ID=%s\n' "$EXPECTED_LAYOUT_ID"
    printf 'PARTITION_LAYOUT_SHA256=%s\n' "$EXPECTED_LAYOUT_SHA256"
    printf 'PARTITION_LAYOUT_SOURCE=%s\n' "$EXPECTED_LAYOUT_SOURCE"
    printf 'VERIFIED_PACKAGE_SHA256=%s\n' "$VERIFIED_PACKAGE_SHA256"
    sha256sum "$FIRMWARE_PACKAGE"
  fi
  sha256sum "$FIRMWARE"
  stat -c '%y %s %n' "$FIRMWARE"
  if ((SPARSE_FLASH)); then
    sha256sum "${sparse_images[@]}"
    stat -c '%y %s %n' "${sparse_images[@]}"
  elif ((DO_FLASH)); then
    sha256sum "$LITTLEFS_CLEAR_IMAGE"
    stat -c '%y %s %n' "$LITTLEFS_CLEAR_IMAGE"
    if [[ "$PACKAGED_MCUBOOT_PROFILE" == true ]]; then
      sha256sum "$BL2_IMAGE"
      stat -c '%y %s %n' "$BL2_IMAGE"
      sha256sum "$BL2_SECONDARY_IMAGE"
      stat -c '%y %s %n' "$BL2_SECONDARY_IMAGE"
    fi
  fi
  if [[ -f "$PROFILE_FILE" ]]; then
    cat "$PROFILE_FILE"
  fi
  if [[ -f "$TRUST_CHAIN_CONTRACT" ]]; then
    sha256sum "$TRUST_CHAIN_CONTRACT"
    stat -c '%y %s %n' "$TRUST_CHAIN_CONTRACT"
  fi
  if [[ -f "$TRUST_PREFLIGHT_JSON" ]]; then
    sha256sum "$TRUST_PREFLIGHT_JSON" "$TRUST_JLINK_LOG"
    cat "$TRUST_PREFLIGHT_JSON"
  fi
} > "$ARTIFACT_FILE"

PS1_WIN=$(wslpath -w "$CAPTURE_PS1")
RAW_WIN=$(wslpath -w "$SERIAL_RAW")
READY_WIN=$(wslpath -w "$READY_FILE")

cleanup()
{
  if [[ -n ${CAPTURE_PID:-} ]] && kill -0 "$CAPTURE_PID" 2>/dev/null; then
    kill "$CAPTURE_PID" 2>/dev/null || true
    wait "$CAPTURE_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

if ((NO_CONSOLE)); then
  : >"$SERIAL_RAW"
  : >"$SERIAL_STDOUT"
  CAPTURE_PID=
  echo "==> UART capture disabled; P0/P1 remain exclusively owned by the SWD debug route"
else
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PS1_WIN" \
    -Port "$CONSOLE_PORT" \
    -Baud "$CONSOLE_BAUD" \
    -DurationSec "$CAPTURE_SECONDS" \
    -OutputFile "$RAW_WIN" \
    -ReadyFile "$READY_WIN" \
    >"$SERIAL_STDOUT" 2>&1 &
  CAPTURE_PID=$!

  # A cold Windows PowerShell/interop startup can exceed five seconds even
  # though the console is healthy.  Keep the flash gate fail-closed, but allow
  # up to 30 seconds for the capture process to create its ready marker.

  for _ in $(seq 1 600); do
    [[ -f "$READY_FILE" ]] && break
    if ! kill -0 "$CAPTURE_PID" 2>/dev/null; then
      wait "$CAPTURE_PID" || true
      echo "ERROR: serial capture failed before becoming ready" >&2
      cat "$SERIAL_STDOUT" >&2 || true
      exit 1
    fi
    sleep 0.05
  done

  [[ -f "$READY_FILE" ]] || {
    echo "ERROR: timed out opening $CONSOLE_PORT" >&2
    cat "$SERIAL_STDOUT" >&2 || true
    exit 1
  }

  echo "==> Capturing $CONSOLE_PORT at $CONSOLE_BAUD baud"
fi

if ((DO_FLASH)); then
  if ((SPARSE_FLASH)); then
    BOOT_IMAGE_WIN=$(wslpath -m "$BOOT_IMAGE")
    CP_IMAGE_WIN=$(wslpath -m "$CP_IMAGE")
    AP_IMAGE_WIN=$(wslpath -m "$AP_IMAGE")
    printf -v BOOT_LENGTH_HEX '0x%x' "$(stat -c %s "$BOOT_IMAGE")"
    printf -v CP_LENGTH_HEX '0x%x' "$(stat -c %s "$CP_IMAGE")"
    printf -v AP_LENGTH_HEX '0x%x' "$(stat -c %s "$AP_IMAGE")"
    MAIN_BIN_MULTI=
    if ((!APPLICATIONS_ONLY)); then
      MAIN_BIN_MULTI="${BOOT_IMAGE_WIN}@${BOOT_OFFSET}-${BOOT_LENGTH_HEX},"
    fi
    if ((BOOT_ONLY)); then
      MAIN_BIN_MULTI=${MAIN_BIN_MULTI%,}
      echo "==> Boot-only sparse download through COM${DOWNLOAD_PORT}; all application/data regions preserved"
    else
      if [[ "$PACKAGED_MCUBOOT_PROFILE" == true && $APPLICATIONS_ONLY -eq 0 ]]; then
        BL2_IMAGE_WIN=$(wslpath -m "$BL2_IMAGE")
        BL2_SECONDARY_IMAGE_WIN=$(wslpath -m "$BL2_SECONDARY_IMAGE")
        printf -v BL2_LENGTH_HEX '0x%x' "$BL2_IMAGE_SIZE"
        printf -v BL2_SECONDARY_LENGTH_HEX '0x%x' "$BL2_SECONDARY_IMAGE_SIZE"
        MAIN_BIN_MULTI+="${BL2_IMAGE_WIN}@${BL2_OFFSET}-${BL2_LENGTH_HEX},"
        MAIN_BIN_MULTI+="${BL2_SECONDARY_IMAGE_WIN}@${BL2_SECONDARY_OFFSET_HEX}-${BL2_SECONDARY_LENGTH_HEX},"
        if [[ "$PACKAGED_BL1_MANIFEST_RAW_PAGE" == true ]]; then
          MANIFEST_PRIMARY_WIN=$(wslpath -m "$MANIFEST_PRIMARY_IMAGE")
          MANIFEST_SECONDARY_WIN=$(wslpath -m "$MANIFEST_SECONDARY_IMAGE")
          printf -v MANIFEST_PRIMARY_LENGTH_HEX '0x%x' "$MANIFEST_PRIMARY_SIZE"
          printf -v MANIFEST_SECONDARY_LENGTH_HEX '0x%x' "$MANIFEST_SECONDARY_SIZE"
          MAIN_BIN_MULTI+="${MANIFEST_PRIMARY_WIN}@${MANIFEST_A_OFFSET}-${MANIFEST_PRIMARY_LENGTH_HEX},"
          MAIN_BIN_MULTI+="${MANIFEST_SECONDARY_WIN}@${MANIFEST_B_OFFSET}-${MANIFEST_SECONDARY_LENGTH_HEX},"
        fi
      fi
      MAIN_BIN_MULTI+="${CP_IMAGE_WIN}@${CP_OFFSET}-${CP_LENGTH_HEX},"
      MAIN_BIN_MULTI+="${AP_IMAGE_WIN}@${AP_OFFSET}-${AP_LENGTH_HEX}"
      if ((APPLICATIONS_ONLY)); then
        echo "==> CP/AP-only sparse download through COM${DOWNLOAD_PORT}; boot/BL2/data preserved"
      elif [[ "$PACKAGED_MCUBOOT_PROFILE" == true ]]; then
        echo "==> Sparse MCUboot download through COM${DOWNLOAD_PORT}; BL2/CP/AP updated, LittleFS preserved"
      else
        echo "==> Sparse download through COM${DOWNLOAD_PORT}; LittleFS preserved"
      fi
    fi
  else
    FIRMWARE_WIN=$(wslpath -m "$FIRMWARE")
    LITTLEFS_CLEAR_WIN=$(wslpath -m "$LITTLEFS_CLEAR_IMAGE")
    MAIN_BIN_MULTI="${FIRMWARE_WIN}@${FACTORY_PREFIX_OFFSET}-${FACTORY_PREFIX_SIZE},"
    if [[ "$PACKAGED_MCUBOOT_PROFILE" == true ]]; then
      BL2_IMAGE_WIN=$(wslpath -m "$BL2_IMAGE")
      BL2_SECONDARY_IMAGE_WIN=$(wslpath -m "$BL2_SECONDARY_IMAGE")
      printf -v BL2_LENGTH_HEX '0x%x' "$BL2_IMAGE_SIZE"
      printf -v BL2_SECONDARY_LENGTH_HEX '0x%x' "$BL2_SECONDARY_IMAGE_SIZE"
      MAIN_BIN_MULTI+="${BL2_IMAGE_WIN}@${BL2_OFFSET}-${BL2_LENGTH_HEX},"
      MAIN_BIN_MULTI+="${BL2_SECONDARY_IMAGE_WIN}@${BL2_SECONDARY_OFFSET_HEX}-${BL2_SECONDARY_LENGTH_HEX},"
    fi
    MAIN_BIN_MULTI+="${LITTLEFS_CLEAR_WIN}@${LITTLEFS_OFFSET}-${LITTLEFS_SIZE}"
    if [[ "$PACKAGED_MCUBOOT_PROFILE" == true ]]; then
      echo "==> Bounded MCUboot factory rewrite through COM${DOWNLOAD_PORT}; BL2 added, usr_config/tail preserved"
    else
      echo "==> Bounded factory rewrite through COM${DOWNLOAD_PORT}; usr_config/tail preserved"
    fi
  fi
  set +e
  (
    cd "$LOADER_DIR"
    "$LOADER_EXE" download \
      -p "$DOWNLOAD_PORT" \
      -b "$DOWNLOAD_BAUD" \
      --uart-type OTHER \
      --mainBin-multi "$MAIN_BIN_MULTI" \
      --reboot 1 \
      --fast-link 1
  ) 2>&1 | tee "$DOWNLOAD_LOG"
  loader_rc=${PIPESTATUS[0]}
  set -e
  if grep -aEiq -- \
      '->[[:space:]]*fail|GetBus fail|Writing Flash Failed|Read Flash Failed' \
      "$DOWNLOAD_LOG"; then
    echo "ERROR: bk_loader reported a flash operation failure; refusing the global success banner" >&2
    loader_rc=1
  elif grep -aFq 'Writing Flash OK' "$DOWNLOAD_LOG" &&
     grep -aFq '{All Finished Successfully}' "$DOWNLOAD_LOG"; then
    if ((loader_rc != 0)); then
      echo "WARNING: bk_loader returned $loader_rc despite explicit success markers; normalizing to success" >&2
    fi
    loader_rc=0
  else
    echo "ERROR: bk_loader did not emit both complete success markers" >&2
    loader_rc=1
  fi
elif ((RTS_RESET)); then
  echo "==> Pulsing COM${DOWNLOAD_PORT} RTS for a verified physical reset"
  PULSE_WIN=$(wslpath -w "$PULSE_PS1")
  set +e
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$PULSE_WIN" \
    -Port "COM${DOWNLOAD_PORT}" -Mode RTS -PulseMs 150 \
    2>&1 | tee "$RESET_LOG"
  reset_rc=${PIPESTATUS[0]}
  set -e
  if ((reset_rc != 0)); then
    echo "ERROR: COM${DOWNLOAD_PORT} RTS reset exited with $reset_rc" >&2
  fi
elif ((JLINK_RESET)); then
  echo "==> Resetting through J-Link RESETPIN strategy (RSetType 2)"
  set +e
  printf 'RSetType 2\nReset\nGo\nExit\n' | \
    "$JLINK_EXE" -device STAR -if SWD -speed 1000 -autoconnect 1 \
    2>&1 | tee "$JLINK_LOG"
  jlink_rc=${PIPESTATUS[1]}
  set -e
  if ((jlink_rc != 0)); then
    echo "ERROR: J-Link reset exited with $jlink_rc" >&2
  fi
else
  echo
  echo "==> Serial capture is ready. Press the board physical RESET now."
  echo "==> Capture will stop automatically after ${CAPTURE_SECONDS}s."
fi

capture_rc=0
if [[ -n "$CAPTURE_PID" ]]; then
  set +e
  wait "$CAPTURE_PID"
  capture_rc=$?
  set -e
  CAPTURE_PID=
fi

python3 - "$SERIAL_RAW" "$SERIAL_TEXT" "$SUMMARY_FILE" <<'PY'
from pathlib import Path
import sys

raw_path, text_path, summary_path = map(Path, sys.argv[1:])
data = raw_path.read_bytes() if raw_path.exists() else b""
text = data.decode("utf-8", errors="replace").replace("\x00", "")
text_path.write_text(text)

ordered = [
    "B1PAGE", "B2INIT", "B2GO", "B2GENBAD", "B2GORET", "B2TRYA", "B2ARET",
    "B2TRYB", "B2BRET", "B2GOOK", "B2SELA", "B2SELB",
    "B2APHDR", "B2APMSP", "B2APTHUMB", "B2APRST", "B2APOK",
    "B2APBAD", "B2HANDOFF",
    "BClk", "S0", "U0", "G1", "U1", "U2", "U3", "U4", "U5",
    "C0", "C1", "C2", "C3", "A0", "A1", "A2", "A3", "A4",
    "A5", "A6", "W0", "W1", "A7", "F1", "F2", "C4", "C5", "C6", "C7", "C8",
]
checkpoint_lines = {line.strip() for line in text.splitlines() if line.strip()}
present = [m for m in ordered if m in checkpoint_lines]
last = present[-1] if present else "none"
if "NuttShell (NSH)" in text or "nsh>" in text:
    verdict = "PASS_NSH"
elif "U1" in checkpoint_lines and "U2" not in checkpoint_lines:
    verdict = "STOP_BETWEEN_U1_U2"
elif "C8" in checkpoint_lines:
    verdict = "STOP_AFTER_C8_BEFORE_NSH"
elif present:
    verdict = f"STOP_AFTER_{last}"
else:
    verdict = "NO_CHECKPOINT"

lines = [
    f"serial_bytes={len(data)}",
    f"verdict={verdict}",
    f"last_checkpoint={last}",
    f"checkpoints={' '.join(present) if present else 'none'}",
    f"cold_path={'yes' if any(line.startswith('BClk ') for line in checkpoint_lines) else 'no'}",
    f"uart_init_returned={'yes' if 'U2' in checkpoint_lines else 'no'}",
    f"bl2_handoff={'yes' if 'B2HANDOFF' in checkpoint_lines else 'no'}",
    f"ap_timeout_cleanup={'yes' if 'F1' in checkpoint_lines and 'F2' in checkpoint_lines else 'no'}",
    f"nsh={'yes' if verdict == 'PASS_NSH' else 'no'}",
]
summary = "\n".join(lines) + "\n"
summary_path.write_text(summary)
print(summary, end="")
PY

cat "$SUMMARY_FILE"
echo "==> Logs: $RUN_DIR"

if ((capture_rc != 0)); then
  echo "ERROR: serial capture exited with $capture_rc" >&2
  exit "$capture_rc"
fi
if ((DO_FLASH && loader_rc != 0)); then
  exit "$loader_rc"
fi
if ((RTS_RESET && reset_rc != 0)); then
  exit "$reset_rc"
fi
if ((RTS_RESET)) && ! grep -qx 'cold_path=yes' "$SUMMARY_FILE"; then
  echo "ERROR: RTS toggled but no BClk cold-reset signature was captured" >&2
  exit 1
fi
if ((JLINK_RESET && jlink_rc != 0)); then
  exit "$jlink_rc"
fi
