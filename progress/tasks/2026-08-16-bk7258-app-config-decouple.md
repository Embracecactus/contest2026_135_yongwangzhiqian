# TASK: BK7258 应用配置解耦与框架精简

- Created: 2026-08-16
- Owner: user / Codex (deepseek-compatible development)
- Base: `origin/dev-ai-contest-2026@34f4a37bbab8e4ed49904812aaa8dc6330391d9a`
- Status: in progress

## 1. 目标

依据 OpenVela 应用开发指南 733
(https://doc.openvela.com/document?id=733&version=dev&language=cn)，将当前
BK7258 配置模型调整为：

```text
Chip/Board Kconfig 能力与约束
        +
App Kconfig 应用选择
        ↓
build.sh --cmake menuconfig
        ↓
唯一最终 .config
        ↓
CMake/Make 编译
        ↓
交付工具读取并校验 .config
```

实现结果：

- Kconfig 和最终 .config 是唯一编译配置权威。
- App、Driver、Board、Product 配置职责解耦。
- 不再使用 App overlay、fragment preset 或自动 merge。
- 不产生 board × app × mode 的配置目录。
- 精简现有 JSON、Python 工具和错位测试。
- 保留现有三角色 seed、Boot/Partition/SDK/Trust 安全合同。

## 2. 已接受的架构决定

1. 不实现 App overlay。
2. 不增加 `app/<app>/common.conf`、`app/<app>/cp.conf`、`app/<app>/ap.conf`。
3. 不使用 `merge_config.py` 构建新的配置合并层。
4. 不增加 `--set-symbol`、`--app-fragment`、preset 等接口。
5. 不新增 board × app defconfig。
6. 保留三个兼容 seed：
   `board/bk7258/configs/bk7258_cp_base`、
   `board/bk7258/configs/bk7258_ap_base`、`board/bk7258/configs/bl2_mcuboot`
   （当前实际名称为 `t5ai_core_cp_base` / `t5ai_core_ap_base` / `bl2_mcuboot`）。
7. App 不能任意覆盖锁定事实：physical board、CP/AP/BL2 role、boot chain、
   partition layout、SDK ABI、trust root、signing/package policy、
   不可复用的 pin/resource binding。
8. ETCROMFS/rcS 不是基础 BSP 必选项，本任务不增加。
9. 不修改 Boot、Partition、密钥、签名根和硬件烧录策略。
10. 不创建新架构层目录，不新增 JSON schema。

## 3. 开始前检查

先阅读 `AGENTS.md`、`memory/INDEX.md`、`progress/CURRENT.md`、
`memory/ARCHITECTURE.md`、`board/bk7258/configs/README.md`、
`app/hello_app/Kconfig`、`app/hello_app/CMakeLists.txt`、
`app/hello_app/Makefile`、`tools/bk7258/bk7258_framework.py`。

执行要求：

- 有 `.codegraph/` 时必须先使用 CodeGraph（本仓未索引 contest 工具，改用 rg）。
- 从最新 `origin/dev-ai-contest-2026` 建立干净分支。
- 记录 base SHA、HEAD、worktree 状态。
- 不覆盖、删除或提交用户现有未跟踪文件。
- 不使用 `git add -A`。
- 不碰 SDK bytes、私钥、固件包、Flash 和硬件。

## 4. P1：重构 App Kconfig 与构建注册

为每个用户可执行命令建立独立 App Kconfig：

```text
CONFIG_BK7258_APP_HELLO
CONFIG_BK7258_APP_BKVALIDATE
CONFIG_BK7258_APP_APCTL
CONFIG_BK7258_APP_RPMSG_TEST
CONFIG_BK7258_APP_BT_IPC_TEST
CONFIG_BK7258_APP_PSRAM_TEST
CONFIG_BK7258_APP_GPIO_TEST
CONFIG_BK7258_APP_GPIO_IRQ_TEST
CONFIG_BK7258_APP_IRQ_TIMER_TEST
CONFIG_BK7258_APP_TIMER_SELFTEST
CONFIG_BK7258_APP_RPMSGFS_TEST
CONFIG_BK7258_APP_WIFI
```

每个应用至少定义 enable、program name、priority、stack size、depends on。

规则：

- 优先使用 `depends on`；不允许 App 通过 `select` 强行打开板级硬件/驱动/启动链。
- Driver 打开但 App 关闭时，不得注册该命令。
- App 依赖不满足时，menuconfig 中不可选或被明确拒绝。
- CMake/Make 只能依据 `CONFIG_BK7258_APP_*` 注册应用。
- 不再使用底层 Driver/Test 符号充当 App enable。

P1 验收：

- Driver 开启、App 关闭：应用不进入构建。
- App 开启、依赖满足：应用进入构建。
- App 开启、依赖不满足：Kconfig fail closed。
- Make 与 CMake 使用相同 App 符号。
- 不新增配置 fragment 或 JSON。

## 5. P2：确立最终 .config 为唯一配置事实

Product metadata 只保留 product ID、expected board、expected role、boot
policy、partition layout、SDK identity/ABI、trust/package policy、artifact
policy。expected board/role 只能核对最终 .config，不能生成或覆盖 .config。

必须清理：

- Product catalog 中用于设置 UART、RTT、SWD、Driver、App 的符号。
- Validation suite catalog 中的 Kconfig symbol override。
- 未定义 Kconfig 符号：`CONFIG_BK7258_H264`、`CONFIG_BK7258_TF`、
  `CONFIG_BK7258_TF_WIDTH`、`CONFIG_BK7258_WIFI`。
- Board catalog 中与 Kconfig 重复的 console/debug 配置事实。

Framework 改为：

1. 读取构建完成后的 CP/AP/BL2 最终 .config。
2. 核对 board、role、boot、layout、SDK 与 product metadata 一致。
3. 拒绝未知或矛盾配置。
4. 计算最终 .config SHA-256。
5. 将 CP/AP/BL2 配置哈希写入 build/package identity。

P2 验收：无 App overlay 入口；Product JSON 不再设置 App/Driver/console/debug
Kconfig；最终 .config 是 package identity 输入；修改 .config 改变 build
identity；Product 声明与 .config 不一致时 fail closed。

## 6. P3：恢复官方标准应用开发入口

```sh
./build.sh <bk7258-config> --cmake menuconfig
./build.sh <bk7258-config> --cmake -j8
```

要求：CP/AP 可分别配置和构建；menuconfig 能看到 App 自己的
enable/name/priority/stack；App 开启/关闭后各完成一次干净 CMake 构建；最终
.config 与 ELF/map 中实际应用一致。Python isolated executor 降级为读取已有
最终配置 → 多角色编译/交付编排 → 签名、打包、安全校验，不再拥有独立 App
配置模型。Classic Make 只做兼容性检查。

## 7. P4：删除旧 fragment/merge 配置体系

只有 P1～P3 完成并通过后才删除。候选清理：App/validation fragment 合并代码、
app/validation scope precedence、不再被消费的 fragment catalog、
`bk7258_validation_suite_catalog.json` 中的配置覆盖部分、只为 overlay 服务的
schema 字段和测试、无消费者的 materialized-defconfig 生成逻辑。删除前通过
CodeGraph 和 rg 双重确认零活动消费者。

## 8. P5：测试与工具职责清理

- `board/bk7258/tests` 只保留 Board/BSP 测试。
- framework/executor/paths/bkpack/trust/transport/source-snapshot/package
  测试迁到 `tools/bk7258/tests/`。
- Chip/Boot C host 测试迁到仓库级 `tests/bk7258/`。
- 确认无消费者后删除 `qemu_mbox_proxy` 实验、脆弱字符串测试、精确锁死
  scripts 文件数量的一次性测试、`verify_legacy_profile_freeze.py`、
  `legacy_profile_consumers.json`、legacy profile freeze manifest、migration
  ledger、shadow ledger 及 `test_legacy_profile_freeze.py`。
- 保留少量关键行为测试：partition range/overlap、package member hash/size、
  trust-chain verification、path traversal/symlink escape、最终 .config 与
  product identity 不一致、Flash contract 边界。

## 9. P6：最终验收

满足 14 条验收项（见任务原文）：三角色 seed、无 overlay/preset/fragment
merge 接口、新 App 只需 source/Kconfig/CMakeLists/Makefile、同一 seed 通过
menuconfig 选择不同 App、Driver enable 不自动注册测试应用、App disable 不
关闭 Driver、依赖不满足 fail closed、Product metadata 不重复 Kconfig、
最终 .config 哈希进入 build/package identity、官方 build.sh --cmake 干净构建、
Board tests 不承载大量 tools 测试、无 undefined Kconfig symbol、无新增 JSON
schema、Boot/Partition/Trust/SDK ABI 语义不变。

## 10. 建议提交拆分

四个独立提交（本任务不 commit/push，除非 owner 明确授权）：

1. `refactor(app): decouple BK7258 applications from driver symbols`
2. `refactor(config): make final Kconfig output the single build authority`
3. `refactor(framework): retire BK7258 application fragment merging`
4. `test(bk7258): align tests and tools with ownership boundaries`

## 11. 权限边界

允许：修改 contest 仓内 App/Kconfig/CMake/Make/framework/tests/docs；删除已
确认无活动消费者的冗余文件；运行 host tests 和无签名构建。

禁止：修改 NuttX/apps 公共仓；读取/生成私钥；签名/生产包；Flash/串口/J-Link/
复位/硬件；修改分区布局或启动信任链；commit/push/开 PR。
