# BK7258 NuttX SDK 集成实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 BK7258 NuttX 底层驱动从寄存器级手写全面转为调用 Beken SDK API（armino_as_lib 预编译库模式），修复 AON WDT 无人喂导致的无限重启根因。

**架构:** AMP 双镜像——CP(CPU0) 和 AP(CPU1+CPU2 SMP) 独立编译，各自链接自己的 armino_as_lib 预编译库。NuttX 侧只写薄 wrapper 调 `bk_*` API，OS 适配层桥接 FreeRTOS→NuttX。先做 CP 跑通，AP 后续。

**Tech Stack:** arm-none-eabi-gcc 10.3.1, Beken bk_avdk_smp SDK, NuttX RTOS, armino_as_lib.sh 打包工具

> **归档说明（2026-07-31）：** 本计划中的单层 `armino_as_lib/{cp,ap}` 是当时路径，
> 不再作为当前操作说明。现目录为
> `armino_as_lib/versions/<version>/{cp,ap}`，默认 `v3.1.1.9`，legacy 同样位于
> `versions/legacy/`。请使用当前导入脚本和
> `docs/bk7258-t5ai/nuttx-port/sdk-static-library-import.md`。

## Global Constraints

- 不修改 nuttx 官方树，所有改动在 `contest2026_135_yongwangzhiqian/board/bk7258/` overlay 内
- SDK 源码一律取 `$BK_AVDK/cp/`（CP 路径），不碰 `ap/`
- bootloader 保留自己的（全流程自己控制），不使用官方闭源 bootloader
- wrapper 层零寄存器操作（不出现 `putreg32`/`getreg32`）
- OS 适配层函数签名必须与 SDK 预编译库期望的一致
- `-DCONFIG_FREERTOS=0` 禁用 SDK 头里的 FreeRTOS 假设

## 文件结构

```
board/bk7258/
├── bootloader/                  # 不变
│   └── boot_main.c              # 修改 FAL 分区表（Task 6）
├── chip/                        # 现有文件
│   ├── bk7258_wdt.c             # 重写为 SDK wrapper（Task 3）
│   ├── bk7258_flash_mtd.c       # 重写为 SDK wrapper（Task 4）
│   ├── Make.defs                # 更新构建规则（Task 5）
│   └── Kconfig                  # 更新配置（Task 5）
├── bk_idk/                      # 新增：SDK 预编译库
│   └── armino_as_lib/
│       └── cp/                  # Task 1 产出
│           ├── libs/
│           ├── config/
│           └── include/
└── src/
    └── bk7258_bringup.c         # 修改 WDT 调用时机（Task 3）
```

---

### Task 1: 编译 SDK 产出 armino_as_lib

**Files:**
- Create: `board/bk7258/bk_idk/armino_as_lib/cp/` (整个目录，从 SDK 编译产出拷入)

**Interfaces:**
- Produces: `bk_idk/armino_as_lib/cp/libs/*.a`（预编译静态库）、`config/sdkconfig.h`、`include/`（SDK 头文件）

- [ ] **Step 1: 确认工具链可用**

```bash
/usr/bin/arm-none-eabi-gcc --version
```

Expected: `arm-none-eabi-gcc (15:10.3-2021.07-4) 10.3.1`

- [ ] **Step 2: 编译 SDK（CP 配置）**

```bash
cd /home/lijian/project/armino/bk_avdk_smp
./tools/build_tools/build.sh . projects/app build bk7258
```

Expected: 编译成功，产出 `build/bk7258/` 目录

- [ ] **Step 3: 打包 armino_as_lib**

```bash
cd /home/lijian/project/armino/bk_avdk_smp
./tools/build_tools/armino_as_lib.sh bk7258 . build/bk7258 projects/app
```

Expected: 产出 `build/armino_as_lib/bk7258/` 目录，包含 `libs/`、`config/`、`include/`

- [ ] **Step 4: 拷到工程**

