> **事实截止日期**：2026-08-04
> **权威来源**：a. 实际 team-owned 代码、构建产物与原始板测证据；b. memory ADR/规则、progress verification/milestone、阶段 worklog；c. 官方 Beken v3.1.1.9 源码/头文件/静态库与二进制逆向证据（只读）；d. 本指南仅为教学索引，冲突时回到上述来源。
> **证据边界**：本文件不复制动态状态，不发明命令/日志/函数；官方 NuttX/apps/SDK 源码与静态库为只读；动态状态以 [当前状态与下一步](11-current-status-and-next-steps.md) 为唯一来源。

# BK7258 / T5-AI 移植过程新手指南

## 1. 教程目的与读者假设

本指南服务一个具体目标：在 Beken BK7258 / Tuya T5-AI 上，让 openvela/NuttX 以**双镜像**方式运行——一个 CP（Control Processor）NuttX 与一个 AP（Application Processor）NuttX SMP 集群各司其职。

读者假设：
- 完全不懂嵌入式，但会基本命令行与 Git。
- 愿意先建立概念，再碰命令；不追求一次读懂全部。
- 接受“有些结论要等第 11 章才给”，不在前面章节里猜状态。

## 2. “逐行讲解”的范围

本指南会**逐行讲解**的是：
- 引用的关键构建/烧录命令（来自 team-owned 文档，非发明）；
- 需要改的配置文件片段；
- 短小的 wrapper/overlay 代码片段；
- 真实的日志样例（仅当来自已确认资料）。

本指南**不**做的是：
- 抄解整个官方 NuttX / apps / SDK 源码树；
- 凭空写出命令、日志或函数名；
- 把官方只读源码当成可改对象讲解。

## 3. 顺序阅读表（每章一句学习成果）

| 章 | 文件 | 学完你能 |
|----|------|----------|
| 00 | 00-reading-map-and-glossary.md | 建立脑内地图，认识全部术语与状态读法 |
| 01 | 01-hardware-and-boot-chain.md | 认识硬件框图与真实启动链（BootROM→Tier-1→CP→AP） |
| 02 | 02-repository-wrapper-and-build-model.md | 分清 team-owned 可改区与官方只读区，理解构建模型 |
| 03 | 03-tier1-bootloader-and-crc.md | 理解 Tier-1 引导与 32+2 CRC 校验约定 |
| 04 | 04-cp-nuttx-foundation-n1-n6.md | 建立 CP NuttX 基础（N1–N6） |
| 05 | 05-ap-and-smp-n7-n8.md | 理解 AP NuttX SMP 集群起步（N7–N8） |
| 06 | 06-rptun-supervision-rpmsgfs-n9-n11.md | 认识 RPTUN 监管与 RPMsgFS（N9–N11） |
| 07 | 07-bluetooth-n12-n13.md | 认识蓝牙栈分工（N12–N13） |
| 08 | 08-psram-n14.md | 理解 PSRAM 硬件初始化与 role-local heap（N14） |
| 09 | 09-paired-ota-n15.md | 理解配对 OTA 与 fail-closed 兜底（N15） |
| 10 | 10-build-flash-debug-and-evidence.md | 知道构建/烧录/调试与证据查阅位置 |
| 11 | 11-current-status-and-next-steps.md | 查看唯一动态状态入口与下一步 |
| 附录 | appendix-key-files.md | 查关键文件索引（不抄整树） |

## 4. 按问题跳读

- 我想先认识硬件、搞清板子怎么一步步起来 → 01、03、04、05
- 我想知道哪些能改、构建怎么组织 → 02、10
- 两个核怎么说话、内存怎么分 → 06、08
- 蓝牙怎么接 → 07
- 升级怎么保证不刷砖 → 09
- 我卡住了要去哪查命令/文件 → 10、附录（appendix-key-files.md）
- 现在到底做到哪了 → 11（且只信 11）

## 5. 当前状态唯一入口

动态状态（做到哪、卡在哪、下一步）**只**在 [当前状态与下一步](11-current-status-and-next-steps.md) 维护。其他任何章节都不复制它。读指南时若想确认进度，只去这一处。

## 6. 贡献与更新纪律

- **历史章不复制动态状态**：00–10 与附录讲概念、约定和已经发生的阶段/故障复盘；“板子此刻是什么状态、下一步做什么”只写进 11（单一来源）。
- **不改官方树**：正式代码只改 team-owned `wrapper/overlay`；官方 NuttX / apps / SDK 源码与 SDK 静态库保持只读。
- **SDK 版本纪律**：唯一 active SDK = `Beken v3.1.1.9`；Tuya/Beken bootloader 二进制仅作逆向证据参考，不进入运行链决策，也不暗示 Tuya SDK 为 active。
- **不发明事实**：命令、日志、函数名若未在项目资料中出现，本指南不写。

## 7. 相关文档（真实相对链接）

- 上层总览：[BK7258 移植总览](../README.md)
- 技术总报告：[移植报告](../porting-report.md)
- 当前状态：[当前状态与下一步](11-current-status-and-next-steps.md)
- 记忆库：[PROJECT](../../../../memory/PROJECT.md) · [ARCHITECTURE](../../../../memory/ARCHITECTURE.md) · [RULES](../../../../memory/RULES.md) · [OPERATIONS](../../../../memory/OPERATIONS.md)
- 进度：[ROADMAP](../../../../progress/ROADMAP.md) · [CURRENT](../../../../progress/CURRENT.md)
