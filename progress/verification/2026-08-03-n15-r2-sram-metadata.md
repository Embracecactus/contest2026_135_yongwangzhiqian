# N15-R2 SRAM engine and metadata ABI verification

> Historical rejected-option evidence. ADR-003 was superseded by ADR-004
> before any board write. The archived SRAM engine/metadata ABI is not an
> active build, recovery or deployment gate. Current evidence:
> [N15-M board verification](2026-08-03-n15-migration-board-verification.md).

- Date: 2026-08-03
- Branch: `feat/bk7258-n15-ota`
- Source point: `6de4962147e5ee180def704d219ace9ae11f6e4e`
- SDK: official Beken v3.1.1.9 only
- Scope: host/source/link verification; no board connection and no Flash write
- Result: PASS, with `writes_enabled=false`

## Verified implementation boundary

The team Tier-1 bootloader now links a complete low-level Flash-controller
implementation into `.ota_sram`, with a boot-Flash load image and SRAM run
address. It is retained only for static verification in R2:

- `.ota_sram` VMA: `0x28000000`;
- `.ota_sram` LMA: `0x02000c40`;
- linked size: `0x680` (1664 bytes) within the `0x2000` reservation;
- maximum static entry stack: 176 bytes, limit 512 bytes;
- external calls reachable from the SRAM closure: 0;
- XIP pointer literals in the SRAM closure: 0;
- `.data` and `.bss`: empty;
- normal Tier-1 boot-path callers of `boot_ota_engine_call`: 0;
- sole caller of the installer: inactive `boot_ota_engine_call` wrapper;
- source and linked write-gate value: 0.

The entry requires IRQs masked, D-cache and MPU disabled, its request and MSP
inside the boot SRAM stack range, and CPU1/CPU2 reset/powered down. The closure
contains its own controller wait, watchdog feed/fail-reset, Flash ID/status,
32-byte read/program, 4 KiB erase/copy, range checking, and read-back paths. A
stuck controller never returns to XIP; it stops feeding the watchdog and waits
for reset.

This is not permission to run the read-only probe or any mutation command on a
board. The mutation dispatch is compiled into the audited section but cannot
pass the immutable zero-valued gate.

## Official source contracts

`verify_bk7258_ota_sram.py` checks the exact v3.1.1.9 source directory name,
critical source fragments, and hashes for the Flash driver/LL/register model,
SYS secondary-core state, watchdog register model, and SDK configuration. The
verified contracts include:

- current Flash identity `0xC86517`, 8 MiB, two status bytes;
- 4 KiB erase and 32-byte SDK transaction chunk;
- raw read/program/sector-erase commands `5/12/13`;
- Flash controller base `0x44030000` and register-field layout;
- SYS, APB WDT, and AON WDT bases;
- CPU1/CPU2 reset, power-down, and power-down-status fields;
- `CONFIG_SOC_BK7236XX=y`, quad disabled, volatile status write disabled, and
  Flash mailbox lock disabled in the pinned library configuration.

The implementation preserves all non-protection status bits. It clears and
later restores only the v3.1.1.9 C86517 BP/CMP protection fields. The future
transaction parser must make restoration after reset part of recovery before
the write gate can be reconsidered.

## Persistent metadata ABI

`boot_ota_abi.h` and `bk7258_ota_metadata.py` define and cross-check exact
little-endian v1 records:

| Object | Size | Integrity | Commit rule |
|---|---:|---|---|
| immutable journal header | `0x100` | CRC32 at `0xfc`; reserved bytes must be `0xff` | all four copies and arm markers must match before active erase |
| control/phase marker | `0x20` | CRC32 at `0x1c` | exact full record only; torn prefix remains uncommitted |

The header freezes sequence/generation, Flash/layout constants, CP/AP sizes,
pre-transaction protection status, pair/slot SHA-256 digests, and the raw
CRC-expanded image encoding. SHA-256 and CRC32 are integrity checks only; the
ABI does not claim publisher authentication or anti-rollback security.

Metadata self-test results:

- 256 single-byte header corruption cases rejected;
- 32 single-byte marker corruption cases rejected;
- 32 torn-marker prefix cases classified correctly;
- C/Python size, offset, magic, and layout constants agree;
- `writes_enabled=false`.

The exact-byte journal simulator uses these records and still passes all
32,915 reset/torn-write cases across the forward/reverse swap, activation,
one-trial confirmation, recovery, and negative paths.

## Full v3.1.1.9 regression build

The exact `cp_nsh_psram + ap_smp_psram` dual-image build completed after the
SRAM/ABI changes. SDK CP/AP checksums and the existing RPTUN, BLE GATT, and
PSRAM verifiers all passed. The resulting manifest records:

- CP raw/CRC/padded sizes: `676820 / 719134 / 720896 (0xb0000)` bytes;
- AP raw/CRC/padded sizes: `173640 / 184518 / 188416 (0x2e000)` bytes;
- CP raw SHA-256: `5dfed72db261aa45a291d643977ea2976d014a6b7987e5ff94b9f029ad558f1e`;
- AP raw SHA-256: `9e95bbb7f9cfc8d4302929bc18eeb81ca85f23accb7e087b1d238a8afad259ce`;
- padded Tier-1 boot SHA-256: `cd3d1452c50cd6b8bc509e293bc19598025abb81b75fda59d41078bb24b0e48a`;
- factory-image SHA-256: `d9e03048aeb04bc6827384ab2b620902ca27029bfde87e695d80337107b1c93c`.

The build exported `bk7258-ota-sram.json` next to the dual-image manifest, so
the shipped build evidence carries the same `writes_enabled=false`, SRAM
size, stack, reachability, and exact-SDK contracts. Tracked files in the
official NuttX and apps trees remained clean; the official SDK source was
used read-only and bound by the verifier's exact hashes.

## Commands

```bash
make -C board/bk7258_t5ai/bootloader verify \
  BK7258_SDK_SOURCE=/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9

python3 board/bk7258_t5ai/scripts/bk7258_ota_metadata.py \
  --self-test --json

python3 board/bk7258_t5ai/scripts/simulate_bk7258_ota_journal.py --json

python3 board/bk7258_t5ai/scripts/verify_bk7258_ota_layout.py \
  --sdk-source /home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9 \
  --json
```

## Remaining gate

N15-R2 technical evidence is complete, but ADR-003 remains Proposed. The next
stage is explicit owner acceptance or rejection of the layout/recovery
tradeoff, followed by N15-A deterministic pair manifest/bundle work. N15-B
staging and N15-D swap remain forbidden until their own later safety gates;
changing `BK7258_OTA_ENGINE_WRITE_GATE` is not part of ADR acceptance.
