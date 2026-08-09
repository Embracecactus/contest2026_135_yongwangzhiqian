# Stage N6 — Beken SDK 集成框架：移植架构 / 目录组织 / 宏隔离体系

> **范围**：定义 BK7258 NuttX 适配引入 Beken(bk_avdk_smp) SDK 的**框架性架构**——目录组织、
> 宏隔离体系、AP/CP 三核适配机制、OS 适配层、依赖收敛策略。这是后续每个模块
> (flash/WDT/UART/DVFS/timer/gpio/...) 统一遵循的骨架，不只服务单个模块。
> **状态**：框架设计；WDT 为首个按此框架落地的模块（见 `b2-wdt-fix.md`），flash 紧随其后
> （见 `n6a-sdk-integration-research.md`）。
> **决策**：全面转 SDK——底层不再从零手写寄存器，NuttX 侧只写薄 wrapper 调 `bk_*` API。

> **路径约定**（避免暴露本机绝对路径）：
> - `$CONTEST` = 本团队 overlay 根（含 `board/bk7258/`）
> - `$BK_AVDK` = Beken 官方 SDK（bk_avdk_smp，`cp/` 与 `ap/` 两套独立编译路径）
> - `$VENDOR_BEKEN` = 7236N 参考实现（vendor_beken）
> - `$TUYAOPEN` = TuyaOpen SDK

---

## 1. 背景：为什么需要框架

BK7258 NuttX 适配初期所有底层驱动（UART/flash/DVFS/WDT/timer）从寄存器级手写，踩了大量
SDK 已解决的坑（见各 N2/N4/N5 记忆）。决定全面转 SDK 后，若每个模块各自为政地拷源码、
定宏、写 wrapper，会迅速演变成难以维护的拼凑。需要一套**统一框架**让每个模块按相同方式
引入，保证：可裁剪、可裁依赖桩化、AP/CP 三核正确、宏隔离清晰、增量可回退。

## 2. BK7258 AP/CP 三核架构与 SDK 编译模型

### 2.1 硬件/SDK 架构

- **BK7258 = 三核 AMP**：CPU0 + CPU1 + CPU2
- **CP = CPU0**（单核）：Wi-Fi、BLE、少量基础外设。**我们的 NuttX 跑在 CPU0 → 我们是 CP 侧**
  （见 [[bk7258-nuttx-boot-core-verified]]：bootloader 跳 CPU0、N1/N2 板端在 CPU0、SDK 分区
  `primary_cp_app`@0x02010000）
- **AP = CPU1+CPU2 SMP**：多媒体方案、客户应用开发
- **SDK 一套源码，按核独立编译**：`$BK_AVDK/cp/`（CP 路径）与 `$BK_AVDK/ap/`（AP 路径）
  独立构建；CP/AP 及核间功能差异用 `CONFIG_*` 宏隔离

### 2.2 三类宏（框架必须区分）

| 宏类别 | 示例 | 作用 | 框架处理 |
|---|---|---|---|
| **芯片族宏** | `CONFIG_SOC_BK7256XX` / `BK7236XX` / `BK7239XX` / `BK7286XX` | 选择芯片族专有寄存器布局/HAL 分支 | 我们是 BK7258，**这些都不命中**→大量 AP/其它芯片族代码编译掉（已实测：`wdt_hal_close` 等整段消失） |
| **功能开关宏** | `CONFIG_INT_WDT` / `CONFIG_TASK_WDT` / `CONFIG_INT_AON_WDT` / `CONFIG_SYSTEM_CTRL` / `CONFIG_NMI_WDT_EN` / `CONFIG_AON_RTC` / `CONFIG_DEBUG_VERSION` | 开关某子功能/某条代码路径 | 按"只留硬件主路径"原则关闭 FreeRTOS/任务级/ISR 自动喂狗等，见 §5 |
| **单份资源核间互斥宏** | `CONFIG_TRNG` / `CONFIG_FLASH` / 各外设归属 | 单份外设只在某核用，哪个核需要就在哪个核的 defconfig 开 | 我们是 CP(CPU0)，需用的外设在 `bk7258.defconfig` 开；不用的关闭，避免与 AP 抢资源 |

### 2.3 关键判断（已实测）

- **APB WDT vs AON WDT 在 SDK 是两套独立 API**：
  - `bk_wdt_*` → `wdt_ll` → `WDT_LL_REG_BASE` ≡ `SOC_WDT_REG_BASE = 0x44800000`（APB），与 `hal->id` 无关
  - `bk_aon_wdt_*` → `aon_wdt_ll` → `SOC_AON_WDT_REG_BASE = 0x44000600`（AON）
  - `wdt_hal.c` 里混写两个基址的代码全在 `BK7236XX/7239XX/7286XX` 守卫内，BK7258 编译掉
- **CP 路径 = `$BK_AVDK/cp/middleware/`**：我们引入的源码一律取 `cp/`，不碰 `ap/`
- **共享资源**：跨核共享的外设（如 flash、TRNG）需确认 CP 侧可用且不与 AP 抢；WDT/APB/AON
  是 CP 基础外设，CPU0 可用

