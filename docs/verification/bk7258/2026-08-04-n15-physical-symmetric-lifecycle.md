# N15 physical symmetric A/B lifecycle verification

- Status: `board-verified; minimal physical symmetric lifecycle complete`
- Date: 2026-08-04 (Asia/Shanghai)
- Branch baseline: `feat/bk7258-n15-ota` at `68b9436`
- SDK: official Beken v3.1.1.9 only
- Board path: T5-AI, COM11 console at 460800 8N1, COM7 RTS reset,
  J-Link SWD
- Architecture: `memory/decisions/ADR-006-n15-symmetric-dual-bank-ota.md`

## Result

The minimal symmetric physical lifecycle passed in both directions:

```text
confirmed A
    -> stage generation 314 into B
    -> publish bank 0 -> trial B -> regressions -> confirmed B
    -> stage generation 315 into A
    -> publish bank 1 -> trial A -> regressions -> confirmed A
    -> COM7 RTS reset -> confirmed A
    -> remove board USB and J-Link power -> reconnect -> confirmed A
```

Both inactive-pair writes covered exactly `2576384` bytes and passed complete
Flash read-back plus SHA-256 comparison. Both metadata publications verified
the base and candidate pair before mutating one inactive 4 KiB metadata bank.
The final observed state was generation 315, bank 1, confirmed A, active A,
secondary mapping disabled, with both CP runtime write gates closed.

This closes the physical A-to-B-to-A, dual-bank publication, trial, health
confirmation, hardware-reset and post-confirm complete-power-removal gates.
After the owner removed both board USB and J-Link power and reconnected them,
a capture-only COM11 session read back the same generation-315 confirmed-A
record. No reset, J-Link Commander or Flash command ran between reconnect and
the acceptance read.

## Frozen board inputs

The physical run used frozen packages outside the mutable build output:

| Generation | Target | Artifact | SHA-256 |
|---:|:---:|---|---|
| 314 | B | `s_app-candidate.bin` | `7fc083a258ee3b848e356d3adc0e7bc350234a821942a7b4e115755753689b63` |
| 314 | B | `bk7258-ota-stage.bin` | `26b3cd9ac4c80e8963d702bebea8ee4f0cbcf93ce3b052579bee04c46b52123d` |
| 314 | B | `bk7258-ota-pending-record.bin` | `f2e7eb4b8467dbf9619ed191083cc281a10e7dbe156dbdd0ffd4311d5defc13b` |
| 315 | A | `s_app-candidate.bin` | `ab516eb737299e15ece2948b46ae07e7e505a53db7492a4f65173a2c4dc5fa0f` |
| 315 | A | `bk7258-ota-stage.bin` | `bc658e2ee2baecabd6d8a2ca20020e58a8994ee2ee0ef5235730350c49c3cc7a` |
| 315 | A | `bk7258-ota-pending-record.bin` | `dc8529d7d5e8966ef6a782b1fb60fdb2ba9edcb9cccc8a97d48cf259969f7f78` |

Package roots:

```text
/home/lijian/project/open-vela/nuttx/bk7258-n15-physical-g314
/home/lijian/project/open-vela/nuttx/bk7258-n15-physical-g315
```

The offline carrier record uses its fixed package bank field. Runtime
publication is authoritative for bank selection: generation 314 published
bank 0 and generation 315 selected the other valid bank, bank 1.

## Physical evidence

All paths below are relative to the repository root.

