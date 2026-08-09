# Stage N6-A — SDK 集成调研：从寄存器级重写改为调用 Beken SDK API

> **范围**：调研 BK7258 NuttX 适配的当前方式（寄存器级从零重写）与参考实现（7236N `vendor_beken`
> wrapper + 预编译 SDK 库）的差异，论证改为「引入 SDK、调用 `bk_*` API」的必要性，给出 flash 模块
> 的具体引入方案与目录组织，并对比两种引入路径（拷源码 vs `armino_as_lib` 打包）。
> **状态**：调研完成，方案待用户确认后进入实现。
> **结论**：当前方式有问题——底层不应从零重写，应引入 SDK 调用其接口；flash 模块先采用
> **方案 1（从 bk_avdk_smp 拷 flash 源码进 `board/bk7258/sdk/`）**，后续 UART/DVFS 照此扩大。

> **路径约定**：本文所有外部 SDK 路径用占位符表示，避免暴露本机绝对路径——
> - `$CONTEST` = 本团队 overlay 根（即含 `board/bk7258/` 的 contest 仓）
> - `$BK_AVDK` = Beken 官方 SDK（bk_avdk_smp，开源，含 `cp/middleware/driver/` 与 `soc/bk7258/`）
> - `$VENDOR_BEKEN` = 7236N 参考实现（vendor_beken 仓，含 `chips/bk7236n/`）
> - `$TUYAOPEN` = TuyaOpen SDK（含 `platform/T5AI/tuyaos/tuyaos_adapter/`）
>
> 本 overlay 内部路径用相对于 `$CONTEST` 的相对路径（如 `board/bk7258/chip/`）。

---

## 1. 背景与问题陈述

### 1.1 当前适配方式（寄存器级从零重写）

BK7258（T5-AI 模组）的 NuttX 适配目前所有底层驱动都由团队**从寄存器级手工重写**，位于
`contest2026_135_yongwangzhiqian/board/bk7258/chip/`：

| 文件 | 模块 | 实现方式 | 已踩坑（板端实证） |
|---|---|---|---|
| `bk7258_serial.c` | UART | 直接操作 `0x4583xxxx` 寄存器 | RX 中断 4 个叠加 bug：FIFO bit[8:15] 取位、`rx_enable` 位、三道中断门、RX FIFO 阈值默认 0→ISR storm |
| `bk7258_flash_mtd.c` | Flash/MTD | 直接操作 `0x44030000` flash 控制器、手写 WREN/PP/SE/RDSR/WRSR、手管 SR0 block-protect | 32B 对齐（控制器 READ 是 32 字节 burst，非对齐返回错数据）、SR0=0x1c 默认保护需清/恢复 |
| `bk7258_dvfs.c` + `boot_clock.c` | Clock/DVFS | 直接操作 ANA_REG9/VDDIG/VDDD/DPLL band/M1 mux | DPLL 320MHz 花数天：VDDIG 档位 0xC/0xE 证否、0xD 命中；SDK 注释「cpu0:160m」在本 mux 下不成立；band 字段解码 [25]；cold vs soft 路径差异 |
| `bk7258_wdt.c` | WDT | 手写 APB+AON 喂狗 key 序列 | 照搬原厂 `sub_2000FE4` key 序列 `0x005A1F40/0x00A51F40` |
| `bk7258_timerisr.c` | SysTick | NuttX 标准 SysTick + 切档后手动重算 RELOAD | 频率切换后需按 `current_cpu_hz()` 原子重写 RELOAD+清 CVR |
| `bk7258_vectors.c` / `bk7258_start.c` / `bk7258_irq.c` | 启动/向量/NVIC | 自有向量表 + 裸 `__start` + NVIC glue | FPCCR bit29/30/31 清、slot[15] exception_direct 还原 |

每一个模块都是从零逆向/推导出来的，过程中踩了大量坑——而这些问题 **Beken SDK 内部早已解决**。

### 1.2 参考实现：7236N `vendor_beken`（wrapper + 预编译 SDK 库）

路径：`$VENDOR_BEKEN/chips/bk7236n/`

该适配**不重写任何底层驱动**，而是构建在 Beken Armino SDK 的预编译静态库之上：

