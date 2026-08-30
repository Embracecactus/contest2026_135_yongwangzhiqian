# BK7258 N8 physical cold-reset NSH 静默问题完整复盘

日期：2026-07-30（2026-07-31 完成最终收口）

> 本文是该次故障的固定历史综合，不维护现行支持状态或操作流程；现行操作以
> [构建、烧录与调试 SOP](bk7258-build-flash-debug-sop.md)为准。

最终状态：`resolved / final image board-verified / warm 3/3 / physical-reset 3/3 / AP SMP passed`

> 实板范围：本复盘对应涂鸦 T5AI-Core 首板（原记录称 T5-AI）及当时镜像；其中
> 冷复位和 SMP 机制可作为 BK7258 调试参考，但不能替代其他板卡的独立验收。

关联 SOP：[BK7258 自动编译、下载与板端调试 SOP](bk7258-build-flash-debug-sop.md)

原始调查、临时路标与自动化过程中的可复用结论已合并到本文；一次性工作日志不再单独维护。

## 1. 文档目的

本文把一次跨 bootloader、CP NuttX、Beken SDK UART、AP SMP 和 Windows/WSL2 工具链的问题，整理成后续接手者可以快速理解的完整过程。

需要记住的最终结论有三条：

1. 原始问题“手动 RESET 后 GPIO 日志结束即永久静默、NSH 不出现”已经修复；
2. 曾经独立存在的 AP READY timeout 也已经修复；最终 `ap_smp_bidir` 镜像中
   AP、CPU2、SMP 和 bidirectional gate 均为 `PASSED/error=0`；
3. 清除全部临时 checkpoint 后，正式镜像连续通过 warm 3/3 和 COM7 RTS
   physical-reset 3/3。physical power cut 尚未执行，不能把 RESET 验证写成断电验证。

UART 静默、AP READY timeout 和 bootloader 冷启动契约是三个相互影响但可独立验证的问题，
下文保留排查过程，并在第 18 节记录最终收口证据。

## 2. 环境和系统结构

### 2.1 硬件

```text
SoC:          Beken BK7258
First board:  Tuya T5AI-Core (historical name: T5-AI)
CP:           physical CPU0，运行 NuttX/NSH
AP:           physical CPU1 + CPU2，运行 NuttX SMP
console:      UART1，460800 8N1
下载器:       CH342-A / Windows COM7
日志口:       CH342-B / Windows COM11
调试器:       SEGGER J-Link V9，SWD 1000 kHz
```

### 2.2 启动链

```text
BootROM
  -> Tier-1 bootloader
     -> CP app @ 0x02010000
        -> arm_serialinit()
        -> board_app_initialize()
        -> AP autostart
        -> GPIO/procfs/LittleFS
        -> NuttShell (NSH)
```

### 2.3 双镜像构建配置

本次最终验证使用以下实例配置（不是 builder 的唯一组合）：

```text
CP_CONFIG_NAME=cp_nsh
AP_CONFIG_NAME=ap_smp_bidir
```

最终烧录文件：

```text
<openvela-workspace>/nuttx/bk7258-dual/all-app-factory.bin
```

不要用 root `nuttx/all-app.bin` 代替。root 文件只包含 bootloader + CP，不包含 AP。

## 3. 原始问题

### 3.1 下载后或 loader reboot

下载器完成烧录并 reboot 后，系统通常能进入 NSH：

```text
u_bootloader enter
partition app @ 0x02010000
jump to:0x02010000
JMP
GPIO busy...
NuttShell (NSH)
nsh>
```

### 3.2 手动 physical RESET

按板卡 RESET 后出现：

```text
u_bootloader enter
BClk A5=8407A76C A9=787BC8A4
partition app @ 0x02010000
jump to:0x02010000
JMP
GPIO busy...
```

然后永久静默：

- 无 NSH；
- 串口不能交互；
- 等待十几秒仍无后续；
- 没有形成 WDT reset loop。

## 4. 第一项关键判断：warm 和 cold 不是同一路径

