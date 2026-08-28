# BK7258 NuttX SDK 集成架构设计

> **日期**：2026-07-20
> **状态**：设计完成，待实施
> **范围**：定义 BK7258 NuttX 适配引入 Beken SDK 的完整架构——三核 AMP 编译模型、
> armino_as_lib 预编译库集成、OS 适配层、Wrapper 层、构建系统、镜像组装。
> **核心决策**：全面转 SDK——底层不再从零手写寄存器，NuttX 侧只写薄 wrapper 调 `bk_*` API。
>
> **归档说明（2026-07-31）：** 本文保留最初设计时的单层
> `armino_as_lib/{cp,ap}` 路径用于追溯；当前实现已统一为
> `armino_as_lib/versions/<version>/{cp,ap}`，默认 `v3.1.1.9`。当前操作请以
> `docs/platforms/bk7258/nuttx-port/sdk-static-library-import.md` 为准。

---

## 1. 背景

### 1.1 问题

BK7258 NuttX 适配初期所有底层驱动（UART/flash/DVFS/WDT）从寄存器级手写，踩了大量 SDK 已解决的坑：
- UART：4 个 RX 中断叠加 bug
- Flash：32B 对齐、SR0 保护问题
- DVFS：DPLL 320MHz 调数天
- WDT：AON WDT 无人喂导致无限重启

### 1.2 决策

全面转 SDK，参考 Beken 官方 7236N 的 `armino_as_lib` 模式（`tao.yang@bekencorp.com` 维护）。

### 1.3 参考实现

- **Beken 官方 SDK** (`bk_avdk_smp`)：CP/AP 分离编译，`armino_as_lib.sh` 打包预编译库
- **7236N vendor_beken**：NuttX 薄 wrapper + `beken_os_adapt.c`（~1900 行 FreeRTOS→NuttX shim）
- **Tuya SDK** (`t5_os`)：CP/AP 分离目录，`tkl_*.c` HAL 适配层，`create_ua_file.py` 镜像合并

---

## 2. 架构设计

### 2.1 三核 AMP 架构

