# GPIO Lower-half：用户态 `/dev/gpioN` 驱动

本篇讲解 NuttX GPIO 的 upper/lower half 架构、用户态 ioctl 接口，以及 BK7258 T5-AI 板上两个 GPIO 实例（P9 LED 和 P29 USERKEY）从寄存器到中断回调的完整实现。

> **来源记录**
>
> - 教学主题：NuttX GPIO upper/lower half 架构与 BK7258 C0/C1/C2 用户态 GPIO 驱动
> - `$CONTEST` source commit：`c588afbd8e0f1d30723f5076e585673a6ace8a4e`
> - 实现 source：`$BOARD/chip/cp/bk7258_gpio_lowerhalf.c`
> - 最后核对日期：2026-07-27
> - 验证状态：C0（LED 轮询）/ C1（USERKEY route gate）/ C2（USERKEY 边沿中断）均已 board-verified
> - 教学简化：本文复用 NuttX 标准 GPIO upper-half 概念，不展开其内部 `ioctl` 转发链的完整实现

## 1. NuttX GPIO 分层模型

NuttX 将 GPIO 子系统分成两层：

```text
        应用层
          │  open("/dev/gpio0") / read / write / ioctl
          ▼
   ┌───────────────────┐
   │  GPIO upper-half   │  nuttx/drivers/ioexpander/gpio.c
   │  (框架无关)        │  提供统一的 /dev/gpioN 设备节点
   │                    │  实现 ioctl(GPIOC_READ / GPIOC_WRITE / ...)
   └────────┬───────────┘
            │  struct gpio_operations_s 的六个函数指针
            ▼
   ┌───────────────────┐
   │  GPIO lower-half   │  $BOARD/chip/cp/bk7258_gpio_lowerhalf.c
   │  (芯片/板相关)     │  实现 go_read / go_write / go_attach / go_enable ...
   └───────────────────┘
            │  调用 SDK API
            ▼
   ┌───────────────────┐
   │  BK7258 硬件寄存器 │  GPIO 方向、电平、中断配置
   └───────────────────┘
```

### upper-half 做什么

- 注册 `/dev/gpioN` 字符设备（`gpio_register()`）
- 处理用户态 `open()/read()/write()/ioctl()` 调用
- 将 ioctl 命令分发给 lower-half 的对应函数

### lower-half 做什么

- 提供 `struct gpio_operations_s` 六个回调：
  - `go_read` — 读引脚电平
  - `go_write` — 写引脚电平
  - `go_attach` — 绑定中断回调
  - `go_enable` — 打开/关闭中断
  - `go_setpintype` — 设置引脚方向（输入/输出/中断边沿类型）
  - `go_setmask` — 控制 NVIC 中断使能

### gpio_pintype_e 决定你的行为

| pintype | 含义 | 典型操作 |
|---|---|---|
| `GPIO_OUTPUT_PIN` | 输出引脚 | go_read / go_write |
| `GPIO_INPUT_PIN` | 输入引脚 | go_read |
| `GPIO_INTERRUPT_PIN` | 中断引脚（双沿） | go_read / go_attach / go_enable |
| `GPIO_INTERRUPT_FALLING_PIN` | 下降沿中断 | 同上，go_setpintype 转换为下降沿 |
| `GPIO_INTERRUPT_RISING_PIN` | 上升沿中断 | 同上，转换为上升沿 |

## 2. BK7258 的两个 GPIO 实例

### `/dev/gpio0` — P9 LED（输出）

```c
static struct bk7258_gpio_output_s g_bk7258_gpio_led =
{
  .gpio =
    {
      .gp_pintype = GPIO_OUTPUT_PIN,
      .gp_ops = &g_bk7258_gpio_output_ops,
    },
  .lock = NXMUTEX_INITIALIZER,
  .pin = BK7258_GPIO_LED_PIN,  // GPIO_9 = 9
};

static const gpio_config_t g_bk7258_gpio_led_config =
{
  .io_mode = GPIO_OUTPUT_ENABLE,
  .pull_mode = GPIO_PULL_DISABLE,
  .func_mode = GPIO_SECOND_FUNC_DISABLE,
};
```

