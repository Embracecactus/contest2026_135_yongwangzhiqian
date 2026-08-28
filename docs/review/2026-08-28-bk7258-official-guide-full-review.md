# BK7258 全仓官方适配指导评审

[English](TBD) | 简体中文

> **文档状态：统一复核结论。** 本文吸收第一轮施工单、重构后复核与
> `docs/platforms/bk7258/official-compliance-review.md` 的有效发现，并记录后续
> 交叉核验结果。若其他 2026-08-28 评审与本文冲突，以本文和源码现状为准。

- 评审日期：2026-08-28（第二轮复审见第八节，复核提交 `7af5946` 之后的暂存改动）
- 复审日期：2026-08-28
- 评审范围：`contest2026_135_yongwangzhiqian` 全仓（`chips/`、`boards/`、`tests/`、`tools/`、`app/`、`quickapp/`、`integration/`、`prebuilt/`、`docs/`、`progress/`）
- 评审基准：openvela 官方 `dev-ai-contest-2026` 文档全量镜像 `docs/openvela-guides/`（319 页，2026-08-28 拉取，等同 <https://doc.openvela.com>）；另以工作区官方模板实物 `vendor/template/` 做逐文件骨架对照
- 判定口径：**🔴 硬性缺陷**（官方明确要求缺失、会导致复现/构建失败或验收扣分）；**🟠 偏离但已论证**（需在交付文档显式声明，否则评审按字面核对时首轮即判不符）；**🟡 建议项**
- 复核说明：本评审为独立复核，对 `docs/platforms/bk7258/official-compliance-review.md` 的自评结论逐条抽查，标注"自评一致 / 自评不成立 / 新增发现"

## 一、结论速览

| 维度 | 官方条款 | 判定 | 关键证据 |
|---|---|---|---|
| 芯片层接口（串口/中断/堆/启动） | 1443 二章、1444 一章 | ✅ 符合 | `chips/bk7258/common/bk7258_serial.c:794,813,829`、`bk7258_irq.c:186,402,372,133`、`bk7258_allocateheap.c:68` |
| 中断宏与优先级 | 1444 一.2 | ✅ 符合（取值自洽） | `chips/bk7258/include/irq.h:60-120` |
| 定时器模型 | 1443 二.3 | 🟠 arch_timer，非优先推荐的 arch_alarm | `chips/bk7258/common/bk7258_timerisr.c:500,507` |
| `__start` 时钟初始化 | 1443 二.1 | 🟠 延后到 `sys_drv_init()` | `chips/bk7258/cp/bk7258_start.c:256-270`、`common/bk7258_sdk_runtime.c:81` |
| `up_trigger_irq` / `up_secure_irq` | 1444 一.6/7 | 🟠 条件性未实现 | 全仓 0 命中；TrustZone 关闭、IPI 走 SDK mailbox |
| 板级目录骨架 | 1443 三章 | ✅ 符合（命名偏离） | 三板 `Kconfig`/`include/board.h`/`scripts/Make.defs`/`src/` 齐备 |
| 板级初始化阶段 | 1443 三.1、1456 | ✅ 符合 | `common/src/bk7258_boot.c:27`、`bk7258_appinit.c:33`、`bk7258_finalinit.c:103` |
| 链接脚本 | 1443 三.3 | ✅ 符合 | `common/scripts/ld.script:59,169-174`、`:88,130` |
| ETC ROMFS | 1443 三.2 | 🟠 无 `group`/`passwd` | 未启用登录，官方为可扩展示例 |
| `configs/nsh` 命名 | 1443 三.4 | 🟠 采用 CP/AP 配对配置 | 10 个 defconfig，无 `nsh` |
| 构建入口 | 1443 四章 | 🟠 wrapper 而非 `./build.sh`，底层一致 | `tools/bk7258/_lib/build.py:786-790` |
| 产物命名 | 1443 四章 | 🟠 非 `vela_ap.bin` | `README.md:112-114` 已声明多镜像设计 |
| 必测自测用例 | 1443 五章 | 🔴 正式验收矩阵未闭环 | GPIO/UART 有功能证据但缺 loopback 专项；I2C/SPI 传输专项和 RTC 未闭环 |
| 稳定性测试 | 1443 五章 | 🔴 12 小时资格长稳未完成 | 已有约 14 分钟连续压力和两轮无注入 soak；12 小时项目门禁明确延期 |
| 性能测试 | 1443 五章 | ✅ 有实板数据 | `2026-08-27-bk7258-p0-diagnostics-performance.md:17-20` |
| Vendor 仓定位 | 1445 | ✅ 符合 | `contest2026_135_yongwangzhiqian.xml:25-26` linkfile |
| 许可证 | 1650:51 | 🔴 tests 等新增源码头覆盖不足；🟡 顶层 LICENSE 待决策 | `tools 11/11` 已修；`tests 31/160` 待溯源整改 |
| 文档与源码一致性 | 仓库分层规则 | 🔴 已确认死引用已修；仍有历史与分层问题 | 见第五节 |

