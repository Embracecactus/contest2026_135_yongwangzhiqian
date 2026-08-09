# BK7258 N8 — 物理冷复位后 NSH 不出现调研记录

日期：2026-07-30

状态：`cold-reset-nsh-first-pass-board-verified / original-uart-blocker-fixed / ap-autostart-timeout-follow-up / repeatability-matrix-pending`

> 本文记录 N8-C1 完成后发现的物理 RESET 冷启动问题、历史对照、源码路径分析、板端路标、候选修复和验证状态。当前保留临时诊断路标；第二轮 console-restore 已写入源码，但尚未进入任何已生成的 factory 产物，因此不能宣称已完成第二轮板测或最终修复。

## 1. 问题现象

同一套双核 factory 固件出现两种启动结果。

### 1.1 下载器启动 / warm path

```text
u_bootloader enter
partition app @ 0x02010000
jump to:0x02010000
JMP
[hal]
[gpio_log]:gpio:0 was busy: device num:0x22!
[hal]
gpio: 0 is used.Please confirm unmap isn't impact is working module.!

NuttShell (NSH)
nsh>
```

NSH 出现后还可能看到：

```text
[ipc_svr]
create_socket failed.
```

### 1.2 物理 RESET / cold path

```text
u_bootloader enter
BClk A5=8407A76C A9=787BC8A4
partition app @ 0x02010000
jump to:0x02010000
JMP
[hal]
[gpio_log]:gpio:0 was busy: device num:0x22!
[hal]
gpio: 0 is used.Please confirm unmap isn't impact is working module.!
```

随后持续静默：

- 等待超过 15 秒仍无 NSH；
- 串口不能交互；
- 没有再次出现 `u_bootloader enter`；
- 没有形成约 8 秒一次的 WDT reset loop。

用户确认本轮烧录的是包含 bootloader、CP 和 AP 的双核 `all-app-factory.bin`，不是只含 bootloader + CP 的 root `nuttx/all-app.bin`。

## 2. 状态边界

本问题不推翻 N8-C1 已经取得的功能证据，但必须收紧其验证范围：

- N8-C1 的 CPU2 scheduler-online、双向 SMP-call、AP SysTick、周期 sleep-return 和 STAR IRQ context restore 均已在真实板卡通过；
- 这些采样均发生在重新构建、下载 factory image 后的运行路径；
- 现有 N8-C1 记录没有覆盖按键 RESET 或掉电重上电的 cold-start matrix；
- 因此 N8-C1 的多核运行功能仍是 `board-verified`，但“物理冷复位后可重复进入 NSH 并自动恢复 AP SMP”尚未验证，当前已观察到失败。

相关记录：

- [`n8-c1-scheduler-online-idle.md`](n8-c1-scheduler-online-idle.md)
- [`n4-d0-clock-diag.md`](n4-d0-clock-diag.md)
- [`n7-ap-singlecore-bringup.md`](n7-ap-singlecore-bringup.md)

## 3. 调研过程

### 3.1 先区分 warm path 与 cold path

`BClk A5=... A9=...` 来自 Tier-1 bootloader 的 `boot_clock_cold_init()`。只有物理冷态需要执行并打印该冷启动时钟初始化证据；下载器或软复位留下 DPLL/clock residue 时，guard 可以直接跳过该打印。

因此两份日志不是同一路径上的随机差异：

```text
下载后启动：bootloader warm/guard path -> CP app
物理 RESET：bootloader cold clock path -> CP app
```

历史 N4 已经记录 manual reset 与 loader reboot 的硬件初始状态不同，且后续 320 MHz runtime DVFS 的板测主要覆盖 loader/soft-reset 路径。物理 RESET cold path 一直是单独的未闭环验证项。

### 3.2 判断 bootloader 是否卡住

冷复位日志已经完整出现：

```text
BClk A5=8407A76C A9=787BC8A4
partition app @ 0x02010000
jump to:0x02010000
JMP
```

这说明：

1. cold DPLL/flash-clock 初始化返回成功，没有进入 `BClk RETRY FAIL`；
2. FAL 找到 CP app；
3. app header 校验通过；
4. bootloader 已执行 MSP/VTOR handoff 并跳进 CP NuttX。

因此当前静默点不在 bootloader 的 partition lookup 或 jump 之前。`BClk` 是冷路径标志，不是本次直接报错行。

主机侧产物检查还确认当时的 `all-app.bin` 前缀与 team overlay 当前 `bl_crc.bin` 字节一致，cold clock/WDT 代码已进入构建产物。该检查只能证明主机产物代际一致，不能替代板上 flash readback；本轮用户已额外确认烧录的是双核 factory image。

### 3.3 追踪 GPIO busy 日志

