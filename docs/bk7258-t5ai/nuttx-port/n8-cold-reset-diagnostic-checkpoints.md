# BK7258 N8 cold-reset — 可恢复诊断路标代码

日期：2026-07-30

状态：`applied / not-built / board-validation-pending`

关联调研：[`n8-cold-reset-nsh-hang-investigation.md`](n8-cold-reset-nsh-hang-investigation.md)

> 本文归档并跟踪 2026-07-30 raw UART checkpoint。它不是根因修复，只用于一次性定位静默发生在 UART、NSH board init、AP lifecycle、第一次 timeout sleep 还是 flash/LittleFS。当前 checkpoint 已恢复到三个 team-overlay 源文件；尚未编译、下载或执行板端验证，也未执行静态验证。

## 1. 适用基线与边界

预期基线为 N8-C1 完成后的 team overlay，核心提交：

```text
90581d9 feat(bk7258): bring AP SMP scheduler online
```

涉及且仅涉及三个文件：

```text
board/bk7258/chip/common/bk7258_serial.c
board/bk7258/chip/cp/bk7258_ap_control.c
board/bk7258/src/bk7258_bringup.c
```

约束：

- 诊断部分只增加 `up_putc()` raw UART 标签；
- Phase A 定位后唯一功能变化是在 UART setup 前调用 SDK 幂等的 `bk_gpio_driver_init()`，建立 GPIO HAL/pinmux state；
- 不改变 timeout；
- 不改变 CPU1/CPU2 reset/power 顺序；
- 不改变 mailbox 或 SMP ABI；
- 不改变 WDT、GPIO lower-half 设备语义、procfs、flash 或 LittleFS 行为；
- 定位完成后必须删除全部 `cold_ckpt()` helper 和调用，但保留 GPIO-before-UART 初始化顺序。

## 2. 公共 helper

三个 translation unit 各自使用一个 `static` helper。插入到各文件 private data/include 后、首次调用之前：

```c
/* TEMP cold-reset diagnostic: emit a short raw-UART tag so the last
 * completed startup checkpoint remains visible without /dev/console,
 * printf or syslog.  Delete after diagnosis.
 */

static void cold_ckpt(const char *tag)
{
  up_putc('\r');
  up_putc('\n');
  while (*tag)
    {
      up_putc(*tag++);
    }

  up_putc('\r');
  up_putc('\n');
}
```

`bk7258_serial.c` 已通过 `arm_internal.h` 取得 `up_putc()` 声明，并因 GPIO-before-UART 顺序修复增加 `<driver/gpio.h>`；`bk7258_ap_control.c` 已包含 `<nuttx/arch.h>`；当前基线的 `bk7258_bringup.c` 原先未显式包含该头文件，因此恢复路标时补充了 `<nuttx/arch.h>`。实现没有增加格式化或日志依赖。

## 3. `bk7258_serial.c` 路标

文件：

```text
board/bk7258/chip/common/bk7258_serial.c
```

### 3.1 helper 插入点

放在：

```c
#define CONSOLE_DEV  g_uart1port
```

之后。

### 3.2 临时版 `bk7258_uart_setup()`

用下列函数体替换基线函数；UART config initializer 保持原值：

