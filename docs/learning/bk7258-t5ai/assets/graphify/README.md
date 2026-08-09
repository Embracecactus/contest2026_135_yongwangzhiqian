# Graphify 使用与入仓规则

本目录说明怎样把 Graphify 用作**源码导航工具**，并把经过人工核对的结果转化为教学材料。Graphify 原始输出不是 implementation truth，也不能代替 Kconfig、有效 `.config`、ELF/map 或板端证据。

> **来源记录**
>
> - 教学主题：Graphify 安装、AST-only 提取、缓存隔离与教学图入仓规则
> - source ref：`$CONTEST` 的 `HEAD`（撰写时）
> - source commit：`c588afbd8e0f1d30723f5076e585673a6ace8a4e`
> - 已验证工具：PyPI 包 `graphifyy`，CLI/module version `0.9.25`
> - 最后核对日期：2026-07-24
> - 教学简化：这里只记录已实际验证的 AST-only 路径；需要模型 API 的语义提取不属于本次结果

## 1. 路径变量

```bash
cd "<openvela-workspace-root>"
export WORKSPACE="$PWD"
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export LEARN="$CONTEST/docs/learning/bk7258-t5ai"
export GRAPHIFY_ROOT="$WORKSPACE/.cache/graphify"
export GRAPHIFY_VENV="$GRAPHIFY_ROOT/venv"
```

约定：

- venv、输入快照、`graphify-out/` 和增量缓存全部位于 `$GRAPHIFY_ROOT`；
- 原始输出不放入 `$CONTEST`；
- 只有人工复核后的 Markdown/Mermaid 教学图进入 `curated/`；
- 教学图在 [`95-graph-index.md`](../../90-reference/95-graph-index.md)登记来源和状态。

## 2. 安装方式

Graphify 需要 Python 3.10+。为避免污染系统 Python，推荐使用工作区级隔离 venv：

```bash
python3 -m venv "$GRAPHIFY_VENV"
"$GRAPHIFY_VENV/bin/python" -m pip install graphifyy
"$GRAPHIFY_VENV/bin/graphify" install
```

本工作区已实际确认：

```text
Python 3.10.12
graphify 0.9.25
```

版本记录只说明工具入口可执行，不保证未来 PyPI 版本具有完全相同的参数和输出结构；升级后应重新核对本页。

## 3. AST-only 的正确最小流程

如果目的只是建立代码文件、函数、包含和静态调用关系，可以准备一个只含受支持代码文件的稳定快照，然后在输入目录中运行：

```bash
"$GRAPHIFY_VENV/bin/python" -m graphify extract .
```

本次首轮验证使用两个 committed snapshot 文件：

```text
board/bk7258/include/board.h
board/bk7258/src/bk7258_bringup.c
```

实际结果：

```text
2 code files
4 nodes
4 edges
2 communities
```

生成了：

```text
graphify-out/graph.json
graphify-out/.graphify_analysis.json
```

`extract` 完成后提示可以继续运行 `cluster-only` 生成 `GRAPH_REPORT.md` 并命名 communities；本次没有执行该语义/聚类后处理，因此不声称已生成 `GRAPH_REPORT.md` 或 `graph.html`。

## 4. 为什么输入要先做稳定快照

另一个实施会话可能正在修改源码。若直接扫描活动工作树，图可能混合两个编辑时刻，无法稳定复现。

正确做法：

1. 选定实际源码仓；
2. 记录 branch 和 commit；
3. 从 committed Git objects 导出本次所需的最小文件集合；
4. 把导出内容放在 `$GRAPHIFY_ROOT/runs/<scope>/`；
5. 只对该快照运行 Graphify；
6. 在图索引中记录 commit、输入清单和遗漏范围。

未提交工作树内容默认不进入教学图。确需讲解未提交实现时，必须显式标注为临时快照，不能冒充稳定来源。

## 5. 禁止扫描的内容

以下路径或内容不应进入 Graphify 教学输入：

- `$WORKSPACE/.repo/**`、Git 元数据和 worktree 管理目录；
- `$WORKSPACE/prebuilts/**`；
- `$CONTEST/logs/**`；
- 固件构建目录、ELF、bin、obj、archive、map 和缓存；
- 密钥、token、凭据、`.env`、私有配置；
- 与当前课程无关的完整 SDK 或整个 openvela 工作区；
- 另一个会话尚未形成 checkpoint 的未提交内容。

即使某个 Graphify 模式只发送语义内容，也必须先最小化输入范围；“不发送原始源码”不等于“可以无边界扫描”。

## 6. 原始产物与入仓产物

### 只保存在缓存

```text
$GRAPHIFY_ROOT/runs/<scope>/graphify-out/
├── graph.json
├── .graphify_analysis.json
├── GRAPH_REPORT.md       # 只有相应阶段实际生成时才存在
├── graph.html            # 只有相应阶段实际生成时才存在
└── cache/                # 只有相应阶段实际生成时才存在
```

不要为了凑齐固定树而创建假的空文件。

### 可以进入教学区

```text
$LEARN/assets/graphify/curated/
├── <scope>-topology.md
└── ...
```

精选文档必须包含：

- source repository/ref/commit；
- 输入文件清单；
- Graphify 版本；
- 节点和边的来源位置；
- 文本表格形式的无障碍替代；
- 未覆盖的动态调用、条件编译和外部依赖；
- “静态 AST 图不等于运行时或板测事实”的明确声明。

## 7. 人工复核清单

把一张图标为 `curated` 前逐项确认：

- [ ] 输入来自固定 commit，而不是变化中的工作树；
- [ ] 每个节点都能回到源码路径；
- [ ] 每条边标明 `imports`、`contains`、`calls` 等关系类型；
- [ ] 对关键边人工核对源码位置；
- [ ] 图中没有密钥、个人路径或无关文件；
- [ ] 同时提供文本关系表，不只依赖视觉布局；
- [ ] 明确列出输入范围之外的依赖；
- [ ] 没有把图误写成构建、链接、运行或板测结论；
- [ ] 已在 [`95-graph-index.md`](../../90-reference/95-graph-index.md)登记。

## 8. 本次首图

第一张已整理的教学图是：

- [Board bring-up 最小 AST 拓扑](curated/01-board-bringup-topology.md)

它只覆盖 `board.h` 与 `bk7258_bringup.c`，用于演示怎样读 Graphify 的节点和关系；它不是完整 BK7258 bring-up 调用图。
