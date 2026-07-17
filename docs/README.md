# 文档索引

本仓库支持多块开发板的 openvela 适配。文档按硬件分顶层目录组织。

## 硬件适配总览

| 硬件 | 状态 | 文档入口 |
|---|---|---|
| **RV1126B HPMCU**（RISC-V，AMP） | P1 NSH baseline + P2-A RPMsg 已板端验证 | [rv1126b-hpmcu/](rv1126b-hpmcu/adaptation/nsh-port.md) |
| **BK7258 T5-AI**（Cortex-M33，三核） | 技术探索阶段（bootloader 逆向 + SMP 可行性） | [bk7258-t5ai/](bk7258-t5ai/README.md) |

## 跨硬件

- [Skills 使用说明](cross-cutting/skills-usage.md) — hardware-adaptation Skills 集合如何应用于各硬件

## 目录约定

```
docs/
├── README.md                    本索引
├── rv1126b-hpmcu/               RV1126B HPMCU 适配
│   ├── adaptation/              移植指南、SDK 集成、适配研究
│   ├── worklog/                 AI worklog + 阶段恢复 prompts
│   ├── review/                  静态评审报告
│   ├── verification/            板端验证记录
│   └── next-stage-prompts/      上下文恢复提示词
├── bk7258-t5ai/                 BK7258 T5-AI 适配
│   ├── bootloader/              bootloader 逆向与实现
│   └── nuttx-port/              NuttX 移植
└── cross-cutting/               跨硬件文档
```