- **SDK 交付物**：`chips/bk7236n/bk_idk/armino_as_lib/bk7236n/libs/` 下约 30 个预编译 `.a`
  （`libdriver.a` 2.4MB、`libbk_system.a`、`libbk_pm.a`、`libbk_wifi.a` ~7MB、
  `libbluetooth_controller_*.a`、`libeasy_flash.a`、`libcm33.a`、`libbk_rtos.a` 等，共 ~24MB）。
  这些 `.a` 由 `tools/build_tools/armino_as_lib.sh` 打包生成（见 §4）。
- **NuttX 侧代码**全部是薄 wrapper 或 OS 适配 shim：
  - `beken_uart.c` —— `receive/send/rxint/txint/attach` 全部转发到 `bk_uart_read_bytes` /
    `bk_uart_write_bytes` / `bk_uart_register_rx_isr` / `bk_uart_enable_rx_interrupt` 等 SDK API；
    `setup()` 为空（"configured by internal driver libs"）。
  - `beken_flash.c` —— NuttX `struct mtd_dev_s` lower-half，`erase/bread/bwrite` 转发到
    `bk_flash_erase_sector` / `bk_flash_read_bytes` / `bk_flash_write_bytes`，用
    `bk_flash_set_protect_type(FLASH_PROTECT_NONE)` ... `bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK)`
    包裹保护切换。**没有任何 SPI NOR opcode 或 SR0 寄存器操作**。
  - `beken_tim_lowerhalf.c` / `beken_wdt_lowerhalf.c` / `beken_rtc_lowerhalf.c` /
    `beken_pwm_lowerhalf.c` / `beken_spi.c` / `beken_i2c_lowerhalf.c` / `beken_adc_lowerhalf.c` /
    `beken_trng_lowerhalf.c` —— 同样转发到对应 `bk_*` SDK API。
  - `beken_os_adapt.c`（~1900 行）—— 把 SDK 内部用到的 FreeRTOS 原语
    （`rtos_create_thread` / `rtos_init_semaphore` / `os_malloc` / `bk_get_tick` 等）
    在 NuttX 原语（`kthread_create` / `nxsem_post` / `kmm_malloc` / `clock_systime_timespec`）上重实现。
  - `beken_interrupt_base.c` —— 实现 SDK 的 `bk_int_isr_register`/`unregister`，在 NuttX
    `irq_attach`/`up_enable_irq` 之上，用 `--whole-archive` 强符号覆盖 `libdriver.a` 里的 weak 桩。
  - `beken_irq.c` —— 唯一含 `putreg32/getreg32` 的 chip 源文件，但**只写 NVIC**（`NVIC_SYSH*_PRIORITY`
    等），不碰任何 BK SoC 外设寄存器。
- **Board bringup**（`beken_ap.c` 的 `board_early_initialize`）：
  ```c
  sys_drv_init();              // SDK 主初始化（内部等价 sys_hal_early_init）
  bk_wdt_driver_init();
  bk_gpio_driver_init();
  bk_uart_driver_init();
  bk_aon_rtc_driver_init();
  bk_timer_driver_init();
  bk_spi_driver_init();
  bk_dma_driver_init();
  bk_otp_driver_init();
  bk_adc_driver_init();
  ...
  ```
  DVFS 也走 SDK 投票 API：
  ```c
  bk_pm_module_vote_cpu_freq(PM_DEV_ID_DEFAULT, PM_CPU_FRQ_120M);
  ...
  bk_pm_module_vote_cpu_freq(PM_DEV_ID_DEFAULT, PM_CPU_FRQ_240M);
  ```
- **构建接线**（`boards/bk7236n/bk7236n-evb/scripts/Make.defs`）：
  ```makefile
  EXTRA_LIBS += $(wildcard .../bk_idk/armino_as_lib/bk7236n/libs/*.a)
  ```
  无条件链接 `libs/` 下所有 `.a`；Kconfig 只控制 NuttX 侧 glue 与功能开关，不门控 `libdriver.a`。
- **Make.defs 注入 `-DCONFIG_*=0`**：`CONFIG_FREERTOS=0`、`CONFIG_SOC_BK7236=0`、`CONFIG_SYS_CPU1=0` 等，
  让预编译库的头文件在非 FreeRTOS 环境下编译通过。

**7236N 全芯片源码 grep 结论**：除 `beken_irq.c` 的 NVIC 写入外，**没有任何 `putreg32/getreg32`
或 `__asm`**；**DPLL 作为寄存器**零命中（唯一命中是 IRQ 源号 `BK7236N_IRQ_DPLL_UNLOCK`）。
UART baud/FIFO/中断屏蔽、flash SPI NOR opcode/SR0、DPLL/VDDIG/mux —— 全部在 `libdriver.a`/
`libbk_pm.a` 内部，不可见。

