# 32｜五个固定接口：IRQ、tick、early UART、serial 和 heap

本篇解释 NuttX 如何通过固定函数名让 architecture port 接入内核。这些名字不是随意起的，也不是由 BK7258 自由命名的；它们由 NuttX 的 `nuttx/include/nuttx/arch.h` 和 ARM common 层共同规定，被内核启动流程按固定顺序调用。

> **来源记录**
>
> - 教学主题：NuttX 启动链中的五个标准架构接口及其在 BK7258 中的实现
> - `$WORKSPACE/nuttx` commit：`e02f581e235fc7b527d57ff62b668ce625d139ab`
> - `$CONTEST` source commit：`c588afbd8e0f1d30723f5076e585673a6ace8a4e`
> - 有效配置来源：当前 `$WORKSPACE/nuttx/.config`
> - 最后核对日期：2026-07-24
> - 重要纠正：BK7258 部分源码注释写 "Called from up_initialize()"，但当前 NuttX 代码中 `up_irqinitialize()` 实际由 `irq_initialize()` 调用、`up_timer_initialize()` 由 `clock_initialize()` 调用，二者都在 `up_initialize()` 之前；只有 `arm_serialinit()` 确实由 `up_initialize()` 调用
> - 未覆盖：MPU 完整配置、Tickless 模式、完整 UART DMA、其他核的启动路径

## 1. 五个接口的总览

本课涉及的五个标准接口按实际调用顺序是：

| 顺序 | 函数 | 谁调用它 | 源码位置 |
|---|---|---|---|
| 1 | `up_allocate_heap()` | `memory_initialize()` | `$BOARD/chip/common/bk7258_allocateheap.c:65` |
| 2 | `up_irqinitialize()` | `irq_initialize()` | `$BOARD/chip/common/bk7258_irq.c:165` |
| 3 | `up_timer_initialize()` | `clock_initialize()` | `$BOARD/chip/common/bk7258_timerisr.c:79` |
| 4 | `arm_earlyserialinit()` | `__start()` | `$BOARD/chip/common/bk7258_serial.c:477` |
| 5 | `arm_serialinit()` | `up_initialize()` | `$BOARD/chip/common/bk7258_serial.c:494` |

注意 `arm_earlyserialinit()` 实际出现在 `__start()` 中，比其余四个都早；上表按"名字第一次出现"排序，不是严格按调用时间。精确调用链见下文。

## 2. 真实调用顺序

```text
__start()
  ├─ VTOR / FPU / .data / .bss
  ├─ arm_earlyserialinit()          ← 接口 4
  ├─ [可选] bk7258_clock_bringup_240m()
  └─ nx_start()
       ├─ memory_initialize()
       │    └─ up_allocate_heap()   ← 接口 1
       ├─ hardware_initialize()
       │    ├─ irq_initialize()
       │    │    └─ up_irqinitialize()   ← 接口 2
       │    ├─ clock_initialize()
       │    │    └─ up_timer_initialize() ← 接口 3
       │    └─ up_initialize()
       │         └─ arm_serialinit()     ← 接口 5
       ├─ board_early_initialize()  [当前未启用]
       └─ nx_bringup()
```

这张顺序表比任何单个源码注释都更可靠。原因是：BK7258 的 `bk7258_irq.c` 和 `bk7258_timerisr.c` 注释写着 "Called from up_initialize()"，但追踪实际 NuttX 内核代码，调用者分别是 `irq_initialize()` 和 `clock_initialize()`，二者在 `up_initialize()` 之前执行。源码注释可能是历史版本的残留。

### 教训

不要只凭源码注释判断调用关系。注释可能会：
- 跟随某个旧版内核而写；
- 复制自另一个架构 port；
- 在重构后没有更新。

正确做法是：追踪内核代码中的实际调用点。

## 3. M-003：接口 1 —— `up_allocate_heap()`

### 声明

`nuttx/include/nuttx/arch.h:738`

