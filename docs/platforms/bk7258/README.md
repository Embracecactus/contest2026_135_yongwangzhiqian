# BK7258 芯片与板卡集成

[English](README_EN.md) | 简体中文

BK7258 是 SoC；T5AI-Core、T5-Board 和 AIDK AI Toy 是三块物理板。本目录只维护
三板共用的构建、启动、更新、交付和历史工程资料，不把某块板的引脚、外设或实板
结论写成芯片属性。

| 展示名 | 稳定机器标识 | 说明 |
|---|---|---|
| T5AI-Core | `t5ai_core` | 涂鸦 T5AI-Core V1.0.1；历史资料也可能写作 T5-AI |
| T5-Board | `t5_board` | T5-Board V1.0.2 |
| AIDK AI Toy | `aidk_ai_toy` | 原理图名 AIToyBoard；别名不产生第二套板级实现 |

## 当前架构边界

- 系统由独立 CP/AP NuttX 镜像组成；AP 使用物理 CPU1/CPU2 运行 SMP。
- `--boot direct` 只用于未签名的 bring-up/诊断，不是产品发布。
- 签名产品链为 board-owned BL1 → pinned NuttX MCUboot BL2 → 同槽签名 CP/AP，
  使用 A/B 分区模型。
- 有线整机恢复与 apps-only OTA 是两条不同路径。普通构建/整机下载复用明确批准的
  同板信任关系，不自动生成或轮换 BL1/MCUboot 密钥；OTA 绑定目标已安装的信任契约。
  full 包采用与自身代数相同的 floor（635/637），OTA-only 包不替换 BL1/BL2，
  不能混淆两种部署。

完整命令、包格式、accepted-base、设备绑定恢复、密钥生命周期和硬件证据要求统一见
[构建、发布与硬件证据 SOP](nuttx-port/bk7258-build-flash-debug-sop.md)。不要从历史
N15/N17 文档恢复旧地址、旧脚本或旧信任设计。

## 事实所有权

| 事实 | 权威来源 |
|---|---|
| 板型名、引脚、极性、外设实例 | `boards/bk7258/README.md` 与对应物理板目录 |
| CP/AP profile、分区和 release-policy 选择 | `boards/bk7258/CONFIGS.md`、各板 `openvela.conf` 和所选 CSV |
| SoC IRQ、clock、PM、SDK ABI、控制器机制 | [BK7258 chip 文档](../../chips/bk7258/README.md)与 `chips/bk7258/` |
| 公共构建、签名、包、发布和部署命令 | `tools/bk7258/bk7258.py --help` 与上述 SOP |
| 某次构建或板测是否通过 | [日期化 verification](../../verification/bk7258/)中的匹配记录 |
| 许可证与派生来源 | `SOURCE_PROVENANCE.md` |

## 平台文档

- [参赛技术报告](../../contest/技术报告-BK7258三核适配与傻妞AI伴侣.md)：当前作品、量化结果和分版本验收；
- 视频与三板构建入口见仓库 README；三板共享 SoC 实现，T5-Board 运行 Dolphin，
  AIToyBoard 运行傻妞。637 全链路（认领→连接→设置→唤醒→“我在”→对话）已由用户
  实测，见 [637 验收摘要](../../verification/bk7258/2026-09-20-shaniu-637-full-image.md)；
  App OTA 引用 634。
  Agent 既有扩展已发布并由 manifest 固定到 fork `add0db19`；干净复现结果与
  fork 发布、官方合入及实板验收分别报告，见来源记录与 Master Plan。

- [傻妞 AIDK AI Toy 全项目 Master Plan](shaniu-master-plan.md)：统一产品目标、官方框架/
  设备适配、控制 App、模型资产、实际里程碑与验收结果；
- [BKVoice 历史方案](bkvoice-authorized-voice-app.md)：已退出当前主线的 Gateway/音色探索
  及复现来源，不作为当前开发或恢复旧运行时的入口；
- [傻妞 Android companion 范围](shaniu-android-companion-plan.md)：现役 BLE 控制 App 与明确标记的历史方案；
- [RF 校准与工厂烧录规范](rf-calibration-and-factory-provisioning.md)：设备唯一 RF
  数据、Beken 量产/测试工具职责、工位流程及恢复/OTA/通用工厂镜像边界；
- [官方符合性复核](official-compliance-review.md) / [English](official-compliance-review.en.md)：
  2026-08-28 审计快照，解释 openvela 1443/1444/1445 的强制项与架构差异；
- [官网文档适配矩阵](openvela-document-adaptation-matrix.md)：2026-08-28 能力审计快照，
  不是当前路线图；
- [T5-Board P0 调试、xTS 与性能手册](p0-diagnostics-performance.md)：2026-08-27
  诊断/性能 profile 与证据边界；
- [构建、发布与硬件证据 SOP](nuttx-port/bk7258-build-flash-debug-sop.md)：现役操作入口。

## 历史与板级证据

- [移植报告](porting-report.md)记录以涂鸦 T5AI-Core 首板 bring-up 为起点的 BK7258
  芯片移植历史；其中 N1–N15 详细实板证据止于 2026-08-04，不发布当前多板状态；
- [`bootloader-analysis/`](bootloader-analysis/)保留涂鸦与 Beken Bootloader 的
  逆向证据；
- [`nuttx-port/`](nuttx-port/)只保留仍有独立技术价值的故障复盘、源码核验和历史设计；
- [`hardware/t5ai-core/`](hardware/t5ai-core/)保存 T5AI-Core 原理图、验证记录和历史裸机探针。

日期化 verification 文件保留创建时的证据名称，不因后续命名规范而重命名。文件名中的
`BK7258` 只标识 SoC；物理板必须从正文记录的 board、profile、夹具和构建身份判定。

历史资料中的 `board/bk7258/`、旧 profile、绝对主机路径或 N15/N17 地址只描述当时
工作树。当前实现必须回到本页列出的权威来源重新取证。
