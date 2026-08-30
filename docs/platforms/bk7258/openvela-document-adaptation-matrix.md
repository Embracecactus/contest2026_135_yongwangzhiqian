# BK7258 × openvela 官网文档适配审计矩阵（2026-08-28 快照）

## 1. 文档目的

本文把 openvela 中文官网 dev-ai-contest-2026 版本的目录逐项映射到 BK7258 平台
（T5AI-Core、T5-Board、AIDK AI Toy）在 2026-08-28 的交付状态，用于回答三个问题：

> 本文是固定日期的审计快照，不是当前状态表或后续路线图。当前事实以源码、
> resolved config、构建 manifest、`boards/bk7258/CONFIGS.md` 和更新日期的
> verification 记录为准。

1. 哪些能力已经进入当前维护源码和配置，并有可追溯证据；
2. 哪些能力只有部分实现、历史证据或尚缺实板闭环；
3. 哪些页面只是通用参考，或因板卡没有对应硬件而不应进入适配待办。

官网入口：

- [openvela 开源项目首页（1423）](https://doc.openvela.com/document?id=1423&version=dev-ai-contest-2026&language=cn)
- 审计版本：dev-ai-contest-2026 / 中文
- 审计日期：2026-08-28
- 集成起点：`ecc1c0a185896d6afce165d20ebbf1a270782683`；审计输入也包含当时的 P0 xTS 工作区差异
- 审计日之后的交付状态以 `boards/bk7258/CONFIGS.md`、manifest、维护 defconfig、
  分区 CSV 和新日期 verification 为准。
- 1443/1444/1445 的强制项、推荐项、模板目录和架构差异判定见
  [官方符合性复核（中文）](official-compliance-review.md) /
  [English](official-compliance-review.en.md)。

本文审计了官网完整目录的 320 个节点。详细适配矩阵覆盖“芯片移植”“设备开发”
“调试”“测试开发”；其余目录按用途归类，避免把应用教程或 API 说明误判为 BSP 缺口。

## 2. 状态定义

| 状态 | 含义 | 能否宣称完成 |
|---|---|---|
| ✅ 已完成 | 当前维护源码和 profile 已包含，且有匹配的构建或实板证据 | 可以，但只能在已记录边界内 |
| 🟡 部分完成 | 已有实现，但缺模式、外设、当前分支回归、上游依赖或实板验收 | 不可以整体宣称完成 |
| 🔴 待适配 | 对当前产品有价值，但当前维护 profile 中没有闭环 | 不可以 |
| 🕰 历史能力 | 曾在旧提交或旧 profile 板测，不在当前维护交付中 | 不可以按当前版本宣称完成 |
| ⚪ 参考项 | NuttX/openvela 通用能力，不要求 BK7258 写专用适配 | 不作为 BSP 待办 |
| ➖ 不适用 | 当前 SoC/板卡没有对应硬件，或页面只针对 SIM/其他开发板 | 不作为待办；需记录原因 |

“源码存在”“编译通过”“旧日志曾通过”都不自动等于“当前版本已完成”。状态升级为
✅ 至少要满足：当前配置可达、干净构建通过、产物身份可记录，并按风险完成主机或实板验收。

## 3. 官网完整目录处置

| 官网目录 | 文档范围 | 对本项目的处置 |
|---|---:|---|
| 了解 openvela | 1423–1424 | ⚪ 项目背景和术语参考 |
| 快速入门 | 1426–1441 | ⚪ Ubuntu/工具参考；SIM 和其他开发板页面为 ➖，不形成 BK7258 缺口 |
| 芯片移植 | 1443–1445 | 纳入 §4 逐项审计 |
| 设备开发 | 1447–1608 | 纳入 §4–§8 逐项审计 |
| 应用开发 | 1611–1617 | ⚪ 示例应用参考；当前作品使用官方 Agent，不要求逐个移植示例 |
| 调试 | 1620–1646 | 纳入 §9 逐项审计 |
| 测试开发 | 1648 | 纳入 §9，作为发布闭环重点 |
| 贡献 | 1650–1653 | ⚪ PR、文档、风格和三方声明流程参考 |
| API 参考 | 1655–1737 | ⚪ 调用接口参考；仅在产品启用对应服务时重新打开适配项 |
| FAQ | 1739–1740 | ⚪ 故障排查参考 |

## 4. 芯片、构建和内核

| 官网文档 | 状态 | 截至审计日的证据与边界 | 审计日未覆盖边界 |
|---|---|---|---|
| [1443 新平台适配](https://doc.openvela.com/document?id=1443&version=dev-ai-contest-2026&language=cn) | ✅ 功能 / 🟡 模板目录 | CP/AP/CPU2、串口、IRQ、timer、heap、板级生命周期、链接输入和统一构建入口均为当前交付；产品使用 `openvela_cp` + `openvela_ap`，CP 已启用 NSH，但没有字面 `configs/nsh` 单镜像目录 | 保持配对构建回归；只有评审强制目录逐字匹配时才增加 CP-only diagnostic `nsh` |
| [1444 中断适配](https://doc.openvela.com/document?id=1444&version=dev-ai-contest-2026&language=cn)、[1468 中断系统](https://doc.openvela.com/document?id=1468&version=dev-ai-contest-2026&language=cn) | ✅ | 64 路 SDK source、80 槽完整 IRQ 表、RAM vector、SDK IRQ bridge、GPIO IRQ、SMP IPI 已实现；minimal vector 是可选内存优化，当前一一映射有界且稳定 | 保持 IRQ、GPIO、优先级和 SMP 回归；IRQ 数量显著增长时再评估 minimal vector |
| [1445 Vendor 代码仓](https://doc.openvela.com/document?id=1445&version=dev-ai-contest-2026&language=cn) | ✅ 功能 / 🟡 模板目录 | 团队代码只落在 contest 仓并通过 manifest/linkfile 消费；NuttX 和 SDK 保持上游/只读边界。链接脚本按 SoC 共用，ROMFS 只含当前启用的 rc 文件，没有照抄示例中的全部可选文件 | 公共 NuttX 改动继续单独提 PR；只有启用账户数据库时才增加 passwd/group 和 RCRAWS |
| [1448 CMake](https://doc.openvela.com/document?id=1448&version=dev-ai-contest-2026&language=cn)、[1449 CMake 维护](https://doc.openvela.com/document?id=1449&version=dev-ai-contest-2026&language=cn)、[1450 Kconfig](https://doc.openvela.com/document?id=1450&version=dev-ai-contest-2026&language=cn)、[1451 Makefile](https://doc.openvela.com/document?id=1451&version=dev-ai-contest-2026&language=cn) | ✅ 维护 profile / 🟡 未选组合 | 统一 CLI 对 CP/AP 均调用官方 `build.sh ... --cmake`；CMake 的三个生成循环覆盖 Make 字面差集中的 13 个驱动，现有 Ninja 规则可见维护 profile 启用对象。未穷举所有关闭/组合配置，不把未选组合宣称为等价验证 | 新增或改名驱动时保持 Make/CMake source gate 同步，并用维护 profile 的构建产物回归 |
| [1453 RTOS 入门](https://doc.openvela.com/document?id=1453&version=dev-ai-contest-2026&language=cn)、[1454 内核开发](https://doc.openvela.com/document?id=1454&version=dev-ai-contest-2026&language=cn)、[1455 异步编程](https://doc.openvela.com/document?id=1455&version=dev-ai-contest-2026&language=cn) | ⚪ | 通用 NuttX 编程模型；当前已有线程、SMP、work queue 和 IPC 实板用例 | 不写 BK 专用替代实现 |
| [1456 启动流程](https://doc.openvela.com/document?id=1456&version=dev-ai-contest-2026&language=cn) | ✅ T5-Board / 🟡 其他板型 | 当前代码和 clean build 支持三块板的 BL1 → MCUboot BL2 → 同槽签名 CP/AP、ROMFS 双脚本和 final-init；T5-Board 当前签名整机已有 [Agent 验收](../../verification/bk7258/2026-08-24-bk7258-openvela-agent-ap.md)，AIDK/T5AI-Core 当前只按构建/包验证计，不借用 T5-Board 实板结论；开发期链路边界见 [启动链证据矩阵](../../verification/bk7258/2026-08-07-bk7258-boot-chain-evidence-matrix.md) | 各板型分别记录 clean/factory/warm/cold；未跑的板型保持 🟡 |
| [1457 时间系统](https://doc.openvela.com/document?id=1457&version=dev-ai-contest-2026&language=cn) | 🟡 | T5-Board AP 已有 AON RTC、时区和 localtime；SoC 供电时不需要 RTC 电池。断电后没有独立后备电源保持 UTC | 若产品要求断电保持时间，增加带电池 RTC、网络校时或 CP 持久化同步 |
| [1459 Syslog](https://doc.openvela.com/document?id=1459&version=dev-ai-contest-2026&language=cn)、[1460 printf 规范](https://doc.openvela.com/document?id=1460&version=dev-ai-contest-2026&language=cn)、[1461 日志排障](https://doc.openvela.com/document?id=1461&version=dev-ai-contest-2026&language=cn) | ✅ / ⚪ | CP 为 RPMsg Syslog server，AP 为 client，前缀、PID、优先级、时间戳和缓冲已配置；printf/排障页作编码规范 | 后续只做压力、掉线重连和日志丢失统计 |
| [1463 Trace](https://doc.openvela.com/document?id=1463&version=dev-ai-contest-2026&language=cn) | ✅ | 维护的交互式诊断/console CP profile 启用调度、IRQ、switch 和 DWT perf Trace；P0 诊断 generation 143 实板 dump 为 55,920 bytes/679 行，含 IRQ entry/exit；独立性能 profile 按低噪声契约关闭 Trace；操作见 `boards/bk7258/README.md` | 发布时保留有界采样，性能采样继续使用独立 profile |
| [1465 内存管理](https://doc.openvela.com/document?id=1465&version=dev-ai-contest-2026&language=cn) | ✅ 当前 heap / 🕰 容量基线 | 当前 T5 Agent 使用 512 KiB AP PSRAM system heap；generation 149 的 CP xTS profile 另用 64 KiB role-local PSRAM system heap，Umem total 精确增加 64 KiB，mm 8/8 与 sched 16/16 实板通过。16 MiB 全容量边界仍来自 [历史 N14 板测](../../verification/bk7258/2026-08-03-n14-psram-board-verification.md) | 产品 profile 做长稳和碎片统计；需要全容量声明时重跑 N14 门禁。当前 xTS 证据见 [g149 记录](../../verification/bk7258/2026-08-27-bk7258-p0-xts-completion.md) |
| [1469 Cortex-M 中断嵌套](https://doc.openvela.com/document?id=1469&version=dev-ai-contest-2026&language=cn) | 🟡 | 当前 BASEPRI、异常栈和无切换恢复路径已稳定；未启用 ARCH_HIPRI_INTERRUPT，SDK 设备 IRQ 被约束为不可相互抢占 | 只有出现明确零延迟需求时，才设计 HIPRI/嵌套压力测试 |
| [1471 原子操作](https://doc.openvela.com/document?id=1471&version=dev-ai-contest-2026&language=cn)、[1472 信号量](https://doc.openvela.com/document?id=1472&version=dev-ai-contest-2026&language=cn) | ⚪ / ✅回归 | 使用 NuttX 通用实现；AP 双核原子、信号量、远程唤醒和 ping-pong 已有实板证据 | 不另写 SoC 私有 API |
| [1474 Pipe](https://doc.openvela.com/document?id=1474&version=dev-ai-contest-2026&language=cn)、[1475 工作队列](https://doc.openvela.com/document?id=1475&version=dev-ai-contest-2026&language=cn)、[1476 消息队列](https://doc.openvela.com/document?id=1476&version=dev-ai-contest-2026&language=cn) | ⚪ / ✅回归 | NuttX 通用能力；generation 149 的 FIFO interlock、FIFO、PIPE redirection、PIPE 和 scheduler 16/16 实板通过，网络/媒体 wrapper 使用 work queue | 保持当前代回归；消息队列专项仍按上层需求补充 |
| [1479 VirtIO 简介](https://doc.openvela.com/document?id=1479&version=dev-ai-contest-2026&language=cn)、[1480 VirtIO 框架](https://doc.openvela.com/document?id=1480&version=dev-ai-contest-2026&language=cn) | ⚪ | 当前 BK7258 CP/AP transport 直接使用 RPTUN/OpenAMP/RPMsg，不依赖单独 VirtIO 设备适配 | 不列为产品待办 |
| [1482 RPMsg](https://doc.openvela.com/document?id=1482&version=dev-ai-contest-2026&language=cn) | ✅ | mailbox、RPTUN、RPMsg、RPMsgFS、health、Syslog、OTA 和 Wi-Fi VNET 均基于当前跨核链路 | 保持重连、负载和故障注入回归 |
| [1483 RPMsg Clock](https://doc.openvela.com/document?id=1483&version=dev-ai-contest-2026&language=cn) | 🔴 可选 | 当前没有 CLK_RPMSG 配置或 openvela RPMsg Clock server/client；现有跨核 PM/时钟由 BK wrapper 协调 | 仅在 AP 需要按名字远程控制 CP 时钟资源时适配 |

## 5. 总线、外设、文件系统和系统组件

| 官网文档 | 状态 | 截至审计日的证据与边界 | 审计日未覆盖边界 |
|---|---|---|---|
| [1485 驱动开发](https://doc.openvela.com/document?id=1485&version=dev-ai-contest-2026&language=cn)、[1486 最佳实践](https://doc.openvela.com/document?id=1486&version=dev-ai-contest-2026&language=cn)、[1488 总线驱动](https://doc.openvela.com/document?id=1488&version=dev-ai-contest-2026&language=cn) | ⚪ | 通用 lower-half、upper-half、注册和生命周期规范，现有 wrapper 按此边界实现 | 用于后续驱动评审 |
| [1490 UART](https://doc.openvela.com/document?id=1490&version=dev-ai-contest-2026&language=cn) | ✅ | CP UART0 为生产控制台/下载路径，RX、NSH、时钟恢复和引脚切换已有实板证据 | 增加物理 UART loopback xTS；不要在 SWD 开关位置误启 UART1 |
| [1492 I2C 起步](https://doc.openvela.com/document?id=1492&version=dev-ai-contest-2026&language=cn)、[1493 Master](https://doc.openvela.com/document?id=1493&version=dev-ai-contest-2026&language=cn)、[1496 验证](https://doc.openvela.com/document?id=1496&version=dev-ai-contest-2026&language=cn) | ✅ / 🟡 | AP I2C master wrapper 已驱动 GT1151 和 camera，触控实板通过 | 增加独立总线错误、超时和恢复用例 |
| [1494 I2C Slave](https://doc.openvela.com/document?id=1494&version=dev-ai-contest-2026&language=cn)、[1495 Bit-Bang](https://doc.openvela.com/document?id=1495&version=dev-ai-contest-2026&language=cn) | 🔴 可选 | 当前产品不需要 slave 或 bit-banging，维护 profile 未启用 | 只有硬件需求出现时再适配 |
| [1498 USB Device](https://doc.openvela.com/document?id=1498&version=dev-ai-contest-2026&language=cn) | 🟡 | USB 相关 wrapper/历史代码存在，但当前产品 profile 和当前板测没有形成 Device/CDC 闭环；板上原生 USB-A 与 Type-C CH342 不是同一控制器路径 | 先确认目标是原生 host 还是 device，再做枚举、传输、重插和复位测试 |
| [1499 USB SIM](https://doc.openvela.com/document?id=1499&version=dev-ai-contest-2026&language=cn) | ➖ | SIM 驱动，不是 BK7258 实板适配 | 可作为上层协议主机测试参考 |
| [1501 PCI](https://doc.openvela.com/document?id=1501&version=dev-ai-contest-2026&language=cn)、[1502 PCI EPC](https://doc.openvela.com/document?id=1502&version=dev-ai-contest-2026&language=cn)、[1503 PCIe Host](https://doc.openvela.com/document?id=1503&version=dev-ai-contest-2026&language=cn) | ➖ | BK7258 MCU 和当前板卡没有 PCI/PCIe | 不适配 |
| [1505 Arch Timer](https://doc.openvela.com/document?id=1505&version=dev-ai-contest-2026&language=cn) | ✅ | TIMER_ARCH、SysTick/perf 计数、SDK timer wrapper 和自测已构建/板测 | 保持时钟切换与溢出回归 |
| [1506 Arch Alarm](https://doc.openvela.com/document?id=1506&version=dev-ai-contest-2026&language=cn) | 🔴 可选 | 当前没有独立 Arch Alarm lower-half 验收，现有需求由 timer/RTC/wdog 完成 | 只有 tickless 或高精度唤醒需求出现时适配 |
| [1508 外设总览](https://doc.openvela.com/document?id=1508&version=dev-ai-contest-2026&language=cn) | ⚪ | 作为能力清单；具体状态以本表和 [驱动缺口记录](../../verification/bk7258/2026-08-19-bk7258-missing-drivers.md) 为准 | 不单独实现 |
| [1510 GPIO 应用](https://doc.openvela.com/document?id=1510&version=dev-ai-contest-2026&language=cn)、[1511 GPIO 驱动](https://doc.openvela.com/document?id=1511&version=dev-ai-contest-2026&language=cn) | ✅ | GPIO lower-half、P12 key、GPIO IRQ 和示例已接入；板上 LED、SWD、LCD、camera 存在引脚复用约束 | 完成 jumper 输入/输出 xTS，按板型维护冲突表 |
| [1513 GNSS](https://doc.openvela.com/document?id=1513&version=dev-ai-contest-2026&language=cn)、[1514 Goldfish GNSS](https://doc.openvela.com/document?id=1514&version=dev-ai-contest-2026&language=cn) | ➖ | 当前板卡无 GNSS；Goldfish 只面向 Emulator | 不适配 |
| [1516 Sensor 框架](https://doc.openvela.com/document?id=1516&version=dev-ai-contest-2026&language=cn)、[1517 Sensor 配置](https://doc.openvela.com/document?id=1517&version=dev-ai-contest-2026&language=cn)、[1518 Sensor 驱动](https://doc.openvela.com/document?id=1518&version=dev-ai-contest-2026&language=cn)、[1519 厂商 Sensor](https://doc.openvela.com/document?id=1519&version=dev-ai-contest-2026&language=cn) | 🟡 / 🔴 | SARADC/ADC-key 有实现和部分证据，但缺新鲜物理电压/按键跃迁；SoC 温度只有历史记录，当前维护交付未闭环；通用 Sensor upper-half 未启用 | 先完成 ADC-key 实物测试；产品若需要统一传感器 API，再接 Sensor 框架 |
| [1521 文件系统](https://doc.openvela.com/document?id=1521&version=dev-ai-contest-2026&language=cn)、[1522 存储框架](https://doc.openvela.com/document?id=1522&version=dev-ai-contest-2026&language=cn)、[1523 MTD](https://doc.openvela.com/document?id=1523&version=dev-ai-contest-2026&language=cn) | ✅ | ROMFS、procfs、raw flash、MTD、LittleFS、RPMsgFS 均为当前交付并有重启/读写证据 | 完成 fstest 长稳与掉电策略测试 |
| [1524 块设备](https://doc.openvela.com/document?id=1524&version=dev-ai-contest-2026&language=cn) | 🟡 | T5-Board TF/FAT 已在 Agent 全镜像通过；当前设计是 fixed-media，插卡后需复位，不宣称热插拔；AIDK SD NAND 缺实板闭环 | 明确 fixed-media UX，或增加 card-detect/热插拔设计 |
| [1526 uORB](https://doc.openvela.com/document?id=1526&version=dev-ai-contest-2026&language=cn)、[1527 uORB Topic](https://doc.openvela.com/document?id=1527&version=dev-ai-contest-2026&language=cn)、[1528 uORB 配置](https://doc.openvela.com/document?id=1528&version=dev-ai-contest-2026&language=cn) | 🔴 可选 | 当前产品没有启用 uORB | 只有多传感器发布/订阅需求出现时适配 |

## 6. 网络、蓝牙和 Telephony

| 官网文档 | 状态 | 截至审计日的证据与边界 | 审计日未覆盖边界 |
|---|---|---|---|
| [1531 网络栈](https://doc.openvela.com/document?id=1531&version=dev-ai-contest-2026&language=cn) | 🟡 | T5 Agent AP 当前 profile 已启用 NET/TCP/UDP/DNS/Wi-Fi VNET，当前整机实板只确认 Wi-Fi init；STA、DHCP、ping 属历史 N16 证据，不能代替当前 Agent 全链回归 | 补当前 Agent 的连接、DHCP、ping、断网重连、吞吐和长稳 |
| [1532 网络驱动](https://doc.openvela.com/document?id=1532&version=dev-ai-contest-2026&language=cn) | 🟡 | Wi-Fi VNET 是当前产品路径；Ethernet MAC wrapper 仅 drivercheck 构建通过，T5AI-Core/T5-Board 均没有外置 RMII PHY | Wi-Fi 做实板吞吐/重连；Ethernet 要么配 PHY carrier 验收，要么明确 ➖ |
| [1534 ifconfig](https://doc.openvela.com/document?id=1534&version=dev-ai-contest-2026&language=cn)、[1535 iperf](https://doc.openvela.com/document?id=1535&version=dev-ai-contest-2026&language=cn)、[1536 tcpdump](https://doc.openvela.com/document?id=1536&version=dev-ai-contest-2026&language=cn)、[1537 curl](https://doc.openvela.com/document?id=1537&version=dev-ai-contest-2026&language=cn)、[1538 iperf2](https://doc.openvela.com/document?id=1538&version=dev-ai-contest-2026&language=cn)、[1539 iperf3](https://doc.openvela.com/document?id=1539&version=dev-ai-contest-2026&language=cn)、[1540 ftp](https://doc.openvela.com/document?id=1540&version=dev-ai-contest-2026&language=cn) | 🔴 测试项 | 当前维护 Agent profile 没有形成这些工具的统一板测矩阵 | 优先加入 ifconfig/ping/iperf；其余按 ROM 和产品需要选择 |
| [1542 SocketCAN](https://doc.openvela.com/document?id=1542&version=dev-ai-contest-2026&language=cn)、[1543 SIL SocketCAN](https://doc.openvela.com/document?id=1543&version=dev-ai-contest-2026&language=cn) | 🟡 | CAN lower-half 和内部 loopback 已验证；SocketCAN 用户 API、外部收发器和 SIL 流程未形成当前完整证据 | 有 CAN 产品需求时完成外部总线和 SocketCAN 工具闭环 |
| [1545 蓝牙概述](https://doc.openvela.com/document?id=1545&version=dev-ai-contest-2026&language=cn)、[1546 蓝牙驱动](https://doc.openvela.com/document?id=1546&version=dev-ai-contest-2026&language=cn)、[1547 功能测试](https://doc.openvela.com/document?id=1547&version=dev-ai-contest-2026&language=cn)、[1548 bttool](https://doc.openvela.com/document?id=1548&version=dev-ai-contest-2026&language=cn)、[1549 enable](https://doc.openvela.com/document?id=1549&version=dev-ai-contest-2026&language=cn)、[1550 disable](https://doc.openvela.com/document?id=1550&version=dev-ai-contest-2026&language=cn)、[1551 state](https://doc.openvela.com/document?id=1551&version=dev-ai-contest-2026&language=cn)、[1552 set](https://doc.openvela.com/document?id=1552&version=dev-ai-contest-2026&language=cn)、[1553 get](https://doc.openvela.com/document?id=1553&version=dev-ai-contest-2026&language=cn) | 🕰 / 🔴 | HCI、GAP、GATT 和 BLE 扫描/广播曾有实板证据，源码仍可追溯；但相关功能提交不属于当前基线祖先，当前维护 profile 未启用，不能按现版本宣称完成 | 恢复到维护 profile，重新 clean build、全量下载、扫描/广播/GATT 回归 |
| [1555 概述](https://doc.openvela.com/document?id=1555&version=dev-ai-contest-2026&language=cn)、[1556 配置](https://doc.openvela.com/document?id=1556&version=dev-ai-contest-2026&language=cn)、[1557 RIL](https://doc.openvela.com/document?id=1557&version=dev-ai-contest-2026&language=cn)、[1558 API](https://doc.openvela.com/document?id=1558&version=dev-ai-contest-2026&language=cn)、[1560 telephonytool](https://doc.openvela.com/document?id=1560&version=dev-ai-contest-2026&language=cn)、[1561 radio/modem](https://doc.openvela.com/document?id=1561&version=dev-ai-contest-2026&language=cn)、[1562 call](https://doc.openvela.com/document?id=1562&version=dev-ai-contest-2026&language=cn)、[1563 sim](https://doc.openvela.com/document?id=1563&version=dev-ai-contest-2026&language=cn)、[1564 sms/cbs](https://doc.openvela.com/document?id=1564&version=dev-ai-contest-2026&language=cn)、[1565 network](https://doc.openvela.com/document?id=1565&version=dev-ai-contest-2026&language=cn)、[1566 ims](https://doc.openvela.com/document?id=1566&version=dev-ai-contest-2026&language=cn) | ➖ | 当前板卡没有蜂窝 modem、SIM 或 RIL 硬件 | 不适配；未来外挂 modem 时重新评估 |

## 7. 图形与音视频

| 官网文档 | 状态 | 截至审计日的证据与边界 | 审计日未覆盖边界 |
|---|---|---|---|
| [1568 Display](https://doc.openvela.com/document?id=1568&version=dev-ai-contest-2026&language=cn)、[1569 Framebuffer](https://doc.openvela.com/document?id=1569&version=dev-ai-contest-2026&language=cn)、[1570 LCD](https://doc.openvela.com/document?id=1570&version=dev-ai-contest-2026&language=cn)、[1571 VSync](https://doc.openvela.com/document?id=1571&version=dev-ai-contest-2026&language=cn) | ✅ | T5-Board RGB LCD、DMA2D、RGB565 双页、FB_SYNC、EOF page flip 和 LVGL Agent UI 已实板接受 | 做长时间翻页/撕裂/内存压力回归 |
| [1572 Input](https://doc.openvela.com/document?id=1572&version=dev-ai-contest-2026&language=cn)、[1573 getevent](https://doc.openvela.com/document?id=1573&version=dev-ai-contest-2026&language=cn) | 🟡 | GT1151 原始输入继续使用未修改的 NuttX GT9XX ABI；T5-Board 私有 LVGL 设备适配器只补充该单点面板所需的能力查询，UI 操作已实板通过；getevent 尚未单独记为发布验收 | 在干净上游上补 getevent 坐标/中断与 Agent UI 回归，不修改通用 GT9XX 驱动 |
| [1575 Media Framework](https://doc.openvela.com/document?id=1575&version=dev-ai-contest-2026&language=cn)、[1576 服务端](https://doc.openvela.com/document?id=1576&version=dev-ai-contest-2026&language=cn)、[1577 客户端](https://doc.openvela.com/document?id=1577&version=dev-ai-contest-2026&language=cn)、[1578 Mediatool](https://doc.openvela.com/document?id=1578&version=dev-ai-contest-2026&language=cn) | ⚪ / 🟡 | 当前官方 Agent 通过 recorder/player ABI 使用 NuttX audio；没有把完整 media server/client/mediatool 作为 BSP 发布门禁 | 只有上层产品依赖完整 Media Framework 时再启用 |
| [1581 Audio 配置](https://doc.openvela.com/document?id=1581&version=dev-ai-contest-2026&language=cn)、[1582 原理](https://doc.openvela.com/document?id=1582&version=dev-ai-contest-2026&language=cn)、[1583 适配](https://doc.openvela.com/document?id=1583&version=dev-ai-contest-2026&language=cn)、[1584 测试](https://doc.openvela.com/document?id=1584&version=dev-ai-contest-2026&language=cn) | ✅ | MIC/AUD、录音器、PCM player、共享 ADC/DAC ownership、PTT start/stop 和 teardown 已在 Agent 实板接受 | 增加长录放、异常关闭和音质指标 |
| [1586 Camera](https://doc.openvela.com/document?id=1586&version=dev-ai-contest-2026&language=cn)、[1587 Camera 测试](https://doc.openvela.com/document?id=1587&version=dev-ai-contest-2026&language=cn) | 🕰 / 🟡 | DVP/PWM MJPEG 曾验证，但不是当前 Agent 默认路径；camera 与 RGB LCD/触控存在 pinmux 冲突 | 先定义显式运行时 pinmux 切换和互斥策略，再做当前全镜像实板回归 |
| [1589 V4L2 M2M](https://doc.openvela.com/document?id=1589&version=dev-ai-contest-2026&language=cn)、[1590 Codec 驱动](https://doc.openvela.com/document?id=1590&version=dev-ai-contest-2026&language=cn)、[1591 FFmpeg](https://doc.openvela.com/document?id=1591&version=dev-ai-contest-2026&language=cn)、[1592 nxcodec](https://doc.openvela.com/document?id=1592&version=dev-ai-contest-2026&language=cn) | 🟡 | JPEG V4L2 M2M 已实板；H264、FFmpeg/nxcodec 和连续回归不在当前产品门禁 | 按产品编码需求补 H264/FFmpeg/nxcodec 端到端测试 |

## 8. 安全、电源和端侧 AI

| 官网文档 | 状态 | 截至审计日的证据与边界 | 审计日未覆盖边界 |
|---|---|---|---|
| [1594 安全配置](https://doc.openvela.com/document?id=1594&version=dev-ai-contest-2026&language=cn) | 🟡 | 软件信任根下的 BL1/BL2/CP/AP 全链签名、同槽配对和回滚计数已完成；没有烧写 OTP/eFuse，也不宣称硬件不可篡改 Secure Boot、TEE 或设备唯一密钥 | 比赛阶段保持可恢复开发边界；生产 provisioning 必须单独授权和评审 |
| [1596 PM 框架](https://doc.openvela.com/document?id=1596&version=dev-ai-contest-2026&language=cn)、[1597 PM 驱动](https://doc.openvela.com/document?id=1597&version=dev-ai-contest-2026&language=cn) | 🟡 | CP/AP coordinated standby、AON RTC wake、活动票据、DVFS 和 WDT 已实现并有基础板测 | 完整状态矩阵、并发外设、性能和长稳仍需闭环 |
| [1598 Wakelock](https://doc.openvela.com/document?id=1598&version=dev-ai-contest-2026&language=cn)、[1599 PM Procfs](https://doc.openvela.com/document?id=1599&version=dev-ai-contest-2026&language=cn) | 🔴 | 当前有 BK 自有 activity/vote 机制，但未形成官网所述 wakelock/procfs 可观测接口的发布验收 | 增加统一状态查询和泄漏检查 |
| [1600 IDLE PM](https://doc.openvela.com/document?id=1600&version=dev-ai-contest-2026&language=cn)、[1601 pm_idle](https://doc.openvela.com/document?id=1601&version=dev-ai-contest-2026&language=cn)、[1602 up_cpu_wfi](https://doc.openvela.com/document?id=1602&version=dev-ai-contest-2026&language=cn) | 🟡 | CP/AP idle、WFI、BASEPRI/PRIMASK 边界和协调唤醒已有实现；未完成所有负载状态与长稳 | 与网络、TF、LCD、audio 并发做功耗/唤醒矩阵 |
| [1603 时钟框架](https://doc.openvela.com/document?id=1603&version=dev-ai-contest-2026&language=cn) | 🟡 | 运行时 DVFS、DWT 刷新和 SDK clock ownership 已处理；没有把所有外设建模为通用 openvela clk tree | 当前 wrapper 足够；仅在需要 RPMsg Clock/通用 clk consumer 时扩展 |
| [1605 TFLite Micro](https://doc.openvela.com/document?id=1605&version=dev-ai-contest-2026&language=cn)、[1606 集成](https://doc.openvela.com/document?id=1606&version=dev-ai-contest-2026&language=cn)、[1607 环境](https://doc.openvela.com/document?id=1607&version=dev-ai-contest-2026&language=cn)、[1608 模型转换](https://doc.openvela.com/document?id=1608&version=dev-ai-contest-2026&language=cn) | 🔴 可选 | 当前仓库没有 TFLite Micro runtime、模型或维护 profile；现有 Agent 走云端 ASR/LLM 路径 | 只有产品决定做端侧推理时再估算 ROM/RAM/PSRAM 和算子集 |

## 9. 调试、性能、压力测试和 xTS

| 官网文档 | 状态 | 截至审计日的证据与边界 | 审计日未覆盖边界 |
|---|---|---|---|
| [1620 GDB](https://doc.openvela.com/document?id=1620&version=dev-ai-contest-2026&language=cn) | 🟡 / ⚪ | 已有 J-Link、RTT、只读内存/寄存器和自动化硬件调试 SOP；没有把标准 GDB 会话作为当前发布门禁 | 需要源码级停机调试时再补可复现 GDB 配置 |
| [1621 VSCode SIM](https://doc.openvela.com/document?id=1621&version=dev-ai-contest-2026&language=cn) | ➖ | 面向 SIM，不是 BK7258 实板适配 | 可供上层应用主机调试 |
| [1623 Backtrace](https://doc.openvela.com/document?id=1623&version=dev-ai-contest-2026&language=cn) | ✅ 非破坏性 / 🟡 fault | generation 143 已对当前 task 和 PID 1 输出符号化回溯；generation 149 当前代 `dumpstack` 再次输出 `up_backtrace`、`sched_dumpstack`、`dumpstack_main` 等符号 | 另行设计可恢复的受控 fault；不得用未授权空指针破坏当前 Agent 数据 |
| [1624 Allsyms](https://doc.openvela.com/document?id=1624&version=dev-ai-contest-2026&language=cn) | ✅ | Allsyms 已链接到 XIP FLASH，避免占用运行期 SRAM；generation 143 的专项和 generation 149 clean build/符号化 dumpstack/最终冷启动均通过；见 [P0 记录](../../verification/bk7258/2026-08-27-bk7258-p0-diagnostics-performance.md)与 [g149 记录](../../verification/bk7258/2026-08-27-bk7258-p0-xts-completion.md) | 后续关注符号表 ROM 增量和链接脚本回归 |
| [1626 ASan](https://doc.openvela.com/document?id=1626&version=dev-ai-contest-2026&language=cn)、[1628 LSan](https://doc.openvela.com/document?id=1628&version=dev-ai-contest-2026&language=cn) | 🟡 host / 🔴 target 可选 | 完整 host fixture 已执行；BL1 policy 目标使用 ASan+UBSan，嵌入式 profile 未启用，且 host sanitizer 不能替代实板内存压力 | 逐步扩大可主机编译模块的 sanitizer 覆盖；不强行纳入小 ROM 生产镜像 |
| [1627 Fortify](https://doc.openvela.com/document?id=1627&version=dev-ai-contest-2026&language=cn) | 🔴 | 当前维护 profile 未形成 _FORTIFY_SOURCE 构建和负例测试 | 建立独立 hardened profile，解决告警后再决定生产启用 |
| [1630 J-Link GDB 插件](https://doc.openvela.com/document?id=1630&version=dev-ai-contest-2026&language=cn) | 🟡 | J-Link 实板基础设施和线程外的 SWD/RTT 调试成熟；openvela 线程感知 GDB 插件未单独验收 | 与 1620 合并为一次调试工具闭环 |
| [1632 硬件性能](https://doc.openvela.com/document?id=1632&version=dev-ai-contest-2026&language=cn) | ✅ CP 性能基线 | generation 146 已按 SDK OPP 240M 在 T5-Board 验证 CP/CPU0/Bus 240/240/240 MHz；新密钥全量下载、稳定回读、冷启动及 CoreMark/Ramspeed/Whetstone 各 10 次通过。CoreMark mean 561.576945，较旧 160 MHz 基线提升 50.030891%；AP OPP 480M/480 MHz 能力保留 | 后续只做 AP 320M/480M 动态 vote 产品 profile 回归；见 [chip 层 OPP 契约](../../chips/bk7258/sdk-clock-operating-points.md)和 [generation 146 记录](../../verification/bk7258/2026-08-27-bk7258-sdk-clock-240m-validation.md) |
| [1633 irqinfo/critmon](https://doc.openvela.com/document?id=1633&version=dev-ai-contest-2026&language=cn) | ✅ 可观测 / 🟡 告警 | generation 143 的 critmon daemon 可 start、输出 task/runtime 统计并 stop；generation 149 再次确认 IRQ 11/15/20/70/79 有效计数。resolved config 仅 `MAXTIME_THREAD=0`，其余六类为 `-1`，未配置或验证非零超限告警预算 | 用真实负载确定非零阈值，再覆盖 GPIO、timer、mailbox、LCD 的告警测试 |
| [1634 cpuload](https://doc.openvela.com/document?id=1634&version=dev-ai-contest-2026&language=cn) | ✅ 当前功能 | generation 143 中 `cpuload -p 50` 后 task=49%、`/proc/cpuload=56.5%`；`kill -9` 后 PID 消失并回落到 7.5% | 12 小时长稳恢复后再与 PM/Agent 联测 |
| [1636 Dhrystone](https://doc.openvela.com/document?id=1636&version=dev-ai-contest-2026&language=cn)、[1637 CoreMark](https://doc.openvela.com/document?id=1637&version=dev-ai-contest-2026&language=cn)、[1638 CacheSpeed](https://doc.openvela.com/document?id=1638&version=dev-ai-contest-2026&language=cn)、[1639 ramspeed](https://doc.openvela.com/document?id=1639&version=dev-ai-contest-2026&language=cn)、[1640 Tinymembench](https://doc.openvela.com/document?id=1640&version=dev-ai-contest-2026&language=cn)、[1641 whetstone](https://doc.openvela.com/document?id=1641&version=dev-ai-contest-2026&language=cn) | 🟡 汇总 / ✅ 三项 | generation 146 已在 CP 240 MHz 完成 CoreMark、Ramspeed、Whetstone 各 10 次，CoreMark mean=561.576945；Whetstone 明确记录上游毫秒单位换算缺陷。Dhrystone/TinyMemBench 仍依赖未锁定网络源码，CacheSpeed 缺 BK 通用 cache capability | 已支持三项只做回归；其余先锁定来源/补 cache 契约再启用 |
| [1643 blktest](https://doc.openvela.com/document?id=1643&version=dev-ai-contest-2026&language=cn)、[1644 memstress](https://doc.openvela.com/document?id=1644&version=dev-ai-contest-2026&language=cn)、[1645 fstest](https://doc.openvela.com/document?id=1645&version=dev-ai-contest-2026&language=cn)、[1646 opus_ramtest](https://doc.openvela.com/document?id=1646&version=dev-ai-contest-2026&language=cn) | 🟡 汇总 | generation 143 的 10 秒 memstress 通过；generation 149 的 `mm`、4 KiB `ramtest` 和 RAM tmpfs `scanftest` 164/0 当前代通过。fstest 仍只有历史证据，blktest/opus_ramtest 未执行，避免破坏 Agent/TF 数据 | 在专用可恢复介质上执行 fstest/blktest；为 opus_ramtest 建独立 RAM 预算；长时 memstress 与 soak 后续恢复 |
| [1648 自测试框架](https://doc.openvela.com/document?id=1648&version=dev-ai-contest-2026&language=cn) | 🟡 | [现役 host fixture](../../verification/bk7258/2026-08-27-bk7258-host-regression-fixture.md) 已完成公共门禁 + 281/281 cmocka；[generation 149](../../verification/bk7258/2026-08-27-bk7258-p0-xts-completion.md) 已在标准 16 KiB runner 下完成 mm 8/8、sched 16/16、ostest 状态 0、getprime、mm/ramtest、scanftest 164/0、hello/pipe 和最终冷启动 | 剩余 LIBCXX、AP RTC/timer/RNG、GPIO/UART loopback、driver_test、受控 fault；破坏性存储独立隔离；12h soak 已由 owner 延期 |

## 10. 复核、全量下载和证据规则

后续复核若要升级某项状态，应遵循以下规则，避免旧镜像、旧密钥或旧日志被误认为新证据：

1. 以当前 manifest、维护 defconfig 和分区 CSV 重新 clean build；记录源码提交、
   配置哈希、包版本、rollback counter 和最终镜像 SHA-256。
2. 每次经授权的全量下载都创建全新的 trust generation：BL1 与 MCUboot 分别生成
   新的临时 P-256 密钥对，把新公钥嵌入干净构建，再签完整 BL1/BL2/CP/AP 链。
   上一次全量下载使用的私钥不得复用。
3. 临时私钥只放在权限 0600 的临时目录，不打印、不写入日志、不记录路径、不提交；
   包验证和实板验收完成后删除。仓库只记录公钥指纹等公开证据。
4. 下载前只读 preflight 必须匹配上一代已接受目标；新包独立完成全链内部信任校验，
   rollback counter 严格递增。不要要求尚未安装的新公钥匹配旧板。
5. 使用目标记录所绑定的下载端口和 BK Loader/bk_loader，对地址零处的一份完整
   Flash operator image 执行全量下载；该镜像必须从同一设备 accepted base 物化，
   不 chip erase，并逐字节保留策略要求的校准、device-unique 与 immutable 数据。
6. 记录下载器的单输入、擦写成功文本、最终 boot/readback、新 generation、公钥指纹、
   CP/AP readiness 和本工作项的功能门禁。仅有“编译成功”或“Writing Flash OK”
   都不能宣称板级完成。
7. apps-only 下载继续绑定板上已安装的公开信任契约，不能与 fresh-key 全量下载流程混用。

## 11. 再审计方法

需要重新审计官网、代码或板测变化时：

1. 修改本文的审计日期和代码基线；
2. 更新对应行的状态、当前证据和退出条件；
3. 新建一份 docs/verification/bk7258/YYYY-MM-DD-*.md 保存可复现命令、产物身份、
   实际结果、失败边界和未覆盖项；
4. `boards/bk7258/CONFIGS.md` 只维护板型/profile/分区与构建契约，不复制长日志或
   本文的能力矩阵；
5. 历史通过但不在当前维护 profile 的能力改标为 🕰，完成当前分支回归后再恢复为 ✅。

官网目前由页面动态加载目录和正文。后续复核可以使用以下只读入口；它们是
2026-08-27 观察到的网站接口，不视为长期稳定 API：

- 目录：https://doc.openvela.com/doc/getMenu?version=dev-ai-contest-2026&language=cn
- 正文：https://doc.openvela.com/doc/getContent?id=文档ID&version=dev-ai-contest-2026&language=cn

若接口结构变化，以 [官网首页](https://doc.openvela.com/document?id=1423&version=dev-ai-contest-2026&language=cn)
可见目录和页面正文为准，并重新记录节点总数、版本和审计日期。

这样本文负责“官网能力全景图”，平台入口、源码和配置负责“现在正在做什么”，
verification 文档负责“为什么可以相信这个结论”，三者职责不会混在一起。
