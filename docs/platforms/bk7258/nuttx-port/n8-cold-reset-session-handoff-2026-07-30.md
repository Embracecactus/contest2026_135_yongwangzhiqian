# BK7258 N8 cold-reset 项目交接记录

日期：2026-07-30 23:25 CST（Asia/Shanghai）

状态：`handoff-complete / cold-reset-nsh-first-pass-verified / original-blocker-fixed / repeatability-pending`

来源 session：`934a5050-b83b-499f-ab57-910a50f94e83`

上下文位置：

```text
/home/lijian/.claude/projects/-home-lijian-project-open-vela/934a5050-b83b-499f-ab57-910a50f94e83.jsonl
/home/lijian/.claude/projects/-home-lijian-project-open-vela/934a5050-b83b-499f-ab57-910a50f94e83/subagents/
```

项目仓库：

```text
/home/lijian/project/open-vela/contest2026_135_yongwangzhiqian
```

> 本交接轮次只读取 session、文档、CodeGraph 和 Git/产物状态，没有修改业务代码、配置或既有调查文档；唯一新增内容是本交接文档。

## 1. 一句话结论

物理 RESET 冷启动问题已经定位到 CP 的 `bk_uart_init()` 窗口：第一轮 GPIO-before-UART 顺序修复已由板端日志证明生效，但冷启动仍在 GPIO busy 日志后失去输出；第二轮“SDK UART 初始化返回后恢复 UART1 console 寄存器”的候选修复已经写入源码，但当前 factory 产物早于该源码改动，尚未重新构建、下载或板测，因此当前不能宣称问题已修复。

## 2. 当前问题与范围

同一套双核 factory 固件存在明确的 warm/cold 差异：

- 下载器启动或 soft/warm path 能完整进入 NSH；
- physical RESET path 出现 bootloader 的 `BClk A5=... A9=...` 后进入 CP app，但在 NSH banner 之前静默；
- bootloader 已打印 `partition app`、`jump to` 和 `JMP`，所以本轮直接 blocker 不在 bootloader 跳转前；
- raw UART 路标已把 blocker 放在 `arm_serialinit()` 内的 `bk_uart_init()` 窗口，发生在 `board_app_initialize()` 的 `C0` 之前；
- 因此本轮 blocker 与 AP autostart、WDT、第一次 `nxsig_usleep()`、procfs、flash 和 LittleFS 无关。

这不推翻 N8-C1 至 N8-D1 已取得的 warm/download-path 多核板端证据，但说明 physical RESET 和掉电重启矩阵尚未闭环。

## 3. 已取得的板端证据

### 3.1 Phase A 路标版本

warm path 已完整通过：

```text
S0
U0
U1
GPIO busy device 0x22
U2
U3
U4
U5
C0 ... C8
NuttShell (NSH)
```

同固件 physical RESET path：

```text
u_bootloader enter
BClk A5=8407A76C A9=787BC8A4
partition app @ 0x02010000
jump to:0x02010000
JMP
S0
U0
U1
GPIO busy device 0x22
```

没有 `U2`。这把问题定位到 `bk_uart_init()` 的 GPIO 日志与 wrapper 返回路标之间。

### 3.2 第一轮修复：GPIO-before-UART

已在 `bk7258_uart_setup()` 中将初始化顺序改为：

```text
bk_gpio_driver_init()
bk_uart_driver_init()
bk_uart_init()
```

physical RESET 复测结果：

```text
S0
U0
G1
U1
GPIO busy device 0x21
```

仍没有 `U2`。

该结果证明：

1. `bk_gpio_driver_init()` 已返回；
2. GPIO peripheral-mode table 已从未正确初始化时的 `0x22` 修正为 `GPIO_DEV_UART1_TXD = 0x21`；
3. GPIO-before-UART 是真实缺陷，但不是完整根因；
4. 剩余二分是 `bk_uart_init()` 未返回，或它返回前后的 UART clock/divider/config 重写令后续 460800 输出不可见。

### 3.3 第二轮修复：UART console restore

当前源码已在 `bk_uart_init()` 返回后、`U2` 之前调用：

```c
result = bk_uart_init(priv->id, &config);
bk7258_uart_restore_console();
```

恢复的 UART1 板级 invariant 为：