GPIO 提示来自 CP SDK UART 初始化中的 GPIO HAL：

```text
arm_serialinit()
  -> bk7258_uart_setup()
  -> bk_uart_init(UART_ID_1)
  -> uart_init_gpio()
  -> gpio_dev_map()
  -> gpio_hal_map_check()
```

bootloader 已经为了 UART1 early console 配置 GPIO0，CP SDK 再检查 pinmux 时看到 second-function 已启用，于是打印：

```text
[gpio_log]:gpio:0 was busy: device num:0x22!
```

同源 SDK 中 `uart_init_gpio()` 不检查 `gpio_dev_map()` 的返回值，而是继续完成 UART common init。因此该提示本身不会直接中止 UART 初始化。更重要的是，warm path 也出现完全相同的两条 GPIO 提示后正常进入 NSH。

结论：

> GPIO busy 是 bootloader early UART 与 SDK UART 重复接管的可见提示，不是本问题已经证实的根因。

它仍然是一个很有价值的时间锚点：静默发生在 CP SDK UART 初始化期间或其后、NSH greeting 之前。

### 3.4 追踪 GPIO 提示到 NSH banner 的完整路径

GPIO 提示出现后，系统还需要完成：

```text
bk7258_uart_setup()
  -> uart_register("/dev/console")
  -> up_initialize() 返回
  -> nx_bringup()
  -> 创建 nsh_main task
  -> sched_unlock()
  -> nsh_main()
  -> nsh_initialize()
  -> boardctl(BOARDIOC_INIT)
  -> board_app_initialize()
       1. bk7258_wdt_initialize()
       2. bk7258_ap_control_initialize()
       3. bk7258_ap_start()            [CONFIG_BK7258_AP_AUTOSTART]
       4. GPIO lower-half
       5. procfs
       6. flash MTD + LittleFS probe
  -> nsh_consolemain()
  -> nsh_session()
  -> 输出 NuttShell banner
```

所以最后看到 GPIO 提示并不能说明一定卡在 GPIO；串口 setup、NSH task 启动、WDT 接管、AP autostart、procfs 和 flash/LittleFS 都位于该提示与 banner 之间。

### 3.5 对照 N7 历史 WDT 问题

N7 曾出现高度相似的外观：AP/GPIO 日志后反复回到 `HFu_bootloader enter`。当时根因是：

```text
旧顺序：AP start 最长等待 3 秒 -> WDT initialize
```

bootloader AON WDT 在 AP wait 尚未结束时拉低整 SoC，形成 reset loop。修复后顺序为：

```text
bk7258_wdt_initialize()
  -> bk_aon_wdt_stop()
bk7258_ap_control_initialize()
bk7258_ap_start()
```

当前 `board_app_initialize()` 仍保持该正确顺序，而且本轮冷复位静默超过 15 秒，没有再次进入 bootloader。因此：

> 当前问题与 N7 属于同一个“NSH 前 AP/WDT/cold-state handoff”问题域，但不是已经修复的 WDT-before-AP 顺序回归。

### 3.6 对照 N6 SysTick/WDT 问题

N6 曾出现进入 NSH 后 32 秒、随后 8 秒的稳定 WDT 重启。其根因是 CPU0 SysTick 走错 dispatcher，WDog queue 和 automonitor keepalive 没有被服务。

当前现象不同：

- NSH 尚未出现；
- 15 秒以上没有 WDT reset loop；
- N8-C1 下载路径中的 CPU0 SysTick 已经持续增长。

因此不能直接复用 N6 的“SysTick dispatcher 已回归”结论。cold path 的 SysTick/timeout 仍应在下一次板测中检查，但当前没有证据证明它已经是根因。

### 3.7 排除 AP segment 缺失

root `nuttx/all-app.bin` 只含 bootloader + CP。如果打开 `CONFIG_BK7258_AP_AUTOSTART` 却只烧 root image，AP slot 可能缺失或残留旧镜像，确实会在 NSH 前失败。

本轮用户确认烧录的是：

```text
nuttx/bk7258-dual/all-app-factory.bin
```

因此“只烧 CP、AP segment 未更新”不作为本次主因继续追踪。

### 3.8 排除 IPC socket 日志

`[ipc_svr] create_socket failed.` 出现在 warm path 已进入 NSH 之后。历史 worklog 也将其归类为旁支。它不能解释 cold path 在 NSH greeting 之前静默。

### 3.9 Phase A 板端路标完成定位

恢复 raw UART checkpoint 后，同一固件取得了明确 A/B：