### 1.3 两种方式的本质对比

| 维度 | 7236N（wrapper + 预编译库） | BK7258 当前（从零重写） |
|---|---|---|
| 底层驱动策略 | 薄 wrapper 转发 `bk_*` API | 寄存器级从零实现 |
| SDK 机制 | ~30 个预编译 `.a`（闭源，~24MB）链接进 NuttX | TuyaOpen/bk_avdk_smp 源码人工逆向移植 |
| SDK 主初始化 | 一行 `sys_drv_init()` | 手工复刻 `sys_hal_early_init`（`boot_clock.c`） |
| UART | `bk_uart_*` SDK API | 寄存器级，踩 4 个 RX bug |
| Flash | `bk_flash_*` SDK API | 寄存器级，踩对齐/SR0 问题 |
| Clock/DVFS | `bk_pm_module_vote_cpu_freq()` 投票 API | 寄存器级，DPLL 320MHz 调数天 |
| 可调试性 | 低（`.a` 不可改，底层不可见） | 高（全源码，可 bit 级调试） |
| 工作量 | 低（SDK 已验证，wrapper 很薄） | 高（每模块从零，反复踩坑） |
| Bug 密度 | 低 | 高（每模块都踩坑） |
| 适用前提 | 厂商交付该芯片的预编译 SDK 库 | 有源码 SDK 可参考即可 |

### 1.4 结论：当前方式有问题

当前方式**重复实现了 SDK 已经解决的问题**。每手写一个寄存器级驱动，都在重新踩 Beken 工程师
早已填平的坑（UART RX FIFO 阈值、flash 32B burst 对齐、DPLL VDDIG 档位...）。正确做法是
**引入 SDK，NuttX 侧只写薄 wrapper 调 `bk_*` API**，把硬件细节交给已验证的 SDK 实现。

---

## 2. 可用 SDK 源码确认

本次调研确认两个 SDK 都有完整 BK7258 驱动 API：

### 2.1 bk_avdk_smp（Beken 官方 SDK，开源）

路径：`$BK_AVDK/cp/middleware/`

| 当前手写模块 | SDK API（有源码，可调用） | 源文件 |
|---|---|---|
| UART | `bk_uart_init()` / `bk_uart_write_bytes()` / `bk_uart_read_bytes()` / `bk_uart_register_rx_isr()` | `driver/uart/uart_driver.c:1044+` |
| Flash | `bk_flash_erase_sector()` / `bk_flash_read_bytes()` / `bk_flash_write_bytes()` / `bk_flash_set_protect_type()` / `bk_flash_driver_init()` | `driver/flash/flash_driver.c`、`flash_driver_ext.c` |
| Clock/DVFS | `sys_drv_switch_cpu_bus_freq(pm_cpu_freq_e)`（内部自动处理 VDDIG/VDDD 升压+_mux） / `bk_pm_module_vote_cpu_freq()` | `driver/sys_ctrl/sys_ps_driver.c:244` |
| 系统初始化 | `sys_hal_early_init()`（BK7258 专用） | `soc/bk7258/hal/sys_hal.c:2793` |
| Timer | `bk_timer_*` | `driver/timer/` |
| WDT | `bk_wdt_*` | `driver/wdt/` / `driver/aon_wdt/` |

关键子目录：
- `soc/bk7258/hal/` —— BK7258 专用 HAL（`sys_hal.c`、`flash_ll.c/h`、`uart_ll.c/h`、`wdt_ll.c/h`、`timer_ll.c/h`...）
- `soc/bk7258/soc/` —— BK7258 寄存器定义（`flash_reg.h`、`uart_reg.h`、`icu_reg.h`、`sys_reg.h`...）
- `soc/common/hal/include/` —— 通用 HAL 头（`sys_hal.h`、`flash_hal.h`...）
- `driver/flash/` —— flash 驱动源码（`flash.c/h`、`flash_driver.c/h`、`flash_driver_ext.c`）
- `driver/common/` —— 驱动公共框架（`dd.c`、`driver.c`、`drv_model.c`）

### 2.2 TuyaOpen SDK（有 HAL 适配层）

