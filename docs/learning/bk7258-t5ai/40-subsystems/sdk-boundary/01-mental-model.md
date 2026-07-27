# SDK 边界：外部库与 RTOS 的共存模式

本篇讲解嵌入式移植中如何处理外部厂商 SDK 与 RTOS 内核的边界问题。以 BK7258 SDK 与 NuttX 的集成为例，覆盖 IRQ Bridge 设计模式、archive ownership 陷阱、link-order 冲突等关键概念。

> **来源记录**
>
> - 教学主题：外部 SDK 与 NuttX RTOS 的边界管理模式
> - `$CONTEST` source commit：`c588afbd8e0f1d30723f5076e585673a6ace8a4e`
> - 实现 source：`$BOARD/chip/bk7258_sdk_irq.c` / `bk7258_sdk_irq.h`
> - 直接上游：BK7258 SDK（`bk_idk/` 下的预编译 archive）
> - 最后核对日期：2026-07-27
> - 验证状态：IRQ Bridge（Stage N6）build-verified；TIMER1/GPIO 通过 Bridge 通达 ISR
> - 教学简化：本文只分析 SDK API 适配模式，不展开 BK7258 SDK 内部实现

## 1. 问题：两个世界的碰撞

嵌入式移植中，厂商 SDK 通常为一套独立的 bare-metal 或 FreeRTOS 环境设计。当需要把这些 SDK 放到 NuttX 这样的 RTOS 中时，会产生以下冲突：

```text
   BK7258 SDK 世界                NuttX RTOS 世界
   ──────────────                 ──────────────
   bk_int_isr_register()      vs  irq_attach()
   SDK 私有内存分配器          vs  kmm_malloc()
   SDK 服务线程               vs  NuttX task/thread
   预编译 archive (.a)         vs  链接时优先级
   SDK 权限/HAL 调用           vs  NuttX 内核隔离
```

核心问题不是"能不能调用 SDK 函数"，而是**谁拥有哪个资源，在什么时刻拥有**。

## 2. 三种共存模式

### 模式 A：直接重写

```text
NuttX 驱动直接操作寄存器，不使用 SDK API。
优点：可控性强，无 link-order 问题。
缺点：工作量大，需要完整芯片手册。
适用：启动代码、关键实时路径。
```

BK7258 的 `bk7258_start.S`、`bk7258_serial.c` 属于此类。

### 模式 B：Adapter 适配层

```text
NuttX 驱动 → thin adapter → SDK API → 寄存器
优点：复用 SDK 已有驱动逻辑。
缺点：adapter 必须理解两边的语义差异。
适用：复杂外设（如 GPIO、I2C、SPI、WDT）。
```

BK7258 的 GPIO lower-half 和 WDT 驱动属于此类。Adapter 层将 NuttX 的 `irq_attach()/up_enable_irq()` 请求翻译为 SDK 的 `bk_int_isr_register()/gpio_int_enable()` 调用。

### 模式 C：Bridge 桥接

```text
SDK API → bridge → NuttX 内核 API
优点：SDK 内部逻辑不感知 NuttX。
缺点：bridge 成为所有中断的瓶颈；需要处理优先级反转。
适用：SDK 中断注册 API（例如 bk_int_isr_register）。
```

BK7258 的 SDK IRQ Bridge 是这种模式的典型实现。

## 3. IRQ Bridge：从 SDK 中断到 NuttX ISR

### 设计的核心矛盾

SDK 对外暴露的是：

```c
// SDK API（BK7258 CP SDK）
bk_err_t bk_int_isr_register(icu_int_src_t source,
                              int_group_isr_t handler, void *arg);
```

而 NuttX 需要的是：

```c
// NuttX API
int irq_attach(int irq, xcpt_t isr, FAR void *arg);
```

两者的接口签名完全不同：
- SDK 用 `icu_int_src_t`（枚举 0..63），NuttX 用 `int irq`（16..79）
- SDK 的 handler 签名 `void (*)(void)` 与 NuttX 的 `int (*)(int, void*, void*)` 不兼容

### Bridge 的解决方案

```text
SDK 调用者                   Bridge 内部                    NuttX
────────                    ──────────                    ─────
bk_int_isr_register(        bk7258_sdk_source_to_irq() → 计算 irq = source + 16
  INT_SRC_GPIO,             irq_detach(irq)              → 卸载旧 handler
  my_handler,               up_prioritize_irq(irq, prio)
  NULL)                     irq_attach(irq,                → 绑定统一的
                              bk7258_sdk_irq_dispatch)      dispatch 入口
                            ledger[source] = my_handler   → 记录真实 SDK handler
                            up_enable_irq(irq)
```

当中断触发时：

```text
硬件中断
  → NVIC
  → exception_common
  → bk7258_sdk_irq_dispatch(irq)   // NuttX 看到的 ISR
    → source = irq - 16
    → handler = ledger[source]     // 查找真正的 SDK handler
    → handler()                    // 调用 SDK handler
```

### 这个设计的优点

1. **SDK 代码不修改**：`bk_int_isr_register()` 是 Bridge 对外暴露的同名函数，SDK 内部代码调用的是 Bridge 版本，不需要改动
2. **编译时验证**：`_Static_assert` 确保 source↔irq 映射在编译时就正确
3. **共享 dispatch**：所有 SDK 中断共用同一个 `exception_common` 入口

### 这个设计的代价

