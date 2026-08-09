# BK7258 T5-AI 小白学习入口

这是一套从"看不懂仓库"走向"能沿证据链解释一个子系统"的中文学习材料。默认读者不了解 openvela、NuttX、BSP、Kconfig、链接脚本、向量表或寄存器，也不要求先具备板级移植经验。

本目录**不是**当前 BK7258 实施文档的简写版。它只建立稳定的学习顺序、阅读方法和证据纪律；当前进展、恢复入口和板端结论仍以 `$IMPL` 为准。

> **来源记录**
>
> - 教学主题：BK7258 T5-AI 学习入口与课程约束
> - source ref：`$CONTEST` 的 `HEAD`（撰写时）
> - source commit：`c588afbd8e0f1d30723f5076e585673a6ace8a4e`
> - 最后核对日期：2026-07-27
> - 已核对入口：contest README、manifest、`$IMPL/README.md`、`$IMPL/next-stage-prompt.md`、`$BOARD` 文件树
> - 新增课程来源：Stage N6-GPIO（C0/C1/C2 lower-half）、Stage N6（SDK IRQ Bridge）、Stage N7（CP/AP 多核）
> - 教学简化：本文只说明"去哪里找"和"怎样学习"，不复述任何 current Stage 技术细节

## 1. 路径变量

在完整 openvela 工作区根目录执行：

```bash
cd "<openvela-workspace-root>"
export WORKSPACE="$PWD"
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export IMPL="$CONTEST/docs/bk7258-t5ai"
export LEARN="$CONTEST/docs/learning/bk7258-t5ai"
export BOARD="$CONTEST/board/bk7258"
```

这些变量表达的是**角色**，不是某台电脑的固定路径。后续所有命令和来源记录都应继续使用它们。

## 2. 第一次阅读只做五件事

按顺序完成，不要一上来就改源码或烧录：

1. 阅读[学习路线](00-orientation/01-learning-roadmap.md)，知道先学什么、每一层的退出条件是什么。
2. 阅读[仓库地图与边界](00-orientation/02-repo-map-and-boundaries.md)，区分 `$LEARN`、`$IMPL`、队伍源码、manifest 映射和上游仓。
3. 阅读[权威来源地图](00-orientation/03-authoritative-source-map.md)，学会为一个结论选择正确证据。
4. 打开 [`$IMPL/README.md`](../../bk7258-t5ai/README.md) 和 [`$IMPL/next-stage-prompt.md`](../../bk7258-t5ai/next-stage-prompt.md)，只观察它们如何承担"实施状态入口"和"恢复指针"职责，不把其中 current 细节复制到学习笔记。
5. 打开 [`$BOARD`](../../../board/bk7258/) 和 [contest manifest](../../../contest2026_135_yongwangzhiqian.xml)，尝试回答："队伍真正拥有的源文件在哪里？它们被映射到完整工作区的哪里？"

完成后，你应能用自己的话解释：

- 为什么 `$WORKSPACE` 不是一个普通的单 Git 仓库；
- 为什么队伍改动应落在 `$CONTEST`，而不是直接改 manifest 生成的工作树入口；
- 为什么一篇教学文章不能宣布某项实现已经通过构建或板测；
- 为什么"源文件里有代码"不等于"该代码已进入最终固件并在硬件上执行"。

## 3. 三区隔离：学习时最重要的安全栏

### A. 学习区：`$LEARN`

放稳定的概念解释、阅读顺序、只读练习、术语和人工整理的教学图。

- 可以：解释概念、画简化图、链接来源、设计只读练习。
- 不可以：维护 current Stage、发布新板端结论、把推测写成实现事实。

### B. 实施区：`$IMPL` 与 `$BOARD`

放当前移植事实、源码、配置、实施记录和恢复指针。

- `$IMPL` 回答"当前做到了哪里、证据在哪里、下一实施入口是什么"。
- `$BOARD` 回答"队伍 overlay 中实际有哪些构建、启动、芯片和板级实现"。
- 教程发现事实变化时，应先由实施流程更新实现及证据，再回头更新教学解释。

### C. 工作区与参考区：`$WORKSPACE`

包括上游 NuttX/openvela 仓、manifest 映射视图、外部 SDK/手册和构建产物。

- 上游源码和外部资料必须记录各自 ref/commit 或版本。
- 映射目标是工作区视图，不是绕开 `$CONTEST` 的第二份队伍源码。
- `.config`、ELF、map、bin、串口输出等可以成为证据，但不是教学源文件。

## 4. 当前源码课

1. [四级初始化：reset、early、late 与 app](30-nuttx-core/30-arch-chip-board-layers.md)：判断一段初始化代码应该放在哪个执行阶段。
2. [五个固定接口：IRQ、tick、early UART、serial 和 heap](30-nuttx-core/32-irq-and-critical-sections.md)：NuttX 如何通过固定函数名让 architecture port 接入内核。
3. [NSH 怎样调用 `board_app_initialize()`](30-nuttx-core/34-bringup-nsh-and-procfs.md)：从 `CONFIG_NSH_ARCHINIT`、`boardctl(BOARDIOC_INIT)` 追到 BK7258 board implementation。
4. [Flash 存储栈：从硬件到文件](40-subsystems/flash-mtd-filesystem/01-mental-model.md)：Flash → MTD → FTL → LittleFS 逐层讲解。
5. [IRQ/Vector Bridge：从硬件中断到 NuttX ISR](40-subsystems/interrupt-vector/01-mental-model.md)：SDK source → NuttX IRQ → RAM vector → ISR 四层映射。
6. [GPIO Lower-half：用户态 `/dev/gpioN` 驱动](40-subsystems/gpio/01-mental-model.md)：NuttX GPIO upper/lower half 架构、ioctl 接口、BK7258 P9 LED / P29 USERKEY 实现。
7. [SDK 边界：外部库与 RTOS 的共存模式](40-subsystems/sdk-boundary/01-mental-model.md)：SDK 与 NuttX 的分界模式、IRQ Bridge 设计、archive ownership 陷阱。
8. [多核基础：BK7258 三核架构与 CP/AP 分离](40-subsystems/multicore-basics/01-mental-model.md)：CPU0 CP / CPU1 AP 启动链、mailbox doorbell、双 NuttX 镜像模型。

