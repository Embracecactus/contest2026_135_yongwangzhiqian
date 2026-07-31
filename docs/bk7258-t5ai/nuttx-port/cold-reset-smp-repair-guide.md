# BK7258 冷复位与 SMP 修复入门指南

日期：2026-07-31

状态：正式镜像 `board-verified`，warm 3/3、physical reset 3/3，AP SMP 全部通过

这篇文档面向第一次接触 BK7258、多核启动或 bootloader 的同学。先不用记寄存器，
只要抓住一个原则：

> warm 启动“碰巧能跑”，不等于上电或 RESET 后的硬件初始状态正确。

本次故障正是多个旧状态互相掩盖：UART 来不及发完、cache 中残留旧数据、watchdog
所有权不清楚、CPU2 已启动但调度器还没准备好。临时日志会改变时序，所以删除日志后还要
用正式镜像重新验证。

## 1. 这块芯片上谁在运行什么

可以把系统想成三个人协作：

```text
BootROM
  └─ Tier-1 bootloader
       └─ CP / physical CPU0：NuttX UP、UART、NSH、启动 AP
            └─ AP / physical CPU1：NuttX SMP 主核
                 └─ physical CPU2：AP NuttX 的第二个逻辑 CPU
```

- CP 是控制面，负责启动 AP、提供 NSH 和板级设备；
- AP 是另一个 NuttX 镜像；
- CPU2 不是第三份固件，而是 AP 这份 SMP NuttX 的 secondary CPU；
- shared SRAM 是三者交换状态的“白板”，cache/MPU 属性错了就会各看各的旧内容。

## 2. 原始症状

下载器 reboot 后通常能看到 NSH；按 RESET 或 RTS reset 后却停在 GPIO 提示：

```text
u_bootloader enter
BClk ...
partition app @ 0x02010000
jump to:0x02010000
JMP
gpio: 0 is used.Please confirm unmap isn't impact...
```

这段日志已经证明 bootloader 找到了 CP app 并完成跳转。问题不一定在
“bootloader 能否跳转”，也可能是 app 继承的硬件状态不符合预期。

另一个阶段性症状是：

```text
AP state=FAILED error=6
```

`error=6` 表示 CP 等 AP READY 超时。后续发现它不是“AP 完全没运行”，而是 cache、
握手和调度时机共同造成的假象。

## 3. 如何避免被临时日志骗到

排查时使用 raw UART checkpoint 很有效，因为 `/dev/console` 还没注册也能输出。但
checkpoint 会增加几十到几百微秒延迟，可能把 race 暂时隐藏。

正确做法分两轮：

1. 诊断镜像：用 checkpoint 定位函数窗口；
2. 正式镜像：删掉所有 checkpoint，重新全量构建、重新烧录，再做重复性验证。

自动判读也必须按“整行”匹配。例如 `A7` 不能误匹配
`BClk A5=8407A76C` 中间的字符。

## 4. 官方 SDK 告诉了我们什么

本次分析的技术支持 SDK 是：

```text
C:\Users\lijian\Downloads\BK7258_SMP\bk_avdk_smp-release-v3.1.1.9.tar.gz
```

只在 `/tmp` 解出需要的文件做只读分析，没有修改官方包：

```text
/tmp/bk7258-sdk.oPn1zx/bk_avdk_smp-release-v3.1.1.9
```

从官方源码确认了四条重要契约：

1. AP shared SRAM `0x28000000..0x3fffffff` 是 Inner Shareable、
   Normal non-cacheable，MAIR attribute 1 为 `0x44`；
2. 使用 D-cache 前要 clean/invalidate，不能假设 RESET 自动清掉所有 cache 状态；
3. UART1 的 TX/RX 是 GPIO0/1，`bk_uart_init(UART1)` 会先打印提示，再 unmap/remap；
4. WDT stop/feed 有 APB 和 AON 两套 ownership，不能只关其中一个。

注意：项目约束是不修改 NuttX 官方源码。这里是读 SDK 学硬件契约，然后在 team overlay
中实现。

## 5. bootloader 为什么也有关

官方 v3.1.1.9 normal bootloader：

```text
size        52352 bytes
SHA-256     105161bb603eedafbffcb5efb8f7c06a0c8503e42ba4da46490c2c21ed813de6
base        0x02000000
SP          0x28030000
reset       0x020001c1
version     bc31115
```

Ghidra 识别出 134 个函数、224 条调用边。官方 reset path 做的事情远不止
“读 MSP/Reset_Handler 然后跳转”：

```text
早期 SoC/flash/WDT/UART/clock
  -> MSPLIM/runtime data
  -> cache/MPU 初始化或清理
  -> app 选择和检查
  -> 关闭/清理运行环境
  -> 设置 VTOR/MSP
  -> 跳转 app
```

