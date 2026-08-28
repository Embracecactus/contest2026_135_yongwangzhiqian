# BK7258 官方符合性复核说明

[English](official-compliance-review.en.md) | 简体中文

- 复核日期：2026-08-28
- 复核提交：`ce435f5ed66f5744339a3dc846405d9f7bc4b93a`
- 对照版本：openvela `dev-ai-contest-2026` 文档 1443、1444、1445
- 判定口径：区分硬性接口、推荐方案、示例目录和 BK7258 架构差异；源码存在、配置
  可达、构建产物和实板证据分别陈述

## 结论

原评审发现了一个真实的可选功能链接缺口和一个高价值交付文档缺口，但把多项
推荐/示例写法误判为强制要求，并且漏读了 CMake 中生成源文件名的三个循环。

- **真实代码缺口：**开启 CP `CONFIG_BK7258_TOUCH` 时会调用
  `bk7258_board_cp_devices_initialize()`，当前生产板目录没有定义，唯一实现是主机
  测试桩。现有维护 defconfig 均未开启该选项，因此当前产品构建不受影响；在任何板
  启用该功能前必须补板级实现和构建门禁。
- **已修复的交付文档缺口：**原根 `README.md` 仍保留大赛模板，且没有在主路径写明
  工具链安装、SDK bundle 重建、配对构建和产物命名。本次已用中英文作品说明替换。
- **已修复的文档归属问题：**原 `docs/bk7258-t5ai/` 实际覆盖三块板，现已迁移为
  `docs/platforms/bk7258/`；学习资料迁为 `docs/learning/bk7258/`，逆向资料改名为
  `bootloader-analysis/`，T5AI Core 专属探针下沉到
  `hardware/t5ai-core/probe/`。现役源码、导航和验证文档中的引用同步更新。
- **不是缺口：**所谓“CMake 漏编 13 个驱动”不成立；13 个名称正好由三个
  `foreach` 循环生成。当前 CMake/Ninja 产物也包含维护 profile 已启用的
  `bk7258_aud.c`、`bk7258_i2c.c`、`bk7258_mic.c` 和 `bk7258_rtc.c` 对象。

因此不接受把 1443/1444/1445 无差别全部降为 🟡。1444 的核心中断适配仍为 ✅；
1443 和 1445 应同时显示“功能适配”和“官方模板目录字面对齐”两个维度。

## 架构差异及其边界

### CP/AP 配对，而不是单镜像产品

每块物理板的 `openvela.conf` 选择一个兼容的 CP/AP 配置对和一个分区 CSV。
`tools/bk7258/bk7258.py build` 先后通过官方 `build.sh ... --cmake` 构建两个角色，
再校验二者的板型、SDK profile、内存和存储拓扑。CP 的 `openvela_cp` 已以
`nsh_main` 为入口并启用 `CONFIG_SYSTEM_NSH`，所以“没有 NSH 基线”不准确；准确
说法是**没有名为 `configs/nsh` 的单镜像产品目录**。

`configs/nsh` 是官方指南中的通常/默认目录约定。BK7258 保留
`configs/openvela_cp` 和 `configs/openvela_ap`，因为把其中任一目录改名为 `nsh`
都不能表达另一个必需角色。若未来增加 `configs/nsh`，它只能是额外的 CP-only
诊断目标，不能冒充正常产品配置。

### 启动职责按物理 owner 划分

CP 是系统启动 owner：其 `__start()` 完成 C 运行时、早期串口和可选启动频率设置，
随后由 CP 生命周期释放 AP。AP `__start()` 仍完成本核向量、FPU、AP SMP 独占区、
`.data`、`.bss` 和 `nx_start()`，但不重复全 SoC 时钟初始化。维护 AP defconfig
明确关闭 `CONFIG_DEV_CONSOLE`/`CONFIG_SERIAL`，因此没有可调用的
`arm_earlyserialinit()`。这不是遗漏同一启动步骤，而是避免两个镜像争用全局时钟
和控制台 owner。

### 三阶段板级生命周期

