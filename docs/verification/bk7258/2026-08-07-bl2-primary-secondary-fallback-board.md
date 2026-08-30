# BK7258 primary BL2 corruption → secondary BL2 recovery

Date: 2026-08-07

## Method

The known-good secondary BL2 and both Manifest records remained on the board.
Only a temporary CRC-valid primary BL2 was written to raw `0x51d000`. Its
logical code byte at offset `0x200` was changed and the official board-local
32+2 CRC expansion was regenerated. This keeps the flash decoder valid while
making the primary Manifest digest reject the image. No OTP/eFuse or NuttX/
SDK source was changed.

## UART result

Capture:

```text
/tmp/bk7258-bl2-fallback2.txt
```

Observed trace:

```text
u_bootloader enter
B1PRIMARY
partition bl2 @ 0x024D0000
bl1 manifest rc 0x00000002
B1PRIMARY BAD
B1SECONDARY
partition bl2 @ 0x024F0000
bl2 ram @ 0x28020000
BL2RAM
B2INIT
B2GO
B2GORET
B2GOOK
B2APOK
B2HANDOFF
NuttShell (NSH)
```

This proves the new BL1 failure branch reaches the secondary XIP slot and the
same NuttX MCUboot CP/AP handoff. The `0x02` return is the expected board-owned
Manifest image-digest rejection.

## Recovery

The valid primary `bl2_crc.bin` was written back to `0x51d000`. A subsequent
COM7 RTS reset reached `B1PRIMARY -> BL2RAM -> B2GOOK -> B2APOK -> B2HANDOFF ->
NuttShell`; capture directory:

```text
/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260807-182808/
```

The summary reports `cold_path=yes`, `bl2_handoff=yes` and `nsh=yes`.

## Boundary

This validates the board-owned recoverable fallback, not the unpublished
immutable BK7258 BootROM Manifest ABI or OTP-backed Secure Boot lifecycle.