`BClk A5=... A9=...` 是 bootloader cold-clock 初始化路标。

```text
下载后启动 / loader reboot：warm residue path，通常无 BClk
手动 RESET：               physical cold path，有 BClk
```

冷复位日志已经出现 `partition app`、`jump to` 和 `JMP`，说明：

- bootloader 已找到 CP 分区；
- image header 和跳转已完成；
- blocker 位于 CP app 内，而不是 bootloader 跳转之前。

## 5. Raw UART 路标定位

为了避免依赖尚未注册的 `/dev/console`，在三个 team-overlay 文件中临时加入 `up_putc()` 路标。

### 5.1 UART 路标

```text
S0  arm_serialinit() 入口
U0  bk7258_uart_setup() 入口
G1  bk_gpio_driver_init() 返回
U1  bk_uart_driver_init() 返回
U2  bk_uart_init() 返回
U3  SDK software FIFO disable 完成
U4  UART setup 完成
U5  /dev/console 注册完成
```

### 5.2 Board bringup 路标

```text
C0  board_app_initialize() 入口
C1  WDT 接管完成
C2  AP control initialize 返回
C3  AP autostart 开始
C4  AP autostart 返回
C5  GPIO lower-half 完成
C6  procfs 完成
C7  flash/LittleFS 完成
C8  board_app_initialize() 返回
```

### 5.3 AP 路标

```text
A0..A6  AP reset/power/mailbox/boot-address/release 流程
W0/W1   第一次 AP READY wait sleep 前后
A7       AP wait 返回
F1/F2    AP timeout 后 reset/power-down cleanup
```

这些路标仅用于该轮定位，并已在最终镜像中删除；必要含义保留在本节。

## 6. Phase A 定位结果

同一镜像取得 warm/cold A/B：

### 6.1 Warm

```text
S0 U0 U1
GPIO busy device 0x22
U2 U3 U4 U5
C0 ... C8
NSH
```

### 6.2 Cold

```text
BClk ... JMP
S0 U0 U1
GPIO busy device 0x22
```

无 `U2`。

因此 blocker 被精确限定为：

```text
bk7258_uart_setup()
  -> bk_uart_init()
     -> GPIO pinmux 日志
     -> 未观测到 wrapper 返回点 U2
```

此时可以排除：

- AP autostart；
- WDT；
- SysTick wait；
- procfs；
- flash/LittleFS；
- NSH session 创建。

因为这些都发生在 `C0` 之后，而 cold path 尚未到达 `C0`。

## 7. 第一层根因：GPIO-before-UART 初始化顺序

SDK GPIO LL 使用一个静态 peripheral-mode table 指针，该指针只在以下路径初始化：

```text
bk_gpio_driver_init()
  -> gpio_hal_init()
     -> gpio_ll_init()
```

原启动顺序却是：

```text
arm_serialinit()
  -> bk_uart_init()
     -> uart_init_gpio()

稍后 C5：
  -> bk_gpio_driver_init()
```

真实 cold state 下，UART pinmux 路径在 GPIO HAL 尚未初始化时运行；warm residue 可以掩盖这个顺序缺陷。

第一轮修复将顺序改为：

```text
bk_gpio_driver_init()
bk_uart_driver_init()
bk_uart_init()
```

`bk_gpio_driver_init()` 本身有幂等 guard，后续 GPIO lower-half 重复调用不会改变设备注册职责。

### 7.1 第一轮板测结果

```text
S0
U0
G1
U1
GPIO busy device 0x21
```

`device num` 从错误的 `0x22` 变成正确的：

```text
GPIO_DEV_UART1_TXD = 0x21
```

这证明 GPIO HAL/pinmux table 已正确初始化。

但是仍无 `U2`，说明 GPIO-before-UART 是真实缺陷，却不是完整修复。

## 8. 第二层根因：SDK UART 重配导致 console 消失

`bk_uart_init()` 在 GPIO 日志之后还会处理：

