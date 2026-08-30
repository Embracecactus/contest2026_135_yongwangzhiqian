# BK7258 SDK 时钟适配与 CPU0 240 MHz 实板验证

- 日期/时区：2026-08-27，Asia/Shanghai
- 分支：`fix/bk7258-sdk-clock-adaptation`
- 集成起点：`e612875`
- 目标板：T5-Board V1.0.2
- 下载与控制台：COM3，BK Loader 2.1.11.15 / UART0 115200 8N1
- SDK：manifest 固定 Beken AVDK SMP `release/v3.1.1.9`
- 结论：CP/CPU0 已从误用 SDK OPP 320M 时的 160 MHz，修正为 SDK OPP 240M
  对应的 240 MHz；构建、签名全量下载、稳定回读、冷启动以及 30 个独立 benchmark
  session 全部通过。

OPP 的长期软件契约见
[BK7258 SDK 时钟 OPP 与每核频率契约](../../chips/bk7258/sdk-clock-operating-points.md)。

## 1. 问题与官方结论

旧性能 profile 选择了名为 `320M` 的共享 SoC OPP，并把名字误当成 CPU0 频率。
固定 SDK 的正式表实际为：

| SDK OPP | CP/CPU0 | AP/CPU1、CPU2 | Bus |
|---|---:|---:|---:|
| 240M | 240 MHz | 240 MHz | 240 MHz |
| 320M | 160 MHz | 320 MHz | 160 MHz |
| 480M | 240 MHz | 480 MHz | 240 MHz |

因此“官方 480 MHz”指 AP 核在 OPP 480M 下的频率，不是 CP 所在 CPU0 的频率。
CPU0 在固定 SDK 正式 OPP 中的上限是 240 MHz。CP 性能镜像应选择 OPP 240M；AP
音视频路径仍必须保留 OPP 320M/480M，不能把全局最大 OPP 截断为 240M。

权威实现位于固定 SDK 的 `sys_hal_switch_cpu_bus_freq_high_to_low()`、
`sys_hal_switch_cpu_bus_freq_low_to_high()`、`sys_hal_cpu_clk_div_set()` 和
`sys_hal_core_bus_clock_ctrl()`。本次没有采用“取消 CPU0 `/2` 后直接跑 480 MHz”的
私有超频方案，因为该状态不属于 SDK 正式 OPP，也没有配套的电压、总线和稳定性契约。

## 2. 适配范围

本次修正不限于 boot/bringup，而是统一了整个 chip 层的 OPP 语义：

1. 性能 profile 从 `CONFIG_BK7258_CLOCK_320M` 改为
   `CONFIG_BK7258_CLOCK_240M`，在 `nx_start()` 前通过 DVFS lower-half 选择正式
   OPP 240M。
2. DVFS 对外采用 `BK7258_OPP_*`、`set_opp/get_opp`；旧 `BK7258_FREQ_*` 和
   `set_freq/get_freq` 只作为 ABI 兼容别名保留。
3. OPP 0..6 全部保留，并明确 CPU0/AP/Bus 三套实际频率映射。PM wire enum 数值与
   布局保持不变，项目调用者改用 `BK7258_PM_OPP_*` 表达真实语义。
4. clockdiag、DWT/perf 和 Ethernet HCLK 查询返回当前角色/总线的实际 Hz，而不是
   OPP 名称。SDK 当前 MDIO 固定 DIV62，动态 HCLK 代码在固定 SDK 中被 `#if 0`
   关闭；若未来启用 ETH LPI 与运行时 DVFS，仍需增加 notifier 重算 `MAC1USTCR`。
5. `/proc/dvfs` 输出 `cur_opp/cpu0_hz/ap_hz/bus_hz`，写入以 OPP 编号为单位；旧
   `cur_freq <opp>` 仅为调试脚本兼容入口。
6. CP 本地 OPP 切换与 AP start/stop/restart 共用 AP 控制锁；只有 AP 为 OFF 或
   STOPPED 时允许 CP 发起切换，避免 AP 运行时的 DWT 时基失效，也关闭了状态检查与
   AP start 之间的竞态。AP 自己通过 PM vote 请求 320M/480M 的路径不受影响。
7. BL1 仍按 SDK early-init 语义完成 DPLL 和 120 MHz 交接，不在 bootloader 中提前
   选择运行时性能 OPP或改变 Flash/PSRAM 独立时钟。

## 3. 构建门禁与产物身份

最终使用 MCUboot clean build，layout identity 为 `bk7258-2b86faab14dca06e`。
resolved config 确认 `CONFIG_BK7258_CLOCK_240M=y`、`-O3`、CoreMark、Ramspeed、
Whetstone 和 SDK IRQ bridge 开启；AP autostart、Wi-Fi、RPTUN、WDT、Trace、调度
监控和调试 probe 关闭。

