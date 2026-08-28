# BK7258 N8 physical cold-reset NSH 静默问题完整复盘

日期：2026-07-30（2026-07-31 完成最终收口）

最终状态：`resolved / final image board-verified / warm 3/3 / physical-reset 3/3 / AP SMP passed`

关联 SOP：[BK7258 自动编译、下载与板端调试 SOP](bk7258-build-flash-debug-sop.md)

详细原始记录：[n8-cold-reset-nsh-hang-investigation.md](n8-cold-reset-nsh-hang-investigation.md)

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
SoC / module: Beken BK7258 / Tuya T5-AI
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
/home/lijian/project/open-vela/nuttx/bk7258-dual/all-app-factory.bin
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

完整路标实现见：[n8-cold-reset-diagnostic-checkpoints.md](n8-cold-reset-diagnostic-checkpoints.md)。

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
cd /home/lijian/project/open-vela

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

## 15. 当前状态矩阵

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

## 17. 证据索引

```text
主调查：docs/platforms/bk7258/nuttx-port/n8-cold-reset-nsh-hang-investigation.md
路标代码：docs/platforms/bk7258/nuttx-port/n8-cold-reset-diagnostic-checkpoints.md
自动化实现说明：docs/platforms/bk7258/nuttx-port/n8-cold-reset-automation.md
正式 SOP：docs/platforms/bk7258/nuttx-port/bk7258-build-flash-debug-sop.md
交接记录：docs/platforms/bk7258/nuttx-port/n8-cold-reset-session-handoff-2026-07-30.md
自动化日志：/home/lijian/project/open-vela/logs/bk7258-auto-debug/
```

## 18. 收口任务执行授权

用户已明确授权由自动化代理接管以下任务，包括构建、Windows 下载、COM11 串口采集和板端判读：

1. 完成当前 checkpoint 镜像的 physical cold-reset 重复性矩阵；
2. 定位并修复独立的 AP READY timeout；
3. 在上述门禁通过后删除临时 checkpoint；
4. 重建、下载最终无诊断路标镜像并执行 warm/cold 回归；
5. 探测现有 J-Link/USB 设备是否能真正控制目标电源；普通 reset 不计作 power cycle。

执行顺序固定为：

```text
checkpoint image repeatability
  -> AP timeout fix
  -> repeat verification
  -> checkpoint cleanup
  -> final image build/download
  -> final cold regression
```

当前主线程负责硬件闭环；AP timeout 源码分析和 checkpoint 清理范围可以并行进行，但在板端门禁完成前不提前删除路标。

### 18.1 J-Link RESETPIN 自动复位实测失败

自动采集 COM11 后，J-Link 已连接 STAR target 并执行：

```text
RSetType 2
Reset
Go
```

Commander 明确显示 `Reset type: RESETPIN`，但随后报告：

```text
WARNING: RESET (pin 15) high, but should be low. Please check target hardware.
```

COM11 30 秒采集为 0 字节，没有 `BClk/S0`。因此现有 J-Link RESET 接线或电气路径不能实际拉低 SoC nRESET，本次不计入 physical cold-reset 矩阵。

下一自动化候选为 CH342-A/COM7 的 DTR/RTS 控制线和 J-Link target-power 输出。任何候选都必须以串口重新出现 `BClk/JMP/S0` 为有效判据。

### 18.2 CH342-A RTS 自动 physical reset 已验证

在 COM11 raw capture 提前打开的条件下，对 COM7/CH342-A 分别测试 150 ms 控制线脉冲：

```text
DTR       -> 0 字节，无复位
RTS       -> BClk/JMP/S0/U2...，真实 physical cold reset
DTR + RTS -> 0 字节，无复位
```

RTS 测试日志明确出现：

```text
u_bootloader enter
BClk A5=8407A76C A9=787BC8A4
partition app @ 0x02010000
jump to:0x02010000
JMP
S0 U0 G1 U1 U2 U3 U4 U5
C0 C1 C2 C3
A0..A6 W0 W1
```

因此 COM7 的 RTS 控制线已经由板端 `BClk` 证实可等价于手动按键 RESET，可用于自动完成 physical cold-reset 重复矩阵。DTR 不应单独或与 RTS 同时切换。


