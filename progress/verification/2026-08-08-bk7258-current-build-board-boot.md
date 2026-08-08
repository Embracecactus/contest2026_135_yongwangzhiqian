# BK7258 current BL1/BL2/MCUboot build board boot

Date: 2026-08-08 (Asia/Shanghai)

Scope: reversible board boot verification of the full CP/AP `mcuboot` build.
This record does not prove BK7258 BootROM Secure Boot acceptance, OTP/eFuse
root binding, persistent rollback, or production readiness.

## Pre-flash observation

The existing board image was reset through the repository hardware-debug
toolkit using COM11 at 460800 baud and a 150 ms asserted RTS pulse on COM7.
The capture was bounded to 25 seconds and reached only repeated `B1PAGE`
markers. It did not reach BL2 or NuttShell. Hy3 delegation was unavailable due
to a rate-limit response, so the primary agent performed the bounded capture
and reviewed `session.json` and `serial.raw` directly.

## Image and write boundary

The package was built with CP `cp_nsh_mcuboot`, AP `ap_smp_mcuboot`, SDK
v3.1.1.9 and `MCUBOOT_VERSION=18.1.1`. The existing layout verifier accepted
layout `bk7258-v3119-ab-7f14d67587a17bf9` and reported `writes_enabled=false`.

Only these sparse ranges were written through `bk_loader` on COM7:

| Segment | Raw range | Artifact |
|---|---:|---|
| BL1 | `0x000000..0x011000` | `bl_crc.bin` |
| BL2 primary | `0x51d000..0x521000` | `bl2_crc.bin` |
| BL2 secondary | `0x53f000..0x543000` | `bl2_secondary_crc.bin` |
| Manifest A | `0x50b000..0x50c000` | `bl1-manifest-primary.bin` |
| Manifest B | `0x50c000..0x50d000` | `bl1-manifest-secondary.bin` |
| CP A | `0x011000..0x03a000` | `app_crc_flash.bin` |
| AP A | `0x165000..0x175000` | `app1_crc_flash.bin` |

The command did not include LittleFS (`0x600000..0x700000`), `usr_config`,
the B application slot, calibration tail, or OTP/eFuse.

`bk_loader` reported `Writing Flash OK` and `{All Finished Successfully}`
for all seven segments. Its process returned `1` after those success markers;
the project wrapper normalized that known tool behavior to success.

The loader log prints `Start 64K Erase` even for the 4 KiB Manifest entries, so
the physical erase granularity was checked instead of inferred from the sparse
arguments. Two independent 115200 readbacks of `0x4fb000..0x510000` were
byte-identical (84 KiB, SHA-256
`975124de9d46b154740e073b2e63fd886d496f7bf5027af2685ad15cf863913b`). The
readback retained the non-`0xff` N15 primary and mirror records, kept
`usr_config` erased as it was before the run, and left the unarmed policy
sector at all `0xff`. This is evidence for this board/run only; it does not
turn the loader's printed 64 KiB operation into a general 4 KiB erase
guarantee. The BKFIL read command's own reboot then left the board repeating
`B1PAGE` again even though the readback Manifest bytes matched the package;
the previously verified full sparse deployment restored `B2HANDOFF` and
NuttShell. Treat BKFIL read-triggered reset as a separate diagnostic side
effect until its reset/flash-controller behavior is understood.

## Negative-gate boundary

Before the negative test, a temporary primary Manifest sample was created with
exactly one flipped signature bit at offset `0x95`; it was never written. The
direct single-page download was rejected by the execution safety gate because
the loader may erase the surrounding 64 KiB block. No mutation occurred. A
normal full sparse deployment was then used to recover the board, and the
capture at `logs/bk7258-auto-debug/20260808-012727/` again reached
`B2HANDOFF -> NuttShell`.

The primary-to-secondary negative gate therefore remains open. Its safe form
requires a complete 64 KiB read-modify-write image that preserves every byte
except the selected Manifest signature bit, followed by explicit owner
approval for that physical erase block. It was not executed in this phase.

## Board result

The post-download capture was stored at:

`logs/bk7258-auto-debug/20260808-011503/`

Raw UART SHA-256:

`a0f90fb4cde91c91159bb537cecf471e5eddd12ad0f82047b59e757cf776e7a5`

Observed sequence:

```text
B1PAGE -> B2INIT -> B2GO -> B2GORET -> B2GOOK
-> B2SELA -> B2APOK -> B2HANDOFF -> NuttShell
```

No `HardFault`, `ASSERT`, `B1FAIL` or `B2BAD` marker appeared. The capture
also contained the existing application warnings `gpio: 0 is used` and
`[ipc_svr] create_socket failed`; therefore this record proves the boot-chain
handoff only, not complete runtime-service health.

## Conclusion

The current generated BL1/BL2/MCUboot artifacts are board-bootable through the
repository's recoverable, software-rooted chain after sparse deployment. The
earlier `B1PAGE` loop was resolved by deploying the complete matching set of
BL1, BL2, Manifest and CP/AP artifacts. The real BK7258 BootROM Manifest ABI,
hardware root, AES/CRC consumer and OTP/NV rollback remain open gates.