```bash
mkdir -p /home/lijian/project/open-vela/contest2026_135_yongwangzhiqian/board/bk7258/bk_idk/armino_as_lib/
cp -r /home/lijian/project/armino/bk_avdk_smp/build/armino_as_lib/bk7258 \
      /home/lijian/project/open-vela/contest2026_135_yongwangzhiqian/board/bk7258/bk_idk/armino_as_lib/cp/
```

- [ ] **Step 5: 验证产出**

```bash
ls /home/lijian/project/open-vela/contest2026_135_yongwangzhiqian/board/bk7258/bk_idk/armino_as_lib/cp/libs/*.a | wc -l
ls /home/lijian/project/open-vela/contest2026_135_yongwangzhiqian/board/bk7258/bk_idk/armino_as_lib/cp/config/sdkconfig.h
ls /home/lijian/project/open-vela/contest2026_135_yongwangzhiqian/board/bk7258/bk_idk/armino_as_lib/cp/include/
```

Expected: `.a` 文件数量 > 10，`sdkconfig.h` 存在，`include/` 目录有内容

- [ ] **Step 6: 提交**

```bash
cd /home/lijian/project/open-vela/contest2026_135_yongwangzhiqian
git add board/bk7258/bk_idk/
git commit -m "feat(bk7258): add armino_as_lib prebuilt SDK libraries for CP

Compiled from bk_avdk_smp SDK with CP configuration.
Contains libdriver.a, libbk_system.a, libbk_pm.a, etc."
```

---

### Task 2: 创建 OS 适配层

**Files:**
- Create: `board/bk7258/chip/common/bk7258_os_adapt.c`

**Interfaces:**
- Produces: FreeRTOS API 的 NuttX 实现（`rtos_create_thread`、`os_malloc`、`GLOBAL_INT_DISABLE` 等），供 `libdriver.a` 链接时解析符号

**参考:** `/home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_os_adapt.c`（~1900 行）

- [ ] **Step 1: 读取 7236N 的 beken_os_adapt.c 完整内容**

```bash
wc -l /home/lijian/project/armino/vendor_beken/chips/bk7236n/beken_os_adapt.c
```

了解完整的函数清单和实现模式。

- [ ] **Step 2: 创建 bk7258_os_adapt.c 骨架**

创建 `board/bk7258/chip/common/bk7258_os_adapt.c`，包含以下核心函数：

```c
/****************************************************************************
 * board/bk7258/chip/common/bk7258_os_adapt.c
 *
 * BK7258 NuttX OS adaptation layer.
 * Bridges SDK prebuilt library (libdriver.a) FreeRTOS API calls to NuttX.
 * Reference: vendor_beken/chips/bk7236n/beken_os_adapt.c
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/kthread.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/kmalloc.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/sched.h>
#include <nuttx/signal.h>
#include <string.h>

/* SDK headers */
#include "os/os.h"
#include "os/mem.h"

/****************************************************************************
 * Memory
 ****************************************************************************/

void *os_malloc(unsigned int size)
{
  return kmm_malloc(size);
}

void os_free(void *ptr)
{
  kmm_free(ptr);
}

void *os_calloc(unsigned int nmemb, unsigned int size)
{
  return kmm_calloc(nmemb, size);
}

void *os_realloc(void *ptr, unsigned int size)
{
  return kmm_realloc(ptr, size);
}

void *os_memset(void *s, int c, unsigned int n)
{
  return memset(s, c, n);
}

void *os_memcpy(void *dest, const void *src, unsigned int n)
{
  return memcpy(dest, src, n);
}

int os_memcmp(const void *s1, const void *s2, unsigned int n)
{
  return memcmp(s1, s2, n);
}

/****************************************************************************
 * Tick / Time
 ****************************************************************************/

unsigned int bk_get_tick(void)
{
  return (unsigned int)clock_systime_ticks();
}

unsigned int rtos_get_ms_per_tick(void)
{
  return MSEC_PER_TICK;
}

unsigned int rtos_ms_to_tick(unsigned int ms)
{
  return MSEC2TICK(ms);
}

/****************************************************************************
 * Interrupt
 ****************************************************************************/

unsigned int GLOBAL_INT_DISABLE(void)
{
  return (unsigned int)irqsave();
}

void GLOBAL_INT_RESTORE(unsigned int flags)
{
  irqrestore((irqstate_t)flags);
}

/****************************************************************************
 * Thread
 ****************************************************************************/

/* rtos_create_thread: SDK signature varies, adapt from 7236N reference */

/****************************************************************************
 * Semaphore / Mutex / Delay
 ****************************************************************************/

/* rtos_init_semaphore, rtos_get_semaphore, rtos_set_semaphore */
/* rtos_init_mutex, rtos_mutex_lock, rtos_mutex_unlock */
/* rtos_delay_milliseconds */

/* TODO: Add remaining functions from 7236N reference as needed */
```