### 18.3 自动 cold-repeat 首轮未通过

使用已验证的 COM7 RTS physical reset，COM11 30 秒原始日志为：

```text
BClk -> JMP -> S0/U0/G1/U1 -> U2/U3/U4/U5
-> C0/C1/C2/C3 -> A0..A6 -> W0/W1
```

随后 30 秒内没有 `A7/F1/F2/C4..C8/NSH`。因此重复性矩阵当前不能判定通过；AP READY wait 在重复 cold state 下仍可能不返回或超长延迟。

自动 summary 同时发现一个 host-side 判读缺陷：旧实现按任意 substring 搜索 `A7/C8`，会误匹配 `BClk A5=8407A76C A9=787BC8A4` 中的十六进制字符。判读器已改为按去空白后的整行精确匹配 checkpoint；该次实际最后路标是 `W1`。

下一步执行 90 秒长采样；若仍无 `A7`，则把问题收敛为 repeated-cold 下 `bk7258_ap_wait()` timeout 机制失效或时间基准异常，而不是普通 AP READY timeout。

### 18.4 90 秒长采样确认 CP timeout 机制失效

2026-07-31 使用同一 COM7 RTS physical-reset 路径执行 90 秒原始采集：

```text
logs/bk7258-auto-debug/20260731-011502/
```

结果仍精确停在：

```text
BClk -> JMP -> S0/U0/G1/U1 -> U2/U3/U4/U5
-> C0/C1/C2/C3 -> A0..A6 -> W0 -> W1
```

`serial_bytes=362`，整行判读为 `STOP_AFTER_W1`；90 秒内没有
`A7/F1/F2/C4..C8/NSH`。这排除了普通 AP 启动延迟，并证明当前
`bk7258_ap_wait()` 的 3000 次循环没有在预期约 3 秒内结束。

`W1` 证明第一次 `nxsig_usleep(1000)` 返回；之后没有 `A7`，说明后续
sleep/clock-wait 不再推进。当前 timeout 实现因此存在循环依赖：AP 启动控制
需要 timeout 保护，但 timeout 自身依赖 repeated-cold 下不可靠的 NuttX
SysTick/signal sleep。

下一最小修复是仅把 CP `bk7258_ap_wait()` 的每毫秒等待替换为
`up_mdelay(1)`。该路径本来就是同步硬件启动/停止控制，busy wait 可确保
即使调度器 tick 异常也会确定地返回 `-ETIMEDOUT`。checkpoint 暂不删除；
修复镜像必须先证明 `A7/F1/F2/C8/NSH` 能在 repeated-cold 下稳定出现，
再继续定位 AP 自身是否仍被同一时基问题阻塞。

### 18.5 CP 独立 timeout 时基修复首次 cold 验证通过

CP `bk7258_ap_wait()` 已把循环内的 `nxsig_usleep(1000)` 替换为
`up_mdelay(1)`，使同步 AP 硬件控制 timeout 不依赖 NuttX signal sleep。

首次因果验证使用 `cp_nsh + ap_smp` 基础配置：

1. fresh CP/AP/bootloader 全量构建成功；
2. factory image 下载成功；
3. loader warm capture 在 30 秒内得到
   `A7 -> C4..C8 -> NuttShell`；
4. 随后的 COM7 RTS physical cold reset 同样在 30 秒内得到
   `BClk -> ... -> W0/W1 -> A7 -> C4..C8 -> NuttShell`；
5. 两次均没有 `F1/F2`，说明 AP 已 READY，而不是仅靠 timeout cleanup
   恢复 NSH。

证据目录：

```text
warm: logs/bk7258-auto-debug/20260731-012015/
cold: logs/bk7258-auto-debug/20260731-012051/
```

该结果证明纯粹依赖逐次 sleep 计数的旧 timeout 与 repeated-cold 启动时序相关，
busy-wait 是有效的因果探针。基础 `ap_smp` 仅用于快速验证；后续正式实现改为 CP
绝对 tick deadline + scheduled sleep，并在 AP self-test 中使用
`up_mdelay(1) + sched_yield()`。最终 `cp_nsh + ap_smp_bidir` 结果见 18.7 至
18.9，不能把本节的临时实现当成当前源码。