路径：`$TUYAOPEN/platform/T5AI/tuyaos/tuyaos_adapter/`

`tuyaos_adapter` 是一层 HAL 抽象，`include/` 下按子系统分目录（`uart/`、`flash/`、`timer/`、
`watchdog/`、`system/`、`gpio/`、`spi/`、`i2c/`、`adc/`、`pwm/`、`pinmux/`、`init/`...），
`src/driver/` 里在 `bk_*` API 上实现 `tuya_*` 接口。多一层间接，调试多一层。

### 2.3 选型：直接调 bk_avdk_smp 的 `bk_*` API

理由：API 最直接（就是 7236N 的做法）；团队调试 DPLL 时已读过这些源码，熟悉；
TuyaOpen 的 `tuyaos_adapter` 多一层抽象，初期无收益。

---

## 3. flash 模块迁移方案（方案 1：拷 flash 源码）

### 3.1 当前 flash 驱动现状

`chip/cp/bk7258_flash_mtd.c`（630 行）寄存器级实现：
- 直接定义 flash 控制器 MMIO（`0x44030000` + 各寄存器偏移）
- 手写 `bk7258_flash_swop()`（OP_CMD + OP_SW 触发控制器操作）
- 手写 `bk7258_flash_read_sr()` / `bk7258_flash_write_sr()`（RDSR/WRSR）
- 手写 `bk7258_flash_unprotect()` / `bk7258_flash_restore()`（option-A SR0 清/恢复）
- 手写 `bk7258_flash_program32()`（WREN + 8 words to DATA_SW_FLASH + PP）
- `bread` 32 字节 burst 读、`erase` 逐扇区 SE、`bwrite` 逐 32B PP
- `bk7258_flash_mtd_initialize()`读取BK7258集成Flash接口ID，校验8 MiB compatible geometry

板端已验证（见 `n5-flash-filesystem.md`）：raw r/w → MTD → ftl → LittleFS 全链路通过。

### 3.2 目标：改写为调用 `bk_flash_*` SDK API

参考 `vendor_beken/chips/bk7236n/beken_flash.c` 的 wrapper 模式（但**不必照抄**——7236N
是链接整个 `libdriver.a`，我们是拷 flash 源码，且 BK7258 数据分区基址不同）：

```c
/* erase: 包保护切换 + 逐扇区 bk_flash_erase_sector */
bk_flash_set_protect_type(FLASH_PROTECT_NONE);
for (i = 0; i < nblocks; i++)
  bk_flash_erase_sector(offset + i * SECTOR_SIZE);
bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK);

/* bread: bk_flash_read_bytes 一次读整块 */
bk_flash_read_bytes(offset, buf, nbytes);

/* bwrite: 包保护切换 + bk_flash_write_bytes */
bk_flash_set_protect_type(FLASH_PROTECT_NONE);
bk_flash_write_bytes(offset, buf, nbytes);
bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK);

/* init: bk_flash_driver_init() + 读 ID 校验 */
bk_flash_driver_init();
```

SDK 内部自动处理：32B burst 对齐、SR0 保护、WREN/PP/SE 时序、status poll。我们不再需要
`bk7258_flash_wait_ready`/`swop`/`wren`/`read_sr`/`write_sr`/`unprotect`/`restore`/`program32`
这些寄存器级函数。

### 3.3 引入的 SDK 源码（最小集）

从 `bk_avdk_smp/cp/middleware/` 拷到 `board/bk7258/sdk/`：

```
sdk/
├── driver/
│   ├── flash/
│   │   ├── flash.c              # flash_read/write/ctrl 旧接口（可能被 flash_driver 引用）
│   │   ├── flash.h
│   │   ├── flash_driver.c       # bk_flash_erase_sector/read_bytes/write_bytes/set_protect_type/driver_init
│   │   ├── flash_driver.h
│   │   └── flash_driver_ext.c   # 扩展接口（按需）
│   └── common/
│       ├── dd.c / dd_pub.h      # driver framework（flash_driver 依赖）
│       ├── driver.c
│       └── drv_model.c / drv_model.h
├── soc/
│   ├── bk7258/
│   │   ├── hal/
│   │   │   ├── flash_ll.c / flash_ll.h        # flash 寄存器级 LL
│   │   │   └── flash_hal_debug.c              # 可能需要裁剪
│   │   └── soc/
│   │       ├── flash_reg.h / flash_struct.h   # flash 寄存器定义
│   │       ├── icu_reg.h / icu_struct.h       # 中断/时钟门控（flash 依赖）
│   │       └── sys_reg.h / sys_struct.h       # 系统寄存器（按需）
│   └── common/
│       └── hal/
│           └── include/
│               └── flash_hal.h                # flash HAL 接口
└── include/                                   # SDK 公共头（最小子集）
    ├── common/
    │   └── bk_include.h / bk_typedef.h
    ├── driver/
    │   ├── flash.h                            # public flash API
    │   └── flash_types.h
    ├── os/
    │   └── os.h / mem.h                       # RTOS 抽象（见 §3.4）
    └── modules/
        └── chip_support.h / system.h
```