| Gate | Evidence | Observed result |
|---|---|---|
| real power removal before lifecycle | `logs/bk7258-n15/20260804-powercut-reconnect-status/` | erased metadata, A active, AP/CPU2/RPTUN healthy |
| generation-314 volatile transfer | `logs/bk7258-n15/20260804-g314-powercut-psram-fresh-process/` | 40 candidate chunks plus descriptor/record batch verified |
| validate B candidate | `logs/bk7258-n15/20260804-g314-validate-mem-retry/` | `ret=0`, target 1, generation 314, zero Flash mutation |
| stage B | `logs/bk7258-n15/20260804-g314-stage-b/` | 629 sectors, 2576384 programmed/read back, candidate SHA matched |
| 10 s publication timeout | `logs/bk7258-n15/20260804-g314-publish-pending/` | `ret=-110`, phase 5, `mutation=0`; fail-closed |
| publish generation 314 | `logs/bk7258-n15/20260804-g314-publish-pending-180s/` | bank 0, 16 bytes programmed/verified, `mutation=1`, read-back pass |
| trial B after RTS | `logs/bk7258-n15/20260804-g314-trial-b-status/` | state 2, active/secondary 1, gates 0 |
| confirm B | `logs/bk7258-n15/20260804-g314-confirmed-b-status/` | state 3, generation 314, stable/active 1, bank 0 |
| generation-315 volatile transfer | `logs/bk7258-n15/20260804-g315-psram-load/` | 40 candidate chunks plus descriptor/record batch verified |
| validate A candidate | `logs/bk7258-n15/20260804-g315-validate-mem/` | `ret=0`, target 0, generation 315, zero Flash mutation |
| stage A | `logs/bk7258-n15/20260804-g315-stage-a/` | 629 sectors, 2576384 programmed/read back, candidate SHA matched |
| publish generation 315 | `logs/bk7258-n15/20260804-g315-publish-pending-a/` | previous bank 0, published bank 1, read-back pass |
| trial A after RTS | `logs/bk7258-n15/20260804-g315-trial-a-status/` | state 6, active/secondary 0, gates 0 |
| confirm A | `logs/bk7258-n15/20260804-g315-confirmed-a-status/` | state 7, generation 315, stable/active 0, bank 1 |
| confirmed-A RTS recovery | `logs/bk7258-n15/20260804-g315-post-confirm-reset-status/` | same confirmed record; AP READY, CPU2 online, RPTUN connected |
| full-power reconnect boot | `logs/bk7258-n15/20260804-g315-post-confirm-full-power-recovery/` | COM11 reached `NuttShell (NSH)`; commands had been scheduled before NSH and the bounded session timed out, so this row is boot evidence only |
| full-power recovery acceptance | `logs/bk7258-n15/20260804-g315-post-confirm-full-power-recovery-status/` | capture-only PASS: generation 315, bank 1, state 7, stable/active A, secondary/gates 0; AP READY, CPU2 online, RPTUN connected |

The 10-second publication attempt is deliberate negative evidence. Pair
rehashing took about 34 seconds, so it returned before metadata mutation. The
successful retry used 180 seconds and did not reuse a partially written
record.

## Retained service regression

Both trial slots passed the same bounded N14 regression:

| Service | B evidence | A evidence | Result |
|---|---|---|---|
| AP SMP/CPU2 | `20260804-g314-trial-b-regression` | `20260804-g315-trial-a-regression` | online mask `0x3`; SMP/affinity/semaphore tests pass |
| RPTUN | same | same | `CONNECTED(4)`, error 0 |
| LittleFS/PSRAM | same | same | LittleFS probe and PSRAM info pass |
| SDK timer | same | same | 32 callbacks pass |
| RPMsg | same | same | 6 runs x 5 messages pass |
| RPMsgFS | same | same | 4 payload classes x 1 pass |
| Bluetooth | same | same | controller info pass |

## Defects found and closed

1. Tier-1 Boot initialized APB and AON watchdogs but fed only APB. The
   project-owned Boot wrapper now feeds both; the source/ELF check enforces
   both operations. No official SDK or NuttX source was changed.
2. One large J-Link PSRAM load and repeated loads in one Commander process
   were unreliable. The loader now uses 64 KiB chunks, one fresh process per
   candidate chunk, explicit `noreset`, and `verifybin` after every load.
3. The physical publisher needs a full base/candidate rehash and took
   33.8--37.9 seconds. The generated campaign timeout is now 180 seconds.
