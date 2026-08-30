# MCUboot version-aware CP/AP selection: board proof

Date: 2026-08-07 21:06 +08:00  
Board: BK7258 T5-AI; runtime SDK v3.1.1.9; no NuttX/SDK source changes.

## Baseline after flashing the current BL1/BL2

The bounded sparse package update completed through COM7.  It wrote BL1,
both BL2 envelopes, and the A CP/AP regions while preserving LittleFS.  The
COM11 capture in `logs/bk7258-auto-debug/20260807-210308` reported:

```text
B2INIT B2GO B2GORET B2GOOK B2SELA B2APOK B2HANDOFF
```

and `verdict=PASS_NSH`.  `B2SELA` is the expected tie-break when the A and B
images have the same `18.1.1` version.

## Newer B candidate

A host-only pair was generated with the same development signing key and
version `19.1.1`:

```text
/tmp/bk7258-mcuboot-newer/s_app_mcuboot.bin
size   2576384 (0x275000)
sha256 8018c976f8248714b58ef0115b26c662c81c40f5d56b0bb3f022cc111b7b672e
```

The pair metadata records matching CP/AP `ih_ver` and protected-counter
policy.  BKFIL wrote only this file to the B range `0x286000..0x4fb000`;
the loader log is the `2026-08-07 21:04:32` entry in the local BKFIL log.

After a COM7 RTS reset, the board reached:

```text
B2INIT B2GO B2GORET B2GOOK B2SELB B2APOK B2HANDOFF
```

with `verdict=PASS_NSH`.  This is the first hardware proof that BL2 exposes
both slots to MCUboot, accepts the newer valid CP/AP pair, and performs the
secondary-pair handoff.  Capture: `logs/bk7258-auto-debug/20260807-210445`.

## Restoration

The original B package was written back to the same B-only range:

```text
/home/lijian/project/open-vela/nuttx/bk7258-dual/s_app_mcuboot.bin
size   2576384 (0x275000)
sha256 5370fd73930608f24b72b9c10744cab383a2d06be688a50f129825516525f0a3
```

The loader reported `{All Finished Successfully}`.  A final RTS reset
reported `B2SELA B2APOK B2HANDOFF` and `PASS_NSH` in
`logs/bk7258-auto-debug/20260807-210620`, confirming that the development B
candidate was removed and the board is back on the known-good pair.

No OTP/eFuse, policy sector, BL1, BL2 or LittleFS data was modified during the
B-only experiment.  The version-aware selection gate is now hardware-verified
for the valid newer-B path; the mismatched-generation negative is recorded
below, while an independently malformed AP vector remains separate.

## Negative: valid signatures with mismatched B generation

To exercise the both-visible path, a temporary B image was assembled from a
signed `19.1.1` CP member and the signed `18.1.1` AP member.  The pair was
written only to the B range and had SHA-256:

```text
/tmp/bk7258-mcuboot-mixed-b.bin
size   2576384 (0x275000)
sha256 608d97e707c60264f41b586bf6f8d25b1420fe3a74c10960aec9d9e7d391aabb
```

The COM11 capture in `logs/bk7258-auto-debug/20260807-211636` reported:

```text
B2INIT B2GO B2GENBAD B2GORET B2TRYA B2ARET B2GOOK B2SELA B2APOK B2HANDOFF
```

with `verdict=PASS_NSH`.  Thus a newer but non-atomic CP/AP pair is rejected
before handoff and the valid A pair is selected.  The original `18.1.1` B
package was then restored and the final capture
`logs/bk7258-auto-debug/20260807-211806` again reported
`B2SELA B2APOK B2HANDOFF` and `PASS_NSH`.

This closes the version-aware selection and same-generation fallback gate.

## Negative: signed AP image with a non-Thumb reset vector

To exercise the AP launch-vector gate independently of signature validity, a
temporary B package was based on the valid `19.1.1` pair. Its AP image was
re-signed with the same development key after changing only the reset-vector
word from `0x02150559` to the even address `0x02150558`; the resulting image
therefore has a valid MCUboot signature and CRC stream but cannot be entered
as a Cortex-M Thumb image. The temporary package was:

```text
/tmp/bk7258-mcuboot-invalid-ap/s_app_mcuboot-invalid-ap.bin
size   2576384 (0x275000)
sha256 09fff79e7bf3fe38b827f3c998c4071b77624f523ae9c7ceccd813d97e87eef5
```

BKFIL wrote only the B range `0x286000..0x4fb000`. The COM11 capture in
`logs/bk7258-auto-debug/20260807-212527` reported:

```text
B2INIT B2GO B2APTHUMB B2GORET B2TRYA B2ARET B2GOOK B2SELA B2APOK B2HANDOFF
```

with `verdict=PASS_NSH`. The `B2APTHUMB` marker is emitted while the invalid
candidate is checked; `B2GORET` and the isolated-A retry show that BL2 did not
handoff that candidate. The known-good A pair then completed the handoff.

The original `18.1.1` B package was immediately written back to the same
range. The final RTS capture in `logs/bk7258-auto-debug/20260807-212700`
reported `B2SELA B2APOK B2HANDOFF` and `PASS_NSH`. No BL1, BL2, LittleFS,
OTP/eFuse or policy-sector bytes were changed by this negative test.

This closes the malformed-AP-vector fallback gate for the current BL2 path.
