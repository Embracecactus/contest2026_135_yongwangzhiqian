# Stage B2-Fix — WDT 驱动修复：AON WDT 无人喂导致进 app 无限重启（SDK 全量适配）

> **范围**：BK7258 NuttX WDT 驱动审查（driver-code-reviewer 双轮）+ 修复进 app 后 ~8s 无限重启的根因。
> **状态**：审查完成（双轮交叉验证），修复方案已定：**WDT 全量转 Beken SDK 接口**（与 flash 同期，
> 按 N6-A 增量模式）。待实施板端验证。
> **根因**：bootloader `boot_wdt_init()` 同时 arm APB WDT（`0x44800000`）与 AON WDT（`0x44000600`），
> period=8000（~8s）。app 只接管 APB WDT（NuttX automonitor 在 `watchdog_register()` 时自动 `WD_START`+
> 每 4s `WD_KEEPALIVE`），**AON WDT 零处理** → ~8s 后 AON WDT 超时复位 → 无限重启。
> **决策**：既然要全面转 SDK，WDT 不再手写修补，直接用 `bk_wdt_*` + `bk_aon_wdt_*` 从头适配，
> 一次性消除根因（F-01）+ 设计债（DP-07）+ 一批手写隐患（WDT-02/03/04、F-06/F-07）。

---

## 1. 审查方法

按 `driver-code-reviewer` skill 双轮 subagent 独立审查：
- **Round 1**（59 Pattern + DP-07 重复造轮子，参照 `nuttx-driver-development/references/wdg_pattern.md`）
- **Round 2**（深层逻辑：生命周期对称性、boot handoff 状态机、并发竞态）

裁判交叉验证两轮结论。

### 1.1 已纠正的早期误判

| 早期判断 | 纠正后 | 依据 |
|---|---|---|
| "APB WDT 没人喂 → 重启"（DR-002） | **撤回**。APB WDT 由 NuttX automonitor 在 register 时自动接管 | `nuttx/drivers/timers/watchdog.c:786-798` + `watchdog_automonitor_start` 在 register 路径内调 `WD_START`+`WD_SETTIMEOUT`；defconfig `BY_WDOG` 默认 + `PING_INTERVAL=4` |
| "进 app 前手写寄存器关 AON" | 方案改为：AON WDT 关狗用与 bootloader 一致的 key 协议（已板端验证），不引入 SDK AON 子系统 | `boot_wdt.h` AON key 序列已验证；SDK `wdt_driver.c` 依赖全栈，AON WDT 改 SDK 收益不抵成本 |
| Round 2 称"AON WDT 8-bit period→8000 截成 2.5ms" | **误读**。`aon_wdt_ll_set_period` 用 `AON_WDT_F_PERIOD_M=0xffff`（16 位），`0xff` 掩码只是某 get 函数的窄掩码 | `aon_wdt_ll.h` set 用 16 位 period，key 在 bit16+，与 `boot_wdt.h` 一致 |

---

## 2. 双轮交叉验证结果

### 2.1 两轮一致确认（高置信度）

| Finding | R1 | R2 | 严重度 | 置信度 |
|---|---|---|---|---|
| **AON WDT 无人喂 → 重启循环** | F-01 | WDT-01 | **Critical** | HIGH |

两轮独立定位**同一根因**，完全一致。这是进 app 无限重启的**唯一根因**。

### 2.2 采纳的有效问题（两轮一致或高价值）

| Finding | 来源 | 严重度 | 处理 |
|---|---|---|---|
| DP-07 重复造轮子（手写寄存器 vs SDK `bk_wdt_*`） | F-02/WDT-08 | High | APB WDT 不改 SDK（依赖全栈，板端已稳）；AON 关狗沿用 bootloader 已验证协议 |
| `watchdog_register()` 返回值未检查 | F-03 | Medium | 修复时加检查 |
| `settimeout(0)` 等于关狗 | F-06 | Medium | 加 `timeout==0` 拒绝 |
| `settimeout` 运行时不 soft_reset → 实际超时=(period-counter) | WDT-04 | Medium | 加 soft_reset |
| `timeout/started` 共享状态无锁 | F-04 | Medium-Low | 关键段加 irqsave |
| 无双重初始化保护 | F-10 | Low | 加 guard |

