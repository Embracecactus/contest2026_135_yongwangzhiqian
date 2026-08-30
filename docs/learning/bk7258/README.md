# BK7258 学习入口

本目录帮助新读者理解 BK7258 在 OpenVela/NuttX 中的仓库边界、启动链、内核接口和
常用外设子系统。这里不维护当前任务、进度或板端验收结论；发生冲突时，以源码、有效
配置和对应板型的正式验证记录为准。

## 权威入口

- 当前支持板卡、profile 与分区：`boards/bk7258/CONFIGS.md`
- 板级引脚和实例策略：`boards/bk7258/README.md`
- SoC 共用契约：[BK7258 chip 文档](../../chips/bk7258/README.md)
- 芯片与板卡集成：[BK7258 平台入口](../../platforms/bk7258/README.md)
- 日期化验收证据：[BK7258 verification](../../verification/bk7258/)
- 队伍目录映射：`contest2026_135_yongwangzhiqian.xml`

完整工作区不是单一 Git 仓库。本文使用以下逻辑变量描述角色，而不绑定用户目录：

```bash
export WORKSPACE="<openvela-workspace-root>"
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export LEARN="$CONTEST/docs/learning/bk7258"
```

## 第一次阅读

1. 阅读[学习路线](00-orientation/01-learning-roadmap.md)，确定学习顺序和退出条件。
2. 阅读[仓库地图与边界](00-orientation/02-repo-map-and-boundaries.md)，区分队伍真源、
   manifest 映射和官方仓库。
3. 阅读[权威来源地图](00-orientation/03-authoritative-source-map.md)，学会区分源码、
   配置、构建产物和实板证据。
4. 再选择一个子系统，只做定位和调用链追踪；修改、构建或烧录属于单独实施流程。

## 课程索引

- [四级初始化：reset、early、late 与 app](30-nuttx-core/30-arch-chip-board-layers.md)
- [IRQ、tick、early UART、serial 与 heap](30-nuttx-core/32-irq-and-critical-sections.md)
- [NSH 与 `board_app_initialize()`](30-nuttx-core/34-bringup-nsh-and-procfs.md)
- [Flash、MTD 与文件系统](40-subsystems/flash-mtd-filesystem/01-mental-model.md)
- [IRQ 与向量桥接](40-subsystems/interrupt-vector/01-mental-model.md)
- [GPIO lower-half](40-subsystems/gpio/01-mental-model.md)
- [SDK 与 NuttX 的边界](40-subsystems/sdk-boundary/01-mental-model.md)
- [UART SDK wrapper 历史教程](40-subsystems/sdk-boundary/02-uart-sdk-wrapper-history.md)
- [CP/AP 多核基础](40-subsystems/multicore-basics/01-mental-model.md)
- [教学图索引](90-reference/95-graph-index.md)

UART 教程记录的是首次迁移过程，适合解释 wrapper 思路，但不能作为当前实现清单。
当前串口、SDK profile 和构建行为仍须回到源码及本次构建证据确认。

## 证据纪律

- 文件存在只证明源码层事实；有效 `.config` 才证明一次构建选择了它。
- 编译成功不等于最终 ELF、镜像或下载包包含该路径；继续检查 map、符号和包清单。
- 旧板测只证明当时板型和镜像，不自动覆盖其他板卡或当前配置。
- SDK、NuttX、外部资料和队伍仓分别记录版本；不要笼统写“工作区最新版”。
- 教学图和历史教程用于理解关系，不发布 current 状态、下一步或恢复指针。

## 维护边界

新增教学内容前先确认现有课程是否已经回答同一问题。稳定概念放在 learning；当前
硬件/配置事实放在 boards 或平台入口；一次验收结果放在 verification。删除或移动文档后
必须检查所有本地链接，不新建空壳文件维持已经过时的导航。
