# Current Progress

Last updated: 2026-08-08 16:52 GMT+8
Updated by: Codex (`maintain-project-memory` checkpoint)

## Active scope

The active objective is a recoverable BK7258 chain:

```text
legacy BootROM -> board-owned minimal BL1 -> signed Manifest
-> pinned NuttX MCUboot BL2 -> signed same-slot CP/AP pair -> NuttShell
```

Official BK7258 v3.1.1.9 has no buildable Secure Boot adaptation. BK7236
security material is used only as a same-architecture semantic/source
reference; its single-core addresses, OTP/eFuse ABI and TF-M mapping are not
treated as BK7258 facts. NuttX and SDK source trees remain unchanged.

## Current board baseline

- MCUboot version: `18.1.3`; protected security counter: `20`.
- BL1 profile: `BL1_MINIMAL=1`, fixed Primary -> Secondary BL2 ordering.
- BL1 responsibilities: clock/reset normalization, watchdog fail-closed,
  Manifest P-256/SHA-256 verification, BL2 vector/copy validation, checked
  SRAM policy publication and BL2 handoff.
- The final BL1 does not link N15/N17 lifecycle selectors, OTA Flash writer,
  N17 release keys or NuttX ECC. Historical validation profiles remain
  separate and are not part of the MCUboot image.
- BL2 remains the only component that validates and launches a signed CP/AP
  pair. It uses the pinned NuttX MCUboot sources and board-owned Flash/AP
  handoff adapters.
- Final BL1 ELF: `.text + .rodata = 9,878` bytes, `.data = 0`, `.bss = 0`.

Artifact SHA-256:

- `bl_crc.bin`: `b13e9946d0120a170836bef0bf97c2de953ae78db71016c8c6ae8ba9412a49ea`
- `all-app-factory.bin`: `bb80db82a1631112602902069d691a65a99710f74d7f2ac9d537cae796009cbe`
- `bl2_crc.bin`: `535571b677f0ced7d2c8a49b2495fbc0b2778657dfab50cb732c56a106204f17`

## Verification

- Full `JOBS=32` CP/AP MCUboot build passed using immutable SDK v3.1.1.9.
  The build now runs the profile-aware BL1 symbol verifier.
- Host mailbox/BL1-policy tests passed: `0/31` failures.
- Valid factory package reached
  `B1PRIMARY -> BL2RAM -> B2GOOK -> B2SELA -> B2APOK -> B2HANDOFF -> NSH`:
  `logs/bk7258-secureboot-minimal-primary/20260808-164835`.
- Corrupting byte `0x40` of only the Primary Manifest digest, then rebuilding
  its valid 32+2 CRC envelope, produced
  `rc=2 -> B1PRIMARY BAD -> B1SECONDARY -> B2HANDOFF -> NSH`:
  `logs/bk7258-secureboot-minimal-negative/20260808-165028`.
- The valid boot envelope was restored and passed:
  `logs/bk7258-secureboot-minimal-restored/20260808-165102`.
- Independent 150 ms COM7 RTS physical reset passed the Primary path with
  `cold_path=yes`:
  `logs/bk7258-secureboot-minimal-rts/20260808-165125`.
- The board is currently restored to the valid Primary image. No OTP/eFuse,
  secure lifecycle or debug-lock bit was written.

Canonical detail:
[Secure Boot remaining-gates verification](verification/2026-08-08-bk7258-secureboot-remaining-gates.md).

## Honest boundary

This proves a repository-owned, software-rooted Secure Boot chain on BK7258.
It does not prove that BK7258 BootROM consumes the candidate Manifest, and it
does not provide an immutable hardware root or persistent hardware-backed
anti-rollback. The board remains recoverable for unfinished driver work.

## Next step

1. Review the published Secure Boot commits and merge them through a PR.
   Temporary private keys, generated images and raw hardware logs remain
   outside the commits.
2. Resume ordinary driver/N17 work on this recoverable baseline. Reintroduce
   OTA slot policy only through a separately authenticated, fail-closed
   interface; do not put historical N15/N17 writers back into minimal BL1.
3. Hardware Secure Boot provisioning is the final gate, after signed OTA and
   recovery matrices are stable and preferably on a second board. It requires
   separate authorization before any OTP/eFuse or lifecycle operation.

## Open constraints

- Official runtime SDK is fixed to v3.1.1.9; BK7259/v4 artifacts are excluded.
- Do not modify NuttX or SDK sources except temporary debugging that is fully
  restored.
- Private signing keys must never enter the repository, firmware logs or
  project memory.
- Existing runtime warnings `gpio: 0 is used` and
  `[ipc_svr] create_socket failed` are outside this boot-chain verification.