```c
void up_allocate_heap(FAR void **heap_start, size_t *heap_size);
```

### 内核调用者

`nuttx/sched/init/nx_start.c:587`

```c
up_allocate_heap(&heap_start, &heap_size);
kumm_initialize(heap_start, heap_size);
```

含义是：先问 architecture "heap 在哪里、有多大"，然后把结果交给用户空间内存管理器。

### ARM common 默认实现（weak）

`nuttx/arch/arm/src/common/arm_allocateheap.c:110`

```c
weak_function up_allocate_heap(void **heap_start, size_t *heap_size)
{
  uintptr_t base = g_idle_topstack;
  ...
  *heap_size  = end - base;
  *heap_start = (void *)base;
}
```

关键词是 `weak_function`：这是一个**弱符号**，允许任何架构或板级文件提供同名强符号来覆盖它。

### BK7258 的覆盖

`$BOARD/chip/common/bk7258_allocateheap.c:65`

```c
void up_allocate_heap(void **heap_start, size_t *heap_size)
{
  *heap_start = (void *)g_idle_topstack;
  *heap_size  = (size_t)((uintptr_t)_eheap - (uintptr_t)g_idle_topstack);
}
```

没有 `weak_function`，所以它是**强符号**，链接器会选择它而不是 ARM common 的默认实现。

### 内存布局

```text
0x28000000  g_intstackalloc  .irq_stack
            ...              .data / .bss
            _ebss
            IDLE stack       CONFIG_IDLETHREAD_STACKSIZE 字节
g_idle_topstack
            ...              heap (向上增长)
_eheap      0x2809FFFC       SRAM 顶端减一个 word
```

`g_idle_topstack` 定义在 `$BOARD/chip/cp/bk7258_start.c:70`：

```c
const uintptr_t g_idle_topstack = HEAP_BASE;
```

`_eheap` 来自链接脚本 `scripts/ld.script`。

### 关键理解

- IDLE 线程栈位于 `.bss` 之后；
- heap 紧接 IDLE 栈之上；
- 两者不重叠；
- 初始 MSP（`0x2809FFFC`）仅在 `__start()` 期间使用；调度器接管后所有任务栈都从 heap 分配。

## 4. 接口 2 —— `up_irqinitialize()`

### 声明

`nuttx/include/nuttx/arch.h:1814`

```c
void up_irqinitialize(void);
```

### 真实调用者

不是 `up_initialize()`。实际调用者是：

`nuttx/sched/irq/irq_initialize.c:77`

```c
void irq_initialize(void)
{
  for (i = 0; i < TAB_SIZE; i++)
    {
      g_irqvector[i].handler = irq_unexpected_isr;
    }

  ...

  up_irqinitialize();
}
```

调用发生在 `hardware_initialize()` 内部，在 `clock_initialize()` 和 `up_initialize()` 之前。

### BK7258 实现做了什么

`$BOARD/chip/common/bk7258_irq.c:165`

按执行顺序：

```text
1. 禁用所有 64 条外部 NVIC IRQ
2. 设置 VTOR
3. [如果 CONFIG_ARCH_RAMVECTORS] 复制向量表到 RAM
4. 修复两个 boot-magic slot（slot 64/65）
5. 设置默认优先级
6. 挂载 SVCall 和 HardFault handler
7. 设置 PendSV 为最低优先级
8. [如果有 MPU] 挂载 MemManage handler
9. 染色中断栈
10. 开启全局中断
```

### 为什么 SVCall 和 HardFault 在这里

调度器的上下文切换通过 SVCall 触发。如果 SVCall handler 没有挂好就开始调度，第一次切换就会 HardFault。

HardFault handler 提供故障诊断，也应该尽早启用。

### PendSV 为什么设最低优先级

PendSV 用于延迟上下文切换。它必须比所有设备 IRQ 优先级都低，否则设备中断处理中触发的 PendSV 可能抢占设备 ISR 本身。

### 中断栈染色