```
┌─────────────────────────────────────────────────────────────┐
│                    BK7258 三核 AMP NuttX                     │
├──────────────────────────┬──────────────────────────────────┤
│  CP (CPU0)               │  AP (CPU1+CPU2 SMP)              │
│  ┌──────────────────┐    │  ┌──────────────────────────┐    │
│  │ NuttX (独立实例)  │    │  │ NuttX SMP (共享实例)      │    │
│  │ - Wi-Fi          │    │  │ - 多媒体                  │    │
│  │ - BLE            │    │  │ - 客户应用                │    │
│  │ - 基础外设       │    │  │ - 高级外设                │    │
│  └────────┬─────────┘    │  └────────────┬─────────────┘    │
│           │              │               │                   │
│  ┌────────▼─────────┐    │  ┌────────────▼─────────────┐    │
│  │ armino_as_lib/cp/ │    │  │ armino_as_lib/ap/         │    │
│  │ libs/*.a          │    │  │ libs/*.a                  │    │
│  └──────────────────┘    │  └──────────────────────────┘    │
├──────────────────────────┴──────────────────────────────────┤
│  Bootloader：一个（CP+AP 共享）                              │
│  核间通信：RPMsg / 共享内存 / 信箱                           │
│  最终镜像：all-app.bin = bootloader + CP app + AP app        │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 分层架构

```
┌─────────────────────────────────────────────────────────────┐
│ NuttX 应用 / NSH / 文件系统 / 网络                          │
├─────────────────────────────────────────────────────────────┤
│ NuttX upper-half（drivers/timers/watchdog.c, mtd, serial…） │
├─────────────────────────────────────────────────────────────┤
│ NuttX lower-half wrapper（chip/cp/bk7258_cp_*.c）           │
│   薄转发：ops->start = bk_wdt_start(timeout) 等            │
├─────────────────────────────────────────────────────────────┤
│ Beken SDK 预编译库（bk_idk/armino_as_lib/cp/libs/*.a）      │
│   libdriver.a（全部外设驱动实现）                            │
├─────────────────────────────────────────────────────────────┤
│ OS 适配层（chip/cp/bk7258_cp_os_adapt.c）                   │
│   rtos_*→NuttX 原语  os_malloc→kmm_malloc  等              │
└─────────────────────────────────────────────────────────────┘
```

### 2.3 分区布局

```
物理地址          大小      内容
0x00000           68KB      bootloader (bl_crc.bin)
0x11000           1360KB    CP app (nuttx_cp_crc.bin)
0x161000          1156KB    AP app (nuttx_ap_crc.bin)
...               ...       ota, usr_config, easyflash, ...
0x7fa000          8KB       easyflash (CP)
0x7fc000          8KB       easyflash_ap (AP)
0x7fe000          4KB       sys_rf
0x7ff000          4KB       sys_net
```

### 2.4 RAM 布局

```
SRAM (0x28000000, 640KB):
  AP_SPINLOCK:  0x28000000, 64KB
  AP_RAM:       0x28010000, 352KB  (AP 使用)
  CP_RAM:       0x28068000, 222KB  (CP 使用)
  PWR_MNG:      尾部 256B
  SWAP:         尾部 2KB
```

---

## 3. SDK 集成：armino_as_lib 模式

### 3.1 编译流程

```bash
# 编译 SDK（CP 配置）
cd $BK_AVDK
./tools/build_tools/build.sh . projects/app build bk7258
# → 产出 build/bk7258/（编译产物）

# 打包 armino_as_lib
./tools/build_tools/armino_as_lib.sh bk7258 . build/bk7258 projects/app
# → 产出 build/armino_as_lib/bk7258/

# 拷到工程
cp -r build/armino_as_lib/bk7258 \
      $CONTEST/board/bk7258/bk_idk/armino_as_lib/cp/
```

### 3.2 armino_as_lib 产出结构

```
armino_as_lib/bk7258/
├── libs/              # ~30 个预编译 .a
│   ├── libdriver.a    # 全部外设驱动（UART/flash/WDT/timer/SPI/I2C/...）
│   ├── libbk_system.a # 系统初始化（sys_drv_init, sys_hal_early_init）
│   ├── libbk_pm.a     # 电源管理/DVFS
│   ├── libbk_rtos.a   # RTOS 抽象层
│   ├── libbk_init.a   # 初始化框架
│   ├── libcm33.a      # Cortex-M33 支持
│   ├── libcmsis.a     # CMSIS
│   ├── libwifi.a      # Wi-Fi 协议栈
│   ├── libbluetooth_*.a # BLE 协议栈
│   └── ...
├── config/
│   ├── sdkconfig.h           # SDK 内部 CONFIG 默认值
│   └── sdkconfig.h.properties
└── include/           # SDK 全局头
    ├── common/        # bk_include.h
    ├── driver/        # wdt.h, flash.h, uart.h, ...
    ├── os/            # os.h, mem.h
    ├── components/    # system.h
    └── modules/       # chip_support.h
```

### 3.3 AP 的 armino_as_lib

AP 需要单独编译（不同的 CONFIG）：
```bash
# 编译 SDK（AP 配置）
./tools/build_tools/build.sh . projects/app build bk7258_ap
./tools/build_tools/armino_as_lib.sh bk7258_ap . build/bk7258_ap projects/app
cp -r build/armino_as_lib/bk7258_ap \
      $CONTEST/board/bk7258/bk_idk/armino_as_lib/ap/
```

---

## 4. OS 适配层

### 4.1 职责

让 SDK 预编译库（`libdriver.a` 等）内部的 FreeRTOS API 调用能被 NuttX 正确执行。

### 4.2 核心映射表

| SDK 内部调用 | NuttX 实现 |
|---|---|
| `rtos_create_thread(...)` | `kthread_create()` |
| `rtos_init_semaphore(...)` | `nxsem_init()` |
| `rtos_get_semaphore(...)` | `nxsem_wait()` |
| `rtos_set_semaphore(...)` | `nxsem_post()` |
| `rtos_init_mutex(...)` | `nxmutex_init()` |
| `rtos_mutex_lock(...)` | `nxmutex_lock()` |
| `rtos_mutex_unlock(...)` | `nxmutex_unlock()` |
| `os_malloc(...)` | `kmm_malloc()` |
| `os_free(...)` | `kmm_free()` |
| `os_calloc(...)` | `kmm_calloc()` |
| `os_realloc(...)` | `kmm_realloc()` |
| `os_memset(...)` | `memset()` |
| `os_memcpy(...)` | `memcpy()` |
| `rtos_get_ms_per_tick()` | `MSEC_PER_TICK` |
| `bk_get_tick()` | `clock_systime_ticks()` |
| `GLOBAL_INT_DISABLE()` | `irqsave()` |
| `GLOBAL_INT_RESTORE()` | `irqrestore()` |
| `rtos_delay_milliseconds(...)` | `nxsig_usleep()` |
| `vTaskSuspendAll()` | `sched_lock()` |
| `xTaskResumeAll()` | `sched_unlock()` |

### 4.3 实现要点

- 函数签名必须与 SDK 预编译库期望的一致
- FreeRTOS 特有概念需转换：`portMAX_DELAY`→`UINT32_MAX`，栈大小 字→字节（×4）
- 中断上下文：`xSemaphoreTakeFromISR`→`nxsem_trywait`
- 参考 7236N 的 `beken_os_adapt.c`（~1900 行完整范本）

### 4.4 文件位置

```
chip/cp/bk7258_cp_os_adapt.c    # CP 的 FreeRTOS→NuttX shim
chip/ap/bk7258_ap_os_adapt.c    # AP 的 FreeRTOS→NuttX shim（后续）
```

---

## 5. Wrapper 层

### 5.1 设计原则

1. **零寄存器操作**——wrapper 里不出现 `putreg32`/`getreg32`
2. **薄转发**——每个 ops 回调就是 1-3 行 SDK API 调用
3. **输入校验在 wrapper 做**——如 `settimeout(0)` 返回 `-EINVAL`
4. **遵循 skill pattern**——WDT 用 `wdg_pattern.md`，flash 用 MTD pattern

### 5.2 Wrapper 文件清单

```
chip/cp/
├── bk7258_cp_serial.c         # UART：struct uart_ops_s → bk_uart_*
├── bk7258_cp_flash_mtd.c      # Flash：struct mtd_dev_s → bk_flash_*
├── bk7258_cp_wdt.c            # WDT：struct watchdog_ops_s → bk_wdt_* + bk_aon_wdt_*
├── bk7258_cp_timer.c          # Timer：struct timer_lowerhalf_s → bk_timer_*
├── bk7258_cp_spi.c            # SPI：struct spi_dev_s → bk_spi_*
├── bk7258_cp_i2c.c            # I2C：struct i2c_master_s → bk_i2c_*
├── bk7258_cp_gpio.c           # GPIO → bk_gpio_*
├── bk7258_cp_adc.c            # ADC → bk_adc_*
├── bk7258_cp_pwm.c            # PWM → bk_pwm_*
└── bk7258_cp_rtc.c            # RTC → bk_rtc_*
```

### 5.3 WDT Wrapper 示例

```c
#include <nuttx/timers/watchdog.h>
#include <driver/wdt.h>        // SDK API
#include <driver/aon_wdt.h>   // SDK API

static int bk7258_wdt_start(struct watchdog_lowerhalf_s *lower) {
  struct bk7258_wdt_lowerhalf_s *priv = (void *)lower;
  if (!bk_wdt_is_driver_inited()) bk_wdt_driver_init();
  return (bk_wdt_start(priv->timeout) == BK_OK) ? OK : -EIO;
}

static int bk7258_wdt_stop(struct watchdog_lowerhalf_s *lower) {
  bk_wdt_stop();
  bk_aon_wdt_stop();  // 关 AON WDT，修根因
  return OK;
}

static int bk7258_wdt_keepalive(struct watchdog_lowerhalf_s *lower) {
  return (bk_wdt_feed() == BK_OK) ? OK : -EIO;
}

int bk7258_wdt_initialize(void) {
  bk_aon_wdt_stop();  // ★根因修复：立刻关 AON WDT
  bk_wdt_driver_init();
  // ... register /dev/watchdog0 ...
}
```

### 5.4 Flash Wrapper 示例

```c
#include <nuttx/mtd/mtd.h>
#include <driver/flash.h>  // SDK API

static ssize_t bk7258_flash_bread(struct mtd_dev_s *dev, off_t startblock,
                                   size_t nblocks, uint8_t *buffer) {
  uint32_t offset = startblock * 4096;
  return (bk_flash_read_bytes(offset, buffer, nblocks * 4096) == BK_OK)
         ? nblocks : -EIO;
}

static int bk7258_flash_erase(struct mtd_dev_s *dev, off_t startblock,
                                size_t nblocks) {
  bk_flash_set_protect_type(FLASH_PROTECT_NONE);
  for (size_t i = 0; i < nblocks; i++)
    bk_flash_erase_sector((startblock + i) * 4096);
  bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK);
  return OK;
}
```

---

## 6. 构建系统

### 6.1 CP 的 chip/Make.defs

```makefile
include armv8-m/Make.defs
LDFLAGS += --build-id=none --entry=__start

BK_IDK_PATH = ../../../../board/bk7258/bk_idk

# SDK 头文件（CP）
INCLUDES += ${INCDIR_PREFIX}$(BK_IDK_PATH)/armino_as_lib/cp/include
INCLUDES += ${INCDIR_PREFIX}$(BK_IDK_PATH)/armino_as_lib/cp/config

# OS 适配层
CHIP_CSRCS = bk7258_cp_os_adapt.c

# 基础 wrapper
CHIP_CSRCS += bk7258_cp_start.c bk7258_cp_irq.c bk7258_cp_lowputc.c
CHIP_CSRCS += bk7258_cp_allocateheap.c bk7258_cp_timerisr.c

# 外设 wrapper（按 CONFIG 门控）
ifeq ($(CONFIG_SERIAL_CONSOLE),y)
CHIP_CSRCS += bk7258_cp_serial.c
endif
ifeq ($(CONFIG_WATCHDOG),y)
CHIP_CSRCS += bk7258_cp_wdt.c
endif
ifeq ($(CONFIG_BK7258_FLASH_MTD),y)
CHIP_CSRCS += bk7258_cp_flash_mtd.c
endif
```

### 6.2 CP 的 scripts/Make.defs

```makefile
include $(TOPDIR)/.config
include $(TOPDIR)/tools/Config.mk
include $(TOPDIR)/arch/arm/src/armv8-m/Toolchain.defs

ARCHSCRIPT = $(BOARD_DIR)$(DELIM)scripts$(DELIM)cp$(DELIM)ld.script

# 链接 CP 预编译库
EXTRA_LIBS += $(wildcard $(shell readlink -f $(TOPDIR)/$(CONFIG_ARCH_CHIP_CUSTOM_DIR)/bk_idk/armino_as_lib/cp/libs)/*.a)

# POSTBUILD
define POSTBUILD
    $(Q)$(BOARD_DIR)$(DELIM)scripts$(DELIM)postbuild.sh $(TOPDIR) $(BOARD_DIR)
endef
```

### 6.3 Kconfig

```kconfig
config BK7258_CP
    bool "BK7258 CP (CPU0) support"
    default y
    depends on ARCH_CHIP_CUSTOM
    select ARCH_CORTEXM33

config BK7258_AP
    bool "BK7258 AP (CPU1+CPU2 SMP) support"
    default n
    depends on ARCH_CHIP_CUSTOM
    select ARCH_CORTEXM33
    select SMP
```

---

## 7. 镜像组装

### 7.1 postbuild.sh

```bash
#!/usr/bin/env bash
# 组装：bootloader + CP app + AP app → all-app.bin

TOPDIR=$1
BOARD_DIR=$2
WS_DIR="$(cd "${TOPDIR}/.." && pwd)"

BL_CRC="${BOARD_DIR}/bootloader/bl_crc.bin"
CP_BIN="${TOPDIR}/nuttx.bin"
PACKER="${WS_DIR}/../TuyaOpen/zephyr-bk7258-port/tools/bk7258_crc_expand_app.py"

# CRC 扩展
python3 ${PACKER} --in ${CP_BIN} --out ${TOPDIR}/nuttx_cp_crc.bin

# 拼接（bootloader 68KB + CP 1360KB）
cat ${BL_CRC} ${TOPDIR}/nuttx_cp_crc.bin > ${TOPDIR}/all-app.bin

# AP 后续加：
# python3 ${PACKER} --in ${AP_BIN} --out ${TOPDIR}/nuttx_ap_crc.bin
# dd if=${BL_CRC}              of=all-app.bin bs=1 seek=0         conv=notrunc
# dd if=nuttx_cp_crc.bin       of=all-app.bin bs=1 seek=$((0x11000)) conv=notrunc
# dd if=nuttx_ap_crc.bin       of=all-app.bin bs=1 seek=$((0x161000)) conv=notrunc
```

### 7.2 链接脚本

**CP ld.ld**（已有）：
```
FLASH: ORIGIN = 0x02010000, LENGTH = 0x100000  (1MiB)
RAM:   ORIGIN = 0x28000000, LENGTH = 0xA0000   (640KB)
```

**AP ld.ld**（后续）：
```
FLASH: ORIGIN = 0x02161000, LENGTH = 0x120000  (1156KB)
RAM:   ORIGIN = 0x28010000, LENGTH = 0x58000   (352KB，AP_RAM 区域)
```
> 注：AP_RAM 起始地址参考 Tuya `ram_regions.csv`（AP_SPINLOCK 64KB 后 = 0x28010000）。
> 实际地址需根据 SDK 分区表和 NuttX SMP 配置确认。

---

## 8. 工程目录结构

```
board/bk7258/
├── bootloader/                  # 一个（CP+AP 共享）
├── chip/
│   ├── cp/                      # CP 侧
│   │   ├── bk7258_cp_start.c
│   │   ├── bk7258_cp_irq.c
│   │   ├── bk7258_cp_lowputc.c
│   │   ├── bk7258_cp_serial.c
│   │   ├── bk7258_cp_wdt.c
│   │   ├── bk7258_cp_flash_mtd.c
│   │   ├── bk7258_cp_timerisr.c
│   │   ├── bk7258_cp_allocateheap.c
│   │   ├── bk7258_cp_os_adapt.c
│   │   ├── Make.defs
│   │   └── Kconfig
│   └── ap/                      # AP 侧（后续）
│       ├── bk7258_ap_start.c
│       ├── bk7258_ap_os_adapt.c
│       ├── Make.defs
│       └── Kconfig
├── bk_idk/
│   └── armino_as_lib/
│       ├── cp/                  # CP 预编译库
│       │   ├── libs/
│       │   ├── config/
│       │   └── include/
│       └── ap/                  # AP 预编译库（后续）
│           ├── libs/
│           ├── config/
│           └── include/
├── configs/
│   ├── nsh_cp/defconfig
│   └── nsh_ap/defconfig         # 后续
├── scripts/
│   ├── cp/
│   │   ├── Make.defs
│   │   ├── ld.script
│   │   └── postbuild.sh
│   └── ap/                      # 后续
│       ├── Make.defs
│       └── ld.script
└── src/
    ├── cp/bk7258_cp_bringup.c
    └── ap/bk7258_ap_bringup.c   # 后续
```

---

## 9. 调试策略

### 9.1 阶段式推进

| 阶段 | 目标 | 验证标准 |
|---|---|---|
| **0** | 编译 SDK 产出 armino_as_lib/cp/ | `libs/*.a` 存在，`config/sdkconfig.h` 存在 |
| **1** | CP 最小启动（UART console） | NSH 提示符，`help`/`uname -a` 可用 |
| **2** | CP WDT 适配 | `sleep 12` 不重启（AON WDT 已关） |
| **3** | CP Flash 适配 | `/dev/mtdblock0` + `/data` + LittleFS 持久化 |
| **4** | CP 其他外设 | timer/SPI/I2C/GPIO/ADC 按需 |
| **5** | AP 最小启动 | AP 镜像编译、启动、UART console |
| **6** | AP 外设 | LCD/audio/多媒体按需 |
| **7** | 双镜像组装 | all-app.bin = bootloader + CP + AP，三核同时运行 |

### 9.2 当前阻塞

- **阶段 0 阻塞**：需要编译 SDK 产出 `armino_as_lib`（工具链已有：`/usr/bin/arm-none-eabi-gcc` 10.3.1）
- **WDT 根因**：AON WDT 无人喂（bootloader armed 后 app 无处理）→ 进 app ~8s 重启
  - 最小修复：`bk7258_wdt_initialize()` 调 `bk_aon_wdt_stop()` 关 AON WDT
  - 完整修复：WDT wrapper 转 SDK（`bk_wdt_*` + `bk_aon_wdt_*`）

---

## 10. 共享资源处理

单份外设（如 TRNG）通过 CONFIG 宏互斥分配到某个核：

| 资源 | CP defconfig | AP defconfig | 说明 |
|---|---|---|---|
| TRNG | `CONFIG_TRNG=y` | `CONFIG_TRNG=n` | CP 用，AP 不用 |
| WDT | `CONFIG_WDT=y` | `CONFIG_WDT=y` | 各核有自己的 WDT |
| Flash | `CONFIG_FLASH=y` | `CONFIG_FLASH=y` | 共享 flash，分区隔离 |
| UART | `CONFIG_UART0=y` | `CONFIG_UART1=y` | 各核用不同 UART |

---

## 11. Bootloader 改进计划

### 11.1 启动链

BK7258 完整启动链：
```
BootROM（芯片内部固化，闭源）
  ↓ 验证 bootloader（CRC + magic）
Bootloader（我们逆向实现，~2.3KB 代码，CRC 扩展后 68KB）
  ↓ 冷启动 DPLL + WDT 初始化
  ↓ 验证 app（MSP/Reset/BK7236 magic）
  ↓ 跳转到 app
App（NuttX CP 或 AP 镜像）
```

**BootROM 的角色**：
- 芯片上电后首先执行 BootROM（固化在芯片内部）
- BootROM 验证 flash 上的 bootloader（CRC 校验 + magic 验证）
- 验证通过后跳转到 bootloader
- BootROM 不关心 app，只验证 bootloader

**原厂 bootloader 对比**：

| Bootloader | 大小 | 来源 |
|---|---|---|
| Tuya | 31584 bytes (~31KB) | `t5_os/cp/components/bk_libs/bk7258/bootloader/normal_bootloader/` |
| Beken 官方 | 52352 bytes (~52KB) | `bk_avdk_smp/cp/components/bk_libs/bk7258/bootloader/normal_bootloader/` |
| 我们的 | 2336 bytes (~2.3KB) | 自己逆向实现（CRC 扩展后 68KB） |

Tuya 和 Beken 官方大小不同（31KB vs 52KB），说明它们是不同的实现。它们包含的功能远超我们的 2.3KB：
- FAL 分区表从 flash 动态读取（不是硬编码）
- OTA/A-B failover 支持
- Flash CRC 验证
- 安全启动（可能）
- 更完整的 WDT/时钟管理
- 多 app 支持（CP+AP）

我们的 bootloader 只做了最小集（WDT + DPLL + 硬编码分区表 + app 验证 + 跳转），
但已板端验证工作正常。后续需要扩展支持 CP+AP 双 app 跳转。

我们的 bootloader 匹配的是原厂的打包格式（`bk7236_pack_min_bootloader.py` 逆向自原厂）。

### 11.2 现状

当前 bootloader（`board/bk7258/bootloader/`）是逆向工程实现，已验证的功能：
- UART1 日志（460800 波特率）
- WDT 初始化（APB + AON 双狗，8s 超时）
- 冷启动 DPLL 使能 + SPI 重校准（3 次重试）
- FAL 分区表解析 → 定位 "app"
- App 头验证（MSP/Reset Thumb/BK7236 magic）
- 跳转到 app

### 11.2 需要改进的问题

**问题 1：FAL 分区表需要扩展**

当前 `boot_main.c` 的分区表只有 2 个分区：
```c
{ "bootloader", 0x00000, 0x10000 },  // 64KB logical（CRC 扩展后 68KB physical）
{ "app",        0x10000, 0x10000 },  // 64KB — 太小，NuttX 约 70KB+
```

说明：
- bootloader 实际代码只有 ~2.3KB（`bl.bin`），但 CRC 扩展后 `bl_crc.bin` = 68KB
- `BOOTLOADER_LOGICAL_SIZE = 0x10000`（64KB logical）是正确的，CRC 扩展比 34/32 → 68KB physical
- FAL 分区表用的是 **logical 地址**，bootloader 分区 64KB 是对的

需要扩展为支持 CP+AP 的分区表：
```c
{ "bootloader", 0x00000, 0x10000 },  // 64KB logical（CRC 扩展后 68KB physical）
{ "cp_app",     0x10000, 0x158000 }, // 1360KB logical（CP NuttX 镜像）
{ "ap_app",     0x168000, 0x124000 },// 1156KB logical（AP NuttX 镜像）
```
> 注：logical 地址需要根据 CRC 扩展后的实际偏移计算确认。

**问题 2：只支持单 app，需要支持 CP+AP 双 app**

当前 bootloader 只找一个 "app" 并跳转。三核 AMP 需要：
- 方案 A：bootloader 只跳 CP，CP 负责唤醒 AP（推荐，与 SDK 一致）
- 方案 B：bootloader 依次跳 CP 和 AP

推荐方案 A：bootloader 只跳 CP app，CP app 的 `bk7258_cp_start.c` 负责通过 `start_cpu1_core()` 唤醒 AP。

**问题 3：bootloader 大小确认**

`bl_crc.bin` = 69632 bytes = 68KB。CRC 扩展后的物理大小需要与 SDK 的 68KB 分区对齐。
需要确认：CRC 扩展是否正好填满 68KB？还是需要 padding？

### 11.3 改进计划

| 阶段 | 任务 | 优先级 |
|---|---|---|
| 1 | 修正 FAL 分区表（bootloader=68KB, cp_app=1360KB, ap_app=1156KB） | 高 |
| 2 | 确认 bl_crc.bin 物理大小与 68KB 分区对齐 | 高 |
| 3 | bootloader 只跳 CP app（方案 A） | 高 |
| 4 | CP app 负责唤醒 AP（`start_cpu1_core()`） | 后续 |
| 5 | WDT 关狗时机优化（跳 app 前可选关 AON） | 低 |

### 11.4 Bootloader 设计约束

- **全流程自己控制**：官方 bootloader 闭源，不使用
- **逆向工程为基础**：当前实现已验证（N1/N2/B2 板端通过），在此基础上改进
- **分区表可配置**：支持 CP+AP 双 app，分区大小与 SDK 标准对齐
- **WDT 保留**：bootloader 阶段 WDT 兜底 cold-init 挂死，app 接管后关 AON

**决策**：保留我们自己的 bootloader（全流程自己控制，官方 bootloader 闭源不可用）。
重点改进：修正分区表、支持 CP+AP 双 app 跳转。

---

## 12. 文件索引

**设计文档**：
- `docs/superpowers/specs/2026-07-20-sdk-integration-design.md`（本文档）
- `docs/platforms/bk7258/nuttx-port/n6-sdk-integration-framework.md`（框架设计）
- `docs/platforms/bk7258/nuttx-port/n6a-sdk-integration-research.md`（flash 调研）
- `docs/platforms/bk7258/nuttx-port/b2-wdt-fix.md`（WDT 审查与修复）

**SDK 源**：
- `$BK_AVDK` (`/home/lijian/project/armino/bk_avdk_smp`) — Beken 官方 SDK
- `$BK_AVDK/tools/build_tools/armino_as_lib.sh` — 打包脚本
- `$BK_AVDK/cp/middleware/` — CP 驱动源
- `$BK_AVDK/projects/app/partitions/bk7258/auto_partitions.csv` — 分区表

**参考实现**：
- `$VENDOR_BEKEN/chips/bk7236n/` — 7236N NuttX 适配（armino_as_lib 模式）
- `$VENDOR_BEKEN/chips/bk7236n/beken_os_adapt.c` — OS 适配层范本（~1900 行）
- `$TUYAOPEN/platform/T5AI/t5_os/` — Tuya SDK（CP/AP 分离、tkl_* HAL）
