# 文档分层与导航

本文定义仓库文档的长期归档规则。文档位置应与源码所有权一致，避免把 SoC 共用契约
误写成某一块板的实现，也避免把单板实测结论推广到整个芯片系列。

| 文档层级 | 目录 | 应包含的内容 | 不应包含的内容 |
|---|---|---|---|
| Chip / SoC | [`chips/<soc>/`](chips/) | 寄存器、启动核、IRQ、DVFS/PM、SDK ABI、芯片 bootloader 与通用调试契约 | 某块板的原理图、引脚、COM 口和仅在单板完成的验收结论 |
| Board / 产品目标 | 当前 BK7258/T5-AI 入口 [`bk7258-t5ai/`](bk7258-t5ai/) | 板卡硬件、profile、下载边界、功能矩阵和实板证据 | 声称对所有 BK7258 板型都成立的 SoC 契约 |
| 学习材料 | [`learning/`](learning/) | 从源码和已验证结论提炼的教程、心智模型与索引 | 替代动态状态或权威分区/config |
| 历史设计/计划 | [`superpowers/`](superpowers/)、[`ai-worklog/`](ai-worklog/) | 当时的方案、计划、提示词和取舍过程 | 冒充当前实现或发布门禁 |
| 动态证据 | [`../progress/`](../progress/) | 当前任务、里程碑、构建与实板验证记录 | 反向定义源码或分区要求 |

当前 BK7258 代码也按相同边界拆分：SoC 共用实现位于 `chips/bk7258/`，板级 profile、
引脚和 bringup 位于 `boards/bk7258/`。文档引用源码时应使用这两个当前路径；历史记录
若保留旧路径，必须明确标注它描述的是迁移前状态。

本次从旧的 `docs/bk7258-t5ai/` 提取了四份纯 chip 文档：

- [BK7258 chip 文档索引](chips/bk7258/README.md)；
- SDK OPP 与每核频率契约；
- SDK 上下文索引；
- chip 代码清理评审和 J-Link/SWD 指南。

`docs/bk7258-t5ai/` 暂时保留为 T5-AI 平台、板卡与历史实板证据的兼容入口，避免在
一次迁移中改写大量历史引用。新增纯 chip 文档不得再放回该目录。

其中 `probe/` 明确绑定 T5-AI UART/loader 实测；`bootloader/` 同时包含 Tuya T5 与
BK 官方 binary 的对照逆向；`nuttx-port/` 大量记录具体 T5 板型、profile 和实板
generation。它们虽然包含 chip 技术内容，但不是“对所有 BK7258 板卡无条件成立”的
纯 SoC 文档，因此本次不拆分。以后若抽取共享结论，应在 `docs/chips/bk7258/` 新建
精炼契约，并从历史 worklog 链接过去。