**一句话结论**：芯片层与板级层的**功能适配**齐全，自评结论基本成立；当前真正影响
验收的是正式外设自测矩阵、12 小时资格长稳和 tests 等新增源码许可证头。TOUCH 已
前移为配置期门禁；README 的 SDK 默认分组说明经 repo 源码复核确认正确，不属于整改项。

## 二、芯片层（`chips/bk7258/`）

### 2.1 符合项（抽查证据）

| 官方要求 | 实现 | 证据 |
|---|---|---|
| `up_putc` | UART / RTT 双实现 | `common/bk7258_lowputc.c:346`、`common/bk7258_rtt_lowputc.c:31` |
| `arm_earlyserialinit` / `arm_lowputc` | ✅ | `common/bk7258_serial.c:794`、`common/bk7258_lowputc.c:280` |
| `arm_serialinit` + `uart_register("/dev/console")` | ✅，另注册 ttyS0/1/2 | `common/bk7258_serial.c:813-832`（console 在 `:829`） |
| `__start` 清 BSS / 拷 .data / 调 `nx_start()` | ✅（CP+AP 双入口） | `cp/bk7258_start.c:199-204,191-195,282`、`ap/bk7258_ap_start.c:263-269,236-242,273` |
| 堆注册 | ✅ `up_allocate_heap()`（现代 NuttX 命名） | `common/bk7258_allocateheap.c:68-76`，起始 `g_idle_topstack`、大小 `_eheap - g_idle_topstack` |
| 中断栈 / IDLE 栈 | ✅ 全部 defconfig 配置 | `CONFIG_ARCH_INTERRUPTSTACK=2048`、`CONFIG_IDLETHREAD_STACKSIZE=2048` |
| `up_irqinitialize` 全动作 | ✅ 关中断→VTOR→默认优先级→挂 SVCall/HardFault→开中断 | `common/bk7258_irq.c:197-200,206,266-281,285-286,305` |
| `up_enable_irq` / `up_disable_irq` / `up_prioritize_irq` | ✅ | `common/bk7258_irq.c:402 / 372 / 133` |
| `up_irq_save/restore/enable/irqstate` | ✅ 由 ARMv8-M 架构公共代码提供 | 芯片层仅调用（`ap/bk7258_pm_coord_ap.c:211`、`cp/bk7258_wdt.c:174`） |
| irq.h 七宏 | ✅ 取值按 STAR NVIC 3 位优先级调整，有 `#error` 守卫 | `include/irq.h:60-120,126-130` |
| 构建覆盖 | ✅ 122 个 `.c` 零悬空、零遗漏 | `Make.defs:56-534`；`CMakeLists.txt:304,412,432` 三个 `foreach` 覆盖 13 个"看似漏编"驱动 |

最后一项与 `official-compliance-review.md:89` 的自评一致：**"CMake 漏编 13 个驱动"不成立**，13 个名称正好由三个 `foreach` 循环生成。

### 2.2 🟠 偏离但已论证

| 项 | 现状 | 论证 | 证据 |
|---|---|---|---|
| 定时器用 arch_timer，无 oneshot | 构造 `struct timer_lowerhalf_s`，`up_timer_set_lowerhalf()` | 32 kHz 外部 SysTick + 整除断言 + 待机相位补偿；1443 明确两种模型并存，alarm 为"优先推荐"非强制 | `common/bk7258_timerisr.c:92,500,507,71-83` |
| `__start` 未初始化系统时钟 | 仅有可选 `CONFIG_BK7258_CLOCK_240M`，实际时钟初始化延后 | SDK `sys_drv_init()` 依赖调度器/互斥锁，无法在 `__start` 阶段调用；默认频率由 BL1 建立 | `cp/bk7258_start.c:256-270`、`common/bk7258_sdk_runtime.c:59,81` |
| 无 `up_trigger_irq` / `up_secure_irq(_all)` | 全仓 0 命中 | 核间中断走 SDK mailbox；TrustZone 在 direct-boot 下禁用 | `ap/bk7258_ap_ipi.c`、`Kconfig:40-44` |
| 无局部 `irq.h` / `_lowputc.h` / `_start.h` | 只有公共 `include/irq.h` | 1443 展示的是典型目录，非强制清单；模板 `vendor/template/chips/chip_name/` 确有 `vendor_name_irq.h`，此处为字面对齐偏差 | `chips/bk7258/include/irq.h` |

**注意**：官方模板 `vendor/template/chips/chip_name/` 含 `vendor_name_oneshot.c`，本仓无对应文件。这是与模板实物对照后新增的发现，自评文档未提及。

### 2.3 🟡 分层与建议

