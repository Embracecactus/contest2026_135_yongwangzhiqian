# BK7258 final four-role isolated compile-only verification

> **Superseded/current-state note:** This is a phase-local record from
> 2026-08-16. Subsequent work completed the 27-to-3 profile cutover, the
> four-role compile contract, and the postbuild command alias. The
> `NOT_RUN`/compile-only boundaries below remain the facts of this captured
> run; do not read them as the current project state. See the authoritative
> [current progress](../CURRENT.md).

- Date: 2026-08-16 (Asia/Shanghai)
- Base source head: `54ff505912baf4c23e2515ffa60e6c8df18933b5`
- Branch: `feat/bk7258-partition-layout-identity`
- Product: `t5_board_bringup` (`t5_board`, MCUboot)
- Evidence root: `/tmp/bk7258-boot-phase-accept-PFiC5g`
- Phase: `runtime-built`; execution mode: `compile-runtime`
- Manifest: `execution.json`; identity
  `ce21a8ec9b4513baf33048f018e14db7295135624b1d39e95dc60817d742b0ef`;
  SHA-256 `68cc9e93f4e7b6a04e4a2df2dc7838e95f38b6940ca66c4f23d9ca16bf2fef09`
- Snapshot identity: `d063bd3b632c64241fde8a828e854aed7e581f286e7461d41a4c88b40c2eb25a`
- Source manifest SHA-256:
  `6a7d99ca8a676296b0c83ee08e3ab2049f7dbe58babae6a861ab26b05bd59704`
- Post-command source-snapshot tree gate: 144,039 filesystem entries,
  unchanged.

## Result

The final isolated build prepared and compiled all four required roles from
one materialized read-only entity snapshot. Every command returned zero and
reported `PASS`:

| Role | Backend | Commands | Result |
|---|---|---:|---|
| BL1 | bootloader adapter | 2 | 2/2 PASS |
| BL2 | minimal Make | 2 | 2/2 PASS |
| CP | CMake | 4 | 4/4 PASS |
| AP | CMake | 4 | 4/4 PASS |

The role-private build roots and artifact records were emitted for all four
roles. BL1/BL2 boot artifacts are explicitly marked `runnable=false` and
`trusted=false`; this is compile-only evidence, not a boot or trust result.

## Policy and boundaries

- Boot policy: `status=RECONCILED`, `execution=COMPILE_ONLY`.
- Compile side effect: `PASS`.
- `sign`, `package`, `hardware`, `network` and `private_key_read`:
  `NOT_RUN`.
- No final postbuild, signed package, Flash or hardware result is claimed.
- Final focused acceptance reported 50 tests `PASS`; `git diff --check`
  also reported `PASS`.

This record establishes the four-role isolated compile-only baseline. The next
implementation phase is isolated postbuild; signing and package delivery come
after that and signing requires separate authorization. P9b, validation
migration and hardware verification remain incomplete.