`arm_color_intstack()` 用特殊哨兵模式填充中断栈区域。此后在调试时可以观察栈使用深度，判断是否接近溢出。

### 关键理解

- `up_irqinitialize()` 完成后，中断系统处于"已知就绪"状态；
- 所有 IRQ 默认指向 `irq_unexpected_isr`，后续由各驱动通过 `irq_attach()` 绑定真实 handler；
- 这不是在"注册设备驱动"，而是在"建立 IRQ 框架"。

## 5. 接口 3 —— `up_timer_initialize()`

### 声明

`nuttx/include/nuttx/arch.h:1977`

```c
void up_timer_initialize(void);
```

### 真实调用者

不是 `up_initialize()`。实际调用者是：

`nuttx/sched/clock/clock_initialize.c:229`

```c
void clock_initialize(void)
{
  ...

  up_timer_initialize();

  ...
}
```

调用发生在 `irq_initialize()` 之后、`up_initialize()` 之前。

### BK7258 实现

当前实现把 scheduler SysTick 固定到芯片的 32 kHz 外部路由，不再使用 CPU clock：

```c
reload = (BK7258_SYSTICK_FREQUENCY_HZ / CLK_TCK) - 1;
modifyreg32(BK7258_SYS_CPU_POWER_SLEEP_WAKEUP, 0,
            BK7258_SYSTICK_32K_ROUTE);
putreg32(reload, NVIC_SYSTICK_RELOAD);
hardware = systick_initialize(false, BK7258_SYSTICK_FREQUENCY_HZ, -1);
```

`false` 表示选择外部 SysTick source；`BK7258_SYSTICK_FREQUENCY_HZ` 为固定
32 kHz。CPU 在 26/60/80/120/160/240/320/480 MHz 间按角色变化，都不会改变
scheduler tick 周期。

### 为什么仍然读取运行时 CPU 频率

Armv8-M 的 DWT cycle counter 跟随本地 CPU，而不是 32 kHz SysTick。Trace、CoreMark
和 `perf_gettime()` 必须通过 `bk7258_clockdiag_current_cpu_hz()` 获得当前角色的实际
频率：同一 SDK OPP 480M 在 CP/CPU0 是 240 MHz，在 AP/CPU1/CPU2 是 480 MHz。

所以初始化时有两条独立时间基准：

```text
Scheduler SysTick = 固定 32 kHz / CLK_TCK
DWT conversion    = 运行时本地 CPU Hz
```

### `up_timer_set_lowerhalf()` 是什么

```c
up_timer_set_lowerhalf(
  systick_initialize(false, BK7258_SYSTICK_FREQUENCY_HZ, -1));
```

- `systick_initialize()` 返回一个 Cortex-M SysTick lower-half 实例；
- `minor=-1` 表示不注册 `/dev/timerN` 节点，这个 timer 专用作系统 tick；
- `up_timer_set_lowerhalf()` 把它注册为 NuttX 通用 clock 框架的 lower half。

此后 NuttX 的 sleep/usleep/sched_yield 等都依赖这个 tick 信号源。

### `bk7258_systick_recalc()`

```c
void bk7258_systick_recalc(void)
```

运行时 DVFS 改变 CPU 频率后调用，但它只刷新 DWT cycle-to-time conversion，绝不
改写 SysTick route、reload 或当前相位。`sleep(1)` 不随 DVFS 漂移，因为 scheduler
从始至终使用固定 32 kHz。

### 关键理解

- `up_timer_initialize()` 必须在 IRQ 初始化之后调用，因为 SysTick 依赖中断分发；
- 性能启动 OPP 在 `nx_start()` 前完成，使初始 DWT conversion 一开始就是正确值；
- CP 性能 profile 使用 SDK OPP 240M/CPU0 240 MHz；AP 320/480 MHz 由运行时共享
  OPP vote 管理，不能把 OPP 名称直接当成 CPU0 Hz。

## 6. 接口 4 —— `arm_earlyserialinit()`

