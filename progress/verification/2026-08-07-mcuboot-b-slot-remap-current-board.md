# BK7258 MCUboot B-slot remap and paired handoff

Date: 2026-08-07

## Result

The latest BL1/BL2 package was written together with an intentionally invalid
primary CP region and the signed CP/AP pair in the secondary `s_app` region.
The test used the existing COM7 BKFIL sparse-download path; it did not write
LittleFS, calibration, user configuration, OTP/eFuse, or Secure-Boot control
state.

The COM11 capture at
`/tmp/bk7258-bslot-diagnostic-20260807-193841/serial.raw` reached:

```text
BL2RAM
B2INIT
B2GO
B2GORET
B2TRYB
B2BRET
B2GOOK
B2APOK
B2HANDOFF
NuttShell (NSH)
```

This is the first current-source board proof that the board-owned BL2 can
reject the A CP image, select the complete B CP/AP pair, validate the B AP
header and A-linked vector contract, enable the B-to-A XIP remap, and hand off
to CP.  The earlier `B2APBAD` capture was produced by the preceding BL2
binary, before the current A-window vector check was flashed; it is not a
failure of this artifact.

## Read-only hardware evidence

J-Link read-only inspection of the test image showed the B AP header and vector
at the expected decoded XIP addresses:

```text
B AP header  @ 0x023a0000: 96f3b83d ... ih_hdr_size=0x0200 ih_img_size=0x0000e258
B AP vector  @ 0x023a0200: MSP=0x280527cc Reset=0x02150559
A AP vector  @ 0x02150200: MSP=0x280527cc Reset=0x02150559
```

After the successful B attempt the remap registers read back as:

```text
0x44030058 = 0x02010000   remap begin
0x4403005c = 0x02260000   remap end
0x44030060 = 0x02250000   B-to-A decoded offset
0x44030064 = 0x00000001   remap enabled
```

The final restore used the normal project sparse path and returned
`verdict=PASS_NSH`, `last_checkpoint=B2HANDOFF`, and the A-pair trace
`B2GOOK -> B2APOK -> B2HANDOFF -> NuttShell`.  Restore capture:
`logs/bk7258-auto-debug/20260807-193956/`.

## Source change exercised

`bk7258_bl2_ap_vector()` now reports distinct diagnostic markers for malformed
header, invalid AP SRAM MSP, non-Thumb reset, and reset-range failures.  These
markers are fail-closed diagnostics only; the successful path emits the normal
`B2APOK` marker.  The AP vector check intentionally uses the A execution window
for both slots because the B physical pair is remapped through that window
before the CP reset vector is used.

The host packer now names the MCUboot-profile B artifact
`s_app_mcuboot.bin` and sets `secondary_pair.boot_selectable=true`; the legacy
non-MCUboot package still uses `s_app_seed.bin` and remains explicitly
non-selectable.  This is metadata and naming only: the encoded B bytes and all
partition offsets are unchanged.

## Boundary

This proves the recoverable, board-owned direct-XIP BL1 -> BL2 -> NuttX MCUboot
path and the reverse-engineered remap register contract.  It does not prove an
immutable BK7258 BootROM Manifest ABI, TrustEngine/OTP provisioning, official
Beken secure-boot signing, or N15/N17 metadata and anti-rollback writes.
