# NuttX Stage N2 — 交互式 NSH console（worklog）

> 板端验证日期：2026-07-18
> 基线 commit：`40495ca`（Stage N1）→ 本阶段产出 commit：`9f45bc6`
> 改动范围：`$CONTEST/board/bk7258/`（chip/ 8 改 + 5 新、board/ 配置、include/irq.h、scripts）

## 目标

在 Stage N1（bootloader 跳进 NuttX、早期 UART 打印可见）的基础上，让 NuttX 完整启动到**交互式
NSH**：`__start` 起调度器（SysTick + PendSV + SVCall），NSH init 任务在 UART1 上跑 readline，
键盘输入 + 回显 + Enter + 命令解析全部可用。UART1 RX 中断驱动、TX 轮询。

## 板端 boot trace（UART1 @ 460800 8N1，照抄）

```
u_bootloader enter               ← 自制 Tier-1 bootloader（start.S + boot_main.c）
partition app @ 0x02010000
jump to:0x02010000
JMP
N2 DBESITtC                      ← NuttX __start 早期 banner（FPCCR 清完、.data/.bss 起、early console）
NuttShell (NSH)                  ← nx_start() 起调度器 + NSH init 任务
nsh> help                        ← 列出全部内建命令
nsh> uname -a
NuttX 0.0.0 ... arm bk7258_t5ai
nsh> echo hello
hello
```

键盘输入、字符回显、Enter 提交、命令解析全部 live。

## 内核侧 bring-up 要点（`chip/cp/bk7258_start.c`）

`__start` 不再停在 N1，而是走完整 bring-up：

1. 设 VTOR 指向 flash 向量表。
2. **清 FPU FPCCR bit29/30/31**（ASPEN/LSPEN 默认 lazy-stacking）：否则首次异常入口 lazy-stacking
   路径 hang（板端实测 SysTick 首次 tick 即死）。这是 N2 解锁调度器的关键。
3. `.data` 加载 / `.bss` 清零。
4. `arm_earlyserialinit()`（接 console lower half）。
5. `nx_start()` → 调度器（SysTick 10 Hz + PendSV + SVCall）→ NSH init 任务。

向量表（`bk7258_vectors.c`）：`slot[15..63] = exception_direct`（真实分派器）。调试期临时塞过的
SysTick 探针，在异常入口被证明 OK 后还原成 `exception_direct`。NR_IRQS=48 覆盖 UART1 @ slot 31。

## UART1 RX 输入：4 个叠加 bug（全在 `chip/common/bk7258_serial.c`）

NSH 提示符能打印（TX 路径 OK），但敲键没反应。逐层排查发现 4 个独立 bug 叠加，必须全修才能通。

### Bug 1：`receive()` 取位错

- **现象**：ISR 触发后读到的字节恒为 TX FIFO 里上次写的值（或 0）。
- **定位**：UART1 `fifo_port`（`0x4583001C`）是 TX/RX 共用 32 位寄存器，**bits[0:7] = TX 字段**，
  **bits[8:15] = RX 字段**。原代码 `fifo_port & 0xff` 取的是 TX 那一段。
- **修法**：`receive()` 改为 `(fifo_port >> 8) & 0xff`。

### Bug 2：`CFG.rx_enable` 未开

- **现象**：即使中断门开了，RX 有效字节到不了 RX FIFO 逻辑。
- **定位**：CFG 寄存器 `0x45830010` bit1 = `rx_enable`。Tier-1 bootloader（`start.S`）只做 TX
  bring-up（print banner），所以 bit1 一直是 0，NuttX 接管后没人开。
- **修法**：`setup()` 里 `CFG |= bit1`，同时**不动** `clk_div=0x37`（对应 460800）和 `tx_enable`
  位（bootloader 已设好）—— OR 一个 bit，不重写整个字。

### Bug 3：三道中断门一道没开

- **现象**：`setup()` + Bug 1/2 修完后，按键仍无 ISR 触发。
- **定位**：UART1 RX 从外设到 NVIC 要过三道门，原代码一道没开：
  1. UART `int_enable`（`0x45830020` bit1）—— UART 外设级 RX 中断使能；
  2. 片上中断控制器 `SYS_CPU0_INT_0_31_EN`（`0x44010080` bit15）—— BK7258 自定义 ICU，UART1 接在
     CPU0 的线 15；
  3. NVIC `up_enable_irq(31)` —— Cortex-M 内核级。
  且原 `rxint()` 是空函数、`attach()` 直接 return OK 不做 `irq_attach`。
