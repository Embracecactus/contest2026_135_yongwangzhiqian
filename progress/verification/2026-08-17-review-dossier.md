# BK7258 配置解耦与框架精简 —— 全过程评审档案

> 用途：供 GPT 评审。所有结论均附证据路径/命令/哈希，可复验。

## 0. 评审对象

| 项 | 值 |
|---|---|
| 仓库 | `/home/lijian/project/open-vela/contest2026_135_yongwangzhiqian` |
| 分支 | `feat/bk7258-app-config-decouple`（已推送 fork 远端） |
| Base | `34f4a37bbab8e4ed49904812aaa8dc6330391d9a`（= origin/dev-ai-contest-2026 HEAD） |
| 提交 | `739aecd`(app) → `737c514`(config) → `0aa4960`(test) → `eb9a5d8`(root rotation) → `d2b5d8c`(docs) |
| PR 目标 | base `dev-ai-contest-2026`，创建链接 https://github.com/Embracecactus/contest2026_135_yongwangzhiqian/pull/new/feat/bk7258-app-config-decouple |

## 1. 时间线与过程记录

### 1.1 方案探讨阶段（文档驱动）

1. 用户提供任务计划（P1–P6），要求先探讨方案，并给出 7 篇 OpenVela 文档：602/603/604/607/608/609/610。
2. 用户追加 18 篇：742/745/746/752/754/755/756/758/759/760/761/762/763/765/766/767/768/770。
3. 用户要求把版本从 `dev` 改为 `dev-ai-contest-2026` 全部重看。
4. 期间尝试多 agent 并行（先后 8+ 次 spawn/followup），消息通道约半数无法送达任务正文（多个 agent 回复"没有任务"）；最终由主 agent 直接抓取审阅，无依赖 agent 结果。

### 1.2 架构讨论结论（按主题）

- **配置权威链**：`defconfig` 只是最小 seed，`.config` 是展开后的完整配置（doc 609）；CMake 配置阶段解析 defconfig→Kconfig 树→.config→导入 CMake 变量（doc 608）；`menuconfig` 是官方依赖处理入口。→ 计划 P2/P3 与官方模型逐点吻合。
- **App 注册模板**：官方为 `if(CONFIG_EXAMPLES_HELLO)` + `nuttx_add_application(NAME/PROGNAME/STACKSIZE/PRIORITY)`，过渡期要求 Make 与 CMake 双维护（doc 607）；cmocka 自测模板用 enable+PRIORITY+STACKSIZE+PROGNAME+depends on（doc 770）。→ P1 的 App Kconfig 形状即官方模板。
- **打包**：官方只有"board 自持 post-build 钩子 + manifest"约定（doc 607/608 的 `nuttx_post_build`、doc 610 的 `POSTBUILD`）；NuttX 官方 board（esp32/rp2040/cxd56）不自带 python 打包器，用厂商工具（esptool/imgtool/picotool）。BK7258 因 Beken 官方工具是 Windows/SDK 私有格式，board python 是"host 确定性镜像"，属合理特例。
- **MCUboot**：官方 imgtool 签名，本仓已对齐，不改写。
- **SDK 版本化**：官方无统一导入器（prebuilts=工具链、frameworks=头文件+.a）；本仓 registry/set/lock + SHA manifest 是更严做法，保留；补 CP/AP 版本一致性检查。
- **BL1 命名**：`bk7236_pack_*` 的 "BK7236" 是 ROM 引导格式契约（`"BK7236\x10\x00"` 魔数 + 32+2 CRC，移植自 `bk7236_min_bl.S`），不是芯片型号；用芯片命名统一入口消歧。
- **seed 命名**：runnable 角色 seed=`<board>_<role>_base`；standalone infra seed=`<role>_<boot>`（`bl2_mcuboot` 唯一实例）；计划文档旧名 `bk7258_cp_base` 与仓库实际 `t5ai_core_cp_base` 不一致，已修正文档。
- **测试分层**：纯逻辑→host 测试（`tests/bk7258` + `tools/bk7258/tests`）；命令壳→app（`CONFIG_BK7258_APP_*`）；硬件验收命令不接 cmocka（接上也只目标端跑，CI 无法自动化）。
- **profile.conf**：官方不要求，是 contest 自有 host 元数据（只声明期望事实、不生成 Kconfig 值），保留并写明。