> **实际引入范围以编译驱动为准**：先拷上述清单，编译报缺哪个头/符号再补哪个，
> 逐层收敛（避免一开始就拷一大堆无关依赖）。`flash_driver.c` 的 `#include` 已确认：
> `<common/bk_include.h>`、`<components/ate.h>`、`<os/mem.h>`、`<driver/flash.h>`、
> `<os/os.h>`、`"flash_driver.h"`、`"flash_hal.h"`、`"sys_driver.h"`、
> `<driver/flash_partition.h>`、`<modules/chip_support.h>`、`"flash_bypass.h"`。
> 其中 `ate.h` / `flash_partition.h` / `flash_bypass.h` / `sys_driver.h` 可能需要桩或裁剪。

### 3.4 RTOS 适配层（NuttX shim）

SDK 内部用 FreeRTOS 原语（mutex/semaphore/malloc/tick）。flash 子集需要的 OS 原语很少
（主要是 `os_malloc`/`os_free`、可能一个 mutex）。需要在 `sdk/os/` 下写一个 NuttX 适配：

- `os_malloc` / `os_free` → `kmm_malloc` / `kmm_free`
- `os_mutex_create`/`lock`/`unlock` → `nxmutex_init` / `nxmutex_lock` / `nxmutex_unlock`
- `rtos_get_tick` → `clock_systime_ticks`
- 必要时用 `-DCONFIG_FREERTOS=0` 等宏禁用 SDK 头里的 FreeRTOS 假设

初期预计几百行（远小于 7236N 的 1900 行 `beken_os_adapt.c`，因为只服务 flash）。
**策略**：先写空桩 + 链接时按未定义符号补，避免一开始就猜全部接口。

### 3.5 目录组织（在当前 contest 工程内）

```
contest2026_135_yongwangzhiqian/board/bk7258/
├── bootloader/              # 不动
├── chip/                    # NuttX wrapper 层
│   ├── bk7258_flash_mtd.c       # ← 改写：调 bk_flash_* API
│   ├── bk7258_flash_mtd.h
│   ├── bk7258_serial.c          # 后续改写
│   ├── bk7258_dvfs.c            # 后续改写
│   ├── bk7258_vectors.c / bk7258_irq.c / bk7258_timerisr.c /  # 保留
│   ├── bk7258_start.c / bk7258_allocateheap.c / bk7258_lowputc.c  # 保留
│   ├── Make.defs                # ← 加 sdk 源码编译规则 + include 路径 + -D 宏
│   ├── Kconfig
│   └── CMakeLists.txt
├── sdk/                     # ← 新增：从 bk_avdk_smp 提取的 SDK 源码（见 §3.3）
│   ├── driver/flash/...
│   ├── driver/common/...
│   ├── soc/bk7258/...
│   ├── soc/common/...
│   ├── include/...
│   ├── os/os_adapt.c            # NuttX RTOS shim（见 §3.4）
│   └── Make.defs                # SDK 编译规则
├── configs/cp_nsh/defconfig    # ← 加 CONFIG_BK7258_FLASH_MTD_SDK=y
├── scripts/
└── src/
```

### 3.6 Kconfig / Make.defs 集成

`chip/Kconfig` 增加一个开关，区分「寄存器级实现」与「SDK wrapper 实现」（过渡期两者共存，
便于回退）：

```kconfig
choice
    prompt "BK7258 flash MTD implementation"
    default BK7258_FLASH_MTD_RAW

config BK7258_FLASH_MTD_RAW
    bool "Register-level flash MTD (legacy, board-verified)"
    ---help---
        Original hand-written register-level driver.

config BK7258_FLASH_MTD_SDK
    bool "SDK-wrapped flash MTD (calls bk_flash_* APIs)"
    select BK7258_SDK_FLASH          # 拉入 sdk/ 下 flash 源码
    ---help---
        Thin wrapper over Beken bk_avdk_smp flash APIs.
endchoice
```