- loader/soft path 从 `S0`、`U0..U5`、`C0..C8` 完整进入 NSH；
- physical RESET cold path 出现 `BClk`，随后到达 `S0/U0/U1`；
- cold path 在 `bk_uart_init()` 内打印完 GPIO0 busy/unmap 提示后停止，未到达 `U2`；
- 因而 AP autostart、第一次 `nxsig_usleep()`、procfs、flash 和 LittleFS 均未执行，不是本次 blocker。

对应 SDK 源码路径为：

```text
bk_uart_init()
  -> uart_id_init_common()
     -> uart_init_gpio()
        -> gpio_dev_unmap()
        -> gpio_dev_map()
           -> gpio_hal_func_map()
              -> gpio_ll_set_gpio_perial_mode()
```

BK7258 GPIO LL 的 `gpio_system_gpio_func_mode` 是 translation-unit static 指针，只在：

```text
bk_gpio_driver_init()
  -> gpio_hal_init()
     -> gpio_ll_init()
```

中设置为系统 pinmux 寄存器基址。现有启动顺序却先在 `arm_serialinit()` 中执行 `bk_uart_init()`，直到后续 `C5` 才由 GPIO lower-half 调用 `bk_gpio_driver_init()`。因此 cold boot 的 UART pinmux 路径会在 GPIO HAL 尚未初始化时访问 peripheral-mode table；warm hardware/residue 可以掩盖该顺序缺陷。

第一轮最小修复是在 `bk7258_uart_setup()` 中先执行幂等的 `bk_gpio_driver_init()`，确认成功后再执行 `bk_uart_driver_init()` 和 `bk_uart_init()`。后续 GPIO lower-half 的重复初始化会由 SDK 自身的 `s_gpio_is_init` guard 直接返回，不改变 C5 的设备注册职责。

### 3.10 GPIO-before-UART 修复复测结果

physical RESET 复测得到：

```text
S0
U0
G1
U1
[gpio_log]:gpio:0 was busy: device num:0x21!
gpio: 0 is used...
```

随后仍未出现 `U2`。

这组证据说明：

1. `bk_gpio_driver_init()` 已返回，GPIO HAL 初始化顺序缺陷已补齐；
2. busy 日志中的 device 从此前错误的 `0x22` 变为正确的 `GPIO_DEV_UART1_TXD = 0x21`，证明 peripheral-mode table 已指向正确寄存器；
3. 但 GPIO-before-UART 不是完整修复，剩余静默仍位于 `bk_uart_init()` 返回点之前或返回后 UART 输出参数被改写的窗口；
4. SDK 在 GPIO 路径之后还会分配 kfifo/semaphore、再次选择 UART clock，并由 `uart_hal_init_uart()` 重写 divider/config。

由于 `U2` 本身依赖同一个 UART，单纯“没有 U2”不能区分函数没有返回和函数返回后 baud/clock/pinmux 已使串口不可见。第二轮最小修复因此在 `bk_uart_init()` 返回后、打印 `U2` 之前，重新声明 Tier-1 bootloader 已板测的 UART1 console invariant：

- UART1 clock gate enable；
- UART1 source = 26 MHz XTAL；
- UART1 global control = `1`；
- UART1 config = `0x0000371b`（divider 55、8N1、TX+RX enable）。

如果第二轮出现 `U2`，即可确认此前是 SDK hardware reconfiguration 导致的 console loss；若仍无 `U2`，则确认 `bk_uart_init()` 本身未返回，需要继续在 OS-adapter allocation/semaphore 边界增加非依赖 console reconfiguration 的路标。

### 3.11 23:30 physical RESET 日志属于旧产物，不构成第二轮复测

用户随后提供了一次 physical RESET 日志：

```text
u_bootloader enter
BClk A5=8407A76C A9=787BC8A4
partition app @ 0x02010000
jump to:0x02010000
JMP
S0
U0
G1
U1
[gpio_log]:gpio:0 was busy: device num:0
gpio: 0 is used.Please confirm unmap isn
```

该日志仍未出现 `U2`，并且 GPIO 提示在半句处中断；但主机产物核对证明这不是第二轮 restore 的有效验证：

```text
nuttx/nuttx:                         2026-07-30 23:00:49 +0800
nuttx/bk7258-dual/all-app-factory.bin: 2026-07-30 23:00:51 +0800
bk7258_serial.c:                    2026-07-30 23:08:39 +0800
```

当前 ELF 中存在旧版 `bk7258_uart_setup`，但没有 `bk7258_uart_restore_console` 符号；结合产物时间可确认板端仍运行第二轮源码修改之前的镜像。因而：

1. 这次日志只能再次复现第一轮镜像的 cold failure；
2. 不能据此判断 restore 成功或失败；
3. 不能据此转入 `bk_uart_init()` 内部插桩；
4. 下一动作仍是重新构建并确认 factory image 时间晚于 `23:08:39`，再执行 warm + physical RESET。