## 3. 移植架构总览

```
┌─────────────────────────────────────────────────────────────┐
│ NuttX 应用 / NSH / 文件系统 / 网络                          │
├─────────────────────────────────────────────────────────────┤
│ NuttX upper-half（drivers/timers/watchdog.c, mtd, serial…） │
│   ↑ automonitor / VFS / ioctl 分发                          │
├─────────────────────────────────────────────────────────────┤
│ NuttX lower-half wrapper（chip/common/ + chip/cp/）            │
│   common/: serial, wdt, lowputc, os_adapt, sdk_stubs …        │
│   cp/: flash_mtd, wdt, sdk_irq, gpio …                        │
│   ↑ 薄转发：ops->start = bk_wdt_start(timeout) 等            │
├─────────────────────────────────────────────────────────────┤
│ Beken SDK（board/bk7258/sdk/，从 $BK_AVDK/cp 拷入）     │
│   driver/wdt  driver/flash  driver/uart  driver/sys_ctrl…   │
│   soc/bk7258/hal/*_ll.c  soc/bk7258/soc/*_reg.h             │
│   ↑ 寄存器级实现（已验证）                                  │
├─────────────────────────────────────────────────────────────┤
│ OS 适配层（board/bk7258/sdk/os/os_adapt.c）             │
│   os_malloc→kmm_malloc  bk_get_tick→clock_systime_ticks     │
│   GLOBAL_INT_DISABLE→irqsave  rtos_*→NuttX 原语             │
└─────────────────────────────────────────────────────────────┘
```

**分层职责**：
- **NuttX wrapper（chip/common/ + chip/cp/）**：只做 `struct *_ops_s` 契约 → `bk_*` API 转发，零寄存器操作
- **Beken SDK（sdk/）**：硬件实现，按模块增量拷入，宏隔离裁剪
- **OS 适配层（sdk/os/）**：把 SDK 内部 FreeRTOS/OS 抽象重定向到 NuttX 原语

## 4. SDK 集成方案：armino_as_lib 预编译库模式（推荐）

### 4.1 方案概述

参考 `$VENDOR_BEKEN`（7236N）的官方 NuttX 适配模式：**不从 SDK 源码编译，而是链接预编译静态库**。

Beken 官方开发者 `tao.yang@bekencorp.com` 在 `$VENDOR_BEKEN` 维护的 `armino_as_lib` 机制：
1. 用 `$BK_AVDK/tools/build_tools/armino_as_lib.sh` 打包 SDK 为自包含库包
2. 产出 `armino_as_lib/<soc>/{libs/*.a, config/sdkconfig.h, include/}`
3. NuttX 侧只写薄 wrapper（`beken_*.c`），链接 `libs/*.a`
4. OS 适配层（`beken_os_adapt.c`）把 SDK 内部 FreeRTOS 调用重定向到 NuttX

**优势**：
- 与 7236N 完全一致的官方模式
- 不需要处理 SDK 源码依赖链（`libdriver.a` 已包含全部驱动实现）
- OS 适配层已有 7236N 范本（`beken_os_adapt.c` ~1900 行）可参考
- 宏隔离由 `sdkconfig.h` 统一管理（SDK 编译时已确定）

**当前阻塞**：需要先编译一次 SDK 产出 `armino_as_lib/bk7258/`（见 §4.2）。

### 4.2 编译 SDK 产出 armino_as_lib（需用户操作）

**前置条件**：
- 安装 `arm-none-eabi-gcc` 工具链（当前未安装）
- 或在已有工具链的环境（如 Docker/CI）编译

**编译命令**（在 `$BK_AVDK` 目录下）：
```bash
# 方式 1：全量编译（properties libs + SoC）
./tools/build_tools/build.sh . projects/app build bk7258

# 方式 2：只编译 SoC（不含闭源 properties libs）
./tools/build_tools/build.sh . projects/app build relbk7258

# 方式 3：只编译 properties libs
./tools/build_tools/build.sh . projects/app build libbk7258
```

**产出位置**：`$BK_AVDK/build/armino_as_lib/bk7258/`
```
bk7258/
├── libs/          # ~30 个预编译 .a（libdriver.a, libbk_system.a, libbk_pm.a, ...）
├── config/
│   ├── sdkconfig.h
│   └── sdkconfig.h.properties
└── include/       # SDK 全局头（driver/, soc/, os/, components/, modules/）
```

**集成到工程**：
```bash
# 拷进我们的工程（类似 7236N 的 bk_idk/armino_as_lib/）
cp -r $BK_AVDK/build/armino_as_lib/bk7258 \
      $CONTEST/board/bk7258/bk_idk/armino_as_lib/
```

### 4.3 工程目录组织（armino_as_lib 模式）

