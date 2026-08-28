# 手把手教程：把 BK7258 串口从「寄存器版」改成「SDK Wrapper 版」

> **历史资料 / Historical note：**本文记录 UART wrapper 初次落地时的教学过程，
> 不是当前分支的改码清单。现役 `bk7258_serial.c` 已经采用 SDK wrapper；核角色、
> 配置和 API 必须以当前源码、维护 defconfig、SDK bundle 及主机/实板验证为准。
> 本文保留在 `nuttx-port/` 用于解释设计演进，不能单独作为当前符合性证据。

> 适用对象：嵌入式小白
> 目标文件：`contest2026_135_yongwangzhiqian/chips/bk7258/common/bk7258_serial.c`
> 前置知识：你不用会寄存器、不用会 RTOS 内核，照着本教程一步步抄就能改完。
>
> AP/CP SDK 静态库的构建、UART 对象重编和项目导入流程见：
> `docs/platforms/bk7258/nuttx-port/sdk-static-library-import.md`。
> 当前 SDK bundle 统一放在
> `bk_idk/armino_as_lib/versions/<version>/{cp,ap}`，默认版本是
> `v3.1.1.9`；本文后续命令均按该默认版本书写。

---

## 第 0 章：先搞懂几个「黑话」（小白必读，先别跳过）

改代码前，先把 6 个词讲明白。讲完你再看代码就不慌了。

### 0.1 什么是「寄存器」？
芯片内部有一堆**带固定地址的小盒子**（比如地址 `0x45830000`），CPU 往盒子里写数、或从盒子里读数，就能控制硬件（比如让串口发一个字节）。
旧的 `bk7258_serial.c` 就是直接写这些地址，比如：
```c
BK7258_UART1_FIFO_PORT = ch;   // 把字符 ch 写进 FIFO 端口寄存器
```
这种写法叫**直接操作寄存器**，最底层、最硬核，但也最容易写错、最难维护。

### 0.2 什么是「SDK」？
SDK（Software Development Kit，软件开发包）是原厂工程师写好的**一堆现成函数**。你不用知道寄存器在哪，只要调用函数名就行。比如：
```c
bk_uart_write_bytes(id, &ch, 1);   // 让 SDK 帮你把 ch 发出去
```
本教程的核心思想就是：**别碰寄存器，改去调 SDK 函数**。

### 0.3 什么是 NuttX 串口的「上下两层」？
NuttX 的串口驱动分两层，互相不认识，只靠一张「契约表」对接：

- **上层（upper half，NuttX 自带，你不用管）**：负责把字符放进 `/dev/console`、管理环形缓冲、处理你敲的 `read()/write()` 系统调用。
- **下层（lower half，就是你要写的 `bk7258_serial.c`）**：NuttX 只认识一张叫 `struct uart_ops_s` 的表，里面是一堆**函数指针**（`setup`、`send`、`receive`、`rxint`……）。

> 关键认知：**NuttX 根本不在乎你底层怎么跟硬件说话，它只管调用 `uart_ops_s` 里的函数。** 所以你把「写寄存器」换成「调 SDK 函数」，对 NuttX 来说毫无区别。

### 0.4 什么是 `uart_ops_s`（契约表）？
它长这样（节选）：
```c
struct uart_ops_s
{
  int  (*setup)(struct uart_dev_s *dev);        // 打开设备时初始化
  void (*shutdown)(struct uart_dev_s *dev);
  int  (*attach)(struct uart_dev_s *dev);       // 挂接收中断
  int  (*receive)(struct uart_dev_s *dev, unsigned int *status); // 收 1 字节
  void (*rxint)(struct uart_dev_s *dev, bool enable);  // 开/关 RX 中断
  bool (*rxavailable)(struct uart_dev_s *dev);   // 现在有没有数据？
  void (*send)(struct uart_dev_s *dev, int ch);  // 发 1 字节
  ...
};
```
你要做的，就是给这张表里的每个函数写一个**具体实现**，然后让 NuttX 用你这张表。

### 0.5 什么是 ISR（中断服务程序）？
串口收到一个字节时，硬件会「打断」CPU，让 CPU 立刻跳去执行一段专门的处理函数，这段函数就叫 **ISR（Interrupt Service Routine，中断服务程序）**。
在串口里，RX 的 ISR 职责是：「有数据来了，快把它们搬进 NuttX 的接收缓冲」。

### 0.6 什么是「Wrapper（包装）模式」？
一句话：**把 `uart_ops_s` 里每一个函数，从「直接写寄存器」改成「转发调用 SDK 的 `bk_uart_*` 函数」。你不再碰任何 `0x45830000` 这种地址。**

| 旧（寄存器） | 新（SDK wrapper） |
|---|---|
| `BK7258_UART1_FIFO_PORT = ch` | `bk_uart_write_bytes(id, &ch, 1)` |
| `BK7258_UART1_INT_ENABLE \|= bit` | `bk_uart_enable_rx_interrupt(id)` |
| `irq_attach(...)` + `up_enable_irq(...)` | `bk_uart_register_rx_isr(id, cb, dev)` |
| `读 FIFO_STAT 判断有数据` | `bk_uart_read_bytes(id, &b, 1, 0)` |

