# NuttX Stage N3 — procfs + ps（worklog）

> 板端验证日期：2026-07-18
> 基线 commit：`9f45bc6`（Stage N2）→ 本阶段：`4d9198e`（code，procfs+ps）+ 本 docs 提交
> 改动范围：`$CONTEST/board/bk7258_t5ai/`（configs/nsh/defconfig + src/bk7258_bringup.c）

## 目标

Stage N3 —— 在 BK7258 T5-AI 上**挂载 procfs 到 `/proc`**，使 `ps` / `ls /proc` /
`cat /proc/*` 可用。这是 N2（交互式 NSH console）之后**最低风险的可观测性里程碑**：不需新驱动、
不动 UART/中断/SysTick，仅在 NSH 应用层 init 钩子里挂一个内存文件系统。板端验证。

## 参考：BK7258 芯片 vs T5-AI 模组规格

| 项 | BK7258 芯片能力 | T5-AI 模组实例 | 本移植使用情况 |
|---|---|---|---|
| 核心 | ARMv8-M Star（M33F），高达 480 MHz；双精度 FPU / TrustZone / DSP-SIMD，3.84 CoreMark/MHz | 同 | overlay / Tier-1 未发现 DPLL 配置；CPU0 仍在约 26 MHz XTALH 是工作假设，须由 N4-D0 用寄存器 readback + 独立测量确认 |
| ITCM / DTCM | 16 KB / 16 KB | 同 | 未用 |
| Flash | 高达 16 MB | **8 MB SiP** | app @ logical 0x02010000，当前镜像 ~160 KB |
| PSRAM | 高达 16 MB | **16 MB SiP** | **未用**（heap 仍来自 SRAM） |
| 共享 SRAM | 640 KB | 同 | heap 来源，/proc/meminfo total ≈ 631 KB |
| ROM / eFuse | 64 KB ROM + eFuse | 同 | — |

**时钟树**：26 MHz XTALH（`board.h` 的 `BOARD_CPU_FREQ_HZ=26000000` 即此参考晶振）→ DPLL（320/480 MHz）→ 核心。当前 overlay / Tier-1 未发现已知 DPLL init，因此“CPU0 仍在约 26 MHz”只是工作假设；`/proc/cpuinfo` 的 `cpu MHz : 0.000` / `BogoMIPS : 5.00` 不是频率证明。N4-D0 将用寄存器 readback + 独立测量确认实际频率，再推进 480 MHz bring-up。

**外设 / 无线**（未来"驱动补全"范围，见 porting-report §12）：Wi-Fi 6（802.11ax）+ BLE 5.4 组合；56 GPIO、2×SPI、2×QSPI、3×UART（其一支持硬件流控 + flash 下载）、USB2.0 HS、CAN FD、LIN、SDIO、以太网 MAC、显示控制器（RGB/8080）、段式 LCD、H.264 720p 编码、JPEG 编/解码、3×I2S、12×PWM、12-bit AUX ADC 等；3 核（CPU0 boot master + CPU1/2 AP）尚未唤醒。

> 8 MB SiP flash + 16 MB SiP PSRAM 为未来尚未编号的 MTD/FS（掉电留存）与大 heap 工作提供充足余量；这些工作推迟到当前 DPLL / 480 MHz Stage 完成后，具体 MAIN Stage 编号待前序证据明确后再分配。

## 背景与关键发现

N2 已经能在 UART1 上跑交互式 NSH，但 `ps` 等命令不可用——根因是两个相互独立的配置缺口：

1. **N2 的 NSH 纯靠 Kconfig 直起**：`CONFIG_INIT_ENTRYPOINT="nsh_main"`，而
   `board_app_initialize()` 是死代码——因为 N2 没开 `CONFIG_NSH_ARCHINIT`，NSH 不调
   `boardctl(BOARDIOC_INIT)`，也就不会进我们的 bring-up 钩子。N2 的 boot trace `DBESITtC` 没有
   `'A'` 字符就是这个事实的直接证据（`'A'` 是 `board_app_initialize()` 入口的单字符 trace）。
2. **`cmd_ps` 需要 procfs 显式挂载**：NuttX 的 `cmd_ps` 经 `nsh_foreach_direntry(
   CONFIG_NSH_PROC_MOUNTPOINT, ...)` 枚举 `/proc` 下的 TCB 条目；`nsh_initialize()` **不自动**
   挂 procfs，必须有人显式 `mount(NULL, "/proc", "procfs", 0, NULL)`。

这两点决定了 N3 的最小改动：开 `CONFIG_NSH_ARCHINIT` 激活 bring-up 钩子，开 `CONFIG_FS_PROCFS`
提供 procfs 文件系统，然后在钩子里调一次 `mount()`。

## 改动（全部在团队 overlay）

### 3.1 `configs/nsh/defconfig`

新增两行（CONFIG_NSH_PROC_MOUNTPOINT 由 olddefconfig 从默认值解析为 "/proc"，无需在 defconfig 里写）：