| 字段 | 值 |
|---|---|
| CP role | `bk7258-role-b5559a18749d1dee` |
| CP config SHA-256 | `501fb2c9322be8d41f125f0beab2e7f62e30befe5ba43c01ca393b8c5dd6d65f` |
| CP ELF | 1,387,584 bytes; `731d253ef3cad2eedd703726230073b2087c26260d22151d82df42c492c10930` |
| CP bin | 215,044 bytes; `61fbe1bb255311c99f4990af8b59d244e9fdeeefd9925747404151bc154c4080` |
| AP role | `bk7258-role-56ca787b1de3b763` |
| AP config SHA-256 | `3d40200a0fecbb540bf9d1b0373b403c74a1b879714db7c659bbf6995cff7ad7` |
| AP ELF | 2,704,264 bytes; `4b589c655fd2a1d2ae82c63e5d8bea2217c330badeb74f687678c193f10f3d75` |
| AP bin | 1,045,248 bytes; `c7830b71a0969870222a676786b0eb62809c360709c72d331c89d1931697ce86` |
| BL1 ELF / bin SHA-256 | `858c60587e904dbfa522730d15ba1d410b0801e570dc8ea6ba208ee701ae3b63` / `b93c6f5e0c9400127e9b2157e3cb37d4ec38b0f26efba8fb46ba4671a0c55020` |
| BL2 ELF / bin SHA-256 | `20313984d6bec0d6ed7ea51cd87da9bbe7e49ab4b09332db695b79549069869e` / `8f76a0483b397a28316638e9b95182ab32f9d1af6d78a514bd8306a8e6c5c29b` |

补充验证：drivercheck direct CP/AP、AIDK direct CP/AP、AIDK CP 完整链接、性能
MCUboot clean build 均通过；开启 `CONFIG_BK7258_DVFS_PROCFS` 的定向编译以及 AP
control 定向编译通过。最终代码复核未发现 OPP/ABI 映射错误、锁反转或错误路径漏锁。

## 4. Generation 146 信任与全量下载

| 字段 | 值 |
|---|---|
| version / MCUboot / BL1 counter | `1.88.2+146` / 146 / 146 |
| rollback floor | 146 |
| BL1 public SPKI SHA-256 | `2c442c25153ab769d14f74a57b4477f661e20e8ceefa19f3d1392e20476621d7` |
| MCUboot public SPKI SHA-256 | `d4b4e0917f1dda7d76eb1ec78a6d1b9bed42a1219575029b9aaff9ecd2216a02` |
| signed package | 7,984,249 bytes; `679bf9a24901ea34b42dfcf5fc9b8920f916afc43034719ea3ddf0b361b25afc` |
| operator image | `0x7fa000` bytes; `630cb700ec4bead0ecb14660356a89ca1760b3eea87e3c12bd9c6ffd8ae3760e` |

本代在新的 0700 临时目录中分别生成 BL1 与 MCUboot P-256 密钥，私钥权限 0600，
两套密钥不同且未打印、未提交。package structure、8 张签名 image、BL1/BL2/CP/AP
公开信任链以及 flash contract 均为 PASS。下载、回读、启动和 benchmark 证据接受后，
该临时私钥目录已精确删除并确认不存在；私钥不可恢复，签名包和公开指纹保留。

下载前从板上重新读取 `[0x000000,0x7fa000)` 作为唯一 materialize 基底：

- base SHA-256：`5ab60db0139c0ae59520680d912aabaa95af10e1fd12a5ffa0b7c986baf84600`；
- `usr_config [0x4fc000,0x50a000)`：
  `5e3d970426c42f666f017097cb09ab036e0e3d9a8bc6f4c10f41f28905ea8114`；
- Agent persistent `[0x561000,0x7fa000)`：
  `ef3393a63494d5bb6883ff8455d2803ef94e8d9379c48bc637ad4a0c9f1a347f`。

COM3 只向 BK Loader 提交一个地址 0 的 `0x7fa000` operator 文件，未执行 chip
erase；loader 报告 `EraseFlash ->pass`、`WriteFlash ->pass`、`Enprotect pass`、
`Writing Flash OK` 和 `All Finished Successfully`。输入文件不包含
`[0x7fa000,0x800000)`，未触碰 immutable/calibration tail、OTP/eFuse、lifecycle 或
debug lock。

## 5. 启动与下载后回读

最终 RTS 150 ms 冷启动依次出现 BL1 primary、BL2 CP/AP 验签和 handoff、`B2APOK`、
`NuttShell (NSH)`，未出现 HardFault、ASSERT、panic 或 ERROR。原始串口 SHA-256：
`db70a8cf425fc537e3a13883a17d31d9fad17b98598a8bc177a6d27a032eb23f`。

