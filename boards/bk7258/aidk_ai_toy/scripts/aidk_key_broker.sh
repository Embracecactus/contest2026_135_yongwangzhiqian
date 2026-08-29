#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail
umask 077

die()
{
  printf 'AIDK key broker: %s\n' "$*" >&2
  exit 1
}

[[ $# -ge 3 ]] || die "usage: $0 seal|unseal|discard STATE_DIR GENERATION [KEY]"
action=$1
state_dir=$2
generation=$3
password_file=${AIDK_KEY_BROKER_PASSWORD_FILE:-}
openssl_bin=${AIDK_OPENSSL:-/usr/bin/openssl}

[[ $generation =~ ^[1-9][0-9]*$ ]] || die "invalid generation"
[[ -n $password_file ]] || die "AIDK_KEY_BROKER_PASSWORD_FILE is required"
[[ -f $password_file && ! -L $password_file ]] || die "password file is not regular"
[[ $(stat -c '%a' "$password_file") == 600 ]] || die "password file mode must be 0600"
[[ -x $openssl_bin ]] || die "OpenSSL executable is unavailable"

mkdir -p "$state_dir/sealed"
chmod 700 "$state_dir" "$state_dir/sealed"
sealed="$state_dir/sealed/mcuboot-generation-${generation}.pem.enc"

case "$action" in
  seal)
    [[ $# -eq 4 ]] || die "seal requires one plaintext input"
    [[ -f $4 && ! -L $4 ]] || die "plaintext key is not regular"
    [[ ! -e $sealed && ! -L $sealed ]] || die "sealed generation already exists"
    temporary="$sealed.pending.$$"
    trap 'test ! -e "$temporary" || { shred -u "$temporary" 2>/dev/null || true; }' EXIT
    "$openssl_bin" pkcs8 -topk8 -v2 aes-256-cbc \
      -in "$4" -out "$temporary" -passout "file:$password_file"
    chmod 600 "$temporary"
    mv "$temporary" "$sealed"
    trap - EXIT
    ;;
  unseal)
    [[ $# -eq 4 ]] || die "unseal requires one plaintext output"
    [[ -f $sealed && ! -L $sealed ]] || die "sealed generation is unavailable"
    [[ ! -e $4 && ! -L $4 ]] || die "plaintext output already exists"
    "$openssl_bin" pkcs8 -in "$sealed" -out "$4" \
      -passin "file:$password_file"
    chmod 600 "$4"
    ;;
  discard)
    [[ $# -eq 3 ]] || die "discard takes no key path"
    if [[ -f $sealed && ! -L $sealed ]]; then
      shred -u "$sealed" 2>/dev/null || die "cannot discard sealed generation"
    fi
    ;;
  *)
    die "unknown action: $action"
    ;;
esac
