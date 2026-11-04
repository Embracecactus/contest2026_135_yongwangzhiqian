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
- Wired whole-device recovery and apps-only OTA are distinct. Ordinary builds
  and downloads reuse the approved trust identities; they do not authorize
  generating or rotating BL1/MCUboot keys. Each full package carries the
  rollback floor for its build generation; its OTA-only counterpart does not
  replace BL1/BL2.

The [build, release, and hardware-evidence SOP](nuttx-port/bk7258-build-flash-debug-sop.md)
is the maintained command and safety reference. Do not recover current
addresses, scripts, or trust policy from historical N15/N17 documents.

## Source layers and change entry points

Paths below are relative to the contest repository root.

| Layer | Source | Responsibility |
| --- | --- | --- |
| SoC mechanisms | `chips/bk7258/{common,cp,ap,include}/` | CP/AP lifecycle, IRQ, clock, memory, radio proxy, controllers and cross-core transport |
| Physical boards | `boards/bk7258/{aidk_ai_toy,t5_board,t5ai_core}/` | Wiring, components, electrical policy and profiles; each `openvela.conf` selects configuration and layout |
| NuttX drivers | `nuttx/` | Standard device interfaces and generic driver overlays |
| Firmware products | `app/bk7258/`, `app/dolphin/` | Shaniu product services and the separate Dolphin product |
| Phone | `android/shaniu-companion/` | Native Android claim, authenticated BLE/TLS, settings, approvals and OTA |
| Computer and maintenance | `tools/bk7258/bk7258.py`, `tools/bk7258/_lib/` | One CLI with build, resource, workbench, signing and delivery domains |
| Build integration | `contest2026_135_yongwangzhiqian.xml`, `frameworks/cmake/` | Pinned dependencies, directory mappings and existing framework integration; the Agent Session history safety patch applies only to a SHA-256-pinned build copy |
| Verification | `tests/host/bk7258/`, `app/testing/bk7258/`, `tests/pytest/test_bk7258/` | Host regressions, target tests and controlled hardware automation |

See the [project architecture](../../../README_EN.md#architecture) for runtime
roles. The device's official AI Agent coordinates cloud conversation. Team code
enters the official workspace through directory-level manifest `linkfile`
mappings. Retired project memory, stage goals and old decisions remain in Git
history; historical evidence links to a fixed source version.

## Fact ownership

| Fact | Authoritative source |
|---|---|
| Board name, pinout, polarity, and attached-device instance | `boards/bk7258/README.md` and the selected board directory |
| CP/AP profiles, partition, and release-policy selection | `boards/bk7258/CONFIGS.md`, each board's `openvela.conf`, and selected CSV files |
| Current board support and acceptance boundaries | `boards/bk7258/CONFIGS.md`, linked from the root README |
| SoC IRQ, clock, PM, SDK ABI, and controller mechanism | [BK7258 SoC documentation](../../chips/bk7258/README.md) and `chips/bk7258/` |
| Public build, signing, package, release, and deployment commands | `tools/bk7258/bk7258.py --help` and the maintained SOP |
| Acceptance of one build or board run | Matching [dated verification record](../../verification/bk7258/) |
| Licenses and derived-source origin | `SOURCE_PROVENANCE.md` |

## Platform documents

- [Contest report](../../contest/技术报告-BK7258三核适配与傻妞AI伴侣.md) retains the
  contest snapshot; the root README contains videos and the build guide.
  Current board acceptance is summarized only in `boards/bk7258/CONFIGS.md`.
  Published dependency identities belong to the team manifest and the
  [source record](../../../SOURCE_PROVENANCE.md), which also records the sole active Agent Session
  build-copy patch and its source-hash constraint.
- [November 3 feature checks and recordings](../../verification/bk7258/2026-11-03-feature-validation/README.md)
  retain the dated board, emulator and logging-fix build evidence; they do not
  replace the current acceptance entry.
- [Shaniu Master Plan](shaniu-master-plan.md) retains historical decisions and
  version-bound evidence; the [desktop companion contract](shaniu-desktop-companion.md)
  defines product behavior.

- [RF calibration and factory-provisioning contract](rf-calibration-and-factory-provisioning.md):
  the maintained Chinese product contract for device-unique RF state, Beken
  calibration/test tool roles, station flow, recovery, OTA, and universal
  factory-image boundaries;
- [Official compliance reassessment](official-compliance-review.en.md) /
  [中文](official-compliance-review.md): a 2026-08-28 audit snapshot of openvela
  documents 1443/1444/1445;
- [openvela documentation adaptation matrix](openvela-document-adaptation-matrix.md):
  a 2026-08-28 capability-audit snapshot, not a live roadmap;
- [T5-Board P0 diagnostics, xTS, and performance guide](p0-diagnostics-performance.md):
  the T5-Board profiles and evidence boundary as reviewed on 2026-08-27;
- [BKVision camera and continuous recording](bkvision-camera-recording.md)
  (Chinese): AIDK `bkvision` commands, layering, SD NAND data path and the
  `18.6.321+381` recording evidence; the iteration record is
  [continuous recording adaptation](aidk-continuous-recording.md); and
- [build, release, and hardware-evidence SOP](nuttx-port/bk7258-build-flash-debug-sop.md):
  the maintained operator entry.

## Historical and board evidence

- The [porting report](porting-report.md) records the BK7258 chip-porting history
  that began with bring-up on the Tuya T5AI-Core. Its detailed N1–N15 board
  evidence ends on 2026-08-04; it does not publish the current multi-board status.
- The [AIDK 2026-09-01 board-test evidence](aidk-ai-toy-board-test-handoff.md)
  and [2026-09-29 factory-init validation](factory-validation-20260929.md) are
  board records bound to their build identities.
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