- [ ] **Step 3: 从 7236N 拷贝完整的函数实现**

从 `beken_os_adapt.c` 拷贝所有函数实现（线程、信号量、互斥锁、延时、事件组等），适配 NuttX API。

关键函数清单（从 7236N 参考）：
- `rtos_create_thread()` → `kthread_create()`
- `rtos_init_semaphore()` / `rtos_get_semaphore()` / `rtos_set_semaphore()` → `nxsem_init()` / `nxsem_wait()` / `nxsem_post()`
- `rtos_init_mutex()` / `rtos_mutex_lock()` / `rtos_mutex_unlock()` → `nxmutex_init()` / `nxmutex_lock()` / `nxmutex_unlock()`
- `rtos_delay_milliseconds()` → `nxsig_usleep()`
- `vTaskSuspendAll()` / `xTaskResumeAll()` → `sched_lock()` / `sched_unlock()`

- [ ] **Step 4: 编译验证**

```bash
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/cp_nsh -j8 2>&1 | tail -30
```

Expected: 编译通过（或只有少量未定义符号需要补桩）

- [ ] **Step 5: 提交**

```bash
git add board/bk7258/chip/common/bk7258_os_adapt.c
git commit -m "feat(bk7258): add OS adaptation layer for SDK prebuilt libraries

FreeRTOS→NuttX shim: rtos_*→nxsem/nxmutex/kthread, os_malloc→kmm_malloc,
bk_get_tick→clock_systime_ticks, GLOBAL_INT_*→irqsave/irqrestore."
```

---

### Task 3: WDT 驱动转 SDK wrapper（修 AON 根因）

**Files:**
- Modify: `board/bk7258/chip/cp/bk7258_wdt.c`（完全重写）
- Modify: `board/bk7258/src/bk7258_bringup.c:171-173`（WDT 调用时机）

**Interfaces:**
- Consumes: `bk_wdt_driver_init()`, `bk_wdt_start(timeout_ms)`, `bk_wdt_feed()`, `bk_wdt_stop()`, `bk_aon_wdt_stop()`（来自 `libdriver.a`）
- Produces: `bk7258_wdt_initialize()` 注册 `/dev/watchdog0`

- [ ] **Step 1: 备份当前实现**

```bash
cp board/bk7258/chip/cp/bk7258_wdt.c board/bk7258/chip/cp/bk7258_wdt.c.bak
```

- [ ] **Step 2: 重写 bk7258_wdt.c 为 SDK wrapper**

完全替换 `board/bk7258/chip/cp/bk7258_wdt.c`，内容如下：