### 声明

`nuttx/arch/arm/src/common/arm_internal.h:424`

```c
#ifdef USE_EARLYSERIALINIT
void arm_earlyserialinit(void);
#endif
```

注意这是 `arm_internal.h` 而不是 `nuttx/arch.h`。它是 ARM 架构的内部接口，不是所有架构的公共接口。

### 真实调用者

`$BOARD/chip/cp/bk7258_start.c:157`

```c
#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif
```

调用发生在 `__start()` 中、`nx_start()` 之前。

### BK7258 实现

`$BOARD/chip/common/bk7258_serial.c:477`

```c
void arm_earlyserialinit(void)
{
  CONSOLE_DEV.isconsole = true;
  bk7258_uart_setup(&CONSOLE_DEV);
}
```

`CONSOLE_DEV` 是宏，展开为：

```c
#define CONSOLE_DEV  g_uart1port
```

`g_uart1port` 是一个 `struct uart_dev_s`：

```c
static struct uart_dev_s g_uart1port =
{
  .isconsole = false,
  .ops       = &g_bk7258_uart_ops,
  .priv      = &g_bk7258_uart1priv,
  .recv = { .size = ..., .buffer = g_uart1rxbuffer },
  .xmit = { .size = ..., .buffer = g_uart1txbuffer },
};
```

`arm_earlyserialinit()` 做两件事：
1. 标记这个设备为控制台；
2. 调用 `bk7258_uart_setup()` 配置 UART 寄存器。

此后 `arm_lowputc()` 可以通过轮询方式打印字符，不需要 scheduler、中断或驱动注册。

### 关键理解

- early serial 是轮询模式，不依赖中断；
- 它在 `nx_start()` 之前运行，scheduler 尚未启动；
- 目的是"让我能看到启动过程"，不是"提供完整的串口驱动"；
- `USE_EARLYSERIALINIT` 由 `CONFIG_DEV_CONSOLE` 和 serial console 配置共同派生。

## 7. 接口 5 —— `arm_serialinit()`

### 声明

`nuttx/arch/arm/src/common/arm_internal.h:420`

```c
#ifdef USE_SERIALDRIVER
void arm_serialinit(void);
#endif
```

### 真实调用者

`nuttx/arch/arm/src/common/arm_initialize.c:93`

```c
#ifdef USE_SERIALDRIVER
  arm_serialinit();
#endif
```

调用发生在 `up_initialize()` 内部。

### BK7258 实现

`$BOARD/chip/common/bk7258_serial.c:494`

```c
void arm_serialinit(void)
{
  (void)uart_register("/dev/console", &CONSOLE_DEV);
}
```

### early vs. formal

| 特性 | `arm_earlyserialinit()` | `arm_serialinit()` |
|---|---|---|
| 调用时机 | `__start()` 中 | `up_initialize()` 中 |
| scheduler 是否可用 | 否 | 是 |
| 中断是否可用 | 否（刚进入时被禁用） | 是 |
| 注册 `/dev/console` | 否 | 是 |
| `read()/write()` 可用 | 否 | 是 |
| 目的 | 启动日志 | 完整串口驱动 |

### lower half 的固定接口表

BK7258 实现了 NuttX 要求的所有 `uart_ops_s` 方法：

```c
static const struct uart_ops_s g_bk7258_uart_ops =
{
  .setup       = bk7258_uart_setup,
  .shutdown    = bk7258_uart_shutdown,
  .attach      = bk7258_uart_attach,
  .detach      = bk7258_uart_detach,
  .ioctl       = bk7258_uart_ioctl,
  .receive     = bk7258_uart_receive,
  .rxint       = bk7258_uart_rxint,
  .rxavailable = bk7258_uart_rxavailable,
  .send        = bk7258_uart_send,
  .txint       = bk7258_uart_txint,
  .txready     = bk7258_uart_txready,
  .txempty     = bk7258_uart_txempty,
};
```