1. **多一层查表**：每个 SDK 中断多一次 `ledger[source]` 查表和函数调用
2. **优先级集中管理**：Bridge 为所有 SDK 中断设置统一默认优先级（LCD 除外），SDK 代码不能独立控制 NVIC 优先级
3. **spinlock 持有**：`bk_int_isr_register()` 全程持 spinlock，不允许嵌套注册

## 4. Archive Ownership 的陷阱

BK7258 SDK 以预编译的 `.a` archive 形式提供。这些 archive 是**第三方拥有、不可修改的二进制产物**。这带来了几个工程陷阱：

### 陷阱 1：符号冲突

SDK archive 和 NuttX 内核可能定义同名符号（例如 `__assert_func`、内存分配函数、printf 变体）。链接器按命令行顺序搜索 archive 符号，先找到的定义胜出。

```text
链接命令：ld ... bbk.a bslib.a mmgmt.a ... libnuttx.a ...

如果 bbk.a 中定义了 _sbrk（作为 SDK 私有堆的实现），
而 libnuttx.a 中也需要 _sbrk（作为系统堆的底层），
链接器会先看到 bbk.a 的定义并使用它。
```

### 陷阱 2：全局状态竞争

SDK archive 内部可能维护全局状态（例如中断使能 flags、时钟 gating 计数器），这些状态不在 NuttX 的锁保护范围内。如果 NuttX ISR 和 SDK 回调同时修改同一个外设寄存器，可能导致数据竞争。

### 陷阱 3：boot magic 覆盖

这是 BK7258 实际遇到的问题：SDK bootloader 在向量表 slot [64] 和 [65] 存入 `"BK7236\0\0"` 作为应用镜像合法性标记。但 NuttX 需要这两个 slot 指向 `exception_common` 处理 IRQ 64/65。

解决方法：`up_irqinitialize()` 在 RAM vector 切换后，用 `arm_ramvec_attach()` 覆盖这两个 slot。

## 5. 编译时守卫：`_Static_assert`

IRQ Bridge 使用了一组编译时断言来防止配置漂移：

```c
_Static_assert(BK7258_SDK_IRQ_COUNT == 64,
               "Stage B gate: SDK IRQ source count must be 64");
_Static_assert(BK7258_SDK_IRQ_FIRST + BK7258_SDK_IRQ_COUNT == NR_IRQS,
               "Stage B gate: SDK source 0..63 must map to IRQ 16..79");
_Static_assert(BK7258_SDK_IRQ_PRIORITY_BITS == 3,
               "Stage B gate: STAR NVIC implements three priority bits");
_Static_assert(INT_SRC_LCD == 27,
               "Stage B gate: LCD priority exception must remain source 27");
```

这些断言的价值：
- 如果 SDK 头文件中 `INT_SRC_NONE` 的值发生了变化，编译直接失败
- 如果 `NR_IRQS` 因为配置变化而不再是 80，映射公式立即失效
- 如果 LCD 的中断源编号被 SDK 版本更新改变，优先级特殊处理逻辑会报错

这是一个实用的模式：**把跨仓库的不变量在编译时检查，而不是在运行时发现。**

## 6. 三个子系统的 SDK 适配对比

| 子系统 | 适配模式 | SDK API 使用 | 关键桥接 |
|---|---|---|---|
| UART/console | 直接重写 | 不依赖 SDK 串口 API | 无 |
| WDT | Adapter | `bk_wdt_initialize()` → NuttX WDT upper-half | 在 `$BOARD/src/bk7258_bringup.c` 中注册 |
| GPIO | Adapter + Bridge | `gpio_dev_map()` / `gpio_int_enable()` / `bk_int_isr_register()` | `bk7258_gpio_open_route()` 做 source55/37 统一 |
| Timer1 | Bridge | `bk_int_isr_register(INT_SRC_TIMER1, ...)` | 通过 Bridge 走 NuttX timer ISR |

## 7. 自测题

1. BK7258 SDK 中断注册 API 为什么不能直接用 `irq_attach()`？
2. IRQ Bridge 的 `ledger` 表是什么？它和 `g_irqvector[]` 的关系是什么？
3. Archive ownership 为什么可能导致 link-order 问题？
4. `_Static_assert` 在 IRQ Bridge 中起到了什么作用？
5. 三种 SDK 共存模式分别适合什么场景？

答案：

1. SDK 使用 `icu_int_src_t`（枚举值 0..63）和 `int_group_isr_t`（`void (*)(void)`），而 NuttX 使用 `int irq`（16..79）和 `xcpt_t`（`int (*)(int, void*, void*)`），签名完全不同。
2. `ledger` 即 `g_bk7258_sdk_irq_handlers[]`，按 SDK source 索引存储真正的 SDK handler。`g_irqvector[]` 按 NuttX logical IRQ 索引存储 `bk7258_sdk_irq_dispatch`。中断到达时，`dispatch` 从 `ledger[source]` 取出真实 handler 调用。
3. 预编译 archive 在整个链接命令行中的位置影响符号解析优先级。如果 archive 中定义的同名符号在 NuttX 库之前被找到，链接器就会使用 archive 版本，可能导致 NuttX 内核使用了 SDK 的私有实现。
4. 将跨仓库的数值不变量（source 数量、IRQ 映射范围、优先级位数）在编译时检查。SDK 版本更新或配置变化导致这些值不一致时会立即编译失败。
5. 模式 A（直接重写）适合启动代码和简单外设；模式 B（Adapter）适合需要复用 SDK 复杂驱动的外设；模式 C（Bridge）适合 SDK 中断注册 API 这种语义不兼容但调用频繁的接口。
