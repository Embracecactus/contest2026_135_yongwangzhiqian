# BK7258 chip/ 目录代码评审与清理优化指导（小白版）

> **来源记录**
>
> - 评审对象：`$BOARD/chip/`（contest 仓库 `board/bk7258_t5ai/chip/`，共 33 个文件，约 8400 行）
> - 评审方式：静态阅读（未编译、未烧录）
> - 撰写日期：2026-07-27
> - 评审基线：`$CONTEST` 的 `HEAD`（N7 提交 `38699e8` + 未提交的 UART/GPIO wrapper 工作）
> - 重要约定：本文所有"建议删除/修改"的结论都来自**静态分析**。凡标注"需板端复核"的条目，在板上实测确认前不要动手。

## 0. 这份文档是干什么的

前一轮对 `$BOARD/chip/` 做了逐文件评审，找出两类问题：

1. **调试残留**：bring-up 阶段为了"看得见"而加的打印、探针、测试代码。它们像装修时的脚手架——楼盖好了就该拆。
2. **可优化点**：代码能跑，但有功能缺陷、语义不符或性能浪费。

这份文档把评审结论翻译成小白能执行的操作指导。每条都回答四个问题：**是什么、为什么、怎么做、怎么验证**。

### 0.1 先学三个词

| 词 | 通俗解释 | 在本文中的意义 |
|---|---|---|
| **Kconfig 门控** | 一个配置开关（`CONFIG_XXX=y/n`），决定某段代码是否参与编译 | 很多测试代码靠开关隔离，"关闭开关"比"删代码"更安全 |
| **defconfig** | 板子的默认配置文件（`configs/cp_nsh/defconfig`），决定构建时哪些开关是开的 | 改它 = 改默认行为，是清理工作的主要战场 |
| **死代码** | 存在但没有任何调用者的函数/变量，白白占 Flash | 可以安全删除，不影响任何功能 |

### 0.2 安全纪律（比任何单条建议都重要）

1. **一次只改一类**：删一批 → 编译 → 确认通过，再进行下一批。不要一次改十个文件。
2. **编译命令**（在工作区根目录）：

   ```bash
   cd "$WORKSPACE/nuttx"
   make -j$(nproc) 2>&1 | tee /tmp/build-check.log
   ```

3. **凡涉及 UART 输出路径的删除**（比如 `up_putc` 标记），删除后控制台前几个字符可能变化，这是预期内的，不算故障。
4. **标"需板端复核"的条目**：先把代码留着，等板测确认后再删。

---

## 1. A 级清理：可以直接做（证据充分）

这一级的共同特征：**代码自己注释里就写了"临时的/验证后要关"**，或者**已经没有任何调用者**。

### A1. 删除 `bk7258_vectors.c` 里的 SysTick 探针（死代码）

- **是什么**：`bk7258_systick_probe()` 函数（约第 254-268 行），一个打印字符 `'s'` 的诊断函数。
- **为什么能删**：
  - 函数头注释原文写着 "DIAGNOSTIC SysTick probe **(temporary)**... Pure diagnostic; **revert**"——作者自己留了"用完请删"的纸条。
  - 它**没有任何调用者**：向量表 slot [15]（SysTick 位）现在路由到 `exception_common`（约第 316 行），这个 probe 是悬空死代码。
  - SysTick 路径在 N2/N4 阶段已板端验证（git log 可查）。
- **怎么做**：删除整个 `bk7258_systick_probe()` 函数及其上方的注释块。
- **怎么验证**：重新编译通过即可。不删 `bk7258_hardfault_handler`（见 C1，那个有保留价值）。

### A2. 关闭 defconfig 里的两个"验证后应关闭"的开关

- **是什么**：`configs/cp_nsh/defconfig` 里目前还开着：
  - `CONFIG_BK7258_CLOCK_320M_PROBE=y` —— 每次开机在串口打印一行时钟寄存器十六进制转储
  - `CONFIG_BK7258_SDK_IRQ_TIMER_TEST=y` —— 一个手动 IRQ 桥测试命令
- **为什么能关**：
  - Kconfig 帮助文本原文：`BK7258_CLOCK_320M_PROBE` 写明 "for one verification build. **Disable after board-verify**"；而 git log 里 `6b29ee8` 已记录 "N4 board-verified"。任务完成了，脚手架该拆。
  - `BK7258_SDK_IRQ_TIMER_TEST` 的 Kconfig 自述 "manual test... It never runs from board bring-up"，Stage B 验证（commit `c588afb`）早已完成。它还给固件带进一个 `bkirqtest` 命令和约 341 行测试代码。