```c
/****************************************************************************
 * board/bk7258/chip/cp/bk7258_wdt.c
 *
 * BK7258 WDT NuttX lower-half driver — SDK wrapper.
 * Calls bk_wdt_* / bk_aon_wdt_* SDK APIs. Zero register access.
 *
 * Fixes: AON WDT orphaned by bootloader → infinite reboot (F-01).
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_WDT

#include <stdint.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/timers/watchdog.h>

#include "bk7258_wdt.h"

/* SDK API headers */
#include <driver/wdt.h>
#include <driver/aon_wdt.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_WDT_DEFAULT_TIMEOUT_MS  8000u
#define BK7258_WDT_MAX_TIMEOUT_MS      0xFFFFu

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_wdt_lowerhalf_s
{
  struct watchdog_lowerhalf_s wdt_lh;  /* Must be first */
  uint32_t timeout;                    /* Current timeout in ms */
  bool     started;                    /* WDT is armed */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int bk7258_wdt_start(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdt_stop(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdt_keepalive(struct watchdog_lowerhalf_s *lower);
static int bk7258_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                struct watchdog_status_s *status);
static int bk7258_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                 uint32_t timeout);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct watchdog_ops_s g_bk7258_wdt_ops =
{
  .start      = bk7258_wdt_start,
  .stop       = bk7258_wdt_stop,
  .keepalive  = bk7258_wdt_keepalive,
  .getstatus  = bk7258_wdt_getstatus,
  .settimeout = bk7258_wdt_settimeout,
};

static struct bk7258_wdt_lowerhalf_s g_bk7258_wdt;

/****************************************************************************
 * Private: lower-half operations
 ****************************************************************************/

static int bk7258_wdt_start(struct watchdog_lowerhalf_s *lower)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  if (!bk_wdt_is_driver_inited())
    {
      bk_wdt_driver_init();
    }

  if (bk_wdt_start(priv->timeout) != BK_OK)
    {
      return -EIO;
    }

  priv->started = true;
  wdinfo("started, timeout=%" PRIu32 " ms\n", priv->timeout);
  return OK;
}

static int bk7258_wdt_stop(struct watchdog_lowerhalf_s *lower)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  bk_wdt_stop();
  bk_aon_wdt_stop();  /* Stop AON WDT (fixes F-01 reboot root cause) */

  priv->started = false;
  wdinfo("stopped\n");
  return OK;
}

static int bk7258_wdt_keepalive(struct watchdog_lowerhalf_s *lower)
{
  return (bk_wdt_feed() == BK_OK) ? OK : -EIO;
}

static int bk7258_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                struct watchdog_status_s *status)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  status->flags = WDFLAGS_RESET;
  if (priv->started)
    {
      status->flags |= WDFLAGS_ACTIVE;
    }

  status->timeout = priv->timeout;
  status->timeleft = priv->timeout;  /* No readable counter */
  return OK;
}

static int bk7258_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                 uint32_t timeout)
{
  struct bk7258_wdt_lowerhalf_s *priv =
    (struct bk7258_wdt_lowerhalf_s *)lower;

  if (timeout == 0)
    {
      return -EINVAL;
    }

  if (timeout > BK7258_WDT_MAX_TIMEOUT_MS)
    {
      timeout = BK7258_WDT_MAX_TIMEOUT_MS;
    }

  priv->timeout = timeout;

  if (priv->started)
    {
      bk_wdt_start(priv->timeout);
    }

  wdinfo("timeout set to %" PRIu32 " ms\n", priv->timeout);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_wdt_initialize(void)
{
  static bool s_inited;
  struct bk7258_wdt_lowerhalf_s *priv = &g_bk7258_wdt;
  void *handle;

  if (s_inited)
    {
      return OK;
    }

  s_inited = true;

  /* ★ Root cause fix: stop AON WDT immediately before any other init.
   * Bootloader arms both APB + AON WDTs; AON is not managed by NuttX
   * watchdog framework and would expire ~8s after boot → reboot loop.
   */

  bk_aon_wdt_stop();

  priv->wdt_lh.ops = &g_bk7258_wdt_ops;
  priv->timeout    = BK7258_WDT_DEFAULT_TIMEOUT_MS;
  priv->started    = false;

  handle = watchdog_register("/dev/watchdog0",
                             (struct watchdog_lowerhalf_s *)priv);
  if (handle == NULL)
    {
      wderr("ERROR: watchdog_register failed\n");
      return -ENOMEM;
    }

  wdinfo("BK7258 WDT registered, default timeout=%" PRIu32 " ms\n",
         priv->timeout);
  return OK;
}

#endif /* CONFIG_BK7258_WDT */
```