```
$CONTEST/board/bk7258/
├── bootloader/                  # 不动
├── chip/                        # NuttX 薄 wrapper（调 SDK API）
│   ├── bk7258_start.c
│   ├── bk7258_irq.c
│   ├── bk7258_lowputc.c
│   ├── bk7258_serial.c          # 改写：调 bk_uart_*
│   ├── bk7258_flash_mtd.c       # 改写：调 bk_flash_*
│   ├── bk7258_wdt.c             # 改写：调 bk_wdt_* + bk_aon_wdt_*
│   ├── bk7258_timerisr.c
│   ├── bk7258_allocateheap.c
│   ├── bk7258_os_adapt.c        # ★OS 适配层（参考 7236N 的 beken_os_adapt.c）
│   ├── Make.defs                # Include armino_as_lib 头 + 编译 wrapper
│   ├── Kconfig
│   └── CMakeLists.txt
├── bk_idk/                      # ★SDK 预编译库（编译 SDK 后拷入）
│   └── armino_as_lib/
│       ├── bk7258/
│       │   ├── libs/            # libdriver.a, libbk_system.a, ...
│       │   ├── config/sdkconfig.h
│       │   └── config/sdkconfig.h.properties
│       └── include/             # SDK 全局头
├── configs/cp_nsh/defconfig
├── scripts/
│   ├── Make.defs                # EXTRA_LIBS 链接 libs/*.a
│   └── ld.script
└── src/
    └── bk7258_bringup.c
```

### 4.4 Make.defs 集成（参考 7236N）

**chip/Make.defs**：
```makefile
include armv8-m/Make.defs
LDFLAGS += --build-id=none --entry=__start

BK_IDK_PATH_RELA_TO_SRC = ../../../../board/bk7258

# SDK 头文件路径
INCLUDES += ${INCDIR_PREFIX}$(BK_IDK_PATH_RELA_TO_SRC)/bk_idk/armino_as_lib/include
INCLUDES += ${INCDIR_PREFIX}$(BK_IDK_PATH_RELA_TO_SRC)/bk_idk/armino_as_lib/bk7258/config

# NuttX 薄 wrapper（只编译这些）
CHIP_CSRCS = bk7258_start.c bk7258_irq.c bk7258_lowputc.c
CHIP_CSRCS += bk7258_allocateheap.c bk7258_timerisr.c bk7258_os_adapt.c

ifeq ($(CONFIG_SERIAL_CONSOLE),y)
CHIP_CSRCS += bk7258_serial.c
endif

ifeq ($(CONFIG_WATCHDOG),y)
CHIP_CSRCS += bk7258_wdt.c
endif

ifeq ($(CONFIG_BK7258_FLASH_MTD),y)
CHIP_CSRCS += bk7258_flash_mtd.c
endif

# ... 其他模块按 CONFIG 门控
```

**scripts/Make.defs**（board 级）：
```makefile
# 链接 SDK 预编译库（与 7236N 完全一致的模式）
EXTRA_LIBS += $(wildcard $(shell readlink -f $(TOPDIR)/$(CONFIG_ARCH_CHIP_CUSTOM_DIR)/bk_idk/armino_as_lib/bk7258/libs)/*.a)
```

### 4.5 OS 适配层（`bk7258_os_adapt.c`）

参考 `$VENDOR_BEKEN/chips/bk7236n/beken_os_adapt.c`（~1900 行），把 SDK 内部 FreeRTOS 调用重定向到 NuttX：

| SDK 抽象 | NuttX 实现 |
|---|---|
| `rtos_create_thread` | `kthread_create` |
| `rtos_init_semaphore` / `rtos_get_semaphore` / `rtos_set_semaphore` | `nxsem_init` / `nxsem_wait` / `nxsem_post` |
| `rtos_init_mutex` / `rtos_mutex_lock` / `rtos_mutex_unlock` | `nxmutex_init` / `nxmutex_lock` / `nxmutex_unlock` |
| `os_malloc` / `os_free` / `os_calloc` / `os_realloc` | `kmm_malloc` / `kmm_free` / `kmm_calloc` / `kmm_realloc` |
| `rtos_get_ms_per_tick` / `rtos_ms_to_tick` | `MSEC_PER_TICK` / `MSEC2TICK` |
| `bk_get_tick` | `clock_systime_ticks` |
| `GLOBAL_INT_DISABLE` / `GLOBAL_INT_RESTORE` | `irqsave` / `irqrestore` |
| `rtos_delay_milliseconds` | `nxsig_usleep` |
| `rtos_init_event` / `rtos_set_event` / `rtos_wait_event` | 信号量/条件变量模拟 |

> **初期可只实现 WDT/flash 需要的子集**（`os_mem*`、`GLOBAL_INT_*`、`bk_get_tick`），
> 其余函数按编译报缺逐个补。7236N 的 `beken_os_adapt.c` 是完整范本。

### 4.6 预编译库内容（参考 7236N，BK7258 应类似）