- UART1 clock gate enable；
- UART1 source = 26 MHz XTAL；
- global control = `1`；
- config = `0x0000371b`，即 divider 55、460800、8N1、TX/RX enable；
- 写寄存器后执行 `dsb sy; isb sy`。

该候选修复尚无构建或板端结果。

## 4. 本 session 涉及的文件

### 4.1 业务代码

```text
board/bk7258/chip/common/bk7258_serial.c
board/bk7258/chip/cp/bk7258_ap_control.c
board/bk7258/src/bk7258_bringup.c
```

具体状态：

- `bk7258_serial.c`
  - 保留 `S0/U0..U5/G1/EG/E1..E4/SE` 临时 raw UART 路标；
  - 增加 `<driver/gpio.h>`；
  - UART 初始化前执行幂等的 `bk_gpio_driver_init()`；
  - 增加 `bk7258_uart_restore_console()`；
  - 第二轮 restore 已落盘，未重新构建。
- `bk7258_ap_control.c`
  - 保留 `A0..A7/W0/W1/F1/F2` 临时路标；
  - 功能路径未因本次问题改变。
- `bk7258_bringup.c`
  - 增加 `<nuttx/arch.h>` 以取得 `up_putc()` 声明；
  - 保留 `C0..C8` 临时路标；
  - WDT、AP、GPIO lower-half、procfs、flash/LittleFS 原行为未改变。

### 4.2 调查文档

```text
docs/platforms/bk7258/nuttx-port/n8-cold-reset-nsh-hang-investigation.md
docs/platforms/bk7258/nuttx-port/n8-cold-reset-diagnostic-checkpoints.md
```

主调查文档已经记录第一轮复测结果和第二轮 restore 候选修复。

## 5. 当前 Git 状态

交接检查时：

```text
branch:   feat/bk7258-ap-smp
HEAD:     90581d970e328869b09e9e7956d144d9cefe717f
subject:  feat(bk7258): bring AP SMP scheduler online
upstream: origin/dev-ai-contest-2026
relation: ahead 1, behind 0
```

在新增本交接文档前，工作树已有：

- 13 个 tracked modified 文件；
- 21 个 untracked 文件；
- 本交接文档会再增加 1 个 untracked 文件；
- 没有 staged 文件；
- 本 session 没有创建 commit、没有 push。

工作树包含从 N8-C2 到 N8-D1 的大量未提交开发内容，不仅是 cold-reset 修复。不要用 `git checkout`、`git restore`、`git reset` 或清理 untracked 文件来“恢复基线”，否则会破坏已经取得板端证据但尚未提交的阶段成果。

主要未提交范围包括：

- AP SMP advanced 实现及 `apctl` 输出；
- N8-C2 至 N8-D1 的独立 defconfig 和 worklog；
- cold-reset 三个源码文件的临时路标；
- UART GPIO-before-init 与 console restore 候选修复；
- README 和 next-stage handoff 更新。

## 6. 当前构建产物状态

主机上存在以下产物：

| 产物 | 修改时间 | 字节数 | SHA-256 |
|---|---|---:|---|
| `nuttx/nuttx.bin` | 2026-07-30 23:00:49 +0800 | 179280 | `7e238634ee0d3da613c133ce8bc45a776c74b59c059265b71521935c98739347` |
| `nuttx/all-app.bin` | 2026-07-30 23:00:49 +0800 | 260134 | `5868c8b6b09c195adc81de8af4826b02228d233e95c11f303aee541305f8b6a3` |
| `nuttx/bk7258-dual/all-app-factory.bin` | 2026-07-30 23:00:51 +0800 | 2298910 | `14e6f2cd718fc538f117d528e081804edf8367a49e6eab36b6b970f10b7cedbb` |
| `nuttx/bk7258-dual/bl_crc.bin` | 2026-07-30 22:59:33 +0800 | 69632 | `507400e1f86769d064ad7206814129dd60b8dcc6e483e7ed12440c89fd747f56` |

关键时间关系：

```text
all-app-factory.bin: 2026-07-30 23:00:51 +0800
bk7258_serial.c:     2026-07-30 23:08:39 +0800
```

因此现有 `all-app-factory.bin` 明确早于第二轮 `bk7258_uart_restore_console()` 源码改动，不能用来验证第二轮修复。下一轮必须重新构建。