- [ ] **Step 3: 更新 bk7258_wdt.h**

确认 `bk7258_wdt.h` 的函数原型不变（`int bk7258_wdt_initialize(void)`），无需修改。

- [ ] **Step 4: 更新 bk7258_bringup.c — WDT 调用时机前移**

修改 `board/bk7258/src/bk7258_bringup.c`，把 `bk7258_wdt_initialize()` 前移到 bringup 最开头（在 procfs/DVFS/flash 之前）：

```c
int bk7258_bringup(void)
{
  /* ★ WDT first: stop AON WDT ASAP to prevent bootloader timeout */
#ifdef CONFIG_BK7258_WDT
  (void)bk7258_wdt_initialize();
#endif

  /* ... rest of bringup (procfs, DVFS, flash, etc.) ... */
```

- [ ] **Step 5: 编译验证**

```bash
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/cp_nsh -j8 2>&1 | tail -30
```

Expected: 编译通过（`bk_wdt_*` / `bk_aon_wdt_*` 符号从 `libdriver.a` 解析）

- [ ] **Step 6: 提交**

```bash
git add board/bk7258/chip/cp/bk7258_wdt.c board/bk7258/src/bk7258_bringup.c
git commit -m "fix(bk7258): rewrite WDT driver as SDK wrapper, fix AON reboot

Root cause: bootloader arms both APB+AON WDTs, app only managed APB via
NuttX automonitor. AON WDT orphaned → ~8s reboot loop.

Fix: call bk_aon_wdt_stop() in bk7258_wdt_initialize() before any other
init. APB WDT managed by NuttX automonitor (bk_wdt_feed). All register
access replaced by SDK API calls (bk_wdt_*/bk_aon_wdt_*)."
```

---

### Task 4: Flash MTD 驱动转 SDK wrapper

**Files:**
- Modify: `board/bk7258/chip/cp/bk7258_flash_mtd.c`（完全重写）

**Interfaces:**
- Consumes: `bk_flash_read_bytes()`, `bk_flash_write_bytes()`, `bk_flash_erase_sector()`, `bk_flash_set_protect_type()`, `bk_flash_driver_init()`（来自 `libdriver.a`）
- Produces: `bk7258_flash_mtd_initialize()` 返回 `struct mtd_dev_s *`

- [ ] **Step 1: 备份当前实现**

```bash
cp board/bk7258/chip/cp/bk7258_flash_mtd.c board/bk7258/chip/cp/bk7258_flash_mtd.c.bak
```

- [ ] **Step 2: 重写 bk7258_flash_mtd.c 为 SDK wrapper**

完全替换，内容参考设计文档 §5.4 的 Flash Wrapper 示例。核心改动：
- `bread` → `bk_flash_read_bytes(offset, buffer, nbytes)`
- `erase` → `bk_flash_set_protect_type(FLASH_PROTECT_NONE)` + `bk_flash_erase_sector()` × N + `bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK)`
- `bwrite` → `bk_flash_set_protect_type(FLASH_PROTECT_NONE)` + `bk_flash_write_bytes()` + `bk_flash_set_protect_type(FLASH_UNPROTECT_LAST_BLOCK)`
- `ioctl` → 保留 geometry/erase-state
- `initialize` → `bk_flash_driver_init()` + ID 校验

- [ ] **Step 3: 编译验证**

```bash
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/cp_nsh -j8 2>&1 | tail -30
```

- [ ] **Step 4: 提交**

```bash
git add board/bk7258/chip/cp/bk7258_flash_mtd.c
git commit -m "refactor(bk7258): rewrite flash MTD driver as SDK wrapper

Replace register-level flash controller access with SDK API calls:
bk_flash_read_bytes/write_bytes/erase_sector/set_protect_type.
Zero register access in wrapper."
```