### 1.3 执行阶段（本轮实际完成）

- **阶段 0**：基线 163 pytest；量化清单（14 组重复函数、54 处 import、超长函数 top）；CRC codec 合并 + 字节级对拍。
- **阶段 1**：legacy freeze/shadow 整链退休（用户选 B 授权，framework-check 由 11→10 项、shadow-parity 命令移除；validation descriptors/schema 对齐并重算 identity）。
- **阶段 2**：BL1 入口收敛 `bk7258_bl1_pack.py`（control/manifest/crc），Makefile/executor/build_dual_image/测试切换。
- **阶段 3**：common 原语抽离；materialize 合并；未用 import；SDK lock 版本一致性；JSON 清理；耦合修复（load_board_script 白名单 + verify_* 显式 import）。
- **阶段 4**：framework `cli` 拆分；executor 两个线性大函数有意保留（风险决策）。
- **阶段 5**：README 约定（seed 命名/profile.conf/不回写/测试分层）。
- **阶段 6**：CRC 交叉测试 + 全量验证。

### 1.4 编译与实板验证

- 隔离四角色编译：t5ai_core PASS（manifest `46adce45...`）、t5_board PASS（`35f84fa3...`，配置取自 git 历史 56e574c^ 的 `t5_board_cp/ap_app_mcuboot`，SHA 与 worktree 副本一致）。
- 实板：新密钥 → 重新生成两个根 C → 重建 → `deliver`（firmware.bkpack `e73e6295...`，18.7.0，counter 0x12060052）→ BKFIL COM3 六段写入 → J-Link 验证 VTOR `0x28020000→0x28010800`。
- 期间发现并修复 3 个真实缺陷：`prepare --config-root` 缺失、deliver 不透传 PYTHONPATH、覆盖 0555 产物 EACCES。

### 1.5 提交与推送

5 个提交已推送 `fork` 远端（见 §0）；logs/ 与构建残留未提交；私钥未进仓。

## 2. 文档审阅依据（暂存位置）

25 篇文档以 `version=dev-ai-contest-2026` 抓取，暂存于 `/tmp`（各 3 份：原始 HTML、抽取 content HTML、pandoc 纯文本）：

| 类型 | 路径示例 |
|---|---|
| 原始 HTML | `/tmp/doc_602.html` … `/tmp/doc_770.html`（共 25） |
| 抽取正文 | `/tmp/doc_602_content.html` …（共 25） |
| 纯文本（审阅用） | `/tmp/doc_602.txt` … `/tmp/doc_770.txt`（共 25，已确认存在） |

各文档要点与本方案的印证/冲突：

| 文档 | 主题 | 结论 |
|---|---|---|
| 602 | 新平台适配 | 三层架构/vendor 仓/defconfig 入口；"不要太多配置文件"→ 支持不新增 board×app defconfig |
| 603 | 中断适配 | Chip Kconfig 能力写法（与配置解耦无冲突） |
| 604 | Vendor 仓说明 | 官方目录只有 configs/<cfg>/defconfig；无 profile.conf（后者为 contest 自有） |
| 607 | CMake 快速入门 | 官方 App 注册模板 + 过渡期 Make/CMake 双维护 → P1 依据 |
| 608 | CMake 深度 | defconfig→Kconfig→.config→CMake 变量；POST_BUILD 打包时机 |
| 609 | Kconfig 指南 | .config 唯一完整配置；menuconfig 处理依赖；`build.sh <config> menuconfig/-j8` 官方入口 |
| 610 | Makefile 系统 | Make 后端 CONFIGURED_APPS/register-all；POSTBUILD/manifest 惯例 |
| 742/745/746/752 | GDB/Backtrace/Allsyms/J-Link 插件 | 调试工具与验证手段；Allsyms 是官方符号，与清理的未定义符号无关 |
| 754-763 | 性能评估/工具 | 官方 `CONFIG_BENCHMARK_*` 应用模式 → 印证"App 自选"，不在本次范围 |
| 765-768 | 压力测试 | 官方 `CONFIG_TESTING_*` 应用模式 → 同上 |
| 770 | cmocka 自测框架 | 官方 enable/PRIORITY/STACKSIZE/PROGNAME 模板 → P1 形状依据；硬件命令不接 |

