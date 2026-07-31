# BK7258 Bootloader 逆向综合与当前实现边界

日期：2026-07-31

这份文档替代旧的“最小 bootloader 已完全符合、无需修改”结论。旧结论只覆盖
header/VTOR/MSP/branch 主路径，没有覆盖 cold reset 下的 cache、MPU、watchdog、
secondary-core power 和 UART ownership，因此不够完整。

## 1. 当前分析对象

技术支持提供的 SDK：

```text
C:\Users\lijian\Downloads\BK7258_SMP\bk_avdk_smp-release-v3.1.1.9.tar.gz
```

官方 normal bootloader：

| 项目 | 值 |
|---|---|
| 文件大小 | 52352 bytes |
| SHA-256 | `105161bb603eedafbffcb5efb8f7c06a0c8503e42ba4da46490c2c21ed813de6` |
| 链接基址 | `0x02000000` |
| initial SP | `0x28030000` |
| Reset_Handler | `0x020001c1` |
| version | `bc31115` |
| Ghidra 识别 | 134 functions / 224 call edges |

旧文档引用的其他 SHA 或 SDK 版本只能作为历史样本，不能覆盖本表。

## 2. BootROM 与 flash 格式契约

当前仍成立的基础事实：

```text
Bootloader logical base: 0x02000000
Boot magic logical offset: 0x100
Boot magic: BK7236\x10\x00
Flash CRC layout: 每 32 bytes data 后附 2 bytes CRC16
App CP logical base: 0x02010000
App CP physical flash offset: 0x11000
```

CRC expansion 是构建/烧录格式；CPU 经 flash controller 读取时看到连续 logical data。
这不代表 bootloader 只需要做一次 branch，二者是不同层次的问题。

## 3. Ghidra 复核后的官方 reset/handoff 路径

官方启动路径可概括为：

```text
Reset_Handler
  -> early SoC/clock/flash preparation
  -> WDT and UART setup
  -> MSPLIM and runtime data initialization
  -> cache/MPU/system control preparation
  -> boot source / partition / app validation
  -> jump_to_app
       -> disable and clean/invalidate D-cache
       -> clear MPU state
       -> set VTOR/MSP and sanitize registers
       -> branch to app reset entry
```

SCB 正确基址是：

```text
0xE000ED00
```

旧笔记中的 `0xED00E000` 是错误地址，不能复用。

## 4. 为什么不能直接复制官方 52 KB

官方 bootloader 使用自己的 RBL、分区、下载和升级生态；本项目使用：

- team-owned Tier-1；
- raw NuttX CP/AP 双镜像；
- FAL/固定 flash layout；
- 自己的 4 KiB padded sparse factory image；
- CP app 自己启动 AP/CPU2。

直接替换成官方 binary 可能让 app header、分区表或升级协议不兼容。当前策略是
clean-room 复现“运行 raw NuttX 必需的硬件状态契约”，并保留项目已有功能。

## 5. 当前 Tier-1 功能对照

| 能力 | 当前状态 | 说明 |
|---|---|---|
| BootROM magic / CRC-packed boot image | 已实现 | 保持现有烧录兼容 |
| CP app 固定分区选择与向量检查 | 已实现 | 保持 raw NuttX 路径 |
| VTOR/MSP/Thumb branch | 已实现 | app handoff 主路径 |
| early I-cache invalidate | 已实现 | `start.S` |
| cold reset cache/MPU 清理 | 已实现 | `boot_runtime.c` + `start.S` |
| secondary-core reset/power-down hardening | 已实现 | app 前状态确定化 |
| boot WDT coverage | 已实现 | cold init 有保护 |
| app 接管前 WDT ownership | 已实现 | CP reset 关闭 boot AON/APB WDT |
| UART1 boot trace | 已实现 | 保留 `BClk` 等现有可观测性 |
| 官方 RBL 分区协议 | 刻意不复制 | 与 raw NuttX/FAL 模型冲突 |
| 官方 OTA/download 全协议 | 刻意不复制 | 不是当前产品需求 |
| 官方全部 134 函数语义等价 | 未宣称 | 当前只覆盖启动所需路径 |
| 52 KB binary-by-binary parity | 未完成也非当前目标 | 不能写成“完全逆向” |

当前 bootloader text 为 2980 bytes。体积小不代表缺失当前启动必需契约，也不表示与官方
功能全集等价；它只说明项目选择了更窄的功能范围。

## 6. 与 AP SMP 的边界

bootloader 不负责完整启动 AP NuttX/CPU2 scheduler。它负责给 CP app 一个确定的初始
硬件状态；之后：

```text
CP NuttX
  -> AP controller release CPU1
     -> AP NuttX early cache/MPU setup
        -> CPU2 boot-ready/post-bringup/scheduler-unlock handshake
```

因此：

- bootloader 必须清理 stale cache/MPU/core state；
- CP/AP overlay 必须实现后续多核启动；
- “多核由 app 启动”不能被误读成“bootloader 的 core/cache 状态无关紧要”。

## 7. WDT 的准确结论

官方 SDK 表明：

- stop 使用 period 0 和 `0x5A`/`0xA5` key；
- feed 会按保存的 period 重新初始化；
- APB WDT 与 AON WDT 是两套状态；
- SDK 的 `system_main.c` 只在 JTAG debug 模式主动 `bk_wdt_stop()`，不是所有启动场景
  自动替 app 清理。

所以当前所有权为：

```text
Tier-1 cold init：WDT 保护
CP reset entry：关闭 boot 遗留 AON + APB WDT
AP autostart 完成后：注册 NuttX watchdog
```

## 8. 验证结果与不能过度声称的部分

当前 exact Tier-1 padded segment：

```text
size       69632
SHA-256    8908ebdc8df5aea5ed837e561e94b15c21ec6cdfaf393c8a340ecff376e29184
```

它与最终 CP/AP factory image 一起通过：

- warm boot 3/3；
- COM7 RTS physical reset 3/3，均看到 `BClk`；
- AP READY、CPU2 scheduler、SMP/affinity/semaphore/BP2P 全部
  `PASSED/error=0`；
- bootloader `make clean all verify`。

尚未执行真正的 power cut。因此当前准确表述是：

> 已用官方 v3.1.1.9 binary/SDK 复核并补齐当前 raw NuttX 启动所需的 bootloader
> 契约，physical reset 板测通过；没有宣称完整复刻官方 RBL/OTA/download 功能，也没有
> 完成 52 KB 全函数的语义等价证明。

## 9. 相关文档

- [官方 bootloader 详细逆向](bk-official-bootloader-reverse.md)
- [vendor bootloader 对比](vendor-bootloader-comparison.md)
- [冷复位与 SMP 修复入门指南](../nuttx-port/cold-reset-smp-repair-guide.md)
- [最终冷复位复盘](../nuttx-port/n8-cold-reset-resolution-report.md)
