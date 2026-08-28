# BK7258 第二轮评审：重构后状态复核

> **文档状态：历史第二轮快照。** 本文保留重构复核过程；R-02 的 SDK 分组判定已经
> 后续复核撤销。当前整改结论以 `docs/platforms/bk7258/official-compliance-review.md`
> 和同日的 `2026-08-28-bk7258-official-guide-full-review.md` 为准。

> 评审日期：2026-08-28（第二轮）
> 评审对象：`docs/platforms/bk7258/` 重构 + `README.md` 改写**完成之后**的仓库状态
> 评审基准：本地官方镜像 `docs/openvela-guides/芯片移植/`
> （`1443-新平台适配指南.md`、`1444-中断系统适配指南.md`、`1445-Vendor_代码仓说明.md`）
>
> 第一轮报告见 `docs/review/2026-08-28-bk7258-official-guide-conformance-review.md`，
> 权威评审结论见 `docs/platforms/bk7258/official-compliance-review.md`。
> 本文**只覆盖第一轮之后新产生或仍未解决的问题**，不重复已达成一致的结论。
> 所有路径从 `contest2026_135_yongwangzhiqian/` 起算。

---

## 1. 本轮结论速览

重构与 README 改写整体质量高。第一轮报告的三条误报（CMake 漏编 13 个驱动、
工具链缺失、复现链路断档）已由 `official-compliance-review.md` 正确推翻，
本轮确认该推翻成立。

**本轮新发现 1 类中危问题（8 处文档断链），确认 3 项遗留项仍未处理；本轮曾把
README SDK 分组表述列为错误，但该判定已由后续 repo 源码复核撤销。**

| 编号 | 问题 | 级别 | 是否本轮新增 |
|---|---|---|---|
| R-01 | `memory/` 下 8 处文档断链，仍指向已删除的 `docs/bk7258-t5ai/` | 历史缺口，已修复 | ✅ 重构产生，后续已处置 |
| R-02 | README 中英双语 SDK 分组表述 | 撤销 | ❌ 后续确认是本轮误判 |
| R-03 | `AGENTS.md:20` 子命令清单过时 | 中 | ⬜ 第一轮 F-05 遗留 |
| R-04 | `etc/group`、`etc/passwd` 缺失，`RCRAWS` 未使用 | 低 | ⬜ 第一轮 F-06 遗留 |
| R-05 | `logs/` 下 7 个硬件证据目录违反自身规范 | 低 | ⬜ 第一轮 F-08 遗留 |
| R-06 | `docs/ai-worklog/prompts/phase-05` 断链 | 低 | ⬜ 历史遗留 |
| R-07 | 多份评审报告口径需要统一 | 已处置 | ✅ 增加状态声明与统一结论 |
| R-08 | `CONFIG_BK7258_TOUCH` 链接缺口 | 历史缺口，已门禁 | ✅ 本轮当时确认可达，后续已处置 |

---

## 2. R-01：`memory/` 下 8 处文档断链 【中 · 重构产生】

> **后续处置状态：已修复。** 下列旧路径保留为历史发现证据；当前 `memory/` 中的
> 9 个 Markdown 链接均已迁移到 `docs/platforms/bk7258/`，目标逐一验证实存。

### 现象

`docs/bk7258-t5ai/` 已整体迁移为 `docs/platforms/bk7258/`（92 个文件），
但 `memory/` 下的历史决策记录未同步更新引用。

### 证据

全仓 Markdown 相对链接扫描结果（扫描 370 个相对链接，9 个文件断链）：

```
扫描相对链接总数: 370
断链文件数: 9
```

| 文件 | 断链引用 | 应改为 |
|---|---|---|
| `memory/ARCHITECTURE.md` | `../docs/bk7258-t5ai/porting-report.md` | `../docs/platforms/bk7258/porting-report.md` |
| `memory/OPERATIONS.md` | `../docs/bk7258-t5ai/nuttx-port/bk7258-build-flash-debug-sop.md` | `../docs/platforms/bk7258/nuttx-port/…` |
| `memory/OPERATIONS.md` | `../docs/bk7258-t5ai/nuttx-port/n14-evidence-index.md` | 同上 |
| `memory/decisions/ADR-001-wrapper-only-official-source-boundary.md` | `../../docs/bk7258-t5ai/nuttx-port/n14-psram-source-verification.md` | `../../docs/platforms/bk7258/nuttx-port/…` |
| `memory/decisions/ADR-002-n14-psram-ownership-and-layout.md` | `../../docs/bk7258-t5ai/nuttx-port/n14-evidence-index.md` | 同上 |
| `memory/decisions/ADR-008-n17-phased-ota-authentication.md` | `../../docs/bk7258-t5ai/nuttx-port/n17-signed-manifest-abi.md` | 同上 |
| `memory/decisions/ADR-009-n17-dedicated-manifest-policy-sectors.md` | `../../docs/bk7258-t5ai/nuttx-port/n17-layout-journal-migration.md` | 同上 |
| `memory/decisions/ADR-010-n17-format3-lifecycle-journal.md` | `../../docs/bk7258-t5ai/nuttx-port/n17-layout-journal-migration.md` | 同上 |
| `memory/decisions/ADR-011-n17-fail-closed-format2-migration.md` | `../../docs/bk7258-t5ai/nuttx-port/n17-layout-journal-migration.md` | 同上 |

