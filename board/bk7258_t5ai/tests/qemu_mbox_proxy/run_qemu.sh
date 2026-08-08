#!/usr/bin/env bash
#
# Build and run the BK7258 MBOX0 notify proxy under QEMU mps2-an521 (SMP2).
# Mirrors the host-side unit tests (tests/test_bk7258_rptun_mbox.c) but runs
# the re-implemented notify state machine on two real Cortex-M33 cores, so
# cross-core interrupts, the one-deep mailbox race and the 1 ms worker poll
# are exercised for real.
#
# NOTE: -cpu cortex-m33 is required (the QEMU 6.2 default CPU for this board
# does not start).  Output goes through the CMSDK UART0, captured to a file
# because QEMU's stdio serial is block-buffered and would be lost on timeout.
#
# Usage: ./run_qemu.sh
set -e
cd "$(dirname "$0")"

make clean
make

OUT=qemu_mbox_proxy.out
echo "=== Running under qemu-system-arm -M mps2-an521 -smp 2 -cpu cortex-m33 ==="
# -semihosting lets the firmware's SYS_EXIT (end of scenario) terminate QEMU
# cleanly; -serial file: captures the CMSDK UART0 output (stdio serial is
# block-buffered and would be lost on timeout).  "|| true" so a timeout does
# not abort the script before we print the captured output.
timeout 25 qemu-system-arm \
    -M mps2-an521 -smp 2 -cpu cortex-m33 \
    -kernel firmware.elf \
    -semihosting -display none -serial file:"$OUT" || true
echo "=== RESULT ==="
cat "$OUT"