```
libs/
├── libdriver.a          # 全部外设驱动（UART/flash/WDT/timer/SPI/I2C/...）
├── libbk_system.a       # 系统初始化（sys_drv_init, sys_hal_early_init）
├── libbk_pm.a           # 电源管理/DVFS（bk_pm_module_vote_cpu_freq）
├── libbk_rtos.a         # RTOS 抽象层（rtos_* 原语）
├── libbk_init.a         # 初始化框架
├── libbk_cli.a          # CLI 框架
├── libbk_event.a        # 事件框架
├── libbk_netif.a        # 网络接口
├── libeasy_flash.a      # KV 存储
├── libcm33.a            # Cortex-M33 支持
├── libcmsis.a           # CMSIS
├── libwifi.a            # Wi-Fi 协议栈
├── libbluetooth_*.a     # BLE 协议栈
├── libcom_phy.a         # PHY
└── ...                  # ~30 个 .a
```

**关键**：`libdriver.a` 已包含 `bk_wdt_*`、`bk_flash_*`、`bk_uart_*`、`sys_drv_*` 全部实现——
我们不需要从源码编译这些，直接链接即可。

```
sdk/
├── driver/                       # 驱动层（对应 cp/middleware/driver/）
│   ├── wdt/                      # WDT 模块（首个落地）
│   │   ├── wdt_driver.c / wdt_driver.h
│   │   └── (cli_wdt_api.c 不拷)
│   ├── aon_wdt/
│   │   └── aon_wdt_driver.c / aon_wdt_driver.h / bk_aon_wdt.h
│   ├── flash/                    # flash 模块（紧随）
│   │   ├── flash.c / flash.h / flash_driver.c / flash_driver.h
│   │   └── (flash_server/flash_ipc/flash_shared_lock 等 CP 单核不需要的不拷)
│   ├── uart/                     # 后续
│   ├── sys_ctrl/                 # 系统控制（时钟上电/复位，多模块共享依赖）
│   │   └── sys_clock_driver.c / sys_driver.h（按需拷需要的函数）
│   ├── common/                   # 驱动公共框架（dd/driver/drv_model，多模块复用）
│   │   └── dd.c / driver.c / drv_model.c
│   ├── pmu/                      # aon_pmu（按需）
│   └── include/                  # bk_private 头（bk_wdt.h/bk_aon_wdt.h/bk_fake_clock.h…）
│       └── bk_private/
├── soc/                          # SoC 层（对应 cp/middleware/soc/）
│   ├── bk7258/                   # BK7258 专有
│   │   ├── hal/                  # *_ll.c/.h（寄存器级 LL，多为 static inline 头）
│   │   │   ├── wdt_ll.c / wdt_ll.h
│   │   │   ├── aon_wdt_ll.h
│   │   │   ├── flash_ll.c / flash_ll.h
│   │   │   ├── sys_hal.c / sys_ll.h       # 时钟/power/复位
│   │   │   └── icu_ll.c / icu_ll.h        # 中断/时钟门控（按需）
│   │   └── soc/                  # 寄存器定义
│   │       ├── wdt_reg.h / wdt_struct.h
│   │       ├── aon_wdt_reg.h
│   │       ├── flash_reg.h / flash_struct.h
│   │       └── sys_reg.h / sys_struct.h / icu_reg.h
│   └── common/                   # 通用 HAL（对应 soc/common/hal/）
│       ├── hal/                  # wdt_hal.c / aon_wdt_hal.c / flash_hal_debug.c
│       │   ├── wdt_hal.c / include/wdt_hal.h
│       │   ├── aon_wdt_hal.c / include/aon_wdt_hal.h
│       │   └── include/  (hal_config.h / hal_port.h / sys_hal.h)
│       └── soc/include/          # wdt_hw.h 等（reg+struct 聚合）
├── include/                      # SDK 公共头（对应 cp/include/，最小子集）
│   ├── common/                   # bk_include.h / bk_typedef.h
│   ├── driver/                   # wdt.h / aon_wdt.h / flash.h / wdt_types.h / hal/
│   ├── os/                       # os.h / mem.h（适配声明）
│   ├── components/               # system.h（bk_get_tick/BK_MS_TO_TICKS 等）
│   └── modules/                  # chip_support.h（按需）
├── os/                           # ★OS 适配层（NuttX 侧实现，非 SDK 源码）
│   ├── os_adapt.c                # rtos_*/os_malloc/bk_get_tick → NuttX 原语
│   ├── os_adapt.h
│   └── stubs.c                   # 重依赖桩（bk_timer_start=no-op 等）
├── sdkconfig.h                   # ★SDK 内部 CONFIG 默认值（我们定制的，见 §5）
└── Make.defs                     # ★SDK 编译规则（CSRCS + INCLUDES + -D 宏）
```

### 4.1 目录组织原则

1. **镜像 SDK 子结构**：`sdk/driver/` `sdk/soc/bk7258/` `sdk/soc/common/` `sdk/include/`
   与 `$BK_AVDK/cp/middleware/` + `cp/include/` 对应，拷入时保留相对路径，include 语义不变