特点：
- 纯输出，没有 `go_attach` 和 `go_enable`
- 通过 `gpio_output_write()` → SDK `gpio_output_low()/gpio_output_high()` 控制
- C0 阶段已 board-verified：NSH 下通过 `ioctl(GPIOC_WRITE)` 开关 LED

### `/dev/gpio1` — P29 USERKEY（下降沿中断）

```c
static struct bk7258_gpio_interrupt_s g_bk7258_gpio_key =
{
  .gpio =
    {
      .gp_pintype = GPIO_INTERRUPT_FALLING_PIN,
      .gp_ops = &g_bk7258_gpio_key_ops,
    },
  .lock = NXMUTEX_INITIALIZER,
  .pin = BK7258_GPIO_KEY_PIN,  // GPIO_29 = 29
};

static const gpio_config_t g_bk7258_gpio_key_config =
{
  .io_mode = GPIO_INPUT_ENABLE,
  .pull_mode = GPIO_PULL_UP_EN,
  .func_mode = GPIO_SECOND_FUNC_DISABLE,
};
```

特点：
- 下降沿中断输入，内部上拉（按下为低电平）
- 支持 `go_attach` 绑定回调、`go_enable` 开关中断
- 中断通过 SDK IRQ Bridge source55 到达 NVIC

## 3. P29 中断的三道门（分层验证路径）

BK7258 GPIO 中断启用时有三层使能必须全部打开，这也是 C0/C1/C2 三个阶段的分层验证设计：

### C0 — 门 1：GPIO 引脚配置

```
门 1：gpio_dev_unmap() + gpio_dev_map()
  → gpio_config_t 写入 IO_MODE / PULL_MODE / FUNC_MODE
  → gpio_int_enable(pin, INT_TYPE, handler)
```

验证方法：`/dev/gpio0` 轮询 LED → 确认 SDK GPIO API 链路可达

### C1 — 门 2：中断路由（route gate）

```
门 2：GPIO IRQ 路由到 CPU0 NVIC
  → bk7258_gpio_cp_irq_enable()      // 系统级 GPIO 中断转发
  → bk_int_isr_register(source37, handler)  // 非安全端绑定
  → 写 ROUTE_REG bit5/bit23           // 路由使能
```

验证方法：确认 source55/37 handler 镜像一致性，route bits 可写入并恢复

### C2 — 门 3：中断到达 CPU

```
门 3：NVIC 使能
  → go_setmask(dev, true) → bk_int_isr_register(source55, per_pin_callback)
  → up_enable_irq(55 + 16 = 71)
  → 中断回调记录 g_bk7258_gpio_key_isr_count++
```

验证方法：按下 USERKEY → 中断计数递增 → 回调触发

**当前状态：C0/C1/C2 均已 board-verified。**

## 4. 中断回调的完整链路

按下 P29 USERKEY 后：

```text
1. GPIO29 硬件检测到下降沿
   ↓
2. 芯片级 GPIO 中断控制器产生 source55 (INT_SRC_GPIO) 和 source37
   ↓
3. NVIC 接收 IRQ 55 → slot [71] (55 + 16)
   ↓
4. exception_common → bk7258_sdk_irq_dispatch(71)
   ↓
5. source = 71 - 16 = 55
   ↓
6. g_bk7258_sdk_irq_handlers[55]()  // SDK per-pin callback
   ↓
7. 回调中执行：g_bk7258_gpio_key_isr_count++
```

其中第 6 步的 per-pin callback 是在 `bk7258_gpio_key_attach()` 中通过 `gpio_int_enable()` 注册给 SDK 的，它运行在中断上下文中，不能阻塞、不能分配内存。

## 5. route gate 的设计要点

`bk7258_gpio_open_route()` 实现了三个关键安全保证：