### 2.3 Round 2 新挖出但**非根因**的隐患

| Finding | 评估 |
|---|---|
| WDT-02 keepalive 写错寄存器（`0x04` dev_version vs `0x0C` dev_status） | 需核实布局；但板端 APB WDT 被喂稳 → key+period 重写已足够喂狗，status clear 非必需。改时一并修正或删除该步 |
| WDT-03 keepalive 缺 soft_reset | SDK 每次 feed soft_reset，但板端未挂 → BK7258 上 key+period 重写即喂狗。修复时对齐 SDK 加 soft_reset |
| F-07 start() soft_reset 未重新上电 | 需独立确认；非当前根因 |

> 这些隐患在"保留手写 APB WDT"前提下需逐一对齐 SDK；若后续 APB 改 SDK 接口则一并消失。

### 2.4 评分（双轮加权）

- L1-3 资源管理：F-01 Critical + F-03 → 重扣
- L1-7 嵌入式：F-01 Critical + WDT-03/F-07 + F-05 → 重扣
- L1-8 设计（独立）：DP-07 -4 → B 级
- **两个 Critical（F-01 重叠计）→ 一票否决封顶 60 → 实际 ~45，NEEDS_FIX**
- 修复 F-01（AON WDT）后即消除 Critical，进 PASS 区

---

## 3. 根因详解

### 3.1 Boot handoff 状态机

```
bootloader:
  start.S:70-84      disarms APB+AON (period=0)
  boot_main.c:193    boot_wdt_init() → arm APB(8s) + AON(8s)   [T0]
  boot_main.c:259    boot_wdt_feed() → 喂 APB only            [最后喂]
  → jump to app

app:
  board_app_initialize():
    procfs mount、DVFS procfs、flash MTD/LittleFS...           [耗时可能数秒]
    line 172: bk7258_wdt_initialize()
      watchdog_register("/dev/watchdog0", APB_lowerhalf)
        → automonitor_start:
            WD_SETTIMEOUT(8000)  → settimeout(8000)
            WD_START             → start()                     [APB 被 NuttX 接管]
        → 此后每 4s WD_KEEPALIVE → keepalive() → 喂 APB        ✅ APB 有人喂
      AON WDT: 无任何代码引用 0x44000600                          ❌ AON 无人喂

[ T0 + 8s ] AON WDT 超时 → 芯片复位 → 回 bootloader → 再 arm → 再 reset
= 无限重启
```

### 3.2 为什么 AON WDT 比其它 init 先到

`bk7258_wdt_initialize()` 在 bringup **第 172 行**，前面已做 procfs mount + DVFS procfs 注册。
但更关键：AON WDT 从 **bootloader T0 就开始倒计时**，到 app 真正站稳并喂上它，已过了
bootloader 全流程 + app early/late initialize 的时间。即使 app 8s 内跑到 line 172 也没用——
**之后**再没人喂 AON（automonitor 只喂 APB）。

---

## 4. 修复方案：WDT 全量转 Beken SDK 接口

### 4.1 决策依据

- 早晚全面转 SDK（用户决策），WDT 现在转 = 与 flash 同期增量，**不攒技术债、不重复劳动**
- 一次性清掉：根因 F-01（AON 无人喂）+ 设计债 DP-07（手写寄存器）+ 手写隐患
  WDT-02/03/04、F-06/F-07（见 §4.6）
- 调 SDK 的 `bk_wdt_feed/start/stop` 内部已含 soft_reset + 时钟上电 + key 序列，
  NuttX wrapper 只需薄转发，符合 `wdg_pattern.md` lower-half 契约