> 注：上表 9 行中，`OPERATIONS.md` 占 2 处，故为 8 个文件 9 处断链。

### 判定

- **并非**遗漏修复整个目录：`docs/` 内部 5 处对 `bk7258-t5ai` 的引用经核查
  全部是"迁移前 → 迁移后"的说明性文字（`docs/README.md:27-28`、
  `docs/README_EN.md:29-31`、`official-compliance-review.md:22`、
  `official-compliance-review.en.md:29`），属有意保留，**不是断链，无需修改**。
- `memory/` 下这 8 处是**真正的断链**（链接目标已不存在）。

### 修复动作

对 `memory/` 下文件做路径替换：`docs/bk7258-t5ai/` → `docs/platforms/bk7258/`。
注意两个层级的相对前缀不同（`../docs/` 与 `../../docs/`），但替换子串一致，
可安全批量替换。

### 验收方法

```bash
# 期望输出：断链文件数: 1（仅剩 R-06 的历史遗留）
python3 - <<'PY'
import os,re,collections
broken=collections.defaultdict(list); total=0
for root,dirs,files in os.walk('.'):
    dirs[:] = [d for d in dirs if d not in ('.git','node_modules','__pycache__','openvela-guides')]
    for fn in files:
        if not fn.endswith('.md'): continue
        p=os.path.join(root,fn)
        try: txt=open(p,encoding='utf-8').read()
        except Exception: continue
        for m in set(re.findall(r'\]\((\.\.?/[^)\s]+)\)', txt)):
            t=m.split('#')[0]
            if not t: continue
            total+=1
            if not os.path.exists(os.path.normpath(os.path.join(os.path.dirname(p),t))):
                broken[p].append(t)
print(f"扫描相对链接总数: {total}"); print(f"断链文件数: {len(broken)}")
for p,v in sorted(broken.items()): print(" ", p, sorted(v))
PY
```

---

## 3. R-02：README 中英双语 SDK 分组表述 【撤销误判】

### 现象

本轮曾把 README 的解释文字判为错误；后续直接复核 repo 的分组实现后确认，
README 的解释和命令均正确。

### 证据

`README.md:55-56`：

> 以下命令显式选中默认项目和 BK7258 SDK 组。**SDK 项目没有 `notdefault` 标记，
> 因此普通默认同步也会包含它**；显式写出分组是为了让复现输入一目了然。

`README_EN.md:63-66` 有相同含义的正确表述：

> The command below selects both the default projects and the BK7258 SDK group
> explicitly. **The SDK project has no `notdefault` marker, so an ordinary
> default sync also includes it**; spelling out the group makes the reproduction
> input unambiguous.

manifest 实际内容（`contest2026_135_yongwangzhiqian.xml:10-16`）：

```xml
<project path="vendor/beken/bk_avdk_smp"
         name="Embracecactus/bk_avdk_smp"
         remote="git"
         revision="cb080de1655d579c7593ecf504c440997c4c137b"
         upstream="refs/heads/openvela/v3.1.1.9"
         groups="bk7258-sdk"/>
```

SDK 项目没有 `notdefault`。

### 判定

**撤销原判。** repo 会给所有没有 `notdefault` 的项目隐式加入 `default` 组；项目
额外声明 `groups="bk7258-sdk"` 不会移除这个隐式成员关系。因此普通默认同步会包含
SDK，README 显式写 `-g default,bk7258-sdk` 只是让复现选择一目了然。

直接证据位于工作区 repo 实现 `project.py` 的 `MatchesGroups()`：特殊 manifest 组
`default` 匹配所有不含特殊项目组 `notdefault` 的项目；repo 自带
`docs/manifest-format.md` 也说明只有放入 `notdefault` 才不会自动下载。

### 修复动作

无需修改 README；删除所有把该项列入整改清单的内容。

### 验收方法

人工确认中英两份表述一致；用 repo 自带分组实现和格式文档复核语义。