1. **分层轻度越界**：`chips/bk7258/ap/PWM_BLOCKED_ROOT_CAUSE.md:82` 把 `t5_board_bringup` 上的验证套件称为 "canonical replacement"，属按单板得出的结论，违反 `docs/README.md:10`「chips 层不得放单板引脚或单板验收结论」。建议迁到 `docs/platforms/bk7258/` 或 `progress/verification/`。
2. **12 个自检文件位于 chips 层**：`ap/bk7258_{aud,mic,saradc,temperature,jpeg_m2m}_validation.c`、`cp/bk7258_{gpio_foundation,gpio_irq,sdk_irq_timer}_test.c`、`cp/bk7258_sdk_timer_selftest.c`、`common/bk7258_{bt,rpmsg,rpmsgfs}_test.c`。其中 `ap/bk7258_mic_validation.c:6` 自述 "Bounded **real-board** validation"。缓解因素：全部由 `CONFIG_BK7258_*_VALIDATION/TEST` 控制、默认 `n`、不绑定板名，性质接近"芯片 ABI 自检"。建议改述标题并把实板记录迁至 `progress/verification/`。
3. `Make.defs:26` 把 `bootloader/bl2` 加入 VPATH 但主构建未列出 bl2 源（bootloader 走独立 `Makefile`），属冗余项，无害。

## 三、板级层（`boards/bk7258/`）

### 3.1 符合项

三块板（`t5ai_core` / `t5_board` / `aidk_ai_toy`）均具备：`Kconfig`、`include/board.h`、`scripts/Make.defs`、`src/{Make.defs,CMakeLists.txt}`、`src/etc/init.d/{rcS,rc.sysinit}`。

- **初始化阶段正确**：`board_late_initialize`（`common/src/bk7258_boot.c:27`）→ `board_app_initialize`（`bk7258_appinit.c:33` → `bk7258_bringup.c:60`，先注册 DVFS procfs 再建 MTD/FTL，挂载在 `rc.sysinit:10,16`）→ `board_app_finalinitialize`（`bk7258_finalinit.c:103-154`，逐项 statfs 校验）。`board_early_initialize` 有意省略，早期工作由 CP `__start` 承担，见 `official-compliance-review.md:57-64`。
- **链接脚本合规**：`ENTRY(_vectors)` + `EXTERN(_vectors)`（`ld.script:59,39`、`ld_ap.script:29,18`）、`.arm.exidx` 段（`ld.script:169-174`、`ld_ap.script:126-131`）、20+ 处 ASSERT。无 PSRAM MEMORY 区（PSRAM 为运行时 SDK 堆，经 `mm_addregion` 注入），非缺陷。
- **`common/` 共享机制合规**：`BOARD_COMMON_DIR` 是 NuttX 官方机制（`nuttx/tools/Config.mk:185-188`、`Unix.mk:304-307`），与上游 `nuttx/boards/arm/cxd56xx/common/Makefile:25` 写法一致。
- **零悬空引用**：三板 + common 的 `src/Make.defs`、`src/CMakeLists.txt` 中全部 `.c` 与 `etc/init.d/*` 均命中实体文件。
- 三板无 `include/nsh_romfsimg.h`：该文件由构建系统在 `apps/nshlib/` 自动生成（`nuttx/Documentation/applications/nsh/customizing.rst:101`），**非缺陷**。

### 3.2 潜在缺口与本次处置

1. **`bk7258_board_cp_devices_initialize()` 无生产实现，已前移为配置门禁**
   声明在 `common/include/bk7258_board.h:106`，调用在
   `common/src/bk7258_cp_bringup.c:92`，唯一定义在测试桩。`BK7258_TOUCH` 现已移除
   用户可见 prompt，且没有维护板选择它，因此当前配置无法进入链接缺口。两套构建后端
   还会拒绝 AP 侧误选。未来板必须先实现函数与链接测试，再由自身依赖
   `!BK7258_AP_CORE` 的板级 selector 选择 `BK7258_TOUCH`。

2. **板级 README 的旧钩子说明已修复**（本次整改）
   原文引用不存在的 `bk7258_board_early_initialize()` 与
   `bk7258_board_devices_initialize()`。现已改为每板
   `bk7258_board_ap_initialize()` 配合共享 controllers/buses/finalize 三阶段，并单独
   说明 `CONFIG_BK7258_TOUCH` 所需的 CP board hook。

### 3.3 🟠 偏离（需在交付文档声明）

