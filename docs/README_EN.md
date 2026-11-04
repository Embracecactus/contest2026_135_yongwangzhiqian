# Documentation Layers and Navigation

English | [简体中文](README.md)

Documentation is organized by claim scope. A single-board observation is not
an SoC contract, and historical acceptance evidence does not override current
source, configuration, or build artifacts.

| Layer | Directory | Content and boundary |
|---|---|---|
| Contest delivery | [`contest/`](contest/) | Published technical-report snapshot and photos; [finals submission and updates](contest/决赛提交与初赛后更新.md); [presentation and film sources](contest/答辩材料/README.md) are committed; PPT/PDF, the official template and finished videos remain local and are supplied through the contest form; products and videos are linked from the repository README |
| Chip / SoC | [`chips/<soc>/`](chips/) | Board-independent register, startup, IRQ, clock, PM, SDK ABI, and shared-debug contracts |
| Platform integration | [`platforms/<soc>/`](platforms/) | Cross-board build, boot/update model, delivery method, compliance notes, and explicitly historical engineering records |
| Verification | [`verification/<soc>/`](verification/) | Immutable, dated host/board acceptance records with build identity and applicability |

Board names, profiles, partitions, and release policy are source/configuration
facts and are not duplicated under `docs/`. For BK7258, use
`boards/bk7258/CONFIGS.md` and `boards/bk7258/README.md`.

## BK7258 entry points

| Task | Entry |
| --- | --- |
| Understand the project, view videos/materials or build | [Project README](../README_EN.md) |
| Connect, configure and update with Android | [Android guide](../android/shaniu-companion/README.md) |
| Check current support and acceptance | [CONFIGS.md](../boards/bk7258/CONFIGS.md) |
| Find build, deployment and hardware contracts | [Platform entry](platforms/bk7258/README_EN.md) / [chip contracts](chips/bk7258/README.md) |
| Follow onboarding, voice, resources, desktop, OTA and reviewer reproduction | [Hands-on guide (Chinese)](platforms/bk7258/shaniu-user-guide.md) / [feature inventory (Chinese)](platforms/bk7258/shaniu-feature-inventory.md) |
| Finals submission checklist, defense logistics and post-preliminary updates | [Finals submission (Chinese)](contest/决赛提交与初赛后更新.md) |
| Review the latest recorded feature run | [November 3 evidence and videos](verification/bk7258/2026-11-03-feature-validation/README.md) |
| Check licensing and source origins | [SOURCE_PROVENANCE.md](../SOURCE_PROVENANCE.md) |

Historical sources: [dated evidence](verification/bk7258/),
[Master Plan decisions](platforms/bk7258/shaniu-master-plan.md), and
[September 20 contest report](contest/技术报告-BK7258三核适配与傻妞AI伴侣.md), and the
[September 20 preliminary-review snapshot (Chinese)](contest/初赛评审流程与实测快照-20260920.md).

A document marked historical, snapshot, or retired is bounded by its recorded
date and build identity. It must not be used as the current command, trust-root,
partition, or board-status source.