最爽的一点：**旧代码里那「三道中断门」（外设 → 片上中断控制器 → NVIC）你全都不用管了**，SDK 内部自己打开。你只负责「告诉 SDK：RX 来了叫我」。

---

## 第 1 章：先确认「你在哪个核上」（这是前提，否则白干）

BK7258 不是单核芯片，它是一个 **AP/CP 双核（实际是三核 Cortex-M33，但逻辑角色分 AP 和 CP）** 的结构。SDK 根目录 `bk_avdk_smp` 下一眼就能看到 `ap/` 和 `cp/` 两个并列大目录，而且 `uart.h` 都有**两份**（`ap/include/driver/uart.h` 和 `cp/include/driver/uart.h`）。

> 为什么这件事重要？因为 `ap/` 和 `cp/` 各自有一套编译好的库（`.a` 文件）。如果你站错核，调的 `bk_uart_*` 符号虽然能编过，运行时却会跑飞。

**我们核实完的结论：NuttX 跑在 CP 核，UART1 在 CP 上直连，wrapper 用 CP 的 SDK 完全自洽。**

证据链（都在你工程里）：
1. 构建目标是 CP 镜像，使用的 SDK 头位于
   `armino_as_lib/versions/v3.1.1.9/cp/include/`；
2. 活跃基础配置 `configs/openvela_cp/defconfig` 里是 `CONFIG_BK7258_AP_CONTROL=y`（即 Beken 术语里的 CP 角色），入口 `nsh_main`——这才是带串口的镜像；
3. 原 `bk7258_serial.c` 直接在 CP 地址 `0x45830000` 上戳寄存器、用 `irq_attach(BK7258_IRQ_UART1,…)` 挂**原生**中断，说明 UART1 硬件对 CP 核是直接可访问的，没走跨核 RPC。

还有一个开关对我们有**实质影响**：

```27:27:.../configs/openvela_cp/defconfig
CONFIG_BK7258_SDK_IRQ_BRIDGE=y
```
`SDK_IRQ_BRIDGE`（SDK 中断桥）开着，意味着「SDK 接管中断、再桥接到 NuttX」是预期路径——正好和我们的 wrapper 写法（不自己 `irq_attach`、用 `bk_uart_register_rx_isr`）对上。**这个开关千万别随手关掉，否则 RX 中断投不进 NuttX，控制台收不到键盘输入。**

> 好消息：我们 diff 过 `ap/` 和 `cp/` 两份 `uart.h`，除两个边角函数外，本教程依赖的全部 `bk_uart_*` 签名两份完全一致。所以接口本身不会让你链接报错。真正要守住的是「编 CP 镜像 + 中断桥开着」这条运行期逻辑。

---

## 第 2 章：旧代码在干什么（看懂才能改）

旧 `bk7258_serial.c` 是纯寄存器版，核心逻辑：

- `bk7258_uart_setup()`：把 bootloader 配好的 UART1 补充打开「RX 使能」、把 RX FIFO 阈值设成 1 字节（防中断风暴）。
- `bk7258_uart_attach()`：`irq_attach(BK7258_IRQ_UART1, bk7258_uart_isr, dev)` —— 自己把中断挂到 NVIC。
- `bk7258_uart_isr()`：清中断位 → 调 `uart_recvchars(dev)` 把 FIFO 数据搬进上层缓冲。
- `bk7258_uart_rxint()`：手动打开「三道中断门」（外设使能 → 片上中断控制器 → NVIC）。
- `bk7258_uart_send()`：调 `arm_lowputc()` 轮询发一个字节。

这些都会在本教程里被替换成 SDK 调用。

---

## 第 3 章：我们要改成的样子（总览）

| 模块 | 旧做法 | 新做法（wrapper） |
|---|---|---|
| 私有数据 | 存寄存器基地址 `uartbase` | 存 `id`（哪路 UART）+ `rxbyte` 缓存 |
| `setup` | 写寄存器补 RX 使能、设阈值 | 调 `bk_uart_driver_init` + `bk_uart_set_baud_rate` + `bk_uart_set_rx_full_threshold` |
| `attach` | `irq_attach` + 自己挂 NVIC | `bk_uart_register_rx_isr(id, isr, dev)` |
| `isr` | 自己清中断位 | 只调 `uart_recvchars(dev)`，清中断交给 SDK |
| `rxint` | 手动开三道中断门 | `bk_uart_enable/disable_rx_interrupt(id)` 一行 |
| `receive` | 读 `FIFO_PORT` | 从 `rxbyte` 缓存取（配合 `rxavailable`） |
| `rxavailable` | 读 `FIFO_STAT` 位 | `bk_uart_read_bytes(id, &b, 1, 0)` 非阻塞读 |
| `send` | `arm_lowputc` | `bk_uart_write_bytes(id, &b, 1)` |
| `txready`/`txempty` | 读 `FIFO_STAT` | `txready` 返回 `true`；`txempty` 用 `bk_uart_is_tx_over` |

---

## 第 4 章：SDK 函数速查表（抄作业专用）

以下函数声明都在 `<driver/uart.h>`，我们已逐一核实存在。返回值是 `bk_err_t`，成功就是 `BK_OK`（定义在 `common/bk_err.h`）。