6 Mbit/s 的第一次全量回读出现分散噪声，不能作为 Flash 一致性证据，已明确废弃。
随后以 460800 bit/s 稳定回读，文件 SHA-256 为
`35574044eb027b8b19ff2e7a57ef86919b8c999c379add7c2ea6f2a378017e03`。
与 operator 比较时只有 6 个物理字节不同：

```text
[0x164fde,0x164fdf)  [0x164ffe,0x165000)
[0x285fde,0x285fdf)  [0x285ffe,0x286000)
```

两组各由一个 trailer 数据字节和该 32-byte BK7258 CRC 包的两个 CRC 字节组成。
按 32+2 编码逆映射，两个数据字节分别是 CP logical slot `0x13ffe0` 和 AP logical
slot `0x10ffe0`，恰好是各自 trailer 的 `copy_done` 偏移。
签名镜像契约要求 primary slot 初始 `copy_done=0xff`、`image_ok=1`；BL2 direct-XIP
启动后只允许把可见 CP/AP 主槽的标准 MCUboot `copy_done` 写成 1，因此上述变化是
预期启动状态提交，不是代码损坏。除这两次状态提交外全部字节一致；下载后的
`usr_config` 和 Agent persistent 仍逐字节相同。

J-Link 在 1 MHz 与 100 kHz 都因物理 RESET/SWD 连接状态报告
`RESET (pin 15) high, should be low`，因此本轮没有伪造或引用 live 寄存器读数。频率
结论由固定 SDK 表、resolved config 和下面的线性实测共同闭环。

## 6. CoreMark 10 次

命令：`coremark`；1 thread；10,000 iterations。10 次均输出
`Correct operation validated`。

| Run | CoreMark | UART raw SHA-256 |
|---:|---:|---|
| 1 | 561.482313 | `5fae7f06a615141bd41b7bfd2f7df07bb0393b5cfba77fb16c345ccbcac66a12` |
| 2 | 561.482313 | `2df150d50ae7eb264726aedfe456b0b49615b25c408acc93e11fe418d85c5733` |
| 3 | 561.482313 | `ba7e9f2a1a2aa1db17074d160d26f5fbef5c9881dac83718fb78758f78d7ab1f` |
| 4 | 561.482313 | `198b835348a2b8db90bd0f17fc9146f96d9e6bf887370625569e91050adc7ed6` |
| 5 | 561.797753 | `1072f4d5878b5e8084d2e078c21747c47fc04b7ab81acb3e6795d24c803f32b3` |
| 6 | 561.482313 | `57cdd9daf9448d28a2272b87886ebd267260c566f73440e6d0851d2f4bf13763` |
| 7 | 561.482313 | `d8f6fc307e1040835b340e30f1599dc1bfb39d4557ffa8aceaa1b84449b21b52` |
| 8 | 561.797753 | `6b8fbe65899fe640c960062f5c10c9e171b186df3f37e2ae789929bc1ba82112` |
| 9 | 561.482313 | `4a86c04cf7999293bacd7df21d4dcb74185e5a13ceb16a7bd1a79ee436f6332f` |
| 10 | 561.797753 | `7a48c524669d331039479b1b8f2a8ee6b933b4a556417d24ba518d67d60ec1e4` |

统计：mean 561.576945、median 561.482313、min 561.482313、max 561.797753、
population stddev 0.144553。generation 145 的 160 MHz mean 为 374.307544；本代
提升 50.030891%，比值 1.500308914。归一化后旧值为 2.339422 CoreMark/MHz，本代
为 2.339904 CoreMark/MHz，IPC 基本不变。这是“实际从 160 MHz 到 240 MHz”而非
仅修改配置名的直接实测证据。

## 7. Ramspeed 10 次

命令：`ramspeed -a -s 65536 -n 1000`；中断开启。每次都完成 48 行结果，下表记录
64 KiB 行，顺序为 system memcpy / internal memcpy / system memset / internal
memset，单位为程序输出的 KB/s。