- software state；
- kfifo；
- semaphore；
- UART clock source；
- divider/config；
- HAL 初始化和 IRQ。

`U2` 本身使用同一个 UART 输出。因此“无 U2”存在两个可能：

1. `bk_uart_init()` 真正未返回；
2. `bk_uart_init()` 已返回，但函数末尾重写 UART clock/config，导致后续 460800 输出不可见。

第二轮修复在 `bk_uart_init()` 返回后、检查返回值和打印 `U2` 之前，重新声明板级 UART1 invariant：

```c
result = bk_uart_init(priv->id, &config);
bk7258_uart_restore_console();
```

恢复内容：

```text
0x44010030 bit10 = 1      UART1 clock gate enable
0x44010020 bit13 = 0      UART1 source = 26 MHz XTAL
0x45830008       = 1      UART1 global enable
0x45830010       = 0x371b divider 55 / 460800 / 8N1 / TX+RX
dsb sy
isb sy
```

## 9. 旧产物误测如何被识别

第二轮源码修改后曾收到一次仍无 `U2` 的日志，但主机核对发现：

```text
旧 all-app-factory.bin: 23:00:51
bk7258_serial.c 修改:  23:08:39
```

当时 ELF 中也没有第二轮 restore 的执行序列。因此该日志只是第一轮旧镜像重复复现，不能判断第二轮成功或失败。

这个教训被固化进 SOP：每次烧录前必须记录产物时间、大小和 SHA-256，不能只凭“刚才点过编译”判断代际。

## 10. 第二轮构建与入包验证

执行：

```bash
cd <openvela-workspace>

AP_CONFIG_NAME=ap_smp_bidir \
  ./contest2026_135_yongwangzhiqian/board/bk7258/scripts/build_dual_image.sh
```

构建成功，返回码 0。

### 10.1 最终产物

| 产物 | 大小 | SHA-256 |
|---|---:|---|
| raw CP `app.bin` | 179328 | `128771c286d35849e321f80acc373a18c1db8143e2a2de7130bd837fb4251c21` |
| CP `app_crc.bin` | 190536 | `5e7b864eebeeda3994712bc1eb8cdaa87183cdb496d82952222ae7b8c724e014` |
| raw AP `app1.bin` | 66520 | `5dc6fbf58b28ed838d79328f2e2cf85385ad564493eee223f0721ab85623b8d8` |
| AP `app1_crc.bin` | 70686 | `c563308f74b46e2aacef9d4bca03b5ba4cb9a51ae3c3f338927ad4f7b2087f4b` |
| factory image | 2298910 | `d83c8e38bec19160f9d54d0832a4f553dab85bd568173f2a1ebe4fc9e860d405` |

### 10.2 Factory 物理布局

```text
0x000000..0x011000  bl_crc.bin
0x011000..0x03f848  CP app_crc.bin
0x03f848..0x220000  0xff padding，包括 LittleFS 区
0x220000..0x23141e  AP app1_crc.bin
```

逐字节比较确认 factory 中三个 segment 均与输入文件 exact match。

### 10.3 ELF 语义验证

`bk7258_uart_restore_console()` 被编译器内联，因此没有独立符号是正常的。

最终 CP ELF 的 `bk7258_uart_setup()` 中可见：

```text
bl bk_uart_init
置 UART1 clock enable
清 UART1 clock-select bit13
写 0x45830008 = 1
写 0x45830010 = 0x371b
dsb
isb
随后才检查 result 并打印 U2
```

这证明第二轮源码不仅存在，而且进入最终 factory 的 CP payload。

## 11. 自动下载和 warm 板测

自动调试脚本在 COM11 提前开始采集，然后通过 Windows COM7 调用 `bk_loader.exe` 下载。

下载器日志：

```text
Writing Flash OK
{All Finished Successfully}
```

该版本 `bk_loader.exe` 虽然打印成功，有时仍返回进程码 1。自动脚本现在以两个完整成功标志和 flash verify 归一化这一兼容问题。

### 11.1 Warm trace

