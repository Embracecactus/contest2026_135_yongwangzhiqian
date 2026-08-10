# BK7258 CAN controller-internal loopback

Date: 2026-08-10 GMT+8

## Scope

- Hardware: T5-Board, without an external CAN transceiver or peer node.
- Runtime: AP SMP image linked only against the official BK7258 SDK
  v3.1.1.9 static-library interface.
- NuttX and official SDK source trees were not modified.
- Driver implementation: local commit `15b1e39`; its public Kconfig,
  Make.defs and bring-up bindings were still uncommitted at this checkpoint.

The board's CAN0 pins GPIO44/TX, GPIO45/RX and GPIO46/standby conflict with
the RGB LCD and SPI0 groups.  Validation therefore used a temporary AP
configuration with LCD and SPI disabled and `CONFIG_CAN_LOOPBACK=y`.  The
temporary configuration and probe were removed after the run.

## Exercised path

```text
open/write/read /dev/can0
        -> NuttX CAN upper half
        -> BK7258 CAN lower half
        -> v3.1.1.9 SDK CAN transmit API
        -> controller internal LBMI loopback
        -> SDK IRQ callback
        -> semaphore + AP RX kthread
        -> SDK CAN receive API
        -> can_receive()
        -> userspace frame comparison
```

The probe used the standard NuttX character-CAN ABI, enabled and read back
the connection mode with `CANIOC_SET_CONNMODES` and
`CANIOC_GET_CONNMODES`, transmitted an 8-byte classic 11-bit CAN data frame,
then verified its identifier, DLC and payload after reception.

## Result

The signed MCUboot CP/AP build used version `18.2.0` and security counter
`21`.  Signing keys were temporary external development keys and no private
key material was stored in the repository or this record.

The runtime evidence is in
`logs/bk7258-auto-debug/20260810-204838/serial.txt`:

```text
B2GORET
B2GOOK
B2SELA
B2APOK
B2HANDOFF
[CPU0] BK7258 CAN LOOP PASS id=325 dlc=8 retries=1
NuttShell (NSH)
nsh>
```

After removing the probe and restoring the normal LCD/SPI profile, the clean
signed image was rebuilt and sparse-flashed.  It completed the BL2 handoff
and reached NuttShell with verdict `PASS_NSH` in
`logs/bk7258-auto-debug/20260810-205543/serial.txt`; that log contains no CAN
probe output.

## Proof boundary

This verifies the controller-level software path, internal loopback, IRQ
callbacks, deferred receive processing and RPMsg-carried AP diagnostics.  It
does not verify an external transceiver, CAN pin electrical behavior, bus
termination, another node's ACK, arbitration, bitrate interoperability or
external-bus error and bus-off recovery.  Those require a transceiver and a
peer node and must not be inferred from this result.