```c
bk_err_t bk_uart_driver_init(void);                       // 拉起 SDK UART 驱动（最先调）
bk_err_t bk_uart_set_baud_rate(uart_id_t id, uint32_t baud_rate); // 显式设波特率
bk_err_t bk_uart_set_rx_full_threshold(uart_id_t id, uint8_t threshold); // RX FIFO 满阈值
bk_err_t bk_uart_enable_rx_interrupt(uart_id_t id);       // 开 RX 中断
bk_err_t bk_uart_disable_rx_interrupt(uart_id_t id);      // 关 RX 中断
bk_err_t bk_uart_register_rx_isr(uart_id_t id, uart_isr_t isr, void *param); // 注册 RX 回调
bk_err_t bk_uart_write_bytes(uart_id_t id, const void *data, uint32_t size); // 发数据
int      bk_uart_read_bytes(uart_id_t id, void *data, uint32_t size, uint32_t timeout_ms); // 收数据
bool     bk_uart_is_tx_over(uart_id_t id);               // TX FIFO 是否发空
```

关键类型（已核实）：
- `uart_id_t` 枚举：`UART_ID_0 = 0`、`UART_ID_1 = 1`、`UART_ID_2 = 2`。控制台用 `UART_ID_1`。
- `uart_isr_t` 签名：`typedef void (*uart_isr_t)(uart_id_t id, void *param);`
- `uart_config_t` 字段：`baud_rate`、`data_bits`、`parity`、`stop_bits`、`flow_ctrl`、`src_clk`、`rx_dma_en`、`tx_dma_en`。
- 枚举值：`UART_DATA_8_BITS`、`UART_PARITY_NONE`、`UART_STOP_BITS_1 = 0`、`UART_FLOWCTRL_DISABLE = 0`、`UART_SCLK_XTAL_26M`、`UART_DMA_DISABLE`。

> 注意：原厂 bk7236n 的参考代码里用了 `bk_uart_read_fifo_is_read()`、`bk_uart_is_tx_ready()` 等 helper，但**那些 bk7258 的 SDK 没有**，所以不能照抄。我们改用 `bk_uart_read_bytes(..., 0)`（非阻塞）和 `bk_uart_is_tx_over()`，对应等价功能。

---

## 第 5 章：逐行改写（每一步精准解释）

下面按文件从上到下的顺序，一步步讲。你最终照抄第 6 章的完整文件即可，这里重点是让你**看懂每一行为什么这么写**。

### 5.1 头文件与宏

```c
#include <driver/uart.h>       // 提供 bk_uart_* API 与 uart_*_t 类型
#include <driver/uart_types.h> // 提供 uart_isr_t 等类型
```
原本的 `#include "arm_internal.h"`、`#include <nuttx/irq.h>`、寄存器 `#define` 全部删掉——我们不再碰寄存器。

```c
#define BK7258_UART_RXBUFSIZE   256   // 接收环形缓冲（和原来一样够 NSH 用）
#define BK7258_UART_TXBUFSIZE   256
#define BK7258_CONSOLE_UART_ID  UART_ID_1   // 控制台用 UART1
#define BK7258_UART_BAUD_RATE   460800u     // 波特率沿用 bootloader 的 460800
```

### 5.2 私有结构体（把「地址」换成「id + 缓存」）

```c
struct bk7258_uart_s
{
  uart_id_t id;     /* 告诉 SDK 操作 UART_ID_1 */
  int       rxbyte; /* 接收字节缓存，-1 = 空 */
};
```

**为什么需要 `rxbyte` 这个缓存？** 这是新手最容易卡的点，重点讲：
NuttX 把「收一个字节」拆成了两个函数：
- `rxavailable()` —— 「现在有没有数据？」
- `receive()` —— 「把那个字节给我」

但 SDK 没有「只看一下不拿走」的接口，`bk_uart_read_bytes` 一调用就把字节**消耗**掉了。如果 `rxavailable` 读一次、`receive` 又读一次，同一个字节就被读了两遍（丢数据/重复）。

**解决办法**：`rxavailable` 读出来先塞进 `rxbyte` 缓存，`receive` 只把缓存交出去并清空。这样每次循环只真正读一次硬件。这是 wrapper 模式处理「无 peek 接口」的标准小技巧。

### 5.3 ops 函数表（契约）

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
这一张表和旧代码几乎一样，只是每个函数名我们要换成下面新写的实现。

### 5.4 RX 中断回调（SDK → NuttX 的「翻译官」）

```c
static void bk7258_uart_sdk_isr(uart_id_t id, void *param)
{
  struct uart_dev_s *dev = (struct uart_dev_s *)param;
  uart_recvchars(dev);   // 让 NuttX 上层把字节搬进它的环形缓冲
}
```
SDK 的 RX 回调签名是 `void (*)(uart_id_t, void*)`，而 NuttX 要的是 `uart_recvchars(dev)`。这里就是把 SDK 的回调「翻译」成 NuttX 的收字符动作。**清中断、挂 NVIC 这些脏活 SDK 内部全包了**，你不用管。

### 5.5 `setup`（最小化初始化，别动 bootloader 配好的东西）