| 项 | 现状 | 说明 |
|---|---|---|
| 无 `configs/nsh` | 10 个 defconfig：`openvela_cp` / `openvela_ap`（各板）+ `perf`、`xts`（t5_board）+ `drivercheck_cp/ap`（t5ai_core） | CP 入口已是 `nsh_main`（`t5ai_core/configs/openvela_cp/defconfig:50`），缺的是**官方默认命名**不是 NSH 功能；`CONFIGS.md:39-49` 论证了 CP/AP 必须成对。官方 1443:608 建议"不要增加太多配置"，10 个属偏多，但每个都有明确角色且 `CONFIGS.md:168-174` 明令禁增 |
| 链接脚本集中在 `common/scripts/` | 板目录只有 `scripts/Make.defs`（3 行） | 官方示例为板内脚本；共享决策非缺陷（1443:34 鼓励相似板共享）；`Make.defs:88` 用 `=` 而非 `+=`，会覆盖此前 ARCHSCRIPT 值，当前无影响 |
| 无 `etc/group` / `etc/passwd` | 三板均无 | 全仓 defconfig 无 `CONFIG_NSH_LOGIN`/账户相关项，登录未启用；1443:544 的 `RCRAWS` 是可扩展示例 |
| 工具链由环境变量注入 | `CROSSDEV` 取自 `BK7258_TOOLCHAIN_BIN`，缺失即 fail-closed | 官方建议放 `prebuilt/` 并由 board `Make.defs` 指定（1443:647）；本仓由构建器注入，**脱离 `bk7258.py` 直接 `./build.sh` 会失败**，`README.md` 已补安装步骤 |

### 3.4 🟡 重复与死代码

- **AP 角色下 `board_app_initialize` 是死代码**：`common/src/Make.defs:10` 无条件编译 appinit/bringup，但 AP 入口是 `bk7258_ap_main` 且未启用 `CONFIG_SYSTEM_NSH`，而 `board_app_initialize` 只由 NSH 经 boardctl 调用 → 永不执行。建议加 `#ifndef CONFIG_BK7258_AP_CORE` 门控。
- **AP 常规驱动初始化不在 late 阶段**：发生在 `bk7258_ap_main()`（`common/src/bk7258_ap_entry.c:101`），late 阶段对 AP 只做 `bk7258_ap_platform_prepare()`。与 1443:490 不一致，但 AP 由 CP 拉起、需 CP 服务就绪，属有据决策。
- **8 类跨板重复**（未下沉 `common/`）：
  1. `src/etc/init.d/rc.sysinit`、`rcS` — 除第 2 行路径注释外内容相同（文件字节与哈希不同）
  2. `include/board.h` — 三份 11 行，仅 include guard 不同
  3. `*_audio.c` — t5ai_core 与 t5_board **约 90% 相同**（26 个 diff 行）
  4. `g_bk7258_board_gpio_config` 初始化块 — 三板逐字相同
  5. 板根 `CMakeLists.txt` — diff 仅 2 行
  6. `cmake/Toolchain.cmake` — 三份各 2 行
  7. `scripts/Make.defs` — 三份各 3 行（必要的板级选择器）
  8. 20 个 `BK7258_BOARD_*` 能力宏在三板头重复出现，**无共享模板、无静态断言**
  补充风险：`t5ai_core/include/bk7258_board_config.h` 仅 47 行，aidk 有 218 行，覆盖密度差异大；新增第四块板极易漏定义契约宏且只在启用某驱动时才暴露。

## 四、Vendor 仓、构建与测试

### 4.1 ✅ 符合项

- **Vendor 仓定位正确**：`contest2026_135_yongwangzhiqian.xml:25-26` 用 linkfile 把 `chips/bk7258`、`boards/bk7258` 部署到 `vendor/beken/`；主仓提供 `apps/external/frameworks/nuttx/tests`（`openvela.xml:7,52,150,186,212`），本仓不重复。
- **构建底层确为官方入口**：`tools/bk7258/_lib/build.py:786-790` 以 `[build.sh, <config>, "--cmake", "-b", <out>]` + `-j<jobs>` 调用。
- **xTS 验收路径存在且核心板端用例已跑通**：`boards/bk7258/t5_board/configs/xts/defconfig:115-137` 启用主仓 `TESTS_TESTSUITES`。板端记录分别给出 mm 8/8、sched 16/16、ostest、getprime、mm、ramtest 与 tmpfs 上 scanftest 164/0；`fstest`、`driver_test` 等不能仅凭 defconfig 推定已执行。`281/281` 是同一记录引用的 host cmocka 回归总数，不是板端 xTS 总数。
- **性能有实板数据**：CoreMark / Ramspeed / Whetstone 各 10 次（`2026-08-27-bk7258-p0-diagnostics-performance.md:17-20`）+ 240 MHz 专项。

### 4.2 🔴 硬性缺陷

1. **官方通用自测的正式验收矩阵未闭环**
   已通过：内存、调度和 Watchdog 专项。T5AI-Core 另有 UART/NSH 持续收发、LED
   写读和按键电平实板证据，因此不能表述成 GPIO/UART 功能缺失；准确缺口是没有
   GPIO/UART 物理 loopback 专项，I2C/SPI 没有独立传输验收用例，RTC 仍未闭环。
   1443:671 将这些项目列为通用自测必测集，故正式证据矩阵仍可能直接扣分。