`chip/Make.defs`：

```makefile
ifeq ($(CONFIG_BK7258_FLASH_MTD_RAW),y)
CHIP_CSRCS += bk7258_flash_mtd.c
endif

ifeq ($(CONFIG_BK7258_FLASH_MTD_SDK),y)
CHIP_CSRCS += bk7258_flash_mtd.c          # 同名文件，内部 #ifdef 走 SDK 分支
include $(CHIP_DIR)/../sdk/Make.defs      # 拉入 SDK flash 源码 + include 路径
endif
```

> 具体是用 `#ifdef` 在同一文件分两套实现，还是拆成 `bk7258_flash_mtd_raw.c` /
> `bk7258_flash_mtd_sdk.c` 两个文件，实现时再定（后者更干净）。

### 3.7 验证

1. **编译**：`./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/cp_nsh -j8`
   —— 先确保 SDK flash 源码在 NuttX 编译环境下过编（逐个补缺失头/符号/桩）。
2. **链接**：确认 `bk_flash_*` 符号全部解析，无未定义引用。
3. **板端回归**（用 N5 已有验证基线）：
   - 烧 `all-app.bin`（BKFIL/bk_loader，见 `bk7258-flash-flow-bkfil`）
   - NSH 下 `/dev/mtdblock0` + `/data` 存在
   - `cat /data/probe.txt` = `BK7258LFS-OK`（LittleFS 挂载 + 探针文件）
   - 写一个新文件 → 软复位 → 内容存活（reboot-persistence）
   - 与 N5-D7 板端证据对齐：`LC`/`LR` boot trace、on-disk littlefs magic @ 0x100000
4. **行为等价性**：SDK wrapper 与原寄存器级实现应在同一数据分区（`0x00100000..0x001FFFFF`）
   产出相同结果；JEDEC ID 校验仍保留。

---

## 4. 两种引入路径对比（方案 1 vs 方案 2）

### 4.1 `armino_as_lib.sh` 机制

`$BK_AVDK/tools/build_tools/armino_as_lib.sh` 是 Beken 把 SDK
打包成「可被第三方 RTOS 链接的预编译库集合」的脚本。`build.sh` 在编译 SoC 后调用它：

```bash
armino_as_lib.sh <soc> <armino_dir> <build_dir> <project>
```

流程：
1. `mkdir armino_as_lib/<soc>/{config,libs}`
2. `cp components/bk_libs/<soc>/libs/* → armino_as_lib/<soc>/libs/`（闭源预编译库：
   wifi/bluetooth/phy/coex 等）
3. 遍历 `build_dir/armino/*/`，把每个子目录编译出的 `.a` 也 `cp` 到 `libs/`
   （`libdriver.a`、`libbk_system.a`、`libbk_pm.a`、`libbk_rtos.a`、`libeasy_flash.a`... 全栈）
4. `cp sdkconfig.h → config/`
5. `cp -rf armino_dir/include → armino_as_lib/include/`（全局头）

产出即 7236N 用的 `bk_idk/armino_as_lib/`：自包含 SDK 包（~30 个 `.a` + 头 + sdkconfig.h）。
第三方 RTOS（NuttX）只需 `EXTRA_LIBS += $(wildcard .../libs/*.a)` 链接，再写
`beken_os_adapt.c` 把 SDK 内部 FreeRTOS 调用重定向到自家 RTOS。

### 4.2 BK7258 预编译库现状（本次调研确认）

- `bk_avdk_smp/cp/components/bk_libs/bk7258/libs/` 下**只有**闭源 RF/蓝牙/wifi/phy 库
  （`libwifi.a`、`libbluetooth_controller_*.a`、`libcom_phy.a`、`libbk_coex.a`、`libbk_phy.a` 等），
  **没有 `libdriver.a`**。
- `libdriver.a` 是从 `cp/middleware/driver/` **开源源码编译**的产物，必须先跑一次
  `build.sh bk7258` 让 `armino_as_lib.sh` 把它编译打包出来，才有现成 `.a` 可用。
- 当前仓库里**没有**已生成的 `armino_as_lib/bk7258/` 目录（需自行跑 build 产出）。