1. **handler 镜像检查**：snapshot source55 和 source37 的 handler，确认两者一致或 source37 为空（此时注册 source37 指向 source55 的 handler）
2. **route bits 写入验证**：写 `BK7258_GPIO_ROUTE_MASK`（bit5 + bit23）后回读，确保 bit5 和 bit23 都已置位
3. **rollback 能力**：如果 route 未完整使能或 handler 冲突，通过 `bk7258_gpio_close_route()` 恢复原状

这保证了每次 attach→enable 和 disable→detach 的配对调用不会留下半打开的路由状态。

## 6. GPIO IRQ 与 UART IRQ 的区别

| 维度 | UART1 | GPIO P29 |
|---|---|---|
| NuttX ISR 注册 | 直接 `irq_attach(31, uart_isr)` | 通过 SDK IRQ Bridge → `bk_int_isr_register(source55, per_pin)` |
| 中断使能时机 | `uart_rxint()` 中同时使能外设 + NVIC | `go_setmask()` 打开 NVIC；外设中断在 `gpio_int_enable()` 时已使能 |
| 上下文切换 | 中断上下文→uart_ops(可能阻塞) | 中断上下文→per-pin callback（非阻塞） |
| 恢复/回滚 | UART upper-half 管理 | lower-half 管理 route 状态 |

GPIO 通过 SDK IRQ Bridge 的原因是：BK7258 SDK 用 `gpio_int_enable()` API 配置外设中断，而该 API 内部会调用 `bk_int_isr_register()` 注册 SDK 风格的回调，不是 NuttX 的 `irq_attach()`。

## 7. 诊断方法论：从外到内

当 GPIO 中断不触发时，按以下顺序逐层排查：

```text
1. 引脚是否产生电平变化？
   → 多用电表或轮询模式读取引脚值

2. SDK GPIO API 是否初始化正确？
   → 检查 gpio_dev_map() / gpio_config_t

3. GPIO 中断使能是否生效？
   → 检查 gpio_int_enable() 返回值和中断类型

4. 中断路由是否打开？
   → 检查 route gate 的 bit5/bit23 状态

5. NVIC 是否使能？
   → 读 NVIC_ISER 确认对应位

6. handler 是否正确注册？
   → bk7258_sdk_irq_snapshot_handler(source) 确认

7. 回调是否执行？
   → 在 callback 中记录计数或打印
```

这正是 C0 → C1 → C2 三个阶段的设计思路：从最外层开始验证，逐步向内推进。

## 8. 自测题

1. NuttX GPIO 的 upper-half 和 lower-half 分别做什么？
2. `/dev/gpio0` 和 `/dev/gpio1` 分别在 BK7258 上对应哪个引脚？哪个支持中断？
3. GPIO P29 中断启用时需要打开哪三道门？
4. 为什么 GPIO 中断走 SDK IRQ Bridge 而不是直接 `irq_attach()`？
5. C1 的 route gate 为什么需要同时 snapshot source55 和 source37？

答案：

1. upper-half 提供 `/dev/gpioN` 设备节点和统一 ioctl 接口；lower-half 实现芯片相关的 `gpio_operations_s` 六个回调。
2. `/dev/gpio0` → P9 LED（输出）、`/dev/gpio1` → P29 USERKEY（下降沿中断输入）。
3. 门 1：GPIO 引脚配置（gpio_dev_map + gpio_int_enable）；门 2：中断路由（route gate bit5/bit23）；门 3：NVIC 使能（up_enable_irq）。
4. 因为 SDK 的 `gpio_int_enable()` API 内部使用 `bk_int_isr_register()` 注册回调，BRIDGE 将 SDK handler 注册到 NuttX `irq_attach()` 的 `bk7258_sdk_irq_dispatch()` 下。
5. BK7258 有两个 GPIO IRQ source：source55（secure）和 source37（non-secure）。两者需要共享同一个 per-pin handler，所以 snapshot 后检查一致性或注册 source37 指向 source55 的 handler。
