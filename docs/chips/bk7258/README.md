# BK7258 chip 层文档

本目录对应源码 [`chips/bk7258/`](../../../chips/bk7258/)，只收录不依赖某一块板卡
原理图、引脚或单板配置的 SoC 共用契约与调试资料。

| 文档 | 定位 | 时效 |
|---|---|---|
| [SDK 时钟 OPP 与每核频率契约](sdk-clock-operating-points.md) | CP/AP/Bus 频率、电压、DVFS/PM、DWT 和外设时钟口径 | 当前权威契约 |
| [SDK 上下文索引](sdk-context-index.md) | BK7258 SDK header/startup/linker/driver 的检索快照 | 历史索引；当前来源仍以 manifest 为准 |
| [chip 代码评审与清理指导](chip-code-review-cleanup-guide.md) | 迁移前 chip 目录的逐项静态审计与后来状态勘误 | 历史评审；不是当前待办清单 |
| [J-Link/SWD 调试指南](jlink-swd-debug-guide.md) | Cortex-M33 fault、寄存器、断点和 BK7258 启动调试 | 通用方法；具体接线与端口由板级文档给出 |

板卡硬件、T5-Board/T5AI-Core/AIDK profile、COM 口、下载边界和实板结论仍从
[BK7258/T5-AI 平台文档](../../platforms/bk7258/README.md)进入。动态状态与正式验收以
[`progress/CURRENT.md`](../../../progress/CURRENT.md)、当前 manifest、resolved config 和
权威分区 CSV 为准。

## 归档规则

- 修改 `chips/bk7258/` 的共享 ABI、IRQ、clock、PM、bootloader 或 SDK wrapper 时，
  同步更新本目录；
- 只在某一块板上验证过的结果必须写明板型，并放在板级文档或 `progress/verification/`；
- 历史文档保留当时事实，但必须在开头注明已经被哪份当前契约取代；
- 不把 SDK OPP 名称直接当成某个物理 CPU 的 MHz。
