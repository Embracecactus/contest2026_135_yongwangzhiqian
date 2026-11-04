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
  full 包的 floor 必须匹配本次包的代数，OTA-only 包不替换 BL1/BL2，
  不能混淆两种部署。

完整命令、包格式、accepted-base、设备绑定恢复、密钥生命周期和硬件证据要求统一见
[构建、发布与硬件证据 SOP](nuttx-port/bk7258-build-flash-debug-sop.md)。不要从历史
N15/N17 文档恢复旧地址、旧脚本或旧信任设计。

## 代码分层与改动入口

| 层 | 代码入口（仓库根相对路径） | 负责的内容 |
| --- | --- | --- |
| 芯片机制 | `chips/bk7258/{common,cp,ap,include}/` | CP/AP 生命周期、IRQ、时钟、内存、无线代理、控制器和跨核通信 |
| 物理板 | `boards/bk7258/{aidk_ai_toy,t5_board,t5ai_core}/` | 引脚、器件、电气策略、产品 profile；各板 `openvela.conf` 选择配置和分区 |
| 上游形态驱动 | `nuttx/` | 标准 NuttX 设备接口与通用驱动覆盖，不承载产品 UI 或板卡分支 |
| 固件产品 | `app/bk7258/`、`app/dolphin/` | 傻妞的 Agent、语音、显示、配置与交互服务；Dolphin 独立产品 |
| 手机 | `android/shaniu-companion/` | 原生 Android，认领、认证 BLE/TLS 会话、配置、审批和 OTA |
| 电脑与维护 | `tools/bk7258/bk7258.py`、`tools/bk7258/_lib/` | 单一 CLI 入口；构建、资源、工作台、签名和交付的领域实现 |
| 构建接线 | `contest2026_135_yongwangzhiqian.xml`、`frameworks/cmake/` | 固定依赖、目录映射及现有框架集成；Agent Session 历史读取安全补丁只对 SHA-256 固定输入的构建副本应用 |
| 验证 | `tests/host/bk7258/`、`app/testing/bk7258/`、`tests/pytest/test_bk7258/` | 主机回归、板上应用与受控硬件自动化，分别报告证据层级 |

设备、手机、电脑与云的运行关系见[项目架构](../../../README.md#系统架构)。
手机和电脑是管理与交互端；设备上的官方 AI Agent 组织云端对话，独立 Python
Gateway 不属于产品运行链路。团队源码通过 manifest 的目录级 `linkfile` 接入官方
工作区；芯片、板、产品代码分别维护，不在上游 checkout 留团队改动。

架构与操作只在本页、对应层文档和 SOP 中维护。已退役的项目记忆、阶段目标及
旧架构决策保存在 Git 历史，历史验收文档的引用固定到对应源码版本。

## 事实所有权

| 事实 | 权威来源 |
|---|---|
| 板型名、引脚、极性、外设实例 | `boards/bk7258/README.md` 与对应物理板目录 |
| CP/AP profile、分区和 release-policy 选择 | `boards/bk7258/CONFIGS.md`、各板 `openvela.conf` 和所选 CSV |
| 板级当前支持与验收边界 | `boards/bk7258/CONFIGS.md`；仓库 README 的“板级配置与当前验收”链接直达 |
| SoC IRQ、clock、PM、SDK ABI、控制器机制 | [BK7258 chip 文档](../../chips/bk7258/README.md)与 `chips/bk7258/` |
| 公共构建、签名、包、发布和部署命令 | `tools/bk7258/bk7258.py --help` 与上述 SOP |
| 某次构建或板测是否通过 | [日期化 verification](../../verification/bk7258/)中的匹配记录 |
| 许可证与派生来源 | `SOURCE_PROVENANCE.md` |

## 平台文档

- [参赛技术报告](../../contest/技术报告-BK7258三核适配与傻妞AI伴侣.md)：赛事材料快照、量化结果和分版本验收；
- [2026-11-03 功能复测与录像](../../verification/bk7258/2026-11-03-feature-validation/README.md)：对应日期的实板、模拟器与日志修复构建证据，不替代当前验收入口；
- 视频与三板构建入口见仓库 README；三板共享 SoC 实现，T5-Board 运行 Dolphin，
  AIToyBoard 运行傻妞。板级现状只在 `boards/bk7258/CONFIGS.md` 汇总；
  固定依赖及发布证据见团队 manifest 与[来源记录](../../../SOURCE_PROVENANCE.md)；后者也记录唯一现役的
  Agent Session 构建副本补丁及其源哈希约束。

- [桌面伙伴行为合同](shaniu-desktop-companion.md)：动作候选、缓存表情、有限触觉及资源预算；
- Android 操作与界面约束见仓库 `android/shaniu-companion/README.md` 及同目录 `DESIGN.md`；
- [RF 校准与工厂烧录规范](rf-calibration-and-factory-provisioning.md)：设备唯一 RF
  数据、Beken 量产/测试工具职责、工位流程及恢复/OTA/通用工厂镜像边界；
- [T5-Board P0 调试、xTS 与性能手册](p0-diagnostics-performance.md)：2026-08-27
  诊断/性能 profile 与证据边界；
- [BKVision 摄像头与连续录像](bkvision-camera-recording.md)：AIDK `bkvision` 命令、分层、
  SD NAND 数据路径与 `18.6.321+381` 录像证据；迭代过程见[连续录像适配](aidk-continuous-recording.md)；
- [构建、发布与硬件证据 SOP](nuttx-port/bk7258-build-flash-debug-sop.md)：现役操作入口。

## 历史与板级证据

- [Master Plan](shaniu-master-plan.md)：历史目标、设计决策与分版本实板记录；
- [BKVoice 历史方案](bkvoice-authorized-voice-app.md)：Gateway/音色探索与复现来源，不作为现役开发入口；
- [9 月 20 日部署输入快照](first-deployment-inputs.md)：旧身份供应、资源与哈希证据；
- [官方符合性复核](official-compliance-review.md) / [English](official-compliance-review.en.md)
  及[官网适配矩阵](openvela-document-adaptation-matrix.md)：2026-08-28 审计快照；
- [移植报告](porting-report.md)记录以涂鸦 T5AI-Core 首板 bring-up 为起点的 BK7258
  芯片移植历史；其中 N1–N15 详细实板证据止于 2026-08-04，不发布当前多板状态；
- [AIDK 2026-09-01 板测证据](aidk-ai-toy-board-test-handoff.md)与
  [2026-09-29 工厂初始化验证](factory-validation-20260929.md)：对应构建身份的实板记录；
- [`bootloader-analysis/`](bootloader-analysis/)保留涂鸦与 Beken Bootloader 的
  逆向证据；
- [`nuttx-port/`](nuttx-port/)只保留仍有独立技术价值的故障复盘、源码核验和历史设计；
- [`hardware/t5ai-core/`](hardware/t5ai-core/)保存 T5AI-Core 原理图、验证记录和历史裸机探针。

日期化 verification 文件保留创建时的证据名称，不因后续命名规范而重命名。文件名中的
`BK7258` 只标识 SoC；物理板必须从正文记录的 board、profile、夹具和构建身份判定。

历史资料中的 `board/bk7258/`、旧 profile、绝对主机路径或 N15/N17 地址只描述当时
工作树。当前实现必须回到本页列出的权威来源重新取证。