- **修法**：`rxint(true)` 按上面 1→2→3 顺序开三道门；`attach()` 做
  `irq_attach(31, bk7258_uart_isr, dev)`。

### Bug 4：RX FIFO 阈值默认 0 → ISR storm

- **现象**：三道门一开，板立刻挂死（ISR 风暴）。
- **定位**：`fifo_config`（`0x45830014`）bits[8:15] = RX FIFO 触发阈值，默认 0。状态位
  `rx_fifo_need_read` 的判定是 "FIFO ≥ 阈值"，阈值=0 即 "FIFO ≥ 0" 永真 → 中断一开就持续触发。
- **修法**：`setup()` 把阈值设为 1。

## 中断号映射（关键，易踩坑）

UART1 = NuttX IRQ **31**，三处证据一致：

| 证据点 | 值 |
|---|---|
| startup 向量表 slot 15 | `UART1_Handler`（slot 15 = 16 + 15 → IRQ 31） |
| BK7258 ICU `icu_map INT_SRC_UART1` | → CPU0 线 15 |
| `SYS_CPU0_INT_0_31_EN`（`0x44010080`） | bit15 |

向量 slot 31 = `exception_direct`（真实分派器，非 common 入口）。

> **踩坑提醒**：`ICU_PRI_IRQ_UART1 = 26` 是**优先级寄存器索引**（ICU 自己的优先级表偏移），
> **不是** NVIC 线号、也不是 NuttX IRQ 号。把它当 IRQ 号 attach 会落到错误 slot。NuttX IRQ 号
> = 16（`BK7258_IRQ_FIRST`）+ 15（ICU 线）= 31，定义在 `chip/include/irq.h`。

## 板端验证证据

```
nsh> uname -a
NuttX 0.0.0 ... arm bk7258_t5ai
```

`arm bk7258_t5ai` 即我们的板名（`board.h` `BOARD_NAME` / uname machine 字段）。`help` 列全部
内建命令、`echo hello` 回 `hello`、键盘输入 + 回显 + Enter + 命令解析全部 live。console 稳定，
未观测到 ISR storm 或 hang。

## 改动清单（commit `9f45bc6`）

代码 14 文件，全在 `$CONTEST/board/bk7258/`：

- `chip/cp/bk7258_start.c`（`__start` 全 bring-up + FPCCR 清位）
- `chip/cp/bk7258_vectors.c`（slot[15..63] = exception_direct）
- `chip/common/bk7258_serial.c`（新，console lower half + 4 RX bug 全修）
- `chip/common/bk7258_lowputc.c`（新，polled `arm_lowputc`/`up_putc`）
- `chip/common/bk7258_irq.c`（新，NVIC glue；删了局部 `#define BK7258_IRQ_FIRST`，改从 `irq.h` 取）
- `chip/common/bk7258_timerisr.c`（新，SysTick 10 Hz）
- `chip/common/bk7258_allocateheap.c`（新，`up_allocate_heap`）
- `chip/include/irq.h`（+ `BK7258_IRQ_UART1 = 31`）
- `chip/CMakeLists.txt` / `chip/Make.defs`（编入新源文件）
- `configs/cp_nsh/defconfig`（NSH readline、`INIT_ENTRYPOINT=nsh_main`、ARMV8M_SYSTICK、FLASH 1 MiB）
- `scripts/ld.script` / `include/board.h` / `src/bk7258_bringup.c`

构建：`./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/cp_nsh -j8`
烧录：`$FW/all-app.bin`（= `bl_crc.bin` + `nuttx_crc.bin`）@ physical `0x0`，console UART1 460800 8N1。

## 未决项（Stage N3 候选）

- `ps` / `procfs` 未开（NSH 里跑 `ps` 报命令不存在或无 procfs 挂载）。
- MTD / 文件系统未接（无 `ls` / `cat` 目标）。
- Tier-2 bootloader OTA（RBL 头 + A-B 分区 + failover）未做。
- 多核 SMP（CPU1/CPU2 唤醒）未做。
- 驱动补全（GPIO / 时钟 / flash / Wi-Fi / BLE）按需再开。
