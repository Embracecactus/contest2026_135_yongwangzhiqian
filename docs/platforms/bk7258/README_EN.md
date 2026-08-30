# BK7258 SoC and Board Integration

English | [简体中文](README.md)

BK7258 is the SoC. T5AI-Core, T5-Board, and AIDK AI Toy are three physical
boards. This directory owns their shared build, boot, update, delivery, and
historical engineering documentation; it does not promote one board's wiring,
device population, or acceptance result to an SoC fact.

| Display name | Stable machine ID | Note |
|---|---|---|
| T5AI-Core | `t5ai_core` | Tuya T5AI-Core V1.0.1; older records may say T5-AI |
| T5-Board | `t5_board` | T5-Board V1.0.2 |
| AIDK AI Toy | `aidk_ai_toy` | Schematic alias AIToyBoard; the alias does not create another implementation |

## Current architecture boundary

- The system uses independent CP and AP NuttX images; physical CPU1/CPU2 run
  the AP SMP instance.
- `--boot direct` is an unsigned bring-up/diagnostic path, not a product release.
- The signed product chain is board-owned BL1 → pinned NuttX MCUboot BL2 →
  signed same-slot CP/AP images in an A/B model.
- Wired whole-device recovery and apps-only OTA are distinct. Every authorized
  full download uses fresh temporary BL1 and MCUboot key pairs; OTA remains
  bound to the public trust contract already installed on the target.

The [build, release, and hardware-evidence SOP](nuttx-port/bk7258-build-flash-debug-sop.md)
is the maintained command and safety reference. Do not recover current
addresses, scripts, or trust policy from historical N15/N17 documents.

## Fact ownership

| Fact | Authoritative source |
|---|---|
| Board name, pinout, polarity, and attached-device instance | `boards/bk7258/README.md` and the selected board directory |
| CP/AP profiles, partition, and release-policy selection | `boards/bk7258/CONFIGS.md`, each board's `openvela.conf`, and selected CSV files |
| SoC IRQ, clock, PM, SDK ABI, and controller mechanism | [BK7258 SoC documentation](../../chips/bk7258/README.md) and `chips/bk7258/` |
| Public build, signing, package, release, and deployment commands | `tools/bk7258/bk7258.py --help` and the maintained SOP |
| Acceptance of one build or board run | Matching [dated verification record](../../verification/bk7258/) |
| Licenses and derived-source origin | `SOURCE_PROVENANCE.md` |

## Platform documents

- [Official compliance reassessment](official-compliance-review.en.md) /
  [中文](official-compliance-review.md): a 2026-08-28 audit snapshot of openvela
  documents 1443/1444/1445;
- [openvela documentation adaptation matrix](openvela-document-adaptation-matrix.md):
  a 2026-08-28 capability-audit snapshot, not a live roadmap;
- [T5-Board P0 diagnostics, xTS, and performance guide](p0-diagnostics-performance.md):
  the T5-Board profiles and evidence boundary as reviewed on 2026-08-27; and
- [build, release, and hardware-evidence SOP](nuttx-port/bk7258-build-flash-debug-sop.md):
  the maintained operator entry.

## Historical and board evidence

- The [porting report](porting-report.md) records the BK7258 chip-porting history
  that began with bring-up on the Tuya T5AI-Core. Its detailed N1–N15 board
  evidence ends on 2026-08-04; it does not publish the current multi-board status.
- [`bootloader-analysis/`](bootloader-analysis/) retains Tuya and Beken
  bootloader reverse-engineering evidence.
- [`nuttx-port/`](nuttx-port/) retains only independently useful fault analyses,
  source reviews, and retired designs.
- [`hardware/t5ai-core/`](hardware/t5ai-core/) retains the T5AI-Core schematic,
  validation record, and historical bare-metal probe.

Dated verification files retain their evidence names and are not renamed when
naming conventions evolve. `BK7258` in such a filename identifies only the
SoC; determine the physical board from the recorded board, profile, fixture,
and build identity.

Old `board/bk7258/` paths, profiles, host-absolute paths, and N15/N17 addresses
describe their recorded worktree only. Re-establish current facts from the
authoritative sources above.