### 3.12 第二轮 `ap_smp_bidir` 双核 factory 构建与入包验证

按用户指定命令执行：

```bash
cd /home/lijian/project/open-vela
AP_CONFIG_NAME=ap_smp_bidir \
  ./contest2026_135_yongwangzhiqian/board/bk7258/scripts/build_dual_image.sh
```

构建于 2026-07-30 23:35 +0800 成功退出，返回码为 0。配置组合为：

```text
CP: cp_nsh
AP: ap_smp_bidir
```

新产物：

| 产物 | 字节数 | SHA-256 |
|---|---:|---|
| `nuttx/nuttx.bin` / raw CP `app.bin` | 179328 | `128771c286d35849e321f80acc373a18c1db8143e2a2de7130bd837fb4251c21` |
| CP `app_crc.bin` | 190536 | `5e7b864eebeeda3994712bc1eb8cdaa87183cdb496d82952222ae7b8c724e014` |
| AP `app1.bin` | 66520 | `5dc6fbf58b28ed838d79328f2e2cf85385ad564493eee223f0721ab85623b8d8` |
| AP `app1_crc.bin` | 70686 | `c563308f74b46e2aacef9d4bca03b5ba4cb9a51ae3c3f338927ad4f7b2087f4b` |
| `all-app-factory.bin` | 2298910 | `d83c8e38bec19160f9d54d0832a4f553dab85bd568173f2a1ebe4fc9e860d405` |

factory manifest 与逐字节比较确认：

- bootloader：`bl_crc.bin` 位于 physical `0x00000000`，exact match；
- CP：`app_crc.bin` 位于 physical `0x00011000`，长度 `0x2e848`，exact match；
- AP：`app1_crc.bin` 位于 physical `0x00220000`，长度 `0x1141e`，exact match；
- root `app.bin/app_crc.bin` 与 `bk7258-dual/` 中保存的 CP 副本 exact match。

CP ELF 的 `bk7258_uart_setup()` 反汇编还确认第二轮 restore 已进入可执行代码。编译器将 `bk7258_uart_restore_console()` 内联，因此没有独立符号；但在 `bk_uart_init` 调用后立即可见：

```text
UART1 clock enable: 0x44010030 bit10 set
UART1 source:       0x44010020 bit13 clear
UART1 global ctrl:  0x45830008 = 1
UART1 config:       0x45830010 = 0x371b
dsb sy
isb sy
随后才检查 bk_uart_init 返回值并打印 U2
```

因此第二轮候选修复当前状态是 `build-verified`，新 factory image 已可用于真正的 warm + physical RESET 板测。该镜像 AP 配置是 `ap_smp_bidir`，板端高级门禁应对应 BP2P baseline，而不是 BLCY lifecycle。

### 3.13 WSL2 → Windows 自动下载与双串口采集框架

Windows PnP 核对确认当前 CH342 双串口分工：

```text
COM7  = USB-Enhanced-SERIAL-A CH342，bk_loader 下载口
COM11 = USB-Enhanced-SERIAL-B CH342，460800 固件 console
COM12 = J-Link CDC UART
COM9  = CH340
```

因此 `bk_loader.exe download -p 7` 是正确下载端口；COM11 不能替换 `-p 7`，而应被独立打开用于捕获 `S/U/G/C/A/W` 路标和 NSH。已新增：

```text
board/bk7258/scripts/bk7258_auto_debug.sh
board/bk7258/scripts/capture_windows_serial.ps1
```

自动化能力包括：

- 可选重新构建 `CP=cp_nsh / AP=ap_smp_bidir`；
- 在下载前打开 Windows COM11 460800/8N1 raw capture；
- 从 WSL 调用 Windows `bk_loader.exe`，通过 COM7 下载 factory；
- 保存 download log、raw serial、text serial、artifact hash 和 marker summary；
- 自动判定 `PASS_NSH`、`STOP_BETWEEN_U1_U2`、`STOP_AFTER_C8_BEFORE_NSH` 等结果；
- 支持只采集 physical RESET，但按键/断电仍需人工或外部继电器。

3 秒非破坏性 COM11 自检没有执行下载或复位，但 `SerialPort.Open()` 返回访问被拒绝，说明 COM11 当前被其他 Windows 串口终端占用。Windows 进程核对显示 `MobaXterm` 正在运行，是当前最可能的占用者；不自动结束该进程，需先由用户关闭对应串口 tab。

### 3.14 J-Link RESET pin 自动 physical reset 能力

Windows 已安装 SEGGER J-Link Commander V9.54，探针连接成功：

