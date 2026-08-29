#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail
umask 077

script_dir=$(cd "$(dirname "$0")" && pwd -P)
board_dir=$(cd "$script_dir/.." && pwd -P)
repo_root=$(cd "$board_dir/../../.." && pwd -P)
workspace=$(cd "$repo_root/.." && pwd -P)
bk7258="$repo_root/tools/bk7258/bk7258.py"
transport="$script_dir/aidk_usb_ota.py"
default_broker="$script_dir/aidk_key_broker.sh"
openssl_bin=${AIDK_OPENSSL:-/usr/bin/openssl}
python_bin=${AIDK_PYTHON:-python3}
jobs=${AIDK_JOBS:-12}
download_baud=${AIDK_DOWNLOAD_BAUD:-460800}

die()
{
  printf 'AIDK pipeline: %s\n' "$*" >&2
  exit 1
}

usage()
{
  printf '%s\n' \
    "usage:" \
    "  $0 provision --state-dir DIR --base FILE --version X.Y.Z+N" \
    "       --output DIR --loader EXE --port COMN [--reset-hook EXE]" \
    "  $0 ota --state-dir DIR --version X.Y.Z+N --output DIR" \
    "       [--ota-port PORT] [--control-port PORT] [--no-deploy]" \
    "  $0 deploy-ota --package FILE [--state-dir DIR --version X.Y.Z+N]" \
    "       [--ota-port PORT] [--control-port PORT]"
}

absolute_dir()
{
  local parent name
  parent=$(dirname "$1")
  name=$(basename "$1")
  mkdir -p "$parent"
  parent=$(cd "$parent" && pwd -P)
  printf '%s/%s\n' "$parent" "$name"
}

outside_repo()
{
  case "$1/" in
    "$repo_root/"*) return 1 ;;
    *) return 0 ;;
  esac
}