---

### Task 5: 更新构建系统

**Files:**
- Modify: `board/bk7258/chip/Make.defs`
- Modify: `board/bk7258/chip/Kconfig`
- Modify: `board/bk7258/scripts/Make.defs`

**Interfaces:**
- Produces: SDK 头文件 include 路径、`EXTRA_LIBS` 链接 `libs/*.a`、`-DCONFIG_*=0` 宏

- [ ] **Step 1: 更新 chip/Make.defs**

参考 7236N 的 `chips/bk7236n/Make.defs`，添加 SDK include 路径和 `-DCONFIG_*=0` 宏：

```makefile
# 在现有 Make.defs 基础上添加：

BK_IDK_PATH_RELA_TO_CHIP = ../../../../board/bk7258/bk_idk

# SDK 头文件路径
INCLUDES += ${INCDIR_PREFIX}$(BK_IDK_PATH_RELA_TO_CHIP)/armino_as_lib/cp/include
INCLUDES += ${INCDIR_PREFIX}$(BK_IDK_PATH_RELA_TO_CHIP)/armino_as_lib/cp/config

# OS 适配层
CHIP_CSRCS += bk7258_os_adapt.c

# 禁用 SDK 内部 FreeRTOS 假设（参考 7236N Make.defs:110-120）
CFLAGS += -DCONFIG_FREERTOS=0
CFLAGS += -DCONFIG_SOC_BK7256XX=0 -DCONFIG_SOC_BK7236XX=0
CFLAGS += -DCONFIG_INT_WDT=0 -DCONFIG_TASK_WDT=0 -DCONFIG_INT_AON_WDT=0
CFLAGS += -DCONFIG_SYSTEM_CTRL=1 -DCONFIG_NMI_WDT_EN=0
CFLAGS += -DCONFIG_DEBUG_VERSION=0 -DCONFIG_GPIO_RETENTION_SUPPORT=0
```

- [ ] **Step 2: 更新 scripts/Make.defs**

添加 `EXTRA_LIBS` 链接 CP 预编译库：

```makefile
# 在现有 scripts/Make.defs 基础上添加：

EXTRA_LIBS += $(wildcard $(shell readlink -f $(TOPDIR)/$(CONFIG_ARCH_CHIP_CUSTOM_DIR)/bk_idk/armino_as_lib/cp/libs)/*.a)
```

- [ ] **Step 3: 更新 chip/Kconfig**

添加 `CONFIG_BK7258_WDT`（如不存在）：

```kconfig
config BK7258_WDT
    bool "BK7258 watchdog support"
    default y
    depends on WATCHDOG
    ---help---
        Enable BK7258 hardware watchdog support via SDK bk_wdt_* APIs.
```

- [ ] **Step 4: 编译验证**

```bash
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/cp_nsh -j8 2>&1 | tail -30
```

Expected: 编译通过，`EXTRA_LIBS` 链接成功

- [ ] **Step 5: 提交**

```bash
git add board/bk7258/chip/Make.defs board/bk7258/chip/Kconfig board/bk7258/scripts/Make.defs
git commit -m "build(bk7258): integrate armino_as_lib SDK into build system

- Add SDK include paths (armino_as_lib/cp/include + config)
- Add EXTRA_LIBS to link prebuilt .a files
- Add -DCONFIG_*=0 to disable FreeRTOS assumptions in SDK headers
- Add OS adaptation layer to CHIP_CSRCS"
```

---

### Task 6: 更新 Bootloader FAL 分区表

**Files:**
- Modify: `board/bk7258/bootloader/boot_main.c:60-64`

**Interfaces:**
- Produces: 扩展的 FAL 分区表（bootloader + cp_app + ap_app）

- [ ] **Step 1: 更新 FAL 分区表**