```c
static int bk7258_uart_setup(struct uart_dev_s *dev)
{
  static bool s_inited;
  struct bk7258_uart_s *priv = dev->priv;

  if (s_inited)
    {
      return OK;
    }
  s_inited = true;

  /* 1) 拉起 SDK UART 驱动（必须最先调用，类似 WDT 的 bk_wdt_driver_init） */
  if (bk_uart_driver_init() != BK_OK)
    {
      return -EIO;
    }

  /* 2) 显式锁波特率，引脚继续用 bootloader 配好的（不要重新 init 改坏引脚） */
  if (bk_uart_set_baud_rate(priv->id, BK7258_UART_BAUD_RATE) != BK_OK)
    {
      return -EIO;
    }

  /* 3) RX FIFO 满阈值设 1 字节：来一个字节触发一次 RX 中断，
   *    防止「FIFO>=0 就一直中断」的风暴（等价于旧代码阈值=1 的修复）。 */
  bk_uart_set_rx_full_threshold(priv->id, 1);

  priv->rxbyte = -1;
  return OK;
}
```

> **为什么 `setup` 必须调完整的 `bk_uart_init(id, &cfg)`？** 这是 SDK wrapper 最容易踩的坑。
> `bk_uart_driver_init()` 只置位**全局** `s_uart_driver_is_init`，它**不碰硬件**，也**不**
> 置位**每路** `id_init_bits`。而 `bk_uart_write_bytes` / `read_bytes` / `set_baud_rate` /
> `enable_rx_interrupt` / `register_rx_isr` 全部以 `UART_RETURN_ON_ID_NOT_INIT(id)` 开头——
> 不先调 `bk_uart_init`，这些函数会**直接 return 错误，一个字节都不收发**。表现就是：
> 开机早期寄存器轮询输出正常（能看到 bootloader/时钟/`ABWT[ipc_svr]` 那些打印），但 NSH
> 一打开 `/dev/console`、输出改走 SDK 的 `bk_uart_write_bytes`，就全断了——控制台卡死。
>
> 那 `bk_uart_init` 会不会"改写引脚/时钟导致乱码"？只要 `cfg` 与 bootloader 一致
> （这里 `baud_rate=460800` + `src_clk=UART_SCLK_XTAL_26M` + 8N1 + 无 DMA + 无流控），
> 它做的正是 bootloader 已经做的事（使能 26M XTAL 时钟、配 UART1 引脚、按 460800 设分频），
> 等效且不破坏既有配置。所以**必须调**，且 config 要对齐 bootloader。

### 5.6 `attach` / `detach`（注册回调，而非自己挂中断）

```c
static int bk7258_uart_attach(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;
  return (bk_uart_register_rx_isr(priv->id,
                                  bk7258_uart_sdk_isr,
                                  dev) == BK_OK) ? OK : -EIO;
}

static void bk7258_uart_detach(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;
  bk_uart_disable_rx_interrupt(priv->id);
  bk_uart_register_rx_isr(priv->id, NULL, NULL);
}
```
旧代码里 `irq_attach` + `up_enable_irq` 那套「自己挂 NVIC」全删了。wrapper 模式就是**把 RX 回调注册给 SDK**，由 SDK 内部挂到 NVIC 上（再经中断桥投给 NuttX）。

### 5.7 `rxavailable` / `receive`（配合 `rxbyte` 缓存）

```c
static bool bk7258_uart_rxavailable(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (priv->rxbyte < 0)
    {
      uint8_t b;
      int n = bk_uart_read_bytes(priv->id, &b, 1, 0);  /* 0 = 不阻塞 */
      if (n == 1)
        {
          priv->rxbyte = b;   /* 读到了，先缓存 */
        }
      else
        {
          priv->rxbyte = -1;
        }
    }

  return priv->rxbyte >= 0;
}

static int bk7258_uart_receive(struct uart_dev_s *dev, unsigned int *status)
{
  struct bk7258_uart_s *priv = dev->priv;
  int ch = priv->rxbyte;

  priv->rxbyte = -1;   /* 消费掉缓存 */
  if (status)
    {
      *status = 0;
    }

  return ch;
}
```
这就是 5.2 讲的缓存机制：`rxavailable` 把字节读进 `rxbyte` 并返回「有」；`receive` 只把缓存交出去、清空。NuttX 的 `uart_recvchars` 循环每次迭代调用 `rxavailable` 和 `receive` 各一次，恰好消费 1 字节，不会重复读。

### 5.8 `rxint`（开/关 RX 中断，一行搞定）

```c
static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (enable)
    {
      bk_uart_enable_rx_interrupt(priv->id);
    }
  else
    {
      bk_uart_disable_rx_interrupt(priv->id);
    }
}
```
旧代码里手动开「三道中断门」的那一长串（外设使能 → 片上中断控制器 → NVIC）全部消失，替换成一行 SDK 调用。三道门 SDK 内部自己打理。

### 5.9 `send` / `txint` / `txready` / `txempty`（轮询 TX，最简单最稳）