当前项目的 bootloader 不需要照搬官方 RBL、OTA、下载协议或分区格式，因为我们使用
raw NuttX/FAL 双镜像布局；但 app handoff 前后的 cache、MPU、core power 和 watchdog
状态必须有确定值。

本轮在 `boot_runtime.c` 和 `start.S` 中补齐了这些必要契约，同时保留原有分区和跳转
功能。这里称为“clean-room 复现启动契约”，不宣称完整逐字节逆向了官方 52 KB。

一个容易抄错的地址是：

```text
SCB base = 0xE000ED00
```

旧文档中的 `0xED00E000` 是字节顺序写错，不能用于代码。

## 6. UART 的三个真实问题

### 6.1 GPIO 必须先准备好

旧顺序：

```text
bk_uart_init()
稍后才 bk_gpio_driver_init()
```

正确顺序：

```text
bk_gpio_driver_init()
bk_uart_driver_init()
bk_uart_init()
```

否则 cold state 下 GPIO HAL 的 pinmux table 还没初始化，warm residue 却可能让它碰巧
工作。

### 6.2 SDK 初始化后要恢复板级 console

SDK 会重新配置 UART clock 和 divider。项目 console 是 UART1、460800 8N1，因此
`bk_uart_init()` 返回后要恢复这些板级 invariant，才能保证 NSH 继续可见。

### 6.3 GPIO ownership 切换前必须真正发完最后一个字节

UART 有两个容易混淆的状态：

```text
WR_READY：FIFO 还能再塞字符
TX_EMPTY：FIFO 和发送移位寄存器都空了，最后一位已经上线路
```

旧 `up_putc()` 等到 `WR_READY` 就返回。SDK 紧接着把 GPIO0 从旧 UART ownership
切走，最后一个字符可能还没发完，于是正式镜像稳定停在半截提示。

最终实现增加了 `bk7258_lowputc_handoff(bool)`：

- 正常期只等 `WR_READY`，不拖慢 console；
- SDK 接管 GPIO0/1 的小窗口里，写完后有界等待 `TX_EMPTY`；
- 轮询有上限，硬件异常时不会永久卡死。

这也解释了为什么“带很多调试日志时正常、删掉日志后又坏了”：日志改变了时序。

## 7. cache 和 MPU 为什么会影响 AP READY

CPU0、CPU1、CPU2 都读写 shared SRAM。若一颗 CPU 从 cache 读旧值，它就可能永远看不到
另一颗 CPU 已经写入 READY。

最终 AP 早期入口在使用 shared SRAM 前：

1. 关闭 D-cache；
2. 根据 CCSIDR 按 set/way invalidate；
3. invalidate I-cache；
4. 用 MPU region 15 覆盖 `0x28000000..0x3fffffff`；
5. 设置 non-cacheable shared 属性后再继续。

板端读回值：

```text
CCR          0x00000201
MPU_CTRL     0x00000007
RBAR15       0x2800001a
RLAR15       0x3fffffe3
```

CPU1 和 CPU2 都一致，这比只看源码更可靠。

## 8. CPU2 握手为什么要分三步

“CPU2 开始执行”不等于“CPU2 调度器已经可以接 task”。最终用三个 token 区分：

```text
boot ready
  -> post-bringup
     -> scheduler unlock
```

这些 token 位于 uncached control register/shared state，避免 cache 再次破坏握手。

另一个 race 是：remote IPI 已到达，但 AP 当前 CPU 的本地 task 尚未得到一次真正调度。
因此 AP self-test 的相关 bounded poll 每轮执行：

```text
up_mdelay(1);
sched_yield();
```

- `up_mdelay(1)` 保证硬件 timeout 不依赖可能异常的 signal sleep；
- `sched_yield()` 给刚刚被远端唤醒的本地 task 运行机会。

这些 self-test 轮询不是无限等待，受固定次数或绝对 tick deadline 限制。

CP 等待 AP READY 的策略不同：它使用绝对 tick deadline，并用短
`nxsig_usleep()` 让出 CPU，避免高优先级 busy poll 饿死 idle thread/watchdog。
早期排查曾临时改成纯 busy wait 做因果实验，但最终源码没有保留那种实现。

代码评审还保留一个明确边界：CPU2 的三阶段底层启动 handshake 在 NuttX scheduler
锁交接窗口中使用 `WFE` 等待，没有独立 timeout。`up_cpu_start()` 前半段有 SysTick
timeout，但一旦进入 post-bringup/scheduler-unlock 交接，若某颗 core 突然故障，系统
可能停在启动阶段。本次 3 次 warm + 3 次 physical reset 未触发该风险；若要做产品级
fault injection，应另行设计不依赖 scheduler lock 的硬件 deadline/recovery，不能在
当前已验证路径上贸然加入普通 sleep。

## 9. watchdog 的正确所有权

bootloader 为了防止冷初始化永久卡死，会临时启动 AON/APB WDT。但 app 接管后必须先明确
关闭 boot 阶段的两套 WDT，再由 NuttX 按自己的生命周期注册 `/dev/watchdog0`。

