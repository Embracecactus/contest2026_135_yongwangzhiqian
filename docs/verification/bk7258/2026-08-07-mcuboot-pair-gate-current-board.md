# BK7258 MCUboot CP/AP pair-gate current build

Date: 2026-08-07

## Result

The board-owned BL2 pair gate was rebuilt and the exact resulting BL1/BL2/
CP/AP package was flashed through the bounded sparse path. COM7 reported
successful writes for boot, BL2, CP and AP; LittleFS and reserved ranges were
not part of the write list. COM11 reached:

```text
BL2RAM
B2INIT
B2GO
B2GORET
B2GOOK
B2APOK
B2HANDOFF
NuttShell (NSH)
```

Capture directory:

```text
/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260807-180147/
```

The capture summary is `verdict=PASS_NSH`, `bl2_handoff=yes`, and `nsh=yes`.
This is the normal A-pair path. The earlier A-invalid → B-remap board proof
is recorded separately in `2026-08-07-bl1-bl2-mcuboot-ab-fallback-board.md`.
The cross-slot-only rejection proof for the same gate is recorded in
`2026-08-07-mcuboot-cp-ap-cross-slot-reject-board.md`.

## Current pair-gate behavior

Upstream NuttX MCUboot is left untouched. The board flash-map adapter can hide
one physical slot by returning an erased view for that slot. BL2 calls
`boot_go()` once with A visible and once with B visible if needed, and accepts
the response only when the returned CP offset is the same slot that was made
visible. The AP vector is checked again before the final CP handoff.

The read adapter now checks `off/len` against the selected flash area before
applying the hidden-slot view. This is a board ABI hardening change and does
not change the official SDK or NuttX source.

## Rebuilt artifact evidence

```text
BL2 raw:        9464 bytes
BL2 CRC:        13056 bytes
BL2 CRC SHA256: fe4bf9b14f2b81bd1e0d39434f25c41e623ab5f70b8f58b1609c8a5cd4ec80bd
BL1 CRC SHA256: 931bcc4bbb2ce5958c1193442ba0f4debe9210f23e9dc24a0ef2e81240795fed
CP A CRC SHA256: 9a0dbe8950bc062b0d4890f2d980127cdfcd320d29a225251c790471f0aa0e4f
AP A CRC SHA256: 1cc203a1f25348a99e5c81b0699d0b56eb8e83dd5c8e62ae9d47c5e0c5f6d1c7
```

The build used the checksum-verified v3.1.1.9 SDK wrapper bundle and the
pinned NuttX MCUboot `imgtool.py`; development keys stayed outside the
repository. No OTP/eFuse, Secure-Boot lifecycle bit, or NuttX/SDK source was
modified.

## Boundary

This proves the current recoverable software BL1-proxy → board BL2 → MCUboot
handoff. It is not evidence that the candidate Manifest is the immutable
BK7258 BootROM ABI, and it does not arm OTA metadata, anti-rollback storage,
OTP/eFuse provisioning, or official secure-boot lifecycle state.