```c
static void bk7258_uart_send(struct uart_dev_s *dev, int ch)
{
  struct bk7258_uart_s *priv = dev->priv;
  uint8_t b = (uint8_t)ch;
  bk_uart_write_bytes(priv->id, &b, 1);   /* 内部等 TX FIFO 有空位再写 */
}

static void bk7258_uart_txint(struct uart_dev_s *dev, bool enable)
{
  if (enable)
    {
      uart_xmitchars(dev);   /* 伪 TX 中断：启用发送时同步把环形缓冲刷出去 */
    }
}

static bool bk7258_uart_txready(struct uart_dev_s *dev)
{
  return true;   /* 当作「永远可以写入」，真正的等待发生在 write_bytes 内部 */
}

static bool bk7258_uart_txempty(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;
  return bk_uart_is_tx_over(priv->id);   /* TX FIFO 真正发空了才返回 true */
}
```
TX 保持「轮询」不上升级版 TX 中断，原因：原厂参考代码的正规 TX 中断需要 `bk_uart_is_tx_ready`，而**这个函数 bk7258 SDK 没有**，强上会卡。控制台交互场景轮询完全够用，风险最低。

### 5.10 公开函数 `arm_earlyserialinit` / `arm_serialinit`

```c
#ifdef USE_EARLYSERIALINIT
void arm_earlyserialinit(void)
{
  /* 仅标记控制台；沉重的 SDK 初始化推迟到第一次打开 /dev/console（setup 里），
     避免在 OS 还没起来时就调用 SDK 导致崩溃。 */
  CONSOLE_DEV.isconsole = true;
}
#endif

#ifdef USE_SERIALDRIVER
void arm_serialinit(void)
{
  (void)uart_register("/dev/console", &CONSOLE_DEV);
}
#endif
```
> 注意：这里**故意不调用 `bk7258_uart_setup()`**。系统刚启动时 OS 还没起来、SDK 还没初始化，这时早起的打印（启动横幅）靠 `bk7258_lowputc.c` 里「直接写寄存器」的 `arm_lowputc` 应急。等 OS 起来、`/dev/console` 第一次打开时，`setup()` 才会拉起 SDK 接管 UART。两套共存、各管一段，互不冲突。

### 5.11 一个你「绝对不要动」的文件

`bk7258_lowputc.c`（里面是 `arm_lowputc` / `up_putc`）**保持原样**。原因就是上面说的：早起打印只能靠直接写寄存器应急，SDK 接管是后面的事。

---

## 第 6 章：完整文件（可直接照抄）

把下面整份内容**替换** `bk7258_serial.c` 的全部内容即可：