---

## 4. R-03：`AGENTS.md:20` 子命令清单过时 【中 · 第一轮 F-05 遗留】

### 证据

`AGENTS.md:20` 当前内容：

```
- `tools/bk7258/bk7258.py` is the only tracked public entry and exposes only
  `build`, `sdk`, `package`, and `verify`; domain implementation belongs under `_lib`.
```

`tools/bk7258/bk7258.py` 实际子命令：

| 子命令 | 行号 | AGENTS.md 是否覆盖 |
|---|---|---|
| `build` | `:34` | ✅ |
| `toolchain install` / `toolchain verify` | `:56-61` | ❌ 未列 |
| `sdk list` / `verify` / `install` / `rebuild` | `:63-72` | ⚠️ 列出 `sdk`，但 `verify` 被写成顶层命令，实为 `sdk` 子命令 |
| `package create` / `extract` / `flash-contract` / `materialize` | `:78-105` | ✅ |
| `release …` | `:107-110` | ❌ 未列 |

### 判定

本轮复核时该行**未被修改**，仍是第一轮的过时内容。

### 为何值得修（不只是洁癖）

`AGENTS.md` 是约束 agent 行为的文件。清单过时会导致后续 agent 误判
`toolchain install` 不存在 —— 第一轮评审就因此差点得出"工具链缺失、无法复现"
的错误结论（已被 `official-compliance-review.md` 推翻）。保留错误清单会持续
诱导同类误判。

### 修复动作

更新 `AGENTS.md:20` 为实际子命令清单，并把 `verify` 归入 `sdk` 下。

### 验收方法

`AGENTS.md` 所列命令与 `tools/bk7258/bk7258.py --help` 输出逐条一致。

---

## 5. R-04：`etc/group`、`etc/passwd` 缺失 【低 · 第一轮 F-06 遗留】

### 官方依据

- `1445-Vendor_代码仓说明.md:85-89`：`etc` 下应含 `group`、`passwd`
- `1443-新平台适配指南.md:542-554`：示例 `RCRAWS += etc/group etc/passwd`

### 证据（本轮复核，状态未变）

```bash
find boards -path "*etc*" -type f
# 仅 6 个：三块板各 rc.sysinit + rcS；无 group、无 passwd
grep -rn "RCRAWS" boards/     # 无输出
boards/bk7258/*/src/Make.defs:6 → RCSRCS = etc/init.d/rc.sysinit etc/init.d/rcS
```

### 判定

仍缺失。但**需先确认是否为有意裁剪**：`/etc` 走 ROMFS，无多用户场景时
可能有意不提供认证文件。若 `progress/` 或 `memory/decisions/` 中有对应决策记录，
本项应降级并在交付说明中声明，而非补文件。

### 修复动作（若确认非有意裁剪）

三块板各补 `src/etc/group`、`src/etc/passwd`，并在 `src/Make.defs` 增加：

```make
RCRAWS += etc/group etc/passwd
```

### 验收方法

clean build 通过；启动后 `ls /etc` 可见 `group`、`passwd`。

---

## 6. R-05：`logs/` 下 7 个硬件证据目录违反自身规范 【低 · 第一轮 F-08 遗留】

### 证据（本轮复核，状态未变）

```
logs/bk7258-n14                        logs/bk7258-n15
logs/bk7258-n15-normal-restore         logs/bk7258-secureboot-minimal-primary
logs/bk7258-secureboot-minimal-rts     logs/bk7258-secureboot-minimal-negative
logs/bk7258-secureboot-minimal-restored
```

`logs/README.md:1` 自述："# logs/ — AI Coding 日志目录 / 存放你在开发中与
AI 工具的对话日志"，`:9-13` 规定的目录结构只有 `<github_login>/<date>/<tool>__<sid>.jsonl`
一种形态。

### 判定

7 个 `bk7258-*` 目录是串口抓取与硬件证据，**不是 AI 对话日志**，与
`logs/README.md` 自述的用途冲突。AI 日志本身合规（`logs/lijian/` 下
`manifest.json` + 162 个 `claude-code__<sid>.jsonl`）。

### 影响

官方《AI Coding 日志归集与提交手册》要求 `logs/` 存放 AI 对话日志。
混放会让评委在 400+ 文件中难辨主次。

### 修复动作（二选一，后者更省事）

1. 迁移到 `progress/verification/`，同步更新引用；
2. 在 `logs/README.md` 增加一节，说明这些是硬件证据、与 AI 日志区分，
   并指向 `progress/verification/`。

### 验收方法

`logs/README.md` 明确区分两类内容；AI 日志结构不受影响。