4. The old validation config emitted `nsh: bkota: too many arguments` for the
   longest stage command even though the explicit result passed. The
   validation profile now sets `CONFIG_NSH_MAXARGUMENTS=10`, and its source
   contract enforces that value.
5. The console is 460800 8N1. Earlier 115200 captures were framing errors,
   not target crashes; only the corrected-baud retries are acceptance data.

## Post-run host closure

The validation profile and the restored normal profile both completed the
full build and host verification flow with official v3.1.1.9 checksums.

| Profile | Boot ELF | CP ELF | AP ELF |
|---|---|---|---|
| validation, gates 1/runtime initially closed | `c46cf283...cae760` | `55092f6d...58e95` | `6b8e1028...e5cc` |
| normal, all OTA gates 0 | `04e193c0...3a793` | `76f17d1a...c4c272` | `6b8e1028...e5cc` |

The normal shared build tree is restored to
`cp_nsh_psram + ap_smp_psram`; `N15_OTA_VALIDATION_ENABLED=false`, the Boot
gate value is 0, CP runtime write gates initialize false, and board-write
authorization is false.

## Normal profile restoration

After the physical lifecycle and complete-power-removal acceptance, the owner
approved restoring the normal gates-zero firmware. The verified normal package
was sparse-flashed through COM7 with exactly three segments:

| Segment | Raw range | SHA-256 |
|---|---:|---|
| Boot | `0x000000 + 0x11000` | `3042fb32a0e61f8f3289355a2ba7ae116a65771f9a1ceb4fb3eaf17558b173fb` |
| CP A | `0x011000 + 0xb4000` | `ac40ea2e90904384c1ce3e9b2b3216d28977437b6077284fe7b14eb58195dc57` |
| AP A | `0x165000 + 0x2e000` | `da640e333416477c42e387213f7d73638ebbf9e206008e8d03ec6a318678573e` |

`logs/bk7258-n15-normal-restore/20260804-195405/download.log` contains
`EraseFlash ->pass` and `WriteFlash ->pass` for all three segments, followed
by `Writing Flash OK` and `{All Finished Successfully}`. The reboot capture
reached NSH. The capture-only post-flash acceptance at
`logs/bk7258-n15-normal-restore/20260804-postflash-status/` passed all expected
predicates: AP READY, CPU2 scheduler-online, RPTUN connected, the existing
`BK7258LFS-OK` LittleFS probe, and PSRAM info. `bkota status` returned
`command not found`, proving that the validation CLI is absent from the board
firmware. The download-log SHA-256 is
`6f7a449ae55446bb381e2a361dd16a527dc900c61e0df89501efef0513aa3e2b`;
the post-flash status raw SHA-256 is
`0f325f07da02e052591a31f0848ae3be46699d0e851e6ee96bf71e280068cd97`.

The sparse ranges end at `0x193000`, before slot B at `0x286000`; neither
metadata bank, `usr_config`, LittleFS, reserved space nor the calibration tail
was included. Generation 315 was directly read before this restore and the
write set preserves its banks, but normal firmware intentionally has no
`bkota` CLI for a post-restore semantic metadata read.

## Safety boundary and next gate

- No chip erase was used during this lifecycle.
- No command targeted `usr_config`, LittleFS, reserved spans or the official
  calibration tail. This is a bounded write-set claim, not a new bit-exact
  read-back claim for those regions.
- The last observed board firmware is the normal
  `cp_nsh_psram + ap_smp_psram` profile. Its post-flash health acceptance
  passed and the `bkota` validation CLI is absent.
- The post-confirm complete-power-removal gate passed after both USB and
  J-Link power were absent. The recovery session matched all four required
  status regexes and no failure regex; its raw SHA-256 is
  `93350c82d63d8016963eba2f16d9c78342877c05a729d6b299213b696e4ed74b`.
- Package hashes provide integrity only. Publisher authentication, key
  provisioning and anti-rollback remain out of scope.