## 3. 原始计划（摘要）

- 目标：Kconfig + 唯一最终 `.config` 为编译权威；App/Driver/Board/Product 解耦；无 overlay/fragment/board×app 配置目录；精简 JSON/Python/错位测试；保留三 seed 与 Boot/Partition/SDK/Trust 合同。
- 10 条不可变更架构决定（详见任务文档 `progress/tasks/2026-08-16-bk7258-app-config-decouple.md`）。
- P1–P6 与各自验收（P6 共 13 条）。
- 权限边界：不碰公共仓/私钥/硬件；commit/push/PR 需授权。
- 建议 4 提交拆分。

## 4. 实际修改内容

- **P1**：`app/hello_app` 每个命令独立 `CONFIG_BK7258_APP_*`（enable/PROGNAME/PRIORITY/STACKSIZE/depends on）；CMake/Make/Make.defs 只认 App 符号；底层符号不再注册应用。
- **P2**：board catalog 删除 console/debug 绑定；validation suite catalog 只保留 resources；清理未定义符号；新增 `verify_final_config()` 与 `verify-config` CLI；executor 记录 `final_config_sha256`/`config_verification`；`config_document` 绑定 seed 或外部 `<role>.config`。
- **P3**：官方入口验证（早期 AP/CP seed `build.sh --cmake` 通过；本轮隔离四角色编译再验证）。
- **P4**：删除 10 个 fragment catalog；`resolve` 只做 metadata；materialize 合并为单实现；source snapshot 排除共享陈旧生成文件。
- **P5**：测试迁移（`tests/bk7258` + `tools/bk7258/tests`）；退休 qemu_mbox_proxy/mocks/scripts gate；新增 CRC 与 SDK 版本一致性用例。
- **全面重构**：common 抽离；framework CLI 拆分；CRC codec 合并；BL1 入口收敛；耦合修复；JSON 清理；未用 import；SDK lock 版本检查；README 约定。
- **缺陷修复**：`prepare --config-root`、PYTHONPATH 透传、只读产物覆盖。
- **信任根轮换**：新密钥（仓库外注册表）+ 根 C 更新 + 重建 + 签名打包 + 实板六段烧录 + J-Link 启动验证。
- **文档**：CURRENT/任务/验收/入门指南更新。

## 5. 完成情况对照

| 项 | 状态 |
|---|---|
| P1/P2/P3/P4 | ✅ PASS |
| P5 | ✅ 大部分（legacy verify_* 门禁待 adapter 退役） |
| P6 1-8、10-13 | ✅ |
| P6 9（BL2 config 哈希） | ⚠️ 部分（BL2 无独立 .config，identity=make-inputs，待决策） |
| 测试基线 | pytest 155 passed；host C tests PASS；diff check 干净 |
| 实板下载 | ✅（BKFIL 六段 + J-Link VTOR 验证） |
| 提交推送 | ✅ 5 commits pushed |

## 6. 关键验证证据