2. **按模块增量**：先 WDT 只建 `driver/wdt` + `driver/aon_wdt` + 对应 `soc/bk7258/hal/{wdt,aon_wdt}_ll`；
   flash 加 `driver/flash` + `soc/bk7258/hal/flash_ll`；不一次性建全
3. **共享依赖单处放置**：`driver/common/`、`driver/sys_ctrl/`、`soc/bk7258/hal/sys_hal.c`、
   `os/os_adapt.c` 是多模块复用的，建一次供所有模块 link
4. **OS 适配层独立**：`sdk/os/` 是我们写的 NuttX 适配，不混在 SDK 源里，便于区分"SDK 原样"
   vs"我们改的"
5. **sdkconfig.h 单点定义宏**：所有 `CONFIG_*` 宏的取值集中在一个头里，不散落各 Makefile

## 5. 宏隔离体系（`sdk/sdkconfig.h`）

按"只留 CP(CPU0) 硬件主路径"原则定制。集中定义，各 Make.defs `-include`。

### 5.1 芯片族宏

```c
/* 我们是 BK7258——以下芯片族守卫全部不命中，使其它芯片族代码编译掉 */
#define CONFIG_SOC_BK7258            1
/* 显式置 0，杜绝误命中 */
#define CONFIG_SOC_BK7256XX          0
#define CONFIG_SOC_BK7236XX          0
#define CONFIG_SOC_BK7239XX          0
#define CONFIG_SOC_BK7286XX          0
```

> 实测价值：`wdt_hal.c` 的 `wdt_hal_close`/`force_reboot` 整段消失；`wdt_hal_init` 里
> `hal->id` 赋值守卫不命中 → `hal->id` 保持 0（但 `WDT_LL_REG_BASE` 恒为 APB 基址，无关）。

### 5.2 功能开关宏（关闭 FreeRTOS/任务级/ISR 自动喂狗等）

```c
/* 只保留硬件 WDT 主路径 */
#define CONFIG_INT_WDT               0   /* 关 ISR 自动喂狗（NuttX automonitor 替代） */
#define CONFIG_TASK_WDT              0   /* 关 FreeRTOS 任务级看门狗 */
#define CONFIG_INT_AON_WDT           0   /* 关 AON 自动喂狗 ISR */
#define CONFIG_NMI_WDT_EN            0   /* 关 NMI WDT 分频缩放，period 直写 */
#define CONFIG_DEBUG_VERSION         0   /* release 路径 */
#define CONFIG_GPIO_RETENTION_SUPPORT 0  /* 不需要 GPIO 保持 */
#define CONFIG_AON_RTC               0   /* 仅 TASK_WDT 用，已关 */

/* 保留（CP 标准配置） */
#define CONFIG_SYSTEM_CTRL           1   /* 走 sys_driver 时钟上电路径 */
```

### 5.3 单份资源核间互斥宏（CP/CPU0 视角）

```c
/* CP(CPU0) 需要的外设在此开；不需要的关闭避免与 AP 抢 */
#define CONFIG_WDT                   1   /* CP 用 APB WDT */
#define CONFIG_AON_WDT               1   /* CP 用 AON WDT（app 接管后关） */
#define CONFIG_FLASH                 1   /* CP 用 flash */
/* #define CONFIG_TRNG  1 */            /* 若 CP 需要 TRNG 才开；单份资源，AP 不能再开 */
```

> 这类宏遵循"哪个核需要就在哪个核的 defconfig 开"——我们是单 CP 编译，等同在 `sdkconfig.h`
> 里定。若将来 AP 也跑 NuttX，AP 的 `sdkconfig.h` 关掉这些。

### 5.4 编译宏注入（`sdk/Make.defs`）

```makefile
SDKDIR := $(BOARD_DIR)/sdk

# 芯片族 + 功能 + 资源宏统一注入
SDK_CFLAGS = -include $(SDKDIR)/sdkconfig.h \
             -DCONFIG_FREERTOS=0 \        # 禁 SDK 头里的 FreeRTOS 假设
             -DCONFIG_SOC_BK7258=1

# Include 路径（镜像 SDK 结构）
SDK_INCLUDES = -I$(SDKDIR)/include \
               -I$(SDKDIR)/include/driver \
               -I$(SDKDIR)/include/driver/hal \
               -I$(SDKDIR)/soc/bk7258/hal \
               -I$(SDKDIR)/soc/bk7258/soc \
               -I$(SDKDIR)/soc/common/hal/include \
               -I$(SDKDIR)/soc/common/soc/include \
               -I$(SDKDIR)/driver/include/bk_private
```

## 6. OS 适配层（`sdk/os/os_adapt.c`）

把 SDK 内部用的 OS/RTOS 抽象重定向到 NuttX。**增量扩充**——每个模块带进的新原语按需补。

### 6.1 映射表（随模块扩充）