23:30 收到的 physical RESET 日志仍停在 `G1/U1/GPIO`，但由于板端使用的仍是上述 23:00 旧产物，该结果只算第一轮 failure 的重复复现，不算第二轮 restore 的失败。

23:35 已按 `AP_CONFIG_NAME=ap_smp_bidir` 重新构建成功。新 factory：

```text
nuttx/bk7258-dual/all-app-factory.bin
size:   2298910
sha256: d83c8e38bec19160f9d54d0832a4f553dab85bd568173f2a1ebe4fc9e860d405
CP:     cp_nsh, app_crc sha256 5e7b864eebeeda3994712bc1eb8cdaa87183cdb496d82952222ae7b8c724e014
AP:     ap_smp_bidir, app1_crc sha256 c563308f74b46e2aacef9d4bca03b5ba4cb9a51ae3c3f338927ad4f7b2087f4b
```

CP ELF 反汇编确认 restore 已内联到 `bk7258_uart_setup()`，factory 偏移 `0x11000` 的 CP 段与新 `app_crc.bin` 逐字节一致。当前可以进入真正的第二轮板测，不需要 SDK 内部插桩。

烧录对象必须是：

```text
nuttx/bk7258-dual/all-app-factory.bin
```

不要误用只包含 bootloader + CP 的 root `nuttx/all-app.bin` 代替双核 factory image。

## 7. 下一最小动作

用户负责构建、下载和板端验证。接手者此时不要继续修改代码，先取得第二轮结果。

### 7.1 重新构建和下载

保持当前 Kconfig、bootloader、CP/AP image 组合不变，重新生成并烧录：

```text
nuttx/bk7258-dual/all-app-factory.bin
```

若构建失败，保留第一处完整编译错误，不要先做大范围重构。

### 7.2 采集 warm path

从 `u_bootloader enter` 开始保留完整原始串口。预期至少看到：

```text
S0
U0
G1
U1
GPIO busy device 0x21
U2
U3
U4
U5
...
C8
NuttShell (NSH)
```

### 7.3 采集 physical RESET cold path

不重新下载，按一次 physical RESET；等待至少 15 秒，从 `u_bootloader enter` 开始保留完整日志，并确认是否出现：

```text
BClk A5=... A9=...
```

## 8. 第二轮结果判读

### 结果 A：出现 `U2`

若 GPIO 日志后出现：

```text
U2
U3
U4
U5
```

说明 `bk_uart_init()` 已返回，之前的静默主要是 SDK UART 重配导致 console 输出不可见。继续观察是否到达 `C8/NSH`。

若完整进入 NSH，先执行：

```text
echo cold-ok
apctl status
cat /data/probe.txt
```

并确认 `/dev/gpio0`、`/dev/gpio1` 仍存在。随后执行最终矩阵：

| 启动方式 | 次数 | 门禁 |
|---|---:|---|
| 下载后首次启动 | 1 | NSH、AP READY、CPU2 scheduler-online |
| loader soft reboot | 3 | NSH、generation、SysTick、sleep-return |
| physical RESET | 3 | 每次进入 NSH，无静默、无 reset loop |
| 断电重上电 | 3 | 与 physical RESET 相同 |
| `apctl status` 连续采样 | 3 | heartbeat/SysTick/sleep-return 持续增长 |
| LittleFS | 1+ | `/data/probe.txt` 语义保持 |

全部通过后再删除临时 `cold_ckpt()` 路标；保留真实功能修复，即 GPIO-before-UART 顺序和经证实必要的 console restore。

### 结果 B：仍停在 GPIO 日志后，无 `U2`

这说明 restore 函数没有机会执行，`bk_uart_init()` 本身没有返回。下一轮应只在 SDK 的 GPIO 后续边界增加受控路标，优先覆盖：

1. GPIO unmap/map 返回；
2. kfifo/`os_malloc()` 分配；
3. `rtos_init_semaphore()`；
4. UART clock/system adapter；
5. `uart_hal_init_uart()` 前后；
6. IRQ 注册/enable 前后。

这些新路标不能依赖可能已经被重配失效的 UART1；应选择不会被该函数重配影响的观测方式，或在每个边界先恢复 UART invariant 再输出。不要转向 AP、WDT、SysTick 或文件系统分支。