2. **12 小时资格长稳未完成，但不能写“稳定性零记录”**
   仓内已有约 14 分钟连续压力（30 次 AP start/stop、5000 条 mailbox 消息）以及
   N10 恢复后的两轮无注入 full-suite soak。项目自行定义的 12 小时 soak 仍于
   2026-08-27 明确延期，且官方 1443:661-665 要求稳定性准入；准确风险是缺发布级、
   有连续日志和健康判据的长稳记录。

3. **`xts/defconfig` 孤立符号已删除，与 scanftest 失败无因果证据**
   Kconfig 树没有 `PSEUDOFS_SOFTLINKS` 定义，本次已从 defconfig 删除。
   scanftest 首次失败的直接原因已由记录说明为 pseudo-root `/tmp` 不能创建普通文件；
   挂载 RAM tmpfs 后 164/0 通过。软链接配置不能解释普通文件能力，二者不得关联。

4. **新增源码许可证头仍未全部闭环；顶层 LICENSE 单独决策**
   1650 明确要求所有新增文件带标准许可证头。本次已为 `bk7258.py` 与 `_lib/*.py`
   补 SPDX，使报告原 `tools` 脚本口径（`*.py`/`*.sh`）从 3/11 达到 11/11；
   `tests 31/160` 等剩余文件需要
   先核对原创、上游复制或生成来源，不能机械改写第三方许可证。
   顶层没有 LICENSE 是交付与法律呈现风险，但 1650:51 本身没有明确要求每个 vendor
   仓必须新增顶层 LICENSE，且 SPDX 标识并不因缺本地副本自动失效；应与源码头整改
   分开定级。

### 4.3 🟠 呈现风险

| 项 | 现状 | 官方口径 | 说明 |
|---|---|---|---|
| 构建入口 | `tools/bk7258/bk7258.py build --board ... --boot direct --jobs 8`（`README.md:104-105`） | `./build.sh vendor/.../configs/nsh --cmake -j8`（1443:655） | 底层一致，但评审按字面核对首轮即判不符。另注：1443:655 写 `vendor/<vendor>/<board>/<chip>/configs/nsh`，与 1445:69-74 的 `boards/<chip>/<board>` 顺序相反，属官方文档笔误 |
| 产物名 | `boot.bin`/`cp.bin`/`ap.bin`/`pair.bin`/`bl2-a.bin`（`README.md:112-114`） | `vela_ap.bin`（1443:657） | 多镜像设计，已声明；`official-compliance-review.md:109` 同步记录 |

**建议**：把「与官方差异的显式声明」提到 `README.md` 首屏，并在 PR 描述中重复。

### 4.4 🟡 建议项

- `tools/bk7258/`（11 个文件）与 `integration/beken/` 无独立 README；`vendorsetup.sh` 未注入变量时是静默 no-op，建议补生效条件注释。
- `round4-ap/`、`round4-cp/` 为空目录、未被 git 跟踪，但也没写进 `.gitignore`，建议补两行（当前不进提交，无风险）。
- `tests/` 目录名与 1445:28「tests 由主仓提供」冲突，本仓实为主机端 mock 单测（`tests/bk7258/README.md:3-5` 已声明"不代替实板 xTS"），建议改名 `host-tests/` 或顶部加粗声明。
- Kconfig 规范：chip 76/216、board 8/44 缺 help 文本；70 处 `select` 目标带 `depends on`。需人工复核的高风险项：`chips/bk7258/Kconfig:1360`（`BK7258_AUD → ARCH_PERF_EVENTS`，依赖未被 select 的 `ARCH_HAVE_PERF_EVENTS`）、`boards/bk7258/common/Kconfig:242,201,119,155`。

## 五、文档与源码一致性及处置状态

下表记录复核发现及本次整改状态；已修项不再计入当前缺陷。