```c
static int bk7258_uart_setup(struct uart_dev_s *dev)
{
  static bool initialized;
  struct bk7258_uart_s *priv = dev->priv;
  const uart_config_t config =
    {
      .baud_rate = BK7258_UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_NONE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_FLOWCTRL_DISABLE,
      .src_clk = UART_SCLK_XTAL_26M,
      .rx_dma_en = UART_DMA_DISABLE,
      .tx_dma_en = UART_DMA_DISABLE,
    };

  cold_ckpt("U0"); /* UART setup entry */

  if (initialized)
    {
      cold_ckpt("U4"); /* Already initialized */
      return OK;
    }

  if (bk_gpio_driver_init() != BK_OK)
    {
      cold_ckpt("EG"); /* GPIO driver init failed */
      return -EIO;
    }

  cold_ckpt("G1"); /* GPIO HAL/pinmux state initialized */

  if (bk_uart_driver_init() != BK_OK)
    {
      cold_ckpt("E1"); /* Driver init failed */
      return -EIO;
    }

  cold_ckpt("U1"); /* Driver init returned */

  if (bk_uart_init(priv->id, &config) != BK_OK)
    {
      cold_ckpt("E2"); /* UART init failed */
      return -EIO;
    }

  cold_ckpt("U2"); /* UART init returned */

  /* NuttX owns the receive ring.  Keep bytes in the hardware FIFO so the SDK
   * callback can hand them directly to uart_recvchars().
   */

  if (bk_uart_disable_sw_fifo(priv->id) != BK_OK)
    {
      cold_ckpt("E3"); /* FIFO disable failed */
      return -EIO;
    }

  cold_ckpt("U3"); /* FIFO disabled */

  if (bk_uart_set_rx_full_threshold(priv->id, 1) != BK_OK)
    {
      cold_ckpt("E4"); /* RX threshold failed */
      return -EIO;
    }

  priv->rxbyte = -1;
  initialized = true;
  cold_ckpt("U4"); /* UART setup done */
  return OK;
}
```

### 3.3 临时版 `arm_serialinit()`

```c
#ifdef USE_SERIALDRIVER
void arm_serialinit(void)
{
  cold_ckpt("S0"); /* arm_serialinit entry */
  CONSOLE_DEV.isconsole = true;

  if (bk7258_uart_setup(&CONSOLE_DEV) < 0)
    {
      cold_ckpt("SE"); /* UART setup rejected */
      return;
    }

  (void)uart_register("/dev/console", &CONSOLE_DEV);
  cold_ckpt("U5"); /* /dev/console registered */
}
#endif
```

## 4. `bk7258_ap_control.c` 路标

文件：

```text
board/bk7258/chip/cp/bk7258_ap_control.c
```

### 4.1 helper 插入点

放在：

```c
#include <arch/chip/bk7258_amp.h>
```

之后、external function prototypes 之前。

### 4.2 第一次 wait sleep 路标

在 `bk7258_ap_wait()` local variables 中增加：

```c
bool first_iter = true;
```

将原来的：

```c
nxsig_usleep(1000);
```

临时替换为：

```c
if (first_iter)
  {
    cold_ckpt("W0"); /* Before first 1 ms sleep */
  }

nxsig_usleep(1000);
if (first_iter)
  {
    cold_ckpt("W1"); /* First 1 ms sleep returned */
    first_iter = false;
  }
```

只在第一次循环打印，避免 3000 次 poll 污染 UART 和时序。

### 4.3 临时版 `bk7258_ap_start_locked()`