```text
J-Link hardware: V9.70
S/N:             20790067
VTref:           3.293 V
interface:       SWD 1000 kHz
target:          CORTEX-M33
```

J-Link Commander 内置命令明确包含：

```text
Reset                 按当前 reset strategy 复位 CPU
RSetType <Type>       设置 reset strategy
ClrRESET              直接清低 RESET 引脚
SetRESET              直接置高/释放 RESET 引脚
```

J-Link 目标连接后返回的 reset type 映射确认：

```text
0 = NORMAL，SYSRESETREQ/VECTRESET
1 = CORE only
2 = RESETPIN，使用 RESET pin 复位 core + peripherals
3 = CONNECT UNDER RESET
8 = CORE AND PERIPHERALS，SYSRESETREQ only
```

因此自动 cold reset 的首选目标命令应为：

```text
RSetType 2
Reset
Go
```

直接 `ClrRESET/SetRESET` 可用于引脚电平诊断，但首轮实测未能将 pin 拉低。

只要 J-Link RESET 线确实接到 SoC nRESET，该序列等价于人工按下并释放 reset 键，可与提前打开的 COM11 raw capture 联动，自动取得 `BClk` cold trace。它仍不等价于完整断电重上电；power-cycle 矩阵需要 USB 继电器、可编程电源或人工断电。SWD 只读连接已成功读取 `CPUID=0x631f1320` 并识别 STAR r1p0。完整用法见 [`n8-cold-reset-automation.md`](n8-cold-reset-automation.md)。

### 3.15 自动下载与首轮 warm/J-Link reset 实测

COM11 释放后，3 秒只读采集自检成功打开端口。随后通过 COM7 自动下载第二轮 factory：

```text
factory SHA-256: d83c8e38bec19160f9d54d0832a4f553dab85bd568173f2a1ebe4fc9e860d405
bk_loader: Writing Flash OK / All Finished Successfully
```

`bk_loader.exe` 进程返回码为 1，但工具日志明确报告下载成功；自动化后续应以成功文本和 flash verify 共同归一化该工具的异常退出码。

warm path 第一段 25 秒采集：

```text
S0 U0 G1 U1
GPIO device 0x21
U2 U3 U4 U5
C0 C1 C2 C3
A0 A1 A2 A3 A4 A5 A6
W0 W1
```

关键结论：第二轮 console restore 首次板端通过，`bk_uart_init()` 已返回，原 cold 调查中的 UART 输出丢失分支被真实板端 `U2` 证实可恢复。

随后尝试在 COM11 capture 已开启时执行：

```text
ClrRESET
Sleep 100
SetRESET
Sleep 100
```

J-Link 报告：

```text
WARNING: RESET (pin 15) high, but should be low. Please check target hardware.
```

串口没有出现 `BClk/S0`，因此 RESET pin 没有被确认拉低，本次不算 physical cold reset。该 capture 只收到了上一 warm path 的后续：

```text
A7 F1 F2 C4 C5 C6 C7 C8
NuttShell (NSH)
nsh>
```

所以完整 warm 启动最终进入 NSH，但 AP READY wait 走了 failure/cleanup 路径；AP `ap_smp_bidir` 本轮没有在 CP 默认 timeout 内达到 READY。NSH 下 `apctl status` 显示：

```text
AP state=FAILED error=6 generation=1 heartbeat=0
CPU2 state=SCHEDULER_ONLINE error=0 ready=1 online=0x3
AP SMP state=PASSED error=0
AP affinity state=RUNNING，task started/completed=1/0
AP sem-loop state=WOKEN，requested/completed=8/7
```

枚举确认 `error=6` 是 `BK7258_AP_ERROR_TIMEOUT`，由 CP `bk7258_ap_start_locked()` 等待 READY 超时后写入。也就是说 AP/CPU2/SMP 已真实运行，但高级测试尚未收口到 READY，CP 即执行 F1/F2 reset/power cleanup；这与已解决的 UART blocker 是两个独立问题。

下一步分为两项：

1. 使用 `RSetType 2 -> Reset -> Go` 尝试 J-Link 明确的 RESETPIN strategy；
2. cold-reset 闭环后，再单独处理 `ap_smp_bidir` 在默认 AP timeout 内未完成的问题。

### 3.16 用户手动 RESET 取得真实 cold-path U2 证据

用户随后手动按下板卡 RESET 键，取得：

```text
u_bootloader enter
BClk A5=8407A76C A9=787BC8A4
partition app @ 0x02010000
jump to:0x02010000
JMP
S0
U0
G1
U1
GPIO busy device 0x21
U2
U3
U4
U5
C0 C1 C2 C3
A0 A1 A2 A3 A4 A5 A6
W0 W1
```

这是真实 physical cold reset，理由是：