```c
/****************************************************************************
 * contest2026_135_yongwangzhiqian/chips/bk7258/common/bk7258_serial.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * BK7258 (T5-AI, CP 核) UART1 串口下层 —— SDK WRAPPER 模式。
 * 本文件零寄存器访问，所有硬件操作转发给 bk_uart_* SDK API。
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include <nuttx/fs/fs.h>
#include <nuttx/serial/serial.h>

/* SDK UART 头文件：提供 bk_uart_* API 与 uart_*_t 类型 */
#include <driver/uart.h>
#include <driver/uart_types.h>

/* RX/TX 环形缓冲大小（256 字节够 NSH 控制台用） */
#define BK7258_UART_RXBUFSIZE   256
#define BK7258_UART_TXBUFSIZE   256

/* 控制台用 UART1（旧代码里的 0x45830000） */
#define BK7258_CONSOLE_UART_ID  UART_ID_1

/* 波特率沿用 bootloader 的 460800 */
#define BK7258_UART_BAUD_RATE   460800u

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_uart_s
{
  uart_id_t id;     /* 告诉 SDK 操作哪路 UART */
  int       rxbyte; /* 接收字节缓存，-1 = 空 */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_uart_s g_bk7258_uart1priv =
{
  .id     = BK7258_CONSOLE_UART_ID,
  .rxbyte = -1,
};

static char g_uart1rxbuffer[BK7258_UART_RXBUFSIZE];
static char g_uart1txbuffer[BK7258_UART_TXBUFSIZE];

/* 前向声明：SDK 的 RX 中断回调，attach 里会注册它 */
static void bk7258_uart_sdk_isr(uart_id_t id, void *param);

static const struct uart_ops_s g_bk7258_uart_ops;

static struct uart_dev_s g_uart1port =
{
  .isconsole = false,
  .ops       = &g_bk7258_uart_ops,
  .priv      = &g_bk7258_uart1priv,
  .recv = { .size = BK7258_UART_RXBUFSIZE, .buffer = g_uart1rxbuffer, },
  .xmit = { .size = BK7258_UART_TXBUFSIZE, .buffer = g_uart1txbuffer, },
};

#define CONSOLE_DEV  g_uart1port

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  bk7258_uart_setup(struct uart_dev_s *dev);
static void bk7258_uart_shutdown(struct uart_dev_s *dev);
static int  bk7258_uart_attach(struct uart_dev_s *dev);
static void bk7258_uart_detach(struct uart_dev_s *dev);
static int  bk7258_uart_ioctl(struct file *filep, int cmd, unsigned long arg);
static int  bk7258_uart_receive(struct uart_dev_s *dev, unsigned int *status);
static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable);
static bool bk7258_uart_rxavailable(struct uart_dev_s *dev);
static void bk7258_uart_send(struct uart_dev_s *dev, int ch);
static void bk7258_uart_txint(struct uart_dev_s *dev, bool enable);
static bool bk7258_uart_txready(struct uart_dev_s *dev);
static bool bk7258_uart_txempty(struct uart_dev_s *dev);

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

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_uart_sdk_isr
 *
 * 适配层：SDK 的 RX 回调签名是 void (*)(uart_id_t, void*)，
 * 而 NuttX 上层要的是 uart_recvchars(dev)。这里把 SDK 的回调
 * "翻译"成 NuttX 的收字符动作。SDK 自己负责清中断、处理 NVIC。
 ****************************************************************************/

static void bk7258_uart_sdk_isr(uart_id_t id, void *param)
{
  struct uart_dev_s *dev = (struct uart_dev_s *)param;

  uart_recvchars(dev);   /* 让 NuttX 上层把 FIFO 里的字节搬进它的环形缓冲 */
}

/****************************************************************************
 * Name: bk7258_uart_setup
 *
 * 第一次打开 /dev/console 时 NuttX 上层会调用它。
 * 只做"初始化一次"：拉起 SDK UART 驱动 + 显式锁波特率 + 设 RX 阈值。
 * 不调完整 bk_uart_init，避免改坏 bootloader 配好的引脚/时钟。
 ****************************************************************************/

static int bk7258_uart_setup(struct uart_dev_s *dev)
{
  static bool s_inited;
  struct bk7258_uart_s *priv = dev->priv;

  if (s_inited)
    {
      return OK;
    }
  s_inited = true;

  /* 1) 拉起 SDK UART 驱动（置位全局 s_uart_driver_is_init，必须最先调用） */
  if (bk_uart_driver_init() != BK_OK)
    {
      return -EIO;
    }

  /* 2) ★关键★ 初始化 UART1 这一路（置位每路 id_init_bits）。
   *    所有 bk_uart_write_bytes / read_bytes / set_baud_rate / enable_rx_interrupt
   *    / register_rx_isr 都以 UART_RETURN_ON_ID_NOT_INIT(id) 开头，不先调
   *    bk_uart_init(id, &cfg) 它们会直接 return 错误——一个字节都不收发，
   *    控制台就死在 NSH 接管那一刻。config 与 bootloader 保持一致，不改引脚/时钟。 */
  uart_config_t cfg =
  {
    .baud_rate  = BK7258_UART_BAUD_RATE,
    .data_bits  = UART_DATA_8_BITS,
    .parity     = UART_PARITY_NONE,
    .stop_bits  = UART_STOP_BITS_1,
    .flow_ctrl  = UART_FLOWCTRL_DISABLE,
    .src_clk    = UART_SCLK_XTAL_26M,
    .rx_dma_en  = UART_DMA_DISABLE,
    .tx_dma_en  = UART_DMA_DISABLE,
  };

  if (bk_uart_init(priv->id, &cfg) != BK_OK)
    {
      return -EIO;
    }

  /* 3) RX FIFO 满阈值设 1 字节：来一个字节触发一次 RX 中断，防中断风暴 */
  bk_uart_set_rx_full_threshold(priv->id, 1);

  priv->rxbyte = -1;
  return OK;
}

static void bk7258_uart_shutdown(struct uart_dev_s *dev)
{
  /* 控制台常开，无需关闭。 */
}

/****************************************************************************
 * Name: bk7258_uart_attach / detach
 *
 * wrapper 模式不再自己 irq_attach + up_enable_irq，而是把 RX 回调
 * "注册"给 SDK，由 SDK 内部挂到 NVIC 上（再经中断桥投给 NuttX）。
 ****************************************************************************/

static int bk7258_uart_attach(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  return (bk_uart_register_rx_isr(priv->id,
                                  bk7258_uart_sdk_isr,
                                  dev) == BK_OK) ? OK : -EIO;
}

static void bk7258_uart_detach(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  bk_uart_disable_rx_interrupt(priv->id);
  bk_uart_register_rx_isr(priv->id, NULL, NULL);
}

static int bk7258_uart_ioctl(struct file *filep, int cmd, unsigned long arg)
{
  return -ENOTTY;
}

/****************************************************************************
 * Name: bk7258_uart_rxavailable / receive
 *
 * 配合完成"从 SDK 收一个字节"：
 *   rxavailable —— 缓存空时非阻塞读 1 字节(timeout=0)，读到就缓存并返回 true
 *   receive    —— 把缓存的字节交给 NuttX 上层并清空
 * 这样 uart_recvchars 循环每次迭代恰好消费 1 字节，不会重复读。
 ****************************************************************************/

static bool bk7258_uart_rxavailable(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (priv->rxbyte < 0)
    {
      uint8_t b;
      int n = bk_uart_read_bytes(priv->id, &b, 1, 0);  /* 0 = 不阻塞 */
      if (n == 1)
        {
          priv->rxbyte = b;
        }
      else
        {
          priv->rxbyte = -1;
        }
    }

  return priv->rxbyte >= 0;
}

static int bk7258_uart_receive(struct uart_dev_s *dev, unsigned int *status)
{
  struct bk7258_uart_s *priv = dev->priv;
  int ch = priv->rxbyte;

  priv->rxbyte = -1;   /* 消费掉缓存 */
  if (status)
    {
      *status = 0;
    }

  return ch;
}

/****************************************************************************
 * Name: bk7258_uart_rxint
 *
 * 开/关 RX 中断：wrapper 模式就是一行 bk_uart_enable/disable_rx_interrupt。
 * 三道中断门（外设→片上中断控制器→NVIC）全部由 SDK 内部打理。
 ****************************************************************************/

static void bk7258_uart_rxint(struct uart_dev_s *dev, bool enable)
{
  struct bk7258_uart_s *priv = dev->priv;

  if (enable)
    {
      bk_uart_enable_rx_interrupt(priv->id);
    }
  else
    {
      bk_uart_disable_rx_interrupt(priv->id);
    }
}

static void bk7258_uart_send(struct uart_dev_s *dev, int ch)
{
  struct bk7258_uart_s *priv = dev->priv;
  uint8_t b = (uint8_t)ch;

  /* 阻塞写一个字节：SDK 内部等 TX FIFO 有空位再写。 */
  bk_uart_write_bytes(priv->id, &b, 1);
}

static void bk7258_uart_txint(struct uart_dev_s *dev, bool enable)
{
  /* 没有 TX 中断：启用发送时同步把环形缓冲刷出去（伪 TX 中断）。 */
  if (enable)
    {
      uart_xmitchars(dev);
    }
}

static bool bk7258_uart_txready(struct uart_dev_s *dev)
{
  /* 当作"永远可以写入"：真正的等待发生在 bk_uart_write_bytes 内部。 */
  return true;
}

static bool bk7258_uart_txempty(struct uart_dev_s *dev)
{
  struct bk7258_uart_s *priv = dev->priv;

  /* TX FIFO 真正发空了才返回 true。 */
  return bk_uart_is_tx_over(priv->id);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef USE_EARLYSERIALINIT
void arm_earlyserialinit(void)
{
  /* 仅标记控制台；重量级 SDK 初始化推迟到第一次打开 /dev/console
   * （setup 里），避免在 OS 还没起来时就调用 SDK。 */
  CONSOLE_DEV.isconsole = true;
}
#endif

#ifdef USE_SERIALDRIVER
void arm_serialinit(void)
{
  (void)uart_register("/dev/console", &CONSOLE_DEV);
}
#endif
```