当前板 Kconfig 选择 `CONFIG_BOARD_LATE_INITIALIZE`，没有选择
`CONFIG_BOARD_EARLY_INITIALIZE`。芯片复位前置工作已在 BL1/CP `__start()` 完成，
板级设备和需要调度器的 SDK 操作从 `board_late_initialize()` 开始，然后进入
`board_app_initialize()` 和 `board_app_finalinitialize()`。当前没有必须在 idle
任务前执行的板级操作，因而不注册空的 early hook。新增此类硬件需求时必须重新
打开该阶段。

### 公共链接模板，板级生成内存布局

三个物理板复用 `boards/bk7258/common/scripts/ld.script` 和 `ld_ap.script`，避免复制
同一 SoC 段布局。物理板仍通过 `openvela.conf` 选择分区 CSV；构建器由 CSV 生成
`BK7258_PARTITION_HEADER` 和 `BK7258_PARTITION_LINKER`，Make/CMake 都把生成文件
作为链接输入。因此“公共脚本导致各板无法定制内存布局”不成立。只有段组织真的
不同的新板才需要板级脚本覆盖。

### 选择 arch_timer 和完整 IRQ 表

1443 同时支持 `arch_alarm` 与 `arch_timer`，并将前者描述为优先推荐，而非强制。
BK7258 的调度时钟是固定 32 kHz 外部源上的 SysTick；代码用静态断言要求其能被
`CLK_TCK` 整除，并由 `systick_initialize(false, 32000, -1)` 注册 arch_timer。
协调待机还需要周期 tick 的相位补偿，因此当前不宣称 tickless/arch_alarm。

BK7258 只有 80 个逻辑 IRQ（16 个系统异常加 64 个 SDK source），SDK bridge 对
source 0..63 使用稳定的一一映射。1444 的 minimal vector table 是面向数百个稀疏
IRQ 的内存优化章节，不是符合性门槛；当前选择完整 80 项数组是有界的架构决策。

## 原评审逐项判定