```text
S0 U0 G1 U1
GPIO device 0x21
U2 U3 U4 U5
C0 C1 C2 C3
A0 A1 A2 A3 A4 A5 A6
W0 W1
```

首次板端确认 `U2`，说明 console restore 生效。

后续采集得到：

```text
A7
F1
F2
C4 C5 C6 C7 C8
NuttShell (NSH)
nsh>
```

因此 warm path 最终进入 NSH。

## 12. J-Link reset 与手动 RESET 的差异

### 12.1 J-Link 首次尝试

执行 `ClrRESET/SetRESET` 时，J-Link 报告：

```text
RESET (pin 15) high, but should be low
```

串口无 `BClk/S0`，只收到了上一 warm boot 的后续输出。因此这次没有形成有效 physical reset。

### 12.2 手动 RESET

用户手动按下板卡 RESET，得到：

```text
u_bootloader enter
BClk A5=8407A76C A9=787BC8A4
partition app @ 0x02010000
jump to:0x02010000
JMP
S0 U0 G1 U1
GPIO device 0x21
U2 U3 U4 U5
C0 C1 C2 C3
A0 A1 A2 A3 A4 A5 A6
W0 W1
A7 F1 F2
C4 C5 C6 C7 C8
NuttShell (NSH)
nsh>
```

这是本问题最关键的最终证据：

```text
BClk -> U2..U5 -> C8 -> NSH
```

原始 physical cold-reset 永久静默问题已消失。

### 12.3 J-Link 等价判据

J-Link 即使命令返回成功，也只有串口出现以下完整前缀时才算等价于手动 RESET：

```text
u_bootloader enter
BClk ...
...
JMP
S0
```

当前脚本已切换为实验性的：

```text
RSetType 2   # RESETPIN
Reset
Go
```

但在板端得到 `BClk` 之前，手动 RESET 仍是 authoritative cold-reset 方法。

## 13. 历史阶段：AP timeout 曾是独立问题，现已解决

完整 cold trace 包含：

```text
W1
A7
F1
F2
C4 ... C8
NSH
```

这意味着：

- CP 等待 AP READY 数秒；
- AP 未在默认 timeout 内发布 READY；
- CP 执行 deterministic reset/power-down cleanup；
- CP fail-open，继续进入 NSH。

NSH 下 `apctl status` 曾显示：

```text
AP state=FAILED error=6
CPU2 state=SCHEDULER_ONLINE error=0 online=0x3
AP SMP state=PASSED error=0
affinity task started/completed=1/0
sem-loop requested/completed=8/7
```

枚举 `error=6` 是 `BK7258_AP_ERROR_TIMEOUT`，由 CP timeout cleanup 写入。

当时可以得出：

- AP/CPU2/SMP 实际已经运行；
- advanced gate 未在 CP timeout 内完成；
- 该问题不影响“cold reset 后 NSH 可恢复”结论；
- 后续应作为独立 AP stage 处理。

`[ipc_svr] create_socket failed.` 出现在 NSH 已经可用之后，同样不是启动 blocker。

本节是 2026-07-30 的历史状态，不是最终状态。一次因果实验曾把 CP
`nxsig_usleep()` 改为 busy wait，从而证明停在 `W1` 与启动时序/调度相关。最终源码没有
保留纯 busy wait：CP 使用绝对 tick deadline 加短 `nxsig_usleep()`，避免高优先级轮询
饿死 idle/watchdog；AP 的 SMP self-test 轮询才使用 `up_mdelay(1)` 加
`sched_yield()`，关闭“IPI 已到但本地 task 未运行”的窗口。配合 cache/MPU 和三阶段握手
后，最终 AP 为 `READY/error=0`，CPU2 为 `SCHEDULER_ONLINE/error=0`。

## 14. 最终根因总结

原始问题不是单一缺陷，而是两层 cold-state 假设叠加：

### 根因 1：初始化顺序错误

```text
UART 使用 GPIO HAL/pinmux table
但 GPIO driver/HAL 尚未初始化
```

