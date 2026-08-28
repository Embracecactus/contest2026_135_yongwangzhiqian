#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Build and run the complete BK7258 host regression fixture.
set -eu
cd "$(dirname "$0")"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
export ASAN_OPTIONS

echo "BK7258_HOST_TEST_BEGIN"
echo "git_commit=$(git rev-parse --verify HEAD)"
echo "cc=$(${CC:-cc} --version | sed -n '1p')"
echo "python=$(python3 --version 2>&1)"
echo "cmocka=$(pkg-config --modversion cmocka)"
echo "sanitizers=address,undefined:test_boot_bl1_policy"
echo "partition_csv_sha256=$(sha256sum ../../boards/bk7258/common/partitions/bk7258/bk7258_ab_agent_onchip_persistent.csv | awk '{print $1}')"

make clean >/dev/null
make run
echo "BK7258_HOST_TEST_PASS"