---

## 7. R-06：`docs/ai-worklog/prompts/phase-05` 断链 【低 · 历史遗留】

### 证据

```
./docs/ai-worklog/prompts/phase-05-verified-baseline-follow-up.md
    ../../../board/contest_board/README.md
```

目标路径为单数 `board/contest_board/`，而仓库实际目录是 `boards/` 下的具体板名
（`t5_board`、`t5ai_core`、`aidk_ai_toy`）。

### 判定

与本次重构**无关**，是更早的历史断链（描述的是 RV1126B 时期的板名）。
严重度低：`docs/ai-worklog/` 按 `docs/README.md:11` 属历史设计/计划层。

### 修复动作

可选：改为指向当前板目录，或在该文件顶部标注"本文描述迁移前状态，路径已失效"。

---

## 8. R-07：多份评审报告口径统一 【已处置】

### 现象

| 文件 | 行数 | 状态 |
|---|---|---|
| `docs/platforms/bk7258/official-compliance-review.md` | — | 官方符合性与架构差异说明，含中英双语版 |
| `docs/review/2026-08-28-bk7258-official-guide-conformance-review.md` | — | 历史第一轮施工单，保留发现过程 |
| `docs/review/2026-08-28-bk7258-post-refactor-review.md` | — | 历史第二轮重构快照，即本文 |
| `docs/review/2026-08-28-bk7258-official-guide-full-review.md` | — | 后续交叉核验后的统一评审结论 |

### 问题

第一轮报告已由 `official-compliance-review.md` 推翻部分结论；第二轮又曾错误判断
SDK 默认分组。若没有清晰状态，多份报告并存时：

- 第一轮报告的 §0"已撤销误报"章节虽然自我更正，但文档正文仍保留 17 条施工单，
  其中 F-01、F-02、F-09 已被执行方完成，F-07 已被否决；
- 后续 agent 若只读到第一轮报告，会去执行已过期或已被否决的条目。

### 处置

保留三份报告的审计价值，但明确主从关系：第一轮和第二轮均在标题后标为历史快照，
SDK 分组误判在正文中撤销；`official-guide-full-review.md` 标为统一评审结论。执行整改
时以统一结论和当前源码为准，不再从历史施工单直接取动作。

---

## 9. R-08：`CONFIG_BK7258_TOUCH` 链接缺口 —— 本轮确认可达 【中】

> **后续处置状态：已配置门禁。** 以下内容记录第二轮复核时仍有用户 prompt 的历史
> 状态。当前 `BK7258_TOUCH` 已改为无 prompt、无维护板选择，并在 CMake 与 Classic
> Make 入口加入 `CONFIG_BK7258_TOUCH && CONFIG_BK7258_AP_CORE` 构建期错误。未来板级 selector
> 还必须自身依赖 `!BK7258_AP_CORE`。当前结论以统一复核报告第三、七、八节为准。

### 结论

`official-compliance-review.md` 对该缺口的判断**正确**，本轮已独立验证。
（第一轮报告遗漏了此条，属第一轮的实质疏漏。）

### 证据

| 角色 | 位置 |
|---|---|
| 声明 | `boards/bk7258/common/include/bk7258_board.h:106`（受 `:105` `#if defined(CONFIG_BK7258_TOUCH) && !defined(CONFIG_BK7258_AP_CORE)` 保护） |
| 调用 | `boards/bk7258/common/src/bk7258_cp_bringup.c:92`（受 `:83` `#ifdef CONFIG_BK7258_TOUCH` 保护） |
| 生产实现 | **无** |
| 唯一定义 | `tests/bk7258/test_bk7258_cp_platform.c:139`（主机测试桩） |
| 测试 mock 声明 | `tests/bk7258/mocks/arch/board/board.h:19` |

复核当时的 Kconfig 定义（`chips/bk7258/Kconfig:1333-1340`）—— **当时确认可达**：

```kconfig
config BK7258_TOUCH
	bool "CP capacitive-touch button lower half"
	default n
	depends on ARCH_CHIP_BK7258 && !BK7258_AP_CORE
	select BK7258_SDK_IPC_RUNTIME
	select BK7258_SDK_IRQ_BRIDGE
	select INPUT
	select INPUT_BUTTONS
```

无任何 defconfig 开启该项（`grep CONFIG_BK7258_TOUCH boards/bk7258/*/configs/*/defconfig`
无输出），因此**当前构建不受影响**。

### 判定

⚠️ **可达的潜在链接失败**：任何板在 CP 侧启用 `CONFIG_BK7258_TOUCH` 后，
`bk7258_cp_bringup.c:92` 会调用一个没有生产实现的函数，链接期失败。
`chips/bk7258/cp/bk7258_touch.c` 整个文件也受同一宏保护，当前不参与编译。

