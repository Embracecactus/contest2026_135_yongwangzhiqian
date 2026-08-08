# BK7258 MCUboot rejects a signed B pair with an invalid AP vector

Date: 2026-08-07

## Result

This is a negative test of the board-owned BL2 vector gate.  The B pair was
signed with the same development EC256 key, version `18.1.1`, and security
counter as the valid CP image.  Only the AP input's reset vector was changed
before signing: it was made even (`0x02150558`) so the resulting MCUboot image
remained cryptographically valid but was not a valid Cortex-M Thumb entry.

The A CP region was intentionally erased/invalid so that BL2 had to inspect B.
The COM11 capture at `/tmp/bk7258-bad-ap-test/serial.raw` was:

```text
BL2RAM
B2INIT
B2GO
B2GORET
B2TRYB
B2BRET
B2GOOK
B2APTHUMB
B2APBAD
B2BAD
```

No `B2HANDOFF` or `NuttShell` appeared.  This demonstrates that MCUboot's
cryptographic validation alone is not treated as a sufficient AP launch
condition: the board-owned BL2 still enforces the SRAM/Thumb/XIP vector ABI
and fails closed.

## Recovery

The normal package was immediately restored through the existing COM7 sparse
path.  The restore capture is
`logs/bk7258-auto-debug/20260807-195220/` and reports:

```text
verdict=PASS_NSH
last_checkpoint=B2HANDOFF
checkpoints=B2INIT B2GO B2GORET B2GOOK B2APOK B2HANDOFF
```

LittleFS and reserved ranges were not written.  No OTP/eFuse or lifecycle
control was touched.

## Boundary

This proves the recoverable BL2 fail-closed behavior for a signed-but-invalid
AP vector.  It does not prove the immutable BK7258 BootROM Manifest ABI,
TrustEngine authorization, or any irreversible secure-boot provisioning.
