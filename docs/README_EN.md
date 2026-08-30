# Documentation Layers and Navigation

English | [简体中文](README.md)

Documentation is organized by claim scope. A single-board observation is not
an SoC contract, and historical acceptance evidence does not override current
source, configuration, or build artifacts.

| Layer | Directory | Content and boundary |
|---|---|---|
| Chip / SoC | [`chips/<soc>/`](chips/) | Board-independent register, startup, IRQ, clock, PM, SDK ABI, and shared-debug contracts |
| Platform integration | [`platforms/<soc>/`](platforms/) | Cross-board build, boot/update model, delivery method, compliance notes, and explicitly historical engineering records |
| Learning | [`learning/`](learning/) | Source-versioned tutorials and mental models; never live implementation or board-acceptance status |
| Workflow | [`workflows/`](workflows/) | Git and collaboration procedures not owned by one SoC or board |
| Verification | [`verification/<soc>/`](verification/) | Immutable, dated host/board acceptance records with build identity and applicability |

Board names, profiles, partitions, and release policy are source/configuration
facts and are not duplicated under `docs/`. For BK7258, use
`boards/bk7258/CONFIGS.md` and `boards/bk7258/README.md`.

## BK7258 entry points

- [SoC and board integration](platforms/bk7258/README_EN.md) for the cross-board
  build, boot, update, and delivery boundary;
- [SoC documentation](chips/bk7258/README.md) for shared BK7258 contracts;
- [Learner documentation](learning/bk7258/README.md) for source-versioned tutorials;
- [Acceptance records](verification/bk7258/) for immutable, dated evidence;
- `boards/bk7258/CONFIGS.md` for the current supported-board, profile,
  partition, and configuration contract; and
- `SOURCE_PROVENANCE.md` for licenses and the origin of source, tables,
  protocols, and initialization sequences.

A document marked historical, snapshot, or retired is bounded by its recorded
date and build identity. It must not be used as the current command, trust-root,
partition, or board-status source.