| # | 文档声称 | 位置 | 实际核查 |
|---|---|---|---|
| 1 | "SDK 项目没有 `notdefault` 标记，因此普通默认同步也会包含它" | `README.md:55-56`、`README_EN.md:63-65` | ✅ **README 正确，无需修改。** repo 给所有不含 `notdefault` 的项目隐式加入 `default`；额外 `groups="bk7258-sdk"` 不会移除默认成员关系 |
| 2 | 目录 `board/bk7258/bootloader/` | 原 `porting-report.md:784-789` | ✅ 已修为 `chips/bk7258/bootloader/`，并补现役 BL1/BL2 文件结构 |
| 3 | 现役命令调用已退役 `bk7236_pack_min_bootloader.py` | 原 `porting-report.md:788,808` | ✅ 已改为根 README 的 `bk7258.py build/package/verify`，并注明内部使用 `_lib/image.py` |
| 4 | 配置项 `CONFIG_BK7258_FLASH_LITTLEFS` | 原 `porting-report.md:615`、`next-stage-prompt.md:241` | ✅ 已改为 `BK7258_STORAGE_ONCHIP_PERSISTENT` → `BK7258_FLASH_MTD` + `FS_LITTLEFS` |
| 5 | 配置 `cp_nsh_psram` + `ap_smp_psram` | `platforms/bk7258/README.md:54` | 不存在，现役为 `openvela_cp` / `openvela_ap` |
| 6 | 配置 `ap_smp_online` / `ap_smp_affinity` | `platforms/bk7258/README.md:71,160` | 已退役，仅残留 `bk7258_ap_smp_affinity_selftest()`（`chips/bk7258/ap/bk7258_ap_smp.c:1641`） |
| 7 | 8 份文档曾引用 `docs/bk7258-t5ai/` 路径 | `memory/ARCHITECTURE.md`、`memory/OPERATIONS.md` 及 ADR-001/002/008/009/010/011 | ✅ 已把 9 个真实 Markdown 链接迁移到 `docs/platforms/bk7258/` 并验证目标实存；报告中的旧路径迁移叙述有意保留 |
| 8 | "RV1126B port 是当前实现" | `docs/rv1126b-nsh-port.md:3`、`docs/ai-worklog/README.md:9,21-22` | 主线已转 BK7258；`docs/rv1126b-sdk-integration.md` 全文无历史标注；`docs/verification/2026-07-*`（7 篇）无"已被 BK7258 取代"标注 |
| 9 | 活动分支 `fix/bk7258-sdk-profile-pins` | `progress/CURRENT.md:14` | 实际检出 `fix/bk7258-review-followup` |
| 10 | bootloader-analysis 索引"完整" | `platforms/bk7258/README.md:201-205` | 只列 4 个，实存 7 个，漏 `reverse-attempt-assets-N17.md`、`reverse-synthesis-N17.md`、`reverse-sop-cd-jlink.md` |

其他文档问题：

- **分层越界**：`docs/chips/bk7258/sdk-clock-operating-points.md:136-152` 整节为 T5-Board generation 146 的单板实板验收结论，违反 `docs/README.md:10`；`docs/learning/bk7258/` 有 4 处出现"当前状态/board-verified"（如 `40-subsystems/gpio/01-mental-model.md:158`），与 `docs/learning/README.md:141`「不复制 current 状态」自相矛盾。
- **中英不同步**：`platforms/bk7258/README_EN.md` 是 1.1 KB 索引（中文 41 KB），缺中文 `:15-19` 的「2026-08-10 N15 已退役」勘误——英文读者看不到最关键的现状更正。
- **路径漂移**：多处文档引用 `t5_board_cp_xts` / `t5_board_cp_perf`，实际目录是 `boards/bk7258/t5_board/configs/xts` 与 `perf`。
- **板级 README 失准**：`boards/bk7258/README.md:123-124` 称 AIDK "reviewed bindings cover SC7A20H I2C0 on P20/P21 and MFRC522 UART1 on P0/P1"，但全仓只有能力宏与引脚常量（`aidk_ai_toy/include/bk7258_board_config.h:54-55,204-206,213-214`），**无绑定源码、无 Kconfig 选项**。`:78` 称 AIDK 修订为 `schematic-only`，实际宏值为 `"schematic-v1.0"`。

## 六、交付完整性（新增发现，自评未覆盖）

复核开始时共有三份 2026-08-28 评审报告未跟踪：第一轮施工单、重构后复核和本文；
此前“只有两份”的统计漏掉了本文自身。本次为前两份增加历史快照声明、撤销 SDK
分组误判，并将本文设为统一结论，三份报告纳入同一 changeset。

TTS 研究目录仍单独保留在交付范围外：已增加醒目的“研究提案、尚未实施”声明和
板端基准未执行声明，但因涉及私人语音数据与产品范围，公开纳入 Git 前仍需 owner
确认脱敏与交付意图。

## 七、整改优先级

### P0（本次处置）

1. ✅ SDK 分组项确认为评审误判，README 保持不变
2. ✅ 修复 `porting-report.md` 与 `next-stage-prompt.md` 的现役配置、路径和打包入口
3. ✅ 统一三份 8 月评审口径并纳入同一 changeset
4. ✅ 修复 `boards/bk7258/README.md` 的旧钩子名

### P1（验收扣分或潜在风险）

5. 补齐正式外设验收矩阵：GPIO/UART loopback、I2C/SPI 传输、RTC
6. 补 12 小时发布级 soak 连续记录，或保持明确延期、健康判据与后续计划
7. ✅ 已删除 xTS 孤立符号；scanftest 的 pseudo-root 原因已明确，不建立错误关联
8. 🟠 `tools` 脚本（`*.py`/`*.sh`）已从 3/11 修至 11/11；`tests 31/160` 等按来源补正确许可证头；顶层 LICENSE 单独决策
9. ✅ `BK7258_TOUCH` 已无用户 prompt、当前无板选择，且 AP 侧误选有构建期守卫；未来板仍须补生产实现与链接测试

### P2（质量与可维护性）