generation_of()
{
  local value=${1##*+}
  [[ $1 =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\+([1-9][0-9]*)$ ]] ||
    die "version must be MAJOR.MINOR.REVISION+GENERATION"
  printf '%s\n' "$value"
}

state_get()
{
  local key=$1 state=$2
  awk -F= -v key="$key" '$1 == key {sub(/^[^=]*=/, ""); print; found=1} END {exit !found}' "$state"
}

safe_delete_keydir()
{
  local directory=${keydir:-}
  [[ -n $directory && $directory == /tmp/aidk-trust.* && -d $directory ]] || return 0
  find "$directory" -maxdepth 1 -type f -exec shred -u {} \; 2>/dev/null || true
  rmdir "$directory" 2>/dev/null || true
}

run_transport()
{
  local arguments=("$@")
  if [[ -n ${AIDK_WINDOWS_PYTHON:-} ]]; then
    local windows_script converted=() item
    windows_script=$(wslpath -w "$transport")
    for item in "${arguments[@]}"; do
      if [[ $item == /* && -e $item ]]; then
        converted+=("$(wslpath -w "$item")")
      else
        converted+=("$item")
      fi
    done
    "$AIDK_WINDOWS_PYTHON" "$windows_script" "${converted[@]}"
  else
    "$python_bin" "$transport" "${arguments[@]}"
  fi
}

manifest_from_log()
{
  local manifest
  manifest=$(sed -n 's/^build manifest=//p' "$1" | tail -1)
  [[ -n $manifest && -f $manifest ]] || die "build did not publish a manifest"
  printf '%s\n' "$manifest"
}

write_state()
{
  local state_dir=$1 root_generation=$2 accepted_generation=$3 version=$4
  local temporary="$state_dir/accepted.env.pending.$$"
  {
    printf 'STATE_SCHEMA=1\n'
    printf 'BOARD=aidk_ai_toy\n'
    printf 'ROOT_GENERATION=%s\n' "$root_generation"
    printf 'ACCEPTED_GENERATION=%s\n' "$accepted_generation"
    printf 'ACCEPTED_VERSION=%s\n' "$version"
    printf 'BL1_PUBLIC=bl1-public.pem\n'
    printf 'MCUBOOT_PUBLIC=mcuboot-public.pem\n'
  } >"$temporary"
  chmod 600 "$temporary"
  mv "$temporary" "$state_dir/accepted.env"
}

provision()
{
  local state_dir= base= version= output= loader= port= reset_hook=
  local reset_hook_pid=
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --state-dir) state_dir=$2; shift 2 ;;
      --base) base=$2; shift 2 ;;
      --version) version=$2; shift 2 ;;
      --output) output=$2; shift 2 ;;
      --loader) loader=$2; shift 2 ;;
      --port) port=$2; shift 2 ;;
      --reset-hook) reset_hook=$2; shift 2 ;;
      *) die "unknown provision option: $1" ;;
    esac
  done
  [[ -n $state_dir && -n $base && -n $version && -n $output && -n $loader && -n $port ]] ||
    die "provision arguments are incomplete"
  [[ -f $base && ! -L $base ]] || die "accepted base is not a regular file"
  [[ -x $loader ]] || die "BK Loader is not executable"
  [[ $port =~ ^COM([1-9][0-9]*)$ ]] || die "port must be COMN"
  local port_number=${BASH_REMATCH[1]}
  [[ $download_baud =~ ^[1-9][0-9]*$ ]] ||
    die "AIDK_DOWNLOAD_BAUD must be a positive integer"
  local generation broker state previous=0 base_sha manifest operator operator_size
  generation=$(generation_of "$version")
  state_dir=$(absolute_dir "$state_dir")
  outside_repo "$state_dir" || die "state/secrets directory must be outside the repository"
  mkdir -p "$state_dir"
  chmod 700 "$state_dir"
  state="$state_dir/accepted.env"
  if [[ -f $state ]]; then
    previous=$(state_get ACCEPTED_GENERATION "$state")
  fi
  (( generation > previous )) || die "full generation must exceed accepted generation $previous"
  output=$(absolute_dir "$output")
  [[ ! -e $output && ! -L $output ]] || die "release output already exists"
  broker=${AIDK_KEY_BROKER:-$default_broker}
  [[ -x $broker ]] || die "key broker is not executable"
  [[ -x $openssl_bin ]] || die "OpenSSL is unavailable"
  keydir=$(mktemp -d /tmp/aidk-trust.XXXXXX)
  chmod 700 "$keydir"
  local sealed=0 accepted=0
  trap 'status=$?; if (( status != 0 && sealed != 0 && accepted == 0 )); then "$broker" discard "$state_dir" "$generation" || true; fi; safe_delete_keydir; exit "$status"' EXIT

  "$openssl_bin" ecparam -name prime256v1 -genkey -noout -out "$keydir/bl1-private.pem"
  "$openssl_bin" pkey -in "$keydir/bl1-private.pem" -pubout -out "$keydir/bl1-public.pem"
  "$openssl_bin" ecparam -name prime256v1 -genkey -noout -out "$keydir/mcuboot-private.pem"
  "$openssl_bin" pkey -in "$keydir/mcuboot-private.pem" -pubout -out "$keydir/mcuboot-public.pem"
  chmod 600 "$keydir"/*.pem

  local build_log
  build_log=$(mktemp /tmp/aidk-build.XXXXXX.log)
  "$python_bin" "$bk7258" build --board aidk_ai_toy --boot mcuboot --clean \
    --jobs "$jobs" --bl1-public-key "$keydir/bl1-public.pem" \
    --mcuboot-public-key "$keydir/mcuboot-public.pem" \
    --openssl "$openssl_bin" --rollback-floor "$generation" | tee "$build_log"
  manifest=$(manifest_from_log "$build_log")
  base_sha=$(sha256sum "$base" | awk '{print $1}')
  "$python_bin" "$bk7258" release full --build-manifest "$manifest" \
    --bl1-key "$keydir/bl1-private.pem" \
    --mcuboot-key "$keydir/mcuboot-private.pem" --version "$version" \
    --base "$base" --base-sha256 "$base_sha" --openssl "$openssl_bin" \
    --output-dir "$output"
  mkdir -p "$output/logs"
  mv "$build_log" "$output/logs/build.log"
  broker=$broker
  "$broker" seal "$state_dir" "$generation" "$keydir/mcuboot-private.pem"
  sealed=1

  operator=$(find "$output/flash" -maxdepth 1 -type f -name 'operator-*.bin' -print)
  [[ $(printf '%s\n' "$operator" | sed '/^$/d' | wc -l) -eq 1 ]] || die "release has no unique operator image"
  operator_size=$(stat -c '%s' "$operator")
  [[ $operator_size -eq $((0x7fa000)) ]] || die "operator image size is not 0x7fa000"
  if [[ -n $reset_hook ]]; then
    [[ -x $reset_hook ]] || die "reset hook is not executable"
    "$reset_hook" &
    reset_hook_pid=$!
  else
    [[ -t 0 ]] ||
      die "manual K1 reset requires an interactive terminal or --reset-hook"
    printf '%s\n' \
      'AIDK pipeline: press Enter when the operator is ready at K1 RESET.' \
      'AIDK pipeline: after BK Loader prints Getting Bus, press and release K1 once.' >&2
    IFS= read -r
  fi
  local windows_operator
  windows_operator=$(wslpath -w "$operator")
  local loader_status
  set +e
  "$loader" download -p "$port_number" -b "$download_baud" --uart-type OTHER \
    --mainBin-multi "${windows_operator}@0x0-0x7fa000" \
    --reboot 1 | tee "$output/logs/bk-loader.log"
  loader_status=${PIPESTATUS[0]}
  set -e
  if [[ -n $reset_hook_pid ]]; then
    local reset_hook_status
    set +e
    wait "$reset_hook_pid"
    reset_hook_status=$?
    set -e
    (( reset_hook_status == 0 )) ||
      die "reset hook failed with status $reset_hook_status"
  fi
  if rg -qi 'GetBus fail|Writing Flash Failed|erase.*fail|download.*fail' "$output/logs/bk-loader.log"; then
    die "BK Loader reported a full-download failure"
  fi
  if ! rg -q 'Writing Flash OK|All Finished Successfully' \
      "$output/logs/bk-loader.log"; then
    die "BK Loader did not publish a full-download success marker (exit $loader_status)"
  fi
  if (( loader_status != 0 )); then
    printf 'AIDK pipeline: BK Loader returned %d after explicit success; continuing acceptance.\n' \
      "$loader_status" >&2
  fi

  run_transport --status-only --control-port "$port" \
    --expected-version "$version" --expected-counter "$generation"
  install -m 0644 "$keydir/bl1-public.pem" "$state_dir/bl1-public.pem"
  install -m 0644 "$keydir/mcuboot-public.pem" "$state_dir/mcuboot-public.pem"
  write_state "$state_dir" "$generation" "$generation" "$version"
  accepted=1
  safe_delete_keydir
  trap - EXIT
  printf 'AIDK provision: PASS version=%s release=%s\n' "$version" "$output"
}

ota()
{
  local state_dir= version= output= ota_port= control_port= deploy=1
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --state-dir) state_dir=$2; shift 2 ;;
      --version) version=$2; shift 2 ;;
      --output) output=$2; shift 2 ;;
      --ota-port) ota_port=$2; shift 2 ;;
      --control-port) control_port=$2; shift 2 ;;
      --no-deploy) deploy=0; shift ;;
      *) die "unknown ota option: $1" ;;
    esac
  done
  [[ -n $state_dir && -n $version && -n $output ]] || die "ota arguments are incomplete"
  local generation state root_generation accepted_generation accepted_version
  local broker manifest package build_log
  generation=$(generation_of "$version")
  state_dir=$(cd "$state_dir" && pwd -P)
  outside_repo "$state_dir" || die "state/secrets directory must be outside the repository"
  state="$state_dir/accepted.env"
  [[ -f $state && ! -L $state ]] || die "accepted device state is unavailable"
  root_generation=$(state_get ROOT_GENERATION "$state")
  accepted_generation=$(state_get ACCEPTED_GENERATION "$state")
  accepted_version=$(state_get ACCEPTED_VERSION "$state")
  (( generation > accepted_generation )) || die "OTA generation must exceed $accepted_generation"
  [[ -f $state_dir/bl1-public.pem && -f $state_dir/mcuboot-public.pem ]] ||
    die "accepted public roots are unavailable"
  output=$(absolute_dir "$output")
  [[ ! -e $output && ! -L $output ]] || die "release output already exists"
  broker=${AIDK_KEY_BROKER:-$default_broker}
  [[ -x $broker ]] || die "key broker is not executable"
  keydir=$(mktemp -d /tmp/aidk-trust.XXXXXX)
  chmod 700 "$keydir"
  trap 'status=$?; safe_delete_keydir; exit "$status"' EXIT
  "$broker" unseal "$state_dir" "$root_generation" "$keydir/mcuboot-private.pem"

  build_log=$(mktemp /tmp/aidk-build.XXXXXX.log)
  "$python_bin" "$bk7258" build --board aidk_ai_toy --boot mcuboot --clean \
    --jobs "$jobs" --bl1-public-key "$state_dir/bl1-public.pem" \
    --mcuboot-public-key "$state_dir/mcuboot-public.pem" \
    --openssl "$openssl_bin" --rollback-floor "$root_generation" | tee "$build_log"
  manifest=$(manifest_from_log "$build_log")
  "$python_bin" "$bk7258" release ota --build-manifest "$manifest" \
    --mcuboot-key "$keydir/mcuboot-private.pem" --version "$version" \
    --openssl "$openssl_bin" --output-dir "$output"
  mkdir -p "$output/logs"
  mv "$build_log" "$output/logs/build.log"
  package=$(find "$output/package" -maxdepth 1 -type f -name '*-ota.bkpack' -print)
  [[ $(printf '%s\n' "$package" | sed '/^$/d' | wc -l) -eq 1 ]] || die "release has no unique OTA package"
  safe_delete_keydir

  if (( deploy != 0 )); then
    local arguments=(--package "$package")
    if [[ $control_port != none ]]; then
      local recovery=(--reboot-only --expected-version "$accepted_version"
        --expected-counter "$accepted_generation")
      [[ -z $control_port ]] || recovery+=(--control-port "$control_port")
      run_transport "${recovery[@]}"
    fi
    [[ -z $ota_port ]] || arguments+=(--ota-port "$ota_port")
    [[ -z $control_port ]] || arguments+=(--control-port "$control_port")
    run_transport "${arguments[@]}"
    write_state "$state_dir" "$root_generation" "$generation" "$version"
  fi
  trap - EXIT
  printf 'AIDK OTA: PASS version=%s release=%s deployed=%s\n' \
    "$version" "$output" "$deploy"
}

deploy_ota()
{
  local package= state_dir= version= ota_port= control_port=
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --package) package=$2; shift 2 ;;
      --state-dir) state_dir=$2; shift 2 ;;
      --version) version=$2; shift 2 ;;
      --ota-port) ota_port=$2; shift 2 ;;
      --control-port) control_port=$2; shift 2 ;;
      *) die "unknown deploy-ota option: $1" ;;
    esac
  done
  [[ -n $package ]] || die "--package is required"
  [[ -z $state_dir && -z $version || -n $state_dir && -n $version ]] ||
    die "--state-dir and --version must be supplied together"
  "$python_bin" "$bk7258" verify trust --package "$package" --openssl "$openssl_bin"
  local arguments=(--package "$package")
  local generation= root_generation= accepted_generation= accepted_version= state=
  if [[ -n $state_dir ]]; then
    generation=$(generation_of "$version")
    state_dir=$(cd "$state_dir" && pwd -P)
    outside_repo "$state_dir" ||
      die "state/secrets directory must be outside the repository"
    state="$state_dir/accepted.env"
    [[ -f $state && ! -L $state ]] || die "accepted device state is unavailable"
    root_generation=$(state_get ROOT_GENERATION "$state")
    accepted_generation=$(state_get ACCEPTED_GENERATION "$state")
    accepted_version=$(state_get ACCEPTED_VERSION "$state")
    (( generation > accepted_generation )) ||
      die "deployed OTA generation must exceed $accepted_generation"
    run_transport --package "$package" --inspect-only \
      --expected-version "$version" --expected-counter "$generation"
    if [[ $control_port != none ]]; then
      local recovery=(--reboot-only --expected-version "$accepted_version"
        --expected-counter "$accepted_generation")
      [[ -z $control_port ]] || recovery+=(--control-port "$control_port")
      run_transport "${recovery[@]}"
    fi
  fi
  [[ -z $ota_port ]] || arguments+=(--ota-port "$ota_port")
  [[ -z $control_port ]] || arguments+=(--control-port "$control_port")
  run_transport "${arguments[@]}"
  if [[ -n $state_dir ]]; then
    write_state "$state_dir" "$root_generation" "$generation" "$version"
  fi
}

[[ $# -gt 0 ]] || { usage; exit 2; }
command=$1
shift
case "$command" in
  provision) provision "$@" ;;
  ota) ota "$@" ;;
  deploy-ota) deploy_ota "$@" ;;
  -h|--help|help) usage ;;
  *) die "unknown command: $command" ;;
esac
