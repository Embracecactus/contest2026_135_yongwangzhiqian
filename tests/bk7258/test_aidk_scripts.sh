#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

test_dir=$(mktemp -d /tmp/bk7258-aidk-scripts.XXXXXX)
repo_root=$(cd "$(dirname "$0")/../.." && pwd -P)
pipeline="$repo_root/boards/bk7258/aidk_ai_toy/scripts/aidk_pipeline.sh"
broker="$repo_root/boards/bk7258/aidk_ai_toy/scripts/aidk_key_broker.sh"

cleanup()
{
  find "$test_dir" -type f -exec shred -u {} \; 2>/dev/null || true
  rm -rf "$test_dir"
}

trap cleanup EXIT INT TERM

password="$test_dir/password"
private_key="$test_dir/private.pem"
unsealed_key="$test_dir/unsealed.pem"
state_dir="$test_dir/state"

printf 'test-only-password\n' >"$password"
chmod 600 "$password"
openssl ecparam -name prime256v1 -genkey -noout -out "$private_key"

AIDK_KEY_BROKER_PASSWORD_FILE="$password" \
  AIDK_KEY_BROKER_ITERATIONS=600000 \
  "$broker" seal "$state_dir" 1 "$private_key"
AIDK_KEY_BROKER_PASSWORD_FILE="$password" \
  "$broker" unseal "$state_dir" 1 "$unsealed_key"

openssl pkey -in "$private_key" -pubout -out "$test_dir/original.pub"
openssl pkey -in "$unsealed_key" -pubout -out "$test_dir/unsealed.pub"
cmp "$test_dir/original.pub" "$test_dir/unsealed.pub"
test "$(stat -c '%a' "$state_dir/sealed/mcuboot-generation-1.pem.enc")" = 600
openssl asn1parse -in "$state_dir/sealed/mcuboot-generation-1.pem.enc" |
  rg -q 'PBKDF2'
openssl asn1parse -in "$state_dir/sealed/mcuboot-generation-1.pem.enc" |
  rg -q 'INTEGER[[:space:]]*:0927C0'

if AIDK_KEY_BROKER_PASSWORD_FILE="$password" \
     AIDK_KEY_BROKER_ITERATIONS=99999 \
     "$broker" seal "$state_dir" 2 "$private_key" >/dev/null 2>&1; then
  echo "AIDK script test: weak KDF iteration count was accepted" >&2
  exit 1
fi

test ! -e "$state_dir/sealed/mcuboot-generation-2.pem.enc"

fake_broker="$test_dir/fake-broker.sh"
fake_python="$test_dir/fake-python.sh"
cat >"$fake_broker" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
case "$1" in
  unseal)
    printf 'test-only-plaintext-key\n' >"$4"
    chmod 600 "$4"
    dirname "$4" >"$AIDK_TEST_KEYDIR_RECORD"
    ;;
  *) exit 0 ;;
esac
EOF
cat >"$fake_python" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
: >"$AIDK_TEST_READY"
while :; do
  sleep 1
done
EOF
chmod 700 "$fake_broker" "$fake_python"

signal_cleanup_test()
{
  local signal=$1 expected=$2
  local signal_state="$test_dir/signal-$signal"
  local record="$test_dir/keydir-$signal" ready="$test_dir/ready-$signal"
  local output="$test_dir/output-$signal" keydir

  mkdir -p "$signal_state"
  printf '%s\n' \
    'STATE_SCHEMA=1' \
    'BOARD=aidk_ai_toy' \
    'ROOT_GENERATION=1' \
    'ACCEPTED_GENERATION=1' \
    'ACCEPTED_VERSION=1.0.0+1' \
    'BL1_PUBLIC=bl1-public.pem' \
    'MCUBOOT_PUBLIC=mcuboot-public.pem' >"$signal_state/accepted.env"
  printf 'test-public\n' >"$signal_state/bl1-public.pem"
  printf 'test-public\n' >"$signal_state/mcuboot-public.pem"

  AIDK_TEST_PIPELINE="$pipeline" \
  AIDK_TEST_BROKER="$fake_broker" \
  AIDK_TEST_PYTHON="$fake_python" \
  AIDK_TEST_KEYDIR_RECORD="$record" \
  AIDK_TEST_READY="$ready" \
  AIDK_TEST_STATE="$signal_state" \
  AIDK_TEST_OUTPUT="$output" \
  AIDK_TEST_SIGNAL="$signal" \
  AIDK_TEST_EXPECTED="$expected" \
  python3 - <<'PY'
import os
import signal
import subprocess
import time

environment = os.environ.copy()
environment["AIDK_KEY_BROKER"] = environment["AIDK_TEST_BROKER"]
environment["AIDK_PYTHON"] = environment["AIDK_TEST_PYTHON"]
command = [
    environment["AIDK_TEST_PIPELINE"],
    "ota",
    "--state-dir",
    environment["AIDK_TEST_STATE"],
    "--version",
    "1.0.1+2",
    "--output",
    environment["AIDK_TEST_OUTPUT"],
    "--no-deploy",
]
process = subprocess.Popen(
    command,
    env=environment,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL,
    start_new_session=True,
)
deadline = time.monotonic() + 10
while not (
    os.path.isfile(environment["AIDK_TEST_READY"])
    and os.path.getsize(environment["AIDK_TEST_KEYDIR_RECORD"]) > 0
):
    if process.poll() is not None:
        raise SystemExit("pipeline exited before the signal test point")
    if time.monotonic() >= deadline:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
        raise SystemExit("pipeline did not reach the signal test point")
    time.sleep(0.05)

os.killpg(process.pid, getattr(signal, "SIG" + environment["AIDK_TEST_SIGNAL"]))
try:
    status = process.wait(timeout=10)
except subprocess.TimeoutExpired:
    os.killpg(process.pid, signal.SIGKILL)
    process.wait()
    raise SystemExit("pipeline did not terminate after signal")

expected = int(environment["AIDK_TEST_EXPECTED"])
if status != expected:
    raise SystemExit(f"pipeline status {status}, expected {expected}")
PY

  keydir=$(<"$record")
  [[ $keydir == /tmp/aidk-trust.* ]]
  test ! -e "$keydir"
}

signal_cleanup_test INT 130
signal_cleanup_test TERM 143

echo "BK7258_AIDK_SCRIPT_TEST_PASS kdf=pbkdf2-600000 signals=INT,TERM"