- bootloader 执行并打印 `BClk` cold-clock path；
- app 从 `JMP` 后重新进入 `S0`；
- 不是上一 warm boot 的日志续段。

与首次 J-Link `ClrRESET/SetRESET` 尝试相比，两者明确不同：J-Link 当时报告 `RESET pin high, but should be low`，没有 `BClk/S0`，只捕获上一 warm path 的 `A7/F1/F2/C4..NSH` 后续。因此首次 J-Link 尝试不构成硬件复位。

最关键结论：

> 第二轮 `bk7258_uart_restore_console()` 已在真实 physical cold reset 下通过。cold path 完整出现 `U2/U3/U4/U5` 并进入 `C0`，原始“GPIO 日志后无 U2/NSH”的 UART blocker 已修复。

用户继续等待数秒后，cold path 完整输出：

```text
W1
A7
F1
F2
C4
C5
C6
C7
C8
NuttShell (NSH)
nsh>
[ipc_svr] create_socket failed.
```

这完成了第一次真实 physical cold-reset 闭环：

- bootloader cold clock `BClk` 通过；
- UART cold setup 越过 `U2..U5`；
- WDT 接管和 board bringup 继续执行；
- AP READY wait 超时后执行 `F1/F2` deterministic cleanup；
- CP 按设计 fail-open，完成 `C4..C8` 并进入 NSH；
- `[ipc_svr] create_socket failed.` 发生在 NSH 已可用之后，仍是旁支，不是启动 blocker。

因此原始“physical RESET 后 GPIO 日志之后永久静默、NSH 不出现”问题已经首次板端修复通过。当前剩余问题是独立的 `ap_smp_bidir` READY timeout，不再属于 cold-reset NSH blocker。最终 `board-verified` 收口仍需 physical RESET 重复 3 次和掉电重启矩阵。

J-Link reset 只有在日志同样出现：

```text
u_bootloader enter
BClk ...
...
JMP
S0
```

时，才能视为与手动 RESET 等价；否则只能算 core/system reset 或未复位。

## 4. 根本原因分层结论

### 4.1 已确认的根本原因层级

当前已经确认两项事实：

1. 原启动顺序确实缺少 GPIO-before-UART 初始化；补齐后 GPIO peripheral-mode 识别从错误的 `0x22` 修正为 UART1 TX 的 `0x21`；
2. 该修复不足以恢复 cold console，静默仍发生在 `bk_uart_init()` 的 GPIO 日志与 wrapper 的 `U2` 之间。

GPIO busy 两条日志仍不是错误本身，而是精确时间锚点。剩余问题有两个互斥分支：

- `bk_uart_init()` 在 GPIO 后续的 allocation/semaphore/clock 路径内没有返回；
- `bk_uart_init()` 已返回，但末尾 `uart_hal_init_uart()` 的 clock/divider/config 重写使 460800 console 消失，因而 `U2` 和后续 NSH 实际不可见。

无论是哪一分支，blocker 都发生在 `C0` 之前，与 AP、WDT、SysTick wait 和文件系统无关。

### 4.2 当前最强技术落点

当前修复落点仍限制在 console SDK wrapper：

```text
bk7258_uart_setup()
  -> bk_gpio_driver_init()        [建立 GPIO HAL/pinmux state]
  -> bk_uart_driver_init()
  -> bk_uart_init()
  -> bk7258_uart_restore_console()[重新声明板级 UART clock/config invariant]
  -> disable SDK software FIFO
  -> set RX threshold = 1
```

第二轮恢复函数只写 Tier-1 bootloader 和 N2 已验证的 UART1 控制面：

- `0x44010030` UART1 clock gate bit10；
- `0x44010020` UART1 source bit13 = 0（XTAL）；
- `0x45830008 = 1`；
- `0x45830010 = 0x0000371b`。

该修复满足以下边界：

1. 只修改 team-overlay 的 `bk7258_serial.c`；
2. 保留 SDK 软件态、kfifo、semaphore 和 per-ID initialized state；
3. 不改变 460800、8N1、FIFO、IRQ bridge 或 NuttX upper-half 语义；
4. 不改变后续 GPIO lower-half 的 `/dev/gpio0`、`/dev/gpio1` 注册；
5. 不触碰已验证的 AP lifecycle、mailbox/SMP ABI、WDT、flash 或 LittleFS。

### 4.3 板端定位后的候选收敛

当前一次 physical RESET 已完成分层定位：

- UART setup：确认进入 `bk_uart_init()`，停在 GPIO pinmux 子路径，未返回 `U2`；
- console registration：未执行；
- WDT/AP autostart/第一次 AP wait sleep：未执行；
- procfs/flash/LittleFS：未执行。

