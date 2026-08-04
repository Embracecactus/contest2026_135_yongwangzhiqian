# N15-V deterministic fault-injection host verification

- Verified: 2026-08-04T07:16:47+08:00
- Scope: validation-only target fault controls, ordered multi-identity campaign,
  deterministic power-cycle workflows, independent campaign verification,
  validation/normal builds, transfer dry-runs and safety closure
- SDK: official Beken v3.1.1.9 only
- Board/J-Link execution: **not authorized and not performed**
- Result: **host/source/ELF/campaign verified; board matrix pending**

> Historical evidence: this record captures the original 15-case A-to-B fault
> matrix. ADR-006 and the
> [format-2 symmetric verification](2026-08-04-n15-format2-symmetric-host.md)
> later added the sixteenth B-to-A case. This file is not an active board
> execution plan.

## Target fault controls

The `cp_nsh_ota` profile now contains generation-bound, operation-family-bound,
one-shot failure points for staging erase/write/read, publication
read/erase/write, and trial read/write. `bkota fault-arm` accepts an exact
ordinal and `N15-WRITE-<generation>` token. The matching mutation command
prints the final plan state, clears it on every exit path, and returns
`-ECANCELED` at the selected callback. A physical reset also clears the BSS
plan. The normal `cp_nsh_psram` profile neither compiles nor links this path.

`bkota corrupt-mem` is separately bounded to one byte inside the fixed
candidate PSRAM window. It requires the same generation token, rejects zero or
multi-byte masks and out-of-range offsets, and verifies the byte after XOR.
It cannot address Flash, the descriptor, the pending record, LittleFS or the
reserved tail.

The portable core/harness passed 7 positive and 12 negative cases under
`-Wall -Wextra -Werror` and applicable GCC `-fanalyzer`. The source/ELF gate
also proved:

- all eight target failure families are wired to their exact wrappers;
- generation mismatch and operation-family mismatch fail closed;
- an ordinal triggers exactly once and is then inactive;
- the 24-byte plan and initialization/runtime gates are zero-initialized BSS;
- validation Boot gates are 1, CP runtime write gates start false, no automatic
  reset exists, and `board_write_authorized=false`.

## Ordered board campaign

`pack_bk7258_ota_campaign.py` format 2 generated 15 independently identified
packages
under the unversioned build output
`/home/lijian/project/open-vela/nuttx/bk7258-n15v-campaign-v2`:

| Generation | Case | Injection |
|---:|---|---|
| 42 | candidate corruption | PSRAM offset `0x100`, XOR `0x01` |
| 43 | staging timeout | 1 ms |
| 44 | staging erase | `stage-erase 1` |
| 45 | staging program | `stage-write 2` |
| 46 | staging program read-back | `stage-read 17` |
| 47 | publication pre-read | `publish-read 1` |
| 48 | one trial without confirm | none |
| 49 | publication erase | `publish-erase 1` |
| 50 | publication program | `publish-write 2` |
| 51 | publication program read-back | `publish-read 3` |
| 52 | explicit rollback | none |
| 53 | trial read | `trial-read 1` |
| 54 | trial write | `trial-write 1` |
| 55 | health-gate refusal | `apctl inject primary` |
| 56 | successful confirm | terminal; must run last |

Each package was regenerated from the same validated CP/AP raw binaries but
has a unique generation, version, timestamp, RBL and pending metadata record.
All 15 passed the pair, official-source, metadata, Boot-ELF and fixed-PSRAM
transfer verifiers. The independent `verify_bk7258_ota_campaign.py` then
rechecked campaign ordering, exact faults, containment, all artifact hashes,
15 unique candidate/descriptor/metadata identities, and reran all 15 pair,
transfer and loader dry-run gates. The loader plans contain only the fixed
candidate/descriptor/record PSRAM load/verify pairs; no J-Link process was
opened and no Flash/reset command ran.

Every row now ends with a recorded `CONTROLLED POWER CYCLE`; ordinary lifecycle
rows retain their required hardware reset as a separate step. For a fault row,
the failpoint returns before the selected callback and the command quiesces
without a later Flash mutation. Removing power afterward therefore exercises
the durable state at that exact software-operation boundary. This is not an
analog mid-pulse brownout test and no such result is claimed.

Campaign manifest:

- file: `bk7258-n15v-campaign.json`
- format: 2
- size: 50,511 bytes
- SHA-256: `b82bba9c7a212457e109b00fc5e044e1fc98fd733424e1ad6bc6793639efbbde`
- status: PASS, generations 42..56, 15 cases
- `ordered_execution_required=true`
- `terminal_case_last=true`
- `controlled_power_cycle_required=true`
- `mid_flash_pulse_brownout_tested=false`
- `board_write_authorized=false`
- `automatic_reset=false`
- `flash_write_performed=false`
- `physical_execution_performed=false`

Independent verification report:

- file: `bk7258-n15v-campaign-verification.json`
- size: 558 bytes
- SHA-256: `4fb569a565d4ca98838849583cd0883b303a53143123b86b151cb00d543a4baf`
- 15/15 package identities and 15/15 loader dry-runs PASS

An attempted regeneration into the existing output failed closed without
overwriting it.

## Complete build closure

The exact validation profile was built first and passed all N15-A..F, fault,
RPTUN, BLE, PSRAM, layout and packaging gates:

| Validation artifact | SHA-256 |
|---|---|
| Boot ELF | `78709c4df09aaa29d78447d86df6ae4c1602ca0fb38f0153547e3f0ebf263e00` |
| CP ELF | `989caffbabffd07f27b1ebd2d6fb9e4c21d223e32c454f92dd9d47f8244e342b` |
| AP ELF | `6b8e102870e82d971a028cc560f18e67fc9a10d429fd33a555485fbb9086e5cc` |

The workspace was then rebuilt and restored to normal
`cp_nsh_psram + ap_smp_psram`:

| Normal artifact | SHA-256 |
|---|---|
| Boot ELF | `df982fb84ac02b3dfdbc1c8c039f7366dd05bc56fea2e3d50a135953cda11ded` |
| CP ELF | `c1e87359d8147106bb4100eda2da5d17321a451965ac13e91905646eaa83b1a6` |
| AP ELF | `6b8e102870e82d971a028cc560f18e67fc9a10d429fd33a555485fbb9086e5cc` |
| Boot physical image | `5c82d084ca2828e5cdb564efa6f4b22f251e39273a10a77b55ff4ce33e132826` |
| CP physical image | `b5bf444346815c5937d035a577c5ea7a82fcf5ea6434d7f28d840248659d3a25` |
| AP physical image | `623779a50627aa4d2ff7559a51db60eef6b7ecad463c4d43fb0baf9d167f11dd` |

The normal profile reports all six Boot gates zero, no host OTA bundle, no
fault injection, both compile write gates false and board authorization false.
Its CP ELF contains no `bkota` or `bk7258_ota_fault` symbol.

## Remaining hardware gate

This evidence does not prove physical Flash timing, remap, reset, rollback or
confirmation. Fresh owner authority must define the exact writable ranges and
lifecycle before physical validation begins. The next gate is one minimal
A-to-B and one B-to-A lifecycle, followed by retained-service checks and
normal-profile restoration. Exhaustive fault campaigns and complete-power-
removal testing are separate gates.