### 18.6 官方 SDK 和 bootloader 契约补齐

本轮使用技术支持提供的最新 SDK，而不是仓库中较旧的摘录：

```text
Windows 原始包:
C:\Users\lijian\Downloads\BK7258_SMP\bk_avdk_smp-release-v3.1.1.9.tar.gz

WSL 只读分析副本:
/tmp/bk7258-sdk.oPn1zx/bk_avdk_smp-release-v3.1.1.9
```

官方 AP 初始化给出的关键契约是：

- shared SRAM `0x28000000..0x3fffffff` 映射为 Inner Shareable、
  Normal non-cacheable，MAIR attribute 1 为 `0x44`；
- 启用 D-cache 前执行 clean/invalidate，复位残留不能直接继承；
- `SystemInit()` 负责 I-cache；
- WDT stop 需要同时处理 APB/AON ownership；
- UART1 使用 GPIO0/1，SDK 初始化会先输出 GPIO 占用提示，然后 unmap 并重新映射引脚。

Ghidra 对官方 v3.1.1.9 `normal_bootloader/bootloader.bin` 的分析确认：

```text
size:          52352 bytes
SHA-256:       105161bb603eedafbffcb5efb8f7c06a0c8503e42ba4da46490c2c21ed813de6
link base:     0x02000000
initial SP:    0x28030000
reset entry:   0x020001c1
version:       bc31115
functions:     134
call edges:    224
```

官方 reset path 不只是检查 header 后跳转，还包含早期 SoC/flash/WDT/UART/clock、
MSPLIM、runtime data、cache/MPU 清理与 app handoff。当前 Tier-1 仍保留项目自己的
raw-NuttX/FAL 分区模型，没有照搬官方 RBL、下载、OTA 等协议；补齐的是启动所需的硬件
状态契约，不宣称逐字节复刻完整 52 KB 官方功能。

### 18.7 AP/SMP 最终修复

最终实现包含以下相互配合的修复：

1. AP 的 stackless 早期入口在使用 shared SRAM 前关闭并 set/way invalidate D-cache；
2. MPU region 15 将 `0x28000000..0x3fffffff` 映射为 non-cacheable shared memory；
3. CPU2 使用 uncached shared control 的 boot-ready、post-bringup、
   scheduler-unlock 三阶段 token，避免“CPU 已运行”和“调度器已可接任务”混为一谈；
4. CP 等待使用绝对 tick 截止条件；控制轮询中组合 `up_mdelay(1)` 与
   `sched_yield()`，既保留硬件超时，又允许远端 IPI 唤醒后的本地 task 真正运行；
5. CP 在 `nx_start()` 前关闭 bootloader 遗留的 AON/APB WDT，NuttX WDT 注册移动到
   AP autostart 之后，避免启动所有权交叉。

板端 `apctl status` 的最终状态：

```text
AP                    READY, error=0
CPU2                  SCHEDULER_ONLINE, error=0, online mask=0x3
AP SMP                PASSED, error=0
affinity               PASSED, error=0
semaphore wake         PASSED, error=0
semaphore loop         PASSED, error=0
bidirectional pingpong PASSED, error=0
```

`ap_smp_bidir` 只启用了 BP2P advanced mode；BDUL/BMIG/BTIM/BLCY 没有在该
配置中启用，因此状态输出中没有这些行是预期行为，不是漏测。

### 18.8 正式镜像 UART ownership handoff 修复

删除 checkpoint 后，正式镜像稳定只收到 205 字节，最后停在：

```text
gpio: 0 is used.Please confirm unmap isn't impact...
```

把采集延长到 25 秒和 40 秒仍完全一致，说明不是 AP 启动慢。官方 SDK 源码表明，
`bk_uart_init(UART1)` 打印这条提示后会立即：

```text
gpio_dev_unmap(GPIO0)
gpio_dev_unmap(GPIO1)
重新映射 UART1 TX/RX
```

旧 `arm_lowputc()` 只等待 UART FIFO `WR_READY`（bit 20），字符进入 FIFO 就返回；
GPIO0 pinmux 随后被切走时，最后一个字符可能还没有从发送移位寄存器发完。临时
checkpoint 增加了时间，恰好掩盖了这个 race。

