# BK7258 MCUboot CP/AP same-generation rejection

Date: 2026-08-07

## Purpose

Prove that the board-owned BL2 does not launch a CP/AP pair whose members are
individually signed but belong to different MCUboot generations. The pair
binding is deliberately implemented outside the upstream NuttX source:

- `ih_ver` major, minor, revision and build fields must be identical;
- if `IMAGE_TLV_SEC_CNT` is present, it must be present in both images and the
  protected counter values must be identical;
- a legacy pair with no counter is accepted only when both members omit it.

The packer records this rule in `mcuboot_pair.json`; BL2 enforces it after
`boot_go()` has independently authenticated the visible CP/AP slot and before
the CP handoff.

## Host artifacts

The normal package was rebuilt with the pinned NuttX MCUboot `imgtool.py`, the
checksum-verified official SDK v3.1.1.9 bundle, and development keys kept under
`/tmp`:

```text
BL2 raw:              9804 bytes
BL2 logical/CRC:      0x3000 / 0x3300 bytes
BL2 package envelope: 0x4000 bytes (erase-sector padded)
BL2 raw SHA256:       609838fa88e9a161b3601416d6cc40d7f48d7836e877a08c392a46f19f23bdf8
BL2 CRC SHA256:       a43bb3026a9a378f44af13f504611a791ff9281525906a225d453e317d0eb4bd (0x3300 bytes)
BL2 package SHA256:   58e97fcb0edaebd7f302be7a21f877931aab1ef95a60d7c155cbc995fc1bee31 (0x4000 bytes)
```

The negative AP was generated from the same raw AP payload and the same EC256
development key, but with version `19.1.1`; the normal CP remained `18.1.1`.
Both signatures and CRC streams were valid. The protected counters therefore
also differed (`0x12010001` versus `0x13010001`). The temporary files were:

```text
/tmp/bk7258-generation-mismatch/ap_mismatch_flash.bin
/tmp/bk7258-generation-mismatch/b_cp_erased_4k.bin
```

The B CP header was erased for this test so that a rejected A pair could not
fall through to a valid B pair. Only boot, BL2, primary CP/AP, and the first
4 KiB of B CP were written; LittleFS, metadata, calibration, and reserved
ranges were untouched.

## Board result

The temporary segments were written through COM7 and the board was reset with
COM7 RTS. Capture:

```text
/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260807-190736/
```

The raw COM11 trace contains the decisive sequence:

```text
B2INIT
B2GO
B2GENBAD
B2GORET
B2TRYB
B2BRET
B2BAD
```

There was no `B2GOOK`, `B2APOK`, `B2HANDOFF`, or `NuttShell`. The new
`B2GENBAD` marker is now included in the WSL2 capture SOP checkpoint order;
the saved capture was produced just before that presentation-only checkpoint
list update, so its raw trace is the canonical evidence.

This proves the intended order: each visible image was first passed through
MCUboot's normal authentication path, then the board-owned generation binding
rejected the mismatched CP/AP pair, and finally the empty fallback failed
closed.

## Recovery

The complete rebuilt package was restored through the bounded sparse SOP:

```text
/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260807-190927/
```

The loader reported successful writes for boot, both BL2 copies, CP and AP.
COM11 reached:

```text
B2INIT
B2GO
B2GORET
B2GOOK
B2APOK
B2HANDOFF
NuttShell (NSH)
```

The board is left on this known-good package. No OTP/eFuse, Secure-Boot
lifecycle, JTAG/SWD lock, official SDK source, or NuttX source was modified.

## Boundary

This is a reversible board-owned CP/AP release-generation binding. It is not
evidence of a vendor BK7258 BootROM Manifest ABI, hardware-backed rollback
counter, or production Secure Boot provisioning.