- **怎么做**：在 `configs/cp_nsh/defconfig` 中把两行改成：

  ```text
  # CONFIG_BK7258_CLOCK_320M_PROBE is not set
  # CONFIG_BK7258_SDK_IRQ_TIMER_TEST is not set
  ```

- **怎么验证**：重新 `./tools/configure.sh` + 编译；开机串口不再出现 `N4Clk tier=...` 那一行。
- **进阶（可选）**：如果确定永不复用，可进一步删除 `bk7258_clock.c` 第 56-96 行的 probe 辅助函数和第 116-147 行的打印块（约 100 行），以及 `bk7258_sdk_irq_timer_test.c` 整个文件（341 行）。删文件前先把开关关掉编译一轮，确认无引用。

### A3. 清除 `bk7258_wdt.c` 的 5 处启动标记

- **是什么**：WDT 驱动里散落的单字符串口打印，是 bring-up 时期确认"代码走到这了"的路标：

  | 位置（约） | 内容 | 当时用途 |
  |---|---|---|
  | 第 90-94 行 + 第 254-256 行 | `bk7258_wdt_tick_probe()` + 一次性 wdog，打印 `'T'` | 验证 wdog 框架能跑 |
  | 第 243 行 | `up_putc('A')` | watchdog_register 前 |
  | 第 252 行 | `up_putc('B')` | watchdog_register 后 |
  | 第 256 行 | `up_putc('W'/'E')` | probe 启动成败 |
  | 第 127-133 行 | `keepalive` 里一次性打印 `'K'` | 确认自动喂狗首次触发 |

- **为什么能删**：WDT 功能（含 F-01 AON WDT 修复）已板端验证；而且项目有明确清理先例——git log `7d78810` "drop last two boot-trace markers (irq/serial init)" 删的就是 IRQ/serial 里的同类标记，WDT 是漏网之鱼。
- **怎么做**：删除上表全部内容。`keepalive` 里的 `static int marker_printed` 变量也一并删掉，函数体只剩一行 `return (bk_wdt_feed() == BK_OK) ? OK : -EIO;`。注意 `bk7258_wdt_tick_probe` 的原型声明（约第 68 行）和 `g_bk7258_wdt_probe` 变量（约第 84 行）也要删。
- **怎么验证**：编译通过；板上 `/dev/watchdog0` 行为不变（开机不再因超时复位即可）。

---

## 2. B 级清理：先验证、再动手

### B1. `bk7258_gpio_lowerhalf.c` 的 8 处 `printf("gpio1: ...")`

- **是什么**：约第 310、319、335、346、632、640、649、663 行的调试打印，外加只为打印服务的 `g_bk7258_gpio_key_isr_count` 计数器（约第 62 行）。
- **为什么要缓一缓**：GPIO 中断功能**还没有完成板端验证**，而且文件本身目前有两个阻断问题（见 3.1 节）。这些 printf 是排障期的眼睛，现在删掉等于蒙眼调试。
- **怎么做**：等 GPIO 中断在板上验证通过后（按 P29 按键能在 NSH 看到回调触发），一次性删除全部 `gpio1:` 打印和 isr_count 计数器。
- **怎么验证**：编译通过 + 按键中断功能仍正常。

### B2. 两个未接入构建的 GPIO 测试文件

- **是什么**：`bk7258_gpio_foundation_test.c`（310 行）和 `bk7258_gpio_irq_test.c`（650 行）。
- **现状**：它们**不在 Make.defs / CMakeLists.txt 里**，根本不参与编译——属于"放在目录里的手工测试脚手架"。
- **建议**：
  - 如果 GPIO 验证还要用：给它们补 Kconfig 门控（仿照 `BK7258_SDK_IRQ_TIMER_TEST` 的模式），明确"默认关闭、测试时手动开"。
  - 如果验证已完成：直接删除文件。
- **不要做的事**：在没有 Kconfig 门控的情况下把它们加进构建——会把测试代码带进所有固件。

---

## 3. 修复项：不是清理，是必须先解决的问题

### 3.1 【阻断】`bk7258_gpio_lowerhalf.c` 调用了不存在的函数