> ⚠️ **用上面这套 SDK wrapper 前，必须先做第 8 章**：代码里调了
> `bk_uart_driver_init()`，会把 `uart_driver.c.obj` 拉进链接，而预编译的
> `libdriver.a` 里这个函数引用了 `bk_printf_init`。不先按第 8 章重编去桩，
> 最后 `make` 会报 `undefined reference to bk_printf_init`。纯寄存器版
> （不调 `bk_uart_*`）不用管这章。

---

## 第 7 章：三个坑与排查锦囊

改完编译烧录后，如果出问题，按下面顺序查：

**坑 1：串口完全没输出 / 控制台卡在 NSH 接管前**
典型现象：bootloader、时钟、`ABWT[ipc_svr]` 那些早期打印都正常，但 NSH 提示符不出来、键盘没反应。
→ 99% 是 `setup` 漏了 `bk_uart_init(id, &cfg)`：所有 `bk_uart_*` 调用被
`UART_RETURN_ON_ID_NOT_INIT(id)` 挡掉，一个字节都不收发。按第 5.5 章补上 `bk_uart_init`。
→ 如果补上后反而**更早**就卡死（连早期打印都少了），多半是 `bk_uart_init` 内部
`uart_init_gpio` / `rtos_init_semaphore` 在这个 NuttX/CP 移植里不工作。此时 SDK wrapper
走不通，退回纯寄存器版 `bk7258_serial.c`（不调任何 `bk_uart_*`）最稳，早期寄存器输出与
SDK init 互不影响。

**坑 2：一按键盘系统卡死**
多半是 `bk_uart_read_bytes(..., 0)` 的 `timeout=0` 在某些 SDK 版本里被当成「一直等」，在中断里就死等了。
→ 排查：进 SDK 源码看 `bk_uart_read_bytes` 对 `timeout_ms=0` 的处理，确认它是非阻塞立即返回；必要时改成极小超时（但 ISR 里不能 sleep，所以优先确认 0 是立即返回）。

**坑 3：`setup` 返回 `-EIO`**
说明 `bk_uart_driver_init` 或 `bk_uart_init` 失败了（比如 SDK 认为该 UART 已被占用）。
→ 可在失败调用前加一句 `bk_uart_deinit(priv->id);` 释放占用再初始化；或检查 `cfg` 字段
（波特率是否被 `UART_RETURN_ON_BAUD_RATE_NOT_SUPPORT` 拒绝、枚举值是否拼写正确）。

---

## 第 8 章：去掉 `bk_printf_init` 桩——重编 `libdriver.a`（SDK wrapper 必做）

> 如果你**只**用第 6 章那套 SDK wrapper（代码里调了 `bk_uart_driver_init` 等
> `bk_uart_*` 函数），本章**必须做**，否则最后链接会报
> `undefined reference to bk_printf_init`。
> 如果你走的是纯寄存器版（不调任何 `bk_uart_*`），`uart_driver.c.obj` 根本不会被
> 拉进链接，这一章可以跳过。