```c
static int bk7258_ap_start_locked(uint32_t timeout_ms)
{
  volatile struct bk7258_ap_boot_state_s *state = bk7258_ap_boot_state();
  int ret;

  cold_ckpt("A0"); /* Enter start_locked */

  if (state->magic == BK7258_AP_BOOT_STATE_MAGIC &&
      (state->state == BK7258_AP_STATE_READY ||
       state->state == BK7258_AP_STATE_STARTING))
    {
      return -EBUSY;
    }

  /* Hold CPU1 while replacing shared state and the boot address. */

  sys_drv_set_cpu1_reset(0);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  cold_ckpt("A1"); /* CPU1 reset hold done */

  bk7258_cpu2_force_reset();
  up_mdelay(BK7258_AP_RESTART_DELAY_MS);
  cold_ckpt("A2"); /* CPU2 force-reset + delay done */

  bk7258_ap_mbox_initialize();
  bk7258_ap_state_prepare();
  cold_ckpt("A3"); /* Mailbox/shared state prepared */

  /* Exact BK7258 CPU1 release order from the CP SDK. */

  sys_drv_set_cpu1_pwr_dw(0);
  sys_drv_set_cpu1_rxevt_sel(1);
  sys_drv_set_cpu1_boot_address_offset(BK7258_AP_FLASH_ADDR >> 8);
  __asm volatile ("dsb sy; isb sy" ::: "memory");
  cold_ckpt("A4"); /* CPU1 power/RXEVT/boot address done */

  sys_drv_set_cpu1_reset(1);
  __asm volatile ("dsb sy; sev" ::: "memory");
  cold_ckpt("A5"); /* CPU1 reset released */
  cold_ckpt("A6"); /* Entering AP READY wait */

  ret = bk7258_ap_wait(BK7258_AP_STATE_READY, timeout_ms);
  cold_ckpt("A7"); /* AP wait returned */
  if (ret < 0)
    {
      sys_drv_set_cpu1_reset(0);
      __asm volatile ("dsb sy; isb sy" ::: "memory");
      bk7258_cpu2_force_reset();
      up_mdelay(BK7258_AP_RESTART_DELAY_MS);
      cold_ckpt("F1"); /* Failure cleanup: AP cores held reset */

      sys_drv_set_cpu1_pwr_dw(1);
      cold_ckpt("F2"); /* Failure cleanup: CPU1 power-down done */

      if (state->state != BK7258_AP_STATE_FAILED)
        {
          state->error = BK7258_AP_ERROR_TIMEOUT;
        }

      state->command = BK7258_AP_COMMAND_NONE;
      state->state = BK7258_AP_STATE_FAILED;
      state->last_event = BK7258_AP_EVENT_FAILED;
      __asm volatile ("dmb sy" ::: "memory");
    }

  return ret;
}
```

## 5. `bk7258_bringup.c` 路标

文件：

```text
board/bk7258/src/bk7258_bringup.c
```

### 5.1 helper 插入点

放在：

```c
#endif /* CONFIG_BK7258_FLASH_LITTLEFS */
```

之后、Public Functions section 之前。

### 5.2 临时版 `board_app_initialize()`

```c
int board_app_initialize(uintptr_t arg)
{
  cold_ckpt("C0"); /* board_app_initialize entry */

#ifdef CONFIG_BK7258_WDT
  /* AP autostart may wait up to three seconds.  Stop the bootloader AON WDT
   * before entering that wait or it resets the entire SoC before AP failure
   * diagnostics can be consumed.
   */

  (void)bk7258_wdt_initialize();
#endif
  cold_ckpt("C1"); /* WDT init done */

#ifdef CONFIG_BK7258_AP_CONTROL
  int apret;

  apret = bk7258_ap_control_initialize();
  cold_ckpt("C2"); /* AP control init returned */
  if (apret < 0)
    {
      _err("bk7258: AP control init failed: %d\n", apret);
    }
#ifdef CONFIG_BK7258_AP_AUTOSTART
  else
    {
      cold_ckpt("C3"); /* AP autostart begins */
      apret = bk7258_ap_start(BK7258_AP_DEFAULT_TIMEOUT_MS);
      if (apret < 0)
        {
          _err("bk7258: AP autostart failed: %d\n", apret);
        }

      cold_ckpt("C4"); /* AP autostart returned */
    }
#endif
#endif

#ifdef CONFIG_BK7258_GPIO_LOWERHALF
  (void)bk7258_gpio_lowerhalf_initialize();
#endif
  cold_ckpt("C5"); /* GPIO lower-half done */

  /* Register the BK7258 DVFS /proc/dvfs entry before mounting procfs. */

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_BK7258_DVFS_PROCFS)
  (void)bk7258_dvfs_procfs_register();
#endif

#if defined(CONFIG_FS_PROCFS) && defined(CONFIG_NSH_PROC_MOUNTPOINT)
  (void)mount(NULL, CONFIG_NSH_PROC_MOUNTPOINT, "procfs", 0, NULL);
#endif
  cold_ckpt("C6"); /* procfs registration/mount done */

#ifdef CONFIG_BK7258_FLASH_MTD
  FAR struct mtd_dev_s *mtd = bk7258_flash_mtd_initialize();
  if (mtd != NULL)
    {
#ifdef CONFIG_BK7258_FLASH_LITTLEFS
      bk7258_fs_probe(mtd);
#endif
    }
#endif
  cold_ckpt("C7"); /* flash MTD / LittleFS done */
  cold_ckpt("C8"); /* board_app_initialize return */

  return 0;
}
```