| SDK 抽象 | NuttX 实现 | 出现模块 |
|---|---|---|
| `os_memset` / `os_memcpy` | `memset` / `memcpy` | WDT/flash/... |
| `os_malloc` / `os_free` | `kmm_malloc` / `kmm_free` | flash/... |
| `bk_get_tick()` | `clock_systime_ticks()` | WDT(INT_AON_WDT 已关，仅桩) |
| `BK_MS_TO_TICKS(x)` | `(x) / MSEC_PER_TICK` | WDT |
| `GLOBAL_INT_DISABLE/RESTORE` | `irqsave()`/`irqrestore()` | WDT/flash |
| `__IRAM_SEC` / `.itcm_sec_code` | 空 `__attribute__((section(".itcm"))))` 或去除 | WDT |
| `bk_timer_start/stop` | **桩 no-op**（NuttX automonitor 自喂） | WDT |
| `aon_pmu_drv_wdt_rst_dev_enable` | **桩 no-op**（bootloader 已配 WDT reset） | WDT |
| `rtos_*` (thread/sem/mutex) | `kthread_create`/`nxsem_*`/`nxmutex_*` | 后续 wifi/ble 等 |

### 6.2 桩策略（`sdk/os/stubs.c`）

重依赖**一律先桩**，编译/运行验证后再决定是否换成真实现：
- `bk_timer_start(...) { return BK_OK; }` —— NuttX automonitor 替代
- `aon_pmu_drv_wdt_rst_dev_enable(...) { return BK_OK; }` —— bootloader 已配
- 跨核/AP 专属函数 —— 桩 no-op，CP 不应执行

> 桩的原则：**功能上 NuttX 已有等价物（automonitor）或 bootloader 已做完的，桩 no-op**；
> **硬件必需的（时钟上电）拷真实现**（`sys_drv_dev_clk_pwr_up` 等）。

## 7. 依赖收敛策略（每个模块通用流程）

每个新模块引入按此流程，避免盲拷：

1. **定位 API 实现**：`bk_<mod>_*` 在 `$BK_AVDK/cp/middleware/driver/<mod>/`
2. **trace 调用链**：每个要调的 API → 即时 callee → 标注 CONFIG 守卫
3. **分类依赖**：
   - (a) **CP 原生**（LL 寄存器写、`os_mem*`）→ 拷
   - (b) **共享资源需 CONFIG 互斥** → 在 `sdkconfig.h` 定归属
   - (c) **AP 专属/FreeRTOS/任务级** → 关宏裁掉
   - (d) **重依赖可桩**（timer/icu/gpio/power/reset_reason/bk_fake_clock）→ 桩 no-op
   - (e) **硬件必需**（时钟上电 `sys_drv_dev_clk_pwr_up`）→ 拷最小函数
4. **定最小文件集**：driver + hal + ll + soc reg + 必需头
5. **拷入 + 改 wrapper + 补 OS 适配 + 桩**
6. **编译收敛**：报缺什么补什么，逐层收敛
7. **板端验证**：功能等价 + 不引入回归

### 7.1 WDT 依赖收敛实例（已完成，作模板）

| 依赖 | 分类 | 处理 |
|---|---|---|
| `wdt_ll_set_period`（soft_reset+key） | (a) CP 原生 | 拷 |
| `aon_wdt_ll_set_period` | (a) CP 原生 | 拷 |
| `bk_timer_start`（启动 TIMER_ID2 自动喂狗） | (d) 重依赖可桩 | 桩 no-op（NuttX automonitor 替代） |
| `sys_drv_nmi_wdt_set_clk_div` / `sys_drv_dev_clk_pwr_up` | (e) 硬件必需 | 拷 `sys_clock_driver.c`+`sys_hal.c` 两函数 |
| `aon_pmu_drv_wdt_rst_dev_enable` | (d) bootloader 已配 | 桩 no-op 或 3 行 `aon_pmu_ll` 直写 |
| `reset_reason` / `icu_driver` / `gpio` / `power_driver` / `bk_fake_clock` | (c)/(d) | 关宏裁掉或桩 |
| `bk_get_tick` | (d) | 桩 `clock_systime_ticks` |
| **`wdt_hal_close` / `wdt_hal_close_unused`** | **链接风险** | 见 §7.2 特殊桩 |

热路径 `bk_wdt_feed` 收敛后 = `wdt_ll_set_period`（soft_reset+2 key 写），零重依赖。

### 7.2 WDT 特殊桩：`close_wdt` / `wdt_hal_close`（两轮分析均点名）

- `wdt_hal_close_unused` / `wdt_hal_close` / `wdt_hal_force_reboot` 在 `wdt_hal.c` 内被
  `#if (CONFIG_SOC_BK7236XX)||(CONFIG_SOC_BK7239XX)||(CONFIG_SOC_BK7286XX)` 守卫包住，
  **BK7258 编译掉**（芯片族宏全置 0）
- 但 `bk_wdt_stop` → `wdt_deinit_common` → `close_wdt()`（`wdt_driver.c:263`）会调
  `wdt_hal_close()` → **BK7258 下该符号不存在 → 链接失败**