### 4.2 引入的 SDK 源码（从 `$BK_AVDK/cp/middleware/` 拷到 `$CONTEST/board/bk7258/sdk/`）

与 flash 同一 `sdk/` 目录，增量引入：

```
sdk/
├── driver/
│   ├── wdt/
│   │   ├── wdt_driver.c / wdt_driver.h        # bk_wdt_start/feed/stop/driver_init/deinit
│   │   └── (cli_wdt_api.c 不需要)
│   ├── aon_wdt/
│   │   ├── aon_wdt_driver.c / aon_wdt_driver.h
│   │   └── bk_aon_wdt.h                        # bk_aon_wdt_stop/feed/set_period
│   ├── common/  (flash 阶段已拷，复用)
│   ├── sys_ctrl/  (依赖，按编译补 sys_driver.c/.h)
│   ├── icu/        (icu_driver，wdt_driver_init 依赖)
│   ├── pmu/        (power_driver，时钟上电依赖)
│   └── aon_pmu/    (aon_pmu_driver，wdt rst dev enable)
├── soc/
│   ├── bk7258/hal/
│   │   ├── wdt_ll.c / wdt_ll.h
│   │   ├── aon_wdt_ll.c / aon_wdt_ll.h
│   │   ├── icu_ll.c / icu_ll.h
│   │   ├── aon_pmu_hal.c
│   │   └── (其余按编译报缺补)
│   └── bk7258/soc/
│       ├── wdt_reg.h / wdt_struct.h
│       └── aon_wdt_reg.h
└── include/  (driver/wdt.h、driver/aon_wdt.h、os/、common/ 等最小子集)
```

> **实际引入范围以编译收敛为准**（与 flash 策略一致）：先拷 wdt + aon_wdt 主干，编译报缺
> 哪个头/符号再补，逐层收敛，避免一开始拷一大堆。

### 4.3 NuttX wrapper —— `bk7258_wdt.c` 改为调 SDK（薄转发）

按 `wdg_pattern.md` 的 `struct watchdog_ops_s` 契约，ops 全部转发到 SDK：

```c
#include <driver/wdt.h>          /* SDK: bk_wdt_* */
#include <driver/aon_wdt.h>     /* SDK: bk_aon_wdt_* */

/* start = SDK bk_wdt_start（内部 wdt_init_common 上电 + wdt_ll_set_period soft_reset + key）*/
static int bk7258_wdt_start(struct watchdog_lowerhalf_s *lower) {
  struct bk7258_wdt_lowerhalf_s *priv = (void *)lower;
  if (!bk_wdt_is_driver_inited()) bk_wdt_driver_init();   /* 首次上时钟/初始化 */
  return (bk_wdt_start(priv->timeout) == BK_OK) ? OK : -EIO;
}

/* keepalive = SDK bk_wdt_feed（内部 soft_reset + key，消除 WDT-03）*/
static int bk7258_wdt_keepalive(struct watchdog_lowerhalf_s *lower) {
  return (bk_wdt_feed() == BK_OK) ? OK : -EIO;
}

/* stop = SDK bk_wdt_stop + 关 AON */
static int bk7258_wdt_stop(struct watchdog_lowerhalf_s *lower) {
  bk_wdt_stop();
  bk_aon_wdt_stop();                                 /* 消除 F-01 根因 */
  return OK;
}

/* settimeout: 拒绝 0（消除 F-06）；运行时由 SDK bk_wdt_start 自动 soft_reset（消除 WDT-04）*/
static int bk7258_wdt_settimeout(struct watchdog_lowerhalf_s *lower, uint32_t timeout) {
  struct bk7258_wdt_lowerhalf_s *priv = (void *)lower;
  if (timeout == 0) return -EINVAL;
  priv->timeout = (timeout > BK7258_WDT_PERIOD_MASK) ? BK7258_WDT_PERIOD_MASK : timeout;
  if (priv->started) bk_wdt_start(priv->timeout);     /* SDK 含 soft_reset */
  return OK;
}
```

