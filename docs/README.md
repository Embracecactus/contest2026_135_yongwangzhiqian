# 文档分层与导航

[English](README_EN.md) | 简体中文

文档按结论的适用范围分层。单板观察不能升级为 SoC 契约，历史验收也不能覆盖
当前源码、配置和构建产物。

| 层级 | 目录 | 内容与边界 |
|---|---|---|
| Chip / SoC | [`chips/<soc>/`](chips/) | 不依赖具体板卡的寄存器、启动、IRQ、时钟、PM、SDK ABI 和通用调试契约 |
| Platform integration | [`platforms/<soc>/`](platforms/) | 跨板构建、启动与更新模型、交付方法、符合性说明，以及明确标为历史的工程记录 |
| Learning | [`learning/`](learning/) | 带来源版本的教程和心智模型；不发布当前实施进度或板端验收状态 |
| Workflow | [`workflows/`](workflows/) | 不属于特定芯片或板卡的 Git 与协作流程 |
| Verification | [`verification/<soc>/`](verification/) | 带日期、构建身份和适用边界的不可变主机/实板验收记录 |

板型、profile、分区和发布策略是源码/配置事实，不在 `docs/` 复制维护。BK7258 的
权威入口是 `boards/bk7258/CONFIGS.md` 与 `boards/bk7258/README.md`。

## BK7258 入口

- [芯片与板卡集成](platforms/bk7258/README.md)：BK7258 跨板构建、启动、更新和交付入口；
- [芯片级文档](chips/bk7258/README.md)：BK7258 SoC 共用契约；
- [学习文档](learning/bk7258/README.md)：按固定来源版本编写的教程；
- [正式验收记录](verification/bk7258/)：按日期保存的不可变证据；
- `boards/bk7258/CONFIGS.md`：当前支持板卡、profile、分区与配置契约；
- `SOURCE_PROVENANCE.md`：源码、表格、协议和初始化序列的许可证及来源。

阅读历史资料时，以其记录日期和构建身份为边界。凡标题或开头标为“历史”“快照”
或“retired”的内容，只用于解释当时决策，不能作为当前命令、信任根、分区或板型状态。
