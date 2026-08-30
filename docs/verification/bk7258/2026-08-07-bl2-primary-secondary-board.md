# BK7258 primary/secondary BL2 package normal-A board run

Date: 2026-08-07

## Download

The exact package produced by the primary/secondary host build was flashed
through the existing WSL2 path:

```text
COM7: BKFIL/bk_loader sparse download
COM11: 460800 baud capture
```

The loader wrote all five requested ranges successfully:

```text
bl_crc.bin              @ 0x000000-0x11000
bl2_crc.bin             @ 0x51d000-0x4000
bl2_secondary_crc.bin   @ 0x53f000-0x4000
app_crc_flash.bin       @ 0x011000-0x29000
app1_crc_flash.bin      @ 0x165000-0x10000
```

LittleFS, `usr_config`, calibration tail and other reserved ranges were not
written.

## UART result

Capture directory:

```text
/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260807-182331/
```

The boot trace is:

```text
u_bootloader enter
B1PRIMARY
partition bl2 @ 0x024D0000
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

The run reached the primary candidate and then the normal NuttX MCUboot
CP/AP handoff. The capture summary reports `verdict=PASS_NSH` and
`bl2_handoff=yes`.

## Boundary

This is the normal-A regression for the new layout. It does not yet prove
primary corruption recovery; that negative test is a separate, bounded
follow-up and must restore the known-good package afterward.