| Run | Sys memcpy | Int memcpy | Sys memset | Int memset | UART raw SHA-256 |
|---:|---:|---:|---:|---:|---|
| 1 | 155826.954 | 248968.144 | 233714.824 | 533009.086 | `b0d5b3f4516e43f6886382ee7eaee82b7fac735e8592eb46c8a884a0e821f60c` |
| 2 | 155828.851 | 248991.390 | 233702.876 | 533009.086 | `a2cf179f233b4a54a991429d4c6912c0d877c642dbc31580679be6f2190d7ae1` |
| 3 | 155826.954 | 248968.144 | 233713.971 | 533009.086 | `8ec4c7ffa920cdbfc92b80c13164bdd206c0034256d973d43b59b9f2a6216316` |
| 4 | 155826.575 | 248969.112 | 233713.971 | 533009.086 | `ae159ff403f62186d81bbb590d7faee7216a29f3dcda84092a4bc8719dbafe07` |
| 5 | 155826.954 | 248968.144 | 233679.837 | 533009.086 | `32bf7101840af6e1a75c9b39686aa8cf097b992c6a9a12f2f3f7ca28ec295c6d` |
| 6 | 155826.575 | 248968.144 | 233713.117 | 533009.086 | `fff1f61f57f7488586d87f5c9872c3ea042fb104162f2c81b31aee24266b2b3d` |
| 7 | 155828.851 | 248990.422 | 233703.729 | 533009.086 | `21b7b8fc4893ddcd12388fcaebc71ebebb6170c0a2f4b4690bc018771025b746` |
| 8 | 155826.954 | 248969.112 | 233683.250 | 533009.086 | `a78953eaa1511e0b691a221fea5fb55b442b0f4ffbe1498d73e0a81b3235c657` |
| 9 | 155826.954 | 248968.144 | 233683.250 | 533009.086 | `3fccaac819808079134fc84b234e59adbb61c9ee51d23af1388a073c21cca81b` |
| 10 | 155826.575 | 248968.144 | 233684.956 | 533009.086 | `e1d99585d1cfe0524e6d6c0e6ccaa2910fa1b95408108d6576e9cd02506d3c21` |

| 统计 | Sys memcpy | Int memcpy | Sys memset | Int memset |
|---|---:|---:|---:|---:|
| mean | 155827.220 | 248972.890 | 233699.378 | 533009.086 |
| median | 155826.954 | 248968.144 | 233703.303 | 533009.086 |
| min | 155826.575 | 248968.144 | 233679.837 | 533009.086 |
| max | 155828.851 | 248991.390 | 233714.824 | 533009.086 |
| population stddev | 0.832 | 9.018 | 14.123 | 0.000 |

相对 generation 145 的四项 mean，比值依次为 1.500386、1.500383、1.500392、
1.500373，与 240/160=1.5 一致，也证明 CPU0 与 Bus 已落到 240 MHz OPP。

## 8. Whetstone 10 次

命令：`whetstone 1000`。10 次均为 3290 ms；UART raw SHA-256 依次为：

```text
e5414c763d2be96fb2674edefa809f315d27230580f5aa86bd08673f4ddd709d
39684ab1381b7ef0f7574ccdbbce6e51c95c031dce9052118cfb5a5cb61f2519
877b842030f2fd65388c418cf4feaabe6478394cfda6fecf0adec74511bf361d
cbef0f95740dd0cf699dcd72af98bdf48644395f20ba145d5be9d96bfea5e67e
cbbe89396714ab722610f44d06ed11074360f1ef4263d6002a1be3870aa3b1e3
e28de74bb789017cf33eae9d52903a9b95c3d9511aaf887dc06098a10ddbc0b0
defbad24ddc9c8f646f6a3d23d6712d6607277adc1598659eefa27ba39ae81b2
08ed8eff98fc3d70c28ea53085788b877b54978234da67861ae5c219e546481e
0e19f47be2692461b0bdaca1f7f0f1338bfd459b6170caa086120da070985cb1
ab548e481a77543195670ab247dbbfc65b5f6c1df8eb914d6587d772b7926f05
```

上游程序仍因把毫秒再次乘 1000 而打印错误的 `0.0 KIPS`，本次未修改只读 `apps/`。
按源码注释要求的公式 `100 * loops * iterations / duration_ms` 换算，每次以及 mean
均为 30.395137 MIPS。相对旧 22.866551 MIPS 提升 32.924%；Whetstone 混合浮点与
库实现，不用它单独反推时钟，CPU 频率判定采用 CoreMark 和四项均线性增长的
Ramspeed。

## 9. 验收结论

- SDK OPP 语义、CP/AP/Bus 映射、DVFS/PM/clockdiag/AP 并发门禁：PASS；
- clean build、签名包、公开信任链、flash contract：PASS；
- 新密钥 COM3 单文件全量下载及禁止区边界：PASS；
- 460800 bit/s 下载后回读与两类持久区保护：PASS；
- 冷启动：PASS；
- CoreMark/Ramspeed/Whetstone 各 10 次：PASS；
- CP/CPU0 从 160 MHz 修正到 SDK 正式 240 MHz：**已完成并实板验证**；
- AP OPP 320M/480M 动态 vote 的双核实板切换仍属于后续产品 profile 回归，不影响
  本次 CP 性能频率问题的关闭。