| 符号 | 值 | 作用 |
|---|---|---|
| `CONFIG_FS_PROCFS` | `y` | 编入 procfs 文件系统驱动 |
| `CONFIG_NSH_ARCHINIT` | `y` | NSH 调 `boardctl(BOARDIOC_INIT)`，激活 `board_app_initialize()` 钩子；auto-select `CONFIG_BOARDCTL` |
| `CONFIG_NSH_PROC_MOUNTPOINT` | `"/proc"` | 由 `olddefconfig` 从默认值解析，作为 `mount()` 目标路径 |

`CONFIG_NSH_DISABLE_PS` / `CONFIG_NSH_DISABLE_CAT` / `CONFIG_NSH_DISABLE_LS` 保持**未定义**
（即这些命令都启用）。`CONFIG_INIT_ENTRYPOINT="nsh_main"` 不变（NSH 直起，与 N2 一致）。

### 3.2 `src/bk7258_bringup.c`

- 头部加 `#include <sys/mount.h>`。
- `board_app_initialize()` 的 `'A'` 标记**之后**调
  `mount(NULL, CONFIG_NSH_PROC_MOUNTPOINT, "procfs", 0, NULL)`：
  - 成功发 `'P'`，失败发 `'p'`（沿用 N2 的单字符 UART1 polled trace 约定，与 `'A'` 同一
    `bk7258_bringup_diag_putc` 路径）。
- 文件头注释与 `board_app_initialize()` 的 doc 同步更新为 N3 描述（procfs 挂载用途，后续阶段
  MTD/SMP 在此扩展）。

核心几行：

```c
bk7258_bringup_diag_putc('A');

if (mount(NULL, CONFIG_NSH_PROC_MOUNTPOINT, "procfs", 0, NULL) < 0)
  {
    bk7258_bringup_diag_putc('p');   /* procfs mount failed */
  }
else
  {
    bk7258_bringup_diag_putc('P');   /* procfs mounted at /proc */
  }

return 0;
```

## 构建

从 workspace 根 `/home/lijian/project/open-vela` 执行：

```bash
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8
```

产物（`out/vendor/openvela/boards/contest2026_135_bk7258/configs/nsh/`）：

- `nuttx.bin` = **88388 B**
- `all-app.bin` = **163574 B = 0x27EF6**（= `bl_crc.bin` 69632 B + `nuttx_crc.bin` 93942 B）

`.config`（`olddefconfig` 解析后）三符号确认：

```
CONFIG_FS_PROCFS=y
CONFIG_NSH_ARCHINIT=y
CONFIG_NSH_PROC_MOUNTPOINT="/proc"
```

构建**零告警**。

## 烧录

```bash
bk_loader.exe download \
  -p 7 -b 6000000 --uart-type OTHER \
  --mainBin-multi <all-app.bin UNC>@0x0-0x27EF6 \
  --reboot 1 --fast-link 1
```

要点：

- `--mainBin-multi` 的第二个数是**文件长度**，不是结束地址。start=0 时两者数值相同，所以
  `0x27EF6` 即 `all-app.bin` 的字节长度，正确。
- 按住 **BOOT** 上电，板经 **DL_UART0** 进 BootROM 下载模式；下载口跑 **6 Mbaud**。
- **UART1 控制台**保持 **460800 8N1**（RX 中断驱动 / TX 轮询，沿用 N2 配置）。

## 板端验证（两轮 —— 证明 verified == committed）

为消除"验证的是不是提交源码构建物"的疑虑，跑两轮：第一轮验证功能，第二轮用提交源码重编镜像复验。

### Round 1 — state-A（初版镜像，构建时间戳 `14:35:19`）

复位后 UART1 照抄：

```
u_bootloader enter
partition app @ 0x02010000
jump to:0x02010000
JMP
N2
DBESITtCAP                          ← 注意结尾 AP：A = board_app_initializer 入口；P = procfs mount 成功
NuttShell (NSH)
nsh>
```

对比 N2 的 `DBESITtC`：尾部的 `'A'` 与 `'P'` 出现，证明 NSH init 任务走到了
`board_app_initialize()` 且 `mount()` 返回 0。

NSH 命令照抄：

```
nsh> ps
PID 0   CPU0  IDLE      Kthread  Ready    stack 0002024
PID 1   CPU0  nsh_main  Task     Running  stack 0002000

nsh> ls
/:
 dev/
 proc/

nsh> ls proc
/proc:
 0/
 1/
 cpuinfo
 fs/
 memdump
 meminfo
 self/
 tcbinfo
 uptime
 version

nsh> cat /proc/version
NuttX version 0.0.0 e02f581e23 Jul 18 2026 14:35:19 ../contest2026_135_yongwangzhiqian/board/bk7258_t5ai/configs/nsh

nsh> cat /proc/cpuinfo
processor :0
BogoMIPS  :5.00
cpu MHz   :0.000
Features  :half fastmult lpae idivt thumb
model name :ARMv8-M rev 0 (v8ml)
CPU architecture :8M
implementer :0x63
variant :0x1
part :0x132
revision :0

nsh> cat /proc/meminfo
       total       used       free    maxused    maxfree      nused      nfree      Umem
     646144       7728     638416       8088     638416         24          1

nsh> uname -a
NuttX 0.0.0 e02f581e23 Jul 18 2026 14:35:19 arm bk7258_t5ai
```