注册函数（initialize 里**第一时间**关 AON WDT 修根因 + register 返回值检查 + 防重入）：

```c
int bk7258_wdt_initialize(void) {
  static bool s_inited;  if (s_inited) return OK; s_inited = true;   /* F-10 guard */

  bk_aon_wdt_stop();          /* ★根因修复：立刻关 bootloader 遗留的 AON 狗，抢在 automonitor 之前 */
  bk_wdt_driver_init();       /* SDK WDT 驱动初始化（时钟/控制结构） */

  priv->wdt_lh.ops = &g_bk7258_wdt_ops;
  priv->timeout = BK7258_DEFAULT_TIMEOUT_MS;        /* 8000ms，对齐 automonitor TIMEOUT */
  priv->started = false;

  void *h = watchdog_register("/dev/watchdog0", (struct watchdog_lowerhalf_s *)priv);
  return h ? OK : -ENOMEM;                          /* F-03 检查返回 */
}
```

调用时机改造（`bk7258_bringup.c`）：把 `bk7258_wdt_initialize()` **前移到 bringup 最开头**
（procfs / DVFS / flash MTD 之前），AON WDT 越早关越好，消除 bootloader 倒计时压力。
NuttX automonitor 在 register 时自动 `WD_START` APB WDT 并每 4s 喂，APB 路径不变且更稳。

### 4.4 依赖处理（与 flash 同策略）

`wdt_driver.c` 依赖偏重：`os/os.h`、`os/mem.h`、`reset_reason`、`bk_fake_clock`、
`icu_driver`、`power_driver`、`sys_driver`、`aon_pmu_driver`、（可选）`aon_rtc`、`gpio_driver`。
`aon_wdt_driver.c` 依赖轻：`aon_wdt_hal` + `os_mem` + `reset_reason`。

处理：
- **最小集拷入 + 空桩 + `#ifdef` 裁剪 + `-DCONFIG_FREERTOS=0`**（`os_malloc`→`kmm_malloc` 等，
  见 N6-A §3.4 `sdk/os/os_adapt.c`，flash 阶段已在建，复用扩充）
- 编译报未定义符号逐个补/桩（如 `bk_fake_clock`、`bk_get_tick` 用 `clock_systime_ticks` 适配）
- 裁剪不需要的分支：`CONFIG_INT_WDT`/`CONFIG_TASK_WDT` 任务级看门狗（SDK 内部 FreeRTOS 任务
  监控）整体关掉，只保留硬件 WDT 主路径 `bk_wdt_start/feed/stop` + `bk_aon_wdt_*`

### 4.5 不改的部分

- **bootloader**：`boot_wdt.h`/`boot_main.c` 不动。bootloader 阶段 AON WDT 仍需兜底 cold-init
  挂死场景；跳 app 前不关狗是合理设计，由 app `bk7258_wdt_initialize` 接管。
- **defconfig**：`CONFIG_WATCHDOG_AUTOMONITOR=y`/`PING_INTERVAL=4`/`TIMEOUT=8000` 不变
  （automonitor 机制对 lower-half 透明，改 SDK 后仍由它在 register 时启动 APB 喂狗）。
- **`CONFIG_BK7258_WDT`** 仍是芯片 Kconfig 开关，`Make.defs` 改为同时拉 SDK WDT 源码。

### 4.6 此方案一并消除的问题

