# Documentation Layers and Navigation

English | [简体中文](README.md)

Documents are organized by the scope of the claims they make. This prevents a
single-board observation from becoming an SoC-wide claim and prevents historical
worklogs from overriding current source, configuration, or acceptance evidence.

| Layer | Directory | Content and boundary |
|---|---|---|
| Chip / SoC | [`chips/<soc>/`](chips/) | Registers, core startup, IRQ, DVFS/PM, SDK ABI, chip bootloaders, and shared debug contracts; no board-only pinout or acceptance claims |
| Platform integration | [`platforms/<soc>/`](platforms/) | Cross-board CP/AP pairing, build and delivery, compliance, debug procedures, and retained engineering-stage records |
| Learning | [`learning/`](learning/) | Tutorials and mental models derived from verified sources; never the current implementation status |
| Workflow | [`workflows/`](workflows/) | Git and collaboration procedures not owned by a single SoC or board |
| Historical design | [`superpowers/`](superpowers/), [`ai-worklog/`](ai-worklog/) | Plans, prompts, and decisions as they existed at the time; not current implementation truth |
| Dynamic evidence | [`../progress/`](../progress/) | Current tasks, build identity, and hardware verification; dynamic claims must resolve here and in source/configuration |

## BK7258 entry points

- [Platform integration](platforms/bk7258/README_EN.md), covering T5AI Core,
  T5 Board, and AIDK AI Toy;
- [Official compliance review](platforms/bk7258/official-compliance-review.en.md),
  separating mandatory requirements, recommendations, example layouts, and
  architecture-specific differences in documents 1443/1444/1445;
- [SoC-level documentation](chips/bk7258/README.md);
- [Learner documentation](learning/bk7258/README.md); and
- [Current status](../progress/CURRENT.md).

The former `docs/bk7258-t5ai/` name incorrectly implied that cross-board material
was specific to T5-AI, so it has moved to `docs/platforms/bk7258/`. For the same
reason, `docs/learning/bk7258-t5ai/` is now `docs/learning/bk7258/`. The platform
tree still contains historical stage records under `nuttx-port/`, `bootloader-analysis/`,
and parts of `research/`; current claims must be checked against the platform
entry, compliance review, current source/configuration, and
`progress/verification/`.