读数对得上：`ps` 列出 PID 0（IDLE Kthread）+ PID 1（nsh_main Task，Running，CPU0），正是 N2 baseline
的调度器状态；`ls /proc` 看到完整 procfs 条目集；`cat /proc/{version,cpuinfo,meminfo}` 返回真实
内核/硅片数据；`uname -a` 回归。

### Round 2 — state-C（提交源码重编镜像，时间戳 `15:11:55`）

为证明板上跑的就是即将提交的源码，重编重刷再验：

```
nsh> cat /proc/version
NuttX version 0.0.0 e02f581e23 Jul 18 2026 15:11:55 ../contest2026_135_yongwangzhiqian/board/bk7258_t5ai/configs/nsh
                                      ^^^^^^^^^ 时间戳变化 = 板上跑的是 state-C 镜像

nsh> cat /proc/meminfo
       total       used       free    maxused    maxfree      nused      nfree      Umem
     646144       7728     638416       8088     638416         24          1
```

复位后 boot trace 与 state-A 一致：

```
... N2 / DBESITtCAP / NuttShell (NSH) / nsh>
```

### 验收结论

trace `A` + `P`、`ps`、`ls /proc`、`cat /proc/{version,cpuinfo,meminfo}`、`uname -a` 全部
**板上通过**。procfs 挂载点 `/proc` 与 `CONFIG_NSH_PROC_MOUNTPOINT` 一致；`ps` 经
`nsh_foreach_direntry` 枚举到 PID 0/1 两个 TCB 条目，证明 procfs 已被 NSH 命令层正确消费。

## 二进制等价性说明（为何要两轮验证）

NuttX 把 `__DATE__` / `__TIME__` 烤进版本串（`/proc/version` 与 `uname -a` 都带构建时间戳）→
**每次构建产出的 nuttx.bin/all-app.bin 哈希都不同**（构建时间戳非可复现）。因此 state-A
（`14:35:19`）与 state-C（`15:11:55`）的镜像哈希必然不同，但差异**仅为构建时间戳字符串**——
两轮间的源码改动只是注释/文档润色，对代码生成零影响。

为满足"已验证 == 板上观察的镜像 == 即将提交的源码构建物"这一竞赛证据链要求，采用 state-C
（= 提交源码）重刷再验。板上观察到 `15:11:55` 时间戳即证明 state-C 在板上运行，验收以此为准。

## 已知小项 / 未来打磨

- `/proc/cpuinfo` 的 `cpu MHz : 0.000`（`BogoMIPS : 5.00`）不是实际频率证明。官方最高规格为 **480 MHz**（26 MHz XTALH → DPLL），当前 `board.h` 有 `BOARD_CPU_FREQ_HZ=26000000`，且 overlay / Tier-1 未发现已知 DPLL init，因此“CPU0 仍在约 26 MHz”只是工作假设。N4-D0 将用时钟寄存器 readback + 独立测量确认实际 baseline，再决定后续 480 MHz 配置；不能把 cpuinfo 数值当作因果证据。
- 回退基线：`zephyr-bk7258-port/out/custom_bootloader/bk7236_min_bl_crc.bin` @ `0x0-0x11000`
  仍是已知良好回退镜像（N1/N2 验证沿用）。

## 下一步：Stage N4 — DPLL / 480 MHz 时钟 bring-up

BK7258 官方时钟树为 26 MHz XTALH → DPLL（320 / 480 MHz）→ CPU core；当前 Tier-1 bootloader
未配置 DPLL，overlay 仍以 `BOARD_CPU_FREQ_HZ=26000000` 为 baseline。N3 已补齐运行态可观测性，
因此下一 MAIN Stage 固定为 N4：先证明当前频率，再分段完成 DPLL lock、CPU0 480 MHz 切换和
UART1 / SysTick / NSH / procfs 回归。

- 主 Stage 顺序与 current pointer：[`../next-stage-prompt.md`](../next-stage-prompt.md)
- 当前 N4 完整恢复提示词：[`prompts/04-n4-clock-bringup.md`](prompts/04-n4-clock-bringup.md)

N4-R → N4-D0 → N4-D1 → N4-D2（optional）→ N4-D3 → N4-V 是**同一个 N4 文件内**的有序
subsection，不拆成多个 Stage 或提示词文件。

MTD / 文件系统、PSRAM、Tier-2 bootloader OTA、SMP 均推迟到 N4 完成之后；只有前一 Stage 的
真实证据已知时，才为下一 MAIN Stage 分配编号并生成对应 prompt，现在不预分配后续 Stage 编号。