修复：

```text
bk_gpio_driver_init() 必须先于 bk_uart_init()
```

### 根因 2：SDK 重配破坏板级 console invariant

```text
bootloader 已建立 26 MHz / 460800 UART1
SDK bk_uart_init() 重新写 clock/divider/config
后续 checkpoint 和 NSH 输出消失
```

修复：

```text
bk_uart_init() 返回后重新声明 UART1 clock/config，再输出 U2
```

最终还补充了第三个 UART 时序修复：SDK 在打印 GPIO 占用提示后会立即
`gpio_dev_unmap(GPIO0/GPIO1)` 并接管 UART1。原始 `up_putc()` 只等待 FIFO
“可写”，最后一个字节可能仍在发送移位寄存器中，pinmux 已被切换，正式镜像就会在该提示
中途静默。handoff 窗口中必须有界等待 UART1 `TX_EMPTY`，正常运行期仍只等待
`WR_READY`，避免给每个字符增加不必要的延迟。

因此最终 UART 修复由三部分组成，缺一不可：

1. GPIO driver/HAL 先于 SDK UART 初始化；
2. SDK UART 初始化完成后恢复板级 UART1 clock/config；
3. SDK 接管 GPIO0/1 前，把最后一个 raw-console 字节真正发送完。

## 15. 验收时状态矩阵

| 项目 | 状态 |
|---|---|
| 第二轮源码 | 已落盘 |
| CP/AP 构建 | `build-verified` |
| restore 入 ELF | 已验证 |
| CP 段进入 factory | exact match |
| loader/warm U2 | 板端通过 |
| 手动 physical RESET U2 | 板端通过 |
| physical RESET 到 NSH | 连续 3 次通过（COM7 RTS，均有 `BClk`） |
| warm 启动到 NSH | 连续 3 次通过 |
| AP READY | `READY/error=0` |
| CPU2 / SMP | `SCHEDULER_ONLINE/error=0`，SMP gates `PASSED` |
| 临时 checkpoint 清理 | 已完成；最终镜像无 `S/U/G/C/A/W/F` 临时路标 |
| NuttX 官方源码 | 零改动，`git -C nuttx diff --exit-code -- .` 通过 |
| 最终 commit/push | 未执行 |
| physical power cut | 未执行；不影响本次 RESET 闭环，但仍是更强的产品门禁 |

## 16. 已完成的收口顺序和剩余边界

1. 用 checkpoint 镜像区分 UART 静默与 AP timeout；
2. 修复 CP/AP cache、MPU、WDT 和 SMP handshake/调度问题；
3. 删除 `S/U/G/C/A/W/F` 临时 checkpoint；
4. 发现正式镜像仍在 GPIO 提示中途静默，补上 UART `TX_EMPTY` ownership handoff；
5. 重建并烧录无诊断路标的最终 `cp_nsh + ap_smp_bidir` 镜像；
6. 连续完成 warm 3/3、COM7 RTS physical-reset 3/3；
7. 在 NSH 中执行 `apctl status`，确认 AP/CPU2/SMP/affinity/semaphore/BP2P 全部通过。

剩余边界只有 physical power cut。它比 RESET 更强，适合发布前追加，但不能否定当前已经
完成的 warm/physical-reset 闭环。最终 commit/push 也尚未执行，避免越过用户授权。

## 17. 保留证据

- 本文第 5 至 16 节：原始调查、诊断路标和自动化过程的合并结论；
- [构建、烧录与调试 SOP](bk7258-build-flash-debug-sop.md)：现行操作入口；
- [Bootloader 逆向综合快照](../bootloader-analysis/full-reverse-synthesis.md)：该轮
  cold-reset 状态契约的二进制与源码依据；
- [2026-08-01 压力测试报告](../../../verification/bk7258/2026-08-01-bk7258-stress-test-report.md)：
  后续不可变板端验收证据。

原始串口日志、会话交接、执行授权和临时 checkpoint 文件是一次性工作材料，不作为
现行文档层保留。