## 5. 推荐学习主线

```text
方向感
  ↓
二进制 / C / 寄存器 / Cortex-M33 基础
  ↓
repo manifest / Kconfig / Make-CMake / linker
  ↓
复位、启动链、C 运行时、第一条可观察输出
  ↓
NuttX arch → chip → board → bring-up 分层
  ↓
UART → 时钟/复位 → IRQ/向量 → timer/tick → WDT → GPIO → Flash/MTD/文件系统
  ↓
SDK 边界与多核基础
  ↓
只读追踪 → 可重复观察 → 小改动 → 独立验证
```

这条主线刻意把"改代码"放在后面。初学者最先需要的是建立**边界感**和**证据感**，而不是记住某个寄存器值或复制一段当前实现。

## 6. 每个 subsystem 都使用同一套五篇模板

未来 `40-subsystems/` 下的每个子系统都固定包含以下内容，文件名和问题顺序保持一致：

1. **`01-mental-model.md` — 心智模型**
   - 它解决什么问题？
   - 输入、输出、状态和失败方式是什么？
   - 哪些类比只是教学简化？
2. **`02-source-and-config-map.md` — 源码与配置地图**
   - 相关 Kconfig、defconfig、Make/CMake、头文件和实现文件在哪里？
   - 哪个仓拥有它们？有效配置从哪里确认？
3. **`03-runtime-call-flow.md` — 运行时路径**
   - 从入口到硬件或内核服务经过哪些层？
   - 中断上下文、线程上下文和启动阶段在哪里切换？
4. **`04-safe-observation-lab.md` — 安全观察实验**
   - 先设计只读或最小扰动观察；写出前置条件、停止条件、恢复方式。
   - 未经明确实施授权，不把教程步骤升级为构建、烧录或板测动作。
5. **`05-evidence-and-review.md` — 证据与复核**
   - 区分静态源码、有效配置、最终产物和板端观察。
   - 列出常见误判、复核问题、source ref/commit 和待确认项。

固定模板让读者换到 UART、IRQ、时钟或 flash 时，仍然使用同一种调查方法，而不是每章重新猜阅读方式。

## 7. implementation truth 与教学解释

### implementation truth

对"当前实现究竟怎样工作"的回答，至少要能追溯到以下链条中的相关部分：

```text
固定版本的源码
  + 有效构建配置
  + 实际链接/打包产物
  + 与结论匹配的运行或板端证据
```

不同问题需要链条中的不同证据，不能只凭注释、文件名、旧 worklog 或教学图下结论。

### 教学解释

教学解释可以：

- 用较少节点呈现主路径；
- 把多个底层步骤合并成一个概念框；
- 用伪代码、比喻和教学图降低第一次阅读难度。

但必须同时：

- 明确哪些步骤被省略；
- 链接到可复查来源；
- 记录来源 ref/commit；
- 遇到来源冲突时标记"待确认"，而不是替 implementation truth 做决定。

## 8. 每篇教程必须带来源记录

最低字段如下；更完整的规则见[权威来源地图](00-orientation/03-authoritative-source-map.md)：

```markdown
> **来源记录**
>
> - 教学主题：
> - source ref：
> - source commit：
> - 上游/外部来源版本：
> - 最后核对日期：
> - 教学简化与未覆盖项：
```

如果一个主题跨越 `$CONTEST`、`$WORKSPACE/nuttx` 和外部 SDK，就分别记录每个拥有者仓库的 commit；不能只写 `$WORKSPACE` 的日期或一个模糊的 "latest"。

## 9. 图和 Graphify

- [教学图索引](90-reference/95-graph-index.md)记录每张图想回答的问题、来源、状态和复核日期。
- [Graphify 使用与入仓规则](assets/graphify/README.md)记录隔离安装、AST-only 提取、输入边界和人工复核要求。
- 已用 Graphify `0.9.25` 对一个 committed board bring-up 最小快照完成 AST-only 提取；首个结果见 [G-001 Board bring-up 最小 AST 拓扑](assets/graphify/curated/01-board-bringup-topology.md)。
- 本次仅生成 `graph.json` 和 `.graphify_analysis.json`，没有使用 LLM，也没有生成或声称存在 `GRAPH_REPORT.md`、`graph.html`。
- 工具原始输出保留在 `$WORKSPACE/.cache/graphify/`，不进入仓库；只有人工筛选、去敏、补充来源记录和文本替代后的教学图进入 `assets/graphify/curated/`。

## 10. 当前实施入口

当你的问题变成"现在应该继续哪一步""某项能力是否已经板端验证""当前分支有哪些门禁"时，请停止阅读学习叙事，转到：

- [`$IMPL/README.md`](../../bk7258-t5ai/README.md)：实施文档入口；
- [`$IMPL/next-stage-prompt.md`](../../bk7258-t5ai/next-stage-prompt.md)：当前恢复指针；
- 这些入口链接到的源码、配置、产物和证据。

学习区不会复述这些文件中的 current 状态，以避免教学内容和实施事实形成两个互相漂移的版本。
