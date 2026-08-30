# BK7258 MCUboot CP/AP cross-slot rejection

Date: 2026-08-07

## Purpose

Prove that the board-owned BL2 does not combine a valid CP from one physical
slot with a valid AP from the other slot. Upstream NuttX MCUboot validates
multi-image IDs independently, so this is the required negative test for the
board flash-map wrapper.

## Temporary fault state

The known-good development package was left intact except for two temporary
RAM-generated test artifacts:

- primary AP at raw `0x165000`: one payload byte was changed and its 32+2 CRC
  stream was regenerated, so MCUboot signature validation rejects it;
- B pair at raw `0x286000`: the CP payload was changed in the same way while
  the B AP remained valid.

Thus the board had valid primary CP and valid secondary AP, but neither
complete CP/AP pair was valid. No bootloader, Manifest, LittleFS, OTP or eFuse
range was changed for this test.

## Board result

The temporary segments were written through COM7 with `bk_loader.exe`, then
the board was reset through COM7 RTS. Capture:

```text
/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260807-184001/
```

UART trace:

```text
B2INIT
B2GO
B2GORET
B2TRYB
B2BRET
B2BAD
```

The board repeatedly entered the watchdog-backed BL2 failure path and never
printed `B2GOOK`, `B2APOK`, `B2HANDOFF` or `NuttShell`. This is the expected
fail-closed result; a mixed CP-A/AP-B launch would have disproved the gate.

## Recovery

The original package's `app1_crc_flash.bin` and `s_app_mcuboot.bin` were
written back to the same two ranges. A subsequent COM7 RTS reset passed:

```text
/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260807-184058/
```

```text
B2INIT
B2GO
B2GORET
B2GOOK
B2APOK
B2HANDOFF
NuttShell (NSH)
```

Summary: `verdict=PASS_NSH`, `bl2_handoff=yes`, `nsh=yes`.

## Boundary

This proves the reversible board-owned same-slot gate. It does not prove the
vendor BootROM Manifest ABI, hardware Secure Boot lifecycle, or same-generation
metadata equality when both images in one slot are independently valid. Those
remain separate work.