## 9. 风险、禁止项与清理规则

当前禁止：

- 不把 GPIO busy 日志本身当作错误直接 unmap 或屏蔽；
- 不把 `[ipc_svr] create_socket failed.` 当作启动 blocker；
- 不回退 N8-C1 的 AP STAR IRQ/context-restore wrapper；
- 不修改已板测的 mailbox/SMP ABI；
- 不通过延长 AP timeout 掩盖 CP console 初始化问题；
- 不因为下载后启动成功就宣称 physical cold reset 已通过；
- 不清理当前大量 uncommitted/untracked N8 成果；
- 未得到板端结果前，不提交第二轮候选修复为最终修复。

定位完成后应删除：

- 三个 translation unit 中的 `cold_ckpt()` helper；
- `S/U/G/E/C/A/W/F` 全部临时路标；
- `bk7258_ap_wait()` 中仅用于第一次 sleep 的 `first_iter` 诊断逻辑。

必须保留：

- WDT-before-AP 顺序；
- CPU1/CPU2 已验证的 reset/power/lifecycle 逻辑；
- mailbox/shared-state 与 SMP data path；
- GPIO-before-UART cold-safe 初始化顺序；
- 经板端证实必要的 UART console restore；
- flash/LittleFS 已验证语义。

## 10. 已发现的文档一致性问题

交接时发现两处不影响当前代码、但后续收口必须修正的文档偏差：

1. `n8-cold-reset-diagnostic-checkpoints.md` 的 `bk7258_uart_setup()` 示例仍是直接判断 `bk_uart_init()` 返回值，没有同步第二轮的 `result = bk_uart_init(); bk7258_uart_restore_console();`；实际源码与主调查文档才是当前状态。
2. `n8-cold-reset-nsh-hang-investigation.md` 开头仍保留“当前不保留临时诊断代码”的旧描述，但实际代码和文档后文均表明路标已恢复并仍在工作树中。

在第二轮板测结果出来前，不建议单独重写这些文档；结果出来后应一次性按真实结论更新状态、示例、清理规则和 CURRENT handoff。

## 11. 推荐阅读顺序

接手时按以下顺序恢复上下文：

1. 本交接文档；
2. `n8-cold-reset-nsh-hang-investigation.md`；
3. `n8-cold-reset-diagnostic-checkpoints.md`；
4. `n8-d1-smp-lifecycle-quiesce.md`；
5. `docs/platforms/bk7258/README.md` 的当前状态；
6. `docs/platforms/bk7258/next-stage-prompt.md` 的 CURRENT handoff；
7. CodeGraph 中的 `bk7258_uart_setup`、`bk7258_uart_restore_console`、`arm_serialinit` 调用关系。

## 12. 当前交接门禁

```text
已完成：session/context/docs/CodeGraph/Git/产物状态恢复
已完成：Phase A 定位
已完成：第一轮 GPIO-before-UART 板测，证明有效但不足
已落盘：第二轮 UART console restore 候选修复
已确认：23:30 physical RESET 使用 23:00 旧产物，不计为第二轮复测
已完成：第二轮 `CP=cp_nsh / AP=ap_smp_bidir` factory 构建及 restore 入包验证
已完成：WSL2 调用 Windows loader 的 COM7/COM11 自动化脚本框架
已完成：第二轮 factory 自动下载，bk_loader 明确 Writing Flash OK
已完成：warm path U2..U5，证明 UART restore 板端生效
已完成：warm path 后续到 A7/F1/F2/C4..C8/NSH
已完成：用户手动 physical RESET 出现 BClk/S0/U2..U5，原 cold UART blocker 修复成立
已完成：手动 cold path 数秒后到 A7/F1/F2/C4..C8/NSH，原永久静默问题首次闭环
独立后续：AP READY timeout；CP fail-open 正常，NSH 可用
待完成：physical RESET 和 power-cycle 重复性矩阵
限制：首次 J-Link ClrRESET 未拉低 RESET pin，不等价于手动 reset
未完成：第二轮 warm + physical RESET 板测
未完成：最终 cold/soft/power-cycle 验收矩阵
未完成：删除临时路标
未完成：最终文档一致性收口
未完成：commit/push
```
