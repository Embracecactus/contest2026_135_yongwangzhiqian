# 95｜教学图索引

本页登记学习区中的手工图和 Graphify 精选图。没有登记来源、版本和限制的图，只能视为临时草稿。

> **来源记录**
>
> - 教学主题：BK7258 教学图的来源、状态和复核索引
> - source ref：`$CONTEST` 的 `HEAD`（撰写时）
> - source commit：`c588afbd8e0f1d30723f5076e585673a6ace8a4e`
> - Graphify version：`0.9.25`
> - 最后核对日期：2026-07-24
> - 说明：图索引只记录教学资产，不发布 current 实施状态

## 1. 状态定义

| 状态 | 含义 |
|---|---|
| `planned` | 已定义问题和范围，尚未生成 |
| `raw-generated` | 工具已生成原始图，但尚未人工核对 |
| `curated-static` | 已核对静态源码路径和 AST 关系，可用于教学导航 |
| `runtime-checked` | 静态图另有匹配的运行证据；必须链接证据来源 |
| `superseded` | 来源版本变化或图被新版本替代，不再作为默认入口 |

`curated-static` 不等于 `build-verified` 或 `board-verified`。

## 2. 已登记图

| ID | 图 | 类型 | Source ref | 输入范围 | 状态 |
|---|---|---|---|---|---|
| G-001 | [Board bring-up 最小 AST 拓扑](../assets/graphify/curated/01-board-bringup-topology.md) | Graphify AST + 人工复核/纠错 Mermaid | `bk7258-n6-sdk-irq-bridge-clean` @ `07c6bbc7e2722f78b5abc5cec9a66d3f091b501b` | `board.h`、`bk7258_bringup.c` | `curated-static` |
| M-001 | [NSH → `board_app_initialize()` 调用链](../30-nuttx-core/34-bringup-nsh-and-procfs.md#2-m-001-调用链图) | 人工跨仓追踪 Mermaid | apps `e81a737` + nuttx `e02f581` + contest `c588afb` | Kconfig、NSH、boardctl、BK7258 bring-up | `curated-static` |
| M-002 | [BK7258 四级初始化阶段图](../30-nuttx-core/30-arch-chip-board-layers.md#2-m-002-初始化阶段图) | 人工跨仓追踪 Mermaid | nuttx `e02f581` + apps `e81a737` + contest `c588afb` | reset、`up_initialize`、early、late、app | `curated-static` |
| M-003 | [五个标准架构接口调用顺序](../30-nuttx-core/32-irq-and-critical-sections.md#2-真实调用顺序) | 人工跨仓追踪文本表 | nuttx `e02f581` + contest `c588afb` | heap、IRQ、timer、early serial、serial | `curated-static` |
| M-004 | [Flash 存储栈分层图](../40-subsystems/flash-mtd-filesystem/01-mental-model.md#2-m-004存储栈分层图) | 人工跨仓追踪 Mermaid | nuttx `e02f581` + contest `c588afb` | flash、MTD、FTL、LittleFS | `curated-static` |
| M-005 | [IRQ/Vector Bridge 链路图](../40-subsystems/interrupt-vector/01-mental-model.md#2-m-005中断链路图) | 人工跨仓追踪 Mermaid | nuttx `e02f581` + contest `c588afb` | SDK source、NVIC、RAM vector、g_irqvector | `curated-static` |

## 3. G-001 生成记录

### 想回答的问题

用一个足够小的例子说明：

- Graphify 的文件节点、函数节点分别是什么；
- `imports`、`contains`、`calls` 三类关系怎样阅读；
- 为什么 AST 静态边不能直接当成运行时调用证明。

### 稳定来源

- 源分支：`bk7258-n6-sdk-irq-bridge-clean`
- 源 commit：`07c6bbc7e2722f78b5abc5cec9a66d3f091b501b`
- 快照来源：committed Git objects
- 未提交工作树内容：明确排除
- 输入文件：
  - `board/bk7258_t5ai/include/board.h`
  - `board/bk7258_t5ai/src/bk7258_bringup.c`

### 实际 Graphify 结果

执行方式：

```bash
"$GRAPHIFY_VENV/bin/python" -m graphify extract .
```

结果：

```text
2 code files
4 nodes
4 edges
2 communities
```

原始缓存产物：

```text
$GRAPHIFY_ROOT/runs/01-board-bringup/ast-input/graphify-out/graph.json
$GRAPHIFY_ROOT/runs/01-board-bringup/ast-input/graphify-out/.graphify_analysis.json
```

本次没有运行 `cluster-only`，所以不登记 `GRAPH_REPORT.md` 或 `graph.html`。

### 复核边界

已核对：

- 图中四个节点及其源码路径；
- 三条 `contains`/`calls` 关系与固定 snapshot 源码一致；
- 一条 `imports` 关系被人工驳回：Graphify 将 `bk7258_bringup.c:L29` 的 `<nuttx/board.h>` 误配为输入中的本地 `board/bk7258_t5ai/include/board.h`；
- Mermaid 图保留该误配边为虚线教学案例，文本关系表明确给出复核结论。

尚未证明：

- Kconfig/`.config` 是否选择相关路径；
- 源文件是否进入某份最终固件；
- `board_app_initialize()` 在板上是否执行；
- 输入范围之外的函数、宏、动态注册和外部依赖；
- 当前实施 worktree 的未提交变化。

## 4. 规划中的图

| ID | 主题 | 首选范围 | 生成前置条件 | 状态 |
|---|---|---|---|---|
| G-002 | manifest/linkfile 所有权图 | contest manifest + board 映射 | 先人工确定映射语义；Graphify 不一定识别 XML/linkfile | `planned` |
| G-003 | 启动链概览 | bootloader + 固定 NuttX 启动入口 | 固定同一 checkpoint，并人工补汇编/链接边 | `planned` |
| G-004 | IRQ/vector bridge | 最小 IRQ bridge 源码集合 | 当前实施形成稳定 checkpoint | `planned` |
| G-005 | flash → MTD → filesystem | board storage + 对应 NuttX upper layers | 先定义跨仓快照清单 | `planned` |
| G-006 | GPIO upper/lower-half | board GPIO + NuttX GPIO framework | 实施接口稳定并完成源码复核 | `planned` |

一次只生成一张图。先提出一个明确问题、收敛输入，再运行工具；不扫描整个 `$WORKSPACE`。