10. 三板 `etc/init.d/rc.sysinit`、`rcS` 与 `include/board.h` 下沉 `common/`（NuttX 原生支持，`Board.mk:33`、`Unix.mk:291-295`）
11. 修 `docs/chips/bk7258/sdk-clock-operating-points.md:136-152` 与 `docs/learning/` 4 处分层越界
12. ✅ 已修 8 份 `memory/` 文档的 9 条 `docs/bk7258-t5ai/` 断链；RV1126B 历史标注仍待统一处理
13. 为 20 个 `BK7258_BOARD_*` 能力宏加静态断言头；AP 角色下门控 `board_app_initialize`
14. `platforms/bk7258/README_EN.md` 补 N15 退役勘误；修 `progress/CURRENT.md:14` 活动分支

### P0/P1/P2 状态追踪

见第八节复审记录。

### 建议统一声明的"架构差异"（避免评审按字面判不符）

- CP/AP 配对构建，非单镜像产品，故无 `configs/nsh`、产物不叫 `vela_ap.bin`
- 定时器选 arch_timer（32 kHz SysTick + 待机相位补偿），非官方优先推荐的 arch_alarm
- `__start` 不运行依赖调度器的完整 SDK `sys_drv_init()`；默认时钟由 BL1 建立，CP
  仅在专用性能配置下于 `nx_start()` 前切换官方 240 MHz OPP
- 未启用 TrustZone，核间中断走 SDK mailbox，故无 `up_secure_irq` / `up_trigger_irq`
- 链接脚本共享于 `common/scripts/`，各板通过分区 CSV 定制内存布局
- 构建入口为 `tools/bk7258/bk7258.py` wrapper，底层调用官方 `build.sh --cmake`

## 八、第二轮复审记录（2026-08-28）

复核对象：`fix/bk7258-review-followup` 上相对 `7af5946` 的暂存改动（18 个文件，+1425/-42，未提交）。

### 8.1 已修复（✅ 验证通过）

| 原项 | 修复方式 | 复核证据 |
|---|---|---|
| P0-2 `porting-report.md` 三处不存在引用 | 全部改为现役实现并加历史标注 | `:325` 打包链标为"历史验证链"并指向 `tools/bk7258/_lib/image.py`；`:613` 配置名改为 `CONFIG_BK7258_STORAGE_ONCHIP_PERSISTENT` 并说明其 select `BK7258_FLASH_MTD` + `FS_LITTLEFS`；`:781-796` 目录改为 `chips/bk7258/bootloader/` 并列出实存文件 |
| P0-2b `next-stage-prompt.md:241` 同一配置名 | 同步修正 | `git diff --cached docs/platforms/bk7258/next-stage-prompt.md` |
| P0-3 两份 8 月评审报告未跟踪 | 已 `git add`，且本文档一并纳入 | `git status --short` 现为 `A` 状态 |
| P0-4 板级 README 钩子名不存在 | 改为真实钩子并补 CP 约束说明 | `boards/bk7258/README.md:159-171`：现列 `bk7258_board_ap_initialize()` + `ap_controllers_initialize/ap_buses_initialize/ap_finalize_initialize`，并注明 CP 侧 attached device 须提供 `bk7258_board_cp_devices_initialize()` |
| P1-7 `xts/defconfig:127` 孤立符号 | 删除该行 | `boards/bk7258/t5_board/configs/xts/defconfig` 已无 `CONFIG_PSEUDOFS_SOFTLINKS` |
| P1-8 `tools/` 许可证头 | 8 个 py 全部补 `SPDX-License-Identifier: Apache-2.0` | `*.py`/`*.sh` 脚本口径从 `tools 3/11` → **`tools 11/11`** |
| P1-9 TOUCH 链接缺口 | 移除用户可见 prompt，改为只能被 `select` 的门禁 | `chips/bk7258/Kconfig:1333-1334` 现为 `bool`（无 prompt）+ `default n`；全仓无 `select BK7258_TOUCH`，当前配置期无法启用 |

`porting-report.md:781-796` 新列的 bootloader 文件已逐项验证实存：`start.S`、`boot_main.c`、`boot_runtime.c`、`bootloader.ld`、`bl2/`、`Makefile`、`README.md`（`git ls-files chips/bk7258/bootloader`）。

回归验证：`python3 -m py_compile` 对 `tools/bk7258/bk7258.py` 与 `_lib/*.py` 全部通过；`bk7258.py --help` 顶层命令仍为 `build/toolchain/sdk/package/release/verify`，与 `AGENTS.md:20` 一致（许可证头插入未破坏 shebang 与 CLI）。

### 8.2 复审争议项裁决