因此本轮不再做关闭 AP autostart、修改 SysTick timeout 或关闭文件系统的 A/B。唯一待验证项是补齐 GPIO-before-UART 初始化顺序后，physical RESET 是否能够越过 `U2` 并完整到达 `C8/NSH`。

## 5. 后续修复方案

Phase A 已完成并将 blocker 定位到 UART GPIO HAL 初始化顺序；最小候选修复已经应用，当前等待同固件 warm/cold 板端验证。

### Phase A：一次物理 RESET 完成分层定位（已完成）

使用 raw polled UART 路标，不依赖 `/dev/console`、printf 或 syslog。

建议路标：

| 路标 | 含义 |
|---|---|
| `S0` | 进入 `arm_serialinit()` |
| `U0` | 进入 `bk7258_uart_setup()` |
| `U1` | `bk_uart_driver_init()` 返回 |
| `U2` | `bk_uart_init()` 返回 |
| `U3` | SDK software FIFO disable 完成 |
| `U4` | UART setup 完成 |
| `U5` | `/dev/console` 注册完成 |
| `C0` | 进入 `board_app_initialize()` |
| `C1` | WDT 接管完成 |
| `C2` | AP control initialize 返回 |
| `C3` | 即将 AP autostart |
| `A0..A6` | CPU1/CPU2 reset、mailbox/state、power、boot address、release 和 wait 入口 |
| `W0/W1` | 第一次 `nxsig_usleep(1 ms)` 前/后 |
| `A7` | AP wait 返回 |
| `C4` | AP autostart 返回 |
| `C5` | GPIO lower-half 完成 |
| `C6` | procfs 完成 |
| `C7` | flash/LittleFS 完成 |
| `C8` | board app init 返回 |

只需一次完整 cold-reset 串口记录即可把问题分到 UART、AP lifecycle、timer 或 flash/LittleFS；在此之前不修改功能行为。

### Phase B：最小顺序修复（已应用，待板测）

在 `bk7258_uart_setup()` 中，将 SDK 依赖初始化固定为：

```text
bk_gpio_driver_init()
bk_uart_driver_init()
bk_uart_init()
```

新增临时路标：

- `G1`：GPIO driver/HAL 初始化返回；
- `EG`：GPIO driver 初始化失败。

板测判据：

- cold path 出现 `G1/U1`、GPIO 提示、`U2..U5`：GPIO-before-UART 顺序修复越过原 blocker；
- 继续到 `C8/NSH`：cold-start 启动问题修复成立，随后执行重复性矩阵；
- 停在 `G1` 之后但仍无 `U2`：继续区分 `bk_uart_init()` 真阻塞与 UART baud/pinmux 输出丢失，不转向 AP 或文件系统。

### Phase C：若最小顺序修复未通过的后备分支

#### C1. 若定位到 AP lifecycle

目标不是简单延长 timeout，而是让 cold/warm 都从确定状态启动：

1. CPU1、CPU2 先进入明确 reset hold；
2. 按 SDK 定义的 power-up → RXEVT → boot address → reset release 顺序初始化；
3. 仅在两个 AP 核都处于 reset hold 时清 mailbox 和 shared boot/IPI/SMP state；
4. 不依赖 retained SRAM 中的旧 `READY/STARTING/generation` 判断当前硬件是否真的在线；
5. 为每个硬件状态等待增加有界 timeout 和失败状态；
6. AP 失败必须 fail-open 到 CP NSH，而不是让 CP 启动永久阻塞；
7. 保留已经验证的 WDT-before-AP 顺序；
8. 不改变 N8-C1 已验证的 mailbox/SMP command ABI。

#### C2. 若定位到 UART setup

1. 将“GPIO 已映射为目标 UART function”视为 idempotent success，而不是重复资源冲突；
2. 检查 `bk_uart_init()`、FIFO、RX threshold 和 `uart_register()` 每一步返回值；
3. console 注册失败时保留 early-polled UART 错误码，而不是静默 return；
4. 不通过禁用 GPIO warning 掩盖实际 console 初始化失败。

#### C3. 若定位到第一次 sleep/SysTick

1. 采集 cold path 的 M1、A5/A9、CPU Hz、SysTick CSR/RVR/CVR；
2. 确认 `ENABLE/TICKINT/CLKSOURCE` 和 `320 MHz / 100 Hz - 1` reload；
3. 在调度器/timeout 尚未被证明可用的早期路径使用有界 busy wait，不让唯一启动 task 阻塞；
4. 保持 N8-C1 已验证的 IRQ context-restore wrapper，不回退 dispatcher。

#### C4. 若定位到 flash/LittleFS