- **处理**：`bk_wdt_stop` 在 BK7258 等价于"复位 ctrl.key=0 / ctrl.period=默认"（`wdt_ll_reset_config_to_default`，已编译）。
  两种做法二选一：
  - (i) `sdk/os/stubs.c` 补 `bk_err_t wdt_hal_close(...) { return BK_OK; }` 空实现（关狗交给
    `wdt_ll_reset_config_to_default`，APB 关狗语义够了）
  - (ii) wrapper 的 `stop` op 不调 `bk_wdt_stop`，改调 `bk_aon_wdt_stop` + 自己 `wdt_ll` 关 APB
- 推荐 (i)（保留 SDK API 完整性，wrapper 仍薄转发）。实施时验证链接通过。
- 复核确认点：`soc/bk7258/hal/wdt_ll.c` 是否空文件（LL 多为头内 static inline，.c 可能仅占位）

## 8. NuttX wrapper 规范（`chip/common/` + `chip/cp/`）

每个模块的 NuttX lower-half 遵循 `nuttx-driver-development` skill 对应 pattern
（WDT→`wdg_pattern.md`，flash→mtd，serial→serial_pattern 等）：

1. `#include <driver/<mod>.h>`（SDK 公开头）
2. `struct bk7258_<mod>_lowerhalf_s` 私有态（`*_lowerhalf_s` 必须首字段，cast-compatible）
3. `ops` 每个回调薄转发到 `bk_<mod>_*`，零 `putreg32/getreg32`
4. 输入校验在 wrapper 做（如 `settimeout==0` 返回 `-EINVAL`）
5. `*_initialize()`：单例 guard + 必要的 SDK init + `*_register()` 检查返回
6. 共享态访问加 `enter/leave_critical_section`

## 9. Kconfig / 构建集成

### 9.1 `chip/Kconfig`（每模块一个 choice，过渡期 raw/sdk 共存可回退）

```kconfig
choice
    prompt "BK7258 WDT implementation"
    default BK7258_WDT_SDK
config BK7258_WDT_RAW
    bool "Register-level WDT (legacy)"
config BK7258_WDT_SDK
    bool "SDK-wrapped WDT (bk_wdt_*/bk_aon_wdt_*)"
    select BK7258_SDK_WDT
endchoice
```

### 9.2 `chip/Make.defs`

```makefile
ifeq ($(CONFIG_BK7258_WDT_SDK),y)
CHIP_CSRCS += bk7258_wdt.c            # wrapper
include $(CHIP_DIR)/../sdk/Make.defs  # 拉入 SDK 源码 + INCLUDES + -D
endif
```

### 9.3 `sdk/Make.defs`（按模块累积 CSRCS）

```makefile
# WDT 模块
ifeq ($(CONFIG_BK7258_SDK_WDT),y)
  SDK_CSRCS += driver/wdt/wdt_driver.c
  SDK_CSRCS += driver/aon_wdt/aon_wdt_driver.c
  SDK_CSRCS += soc/common/hal/wdt_hal.c
  SDK_CSRCS += soc/common/hal/aon_wdt_hal.c
  SDK_CSRCS += os/os_adapt.c os/stubs.c
  # sys_clock 按需
  SDK_CSRCS += driver/sys_ctrl/sys_clock_driver.c
  SDK_CSRCS += soc/bk7258/hal/sys_hal.c
endif
# flash 模块（后续追加）
ifeq ($(CONFIG_BK7258_SDK_FLASH),y)
  ...
endif

# 统一注入到 CHIP_CSRCS / CFLAGS / VPATH
CHIP_CSRCS += $(addprefix $(SDKDIR)/, $(SDK_CSRCS))
CFLAGS += $(SDK_CFLAGS) $(SDK_INCLUDES)
VPATH += $(SDKDIR)/driver/wdt $(SDKDIR)/soc/common/hal ...
```

## 10. 推进路线（armino_as_lib 模式）

### 10.1 阻塞项（需用户操作）

**当前阻塞**：BK7258 SDK 未编译，`arm-none-eabi-gcc` 工具链未安装。

**解除阻塞的步骤**：
1. **安装工具链**：`sudo apt install gcc-arm-none-eabi` 或从 ARM 官网下载
2. **编译 SDK**：
   ```bash
   cd $BK_AVDK
   ./tools/build_tools/build.sh . projects/app build bk7258
   ```
3. **打包 armino_as_lib**：
   ```bash
   ./tools/build_tools/armino_as_lib.sh bk7258 . build/bk7258 projects/app
   ```
4. **拷进工程**：
   ```bash
   cp -r build/armino_as_lib/bk7258 \
         $CONTEST/board/bk7258/bk_idk/armino_as_lib/
   ```

> **替代方案**：如果编译环境不便，可以问 Beken 是否有现成的 BK7258 `armino_as_lib`
> （类似 7236N 已打包好的），或在 CI/Docker 环境编译后拷贝产物。

### 10.2 推进路线