- pytest：`python3 -m pytest tools/bk7258/tests board/bk7258/tests -q` → 155 passed
- host：`tests/bk7258/run_tests.sh` → rptun_mbox 0/31 fail、pm_activity PASS、rptun_core cp/ap PASS、boot_bl1_policy PASS
- 隔离编译：t5ai_core `46adce45d1250f883b0b81239badf490f62793c62c896fd80726f1d713fee9f0`；t5_board `35f84fa3e8177b1b259463a2a679e0b3fd85962ab0be5c7fd711bf2ba70ee24a`
- 最终 .config SHA：t5ai_core CP `070b3f7d...`/AP `b3c1584a...`（与重构前记录逐字节一致）；t5_board CP `827c03e8...`/AP `9e172c1d...`
- 交付包：`firmware.bkpack` `e73e6295c935e5356e6aaa2a740c82be2db7763c57f8411068502f4911a24c0d`
- 信任指纹：BL1 X||Y `a53ff7cb761d5f9f1f87ed11b6a3bd75e8657a76f24a2abc880966bfc270ff22`、SEC1 `2a10778ffaa3ecc4d6d22b1c296f8c56e00201c30e64f61ee4cdd6ed66739349`、MCUboot SPKI `15450ad3092bb232969ce9267fa49c0a05797df4f31339dd0894c4c9ea57325f`（注册表：`/home/lijian/.bk7258/registry.json`，仓库外）
- 烧录：BKFIL COM3 六段 `Writing Flash OK` + `{All Finished Successfully}`；J-Link CPUID `0x631f1320`，VTOR `0x28020000`（BL2 hold）→ 写 `0x2809F7F0=0x4A4C4E4B` 后 `0x28010800`（CP 运行）

## 7. 决策记录（含用户授权）

1. 不实现 overlay/fragment/preset/board×app defconfig（原始决定，不变）。
2. seed 不回写：menuconfig/savedefconfig 只落工作区 .config。
3. 选 B：立即退休 legacy freeze/shadow 链（framework-check 行为变化）。
4. cmocka 不接入 app 硬件测试命令。
5. MCUboot 保持官方 imgtool；bootloader/分区/签名语义不改。
6. ab_layout 保留为派生视图（深挖后非纯 shim）。
7. executor 两个大函数不拆（风险决策）。
8. 信任根轮换：用户授权生成新开发密钥并实板下载（超出原计划第 9 条，属例外）。
9. 包内 config 哈希显式字段：默认不加（成员绑定+manifest 对账）。
10. BL2 配置校验：待确认事实源后接线。

## 8. 剩余项 / 风险 / 待评审确认

1. `build_dual_image.sh` 兼容后端退役时点未定，legacy verify_* 门禁随它保留。
2. BL2 最终配置哈希未入 identity（当前 make-inputs）。
3. 包内 config 哈希是否显式加字段。
4. executor `_validate_manifest`/`deliver` 未拆分（有意）。
5. 信任根轮换是授权例外，评审需确认接受。
6. `cbor/cbor2` 运行时依赖 + PYTHONPATH 透传。
7. C/SDK 层未深度重构（边界内）。

## 9. 给 GPT 评审的聚焦问题

1. P6 验收 9 的"BL2 配置哈希"应如何定性：无独立 .config 时 make-inputs identity 是否可接受？
2. legacy adapter 与 verify_* 门禁的退役时点建议？
3. common 抽离/framework 瘦身后，是否仍有需要优先处理的耦合？
4. 信任根轮换流程（密钥注册表+公钥指纹）是否足够规范？
5. 提交拆分粒度是否适合评审（5 个 vs 建议 4 个）？

## 10. 2026-08-17 最终 App 回归收口（v8）

在 §1–§6 之后，针对 25 篇指定版本官方文档的逐篇对照又完成了四角色最终回归：

- 修复 App→chip 链接断链（App 只依赖独立 chip 公共头，不再包含物理板 `board.h`）。
- 隔离构建禁用 CMake regeneration，避免不可变源码快照被 glob 自动重配置破坏。
- 补齐签名续跑中 `bl2_secondary_crc.bin`/`app*.bin` 的只读产物可写切换。
- 统一隔离交付启动段成员名回 `bl_crc.bin`，修复与 `auto_debug.sh`/工厂布局校验器的契约漂移。
- v8 签名包（`firmware.bkpack` `e3f47d95...`，18.7.1/counter `0x12060053`）COM3 五段下载+回读逐字节一致，BL2 release 后 VTOR `0x28020000→0x28010800`，RTT 捕获 `NuttShell (NSH) / nsh>`。
- 最终树 pytest 157 passed/1 skipped，`framework-check` PASS。

完整证据见 [2026-08-17 最终 App 回归 v8](verification/2026-08-17-final-app-regression-v8.md)。