| 原评审项 | 判定 | 复核结果 |
|---|---|---|
| CMake 比 Make 少 13 个驱动 | ❌ 不成立 | `CMakeLists.txt` 的三个 `foreach` 分别生成 `AUD/GPIOE/I2C/I2S`、`MIC/RTC/SARADC`、`SDMADC/SPI/QSPI/CAN/TIMER/TRNG`，正好覆盖字面差集；现有 Ninja 规则证实已启用对象进入 `libarch.a` |
| 未采用 arch_alarm | 🟠 事实成立，定性过重 | 当前使用 arch_timer；1443 明确同时支持两种模型，只是优先推荐 alarm。固定 32 kHz SysTick、整除断言和待机补偿使当前选择有明确依据 |
| AP `__start` 无 clock/early serial | 🟠 事实成立，属于 owner 划分 | AP 由已运行的 CP 拉起，且维护 AP 配置没有串口控制台；AP 仍完成本核必须的 C/SMP/FPU/VTOR 启动职责 |
| 未启用 minimal vector table | 🟠 事实成立，不是缺口 | 1444 把它列为优化策略；80 槽和 SDK source 一一映射是有界且稳定的实现 |
| `up_prioritize_irq` 未以 `CONFIG_ARCH_IRQPRIO` 包裹 | 🟠 低风险样式差异 | 官方要求是在启用 IRQ priority 时必须提供函数，并未要求关闭时必须删掉符号；维护功能会选择 `ARCH_IRQPRIO` |
| 缺 `bk7258_irq.h/lowputc.h/start.h` | 🟠 文件名事实成立，不是接口缺口 | 1443 展示典型目录，不是强制文件清单；公共 IRQ 契约在 `include/irq.h`，控制台契约在 `bk7258_console.h`，其余启动细节为角色私有 |
| Kconfig 无芯片型号 choice | 🟠 文件事实成立，不适用 | 当前自定义 chip 目录只支持 BK7258；官方 choice 示例用于一个 family 下选择多个型号 |
| CP/AP 必须配对 | ✅ 成立 | 正常产品构建是配对系统，必须在交付文档显式声明；CP 自身仍是 NSH 启动镜像 |
| 未启用 `board_early_initialize` | ✅ 成立且有意 | 当前没有 pre-idle 板级工作；使用 late/app/final 三阶段，并由 BL1/CP `__start` 承担早期 SoC 工作 |
| 无 `configs/nsh` | 🟠 目录事实成立，严重性过高 | `openvela_cp` 已启用 NSH；缺的是官方通常命名，不是 NSH 功能。正常产品还需要配对 `openvela_ap` |
| 13 个空 config 目录 | ❌ 不是仓库缺口 | 这些目录存在于本地磁盘但均未被 Git 跟踪；Git 不能提交空目录，远端评审不会取得它们 |
| 缺 `etc/group`、`etc/passwd` 和 `RCRAWS` | ❌ 不是当前功能缺口 | 维护配置未启用登录/账户数据库；ROMFS 只有 `rc.sysinit`/`rcS`，用 `RCSRCS` 正确。1443/1445 给出的是可扩展示例 |
| 链接脚本集中 common | ✅ 事实成立，结论不成立 | 公共脚本结合每次构建生成的分区头/链接输入；三板仍有独立 CSV 和板级 `Make.defs` 入口 |
| `bk7258_board_cp_devices_initialize` 无生产实现 | ✅ 真实缺口 | 仅在 `CONFIG_BK7258_TOUCH` 打开时暴露；当前维护 defconfig 不启用。启用前必须实现，不能用文档替代代码修复 |
| 三板 rc 脚本相同且 `rcS` marker-only | ✅ 成立且已声明 | 保持每个物理板拥有自己的 ROMFS 输入；产品服务尚未加入，`boards/bk7258/CONFIGS.md` 明确称其为 marker-only |
| `ld.script` 头注释路径过期 | ✅ 成立，已修正 | 仅为注释，不影响链接结果 |
| 根 README 仍是模板 | ✅ 成立，已修复 | 已改为中英文作品说明，包含简介、赛道、目录、复现和 AI Coding 五项 |
| wrapper 注入环境导致复现断档 | 🟠 部分成立 | 环境变量是 fail-closed 的内部契约，不应要求用户手填；原 README 确实缺工具链安装和 SDK 重建步骤，现已补齐 |
| `bk7258-sdk` 组不是 default | ❌ 不成立 | manifest 未设置 `notdefault`；repo 的 `default` 组会匹配所有没有 `notdefault` 的项目。README 仍显式写出两个组以消除歧义 |
| `prebuilt` 是空壳 | 🟠 事实成立，属于再生成设计 | Git 只跟踪安装说明；`toolchain install` 从 Arm 官方地址下载、校验锁定 SHA-256 后填充忽略目录 |
| 产物不叫 `vela_ap.bin` | ✅ 事实成立，属于多镜像设计 | 产物按分区角色命名，避免把 CP/AP/BL1/BL2 或 pair 混成一个泛化名称；README 已说明 |
| 团队 `tests/` 没有 linkfile | 🟠 事实成立，不需要修复 | 测试从团队仓直接运行并编译现役源码；它不是 NuttX 编译树的一部分，根 README 已给唯一入口 |
| SDK bundle 未入库 | ✅ 事实成立，属于第三方边界 | manifest 固定源码和 revision，本机从干净 SDK checkout 确定性重建；忽略第三方二进制，README 已给重建/校验步骤 |
| 硬件证据与 AI 日志同在 `logs/` | ✅ 低风险组织问题 | `logs/lijian/` 才是官方格式 AI 日志；七个 `logs/bk7258-*` 是早期硬件证据。README 已区分，新证据统一进入 `progress/verification/` |

## 尚未关闭的事项

1. 为任何准备启用 `CONFIG_BK7258_TOUCH` 的物理板实现
   `bk7258_board_cp_devices_initialize()`，并增加对应构建/链接测试。
2. 若评审方要求目录名逐字匹配而不接受架构说明，可新增一个明确标为
   CP-only diagnostic 的 `configs/nsh`；不得把它列为正常 CP/AP 产品配置。
3. 硬件证据目录后续可迁移到 `progress/evidence/`，但移动历史引用需要独立审计，
   本次只澄清边界。
