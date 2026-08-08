#!/bin/sh
# Build and run the BK7258 mailbox and BL1-policy host unit tests.
set -e
cd "$(dirname "$0")"
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
export ASAN_OPTIONS
make clean >/dev/null
make
./build/test_bk7258_rptun_mbox
./build/test_boot_bl1_policy