恢复时也可以不替换整个函数，只在基线函数的对应调用前后加入表中的 `cold_ckpt()`；不得删除基线已有注释或改变原调用顺序。

## 6. 路标判读

| 最后路标 | 解释 | 下一步 |
|---|---|---|
| 无 `S0` | 静默发生在更早的 CP startup、clock/DVFS、IRQ/clock init | 记录 M1/A5/A9 和 SysTick 初始化入口 |
| `S0` / `U0` | 已进入 SDK UART setup | 看 `G1/EG/U1/U2/E1/E2` |
| `EG` 或无 `G1` | GPIO driver/HAL 初始化失败或停住 | 检查 GPIO driver 初始化及 IRQ bridge |
| `G1/U1` 后出现 GPIO busy，随后无 `U2` | 已进入 `bk_uart_init()` 的 pinmux 路径 | 检查 GPIO peripheral-mode table、UART clock/baud 或函数是否返回 |
| `E1..E4` / `SE` | UART setup 明确返回错误，console 未注册 | 保留错误码，做 idempotent UART/pinmux 修复 |
| `U5` 后无 `C0` | console 已注册，尚未进入 NSH board init | 检查 init task 创建、stdio inheritance、`nsh_initialize()` |
| `C0` 后无 `C1` | WDT initialization 内停住 | 检查 timer/WDT SDK handoff |
| `C3` / `A0..A6` | AP cold lifecycle 某一步未完成 | 按最后 `A*` 精确修 CPU1/CPU2/mailbox 顺序 |
| `W0` 后无 `W1` | 第一次 `nxsig_usleep()` 不返回 | 查 cold SysTick/clock/timeout path |
| `A7` / `C4` | AP wait 已返回 | AP 不是 NSH blocker，继续 GPIO/procfs/flash |
| `C6` 后无 `C7` | flash MTD 或 LittleFS 卡住 | 分层 A/B MTD、FTL、mount 和 probe |
| `C8` 后无 banner | board init 已返回 | 检查 NSH console session/stdio，而不是 board bringup |

## 7. 恢复和清理规则

### 恢复诊断代码

1. 只编辑本文列出的三个 team-overlay 文件；
2. 每个文件添加自己的 `static cold_ckpt()`；
3. 按本文代码恢复 U/S、A/W/F、C 路标；
4. 不顺手修改 timeout、寄存器顺序、Kconfig 或错误处理；
5. 将恢复动作视为临时诊断，不作为产品修复提交。

### 清理诊断代码

定位完成后删除：

- 三个 `cold_ckpt()` helper；
- `bool first_iter`；
- 所有 `S*`、`U*`、`G*`、`E*`、`C*`、`A*`、`W*`、`F*` 路标调用；

并恢复基线中的单条：

```c
nxsig_usleep(1000);
```

不要删除真实功能代码，包括：

- WDT-before-AP 顺序；
- CPU2 force-reset/lifecycle；
- mailbox/shared-state 初始化；
- AP timeout/failure cleanup；
- UART SDK wrapper；
- `bk_gpio_driver_init()` 先于 `bk_uart_init()` 的 cold-safe 初始化顺序及其 `<driver/gpio.h>` 依赖；
- flash/LittleFS probe。

## 8. 与真正修复的关系

本文只保存定位代码。真正修复必须根据最后路标选择：

- UART idempotent setup；
- CP→AP deterministic cold lifecycle；
- early-safe delay/SysTick；
- flash busy timeout 或挂载延后。

在没有 physical RESET 路标前，不应把本文的任何 checkpoint 或候选改法当作根因修复。