最终改法只在 SDK ownership handoff 窗口启用有界 `TX_EMPTY`（bit 17）等待：

```text
handoff=false: 等 WR_READY，保持正常 console 性能
handoff=true:  写字符后再等 TX_EMPTY，最多轮询 100000 次
```

`bk7258_serial.c` 在调用 `bk_uart_init()` 前后打开/关闭 handoff，再恢复板级
clock/config。这样既保证最后一个诊断字符真正发出，也不会让日常 console 每个字符都
同步等待硬件完全空闲。

### 18.9 最终正式镜像、重复性和哈希

最终 sparse factory 布局固定为 4 KiB 对齐的三个 segment：

```text
0x000000..0x011000  bl_crc.bin
0x011000..0x02f000  app_crc_flash.bin
0x220000..0x232000  app1_crc_flash.bin
```

最终哈希：

| 产物 | 大小 | SHA-256 |
|---|---:|---|
| boot padded segment | 69632 | `8908ebdc8df5aea5ed837e561e94b15c21ec6cdfaf393c8a340ecff376e29184` |
| CP padded segment | 192512 | `379a96cef05e2a1cccd167891083e0cc57930ac4fd85358d89c2acab463fa191` |
| AP padded segment | 73728 | `3144dd41d77cb27dafbae7abb2c5143ad85ca0ec946ad664ac5a47ce35c23c2a` |
| factory image | 2301952 | `f7b62cb0b784612f552a6019728760778b601f6eadfac976e1b260da5c45b95b` |

无临时 checkpoint 的 exact image 连续通过：

```text
warm:
  logs/bk7258-auto-debug/20260731-130256  PASS_NSH
  logs/bk7258-auto-debug/20260731-130415  PASS_NSH
  logs/bk7258-auto-debug/20260731-130500  PASS_NSH

COM7 RTS physical reset:
  logs/bk7258-auto-debug/20260731-130551  PASS_NSH, cold_path=yes
  logs/bk7258-auto-debug/20260731-130627  PASS_NSH, cold_path=yes
  logs/bk7258-auto-debug/20260731-130701  PASS_NSH, cold_path=yes
```

三次 cold 日志均包含 `BClk`，因此确实走了 bootloader cold path；三次 warm 日志
均无 `BClk`，判读器没有把二者混淆。第一次和第三次 cold 后又通过 COM11 执行
`apctl status`，所有已启用 gate 均为 `PASSED/error=0`。

最终静态门禁也通过：

```text
bootloader make clean all verify
CP/AP full build
git diff --check
bash -n
Python py_compile
PowerShell parser
git -C /home/lijian/project/open-vela/nuttx diff --exit-code -- .
```

最后一条证明没有修改 NuttX 官方源码。项目修复全部位于 team overlay、bootloader、
打包和自动化脚本中。

### 18.10 最终代码评审

对所有本轮差异按启动顺序复核：

- Tier-1 reset/handoff 的寄存器地址、barrier、set/way 终止条件和 MPU region clear；
- UART handoff flag 的开启/关闭范围，以及两条 polling path 都有固定上限；
- AP MPU region、MAIR、CPU1/CPU2 telemetry 一致性；
- CP/AP WDT ownership 和 bringup 顺序；
- CPU2 三阶段 token 不与 reset/power bits 冲突；
- SMP self-test 的 timeout 与 `sched_yield()` 只用于 task context；
- sparse segment 的 4 KiB padding、上限检查、offset/length 传给 bk_loader 的格式；
- 自动判读按整行 checkpoint 和 `BClk` 前缀匹配，并拒绝任意 `->fail`。

未发现阻塞合入的功能缺陷。一个保留风险是 CPU2 post-bringup /
scheduler-unlock 的底层 `WFE` handshake 没有独立 timeout：这是 scheduler lock
交接区，不能安全调用普通 clock/sleep API。当前 warm/reset 6 次和 `apctl status`
都通过，但“在该窗口故意杀死一颗 core 后自动恢复”没有验证；如有产品级 fault
injection 要求，应单独设计硬件 deadline 和跨核 recovery。

另一个测试边界仍是 physical power cut 未执行。两项都已记录，不影响本次可复现
physical-reset 修复结论，但不能在发布说明中写成已验证。