`CMakeLists.txt:241` 已按 `if(CONFIG_BK7258_TOUCH)` 把 `cp/bk7258_touch.c`
纳入构建，因此构建系统侧无遗漏，缺口仅在板级 `bk7258_board_cp_devices_initialize()`。

### 当时建议的修复动作

在启用该功能前，为三块板补板级实现；并建议增加一个构建门禁，
使开启 `CONFIG_BK7258_TOUCH` 而无板级实现时**在构建期**而非链接期失败。

后续已完成不可见配置门禁和 AP 侧编译期守卫；CP 板若真正选择该功能，仍必须提供
生产 hook，缺失时链接测试会按预期失败。

---

## 10. 本轮确认无误项（防止重复排查）

| 项 | 复核结果 |
|---|---|
| 相对路径深度补偿 | `docs/bk7258-t5ai/`（3 层）→ `docs/platforms/bk7258/`（4 层）后，文件内 `../../` 已正确补偿为 `../../../`。抽样验证 `openvela-document-adaptation-matrix.md` 的 12 个相对链接全部 OK |
| `docs/` 内对旧路径的 5 处引用 | 全部是"迁移前 → 迁移后"的说明性文字，**不是断链**，有意保留 |
| README 改写质量 | 五要素（作品简介/选题方向/目录结构/运行方式/AI Coding 使用说明）齐全；运行方式分 5 步；中英双语（148 / 173 行）；已说明多镜像产物命名与 `vela_ap.bin` 不适用（`README.md:112-114`） |
| 构建命令正确性 | `toolchain install/verify`、`sdk rebuild/verify`、`build --board … --boot direct` 均与 `bk7258.py` 实际子命令一致 |
| CMake / Make.defs 源文件一致 | 展开三处 `foreach` 后各 120 个文件，双向差集为空（与 `official-compliance-review.md` 结论一致） |
| 工具链 / SDK 安装机制 | `toolchain.json` 锁定 ARM 官方 URL + SHA-256；693 MB 不入仓为正确设计 |
| 空 configs 目录 | 13 个已删除（本轮评审前由评审方执行）；剩余 10 个有效 profile 各含 2 个文件 |
| 编译产物污染 | `git ls-files` 匹配 `__pycache__\|/build/\|\.o$\|\.a$\|\.bin$\|\.elf$\|\.pyc$` 计数为 0 |

---

## 11. 建议处理顺序

| 步 | 编号 | 动作 | 依赖 |
|---|---|---|---|
| — | R-01 | 已替换 `memory/` 下 8 个文件、9 处断链路径 | — |
| — | R-02 | 已撤销误判，不修改 README | — |
| 3 | R-03 | 更新 `AGENTS.md:20` 子命令清单 | 无 |
| — | R-07 | 已用历史快照声明和统一结论解决口径冲突 | — |
| 5 | R-04 | 确认是否"有意裁剪"，再决定是否补 `etc/group`、`passwd` | 需查决策记录 |
| 6 | R-05 | 迁移 `logs/bk7258-*`，或在 `logs/README.md` 加说明 | 需检查 `progress/` 是否引用 |
| 7 | R-08 | 配置门禁与 AP 误选守卫已完成；启用 TOUCH 前仍须补板级实现和链接测试 | 仅在未来启用该功能时 |
| 8 | R-06 | 可选：标注历史断链 | 无 |

---

## 12. 给 owner 的待决问题

1. **R-07**：第一轮报告（659 行）删除、保留、还是并入
   `official-compliance-review.md`？建议删除或并入，避免后续 agent 执行已否决项。
2. **R-04**：`etc/group`、`etc/passwd` 是遗漏还是有意裁剪？
3. **R-08**：`CONFIG_BK7258_TOUCH` 是否在赛前需要启用？若否，建议在其 Kconfig
   项上加一句提示，说明启用前需补板级实现。

---

## 附：本轮评审方法

- 全部为只读检查（`Read` / `Grep` / `Glob` / `Bash` 只读命令）。
- **唯一执行的写操作**：删除 13 个空 configs 目录（在第一轮报告的执行阶段完成，
  早于本轮评审）。这些目录为空且 `git ls-files` 跟踪数为 0，无实质影响；
  如需还原，执行 `mkdir -p` 重建即可。除此外本轮未修改任何文件。
- 断链扫描覆盖全仓 Markdown（排除 `.git`、`node_modules`、`__pycache__`、
  `openvela-guides` 官方镜像），共检查 370 个相对链接。
