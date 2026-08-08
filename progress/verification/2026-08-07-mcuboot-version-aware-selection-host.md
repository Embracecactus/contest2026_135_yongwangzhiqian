# MCUboot version-aware CP/AP selection: host proof and hardware gate

Date: 2026-08-07 20:35 +08:00  
Scope: board-owned BK7258 BL2 selection logic; no NuttX/SDK source changes.

Status: the temporary hardware blocker recorded here was cleared later; the
board result is canonical in
`2026-08-07-mcuboot-version-aware-selection-board.md`.

## Implemented

`bk7258_bl2_main.c` now gives upstream `boot_go()` visibility of both A/B
slots first.  This preserves MCUboot's normal version ordering.  The board
gate then requires the returned CP candidate to have a same-slot AP with the
same image version and security-counter state, and rejects an authenticated
but unusable AP vector before handoff.  If that candidate is not launchable,
BL2 retries A and B in isolation, preserving the CP/AP atomic-pair contract.

The BL2 trace now includes `B2SELA` or `B2SELB`.  The WSL2 capture summary was
updated to report the retry and selection markers instead of silently omitting
them.

## Host evidence

- The board-owned BL2 rebuild completed from the pinned NuttX MCUboot sources;
  the current ELF contains `B2TRYA`, `B2TRYB`, `B2SELA` and `B2SELB`.
- The paired `cp_nsh_mcuboot`/`ap_smp_mcuboot` build completed with SDK
  v3.1.1.9 checksum verification.  The package verifier passed:
  `PASS bk7258-factory-layout: ... B=mcuboot-selectable ...`.
- A separate host-only B candidate was generated with the same EC256 key at
  version `19.1.1` (the board package remains at `18.1.1`).  Its CP/AP headers
  have MCUboot magic `0x96f3b83d` and version `19.1.1+0`.  It is staged under
  `/tmp/bk7258-mcuboot-newer` and has not been flashed.

## Hardware gate (not yet passed)

The latest package was **not** written.  BKFIL failed before `Gotten Bus` at
the LinkCheck/GetBus stage in captures `20260807-201209`, `20260807-201844`,
`20260807-202300`, and the later direct read probes.  Adding the local
BKFIL `--hard-reset`, `--stay-in-rom`, longer `--getbus-timeout`, retries, an
RTS+DTR pulse, and a J-Link default reset did not change that result.  These
attempts therefore provide no new BL2 execution evidence and did not reach a
flash write operation.

Read-only J-Link inspection still shows the board's old BL2 vector handler
(`0x280202c9` at XIP `0x024d0000`), while the current source build has a
different handler address.  A 60-second COM7 RTS capture after the failures
still reached the known-good old path:

```text
B2INIT B2GO B2GORET B2GOOK B2APOK B2HANDOFF
```

Thus the existing board image is recoverable and stable; the version-aware
selection change is host-verified but not hardware-verified yet.

The owner then reconnected the J-Link 3V3 lead.  A fresh RTS capture still
contained zero bytes, and J-Link reported `RESET (pin 15) high` while failing
to connect.  This is now a physical VTref/GND/SWD/RST or USB-power recovery
gate; no further firmware download is justified until `BClk` returns.

## Next hardware action

After a manual USB/board reconnect restores BKFIL's GetBus handshake, run the
existing sparse download once.  Then flash the staged B-only `19.1.1` pair
and expect `B2SELB` followed by `B2HANDOFF`; immediately restore the known-good
`18.1.1` package.  Do not interpret a capture without `B2SELB` as proof of
version-aware B selection.
