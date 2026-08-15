#!/usr/bin/env bash
# Build CP app.bin and CPU1 AP app1.bin, then restore the CP build tree.

set -euo pipefail

usage()
{
    cat <<'EOF'
Usage: build_dual_image.sh

Select the CP/AP profiles and build behavior through the documented
CP_CONFIG_NAME, AP_CONFIG_NAME and BK7258_* environment variables.
EOF
}

if (($# != 0)); then
    if (($# == 1)) && [[ "$1" == --help || "$1" == -h ]]; then
        usage
        exit 0
    fi

    printf 'build_dual_image: unexpected argument:' >&2
    printf ' %q' "$@" >&2
    printf '\n' >&2
    usage >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BOARD_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CONTEST_DIR="$(cd "${BOARD_DIR}/../.." && pwd)"
WORKSPACE="$(cd "${CONTEST_DIR}/.." && pwd)"
TOPDIR="${WORKSPACE}/nuttx"
BUILD="${WORKSPACE}/build.sh"
PARTITION_GENERATOR="${SCRIPT_DIR}/gen_bk7258_partitions.py"
TRUST_CHAIN_TOOL="${SCRIPT_DIR}/bk7258_trust_chain.py"
# The payload-bearing container is an additive delivery artifact.  Keep the
# tool overrideable for CI/worktree validation, but never fall back to the
# metadata-only framework package when the signed profile requires a real
# container.
BKPACK_TOOL="${BK7258_BKPACK_TOOL:-${SCRIPT_DIR}/bk7258_bkpack.py}"
# A worktree validation can point this at a temporary config mirror whose
# custom board/chip paths resolve to that worktree.  Normal builds consume the
# configs owned by this generic BK7258 board tree; the former vendor mirror
# still points at the retired bk7258_t5ai layout and must not select builds.
CONFIG_ROOT="${BK7258_CONFIG_ROOT:-${BOARD_DIR}/configs}"
BK7258_PRODUCT="${BK7258_PRODUCT:-${BK7258_PRODUCT_ID:-}}"
AIDK_PRODUCT_ID=
AIDK_PROFILE_WORK_ROOT=
AIDK_PROFILE_ROOT=
cleanup_aidk_profile_root()
{
    local active_makedefs=

    if [[ -n "${AIDK_PROFILE_WORK_ROOT}" ]]; then
        if [[ -L "${TOPDIR}/Make.defs" ]]; then
            active_makedefs="$(readlink "${TOPDIR}/Make.defs")"
            if [[ "${active_makedefs}" == "${AIDK_PROFILE_WORK_ROOT}"/* ]]; then
                ln -sfn "${BOARD_DIR}/scripts/Make.defs" \
                    "${TOPDIR}/Make.defs"
            fi
        fi
        rm -rf "${AIDK_PROFILE_WORK_ROOT}"
    fi
}
trap cleanup_aidk_profile_root EXIT
if [[ -n "${BK7258_PRODUCT}" &&
      "${BK7258_PRODUCT}" != aidk_ai_toy &&
      "${BK7258_PRODUCT}" != aidk_ai_toy_bringup ]]; then
    printf 'build_dual_image: unsupported BK7258 product: %s\n' \
        "${BK7258_PRODUCT}" >&2
    exit 2
fi
if [[ "${BK7258_PRODUCT}" == aidk_ai_toy ||
      "${BK7258_PRODUCT}" == aidk_ai_toy_bringup ]]; then
    AIDK_PRODUCT_ID=aidk_ai_toy_bringup
    AIDK_PROFILE_WORK_ROOT="$(mktemp -d -t bk7258-aidk-profiles.XXXXXX)"
    AIDK_PROFILE_ROOT="${AIDK_PROFILE_WORK_ROOT}/configs"
    python3 "${SCRIPT_DIR}/materialize_aidk_profiles.py" \
        --seed-root "${BOARD_DIR}/configs" \
        --output "${AIDK_PROFILE_ROOT}" \
        --make-defs "${BOARD_DIR}/scripts/Make.defs"
    CONFIG_ROOT="${AIDK_PROFILE_ROOT}"
    CP_CONFIG_NAME=aidk_ai_toy_cp_mcuboot
    AP_CONFIG_NAME=aidk_ai_toy_ap_mcuboot
fi
if [[ ! -d "${CONFIG_ROOT}" ]]; then
    printf 'build_dual_image: missing BK7258 config root: %s\n' \
        "${CONFIG_ROOT}" >&2
    exit 2
fi
profile_value()
{
    local config="$1"
    local key="$2"
    local value

    value="$(sed -n "s/^${key}=//p" "${config}/profile.conf")"
    if [[ -z "${value}" || "${value}" == *$'\n'* ]]; then
        printf 'build_dual_image: profile %s must define %s exactly once\n' \
            "${config}" "${key}" >&2
        exit 2
    fi
    printf '%s\n' "${value}"
}

profile_value_optional()
{
    local config="$1"
    local key="$2"
    local fallback="$3"
    local value

    value="$(sed -n "s/^${key}=//p" "${config}/profile.conf")"
    if [[ "${value}" == *$'\n'* ]]; then
        printf 'build_dual_image: profile %s may define %s at most once\n' \
            "${config}" "${key}" >&2
        exit 2
    fi
    printf '%s\n' "${value:-${fallback}}"
}

validate_config_name()
{
    local role="$1"
    local name="$2"

    if ! [[ "${name}" =~ ^[a-z0-9][a-z0-9_]*$ ]]; then
        printf 'build_dual_image: invalid %s config name: %s\n' \
            "${role}" "${name}" >&2
        exit 2
    fi
}

validate_profile_enum()
{
    local field="$1"
    local value="$2"
    shift 2

    local allowed
    for allowed in "$@"; do
        if [[ "${value}" == "${allowed}" ]]; then
            return
        fi
    done
    printf 'build_dual_image: invalid profile %s=%s\n' \
        "${field}" "${value}" >&2
    exit 2
}

CP_CONFIG_NAME="${CP_CONFIG_NAME:-t5ai_core_cp_base}"
AP_CONFIG_NAME="${AP_CONFIG_NAME:-t5ai_core_ap_base}"
validate_config_name CP "${CP_CONFIG_NAME}"
validate_config_name AP "${AP_CONFIG_NAME}"
CP_CONFIG="${CONFIG_ROOT}/${CP_CONFIG_NAME}"
AP_CONFIG="${CONFIG_ROOT}/${AP_CONFIG_NAME}"

for config in "${CP_CONFIG}" "${AP_CONFIG}"; do
    if [[ ! -f "${config}/defconfig" || ! -f "${config}/profile.conf" ]]; then
        printf 'build_dual_image: incomplete BK7258 profile: %s\n' \
            "${config}" >&2
        exit 2
    fi
done

CP_PROFILE_SCHEMA="$(profile_value "${CP_CONFIG}" BK7258_PROFILE_SCHEMA)"
CP_PROFILE_BOARD="$(profile_value "${CP_CONFIG}" BK7258_PROFILE_BOARD)"
CP_PROFILE_ROLE="$(profile_value "${CP_CONFIG}" BK7258_PROFILE_ROLE)"
CP_PROFILE_BOOT="$(profile_value "${CP_CONFIG}" BK7258_PROFILE_BOOT)"
CP_PROFILE_CLASS="$(profile_value "${CP_CONFIG}" BK7258_PROFILE_CLASS)"
CP_PROFILE_COMPAT="$(profile_value "${CP_CONFIG}" BK7258_PROFILE_COMPAT)"
AP_PROFILE_SCHEMA="$(profile_value "${AP_CONFIG}" BK7258_PROFILE_SCHEMA)"
AP_PROFILE_BOARD="$(profile_value "${AP_CONFIG}" BK7258_PROFILE_BOARD)"
AP_PROFILE_ROLE="$(profile_value "${AP_CONFIG}" BK7258_PROFILE_ROLE)"
AP_PROFILE_BOOT="$(profile_value "${AP_CONFIG}" BK7258_PROFILE_BOOT)"
AP_PROFILE_CLASS="$(profile_value "${AP_CONFIG}" BK7258_PROFILE_CLASS)"
AP_PROFILE_COMPAT="$(profile_value "${AP_CONFIG}" BK7258_PROFILE_COMPAT)"
BK7258_SDK_BUNDLE_VERSION="${BK7258_SDK_BUNDLE_VERSION:-v3.1.1.9}"
CP_SDK_BUNDLE_VERSION="$(profile_value_optional "${CP_CONFIG}" \
    BK7258_PROFILE_SDK_BUNDLE "${BK7258_SDK_BUNDLE_VERSION}")"
AP_SDK_BUNDLE_VERSION="$(profile_value_optional "${AP_CONFIG}" \
    BK7258_PROFILE_SDK_BUNDLE "${BK7258_SDK_BUNDLE_VERSION}")"

if [[ "${CP_PROFILE_SCHEMA}" != 1 || "${AP_PROFILE_SCHEMA}" != 1 ]]; then
    printf '%s\n' 'build_dual_image: unsupported BK7258 profile schema' >&2
    exit 2
fi
validate_profile_enum board "${CP_PROFILE_BOARD}" aidk_ai_toy t5ai_core t5_board
validate_profile_enum board "${AP_PROFILE_BOARD}" aidk_ai_toy t5ai_core t5_board
validate_profile_enum role "${CP_PROFILE_ROLE}" cp
validate_profile_enum role "${AP_PROFILE_ROLE}" ap
validate_profile_enum boot "${CP_PROFILE_BOOT}" raw mcuboot
validate_profile_enum boot "${AP_PROFILE_BOOT}" raw mcuboot
validate_profile_enum class "${CP_PROFILE_CLASS}" runnable validation ci
validate_profile_enum class "${AP_PROFILE_CLASS}" runnable validation ci

if [[ "${CP_PROFILE_BOARD}" != "${AP_PROFILE_BOARD}" ]]; then
    printf 'build_dual_image: physical-board mismatch: CP=%s AP=%s\n' \
        "${CP_PROFILE_BOARD}" "${AP_PROFILE_BOARD}" >&2
    exit 2
fi
if [[ "${CP_PROFILE_BOOT}" != "${AP_PROFILE_BOOT}" ]]; then
    printf 'build_dual_image: boot-format mismatch: CP=%s AP=%s\n' \
        "${CP_PROFILE_BOOT}" "${AP_PROFILE_BOOT}" >&2
    exit 2
fi
if [[ "${CP_PROFILE_COMPAT}" != "${AP_PROFILE_COMPAT}" ]]; then
    printf 'build_dual_image: incompatible CP/AP profiles: CP=%s AP=%s\n' \
        "${CP_PROFILE_COMPAT}" "${AP_PROFILE_COMPAT}" >&2
    exit 2
fi
if [[ "${CP_PROFILE_BOARD}" == aidk_ai_toy &&
      ("${CP_PROFILE_BOOT}" != mcuboot || "${AP_PROFILE_BOOT}" != mcuboot) ]]; then
    printf '%s\n' 'build_dual_image: AIDK requires the shared MCUboot A/B boot contract' >&2
    exit 2
fi

for entry in "${CP_CONFIG}:${CP_PROFILE_BOARD}" \
             "${AP_CONFIG}:${AP_PROFILE_BOARD}"; do
    config="${entry%%:*}"
    board="${entry##*:}"
    case "${board}" in
        aidk_ai_toy)
            symbol=BK7258_BOARD_AIDK_AI_TOY
            ;;
        t5ai_core)
            # T5AI-Core is the Kconfig choice default.  NuttX
            # savedefconfig therefore removes its explicit selector; reject
            # only the non-default T5-Board selector here and keep the board
            # identity explicit in profile.conf.

            if grep -qx 'CONFIG_BK7258_BOARD_T5_BOARD=y' \
                "${config}/defconfig"; then
                printf 'build_dual_image: %s metadata disagrees with defconfig board\n' \
                    "${config}" >&2
                exit 2
            fi
            continue
            ;;
        t5_board)
            symbol=BK7258_BOARD_T5_BOARD
            ;;
    esac
    if ! grep -qx "CONFIG_${symbol}=y" "${config}/defconfig"; then
        printf 'build_dual_image: %s metadata disagrees with defconfig board\n' \
            "${config}" >&2
        exit 2
    fi
done

if grep -qx 'CONFIG_BK7258_AP_CORE=y' "${CP_CONFIG}/defconfig" ||
   ! grep -qx 'CONFIG_BK7258_AP_CORE=y' "${AP_CONFIG}/defconfig"; then
    printf '%s\n' 'build_dual_image: profile role disagrees with AP_CORE' >&2
    exit 2
fi

for entry in "${CP_CONFIG}:${CP_PROFILE_BOOT}" \
             "${AP_CONFIG}:${AP_PROFILE_BOOT}"; do
    config="${entry%%:*}"
    boot="${entry##*:}"
    if [[ "${boot}" == mcuboot ]]; then
        if ! grep -qx 'CONFIG_BK7258_MCUBOOT_IMAGE=y' "${config}/defconfig"; then
            printf 'build_dual_image: %s metadata requires MCUboot\n' \
                "${config}" >&2
            exit 2
        fi
    elif grep -qx 'CONFIG_BK7258_MCUBOOT_IMAGE=y' "${config}/defconfig"; then
        printf 'build_dual_image: %s raw metadata forbids MCUboot\n' \
            "${config}" >&2
        exit 2
    fi
done

BK7258_ALLOW_CI_PROFILE="${BK7258_ALLOW_CI_PROFILE:-NO}"
validate_profile_enum BK7258_ALLOW_CI_PROFILE \
    "${BK7258_ALLOW_CI_PROFILE}" NO YES
if [[ "${CP_PROFILE_CLASS}" == ci || "${AP_PROFILE_CLASS}" == ci ]]; then
    if [[ "${CP_PROFILE_CLASS}" != ci || "${AP_PROFILE_CLASS}" != ci ]]; then
        printf '%s\n' 'build_dual_image: CI profiles must be selected as a pair' >&2
        exit 2
    fi
    if [[ "${BK7258_ALLOW_CI_PROFILE}" != YES ]]; then
        printf '%s\n' \
            'build_dual_image: CI-only profiles require BK7258_ALLOW_CI_PROFILE=YES' \
            >&2
        exit 2
    fi
fi

BK7258_PROFILE_CHECK_ONLY="${BK7258_PROFILE_CHECK_ONLY:-NO}"
validate_profile_enum BK7258_PROFILE_CHECK_ONLY \
    "${BK7258_PROFILE_CHECK_ONLY}" NO YES

# BL1, BL2 and CP must agree on physical debug/console ownership.  Derive the
# boot-stage constants from Kconfig symbols, never from profile filenames.
config_enabled()
{
    local config="$1"
    local symbol="$2"
    grep -qx "CONFIG_${symbol}=y" "${config}/defconfig"
}

config_disabled()
{
    local config="$1"
    local symbol="$2"
    grep -qx "# CONFIG_${symbol} is not set" "${config}/defconfig"
}

config_value()
{
    local config="$1"
    local symbol="$2"
    local fallback="$3"
    local value

    value="$(sed -n "s/^CONFIG_${symbol}=//p" "${config}/defconfig" | tail -n 1)"
    printf '%s\n' "${value:-${fallback}}"
}

validate_symmetric_feature()
{
    local symbol="$1"
    local label="$2"

    if config_enabled "${CP_CONFIG}" "${symbol}" ||
       config_enabled "${AP_CONFIG}" "${symbol}"; then
        if ! config_enabled "${CP_CONFIG}" "${symbol}" ||
           ! config_enabled "${AP_CONFIG}" "${symbol}"; then
            printf 'build_dual_image: %s must be selected as a CP/AP pair\n' \
                "${label}" >&2
            exit 2
        fi
    fi
}

BL1_SWD_ENABLE=0
BL1_SWD_PIN_GROUP=1
BL1_SWD_TARGET=0
BL1_SWD_BOOT_HOLD=0
if config_enabled "${CP_CONFIG}" BK7258_SWD_DEBUG; then
    BL1_SWD_ENABLE=1
    if config_enabled "${CP_CONFIG}" BK7258_SWD_PINS_P20_P21; then
        BL1_SWD_PIN_GROUP=0
    fi
    if config_enabled "${CP_CONFIG}" BK7258_SWD_TARGET_AP0; then
        BL1_SWD_TARGET=1
    elif config_enabled "${CP_CONFIG}" BK7258_SWD_TARGET_AP1; then
        BL1_SWD_TARGET=2
    elif ! config_disabled "${CP_CONFIG}" BK7258_SWD_BOOT_HOLD; then
        BL1_SWD_BOOT_HOLD=1
    fi
fi

if config_enabled "${CP_CONFIG}" BK7258_CONSOLE_UART0; then
    BL1_CONSOLE_UART=0
    BL1_CONSOLE_BAUD="$(config_value "${CP_CONFIG}" BK7258_UART0_BAUD 115200)"
    BL1_CONSOLE_DATA_BITS="$(config_value "${CP_CONFIG}" BK7258_UART0_DATA_BITS 8)"
    BL1_CONSOLE_PARITY="$(config_value "${CP_CONFIG}" BK7258_UART0_PARITY 0)"
    BL1_CONSOLE_STOP_BITS="$(config_value "${CP_CONFIG}" BK7258_UART0_STOP_BITS 1)"
elif config_enabled "${CP_CONFIG}" BK7258_CONSOLE_UART2; then
    BL1_CONSOLE_UART=2
    BL1_CONSOLE_BAUD="$(config_value "${CP_CONFIG}" BK7258_UART2_BAUD 115200)"
    BL1_CONSOLE_DATA_BITS="$(config_value "${CP_CONFIG}" BK7258_UART2_DATA_BITS 8)"
    BL1_CONSOLE_PARITY="$(config_value "${CP_CONFIG}" BK7258_UART2_PARITY 0)"
    BL1_CONSOLE_STOP_BITS="$(config_value "${CP_CONFIG}" BK7258_UART2_STOP_BITS 1)"
elif config_enabled "${CP_CONFIG}" BK7258_CONSOLE_UART1; then
    BL1_CONSOLE_UART=1
    BL1_CONSOLE_BAUD="$(config_value "${CP_CONFIG}" BK7258_UART1_BAUD 460800)"
    BL1_CONSOLE_DATA_BITS="$(config_value "${CP_CONFIG}" BK7258_UART1_DATA_BITS 8)"
    BL1_CONSOLE_PARITY="$(config_value "${CP_CONFIG}" BK7258_UART1_PARITY 0)"
    BL1_CONSOLE_STOP_BITS="$(config_value "${CP_CONFIG}" BK7258_UART1_STOP_BITS 1)"
elif config_enabled "${CP_CONFIG}" BK7258_CONSOLE_RTT ||
     config_enabled "${CP_CONFIG}" BK7258_CONSOLE_NONE ||
     [[ "${BL1_SWD_ENABLE}" == 1 ]]; then
    BL1_CONSOLE_UART=3
    BL1_CONSOLE_BAUD=115200
    BL1_CONSOLE_DATA_BITS=8
    BL1_CONSOLE_PARITY=0
    BL1_CONSOLE_STOP_BITS=1
else
    BL1_CONSOLE_UART=1
    BL1_CONSOLE_BAUD="$(config_value "${CP_CONFIG}" BK7258_UART1_BAUD 460800)"
    BL1_CONSOLE_DATA_BITS="$(config_value "${CP_CONFIG}" BK7258_UART1_DATA_BITS 8)"
    BL1_CONSOLE_PARITY="$(config_value "${CP_CONFIG}" BK7258_UART1_PARITY 0)"
    BL1_CONSOLE_STOP_BITS="$(config_value "${CP_CONFIG}" BK7258_UART1_STOP_BITS 1)"
fi

BL1_UART2_PIN_GROUP=0
if config_enabled "${CP_CONFIG}" BK7258_UART2_PINS_P40_P41; then
    BL1_UART2_PIN_GROUP=1
fi

if ! [[ "${BL1_CONSOLE_BAUD}" =~ ^[0-9]+$ ]] ||
   (( BL1_CONSOLE_BAUD < 400 || BL1_CONSOLE_BAUD > 5200000 )); then
    printf 'build_dual_image: invalid boot console baud: %s\n' \
        "${BL1_CONSOLE_BAUD}" >&2
    exit 2
fi

if [[ "${BL1_SWD_ENABLE}:${BL1_SWD_PIN_GROUP}" == "1:1" ]] &&
   { [[ "${BL1_CONSOLE_UART}" == 1 ]] ||
     config_enabled "${CP_CONFIG}" BK7258_UART1; }; then
    printf '%s\n' 'build_dual_image: P0/P1 cannot own both SWD and UART1' >&2
    exit 2
fi
if [[ "${BL1_SWD_ENABLE}:${BL1_SWD_PIN_GROUP}" == "1:0" ]] &&
   config_enabled "${AP_CONFIG}" BK7258_LCD; then
    printf '%s\n' 'build_dual_image: P20/P21 SWD conflicts with AP LCD' >&2
    exit 2
fi
if { [[ "${BL1_CONSOLE_UART}" == 2 ]] ||
     config_enabled "${CP_CONFIG}" BK7258_UART2; } &&
   [[ "${BL1_UART2_PIN_GROUP}" == 0 ]] &&
   config_enabled "${AP_CONFIG}" BK7258_T5_BOARD_CAMERA; then
    printf '%s\n' 'build_dual_image: UART2 P30/P31 conflicts with AP camera' >&2
    exit 2
fi
if { [[ "${BL1_CONSOLE_UART}" == 2 ]] ||
     config_enabled "${CP_CONFIG}" BK7258_UART2; } &&
   [[ "${BL1_UART2_PIN_GROUP}" == 1 ]] &&
   config_enabled "${AP_CONFIG}" BK7258_LCD; then
    printf '%s\n' 'build_dual_image: UART2 P40/P41 conflicts with AP LCD' >&2
    exit 2
fi
if config_enabled "${CP_CONFIG}" BK7258_UART0_FLOW_CONTROL &&
   { config_enabled "${AP_CONFIG}" BK7258_GT1151 ||
     config_enabled "${AP_CONFIG}" BK7258_USBHOST; }; then
    printf '%s\n' 'build_dual_image: UART0 CTS/RTS P12/P13 conflict with AP board devices' >&2
    exit 2
fi

if config_enabled "${CP_CONFIG}" BK7258_PM_STANDBY_ONESHOT_VERIFY &&
   config_enabled "${CP_CONFIG}" BK7258_CONSOLE_RTT; then
    printf '%s\n' \
        'build_dual_image: one-shot standby validation requires a non-RTT CP console' >&2
    exit 2
fi

# On T5-Board the fitted CS8302M amplifier owns P28.  Every current SDK I2S
# pin group also routes I2S1_MCLK through P28, so a runnable image cannot
# publish both owners.  Keep the raw CI drivercheck pair exempt because it
# exists only to compile both independent lower halves and is never flashed.

if [[ "${AP_PROFILE_BOARD}" == t5_board &&
      "${AP_PROFILE_CLASS}" != ci ]] &&
   config_enabled "${AP_CONFIG}" BK7258_AUD &&
   config_enabled "${AP_CONFIG}" BK7258_I2S; then
    printf '%s\n' \
        'build_dual_image: T5-Board P28 cannot own both speaker PA and I2S1_MCLK' >&2
    exit 2
fi

# The CP keeps the board-verified v3.1.1.9 bundle.  AP profiles may name a
# role-specific variant, but the fixed one-line and fixed-four-line SDK data
# helpers must never be paired with the opposite NuttX capability.

if [[ "${CP_SDK_BUNDLE_VERSION}" != "v3.1.1.9" ]]; then
    printf 'build_dual_image: CP profile requires SDK v3.1.1.9, got %s\n' \
        "${CP_SDK_BUNDLE_VERSION}" >&2
    exit 2
fi

# T5-Board V1.0.2 has no verified card-detect edge.  Make the fixed-media
# upper-half policy explicit in the board profile rather than silently
# inheriting NuttX's default card-detect setting.

if config_enabled "${AP_CONFIG}" BK7258_T5_BOARD_TF_SLOT &&
   ! config_disabled "${AP_CONFIG}" MMCSD_HAVE_CARDDETECT; then
    printf '%s\n' \
        'build_dual_image: T5-Board TF fixed-media mode requires MMCSD_HAVE_CARDDETECT=n' >&2
    exit 2
fi

# T5-Board V1.0.2 routes TF D2/D3 to P10/P11.  S1-1/S1-2 can connect those
# pins to the CH342F UART0 download channel, and CP UART0 would also fight the
# AP SDIO mux internally.  Four-bit profiles therefore carry an explicit
# physical-route assertion and cannot be paired with an active CP UART0.

if config_enabled "${AP_CONFIG}" BK7258_SDIO_4BIT; then
    if ! config_enabled "${AP_CONFIG}" \
        BK7258_T5_BOARD_SDIO_D2_D3_ISOLATED; then
        printf '%s\n' \
            'build_dual_image: T5-Board 4-bit TF requires the S1-1/S1-2 isolation assertion' >&2
        exit 2
    fi

    if [[ "${BL1_CONSOLE_UART}" == 0 ]] ||
       config_enabled "${CP_CONFIG}" BK7258_UART0; then
        printf '%s\n' \
            'build_dual_image: T5-Board 4-bit TF P10/P11 conflicts with CP UART0' >&2
        exit 2
    fi

    if [[ "${AP_SDK_BUNDLE_VERSION}" != "v3.1.1.9-sdio4" ]]; then
        printf '%s\n' \
            'build_dual_image: four-bit TF requires AP SDK bundle v3.1.1.9-sdio4' >&2
        exit 2
    fi
elif [[ "${AP_SDK_BUNDLE_VERSION}" != "v3.1.1.9" ]]; then
    printf '%s\n' \
        'build_dual_image: one-bit/default AP profiles require SDK bundle v3.1.1.9' >&2
    exit 2
fi

validate_symmetric_feature BK7258_RPTUN RPTUN
validate_symmetric_feature BK7258_AP_SUPERVISOR 'AP supervisor'
validate_symmetric_feature BK7258_PM_COORDINATED_STANDBY \
    'coordinated PM standby'
validate_symmetric_feature BK7258_BT_IPC 'Bluetooth IPC'
validate_symmetric_feature BK7258_WIFI_VNET 'Wi-Fi VNET'

# Temperature is a CP-owned service with an optional AP client.  A client
# without its server can only time out, but an idle server without a client is
# valid and lets the generic CP application serve multiple AP profiles.

if config_enabled "${AP_CONFIG}" BK7258_TEMPERATURE &&
   ! config_enabled "${CP_CONFIG}" BK7258_TEMPERATURE; then
    printf '%s\n' \
        'build_dual_image: AP on-die temperature client requires the CP server' >&2
    exit 2
fi

# The AP SARADC lower half is an IPC client even though it exposes the
# standard local /dev/adcN ABI.  A client without the CP mailbox server can
# only block until the immutable SDK operation timeout.  An idle CP server is
# valid, so this remains a one-way dependency like the temperature service.

if config_enabled "${AP_CONFIG}" BK7258_SARADC &&
   ! config_enabled "${CP_CONFIG}" BK7258_SARADC_SERVER; then
    printf '%s\n' \
        'build_dual_image: AP SARADC client requires the CP mailbox server' >&2
    exit 2
fi

# T5-Board P12 is one physical endpoint with GPIO, UART0_RTS, USB0_DP,
# TOUCH0 and ADC14 alternatives.  Enforce that ownership for any runnable
# ADC14 image, not only for the interactive key validator.  UART0 without
# flow control remains valid because it uses only P10/P11.

if [[ "${AP_PROFILE_BOARD}" == t5_board ]] &&
   config_enabled "${AP_CONFIG}" BK7258_SARADC &&
   [[ "$(config_value "${AP_CONFIG}" BK7258_SARADC_CHAN 0)" == 14 ]]; then
    for conflict in BK7258_UART0_FLOW_CONTROL BK7258_GPIO_LOWERHALF \
                    BK7258_GPIO_FOUNDATION_TEST BK7258_GPIO_IRQ_TEST; do
        if config_enabled "${CP_CONFIG}" "${conflict}"; then
            printf 'build_dual_image: T5-Board P12 ADC14 conflicts with CP %s\n' \
                "${conflict}" >&2
            exit 2
        fi
    done

    if config_enabled "${AP_CONFIG}" BK7258_USBHOST; then
        printf '%s\n' \
            'build_dual_image: T5-Board P12 cannot own both ADC14 and USB0_DP' >&2
        exit 2
    fi

    if config_enabled "${CP_CONFIG}" BK7258_TOUCH &&
       [[ "$(config_value "${CP_CONFIG}" BK7258_TOUCH_CHANNEL 3)" == 0 ]]; then
        printf '%s\n' \
            'build_dual_image: T5-Board P12 cannot own both ADC14 and TOUCH0' >&2
        exit 2
    fi
fi

SARADC_KEY_COMPAT=t5_board_saradc_key_validation_mcuboot_v1
if config_enabled "${AP_CONFIG}" BK7258_T5_BOARD_SARADC_KEY_VALIDATION ||
   [[ "${AP_PROFILE_COMPAT}" == "${SARADC_KEY_COMPAT}" ||
      "${CP_PROFILE_COMPAT}" == "${SARADC_KEY_COMPAT}" ]]; then
    if [[ "${AP_PROFILE_CLASS}" != validation ||
          "${CP_PROFILE_CLASS}" != validation ||
          "${AP_PROFILE_BOOT}" != mcuboot ||
          "${CP_PROFILE_BOOT}" != mcuboot ||
          "${AP_PROFILE_COMPAT}" != "${SARADC_KEY_COMPAT}" ||
          "${CP_PROFILE_COMPAT}" != "${SARADC_KEY_COMPAT}" ]] ||
       ! config_enabled "${AP_CONFIG}" \
           BK7258_T5_BOARD_SARADC_KEY_VALIDATION ||
       ! config_enabled "${AP_CONFIG}" BK7258_SARADC ||
       ! config_enabled "${CP_CONFIG}" BK7258_SARADC_SERVER ||
       [[ "$(config_value "${AP_CONFIG}" BK7258_SARADC_CHAN 0)" != 14 ]] ||
       [[ "$(config_value "${AP_CONFIG}" BK7258_SARADC_BUS 0)" != 0 ]]; then
        printf '%s\n' \
            'build_dual_image: T5-Board ADC-key validation requires its dedicated ADC14 /dev/adc0 CP/AP pair' >&2
        exit 2
    fi
fi

if config_enabled "${AP_CONFIG}" BK7258_PM_COORDINATED_STANDBY; then
    AP_PM_NDOMAINS="$(config_value "${AP_CONFIG}" PM_NDOMAINS 1)"
    AP_SMP_NCPUS="$(config_value "${AP_CONFIG}" SMP_NCPUS 1)"
    if ! [[ "${AP_PM_NDOMAINS}" =~ ^[0-9]+$ &&
            "${AP_SMP_NCPUS}" =~ ^[0-9]+$ ]] ||
       (( AP_PM_NDOMAINS < AP_SMP_NCPUS + 1 )); then
        printf '%s\n' \
            'build_dual_image: coordinated PM requires AP PM_NDOMAINS >= SMP_NCPUS + 1' \
            >&2
        exit 2
    fi
fi

# The retained Audio DAC profile is the bounded hardware-EQ register-lifecycle
# validator.  Its stable profile names catch an accidental downgrade of both
# metadata tokens, while the v2 token protects renamed copies.  Do not trigger
# this board/profile contract from the chip-generic AUD_DAC_EQ feature.

BK7258_AUDIO_DAC_VALIDATION_COMPAT=t5_board_audio_dac_validation_mcuboot_v2
BK7258_AUDIO_DAC_VALIDATION_CP=t5_board_cp_audio_dac_validation_mcuboot
BK7258_AUDIO_DAC_VALIDATION_AP=t5_board_ap_audio_dac_validation_mcuboot
if [[ "${CP_CONFIG_NAME}" == "${BK7258_AUDIO_DAC_VALIDATION_CP}" ||
      "${AP_CONFIG_NAME}" == "${BK7258_AUDIO_DAC_VALIDATION_AP}" ||
      "${CP_PROFILE_COMPAT}" == "${BK7258_AUDIO_DAC_VALIDATION_COMPAT}" ||
      "${AP_PROFILE_COMPAT}" == "${BK7258_AUDIO_DAC_VALIDATION_COMPAT}" ]]; then
    if [[ "${CP_PROFILE_COMPAT}" != "${BK7258_AUDIO_DAC_VALIDATION_COMPAT}" ||
          "${AP_PROFILE_COMPAT}" != "${BK7258_AUDIO_DAC_VALIDATION_COMPAT}" ||
          "${CP_PROFILE_BOARD}" != t5_board ||
          "${AP_PROFILE_BOARD}" != t5_board ||
          "${CP_PROFILE_BOOT}" != mcuboot ||
          "${AP_PROFILE_BOOT}" != mcuboot ||
          "${CP_PROFILE_CLASS}" != validation ||
          "${AP_PROFILE_CLASS}" != validation ]] ||
       ! config_enabled "${AP_CONFIG}" BK7258_AUD ||
       ! config_enabled "${AP_CONFIG}" BK7258_AUD_DAC_EQ ||
       ! config_enabled "${AP_CONFIG}" BK7258_AUD_LIFECYCLE_VALIDATION ||
       ! config_enabled "${CP_CONFIG}" BK7258_PM_STANDBY_ONESHOT_VERIFY; then
        printf '%s\n' \
            'build_dual_image: Audio DAC EQ validation requires the complete v2 MCUboot validation pair' >&2
        exit 2
    fi
fi

# This pair is the bounded standard-V4L2 JPEG M2M validator.  Stable profile
# names catch a compat-token downgrade, while the v1 token protects renamed
# copies.  Keep this board/profile contract independent of the chip-generic
# JPEG decoder and M2M feature symbols so other boards can reuse the driver.

BK7258_JPEG_M2M_VALIDATION_COMPAT=t5_board_jpeg_m2m_validation_mcuboot_v1
BK7258_JPEG_M2M_VALIDATION_CP=t5_board_cp_jpeg_m2m_validation_mcuboot
BK7258_JPEG_M2M_VALIDATION_AP=t5_board_ap_jpeg_m2m_validation_mcuboot
if [[ "${CP_CONFIG_NAME}" == "${BK7258_JPEG_M2M_VALIDATION_CP}" ||
      "${AP_CONFIG_NAME}" == "${BK7258_JPEG_M2M_VALIDATION_AP}" ||
      "${CP_PROFILE_COMPAT}" == "${BK7258_JPEG_M2M_VALIDATION_COMPAT}" ||
      "${AP_PROFILE_COMPAT}" == "${BK7258_JPEG_M2M_VALIDATION_COMPAT}" ]]; then
    if [[ "${CP_PROFILE_COMPAT}" != "${BK7258_JPEG_M2M_VALIDATION_COMPAT}" ||
          "${AP_PROFILE_COMPAT}" != "${BK7258_JPEG_M2M_VALIDATION_COMPAT}" ||
          "${CP_PROFILE_BOARD}" != t5_board ||
          "${AP_PROFILE_BOARD}" != t5_board ||
          "${CP_PROFILE_BOOT}" != mcuboot ||
          "${AP_PROFILE_BOOT}" != mcuboot ||
          "${CP_PROFILE_CLASS}" != validation ||
          "${AP_PROFILE_CLASS}" != validation ]] ||
       ! config_enabled "${CP_CONFIG}" BK7258_AP_AUTOSTART ||
       ! config_enabled "${CP_CONFIG}" BK7258_AP_CONTROL ||
       ! config_enabled "${CP_CONFIG}" BK7258_CONSOLE_UART0 ||
       ! config_enabled "${CP_CONFIG}" BK7258_RPTUN ||
       ! config_enabled "${CP_CONFIG}" BK7258_SDK_IPC_RUNTIME ||
       ! config_enabled "${CP_CONFIG}" BK7258_SWD_DEBUG ||
       config_enabled "${CP_CONFIG}" BK7258_SWD_PINS_P20_P21 ||
       config_enabled "${CP_CONFIG}" BK7258_SWD_TARGET_AP0 ||
       config_enabled "${CP_CONFIG}" BK7258_SWD_TARGET_AP1 ||
       config_disabled "${CP_CONFIG}" BK7258_SWD_BOOT_HOLD ||
       ! config_enabled "${AP_CONFIG}" BK7258_JPEG_DECODER ||
       ! config_enabled "${AP_CONFIG}" BK7258_JPEG_M2M ||
       ! config_enabled "${AP_CONFIG}" BK7258_JPEG_M2M_VALIDATION ||
       ! config_enabled "${AP_CONFIG}" BK7258_RPTUN ||
       [[ "$(config_value "${AP_CONFIG}" BK7258_JPEG_M2M_DEVPATH \
             '"/dev/video1"')" != '"/dev/video1"' ]] ||
       ! grep -qx 'CONFIG_BK7258_JPEG_M2M_DEFAULT_WIDTH=32' \
           "${AP_CONFIG}/defconfig" ||
       ! grep -qx 'CONFIG_BK7258_JPEG_M2M_DEFAULT_HEIGHT=16' \
           "${AP_CONFIG}/defconfig" ||
       ! grep -qx 'CONFIG_BK7258_JPEG_M2M_MAX_WIDTH=32' \
           "${AP_CONFIG}/defconfig" ||
       ! grep -qx 'CONFIG_BK7258_JPEG_M2M_MAX_HEIGHT=16' \
           "${AP_CONFIG}/defconfig" ||
       ! grep -qx 'CONFIG_BK7258_JPEG_M2M_MAX_INPUT_SIZE=4096' \
           "${AP_CONFIG}/defconfig"; then
        printf '%s\n' \
            'build_dual_image: JPEG M2M validation requires the complete v1 MCUboot validation pair' >&2
        exit 2
    fi

    for symbol in BK7258_FLASH_LITTLEFS BK7258_FLASH_MTD \
                  BK7258_GPIO_LOWERHALF BK7258_SARADC_SERVER \
                  BK7258_TEMPERATURE BK7258_WIFI_VNET BK7258_BT_IPC \
                  BK7258_RPMSGFS BK7258_PM_COORDINATED_STANDBY; do
        if config_enabled "${CP_CONFIG}" "${symbol}"; then
            printf 'build_dual_image: JPEG M2M validation CP must not enable %s\n' \
                "${symbol}" >&2
            exit 2
        fi
    done

    for symbol in BK7258_DVP BK7258_T5_BOARD_CAMERA \
                  BK7258_T5_BOARD_CAMERA_RAW_YUV \
                  BK7258_T5_BOARD_CAMERA_H264 \
                  BK7258_T5_BOARD_CAMERA_VALIDATION \
                  BK7258_T5_BOARD_CAMERA_H264_VALIDATION BK7258_LCD \
                  BK7258_DMA2D BK7258_JPEG_ENCODER BK7258_YUV_H264 \
                  BK7258_SCALE_ROTATE BK7258_PSRAM \
                  BK7258_PM_COORDINATED_STANDBY; do
        if config_enabled "${AP_CONFIG}" "${symbol}"; then
            printf 'build_dual_image: JPEG M2M validation AP must not enable %s\n' \
                "${symbol}" >&2
            exit 2
        fi
    done
fi

if [[ "${BK7258_PROFILE_CHECK_ONLY}" == YES ]]; then
    printf 'build_dual_image: profile PASS board=%s boot=%s compat=%s cp=%s ap=%s cp_sdk=%s ap_sdk=%s\n' \
        "${CP_PROFILE_BOARD}" "${CP_PROFILE_BOOT}" \
        "${CP_PROFILE_COMPAT}" "${CP_CONFIG_NAME}" "${AP_CONFIG_NAME}" \
        "${CP_SDK_BUNDLE_VERSION}" "${AP_SDK_BUNDLE_VERSION}"
    exit 0
fi

# The openvela build wrapper configures the shared nuttx/ and apps/ trees in
# place.  Two dual-image builds in the same workspace can otherwise replace
# Make.defs, generated Kconfig files and bk7258-dual artifacts underneath one
# another.  Serialize physical builds across repository worktrees/sessions;
# metadata-only profile checks above remain lock-free.

if ! command -v flock >/dev/null 2>&1; then
    printf '%s\n' 'build_dual_image: flock is required for a shared build tree' \
        >&2
    exit 2
fi

BK7258_BUILD_LOCK_TIMEOUT_SECONDS="${BK7258_BUILD_LOCK_TIMEOUT_SECONDS:-600}"
if ! [[ "${BK7258_BUILD_LOCK_TIMEOUT_SECONDS}" =~ ^[1-9][0-9]*$ ]]; then
    printf 'build_dual_image: invalid build-lock timeout: %s\n' \
        "${BK7258_BUILD_LOCK_TIMEOUT_SECONDS}" >&2
    exit 2
fi

BK7258_BUILD_LOCK="/tmp/openvela-bk7258-build-${UID}.lock"
exec {BK7258_BUILD_LOCK_FD}>"${BK7258_BUILD_LOCK}"
if ! flock -n "${BK7258_BUILD_LOCK_FD}"; then
    printf 'build_dual_image: waiting for shared NuttX build tree lock: %s\n' \
        "${BK7258_BUILD_LOCK}"
    if ! flock -w "${BK7258_BUILD_LOCK_TIMEOUT_SECONDS}" \
        "${BK7258_BUILD_LOCK_FD}"; then
        printf 'build_dual_image: timed out waiting for build lock after %ss\n' \
            "${BK7258_BUILD_LOCK_TIMEOUT_SECONDS}" >&2
        exit 2
    fi
fi

# MCUboot is an explicit, signed build profile.  Do not silently turn a
# metadata-declared MCUboot pair into the unsigned raw-image pipeline:
# the signing key and BL1 authorization key must be supplied from outside the
# repository (normally from /tmp or a developer-owned secure key store).
MCUBOOT_PROFILE=false
BL1_USE_BL2=0
TRUST_CHAIN_CONTRACT=
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
if [[ "${CP_PROFILE_BOOT}" == mcuboot ]]; then
    MCUBOOT_PROFILE=true
    BL1_USE_BL2=1
    TRUST_CHAIN_CONTRACT=bk7258-trust-chain.json
    MCUBOOT_BL2_FLASH_SEGMENT="bl2_crc.bin@0x51d000,bl2_secondary_crc.bin@0x53f000"
    if [[ -z "${MCUBOOT_SIGNING_KEY}" || ! -f "${MCUBOOT_SIGNING_KEY}" ]]; then
        printf '%s\n' \
            'build_dual_image: an MCUboot profile requires an external MCUBOOT_SIGNING_KEY' \
            >&2
        exit 2
    fi
    if [[ -z "${MCUBOOT_VERSION}" ]]; then
        printf '%s\n' \
            'build_dual_image: an MCUboot profile requires MCUBOOT_VERSION' \
            >&2
        exit 2
    fi
    if [[ -z "${BL1_MANIFEST_KEY}" || ! -f "${BL1_MANIFEST_KEY}" ]]; then
        printf '%s\n' \
            'build_dual_image: an MCUboot profile requires an external BL1_MANIFEST_KEY' \
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

JOBS="${JOBS:-8}"
OUTPUT="${TOPDIR}/bk7258-dual"
SDK_BUNDLE_BASE="${BOARD_DIR}/bk_idk/armino_as_lib"
CP_SDK_BUNDLE_ROOT="${SDK_BUNDLE_BASE}/versions/${CP_SDK_BUNDLE_VERSION}"
AP_SDK_BUNDLE_ROOT="${SDK_BUNDLE_BASE}/versions/${AP_SDK_BUNDLE_VERSION}"
CP_SDK_MANIFEST_DIR="${SCRIPT_DIR}/sdk-manifests/${CP_SDK_BUNDLE_VERSION}"
AP_SDK_MANIFEST_DIR="${SCRIPT_DIR}/sdk-manifests/${AP_SDK_BUNDLE_VERSION}"
export BK7258_CP_SDK_BUNDLE_VERSION="${CP_SDK_BUNDLE_VERSION}"
export BK7258_AP_SDK_BUNDLE_VERSION="${AP_SDK_BUNDLE_VERSION}"
TMPDIR="$(mktemp -d)"

cleanup()
{
    rm -rf "${TMPDIR}"
    cleanup_aidk_profile_root
}
trap cleanup EXIT

"${SCRIPT_DIR}/setup_bk7258_sdk.sh" --check \
    --version "${CP_SDK_BUNDLE_VERSION}" --role cp
"${SCRIPT_DIR}/setup_bk7258_sdk.sh" --check \
    --version "${AP_SDK_BUNDLE_VERSION}" --role ap

CP_SDK_ROLE_DIR="$(readlink -f "${CP_SDK_BUNDLE_ROOT}/cp")"
AP_SDK_ROLE_DIR="$(readlink -f "${AP_SDK_BUNDLE_ROOT}/ap")"
CP_SDK_MANIFEST="${CP_SDK_MANIFEST_DIR}/cp.sha256"
AP_SDK_MANIFEST="${AP_SDK_MANIFEST_DIR}/ap.sha256"
CP_SDK_PROVENANCE="${CP_SDK_MANIFEST_DIR}/cp.provenance"
AP_SDK_PROVENANCE="${AP_SDK_MANIFEST_DIR}/ap.provenance"
CP_SDK_MANIFEST_SHA256="$(sha256sum "${CP_SDK_MANIFEST}" | awk '{print $1}')"
AP_SDK_MANIFEST_SHA256="$(sha256sum "${AP_SDK_MANIFEST}" | awk '{print $1}')"
CP_SDK_PROVENANCE_SHA256="$(sha256sum "${CP_SDK_PROVENANCE}" | awk '{print $1}')"
AP_SDK_PROVENANCE_SHA256="$(sha256sum "${AP_SDK_PROVENANCE}" | awk '{print $1}')"

build_config()
{
    local config="$1"
    local cleanup_ap_bundle="v3.1.1.9"
    local cleanup_cp_bundle="v3.1.1.9"
    local configured_distclean=false

    # A removed profile can leave NuttX's Make.defs symlink pointing through
    # the now-absent configs/<old-name> directory.  configure.sh -e sees the
    # retained .config and tries to run distclean through that broken link,
    # so it cannot recover by itself.  This wrapper always replaces the
    # active NuttX configuration and therefore may discard only these stale
    # configuration markers before performing the normal clean build.

    if [[ -f "${TOPDIR}/.config" && ! -e "${TOPDIR}/Make.defs" ]]; then
        printf '%s\n' \
            'build_dual_image: removing stale NuttX config with broken Make.defs'
        rm -f "${TOPDIR}/.config" "${TOPDIR}/defconfig" \
            "${TOPDIR}/Make.defs"
        "${BUILD}" "${config}" distclean
        configured_distclean=true
    fi

    # Clean the active configuration with the bundle that matches that
    # configuration.  Passing the next profile's bundle through build.sh's
    # configure-then-distclean path can make an interrupted AP four-bit build
    # impossible to replace with a one-bit profile (or vice versa), because
    # the selector rejects the old .config before configure.sh can clean it.

    if [[ "${configured_distclean}" != true && -f "${TOPDIR}/.config" ]]; then
        if grep -qx 'CONFIG_BK7258_AP_CORE=y' "${TOPDIR}/.config" &&
           grep -qx 'CONFIG_BK7258_SDIO_4BIT=y' "${TOPDIR}/.config"; then
            cleanup_ap_bundle="v3.1.1.9-sdio4"
        fi
        BK7258_CP_SDK_BUNDLE_VERSION="${cleanup_cp_bundle}" \
        BK7258_AP_SDK_BUNDLE_VERSION="${cleanup_ap_bundle}" \
            make -C "${TOPDIR}" distclean
    fi
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

printf 'build_dual_image: SDK bundles: CP=%s AP=%s\n' \
    "${CP_SDK_BUNDLE_VERSION}" "${AP_SDK_BUNDLE_VERSION}"
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
    "BL1_USE_BL2=${BL1_USE_BL2}"
    "BL1_SWD_ENABLE=${BL1_SWD_ENABLE}"
    "BL1_SWD_PIN_GROUP=${BL1_SWD_PIN_GROUP}"
    "BL1_SWD_TARGET=${BL1_SWD_TARGET}"
    "BL1_CONSOLE_UART=${BL1_CONSOLE_UART}"
    "BL1_CONSOLE_BAUD=${BL1_CONSOLE_BAUD}"
    "BL1_CONSOLE_DATA_BITS=${BL1_CONSOLE_DATA_BITS}"
    "BL1_CONSOLE_PARITY=${BL1_CONSOLE_PARITY}"
    "BL1_CONSOLE_STOP_BITS=${BL1_CONSOLE_STOP_BITS}"
    "BL1_UART2_PIN_GROUP=${BL1_UART2_PIN_GROUP}"
)
if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    BL1_BOOT_ARGS+=("BL1_SWD_BOOT_HOLD=0")
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
        "BL2_SWD_ENABLE=${BL1_SWD_ENABLE}" \
        "BL2_SWD_PIN_GROUP=${BL1_SWD_PIN_GROUP}" \
        "BL2_SWD_TARGET=${BL1_SWD_TARGET}" \
        "BL2_SWD_BOOT_HOLD=${BL1_SWD_BOOT_HOLD}" \
        "BL2_CONSOLE_UART=${BL1_CONSOLE_UART}" \
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
else
    BL1_BOOT_ARGS+=("BL1_SWD_BOOT_HOLD=${BL1_SWD_BOOT_HOLD}")
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
    python3 "${TRUST_CHAIN_TOOL}" emit \
        --bl1-manifest-key "${BL1_MANIFEST_KEY}" \
        --mcuboot-signing-key "${MCUBOOT_SIGNING_KEY}" \
        --bootloader-elf "${TMPDIR}/bootloader.elf" \
        --bootloader-bin "${TMPDIR}/bootloader.bin" \
        --bl2-elf "${TMPDIR}/bl2.elf" \
        --bl2-bin "${TMPDIR}/bl2.bin" \
        --boot-xip-base 0x02000000 \
        --bl2-load-base "${BL2_LOAD_ADDRESS}" \
        --bl2-primary-xip-base "${BL2_XIP_ADDRESS}" \
        --output "${TMPDIR}/bk7258-trust-chain.json"
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

# Materialized product profiles are intentionally temporary.  Preserve the
# logical product/role identity in the archived resolved configurations
# instead of publishing an already-deleted host /tmp path.

if [[ -n "${AIDK_PRODUCT_ID}" ]]; then
    for role in cp ap; do
        config_copy="${TMPDIR}/nuttx-${role}.config"
        if ! grep -q '^CONFIG_BASE_DEFCONFIG=' "${config_copy}"; then
            printf 'build_dual_image: %s config lacks BASE_DEFCONFIG provenance\n' \
                "${role}" >&2
            exit 2
        fi
        sed -i \
            "s|^CONFIG_BASE_DEFCONFIG=.*|CONFIG_BASE_DEFCONFIG=\"bk7258-product:${AIDK_PRODUCT_ID}:${role}\"|" \
            "${config_copy}"
    done
fi

# The SWD route, target and hold are Kconfig defaults, so savedefconfig removes
# their explicit selectors.  The source-profile gate above rejects every
# non-default override; bind the contract to the resolved CP configuration as
# well so a future Kconfig-default change fails before signing or packaging.

if [[ "${CP_CONFIG_NAME}" == "${BK7258_JPEG_M2M_VALIDATION_CP}" ||
      "${AP_CONFIG_NAME}" == "${BK7258_JPEG_M2M_VALIDATION_AP}" ||
      "${CP_PROFILE_COMPAT}" == "${BK7258_JPEG_M2M_VALIDATION_COMPAT}" ||
      "${AP_PROFILE_COMPAT}" == "${BK7258_JPEG_M2M_VALIDATION_COMPAT}" ]]; then
    for symbol in BK7258_SWD_DEBUG BK7258_SWD_PINS_P0_P1 \
                  BK7258_SWD_TARGET_CP BK7258_SWD_BOOT_HOLD; do
        if ! grep -qx "CONFIG_${symbol}=y" \
            "${TMPDIR}/nuttx-cp.config"; then
            printf 'build_dual_image: JPEG M2M resolved CP config lacks %s\n' \
                "${symbol}" >&2
            exit 2
        fi
    done
fi

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

verify_sdk_map_role()
{
    local map="$1"
    local role="$2"
    local expected="$3"
    local actual

    [[ -f "${map}" ]] || {
        printf 'build_dual_image: missing %s link map: %s\n' \
            "${role}" "${map}" >&2
        exit 1
    }
    actual="$(sed -n \
        "s#.*versions/\\([^/]*\\)/${role}/libs/.*#\\1#p" \
        "${map}" | sort -u)"
    if [[ "${actual}" != "${expected}" ]]; then
        printf 'build_dual_image: %s linked SDK bundle mismatch: expected=%s actual=%s\n' \
            "${role}" "${expected}" "${actual:-missing}" >&2
        exit 1
    fi
}

verify_sdk_map_role "${OUTPUT}/nuttx-cp.map" cp \
    "${CP_SDK_BUNDLE_VERSION}"
verify_sdk_map_role "${OUTPUT}/nuttx-ap.map" ap \
    "${AP_SDK_BUNDLE_VERSION}"

require_elf_symbol()
{
    local elf="$1"
    local symbol="$2"

    if ! arm-none-eabi-nm -S --defined-only "${elf}" |
         awk -v symbol="${symbol}" \
             'NF >= 4 && $NF == symbol && $(NF - 2) != "00000000" \
              { found = 1 } END { exit !found }'; then
        printf 'build_dual_image: required symbol %s is not linked in %s\n' \
            "${symbol}" "${elf}" >&2
        exit 1
    fi
}

forbid_elf_symbol()
{
    local elf="$1"
    local symbol="$2"

    if arm-none-eabi-nm --defined-only "${elf}" |
       awk -v symbol="${symbol}" 'NF >= 3 && $NF == symbol { found = 1 }
           END { exit !found }'; then
        printf 'build_dual_image: forbidden symbol %s is linked in %s\n' \
            "${symbol}" "${elf}" >&2
        exit 1
    fi
}

require_map_symbol_owner()
{
    local map="$1"
    local symbol="$2"
    local owner="$3"

    if ! awk -v section=".text.${symbol}" -v owner="${owner}" '
        function nonzero_hex(value) {
          return value ~ /^0x[0-9a-fA-F]+$/ && value !~ /^0x0+$/
        }
        function live_owner_line(i, count, address, size) {
          if (index($0, owner) == 0) return 0
          count = 0
          for (i = 1; i <= NF; i++) {
            if ($i ~ /^0x[0-9a-fA-F]+$/) {
              count++
              if (count == 1) address = $i
              if (count == 2) size = $i
            }
          }
          return count >= 2 && nonzero_hex(address) && nonzero_hex(size)
        }
        $0 == "Linker script and memory map" { memory_map = 1; next }
        $0 == "Cross Reference Table" { memory_map = 0; active = 0 }
        !memory_map { next }
        $1 == section {
          if (live_owner_line()) found = 1
          else active = 1
          next
        }
        active && live_owner_line() { found = 1; active = 0; next }
        active && $1 ~ /^\./ { active = 0 }
        END { exit !found }
      ' "${map}"; then
        printf 'build_dual_image: symbol %s in %s is not owned by %s\n' \
            "${symbol}" "${map}" "${owner}" >&2
        exit 1
    fi
}

if config_enabled "${AP_CONFIG}" BK7258_JPEG_M2M; then
    command -v arm-none-eabi-nm >/dev/null 2>&1 || {
        printf '%s\n' \
            'build_dual_image: arm-none-eabi-nm is required for JPEG M2M ELF gates' >&2
        exit 1
    }

    for symbol in bk7258_jpeg_m2m_register \
                  bk7258_jpeg_decoder_initialize codec_register \
                  bk_jpeg_decode_hw_decode bk_jpeg_dec_hw_start; do
        require_elf_symbol "${OUTPUT}/nuttx-ap.elf" "${symbol}"
    done

    require_map_symbol_owner "${OUTPUT}/nuttx-ap.map" \
        bk7258_jpeg_m2m_register \
        'staging/libarch.a(bk7258_jpeg_m2m.o)'
    require_map_symbol_owner "${OUTPUT}/nuttx-ap.map" \
        bk7258_jpeg_decoder_initialize \
        'staging/libarch.a(bk7258_jpeg_decoder.o)'
    require_map_symbol_owner "${OUTPUT}/nuttx-ap.map" codec_register \
        'staging/libdrivers.a(v4l2_m2m.o)'
    require_map_symbol_owner "${OUTPUT}/nuttx-ap.map" \
        bk_jpeg_decode_hw_decode \
        "versions/${AP_SDK_BUNDLE_VERSION}/ap/libs/libbk_jpeg_decoder.a(bk_jpeg_decode_hw.c.obj)"
    require_map_symbol_owner "${OUTPUT}/nuttx-ap.map" bk_jpeg_dec_hw_start \
        "versions/${AP_SDK_BUNDLE_VERSION}/ap/libs/libdriver.a(jpeg_dec_driver.c.obj)"
fi

if config_enabled "${AP_CONFIG}" BK7258_JPEG_M2M_VALIDATION; then
    for symbol in bk7258_jpeg_m2m_validation_start \
                  g_bk7258_jpeg_m2m_validation_diag; do
        require_elf_symbol "${OUTPUT}/nuttx-ap.elf" "${symbol}"
    done

    require_map_symbol_owner "${OUTPUT}/nuttx-ap.map" \
        bk7258_jpeg_m2m_validation_start \
        'staging/libarch.a(bk7258_jpeg_m2m_validation.o)'

    for symbol in bk7258_dma2d_initialize bk7258_dvp_initialize \
                  bk7258_jpeg_encoder_initialize bk7258_lcd_initialize; do
        forbid_elf_symbol "${OUTPUT}/nuttx-ap.elf" "${symbol}"
    done
fi

if config_enabled "${AP_CONFIG}" BK7258_AUD_LIFECYCLE_VALIDATION ||
   config_enabled "${AP_CONFIG}" BK7258_AUD_DAC_EQ; then
    command -v arm-none-eabi-nm >/dev/null 2>&1 || {
        printf '%s\n' \
            'build_dual_image: arm-none-eabi-nm is required for Audio ELF gates' >&2
        exit 1
    }

    for symbol in bk7258_aud_initialize bk7258_aud_get_diag; do
        require_elf_symbol "${OUTPUT}/nuttx-ap.elf" "${symbol}"
    done
fi

if config_enabled "${AP_CONFIG}" BK7258_AUD_LIFECYCLE_VALIDATION; then
    for symbol in bk7258_aud_validation_start \
                  g_bk7258_aud_validation_diag; do
        require_elf_symbol "${OUTPUT}/nuttx-ap.elf" "${symbol}"
    done

    require_map_symbol_owner "${OUTPUT}/nuttx-ap.map" \
        bk7258_aud_validation_start \
        'staging/libarch.a(bk7258_aud_validation.o)'
fi

if config_enabled "${AP_CONFIG}" BK7258_AUD_DAC_EQ; then
    for symbol in bk_aud_dac_eq_config bk_aud_dac_eq_deconfig; do
        require_elf_symbol "${OUTPUT}/nuttx-ap.elf" "${symbol}"
        require_map_symbol_owner "${OUTPUT}/nuttx-ap.map" "${symbol}" \
            "versions/${AP_SDK_BUNDLE_VERSION}/ap/libs/libdriver.a(aud_dac_driver.c.obj)"
    done

    require_map_symbol_owner "${OUTPUT}/nuttx-ap.map" \
        bk7258_aud_initialize 'staging/libarch.a(bk7258_aud.o)'
    forbid_elf_symbol "${OUTPUT}/nuttx-ap.elf" bk_aud_eq_init
    forbid_elf_symbol "${OUTPUT}/nuttx-ap.elf" bk_aud_eq_deinit
    if grep -Fq '/libs/libeq.a(' "${OUTPUT}/nuttx-ap.map"; then
        printf '%s\n' \
            'build_dual_image: hardware-EQ validation must not link software libeq.a' >&2
        exit 1
    fi
fi

if config_enabled "${CP_CONFIG}" BK7258_PM_STANDBY_ONESHOT_VERIFY; then
    command -v arm-none-eabi-nm >/dev/null 2>&1 || {
        printf '%s\n' \
            'build_dual_image: arm-none-eabi-nm is required for standby ELF gates' >&2
        exit 1
    }

    for symbol in pm_idle bk7258_pm_cp_standby \
                  bk7258_systick_prepare_sleep \
                  bk7258_systick_restore_after_sleep; do
        require_elf_symbol "${OUTPUT}/nuttx-cp.elf" "${symbol}"
    done
fi

if config_enabled "${AP_CONFIG}" BK7258_SARADC; then
    command -v arm-none-eabi-nm >/dev/null 2>&1 || {
        printf '%s\n' \
            'build_dual_image: arm-none-eabi-nm is required for SARADC ELF gates' >&2
        exit 1
    }

    for symbol in bk7258_saradc_server_initialize \
                  bk7258_sdk_runtime_initialize \
                  bk_gpio_driver_init bk_adc_driver_init \
                  mb_saradc_ipc_init bk_saradc_server_init \
                  bk_int_isr_register; do
        require_elf_symbol "${OUTPUT}/nuttx-cp.elf" "${symbol}"
    done

    require_map_symbol_owner "${OUTPUT}/nuttx-cp.map" \
        bk_int_isr_register 'libarch.a(bk7258_sdk_irq.o)'
    require_map_symbol_owner "${OUTPUT}/nuttx-cp.map" \
        bk_adc_driver_init 'cp/libs/libdriver.a(adc_driver.c.obj)'

    for symbol in bk7258_sdk_runtime_initialize \
                  bk7258_saradc_initialize bk7258_saradc_get_diag \
                  g_bk7258_saradc_diag adc_register \
                  bk_adc_chan_init_gpio bk_adc_chan_deinit_gpio \
                  bk_adc_acquire bk_adc_init bk_adc_set_config \
                  bk_adc_enable_bypass_clalibration bk_adc_start \
                  bk_adc_read bk_adc_stop bk_adc_deinit bk_adc_release; do
        require_elf_symbol "${OUTPUT}/nuttx-ap.elf" "${symbol}"
    done

    require_map_symbol_owner "${OUTPUT}/nuttx-ap.map" \
        adc_register 'libdrivers.a(adc.o)'
    require_map_symbol_owner "${OUTPUT}/nuttx-ap.map" \
        bk_adc_read 'ap/libs/libdriver.a(saradc_client.c.obj)'
fi

if config_enabled "${AP_CONFIG}" BK7258_T5_BOARD_SARADC_KEY_VALIDATION; then
    require_elf_symbol "${OUTPUT}/nuttx-ap.elf" \
        bk7258_saradc_validation_start
    require_elf_symbol "${OUTPUT}/nuttx-ap.elf" \
        g_bk7258_saradc_validation_diag
    forbid_elf_symbol "${OUTPUT}/nuttx-ap.elf" bk_adc_single_read
fi

if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    cp "${TMPDIR}/cp-raw-crc.bin" "${OUTPUT}/cp-raw-crc.bin"
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

CP_STANDARD_IMAGE="${TMPDIR}/app.bin"
AP_STANDARD_IMAGE="${TMPDIR}/app1.bin"
if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    CP_STANDARD_IMAGE="${TMPDIR}/cp-raw.bin"
    AP_STANDARD_IMAGE="${TMPDIR}/ap-raw.bin"
fi

PACK_DUAL_ARGS=(
    --boot "${BOARD_DIR}/bootloader/bl_crc.bin"
    --cp-raw "${TMPDIR}/app.bin"
    --cp-standard "${CP_STANDARD_IMAGE}"
    --cp-crc "${TMPDIR}/app_crc.bin"
    --ap-raw "${TMPDIR}/app1.bin"
    --ap-standard "${AP_STANDARD_IMAGE}"
    --ap-crc "${TMPDIR}/app1_crc.bin"
    --output "${OUTPUT}"
)
if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    # Standard role images remain the unsigned logical NuttX outputs; the
    # separately named CRC-expanded files below are the Flash payloads.
    PACK_DUAL_ARGS+=(
        --bl2-primary-crc "${TMPDIR}/bl2_crc.bin"
        --bl2-secondary-crc "${TMPDIR}/bl2_secondary_crc.bin"
        --trust-chain "${TMPDIR}/bk7258-trust-chain.json"
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

if [[ "${CP_SDK_BUNDLE_VERSION}" == "${AP_SDK_BUNDLE_VERSION}" ]]; then
    SDK_BUNDLE_SUMMARY="${CP_SDK_BUNDLE_VERSION}"
else
    SDK_BUNDLE_SUMMARY=role-specific
fi

CP_CONFIG_RECORD="${CP_CONFIG}"
AP_CONFIG_RECORD="${AP_CONFIG}"
CP_PROFILE_METADATA_RECORD="${CP_CONFIG}/profile.conf"
AP_PROFILE_METADATA_RECORD="${AP_CONFIG}/profile.conf"
PROFILE_MATERIALIZER=none
CP_PROFILE_SEED=none
AP_PROFILE_SEED=none
if [[ -n "${AIDK_PRODUCT_ID}" ]]; then
    CP_CONFIG_RECORD="bk7258-product:${AIDK_PRODUCT_ID}:cp"
    AP_CONFIG_RECORD="bk7258-product:${AIDK_PRODUCT_ID}:ap"
    CP_PROFILE_METADATA_RECORD="generated:${CP_PROFILE_COMPAT}:cp"
    AP_PROFILE_METADATA_RECORD="generated:${AP_PROFILE_COMPAT}:ap"
    PROFILE_MATERIALIZER=board/bk7258/scripts/materialize_aidk_profiles.py
    CP_PROFILE_SEED=board/bk7258/configs/t5ai_core_cp_mcuboot
    AP_PROFILE_SEED=board/bk7258/configs/t5ai_core_ap_mcuboot
fi

cat > "${OUTPUT}/build-profile.txt" <<EOF
CP_CONFIG_NAME=${CP_CONFIG_NAME}
AP_CONFIG_NAME=${AP_CONFIG_NAME}
CP_CONFIG=${CP_CONFIG_RECORD}
AP_CONFIG=${AP_CONFIG_RECORD}
PROFILE_SCHEMA=${CP_PROFILE_SCHEMA}
PHYSICAL_BOARD=${CP_PROFILE_BOARD}
PROFILE_BOOT=${CP_PROFILE_BOOT}
PROFILE_COMPAT=${CP_PROFILE_COMPAT}
CP_PROFILE_CLASS=${CP_PROFILE_CLASS}
AP_PROFILE_CLASS=${AP_PROFILE_CLASS}
CP_PROFILE_METADATA=${CP_PROFILE_METADATA_RECORD}
AP_PROFILE_METADATA=${AP_PROFILE_METADATA_RECORD}
PROFILE_MATERIALIZER=${PROFILE_MATERIALIZER}
CP_PROFILE_SEED=${CP_PROFILE_SEED}
AP_PROFILE_SEED=${AP_PROFILE_SEED}
BK7258_SDK_BUNDLE_VERSION=${SDK_BUNDLE_SUMMARY}
CP_SDK_BUNDLE_VERSION=${CP_SDK_BUNDLE_VERSION}
AP_SDK_BUNDLE_VERSION=${AP_SDK_BUNDLE_VERSION}
CP_SDK_BUNDLE_ROOT=${CP_SDK_BUNDLE_ROOT}
AP_SDK_BUNDLE_ROOT=${AP_SDK_BUNDLE_ROOT}
CP_SDK_ROLE_DIR=${CP_SDK_ROLE_DIR}
AP_SDK_ROLE_DIR=${AP_SDK_ROLE_DIR}
CP_SDK_MANIFEST=${CP_SDK_MANIFEST}
AP_SDK_MANIFEST=${AP_SDK_MANIFEST}
CP_SDK_MANIFEST_SHA256=${CP_SDK_MANIFEST_SHA256}
AP_SDK_MANIFEST_SHA256=${AP_SDK_MANIFEST_SHA256}
CP_SDK_PROVENANCE_SHA256=${CP_SDK_PROVENANCE_SHA256}
AP_SDK_PROVENANCE_SHA256=${AP_SDK_PROVENANCE_SHA256}
MCUBOOT_PROFILE=${MCUBOOT_PROFILE}
BL1_USE_BL2=${BL1_USE_BL2}
MCUBOOT_VERSION=${MCUBOOT_VERSION}
MCUBOOT_SECURITY_COUNTER=${MCUBOOT_SECURITY_COUNTER}
MCUBOOT_OFFICIAL_PIPELINE=${MCUBOOT_OFFICIAL_PIPELINE}
MCUBOOT_SIGNING_KEY_REQUIRED=${MCUBOOT_PROFILE}
TRUST_CHAIN_CONTRACT=${TRUST_CHAIN_CONTRACT}
TRUST_CHAIN_PREFLIGHT_REQUIRED=${MCUBOOT_PROFILE}
BL1_MANIFEST_ENABLED=${MCUBOOT_PROFILE}
BL1_MANIFEST_FORMAT=${BL1_MANIFEST_FORMAT}
BL1_MANIFEST_RAW_PAGE=${BL1_MANIFEST_RAW_PAGE}
BL1_SWD_ENABLE=${BL1_SWD_ENABLE}
BL1_SWD_PIN_GROUP=${BL1_SWD_PIN_GROUP}
BL1_SWD_TARGET=${BL1_SWD_TARGET}
BL1_SWD_BOOT_HOLD=${BL1_SWD_BOOT_HOLD}
BL1_CONSOLE_UART=${BL1_CONSOLE_UART}
BL1_CONSOLE_BAUD=${BL1_CONSOLE_BAUD}
BL1_CONSOLE_DATA_BITS=${BL1_CONSOLE_DATA_BITS}
BL1_CONSOLE_PARITY=${BL1_CONSOLE_PARITY}
BL1_CONSOLE_STOP_BITS=${BL1_CONSOLE_STOP_BITS}
BL1_UART2_PIN_GROUP=${BL1_UART2_PIN_GROUP}
BL2_LOGICAL_SIZE=${BL2_LOGICAL_SIZE}
BL2_SECURITY_COUNTER_FLOOR=${BL2_SECURITY_COUNTER_FLOOR}
BL2_BOOT_POLICY_ENABLED=${MCUBOOT_PROFILE}
BL1_BOOT_POLICY=primary-then-secondary
BL2_XIP_ADDRESS=${BL2_XIP_ADDRESS}
BL2_SECONDARY_XIP_ADDRESS=${BL2_SECONDARY_XIP_ADDRESS}
BL2_LOAD_ADDRESS=${BL2_LOAD_ADDRESS}
BL2_FLASH_SEGMENT=${MCUBOOT_BL2_FLASH_SEGMENT}
EOF

if [[ -n "${AIDK_PRODUCT_ID}" ]] &&
   grep -Eq '(^|=)/tmp/|bk7258-aidk-profiles\.' \
       "${OUTPUT}/build-profile.txt" \
       "${OUTPUT}/nuttx-cp.config" \
       "${OUTPUT}/nuttx-ap.config"; then
    printf '%s\n' \
        'build_dual_image: AIDK package metadata contains temporary profile paths' \
        >&2
    exit 2
fi

if [[ "${MCUBOOT_PROFILE}" == "true" &&
      "${BL1_MANIFEST_RAW_PAGE}" == "false" ]]; then
    if [[ ! -f "${BKPACK_TOOL}" ]]; then
        printf 'build_dual_image: MCUboot profile requires container tool: %s\n' \
            "${BKPACK_TOOL}" >&2
        exit 2
    fi
    printf '%s\n' "build_dual_image: creating payload-bearing firmware.bkpack"
    python3 "${BKPACK_TOOL}" create \
        --source "${OUTPUT}" \
        --output "${OUTPUT}/firmware.bkpack"
    printf '%s\n' "build_dual_image: verifying payload-bearing firmware.bkpack"
    python3 "${BKPACK_TOOL}" verify \
        --package "${OUTPUT}/firmware.bkpack"
elif [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    rm -f "${OUTPUT}/firmware.bkpack"
    printf '%s\n' \
        "build_dual_image: firmware.bkpack NOT_GENERATED (raw BL1 manifest pages are not represented by the Windows Flash plans)"
else
    rm -f "${OUTPUT}/firmware.bkpack"
    printf '%s\n' \
        "build_dual_image: firmware.bkpack NOT_GENERATED (raw profile has no BL2/trust-chain contract)"
fi

cp "${OUTPUT}/app.bin" "${TOPDIR}/app.bin"
cp "${OUTPUT}/app_crc.bin" "${TOPDIR}/app_crc.bin"
cp "${OUTPUT}/app1.bin" "${TOPDIR}/app1.bin"
cp "${OUTPUT}/app1_crc.bin" "${TOPDIR}/app1_crc.bin"
cp "${OUTPUT}/bk7258-dual-image.json" "${TOPDIR}/"
if [[ "${MCUBOOT_PROFILE}" == "true" ]]; then
    cp "${OUTPUT}/bk7258-trust-chain.json" "${TOPDIR}/"
else
    rm -f "${TOPDIR}/bk7258-trust-chain.json"
fi

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

if config_enabled "${CP_CONFIG}" BK7258_RPTUN; then
    python3 "${SCRIPT_DIR}/verify_bk7258_rptun_layout.py" \
        --cp-elf "${OUTPUT}/nuttx-cp.elf" \
        --cp-map "${OUTPUT}/nuttx-cp.map" \
        --ap-elf "${OUTPUT}/nuttx-ap.elf" \
        --ap-map "${OUTPUT}/nuttx-ap.map" \
        --json "${OUTPUT}/bk7258-rptun-layout.json"
fi

if [[ "${CP_PROFILE_COMPAT}" == \
      t5ai_core_psram_validation_raw_v1 ]]; then
    BLE_VERIFY_IDENTITY_ARGS=()
    BLE_VERIFY_IDENTITY_ARGS+=(
        --expected-device-name "BK7258 N14"
        --expected-local-name "BK7258-N14"
    )

    python3 "${SCRIPT_DIR}/verify_bk7258_ble_gatt.py" \
        --cp-elf "${OUTPUT}/nuttx-cp.elf" \
        --ap-elf "${OUTPUT}/nuttx-ap.elf" \
        --ap-map "${OUTPUT}/nuttx-ap.map" \
        --json "${OUTPUT}/bk7258-ble-gatt.json" \
        "${BLE_VERIFY_IDENTITY_ARGS[@]}"
fi

if config_enabled "${CP_CONFIG}" BK7258_PSRAM_TEST &&
   config_enabled "${AP_CONFIG}" BK7258_PSRAM_TEST; then
    if [[ "${CP_SDK_BUNDLE_VERSION}" != "${AP_SDK_BUNDLE_VERSION}" ]]; then
        printf '%s\n' \
            'build_dual_image: PSRAM verification requires matching CP/AP SDK bundles' >&2
        exit 1
    fi

    PSRAM_VERIFY_ARGS=(
        --cp-elf "${OUTPUT}/nuttx-cp.elf"
        --cp-map "${OUTPUT}/nuttx-cp.map"
        --ap-elf "${OUTPUT}/nuttx-ap.elf"
        --ap-map "${OUTPUT}/nuttx-ap.map"
        --expected-bundle "${CP_SDK_BUNDLE_VERSION}"
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