这个结构体就是 NuttX upper-half 和 BK7258 lower-half 之间的"合同"。upper-half 不关心寄存器地址、FIFO 深度或中断编号；它只调用这些固定方法。

### 关键理解

- `arm_earlyserialinit()` 先让轮询输出工作；
- `arm_serialinit()` 后注册 `/dev/console`，使标准文件操作可用；
- 两者可以使用同一个 `struct uart_dev_s` 实例；
- 当前 BK7258 的 TX 是轮询模拟（`txint` 直接排空缓冲区），RX 是中断驱动。

## 8. 弱符号覆盖机制

NuttX 的 `up_allocate_heap()` 在 ARM common 层定义为：

```c
weak_function up_allocate_heap(...)
```

BK7258 在 `bk7258_allocateheap.c` 中定义为：

```c
void up_allocate_heap(...)
```

链接器规则：

```text
同名符号：强 > 弱

ARM common: weak
BK7258:     strong  ← 链接器选择这个
```

这就是为什么 architecture port 可以"提供默认值"，而板级可以"选择性覆盖"。

BK7258 的 `up_irqinitialize()` 和 `up_timer_initialize()` 没有使用 weak 方式，因为 ARM common 层没有提供默认实现——每个 architecture port 必须自己提供。

## 9. 构建集成

BK7258 的 `chip/Make.defs` 和 `chip/CMakeLists.txt` 把五个接口文件加入构建：

```makefile
CHIP_CSRCS += bk7258_serial.c
CHIP_CSRCS += bk7258_irq.c
CHIP_CSRCS += bk7258_timerisr.c
CHIP_CSRCS += bk7258_allocateheap.c
```

如果某个文件没有加入构建，对应的函数定义就不存在，链接器会报 undefined reference。

"文件存在于目录"只说明源码在那里，不代表它进入了当前构建。

## 10. 教学简化与真实路径

BK7258 `bk7258_irq.c:161` 注释：

> Called from up_initialize().

BK7258 `bk7258_timerisr.c:73` 注释：

> Called during start-up (up_initialize)

但追踪 NuttX 内核代码：

- `irq_initialize()` 调用 `up_irqinitialize()`；
- `clock_initialize()` 调用 `up_timer_initialize()`；
- 两者在 `up_initialize()` 之前执行。

因此注释中的 "up_initialize" 应理解为"在 `nx_start()` 的硬件初始化阶段"，而不是字面上的"由 `up_initialize()` 函数调用"。

这类不一致在嵌入式代码中很常见。代码本身（`nx_start()` → `hardware_initialize()` → `irq_initialize()` → `up_irqinitialize()`）才是真相，注释只是参考。

## 11. 自测题

1. `up_allocate_heap()` 使用什么机制覆盖 ARM common 默认实现？
2. `up_irqinitialize()` 的真实调用者是谁？
3. `up_timer_initialize()` 为什么要读运行时 CPU 频率？
4. `arm_earlyserialinit()` 与 `arm_serialinit()` 的关键区别是什么？
5. 如果 `bk7258_timerisr.c` 没有加入 Makefile，会发生什么？
6. BK7258 `bk7258_irq.c` 注释说 "Called from up_initialize()"，这准确吗？

答案：

1. 弱符号覆盖：ARM common 定义为 `weak_function`，BK7258 定义为强符号。
2. `irq_initialize()`，不是 `up_initialize()`。
3. 因为 CPU 可能在 `__start()` 中从 26 MHz 切换到 320 MHz；如果 reload 基于错误频率，tick 周期会严重偏差。
4. early serial 在 `__start()` 中用轮询模式工作，不注册设备；formal serial 在 `up_initialize()` 中注册 `/dev/console`。
5. 链接器报 undefined reference `up_timer_initialize`。
6. 不完全准确。当前 NuttX 代码中真实调用者是 `irq_initialize()`，它在 `up_initialize()` 之前执行。注释应理解为"在 `nx_start()` 硬件初始化阶段"。