修改 `boot_main.c` 中的 `fal_partition_table[]`：

```c
__attribute__((used))
const struct fal_partition fal_partition_table[] = {
    { FAL_PART_MAGIC, "bootloader", "beken_onchip_crc", 0x00000L, 0x10000L, 0u },  /* 64KB logical */
    { FAL_PART_MAGIC, "cp_app",     "beken_onchip_crc", 0x10000L, 0x158000L, 0u }, /* 1360KB logical */
    { FAL_PART_MAGIC, "ap_app",     "beken_onchip_crc", 0x168000L, 0x124000L, 0u }, /* 1156KB logical */
};
```

- [ ] **Step 2: 更新 c_main() 使用 cp_app**

修改 `fal_find("app")` → `fal_find("cp_app")`：

```c
app = fal_find("cp_app");
```

- [ ] **Step 3: 编译 bootloader**

```bash
cd /home/lijian/project/open-vela/contest2026_135_yongwangzhiqian/board/bk7258/bootloader
make clean && make
```

Expected: `bl.bin` 和 `bl_crc.bin` 重新生成

- [ ] **Step 4: 提交**

```bash
cd /home/lijian/project/open-vela/contest2026_135_yongwangzhiqian
git add board/bk7258/bootloader/boot_main.c board/bk7258/bootloader/bl.bin board/bk7258/bootloader/bl_crc.bin
git commit -m "feat(bk7258): extend bootloader FAL partition table for CP+AP

Add cp_app (1360KB) and ap_app (1156KB) partitions matching SDK
auto_partitions.csv layout. Bootloader jumps to cp_app (CPU0)."
```

---

### Task 7: 板端验证

**Files:**
- Test: 烧录 `all-app.bin` 到板子，验证 NSH + WDT + Flash

- [ ] **Step 1: 编译最终镜像**

```bash
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/cp_nsh -j8
```

Expected: `nuttx.bin` 生成成功

- [ ] **Step 2: 组装 all-app.bin**

postbuild.sh 自动执行（Make.defs 里的 POSTBUILD），产出 `all-app.bin`。

- [ ] **Step 3: 烧录**

使用 BKFIL/bk_loader 烧录 `all-app.bin` 到 flash 0x0。

- [ ] **Step 4: 验证 NSH 启动**

串口连接（460800 波特率），预期输出：
```
u_bootloader enter
partition app @ 0x02010000
jump to:0x02010000
JMP
NuttShell (NSH) NuttX-xx.x.x
nsh>
```

- [ ] **Step 5: 验证 WDT（AON 根因修复）**

```
nsh> sleep 12
```

Expected: 12 秒后返回 `nsh>`，**不重启**（修复前 ~8s 必重启）

- [ ] **Step 6: 验证 Flash**

```
nsh> ls /dev/mtdblock0
nsh> ls /data
nsh> cat /data/probe.txt
```

Expected: `/dev/mtdblock0` 存在，`/data` 挂载成功，`probe.txt` 内容正确

- [ ] **Step 7: 验证长时间运行**

```
nsh> sleep 30
```

Expected: 30 秒后返回，无重启（APB WDT 由 NuttX automonitor 每 4s 喂）

---

## 执行顺序

```
Task 1 (编译 SDK)         ← 阻塞项，必须先做
    ↓
Task 2 (OS 适配层)        ← 依赖 Task 1 的 libs/*.a
    ↓
Task 5 (构建系统)          ← 依赖 Task 1 + Task 2，必须在 wrapper 之前（提供 include 路径和 EXTRA_LIBS）
    ↓
Task 3 (WDT wrapper)      ← 依赖 Task 1 + Task 2 + Task 5
Task 4 (Flash wrapper)    ← 依赖 Task 1 + Task 2 + Task 5（可与 Task 3 并行）
Task 6 (Bootloader)       ← 独立，可与 Task 3-5 并行
    ↓
Task 7 (板端验证)          ← 依赖 Task 3-6 全部完成
```