### 4.3 方案对比

| 维度 | 方案 1：只拷 flash 源码 | 方案 2：跑 armino_as_lib 打包整库 |
|---|---|---|
| 怎么得到 SDK | 手挑 flash 相关 `.c/.h` 到 `sdk/` | 跑 `build.sh bk7258` 得自包含库包，链 `libdriver.a` 等 |
| 可调试性 | 高（有源码，可 step） | 低（`.a` 不可改，底层不可见） |
| 工作量 | 中（处理 flash 依赖链 + 小 OS shim） | 大（需先跑通 SDK 完整编译；写完整 `beken_os_adapt.c` ~1900 行；牵进 wifi/ble/rtos 全栈依赖） |
| 与 7236N 一致 | 否（7236N 是整库链接） | ✅ 完全一致 |
| 对当前 flash 单模块 | ✅ 合适（增量、可控） | 杀鸡用牛刀（为 flash 链全栈） |
| 后续扩展到 UART/DVFS | 每模块照此扩大，逐步收敛 | 一次到位 |
| 回退能力 | 易（`CONFIG_BK7258_FLASH_MTD_RAW` 切回） | 难（牵一发动全身） |
| 风险 | 低（只动 flash） | 高（RTOS 适配层若不全，全栈链接失败） |

### 4.4 选型结论

**flash 模块先走方案 1**（用户已确认）。理由：
- 只动 flash 一个模块，不想为它牵进整个 `libdriver.a`/`libbk_rtos.a`/wifi/ble 全栈
  （那会引入完整 RTOS 适配层、wifi 依赖等一大堆，初期不可控）。
- 增量验证：先打通 flash 的「NuttX wrapper → bk_flash_* API → SDK 源码」路径，
  证明 SDK API 在 NuttX 环境可编译可链接可运行。
- 后续 UART/DVFS 照此扩大，最终当多个模块都走 SDK 后，再评估是否切到方案 2
  （届时 `beken_os_adapt.c` 已逐步写全，切整库链接的边际成本就低了）。

> **方案 2 的前置条件**（将来切时需要）：
> 1. 跑 `cd bk_avdk_smp && ./tools/build_tools/build.sh bk7258` 产出 `armino_as_lib/bk7258/`；
> 2. 把该目录拷进 `board/bk7258/sdk/`（或软链）；
> 3. `Make.defs` 改 `EXTRA_LIBS += $(wildcard .../libs/*.a)`；
> 4. 写完整 `beken_os_adapt.c`（参考 7236N 的 1900 行，覆盖 rtos/os/mem 全套）；
> 5. 用 `-DCONFIG_*=0` 禁用 SDK 头里的 FreeRTOS/SoC 假设。

---

## 5. 风险与注意事项

1. **`flash_driver.c` 依赖链可能比预期深**：已确认 include `ate.h`/`sys_driver.h`/
   `flash_partition.h`/`flash_bypass.h`/`chip_support.h`。其中 `sys_driver.h` 会拉入
   `sys_hal_early_init` 及整个 sys_ctrl；`flash_partition` 拉分区表逻辑；`ate` 拉 ATE 测试。
   **对策**：用 `#ifdef CONFIG_*` 裁剪不需要的分支，或对这些组件写空桩，只保留 flash
   erase/read/write/set_protect/init 主路径。

2. **`flash_bypass.c` / `flash_shared_lock.c`**：`flash_driver.c` 顶部有
   `#if CONFIG_FLASH_CP_AP_DIRECT_ACCESS` 包裹 shared_lock，单核场景可关掉。
   `flash_bypass` 未必需要，可能要桩。

3. **数据分区基址差异**：7236N 的 `beken_flash.c` 操作整片 flash（offset 从 0 起，用
   `mtd_partition` 切分区）；当前 `bk7258_flash_mtd.c` 直接限定数据分区
   `0x00100000..0x001FFFFF`。改写时需决定：是让 `bk_flash_*` 操作全片再用 `mtd_partition`
   切，还是在 wrapper 里加偏移基址 `BK7258_DATA_PART_BASE`。**推荐后者**（最小改动，
   与现有 LittleFS 链路兼容）。

4. **JEDEC ID 校验**：当前 `bk7258_flash_mtd_initialize` 用自写 `read_id` 校验
   BK7258集成8 MiB Flash。SDK 的 `bk_flash_driver_init` 内部也会读 ID 设配置。需确认两者不冲突，
   或直接信任 SDK 的探测。