### 8.1 为什么会出现这个坑

预编译的 `libdriver.a` 里，`bk_uart_driver_init()` 这个函数体内**无条件调用了
`bk_printf_init()`**（这是 armino 当初编译这个 `.a` 时写死在里面的指令）。

而本工程 `scripts/Make.defs` 把**真正提供 `bk_printf_init` 的 `libbk_system.a` 排除
在链接之外**（这是本工程的既定做法，不能改）。于是：

- 你的 wrapper 一调 `bk_uart_driver_init()` → 链接器把 `uart_driver.c.obj` 拉进来
  → 连带必须解析 `bk_printf_init` → 找不到 → 报 undefined reference。
- 之前我们用 `bk7258_sdk_stubs.c` 里一个**空桩** `int bk_printf_init(void){return 0;}`
  来糊弄过去。能用，但丑，而且那个空函数其实是死代码。

**根治办法**：重新编译 `libdriver.a` 的那个 `uart_driver.c` 成员，编译时带上
`CONFIG_BK_PRINTF_DISABLE` 宏。`bk_printf_init()` 的调用外面正好有这个宏保护：

```c
#ifndef CONFIG_BK_PRINTF_DISABLE
    bk_printf_init();   // 开了宏，这行直接被编译掉
#endif
```

这样从根上消除未定义引用，**桩就可以彻底删掉**。而且我们只是删掉一段死调用，
没改任何函数签名/结构体，所以新编出来的 `uart_driver.c.obj` 跟原来的
**ABI 100% 兼容**，风险极低。

> 关键点：`CONFIG_BK_PRINTF_DISABLE` 在 armino 源码里**只有 `uart_driver.c` 用到**，
> 且它**不在任何 Kconfig 里定义**——纯粹是个编译期宏。所以只要重编这个文件时
> 传进去就行，不用动整个 SDK 配置。

### 8.2 当前重建命令

不要手工修改已安装的 `libdriver.a`，也不要从个人路径复制构建残留。统一入口会在
临时工作区重建 SDK、从唯一的 UART compile-database 条目加入宏、替换 archive
成员，并在安装前后验证 `bk_printf_init` 已消失：

```bash
cd <openvela-workspace>

./contest2026_135_yongwangzhiqian/tools/bk7258/bk7258.py \
  sdk rebuild --role cp --replace
```

默认 SDK 源码路径和版本来自 team manifest；工具链默认从 `PATH` 查找。只有有界
迁移检查才显式传 `--source` 或 `--toolchain-dir`。

### 8.3 验证

```bash
./contest2026_135_yongwangzhiqian/tools/bk7258/bk7258.py sdk verify --role cp
```

验证失败是阻断条件，不能继续使用 manifest/provenance 已失配的 SDK bundle。

### 8.4 回滚 / 注意事项

- bundle、manifest 和 provenance 在同一文件锁下替换；任何安装或安装后验证失败都会
  恢复旧三件套。
- `CONFIG_BK_PRINTF_DISABLE` 的 UART 对象和最终 `libdriver.a` 哈希都写入 provenance。
- `apctl_main` / `bkirqtest_main` 与 UART 无关，不应混入 SDK bundle 重建。

---

## 第 9 章：编译验证步骤（给小白）

1. 用你平时的编译命令重新编译、烧录（CP 镜像）。
2. 上电后，串口工具应该能看到启动横幅（这阶段还是 `bk7258_lowputc.c` 在干活）。
3. 进入 NSH 提示符后，敲键盘——如果能正常回显、能执行 `help` 等命令，说明 RX 中断链路（SDK → 中断桥 → NuttX）通了。
4. 如果卡在坑里，把**编译报错**或**运行现象**发给我，我陪你一个个坑填掉。

---

## 附：本教程的关键结论清单（背下来）

1. ✅ wrapper 模式 = 把 `uart_ops_s` 每个函数从「写寄存器」改成「调 `bk_uart_*`」。
2. ✅ NuttX 跑在 **CP 核**，UART1 在 CP 直连，用 CP 的 SDK 自洽。
3. ✅ `CONFIG_BK7258_SDK_IRQ_BRIDGE=y` 必须开着，否则 RX 中断进不了 NuttX。
4. ✅ `setup` 必须 `bk_uart_driver_init()` **+** `bk_uart_init(id, &cfg)`（cfg 与 bootloader 一致：460800 / XTAL 26M / 8N1 / 无 DMA），再 `set_rx_full_threshold(1)`。少了 `bk_uart_init`，所有 `bk_uart_*` 调用都会因 `UART_RETURN_ON_ID_NOT_INIT` 静默失败，控制台死在接管那一刻。
5. ✅ `rxavailable`/`receive` 用 `rxbyte` 缓存解决「SDK 无 peek 接口」问题。
6. ✅ TX 保持轮询，不上需要 `bk_uart_is_tx_ready`（bk7258 没有）的 TX 中断。
7. ✅ `bk7258_lowputc.c` 保持原样不动，早起打印靠它。
