# openvela 小白学习文档

本目录用于沉淀**面向完全新手、可按顺序阅读、可追溯来源**的学习材料。它帮助读者理解 openvela / NuttX 硬件适配中的概念、仓库结构、构建关系和常见子系统，但不承担当前实施状态、任务恢复或板端结论的发布职责。

> **来源记录**
>
> - 教学骨架基线：`$CONTEST` 的 `HEAD`（撰写时）
> - 基线 commit：`c588afbd8e0f1d30723f5076e585673a6ace8a4e`
> - 最后核对日期：2026-07-27
> - 核对范围：仓库入口、manifest、`$IMPL` 入口与 current pointer、BK7258 board tree
> - 新增核对范围：GPIO lower-half（C0/C1/C2）、SDK IRQ Bridge（N6）、AP/多核（N7）
> - 说明：这个 commit 只是本批教学文档的取材基线，不代表永久 current 状态，也不是本文未来提交的 commit

## 1. 先约定路径变量

后续教程不使用任何人的机器绝对路径。进入 `repo sync` 得到的 openvela 工作区根目录后，统一使用以下变量：

```bash
cd "<openvela-workspace-root>"
export WORKSPACE="$PWD"
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export IMPL="$CONTEST/docs/platforms/bk7258"
export LEARN="$CONTEST/docs/learning/bk7258"
export BOARD="$CONTEST/boards/bk7258"
```

其中：

- `$WORKSPACE`：完整 openvela 多仓工作区根目录，通常包含 `.repo/`、`nuttx/`、`apps/`、`packages/`、`vendor/` 等。
- `$CONTEST`：队伍拥有并提交的 contest 仓库。
- `$IMPL`：BK7258 当前实施文档、工作记录与恢复指针所在区域。
- `$LEARN`：本学习文档区域。
- `$BOARD`：队伍拥有的 BK7258 board overlay 源码。

## 2. 三区隔离

这里的“三区”是**职责隔离**，即使它们在文件系统上存在父子关系，也不能混用：

| 区域 | 典型路径 | 只回答什么问题 | 不应该做什么 |
|---|---|---|---|
| 学习区 | `$LEARN` | 初学者应该怎样建立概念、按什么顺序阅读、如何安全观察 | 不发布 current Stage，不把简化图当作实现证据，不指挥当前实施动作 |
| 实施区 | `$IMPL`、`$BOARD` | 当前移植事实、源码、配置、验证记录与 handoff | 不为照顾教学叙事而改写事实，不把未验证推断写成结论 |
| 工作区与参考区 | `$WORKSPACE/nuttx`、`$WORKSPACE/apps`、manifest 映射目标、外部 SDK/手册、构建产物 | 上游实现、外部规范、实际构建输入与产物证据 | 不直接修改生成映射或公共仓来规避队伍 overlay；不把构建产物当源文件提交 |

发生冲突时，先回到[权威来源地图](bk7258/00-orientation/03-authoritative-source-map.md)，不要让教学文档覆盖实施事实。

## 3. 当前学习入口

- [BK7258 小白学习入口](bk7258/README.md)
- [学习路线](bk7258/00-orientation/01-learning-roadmap.md)
- [仓库地图与边界](bk7258/00-orientation/02-repo-map-and-boundaries.md)
- [权威来源地图](bk7258/00-orientation/03-authoritative-source-map.md)
- [教学图索引](bk7258/90-reference/95-graph-index.md)
- [Graphify 安全使用约定](bk7258/assets/graphify/README.md)

需要了解**当前实施状态**时，应离开学习区，阅读 [`$IMPL/README.md`](../platforms/bk7258/README.md) 和仓库逻辑入口 `docs/platforms/bk7258/README.md`。学习材料不复制这些文件中的 current 细节。

## 4. 完整未来目录树

本次只创建有实际内容的文件。下列树同时是后续课程的规划索引；标为“规划”的目录和文件在内容准备好之前**不会创建**，也不会使用 `.gitkeep` 占位。

