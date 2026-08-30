# N15-M contiguous-layout migration board verification

- Status: `board-verified`
- Verification window: 2026-08-03T22:26:21+08:00 to 2026-08-03T22:36:28+08:00
- Branch: `feat/bk7258-n15-ota`
- Source base/HEAD before commit: `6de4962147e5ee180def704d219ace9ae11f6e4e`
- SDK: official Beken v3.1.1.9 only
- Profiles: `cp_nsh_psram + ap_smp_psram`
- Architecture: `memory/decisions/ADR-004-n15-official-contiguous-ab-layout.md`

## Result

The owner-authorized one-time migration from the N14 layout to the
official-style contiguous CP/AP A/B geometry passed on the physical T5-AI.
The old LittleFS contents were intentionally discarded. The migrated board
boots CP and AP SMP from A, mounts the relocated LittleFS, reconnects RPTUN,
and passes the retained N14 service regressions and three physical resets.

This verifies the **N15-M layout baseline only**. The current Tier-1
bootloader still boots A directly. B contains the same CP/AP pair as a seed,
but its manifest says `boot_selectable=false` and
`rbl_header_present=false`; candidate staging, trial, confirm, rollback and
power-loss behavior are not implemented or claimed.

## Factory inputs and write set

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `all-app-factory.bin` | `5226496` (`0x4fc000`) | `4722e2a81504e5e321f67850c518b0b919b79e796214481d1e0dd01bf9cf8e4b` |
| `littlefs_factory_clear.bin` | `1048576` (`0x100000`) | `f5fb04aa5b882706b9309e885f19477261336ef76a150c3b4d3489dfac3953ec` |
| `s_app_seed.bin` | `2576384` (`0x275000`) | `aa89797aa90bca393061ed100ca03c5cbed7194fb592cc86f60a7fe8af263d4d` |

Before the hardware operation, both the canonical layout verifier and the
independent byte-exact factory verifier passed. The loader was given exactly
these two ranges:

```text
all-app-factory.bin@0x000000-0x4fc000
littlefs_factory_clear.bin@0x600000-0x100000
```

Consequently `usr_config` (`0x4fc000..0x50a000`), both reserved spans, and the
official tail (`0x7fa000..0x800000`) were absent from the loader write set. A
chip erase was not used.

The final host checkpoint reran the positive verifier and two independent
negative fixtures: a truncated LittleFS-clear image was rejected for SHA-256
drift, and a manifest carrying an old/wrong layout ID was rejected for layout
drift.

Additional final host gates passed:

- exact v3.1.1.9 CP and AP SDK bundle checksum checks;
- Tier-1 `make clean all verify`, with the rebuilt bootloader byte-identical
  to the packaged bootloader;
- RBL inspector self-test;
- RPTUN ELF/layout verifier;
- N14-identity BLE-GATT ELF/source verifier;
- PSRAM ownership/ELF/source verifier;
- Python bytecode and per-file Black checks;
- shell syntax and `git diff --check`.

Canonical download evidence:

```text
/home/lijian/project/open-vela/logs/bk7258-auto-debug/20260803-222620/
```

`download.log` records both `EraseFlash ->pass`, both `WriteFlash ->pass`,
and `Writing Flash OK`. The immediate UART verdict was `PASS_NSH`.

## Read-back boundary and caveat

A pre-migration 8 MiB read was retained at:

```text
/home/lijian/project/open-vela/logs/bk7258-layout-migration/20260803-221329/pre-migration-flash-8m.bin
SHA-256 055d6e5befec6488a4b23575e29b75bcea8787252721d9ee442bce7d693ad2e1
```

It was captured at 6 Mbps. Repeated post-analysis showed that BKFIL high-speed
read can insert isolated 128-byte all-zero blocks. Therefore this file is a
forensic reference only and **must not be reflashed or described as a
bit-exact recovery backup**.

Post-migration `usr_config` and official-tail reads were repeated twice at
115200 and each pair was byte-identical:

| Region | Range | SHA-256 of both reads | Result |
|---|---:|---|---|
| `usr_config` | `0x4fc000..0x50a000` | `d078e2a26c51299488a166281edd3c6daebc1611316ef04580d24d6583e79acf` | repeat-identical; all `0xff` |
| official tail | `0x7fa000..0x800000` | `fa92844a96d507bbdeffaedfa164a01f75bac1c18fa99b7b1826b81498258c77` | repeat-identical; 16 non-`0xff` bytes |

The structural write-set proof establishes that these regions were not
targets. Because the only pre-migration full read used the unreliable
high-speed mode, no bit-exact pre/post preservation claim is made from that
dump. Future acceptance read-back uses 115200 and requires two identical
captures.

## Runtime regression

Raw evidence root:

```text
/home/lijian/project/open-vela/logs/bk7258-layout-migration/20260803-221329/
```

| Gate | Evidence | Result |
|---|---|---|
| CP boot/NSH | migration UART and reset summaries | `PASS_NSH` |
| AP primary | `post-migration-status.raw` | `READY`, error 0, generation 1, AP vector `0x02150000` |
| AP logical CPU1 | `post-migration-status.raw` | `SCHEDULER_ONLINE`, online mask `0x3`, vector `0x02150200` |
| transport/supervisor | `post-migration-status.raw` | RPTUN `CONNECTED`, supervisor `HEALTHY` |
| relocated LittleFS | `post-migration-status.raw`, `post-cold-status.raw` | autoformat/mount and `BK7258LFS-OK` after cold resets |
| PSRAM | `post-migration-regression.raw` | info PASS; CP heap 256/256; AP CPU0/CPU1 16/16; free stable |
| SDK timer | `post-migration-regression.raw` | 256/256 callbacks PASS, queued self-delete PASS |
| RPMsg | `post-migration-regression.raw` | six scenarios x20 PASS; heap stable |
| RPMsgFS | `post-migration-rpmsgfs.raw` | four payload classes x1 PASS |
| Bluetooth | `post-migration-bt.raw` | info PASS; `c8:47:8c:47:47:48`; fallback 0; ACL 70/20 |

## Physical reset repeatability

Three independent COM7 RTS reset captures passed:

| Evidence directory | Reset | UART |
|---|---|---|
| `logs/bk7258-auto-debug/20260803-223528` | `PULSE_OK`, `cold_path=yes` | `PASS_NSH` |
| `logs/bk7258-auto-debug/20260803-223558` | `PULSE_OK`, `cold_path=yes` | `PASS_NSH` |
| `logs/bk7258-auto-debug/20260803-223628` | `PULSE_OK`, `cold_path=yes` | `PASS_NSH` |

After the third reset, `post-cold-status.raw` again records LittleFS,
AP/CPU2, RPTUN and supervisor healthy.

## Residual boundary

- Do not restore pre-migration sparse offsets on this board.
- Normal sparse updates use CP `0x011000` and AP `0x165000`, and preserve B,
  metadata, `usr_config`, LittleFS, reserved spans and the official tail.
- Runtime OTA mutation remains `writes_enabled=false`.
- N15-A is the next gate: deterministic pair manifest plus exact v3.1.1.9
  RBL container/parser and negative tests, without board writes.
- Signatures, key provisioning and anti-rollback remain unresolved security
  policy; CRC32/FNV/SHA are integrity checks only.