- **是什么**：约第 308、317 行调用 `bk7258_sdk_irq_snapshot_handler()`，第 665 行调用 `bk7258_sdk_irq_get_source37_count()`。**这两个符号在整个代码树里不存在**。现存的是 `bk7258_sdk_irq_test_snapshot_handler()`（名字不同，且被 TEST 开关门控）。
- **为什么现在没炸**：这个文件还没接入构建系统（Make.defs/CMakeLists 都没有它），不编译就不会链接失败。
- **怎么做**（接入构建前的必修课）：
  1. 在 `bk7258_sdk_irq.c` 里把 snapshot 功能提为正式 API（不带 TEST 门控）：仿照现有 `bk7258_sdk_irq_test_snapshot_handler` 实现，命名与调用处对齐；
  2. 如果需要 source37 派发计数，在 `bk7258_sdk_irq.c` 的 dispatch 函数里加一个计数器并暴露查询函数；不需要就删掉第 663-665 行的打印；
  3. 给 GPIO 文件补 Make.defs/CMakeLists/Kconfig 接线；
  4. 编译链接通过后，再做 B1 的 printf 清理。
- **怎么验证**：`grep -rn "bk7258_sdk_irq_snapshot_handler" $BOARD/chip/` 的每个调用点都能找到定义；编译链接通过。

### 3.2 【功能缺陷】`os_adapt.c` 拒绝创建带参数的线程

- **是什么**：`rtos_create_thread()`（约第 276-286 行）发现 `arg != NULL` 时只打印一句错误就返回失败——线程根本没创建。
- **为什么要紧**：SDK 的 WiFi/BLE 库里很多线程是带参数创建的。现在 WiFi 库没链接所以没暴露；一旦进入 N8，这会是第一批炸点。
- **怎么改（思路）**：NuttX `kthread_create` 支持 argv 传参，把 SDK 的单个 `arg` 包装成单元素 argv 传进去即可。
- **怎么验证**：写一个调用 `rtos_create_thread` 且 arg 非空的最小用例，确认线程能跑起来并收到正确的 arg。

### 3.3 【语义不符】消息队列的两个超时坑

- **是什么**（`os_adapt.c` 约第 692-779 行）：
  1. `rtos_push_to_queue(timeout_ms=0)`：FreeRTOS 语义里 timeout=0 是"队列满就立刻失败"，当前实现却是**阻塞等待**——发送方可能永远卡住；
  2. 队列用 `/tmp/<name>` 命名且 `O_CREAT` 不带 `O_EXCL`：同名队列二次创建会**静默打开旧队列**，两个逻辑队列互相踩踏。
- **为什么要紧**：WiFi Supplicant 的心跳发送、BLE 的 HCI 事件都依赖非阻塞队列语义；Supplicant 重启场景必然重名。
- **怎么改（思路）**：timeout=0 路径改用 try-send；队列名加唯一后缀（如指针值），或打开时加 `O_EXCL` 并在失败时先 unlink 再重建。
- **怎么验证**：构造"队列满 + timeout=0"用例，应立刻返回失败而不是挂起。

### 3.4 【疑似 2 倍频 bug】320 MHz 档的 SysTick 频率口径矛盾 —— 需板端复核

- **是什么**：
  - `bk7258_dvfs.c` 注释（约第 87-89 行）和 Kconfig 都写"320 档时 CPU0 实际 = **160 MHz**"；
  - 但 `bk7258_clockdiag_current_cpu_hz()`（`bk7258_clockdiag.h` 约第 220-221 行）对同一档返回 **320000000**；
  - SysTick reload 是用这个返回值算的。如果 CPU0 真在 160 MHz，`sleep 20s` 实际会变成 40 s。
- **为什么不确定**：`cpu0_speed` 分频位也可能只影响 WFI 休眠态时钟（那样 320 MHz 就是对的）。静态分析无法裁决，**这正是 git log `6b29ee8` "修 sleep=20s" 当时验证过的路径**，但注释与代码的口径矛盾至今没消除。
- **怎么做**：板上跑 `sleep 20`，用秒表/逻辑分析仪量实际时长。
  - 若实际 20s → 代码对，改掉 dvfs.c/Kconfig 里 "160 MHz" 的过时注释；
  - 若实际 40s → `clockdiag` 的 DPLL320 分支应返回 160000000，修正后重新验证 tick。

### 3.5 【小修小补】三处随手能改的

