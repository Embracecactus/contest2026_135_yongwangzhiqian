# openvela 学习文档

本目录收录面向新读者、可按顺序阅读并可追溯来源的 openvela / NuttX 硬件适配
教程。它解释稳定概念和取证方法，不承担当前实现状态、任务恢复、路线规划或板端
验收结论的发布职责。

## 教学版本

- 教学基线：contest commit `c588afbd8e0f1d30723f5076e585673a6ace8a4e`
- 最后系统核对：2026-07-27
- 核对范围：仓库/manifest 边界、BK7258 board/chip 分层、GPIO、IRQ bridge 和
  CP/AP 多核基础

这是教学取材版本，不是永久当前版本。每篇课程还会记录自己的 source ref、外部
版本和限制；与当前源码冲突时，以当前源码、resolved config、构建产物和匹配板型的
日期化验收记录为准。

## 路径约定

教程不绑定个人目录。进入 `repo sync` 得到的工作区后，可使用以下逻辑变量：

```bash
cd "<openvela-workspace-root>"
export WORKSPACE="$PWD"
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export LEARN="$CONTEST/docs/learning/bk7258"
```

## 阅读入口

- [BK7258 学习入口](bk7258/README.md)
- [学习路线](bk7258/00-orientation/01-learning-roadmap.md)
- [仓库地图与边界](bk7258/00-orientation/02-repo-map-and-boundaries.md)
- [权威来源地图](bk7258/00-orientation/03-authoritative-source-map.md)
- [教学图索引](bk7258/90-reference/95-graph-index.md)
- [Graphify 安全使用约定](bk7258/assets/graphify/README.md)

当前平台、板型和验收状态分别从
[BK7258 平台入口](../platforms/bk7258/README.md)、`boards/bk7258/CONFIGS.md` 和
`docs/verification/bk7258/` 读取，学习区不复制这些动态事实。

## 内容边界

| 区域 | 回答的问题 | 不承担的职责 |
|---|---|---|
| `docs/learning/bk7258/` | 怎样理解概念、定位源码、区分证据层级 | 当前 profile、进度、板测结论 |
| `docs/platforms/bk7258/` | 跨板构建、启动、更新、交付与历史工程背景 | 单板引脚或 profile 的第二份事实表 |
| `boards/bk7258/`、`chips/bk7258/` | 当前板级策略与 SoC 实现 | 为教学叙事保留旧接口 |
| `docs/verification/bk7258/` | 某个日期和构建身份下发生了什么 | 自动升级为当前状态 |

## 编写规则

1. 每篇教程先记录来源版本，再给解释和简化图。
2. “源码存在、配置选中、进入 ELF/镜像、硬件执行”是四个不同证据层级。
3. 教学图必须能回指来源，并明确省略项；原始工具输出不直接入仓。
4. 新章节有实际内容时才创建，不维护空目录、未来目录树或动态路线图。
5. 修改、构建、下载和板测属于单独实施流程，不作为新手教程的隐含步骤。