| 复审说法 | 裁决 | 复核证据 |
|---|---|---|
| 根 README 的 SDK 默认分组说明错误 | ❌ 复审误判，README 保持不变 | 随附 repo 的 `Project.MatchesGroups()` 明确给所有不含 `notdefault` 的项目隐式加入 `default`；官方 manifest-format 也明确普通项目隐式属于 `default`。直接调用当前实现得到 `default=True`、加入 `notdefault` 后才为 `False`。`groups="bk7258-sdk"` 是增加组，不是替换默认组 |
| 稳定性测试零记录 | ❌ 复审误判；准确缺口是无 12 小时资格长稳 | 本文 `:147-151` 已列约 14 分钟连续压力和两轮无注入 full-suite soak；`progress/CURRENT.md:135,187` 与 xTS 完成记录明确写 12 小时 soak 延期 |
| 删除孤立符号后，旧 281/281 与当前 defconfig 不对应 | ❌ 混淆两类证据 | `281/281` 是 host cmocka，与板端 xTS defconfig 无关。未定义符号不会进入生成配置；删除它只清理无效输入。板端 scanftest 已在 tmpfs 上 164/0，通过原因与软链接无关。新的实板 xTS 仍可作为发布复验，但不是这次一行清理的因果回归 |
| 顶层无 LICENSE | 🟠 事实成立，是否新增须由 owner 确认许可范围 | SPDX 标识自身有效，不以仓内是否复制许可证全文为条件；但顶层许可证文本有利于交付。仓库含第三方与历史材料，不能仅凭 tools 头部擅自声明全仓 Apache-2.0 |
| `tests` 与少量其他源码缺许可证头 | 🟠 事实成立，需按来源修复 | 不能把 129 个来源未核实文件机械标为 Apache-2.0；应先区分原创、上游派生、夹具与生成物 |
| 正式必测矩阵未闭环 | ✅ 成立 | GPIO/UART 已有功能证据，但正式 loopback、I2C/SPI 专项传输和 RTC 仍待实板补齐 |

### 8.3 新增观察（第二轮发现）

1. **✅ `select` 绕过 `depends on` 的 TOUCH 风险已加固**
   `BK7258_TOUCH` 继续无 prompt；chip CMake 与 Classic Make 入口新增
   `CONFIG_BK7258_TOUCH && CONFIG_BK7258_AP_CORE` 构建期错误。Kconfig 帮助与中英文
   符合性文档统一要求未来板级 selector 自身依赖 `!BK7258_AP_CORE`。

2. **✅ 三份报告主从关系和 TOUCH 口径已收敛**
   第一轮与第二轮报告明确标为历史快照，第二轮 R-08 增加后续处置状态；本文是唯一
   当前统一结论。历史代码片段保留审计价值，但不得作为当前整改动作来源。

3. **✅ `memory/` 9 条真实断链已修复**
   8 个文档中的旧 `docs/bk7258-t5ai/` 链接均改到 `docs/platforms/bk7258/`，目标逐一
   验证实存；评审报告中的旧路径迁移叙述有意保留。

4. **🟡 `research/bk7258-offline-voice-tts/` 仍未跟踪**
   `git status` 仍为 `??`，5 个文件不进交付物。若属废弃研究建议删除；若保留建议补"未实施/研究"
   横幅并跟踪。当前本地目录已补横幅，但因私人语音数据与产品范围尚未确认，仍不纳入 changeset。

### 8.4 第二轮后的整改队列

**P0：无。** SDK 分组项为误判，其余文档复现阻塞项已关闭。

**仍需 owner/实板的 P1：**

1. 决定仓库许可范围后补顶层许可证文本；逐文件确认剩余源码和测试夹具来源，再补正确头部。
2. 补正式 GPIO/UART loopback、I2C/SPI 传输、RTC 板端记录。
3. 执行 12 小时发布级 soak；当前保持“已有短时稳定性证据、12 小时延期”的准确状态。
4. 若需要当前提交代的发布复验，重新构建并执行板端 xTS；不得把 host 281/281 写成板端结果。
5. 由 owner 决定 TTS 研究目录的脱敏、跟踪或删除，以及 RV1126B 历史文档标注范围。

## 九、本次整改验证

- `./tests/bk7258/run_tests.sh`：退出码 0，最终打印 `BK7258_HOST_TEST_PASS`
- 8 个 `tools/bk7258/**/*.py`：Python AST 解析通过，SPDX 覆盖 8/8
- TOUCH 配置门禁：`BK7258_TOUCH` 无用户 prompt；维护板 Kconfig 与全部 defconfig
  均无 `select`/赋值，当前产品配置不可达；CMake 与 Classic Make 拒绝 AP 侧误选
- TOUCH 守卫回归：CMake 和 Classic Make 的 AP+TOUCH 负向配置均命中预期错误；
  host 公共头审计恢复为 86 个头、default/CP/AP、C11/C++17 全通过
- `CONFIG_PSEUDOFS_SOFTLINKS`：当前维护 defconfig 0 命中
- repo 分组实测：`groups={all,bk7258-sdk}` 匹配 `default=True`；加入 `notdefault` 后为 `False`
- `memory/` 中作为实际 Markdown 链接的 `docs/bk7258-t5ai/`：0 命中；新目标均存在
- `git diff --check` 与 `git diff --cached --check`：通过
