# Verification: N15 J-Link implicit-reset failure and noreset recovery

- Date: 2026-08-04 (Asia/Shanghai)
- Board: T5-AI, COM11 console, COM7 RTS reset, J-Link SWD
- SDK: official Beken v3.1.1.9 only
- Scope: volatile upper-PSRAM transfer; no Boot/CP/AP/LittleFS erase or
  download

## Failed v2 attempt

The first reset-only runner attempt is retained under
`logs/bk7258-n15/20260804-n15v-reset-only-42-56/`. It passed the erased-A
preflight and `bkota prepare-transfer 42`, then stopped at step 5 before any
candidate validation, staging or publication.

J-Link reported:

```text
'loadbin': Performing implicit reset & halt of MCU.
Verify failed @ address 0x60800000.
```

The reset destroyed the initialized volatile PSRAM context, so the first
candidate verify failed. The runner returned nonzero and did not advance.
Generation 42 never reached B Flash or metadata.

COM7 RTS recovery and read-only status were delegated to `ask_hy3` and then
independently hash/field checked:

- recovery raw: `1530f249c67d770c3dfd3d7ca75b58a00128642b546450582351a7b716d39ec1`;
- recovery status raw:
  `59214b564a6662770f3d23dcf1cd33402d9e0391ac756d6d4e9d62d39a12a929`;
- target returned to `state=0`, `generation=0`, `trusted=0`, `erased=1`,
  `secondary=0`, both runtime write gates 0 and `watchdog_active=1`.

The v2 execution journal is closed evidence and must not be resumed.

## Corrected transport and bounded board probe

SEGGER documents that file download resets by default and that the explicit
`noreset` argument suppresses it. The loader now uses exactly three
`loadfile ... noreset` writes followed by three independent `verifybin`
operations. The transfer verifier also has a read-only `--check-only` mode so
loader dry-runs cannot rewrite a frozen package.

A bounded 384-byte descriptor probe at `0x60a75000` passed on hardware. Its
J-Link transcript contains:

```text
'loadfile': Skipping reset & halt of MCU before download.
Reading 384 bytes data from target memory @ 0x60A75000.
Verify successful.
```

Evidence:

- J-Link transcript SHA-256:
  `8562168888828e48433ac6ec41955250fbe365ece5e2bd696e9cabb9cd8deae4`;
- continuity status raw SHA-256:
  `cda77a1810dd5a88986f9946c228783122c97cd6d5a5ebdcf5afaa643a48185f`;
- post-probe target still reported `watchdog_active=0`, proving that the
  prepared runtime was not reset; metadata remained erased and both write
  gates remained closed.

The probe was then discarded by COM7 RTS. Final recovery raw SHA-256 was
`575aca5b33662140339f1239f6faa252b281bfbf98e6cac9333244e130e25c50`;
final read-only status raw SHA-256 was
`59214b564a6662770f3d23dcf1cd33402d9e0391ac756d6d4e9d62d39a12a929`.
The board again matched the exact erased-A/watchdog-active start state.

## Replacement frozen campaign

A separate `bk7258-n15v-campaign-v3` was generated; v2 was not edited into a
passing result.

- manifest SHA-256:
  `9a6e2589e928c3197c7d2a02c02642320ba8963e1a3b295aa7463792b95a1748`;
- independent campaign report SHA-256:
  `3aa1d7d44855c16aa8cc11047ace7b8690e4fd4be6177b2afc08d2d765e412a8`;
- validation source/ELF report SHA-256:
  `59aed0ab6d95d4c0f10a6dd19ec2068f7418dbb208433ffe3dbaf2e9d962b7ca`;
- 15 unique generations and all 15 read-only loader dry-runs pass;
- the old v2 package is rejected by the new reset-safety verifier.

Reusing generation 42 in the new campaign is safe because both before and
after evidence prove durable metadata generation 0 and no B staging command
ever ran. This is an explicit reviewed restart from a clean state, not an
automatic resume.

## First v3 observer attempt

The first v3 runner attempt is retained under
`logs/bk7258-n15/20260804-n15v-v3-reset-only-42-56/`. All three full PSRAM
writes and verifies passed with no reset; its J-Link transcript SHA-256 is
`7267fc2aae6990f8a28e1764f853c5384cc9048d9f2ae5a8266b70f7461f50c8`.
The bounded corruption command then passed and target validation correctly
rejected the candidate:

```text
BKOTA VALIDATE-MEM ret=-74 phase=1 generation=0 ...
  sectors=0 programmed=0 readback=0
```

The runner stopped only because its initial regex incorrectly required a
validated generation 42 on this pre-identity error path. The rule now requires
the exact `-EBADMSG`, phase 1, generation 0 and zero-mutation counters. No
stage command ran. Recovery again produced the byte-identical erased-A status
raw SHA-256
`59214b564a6662770f3d23dcf1cd33402d9e0391ac756d6d4e9d62d39a12a929`;
the reset raw SHA-256 is
`1530f249c67d770c3dfd3d7ca75b58a00128642b546450582351a7b716d39ec1`.
This attempt is also closed rather than resumed.

## Remaining boundary

This result proves attached-debug continuity and hardware reset recovery. It
does not prove complete VDD removal; USB still powers the target. The manual
cold-power gate remains open.