5. **保护策略差异**：当前是 option-A「每 op 清/恢复 SR0」；SDK 是
   `set_protect_type(FLASH_PROTECT_NONE)` ... `set_protect_type(FLASH_UNPROTECT_LAST_BLOCK)`。
   需确认 SDK 的保护切换在 BK7258集成Flash上与 option-A 等价（板端回归验证）。

6. **不修改 nuttx 官方树**（团队规矩，见 `do-not-modify-nuttx-official-tree` 记忆）：
   所有改动只在 `contest2026_135_yongwangzhiqian/board/bk7258/` overlay 内。

7. **bootloader 保留手写**：`bootloader/` 不动（SDK bootloader 不适用于自定义分区布局，
   且 B2 产品级 bootloader 已板端验证）。

---

## 6. 实现路线图

1. **建 `sdk/` 目录骨架**：按 §3.3 创建目录树，从 `bk_avdk_smp` 拷 flash 最小源码集。
2. **写 `sdk/os/os_adapt.c`**：NuttX RTOS shim 最小集（malloc/free/mutex/tick）+ 空桩。
3. **改写 `chip/cp/bk7258_flash_mtd.c`**（或拆 `_sdk.c`）：erase/bread/bwrite/init 调 `bk_flash_*`。
4. **更新 `chip/Kconfig`** + `chip/Make.defs`：加 `BK7258_FLASH_MTD_SDK` choice + SDK 编译规则。
5. **编译收敛**：`build.sh ... nsh -j8`，逐个补缺失头/符号/桩直到过编。
6. **板端回归**：按 §3.7 烧录验证，对齐 N5-D7 证据。
7. **后续模块**（本阶段不做，仅规划）：UART → `bk_uart_*`、DVFS → `sys_drv_switch_cpu_bus_freq`，
   逐步扩大 `sdk/`；最终评估切方案 2 整库链接。

---

## 7. 相关文件索引

**当前实现**：
- `contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_flash_mtd.c` — 寄存器级 flash MTD（待改写）
- `contest2026_135_yongwangzhiqian/board/bk7258/chip/cp/bk7258_flash_mtd.h`
- `contest2026_135_yongwangzhiqian/board/bk7258/chip/Make.defs` — 编译规则（待加 SDK 项）
- `contest2026_135_yongwangzhiqian/board/bk7258/chip/Kconfig`
- `contest2026_135_yongwangzhiqian/docs/bk7258-t5ai/nuttx-port/n5-flash-filesystem.md` — N5 板端验证证据

**参考实现（7236N）**：
- `$VENDOR_BEKEN/chips/bk7236n/beken_flash.c` — wrapper 范本
- `$VENDOR_BEKEN/chips/bk7236n/beken_os_adapt.c` — OS shim 范本
- `$VENDOR_BEKEN/chips/bk7236n/Make.defs` — `-DCONFIG_*=0` 范本
- `$VENDOR_BEKEN/boards/bk7236n/bk7236n-evb/scripts/Make.defs` — `EXTRA_LIBS` 链接范本

**SDK 源（bk_avdk_smp）**：
- `$BK_AVDK/cp/middleware/driver/flash/` — flash 驱动源码
- `$BK_AVDK/cp/middleware/soc/bk7258/hal/` — BK7258 HAL
- `$BK_AVDK/cp/middleware/soc/bk7258/soc/` — BK7258 寄存器定义
- `$BK_AVDK/cp/middleware/soc/common/hal/include/` — 通用 HAL 头
- `$BK_AVDK/tools/build_tools/armino_as_lib.sh` — SDK 打包脚本
- `$BK_AVDK/cp/components/bk_libs/bk7258/libs/` — 闭源预编译库（无 libdriver）

**SDK 源（TuyaOpen，备选）**：
- `$TUYAOPEN/platform/T5AI/tuyaos/tuyaos_adapter/` — HAL 适配层

**记忆引用**：
- `bk7258-n5-flash-write-verified` — N5-D5/D6/D7 板端验证状态
- `bk7258-n4-dvfs-320-board-verified` — DVFS 现状（后续模块参考）
- `bk7258-nuttx-boot-core-verified` — N1/N2 boot + UART 现状
- `do-not-modify-nuttx-official-tree` — 改动边界规矩