| 文件 | 问题 | 改法 |
|---|---|---|
| `bk7258_dvfs_procfs.c`（约第 173 行） | `echo "cur_freq 99" > /proc/dvfs` 越界写入静默无效 | 检查 `bk7258_dvfs_set_freq` 返回值，失败时 write 返回 `-EINVAL` |
| `bk7258_clk_ll.h`（约第 102-109 行） | DVFS 电压稳定延时按 26 MHz 标定，160 MHz 下每次多等 6-12 倍（且在关中断区内） | 按当前频率缩放循环数；影响小，可延后 |
| `bk7258_flash_mtd.c`（约第 241-248 行） | JEDEC ID 白名单只认 4 个型号，换料即拒挂 | 量产前改为"告警但继续"；验证期保留也合理 |

---

## 4. 文档债：过时的注释（不改代码，只改字）

这些注释描述的是 N1/N2 时代的旧世界，会误导后来者：

| 文件 | 位置（约） | 现状 | 应改为 |
|---|---|---|---|
| `chip/Make.defs` | 第 9-19 行头部 | 还说 vectors.c 是 "66 entries"、start.c "does NOT call nx_start()" | 80 项向量表；N2 起调用 `nx_start()` |
| `bk7258_lowputc.c` | 第 13-14 行 | 还说 serial.c "reuses the same MMIO" | serial.c 已是 SDK wrapper，不再直接碰寄存器 |
| `bk7258_start.c` | 第 19-22 行 | 内存图还是 640 KiB 整片 + MSP 0x2809FFFC | N7 后 CP 独占 320 KiB（0x28000000..0x2804FFFF），MSP=0x2804FFFC |
| `bk7258_allocateheap.c` | 第 20、48 行 | `_eheap = 0x2809FFFC` | CP 的 `_eheap` 现为 0x2804FFFC |
| `bk7258_vectors.c` | 第 1-59 行 | 约 50 行 N1 史话 | 压缩为 3-5 行永久性设计说明（magic slot 为什么是 64/65） |

**怎么验证**：纯注释修改，编译通过即可；`git diff --check` 确认没有误删代码行。

---

## 5. 架构级风险（先记账，N8 前必须处理）

这两条不是现在能"清理"掉的，是 WiFi/BLE 适配的前置债务，先记录在案：

1. **事件标志退化为二值信号量**（`os_adapt.c` 约第 839-863 行）：`rtos_wait_for_event_flags` 忽略了"等哪些标志"，任何 set 都唤醒。WiFi Supplicant 和 BLE SMP 的多事件状态机跑在上面会虚假唤醒。N8 开工前需要实现真正的多 bit 事件组。
2. **`apctl_main`/`bkirqtest_main` stub 与 hello_app 的潜在重复定义**（`bk7258_sdk_stubs.c` 尾部）：stub 没有 Kconfig 互斥守卫，hello_app 一旦启用就会链接冲突。清理 A2 关闭 `SDK_IRQ_TIMER_TEST` 后，`bkirqtest` 的真实实现来源也要一并理顺。

---

## 6. 建议执行顺序（抄作业版）

```text
第 1 批（纯删除，零风险）
  A1 删 systick_probe          → 编译 → 过了就进入下一批
第 2 批（改开关，低风险）
  A2 关 PROBE + TIMER_TEST     → 重新 configure + 编译 → 串口确认无 N4Clk 行
第 3 批（删标记，低风险）
  A3 清 WDT 五处标记            → 编译 → 板上确认 watchdog 正常
第 4 批（改注释，零风险）
  第 4 节文档债一次改完          → 编译 + git diff --check
第 5 批（修复，中风险，各配验证）
  3.4 先板测 sleep 20 定口径 → 3.1 GPIO 符号补齐 → 3.2/3.3 os_adapt 修复
第 6 批（验证后清理）
  GPIO 板测通过 → B1 删 printf → B2 决定测试文件去留
```

每批做完跑一遍第 0.2 节的编译命令；涉及板上行为的批次（3、5、6）必须先烧录验证再进行下一批。

---

## 7. 术语速查

| 术语 | 一句话解释 |
|---|---|
| bring-up | 让一块新板子从"点不亮"到"跑得起来"的全过程 |
| 探针（probe） | 为确认"代码走到这里了"而临时加的打印/标记 |
| boot-trace marker | 启动早期用 `up_putc` 打的单字符路标（控制台初始化前唯一可靠的输出） |
| 门控（gating） | 用 `#ifdef CONFIG_X` 或 Make 条件把代码隔离在开关后面 |
| lower-half | NuttX 驱动分层里贴硬件的那一半；upper-half 是通用框架 |
| stub | 为了让链接通过而写的空实现，代替真实功能 |
| wrapper | 不碰寄存器、只转发调用到 SDK API 的驱动写法 |
| board-verified | 该功能已在真实板子上实测通过（区别于"编译通过"） |