```text
docs/learning/
├── README.md                                      # 本次已创建
└── bk7258/
    ├── README.md                                  # 本次已创建
    ├── 00-orientation/
    │   ├── 01-learning-roadmap.md                 # 本次已创建
    │   ├── 02-repo-map-and-boundaries.md          # 本次已创建
    │   └── 03-authoritative-source-map.md          # 本次已创建
    ├── 10-foundations/                            # 规划
    │   ├── 10-binary-hex-and-bit-operations.md
    │   ├── 11-c-memory-pointers-and-volatile.md
    │   ├── 12-cortex-m33-exceptions-and-nvic.md
    │   ├── 13-memory-mapped-io-and-registers.md
    │   └── 14-linker-sections-and-images.md
    ├── 20-boot-and-build/                         # 规划
    │   ├── 20-repo-manifest-and-linkfile.md
    │   ├── 21-kconfig-make-cmake-flow.md
    │   ├── 22-reset-to-c-runtime.md
    │   ├── 23-boot-chain-and-image-layout.md
    │   └── 24-first-observable-uart.md
    ├── 30-nuttx-core/                             # 已开始
    │   ├── 30-arch-chip-board-layers.md            # 本次已创建
    │   ├── 31-startup-scheduler-and-idle.md        # 规划
    │   ├── 32-irq-and-critical-sections.md         # 本次已创建
    │   ├── 33-system-tick-and-timekeeping.md       # 规划
    │   └── 34-bringup-nsh-and-procfs.md            # 本次已创建
    ├── 40-subsystems/                             # 已开始
    │   ├── README.md                              # 规划
    │   ├── flash-mtd-filesystem/                  # 已开始
    │   │   └── 01-mental-model.md                 # 本次已创建
    │   ├── uart-console/                          # 规划
    │   ├── clocks-reset-dvfs/                     # 规划
    │   ├── interrupt-vector/                      # 已开始
    │   │   └── 01-mental-model.md                 # 本次已创建
    │   ├── timer-tick/                            # 规划
    │   ├── watchdog/                              # 规划
    │   ├── gpio/                                  # 已开始
    │   │   └── 01-mental-model.md                 # 本次已创建
    │   ├── sdk-boundary/                          # 已开始
    │   │   └── 01-mental-model.md                 # 本次已创建
    │   └── multicore-basics/                      # 已开始
    │       └── 01-mental-model.md                 # 本次已创建
    ├── 50-guided-labs/                            # 规划
    │   ├── 51-read-a-kconfig-path.md
    │   ├── 52-trace-one-symbol-to-elf.md
    │   ├── 53-read-a-linker-map.md
    │   ├── 54-design-a-read-only-board-observation.md
    │   └── 55-write-an-evidence-note.md
    ├── 60-debugging/                              # 规划
    │   ├── 60-observation-before-hypothesis.md
    │   ├── 61-fault-localization-by-boundary.md
    │   ├── 62-uart-swd-reset-and-power-boundaries.md
    │   └── 63-static-build-and-board-evidence.md
    ├── 90-reference/
    │   ├── 90-glossary.md                         # 规划
    │   ├── 91-command-cheatsheet.md                # 规划
    │   ├── 92-evidence-status-lexicon.md           # 规划
    │   ├── 93-tutorial-source-record-template.md   # 规划
    │   ├── 94-diagram-style-guide.md               # 规划
    │   └── 95-graph-index.md                       # 本次已创建
    └── assets/
        ├── diagrams/                              # 规划；只放人工筛选后的教学图
        │   ├── workspace-three-zones.svg
        │   ├── manifest-linkfile-map.svg
        │   ├── boot-chain-overview.svg
        │   ├── nuttx-layer-map.svg
        │   ├── interrupt-learning-flow.svg
        │   └── flash-stack.svg
        └── graphify/
            ├── README.md                          # 本次已创建
            └── curated/
                └── 01-board-bringup-topology.md   # 本次已创建
```

## 5. 所有后续教程的硬规则

1. **先来源，后解释。** 每篇教程开头必须记录 source ref、commit、外部文档版本和最后核对日期；没有来源记录就不能标为完成。
2. **教学解释不等于 implementation truth。** 类比、简化流程和教学图必须明确写出省略项，并链接到可复查的源码或实施文档。
3. **不复制 current 状态。** 当前 Stage、阻塞项、板端结果和下一步只在 `$IMPL` 维护；学习区只链接入口。
4. **先只读、后实验、再修改。** 新手练习先定位文件、追踪配置和验证来源；任何写代码、构建、烧录或板测动作都进入单独、明确授权的实施流程。
5. **一问一证据链。** “配置是否开启”“代码是否编入”“产物是否包含”“硬件是否执行”是四个不同问题，必须分别取证。
6. **图只是导航。** 图中的节点和边必须能回指来源；原始工具输出不直接入仓，人工筛选后的教学图才可进入 `assets/diagrams/`。
7. **不建空目录。** 新章节只有在首篇内容可提交时才创建目录，不使用 `.gitkeep`。
