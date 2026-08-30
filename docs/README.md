# 文档分层与导航

[English](README_EN.md) | 简体中文

文档位置按“结论适用范围”划分，避免把某块板的实测结果推广为芯片契约，也避免用
历史工作记录覆盖当前源码、配置和验收状态。

| 层级 | 目录 | 内容与边界 |
|---|---|---|
| Chip / SoC | [`chips/<soc>/`](chips/) | 寄存器、启动核、IRQ、DVFS/PM、SDK ABI、芯片 bootloader 和通用调试契约；不得放单板引脚或单板验收结论 |
| Platform integration | [`platforms/<soc>/`](platforms/) | 跨板卡的 CP/AP 配对、构建交付、符合性、调试方法和按阶段保留的工程记录 |
| Learning | [`learning/`](learning/) | 从源码和已验证结论提炼的教程与心智模型；不发布当前实施状态 |
| Workflow | [`workflows/`](workflows/) | 不依赖某一芯片或板卡的 Git/协作流程 |
| Verification | [`verification/<soc>/`](verification/) | 带构建身份和适用边界的不可变主机/实板验收记录；当前事实还必须与源码/config 一致 |

## BK7258 入口

- [平台集成文档](platforms/bk7258/README.md)：T5AI Core、T5 Board 和 AIDK AI Toy
  共用的交付入口；
- [官方符合性复核](platforms/bk7258/official-compliance-review.md)：官方 1443/1444/1445
  的硬性要求、推荐项、示例目录和架构差异；
- [芯片级文档](chips/bk7258/README.md)：对 BK7258 SoC 成立的稳定契约；
- [学习文档](learning/bk7258/README.md)：面向初学者的渐进式材料；
- [三板配置契约](../boards/bk7258/CONFIGS.md)：当前可维护的 CP/AP 配置入口。

原目录名 `docs/bk7258-t5ai/` 会把跨三块板的内容误解成只适用于 T5-AI，因此已迁移
为 `docs/platforms/bk7258/`。学习区同理由 `docs/learning/bk7258-t5ai/` 改为
`docs/learning/bk7258/`。平台目录中的 `nuttx-port/`、`bootloader-analysis/` 和部分 research
文件保留阶段技术记录属性；当前结论必须回到平台入口、符合性说明、当前源码/config 和
`docs/verification/bk7258/` 复核。