| 阶段 | 任务 | 前置条件 | 状态 |
|---|---|---|---|
| **0** | **编译 SDK + 打包 armino_as_lib** | 工具链安装 | **阻塞中（需用户操作）** |
| 1 | 拷 `armino_as_lib/bk7258/` 进工程 + 写 `bk7258_os_adapt.c`（WDT/flash 子集） | 阶段 0 完成 | 待启动 |
| 2 | **WDT SDK 适配**（改 wrapper 调 `bk_wdt_*` + `bk_aon_wdt_*`，修 AON 根因） | 阶段 1 完成 | 设计完成（`b2-wdt-fix.md`） |
| 3 | **flash SDK 适配**（改 wrapper 调 `bk_flash_*`） | 阶段 1 完成 | 设计完成（`n6a-sdk-integration-research.md`） |
| 4 | UART → `bk_uart_*` | 阶段 1 完成 | 后续 |
| 5 | DVFS → `sys_drv_switch_cpu_bus_freq` | 阶段 1 完成 | 后续 |
| 6 | timer/gpio/... | 阶段 1 完成 | 后续 |

每阶段产物：wrapper 改写、`bk7258_os_adapt.c` 扩充、板端验证、
`docs/bk7258-t5ai/nuttx-port/n6<mod>-*.md` 记录。

### 10.3 与之前方案的对比

| 维度 | 之前方案（拷源码） | 现在方案（armino_as_lib） |
|---|---|---|
| SDK 引入方式 | 从 `$BK_AVDK/cp/middleware/` 手挑源码拷入 `sdk/` | 编译 SDK 产出预编译库，链接 `libs/*.a` |
| 依赖处理 | 需逐个分析依赖链、桩化重依赖 | `libdriver.a` 已包含全部实现，无需处理 |
| OS 适配 | 需自己写 `os_adapt.c` | 参考 7236N 的 `beken_os_adapt.c`（~1900 行范本） |
| 宏隔离 | 需自己定制 `sdkconfig.h` | SDK 编译时已确定，`config/sdkconfig.h` 带入 |
| 与官方一致性 | 自定义方案 | 与 7236N 完全一致的官方模式 |
| 阻塞项 | 无（可立即开始） | 需先编译 SDK（工具链） |

**结论**：`armino_as_lib` 方案更干净、更官方、更易维护，值得先解除阻塞（装工具链编译 SDK）。

## 11. 不变约束

- **不修改 nuttx 官方树**（见 [[do-not-modify-nuttx-official-tree]]）：所有改动只在
  `$CONTEST/board/bk7258/` overlay 内
- **bootloader 保留手写**：`bootloader/` 不动（自定义分区布局，B2 产品级已板端验证）
- **CP 路径**：SDK 源码一律取 `$BK_AVDK/cp/`，不碰 `ap/`
- **AP/CP 三核正确**：芯片族宏显式置 0、单份资源宏按 CP 归属开、AP 专属路径关宏裁掉

## 12. 相关文件索引

**框架文档**：
- `$CONTEST/board/bk7258/sdk/`（待建，按 §4）
- `$CONTEST/board/bk7258/sdk/sdkconfig.h`（待建，按 §5）
- `$CONTEST/board/bk7258/sdk/os/os_adapt.c`（待建，按 §6）
- `$CONTEST/board/bk7258/sdk/Make.defs`（待建，按 §9.3）

**模块文档**：
- `$CONTEST/docs/bk7258-t5ai/nuttx-port/n6a-sdk-integration-research.md` — flash SDK 集成调研
- `$CONTEST/docs/bk7258-t5ai/nuttx-port/b2-wdt-fix.md` — WDT SDK 全量适配（首个落地）

**SDK 源**：
- `$BK_AVDK/cp/middleware/driver/` — CP 驱动源
- `$BK_AVDK/cp/middleware/soc/bk7258/` — BK7258 HAL/LL/reg
- `$BK_AVDK/cp/include/` — CP 公共头
- `$BK_AVDK/cp/include/soc/bk7258/reg_base.h` — `SOC_WDT_REG_BASE=0x44800000`、`SOC_AON_WDT_REG_BASE=0x44000600`

**参考实现**：
- `$VENDOR_BEKEN/chips/bk7236n/Make.defs` — `-DCONFIG_*=0` + `EXTRA_LIBS` 整库链接范本
- `$VENDOR_BEKEN/chips/bk7236n/beken_os_adapt.c` — OS 适配层范本（~1900 行，我们增量扩充）

**NuttX 参考**：
- `nuttx/drivers/timers/watchdog.c` — automonitor 在 register 时启动
- `.claude/skills/nuttx-driver-development/references/wdg_pattern.md` — WDT lower-half 契约

**记忆引用**：
- `bk7258-nuttx-boot-core-verified` — NuttX 跑在 CPU0=CP
- `bk7258-b2-bootloader-wdt` — bootloader WDT（APB+AON，跳 app 前不关）
- `do-not-modify-nuttx-official-tree` — overlay 边界