| 问题 | 消解方式 |
|---|---|
| **F-01 AON 无人喂（重启根因）** | `bk7258_wdt_stop` 调 `bk_aon_wdt_stop()` + initialize 最先关 AON |
| F-02 DP-07 重复造轮子 | ops 全转 SDK，删除手写寄存器/常量 |
| F-03 register 返回未检查 | initialize 加 `h ? OK : -ENOMEM` |
| F-04 共享状态无锁 | SDK 内部原子性 + NuttX 侧不再有跨上下文手写状态（详见 §4.7） |
| F-06 settimeout(0) 关狗 | 加 `==0` 返回 `-EINVAL` |
| F-07 start soft_reset 未重新上电 | `bk_wdt_start` 内部 `wdt_init_common` 已上电 |
| F-10 双重初始化 | `static bool s_inited` guard |
| WDT-02 keepalive 写错寄存器 | 不再手写 status，整段删除 |
| WDT-03 keepalive 缺 soft_reset | `bk_wdt_feed` 内部含 |
| WDT-04 settimeout 运行时不 reset | `bk_wdt_start` 含 soft_reset |

### 4.7 残留与遗留

- **F-04 共享状态无锁**：转 SDK 后 `priv->timeout/started` 仍可能被 automonitor 的 keepalive
  与用户 ioctl settimeout 跨上下文访问。最小处理：`settimeout`/`start`/`stop` 关键段加
  `enter_critical_section()`/`leave_critical_section()`。SDK 侧硬件操作自身原子。
- **F-09 timeleft 恒等于 timeout**：BK7258 APB WDT 无可读计数器，SDK 亦不提供；保留注释说明，
  NuttX automonitor 不依赖 timeleft。无解，可接受。
- **F-05 W1C status 清除**：转 SDK 后该手写段删除，问题自动消失。

---

## 5. 验证

1. 编译：`./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/cp_nsh -j8`
2. 烧录：`all-app.bin` @ 0x0（BKFIL/bk_loader，见 [[bk7258-flash-flow-bkfil]]）
3. 板端回归（关键指标：**不再 8s 重启**）：
   - 进 NSH 后 `sleep 12`（>8s）→ 仍存活不复位 ✅
   - `cat /proc/dvfs`、`ls /data`、`cat /data/probe.txt` 正常（AON 关狗不影响其它）
   - 长时间空闲（>30s）不复位（APB automonitor 持续喂）
   - 触发一次硬挂（如死循环）→ 应 ~8s 被 **APB** WDT 复位（APB 仍是 active 看门狗）
4. 对比修复前：修复前进 app ~8s 必复位；修复后稳定运行无重启。

---

## 6. 相关文件索引

**当前实现**：
- `$CONTEST/board/bk7258/chip/cp/bk7258_wdt.c`（待修：加 AON 关狗 + 输入校验 + guard）
- `$CONTEST/board/bk7258/chip/cp/bk7258_wdt.h`
- `$CONTEST/board/bk7258/src/bk7258_bringup.c:166-173`（待修：wdt_initialize 前移）
- `$CONTEST/board/bk7258/bootloader/boot_wdt.h`（AON key 协议参考，不改）

**SDK 参考**：
- `$BK_AVDK/cp/include/driver/wdt.h` — `bk_wdt_*` API（后续 APB 切 SDK 用）
- `$BK_AVDK/cp/include/driver/aon_wdt.h` — `bk_aon_wdt_stop/feed/set_period`
- `$BK_AVDK/cp/middleware/driver/aon_wdt/aon_wdt_driver.c` — AON WDT 实现（依赖轻）
- `$BK_AVDK/cp/middleware/soc/bk7258/hal/aon_wdt_ll.h` — period 字段定义

**NuttX 参考**：
- `nuttx/drivers/timers/watchdog.c:786-798` — automonitor 在 register 时启动
- `nuttx/drivers/timers/watchdog.c:253-300` — `watchdog_automonitor_start` 调 WD_START/WD_SETTIMEOUT

**记忆引用**：
- `bk7258-b2-bootloader-wdt` — bootloader WDT 现状（APB+AON，喂狗 key 序列）
- `bk7258-nuttx-boot-core-verified` — N1/N2 boot 现状