最终顺序：

```text
CP reset entry：关闭 boot AON + APB WDT
  -> nx_start()
     -> board bringup
        -> AP autostart
           -> NuttX watchdog register
```

这样既保留 boot 阶段保护，也不会在 AP 尚未启动完成时被遗留 watchdog 复位。

## 10. 为什么 factory image 要 sparse 打包

双镜像的物理 flash 位置固定：

```text
0x000000  bootloader
0x011000  CP image
0x220000  AP image
```

最终打包器把每个 CRC segment 按 4 KiB 补齐：

```text
0x000000..0x011000  bl_crc.bin
0x011000..0x02f000  app_crc_flash.bin
0x220000..0x232000  app1_crc_flash.bin
```

中间空洞填 `0xff`，不能把 AP 紧跟在 CP 后面，否则 CP 启动的是一份镜像，AP controller
却从另一个地址读到垃圾。

## 11. 如何构建和自动验证

在 open-vela 工作区执行：

```bash
cd /home/lijian/project/open-vela

CP_CONFIG_NAME=cp_nsh \
AP_CONFIG_NAME=ap_smp_bidir \
  ./contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/build_dual_image.sh
```

烧录和自动采集使用项目脚本。正式执行前先阅读
[自动调试 SOP](bk7258-build-flash-debug-sop.md)，确认 COM7 是 loader/reset，
COM11 是 460800 8N1 console。

有效 physical reset 必须同时满足：

```text
summary: cold_path=yes
serial:  出现 BClk
result:  PASS_NSH
```

只看到工具报告“Reset success”不算；J-Link reset pin 在本板上没有真正拉低目标，
COM7 RTS 才由 `BClk` 证实有效。

NSH 出现后执行：

```text
nsh> apctl status
```

应看到 AP READY、CPU2 SCHEDULER_ONLINE、online mask `0x3`，以及配置启用的 SMP gate
全部 `PASSED/error=0`。

## 12. 最终验证结果

无 checkpoint 正式镜像连续通过：

| 启动类型 | 结果 | 日志 |
|---|---|---|
| warm 1 | PASS_NSH | `20260731-130256` |
| warm 2 | PASS_NSH | `20260731-130415` |
| warm 3 | PASS_NSH | `20260731-130500` |
| physical reset 1 | PASS_NSH, cold_path=yes | `20260731-130551` |
| physical reset 2 | PASS_NSH, cold_path=yes | `20260731-130627` |
| physical reset 3 | PASS_NSH, cold_path=yes | `20260731-130701` |

日志根目录：

```text
/home/lijian/project/open-vela/logs/bk7258-auto-debug/
```

最终 factory：

```text
size       2301952
SHA-256    f7b62cb0b784612f552a6019728760778b601f6eadfac976e1b260da5c45b95b
```

最终检查还确认：

- bootloader `make clean all verify` 通过；
- CP/AP 全量构建通过；
- shell、Python、PowerShell 脚本语法通过；
- 没有临时 checkpoint；
- `git -C /home/lijian/project/open-vela/nuttx diff --exit-code -- .` 返回 0，
  即 NuttX 官方源码没有任何改动。

## 13. 当前还没有验证什么

没有做真正的断电再上电（power cut）。COM7 RTS 是物理 RESET，已经覆盖 bootloader
cold path，但它不等价于电源轨完全放电。

所以准确说法是：

```text
warm boot verified
physical reset verified
power cut not yet verified
```

此外，本次只复现了官方 bootloader 对当前 raw NuttX 启动必要的硬件契约，不包含官方
RBL/OTA/download 全部功能。若将来要兼容官方升级协议，应单独立项，不能把当前 Tier-1
当成官方 52 KB 镜像的完整替代品。

## 14. 遇到类似问题时的最短排查清单

1. 先确认日志是否有 `BClk`，区分 warm/cold；
2. 确认烧录的 SHA，不要拿旧产物误测；
3. 用 raw UART checkpoint 定位，但最终必须删掉再测；
4. GPIO 初始化必须早于依赖它的 UART pinmux；
5. pinmux ownership 切换前等待 `TX_EMPTY`，不是只等 `WR_READY`；
6. 多核 shared memory 同时检查 cache、MPU 和可见性；
7. 把“CPU 启动”“bringup 完成”“scheduler 可调度”分开握手；
8. timeout 不要只依赖正在被诊断的调度/时钟机制；
9. 查清 bootloader 与 app 的 watchdog ownership；
10. 最后用 exact 正式镜像做至少 3 次重复验证。

更完整的逐轮证据见
[冷复位问题完整复盘](n8-cold-reset-resolution-report.md)；官方 bootloader 的逆向边界见
[官方 bootloader 逆向分析](../bootloader/bk-official-bootloader-reverse.md)。
