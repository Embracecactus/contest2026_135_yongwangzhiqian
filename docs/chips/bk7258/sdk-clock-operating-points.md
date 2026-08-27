# BK7258 SDK 时钟 OPP 与每核频率契约

本文记录 BK7258 时钟适配的当前事实来源和软件契约。后续修改启动、DVFS、性能基线、
AP 音视频、总线定时或板级配置时，应先对照本文和 manifest 固定的 SDK 源码，不能把
SDK 档位名称直接当成 CP/CPU0 的实际频率。

## 1. 权威来源

仓库 manifest 固定 Beken AVDK SMP `release/v3.1.1.9`。频点的权威实现是：

- `cp/middleware/soc/bk7258/hal/sys_hal.c`
  `sys_hal_switch_cpu_bus_freq_high_to_low()`；
- 同文件 `sys_hal_switch_cpu_bus_freq_low_to_high()`；
- `sys_hal_cpu_clk_div_set()` 和 `sys_hal_core_bus_clock_ctrl()`；
- `cp/middleware/soc/bk7258/hal/sys_types.h` 的电压映射。

SDK 源码对 `PM_CPU_FRQ_480M` 的注释是
`cpu0:240m;cpu1:480m;cpu2:480m;bus:240m`，对 `PM_CPU_FRQ_320M` 的注释是
`cpu0:160m;cpu1:320m;cpu2:320m;bus:160m`。这两个枚举是共享 SoC OPP 名称，
不是 CPU0 MHz。

## 2. 固定 SDK OPP 表

以下是当前端口必须逐字段保持的非 DCO、非 ATE 分支：

| SDK OPP | CPU0 | CPU1/CPU2 | Bus | `cksel_core` | `clkdiv_core` | CPU0 divider | VDDD | VDDDIG |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 26M | 26 MHz | 26 MHz | 26 MHz | `0` | `0` | `/1` | `0x6` | `0xB` |
| 60M | 60 MHz | 60 MHz | 60 MHz | `3` | `7` | `/1` | `0x6` | `0xB` |
| 80M | 80 MHz | 80 MHz | 80 MHz | `3` | `5` | `/1` | `0x6` | `0xB` |
| 120M | 120 MHz | 120 MHz | 120 MHz | `3` | `3` | `/1` | `0x6` | `0xC` |
| 240M | 240 MHz | 240 MHz | 240 MHz | `3` | `1` | `/1` | `0x6` | `0xD` |
| 320M | 160 MHz | 320 MHz | 160 MHz | `2` | `0` | `/2` | `0x7` | `0xE` |
| 480M | 240 MHz | 480 MHz | 240 MHz | `3` | `0` | `/2` | `0x7` | `0xE` |

重要推论：

1. CP NuttX 运行在物理 CPU0，官方有效上限是 240 MHz；
2. AP NuttX 运行在物理 CPU1/CPU2，官方有效上限是 480 MHz；
3. 总线上限是 240 MHz；
4. SDK 枚举顺序是共享资源 vote 的优先级，不是 CPU0 频率的单调顺序；
5. `240M -> 320M` 会把 AP 从 240 MHz 提到 320 MHz，同时把 CPU0/Bus 从
   240 MHz 降到 160 MHz；这是 SDK 表的预期行为。

不得把 `BK7258_FREQ_MAX` 截断到 OPP 240M。AP 的 JPEG、H264 和 Audio 路径依赖
OPP 480M；该请求在 CP 侧落成 CPU0/Bus 240 MHz，在 AP 侧落成 480 MHz。

## 3. 当前软件分层

### 3.1 BL1

`chips/bk7258/bootloader/boot_clock.c` 只负责启用、校准 DPLL，并按官方 A/B
bootloader 语义交接 120 MHz。BL1 不负责性能 OPP，也不提前抬高运行时电压。
这里的“120 MHz 交接”是时钟选择器状态，不代表已经执行完整的运行时 OPP 120M
电压步骤：BL1 保持 SDK early-init 的 VDDIG=`0xB`，而 OPP 表中 120M 的目标值是
`0xC`。固定 SDK 自己同样把 `s_pre_cpu_freq` 初始化成 120M；首次切换到其他 OPP
时直接先设置目标电压、再切时钟，因此当前 lower-half 与 SDK 行为一致。

### 3.2 CP 启动与性能 profile

`CONFIG_BK7258_CLOCK_240M` 只用于 CP-only 性能测量。它在 `nx_start()` 前通过统一
DVFS lower-half 选择 SDK `PM_CPU_FRQ_240M`，因此 CPU0/AP/Bus 都是 240 MHz。
此时 AP autostart 必须关闭，避免性能数据混入 AP 负载。

旧 `CONFIG_BK7258_CLOCK_320M` 性能配置实际只给 CPU0 160 MHz，generation 145 的
历史结果仍然有效，但不得再描述成 CP 的最高性能档。

### 3.3 运行时 DVFS 与 AP vote