1. 分别 A/B 关闭 LittleFS、保留 MTD，再关闭 MTD；
2. 为 flash busy/protection wait 增加 timeout；
3. 必要时将非关键文件系统挂载延后到 NSH 可用之后；
4. 不破坏 N5 已验证的 `/data/probe.txt` 持久化语义。

### Phase D：修复验收矩阵

修复不能只重复“下载后能启动”。至少执行：

| 启动方式 | 次数 | 核心门禁 |
|---|---:|---|
| factory 下载后首次启动 | 1 | NSH、AP READY、CPU2 scheduler-online |
| loader soft reboot | 3 | NSH、AP generation、SysTick、sleep-return |
| 按键 physical RESET | 3 | 必须每次进入 NSH，无静默、无 reset loop |
| 断电重上电 | 3 | 与 physical RESET 同等门禁 |
| AP status 连续采样 | 3 | heartbeat/SysTick/sleep-return 持续增长 |
| LittleFS | 1+ | `/data/probe.txt` 保持或按 factory 语义重新建立 |

每次都要保留完整串口，从 `u_bootloader enter` 开始记录，明确区分是否出现 `BClk`。

## 6. 已排除与禁止回退项

当前不要重复以下方向：

- 不把 GPIO0 busy 提示当作根因直接 unmap/禁用；
- 不把 `[ipc_svr] create_socket failed.` 当作启动 blocker；
- 不回退 N8-C1 的 AP STAR `arm_doirq` / `nxsched_resume_scheduler` wrapper；
- 不恢复 N4 已证伪的 BootROM 隐式 DPLL 假设；
- 不改 mailbox command ABI 或已经板测的 SMP data path；
- 不仅通过延长 AP timeout 掩盖 cold-state 初始化不完整；
- 不因为下载后成功就再次宣布 cold reset 已验证。

## 7. 当前恢复点

截至本文写入：

- Phase A 已取得同固件 warm/cold A/B，cold blocker 位于 `bk_uart_init()` 的 GPIO pinmux 路径；
- AP、WDT、SysTick wait、procfs、flash 和 LittleFS 已从本轮 blocker 中排除；
- `bk7258_serial.c` 已在 UART 初始化前增加幂等的 `bk_gpio_driver_init()`；该项复测确认生效但不足以恢复 console；
- `bk_uart_init()` 返回后、`U2` 前新增 `bk7258_uart_restore_console()`，重置 UART1 为已验证的 26 MHz/460800/8N1/TX+RX 状态；
- `G1/EG` 与原 U/S、C、A/W/F 路标暂时保留，待 cold-reset 修复通过后统一清理；
- 23:30 收到的 physical RESET 日志来自 23:00 旧 factory image，只再次复现第一轮 failure，不是第二轮验证；
- 第二轮 console-restore 已于 23:35 使用 `CP=cp_nsh / AP=ap_smp_bidir` 构建成功，CP restore 反汇编和 factory CP 段逐字节比较均已通过；
- 第二轮新 factory 尚未下载或执行板端验证；
- COM11 已释放并完成自动下载；warm path 已越过 U2 且最终进入 NSH，但 AP wait 走 F1/F2 failure cleanup；
- J-Link ClrRESET/SetRESET 报 RESET pin 仍为 high，本轮没有形成 BClk cold path；
- 未执行 Git 操作；
- 未执行额外 static verifier；
- 下一步由用户构建并下载同一份双核 factory image，先执行一次 warm path，再执行 physical RESET cold path。

## 8. 当前结论摘要

```text
N8-C1 多核功能：board-verified（下载/warm path）
物理 cold reset：BClk→U2..U5→A7/F1/F2→C8→NSH 首次完整通过，原永久静默 blocker 已修复
bootloader cold clock：已越过，非当前直接卡点
GPIO-before-UART：缺陷已修，device 识别由 0x22 修正为 UART1 TX 0x21，但不足以恢复 console
UART 根因结论：SDK 重配后需恢复板级 clock/config；GPIO-before-UART + console restore 已经 cold-board-verified
当前修复：bk_uart_init() 返回后恢复 26 MHz/460800/8N1/TX+RX，真实 cold path 已打印 U2
第二轮构建：build-verified；factory SHA-256=d83c8e38bec19160f9d54d0832a4f553dab85bd568173f2a1ebe4fc9e860d405
入包证据：restore 内联指令存在，CP app_crc 与 factory@0x11000 exact match
自动化：COM7 下载和 COM11 capture 已实测；J-Link RESET pin 未成功拉低
UART结论：新 factory warm 和手动 physical cold path 均出现 U2..U5，原 blocker 已修复
当前动作：执行 physical RESET 余下重复性矩阵；AP READY timeout 作为独立后续；矩阵通过后删除临时 checkpoint
```