`chips/bk7258/cp/bk7258_dvfs.c` 保留全部 OPP 0..6，并按 SDK 顺序逐档升降电压和
时钟。`bk7258_pm_policy.c` 聚合 CP/AP 客户端的最大 OPP vote：

- AP 启动阶段可投 OPP 320M，AP 实际 320 MHz、CP 实际 160 MHz；
- AP Audio/JPEG/H264 可投 OPP 480M，AP 实际 480 MHz、CP 实际 240 MHz；
- vote 释放后恢复默认 OPP 120M。

公共枚举保留 `BK7258_PM_CPU_FREQ_*` 拼写以兼容 SDK/wire ABI；项目代码使用
`BK7258_PM_OPP_*` 别名表达真实语义。

### 3.4 定时和外设

- Scheduler SysTick 固定走 32 kHz，不随 OPP 改变；
- DWT/perf conversion 使用 `bk7258_clockdiag_current_cpu_hz()` 的角色实际频率，
  DVFS 后只刷新 DWT conversion；
- `bk7258_clockdiag_current_bus_hz()` 按 SDK 正式 OPP 返回总线频率，不能用 AP
  频率代替；它不承诺解释 SDK 未使用的 raw `CLKDIV_BUS` 状态；
- Ethernet wrapper 的 HCLK 返回正式 OPP Bus Hz。但固定 SDK 已用 `#if 0` 关闭
  MDIO 的 HCLK 动态分频并恒选 DIV62；HCLK 只会在可选 `CONFIG_ETH_LPI` 初始化
  `MAC1USTCR` 时采样，当前 AP SDK profile 未启用 LPI。未来若同时启用 LPI 与
  运行时 DVFS，必须增加 PM notifier，在每次 OPP 切换后重算 `MAC1USTCR`；
- Flash 独立使用 SDK 120/80 MHz 配置；
- PSRAM 独立使用 SDK 240/160/120/80 MHz 配置；
- 改 CPU OPP 不得顺带修改 Flash 或 PSRAM 时钟。

## 4. `/proc/dvfs` 契约

启用 `CONFIG_BK7258_DVFS_PROCFS` 后：

```text
nsh> cat /proc/dvfs
cur_opp 4
cpu0_hz 240000000
ap_hz 240000000
bus_hz 240000000
```

写入使用 OPP 编号，不使用 MHz：

```text
nsh> echo "cur_opp 5" > /proc/dvfs
```

OPP 5 的预期输出是 CPU0 160 MHz、AP 320 MHz、Bus 160 MHz。为了兼容已有调试
脚本，旧 `cur_freq <opp>` 写法仍被接受，但新文档和脚本只能使用 `cur_opp`。

## 5. 验证门禁

CP 性能镜像至少验证：

- resolved config 为 `CONFIG_BK7258_CLOCK_240M=y`；
- M1 为 `cksel=3, clkdiv=1`，CPU0 speed 为 `/1`；
- VDDD/VDDDIG 为 `0x6/0xD`；
- live CPU0/Bus 均为 240 MHz；
- 固定 32 kHz SysTick、DWT、UART、sleep、WDT 和 benchmark 无回归。

双核产品镜像至少验证：

- AP 启动 OPP 320M 时 AP/CP/Bus 为 320/160/160 MHz；
- Audio/H264 OPP 480M 时 AP/CP/Bus 为 480/240/240 MHz；
- vote 释放后回到 OPP 120M；
- Flash、PSRAM、DMA/cache、RPMsg 和外设超时无回归。

直接把 CPU0 speed 位设为 `/1` 并选择未分频 480 MHz，不属于固定 SDK 的任何正式
OPP，也没有对应的供电、总线、Flash、PSRAM 和稳定性保证，不能进入产品适配。

## 6. 2026-08-27 实板闭环

T5-Board generation 146 已按本契约完成：

- resolved config 为 `CONFIG_BK7258_CLOCK_240M=y`，CP 性能 profile 选择 OPP 240M；
- 使用本代新 BL1/MCUboot P-256 密钥和递增 counter 146 完成 COM3 单文件全量下载；
- 460800 bit/s 下载后回读除 CP/AP primary trailer 的标准 `copy_done=1` 及对应 CRC
  外全部一致，`usr_config` 和整个 Agent persistent 逐字节不变；
- 最终冷启动到 NSH，无 HardFault、ASSERT、panic 或 ERROR；
- CoreMark、Ramspeed、Whetstone 各 10 次通过。CoreMark mean 从旧 160 MHz 的
  374.307544 提升到 561.576945，比例 1.500308914；四项 64 KiB Ramspeed mean
  比例均为约 1.50038。归一化 CoreMark/MHz 基本不变，证明 CPU0/Bus 实际由
  160 MHz 落到 240 MHz，而不只是改了配置名称。

J-Link 因当时物理 RESET/SWD 连接状态未能附着，因此本结论没有冒充 live 寄存器
读数。完整构建身份、公钥指纹、下载边界、回读差异和 30 个 UART 原始哈希见
[generation 146 实板验证记录](../../../progress/verification/2026-08-27-bk7258-sdk-clock-240m-validation.md)。
