# BK7258 对照 openvela 官方适配指导的符合性评审

> **文档状态：历史第一轮施工单。** 本文保留发现过程和当时证据，不作为当前整改
> 清单。后续复核已撤销其中关于 `bk7258-sdk` 默认分组等误判；当前结论以
> `docs/platforms/bk7258/official-compliance-review.md` 和同日的
> `2026-08-28-bk7258-official-guide-full-review.md` 为准。

> 评审日期：2026-08-28
> 评审对象：`contest2026_135_yongwangzhiqian`（BK7258 / T5-AI 平台）
> 评审基准：本地官方镜像 `docs/openvela-guides/芯片移植/`（`dev-ai-contest-2026` 中文版）
> - `1443-新平台适配指南.md`（芯片层 §二、板级层 §三、构建 §四、测试 §五）
> - `1444-中断系统适配指南.md`
> - `1445-Vendor_代码仓说明.md`
>
> **本文是给执行 agent 的施工单。** 每条问题给出：官方依据 → 实际证据（文件:行号）→
> 判定 → 修复动作 → 验收方法。所有路径均为仓库相对路径，从 `contest2026_135_yongwangzhiqian/`
> 起算。已用只读命令复核的结论标注 `[已复核]`，仅由子代理报告未亲自复核的标注 `[待复核]`。

---

## 0. 阅读须知：三条已被推翻的初步结论（执行时不要改这些）

评审过程中产生了三条误报，**已从施工清单中删除**。若在其他文档里看到相同说法，以本文为准。

### 0.1 撤销：~~CMake 构建漏编 13 个芯片层驱动~~

**误报原因**：`chips/bk7258/CMakeLists.txt` 用 `foreach` + 变量名拼接源文件，
字面 `grep '\.c'` 抓不到文件名。

**证据**（`chips/bk7258/CMakeLists.txt`）：

```cmake
304: foreach(BK7258_AP_PERIPHERAL AUD GPIOE I2C I2S)      # → ap/bk7258_aud.c 等 4 个
412: foreach(BK7258_AP_PERIPHERAL MIC RTC SARADC)          # → ap/bk7258_mic.c 等 3 个
432: foreach(BK7258_AP_PERIPHERAL SDMADC SPI QSPI CAN TIMER TRNG)  # → 6 个
```

**复核结果** `[已复核]`：展开三处 `foreach` 后，`Make.defs` 与 `CMakeLists.txt`
**各 120 个源文件，双向差集均为空**。两套构建源文件集完全一致，无需任何修改。

复核方法（可复现）：

```bash
cd chips/bk7258
# 展开 foreach 后比对；期望输出：Make.defs 文件数: 120  CMake 展开后: 120 / 两个差集均为 []
python3 - <<'EOF'
import re
mk=set(re.findall(r'\+=\s*([A-Za-z0-9_]+\.c)', open('Make.defs').read()))
cm={x for x in re.findall(r'([A-Za-z0-9_]+\.c)', open('CMakeLists.txt').read())
    if 'BK7258_AP_PERIPHERAL_FILE' not in x}
for ps in re.findall(r'foreach\(BK7258_AP_PERIPHERAL ([^)]+)\)', open('CMakeLists.txt').read()):
    cm |= {'bk7258_%s.c' % p.lower() for p in ps.split()}
print("Make.defs 文件数:", len(mk), " CMake 展开后:", len(cm))
print("仅 Make 有:", sorted(mk-cm)); print("仅 CMake 有:", sorted(cm-mk))
EOF
```

### 0.2 撤销：~~`prebuilt/` 被 .gitignore 清空导致工具链缺失~~

**误报原因**：只看了 `.gitignore`，没看工具链的实际安装机制。

**证据**：`tools/bk7258/bk7258.py:56-61` 提供 `toolchain install` / `toolchain verify`
子命令；`tools/bk7258/toolchain.json` 锁定 ARM 官方 URL 与 sha256
（`97dbb4f019ad1650b732faffcc881689cedc14e2b7ee863d390e0a41ef16c9a3`），
安装目标为 `prebuilt/gcc-arm-none-eabi-10.3-2021.10`。

**结论** `[已复核]`：693 MB 工具链不入仓是**正确设计**（`prebuilt/README.md:1-5` 已声明
"local derived artifacts and are not committed"）。**不要试图把工具链提交进 git。**
真正的问题只是 README 没写这个前置命令（见 F-02）。

### 0.3 撤销：~~复现链路完全断档、评委无法构建~~

**误报原因**：同上。工具链与 SDK 都有自带安装命令：

- `tools/bk7258/bk7258.py toolchain install`
- `tools/bk7258/bk7258.py sdk install --profile <p>`（`:63-72`，含 `list/verify/install/rebuild`）

**结论** `[已复核]`：复现链路**是完整的**，只是 `README.md` 未记录前置步骤。修复成本很低（F-02）。

---

## 1. 总体结论

**技术实现深度显著高于官方模板**：三核 CP/AP 双镜像、BL1→MCUboot BL2 签名启动链、
完整 SDK wrapper、大量实板验收证据（`progress/verification/` 130 份记录）。
芯片层与板级层的**功能性**交付对官方 1443 的覆盖度很高。

**问题集中在"官方逐条清单可对齐性"与"交付文档"两处**，而非代码能力本身：

1. 官方模板按**单核单镜像**编写，BK7258 是**CP/AP 双镜像**，必然产生结构性偏离；
2. 这些偏离多数是**合理且已在 `boards/bk7258/CONFIGS.md` 中论证过**的，但**没有一处
   被写进评委必读的入口文档**，因此按官方字面清单核对时会被判为"缺失"；
3. `README.md` 仍是组委会模板，未改写为作品说明 —— 这是**性价比最高、风险最低**的修复项。

建议同步下调 `docs/bk7258-t5ai/openvela-document-adaptation-matrix.md:57-59`
对 1443/1444/1445 的 ✅ 判定（见 F-07）。

---

## 2. 施工清单

按「修复价值 ÷ 风险」排序。**A 类：必做，零风险。B 类：建议做。C 类：仅文档声明，不要改代码。**

| 编号 | 问题 | 类别 | 风险 | 验收方式 |
|---|---|---|---|---|
| F-01 | `README.md` 仍是组委会说明书，未改写为作品说明 | A | 无 | 人工检查五要素齐全 |
| F-02 | `README.md` 缺构建前置步骤（`toolchain install` / `sdk install`） | A | 无 | 照 README 走一遍 |
| F-03 | 13 个空 configs 目录残留（未入库，仅磁盘） | A | 无 | `find` 无空目录 |
| F-04 | 13 项改动未提交（含 3 个新增文件） | A | 无 | `git status` 干净 |
| F-05 | `AGENTS.md` 子命令清单过时，漏 `toolchain`/`release` | B | 无 | 与 `bk7258.py` 逐一比对 |
| F-06 | `etc/group`、`etc/passwd` 缺失，`RCRAWS` 未使用 | B | 低 | clean build + 启动 |
| F-07 | 适配矩阵对 1443/1444/1445 的 ✅ 过度宣称 | B | 无 | 人工复核 |
| F-08 | `logs/` 混入 7 个硬件证据目录，非 AI 对话日志 | B | 无 | 目录检查 |
| F-09 | 残留 rv1126b 历史文档与 `round4-ap/`、`round4-cp/` 空目录 | B | 无 | 目录检查 |
| F-10 | 芯片层三个配套头文件缺失 | C | — | 只声明，不改代码 |
| F-11 | `up_prioritize_irq` 未按 `#ifdef CONFIG_ARCH_IRQPRIO` 包裹 | C | — | 只声明，不改代码 |
| F-12 | 无 `configs/nsh/`，改用 CP/AP 成对 base pair | C | — | 只声明，**不要新增 nsh** |
| F-13 | 未采用官方优先推荐的 `arch_alarm` 模型 | C | — | 只声明，不改代码 |
| F-14 | `board_early_initialize` 未实现 | C | — | 只声明，不改代码 |
| F-15 | 1444 §四 向量表优化未启用 | C | — | 只声明，不改代码 |
| F-16 | Kconfig 无芯片型号 `choice` | C | — | 只声明，不改代码 |
| F-17 | AP 核 `__start` 无 clock init 与 earlyserialinit | C | — | 已确认架构合理，**不改** |

> **C 类不需要动代码。** 它们的共同性质是：与官方模板字面不符，但由 BK7258 三核架构
> 或既有设计决定，改动收益低于回归风险。统一做法是写进 F-01 的作品说明「与官方模板的
> 差异说明」小节。

---

## 3. A 类：必做项详细施工单

### F-01 `README.md` 未改写为作品说明 【高价值 · 零风险】

**官方依据**：`README.md:147-178` 组委会模板第六节明确要求「作品提交前，请把它替换成
你自己作品的说明」，并给出五要素模板；`README.md:178` 提示评分依赖
「作品本身 + README 说明 + `logs/` 里的 AI Coding 日志」。

**现状证据** `[已复核]`：

| 位置 | 当前内容 | 判定 |
|---|---|---|
| `README.md:3-7` | 「👋 欢迎参加 2026 首届 openvela AI 硬件开发者大赛！…这是组委会为你的队伍创建的**专属参赛仓库**」 | 仍是模板 |
| `README.md:9-27` | 仅新增了一组文档链接（当前 BK7258 作品入口） | 已加工，但非作品说明 |
| `README.md:147-178` | 「## 六、提交前：把本 README 改成你的作品说明 … **本文件目前是组委会给的使用说明书**」 | 模板指令原样保留 |
| `README.md:31-49` | 「## 一、先读这些官方文档」表格 | 模板内容 |
| `README.md:51-61` | 「## 二、第一步：拉取完整工程」 | 模板内容 |

**缺失的五要素**（官方模板要求，当前一个都没有）：
1. 作品简介 —— 一句话说明作品是什么、解决什么问题、亮点
2. 选题方向 —— 三选一并简述理由（本作品属**新硬件适配**赛道）
3. 目录结构 —— 各目录/文件作用
4. 运行方式 —— 评委可照做的完整复现步骤
5. AI Coding 使用说明 —— 需求拆解/方案设计/编码/调试/文档各环节如何与 AI 协作

**修复动作**：改写 `README.md`，保留 `:9-27` 的文档导航（有价值），删除/替换
`:1-7`、`:29-49`、`:147-186` 的模板内容。建议结构：

```markdown
# <作品名：建议 "BK7258 三核 openvela 适配（CP/AP 双镜像 + 签名启动链）">

## 一、作品简介
## 二、选题方向          ← 新硬件适配赛道
## 三、目录结构          ← 见下方速查表
## 四、运行方式          ← 必须含 F-02 的前置步骤
## 五、AI Coding 使用说明 ← 指向 logs/
## 六、与官方适配模板的差异说明  ← 直接复用本文 §5 的 C 类清单
```

「目录结构」可直接引用仓库现状（`README.md:81-87` 已有约定，需补 `docs/`、`progress/`、`tools/`）：

| 目录 | 作用 |
|---|---|
| `chips/bk7258/` | SoC 共用实现：CP/AP/CPU2、IRQ、timer、SDK wrapper、bootloader |
| `boards/bk7258/` | 三块物理板 profile、引脚、bringup；`common/` 承担跨板复用 |
| `tools/bk7258/` | 唯一构建入口 `bk7258.py`（build/sdk/toolchain/package/release） |
| `app/`、`quickapp/` | 示例应用与快应用 |
| `tests/bk7258/` | cmocka 主机回归夹具 |
| `docs/` | 文档分层；`docs/openvela-guides/` 为官方镜像 |
| `progress/` | 动态证据：任务、里程碑、实板验证记录 |
| `logs/` | AI Coding 日志（评分依据） |

**⚠️ 需要 owner 提供的信息**（执行 agent 无法自行编造）：
- 作品正式名称
- 亮点的量化表述（建议取用：CoreMark mean 561.576945 @ CP 240 MHz，较 160 MHz 基线
  +50.030891%，见 `progress/verification/2026-08-27-bk7258-sdk-clock-240m-validation.md`）
- AI 协作方式的真实描述

**验收**：人工确认五要素齐全，且全文不再出现「组委会」「模板」「请替换」字样。
可用 `grep -nE '组委会|模板|请替换' README.md` 辅助（期望：仅剩 `README.md:89` 附近
对 `.gitignore.example` 的正常引用，需人工判别）。

---

### F-02 `README.md` 缺构建前置步骤 【高价值 · 零风险】

**官方依据**：官方 1443 §四 要求给出可复现构建步骤；README 自己承诺
「最好能让评委照着一步步复现」（`README.md:169`）。

**现状证据** `[已复核]`：

- `README.md:55-59` 只给 `repo init` / `repo sync -c -j8`
- `README.md:97-102` 直接跳到 `tools/bk7258/bk7258.py build --board t5ai_core --boot direct`
- 全仓 `README.md` 与 `docs/` 中「toolchain install」「sdk install」说明缺失
- **但安装机制是存在的**：`tools/bk7258/bk7258.py:56-61`（toolchain）、`:63-72`（sdk）

**关键约束证据** `[已复核]`：
- `contest2026_135_yongwangzhiqian.xml:11-16` 中 SDK 项目带 `groups="bk7258-sdk"`，
  且没有 `notdefault`；repo 会自动把这类项目加入 `default`，因此普通默认同步会
  拉取 SDK。README 仍显式使用 `-g default,bk7258-sdk`，用于固定复现输入而非补漏。
- `tools/bk7258/_lib/build.py:754-765` 注入 `BK7258_SDK_DIR`、`BK7258_TOOLCHAIN_BIN`、
  `BK7258_PARTITION_CSV/HEADER/LINKER/ID/SHA256` 等必需环境变量 →
  **直接裸跑 `build.sh` 不成立**，必须经 `bk7258.py`

**修复动作**：在 `README.md`「运行方式」中补成四步：

```bash
# 1. 拉取工程
repo init -u https://github.com/open-vela/contest2026_135_yongwangzhiqian \
  -b dev-ai-contest-2026 -m contest2026_135_yongwangzhiqian.xml
repo sync -c -j8
cd contest2026_135_yongwangzhiqian

# 2. 安装锁定工具链（ARM 官方源 + sha256 校验，落到 prebuilt/）
tools/bk7258/bk7258.py toolchain install

# 3. 从默认同步得到的 manifest 锁定 SDK 重建本机 bundle
tools/bk7258/bk7258.py sdk list
tools/bk7258/bk7258.py sdk install --profile <profile>

# 4. 构建
tools/bk7258/bk7258.py build --board t5ai_core --boot direct
```

**⚠️ 执行 agent 需核实**：`sdk install --profile` 的确切参数名与可用 profile 值
（`tools/bk7258/bk7258.py:66-72` 附近），以及 `sdk list` 的输出格式。**不要凭猜测写死**。

**同时应显式声明**：本作品**不直接调用**通用 `build.sh`（`README.md:126` 已提及，
需保留并强化），因为 CP/AP 双镜像需要配对校验与分区注入。

**验收**：在干净工作区照 README 四步执行，能到达 clean build PASS。
（若环境无网络/无 SDK 权限，退化为：确认三条命令均被 `bk7258.py --help` 接受。）

---

### F-03 删除 13 个空 configs 目录 【零风险】

**现状证据** `[已复核]`（磁盘存在，**但 `git ls-files` 无记录，即未入库**）：

```
boards/bk7258/t5_board/configs/       personal/ t5_board_ap_base/ t5_board_ap_base_vela_claw/
                                      t5_board_cp_base/ t5_board_cp_perf/ t5_board_cp_xts/
boards/bk7258/t5ai_core/configs/      t5ai_core_ap_base/ t5ai_core_ap_base_vela_claw/
                                      t5ai_core_ap_drivercheck/ t5ai_core_cp_base/
                                      t5ai_core_cp_drivercheck/
boards/bk7258/aidk_ai_toy/configs/    aidk_ai_toy_ap_base/ aidk_ai_toy_cp_base/
```

未入库的目录对评委 clone 无影响，但**在评审现场直接看磁盘时会误导为"存在未交付配置"**。

**修复动作**：删除上述 13 个空目录（每个目录下 0 个文件）。**只删空目录**，
先确认再删：

```bash
# 先列出所有空 configs 目录，人工确认后再删
find boards/bk7258 -type d -name configs -exec sh -c \
  'for d in "$1"/*/; do [ -z "$(ls -A "$d")" ] && echo "EMPTY: $d"; done' _ {} \;
```

**注意**：保留 `t5_board/configs/{openvela_cp,openvela_ap,xts,perf}`、
`t5ai_core/configs/{openvela_cp,openvela_ap,drivercheck_cp,drivercheck_ap}`、
`aidk_ai_toy/configs/{openvela_cp,openvela_ap}` —— 这些是有效 profile（各 2 个文件）。

**验收**：上条 `find` 命令无输出；`git status` 无删除项（因未入库）。

---

### F-04 提交 13 项未提交改动 【零风险 · 需 owner 授权】

**现状证据** `[已复核]`（`git status --porcelain`）：

```
 M boards/bk7258/README.md
 M boards/bk7258/aidk_ai_toy/configs/openvela_ap/defconfig
 M boards/bk7258/aidk_ai_toy/configs/openvela_ap/profile.conf
 M boards/bk7258/aidk_ai_toy/include/bk7258_board_config.h
 M boards/bk7258/aidk_ai_toy/src/CMakeLists.txt
 M boards/bk7258/aidk_ai_toy/src/Make.defs
 M boards/bk7258/aidk_ai_toy/src/bk7258_board_bringup.c
 M boards/bk7258/common/Kconfig
 M chips/bk7258/ap/bk7258_mic.c
 M chips/bk7258/bk_idk/README.md
 M chips/bk7258/include/bk7258_mic.h
?? boards/bk7258/aidk_ai_toy/src/bk7258_aidk_camera_phase0.c
?? chips/bk7258/bk_idk/sdk-profiles/v3.1.1.9/ap-aidk.config
?? docs/bk7258-t5ai/research/bk7258-offline-voice-tts/
```

**判定**：这是**进行中的工作**（AIDK camera phase0 + mic 修改），不是遗留垃圾。

**修复动作**：⚠️ **不要由执行 agent 自行 commit。** 提交属于需 owner 授权的共享状态操作。
执行 agent 应：

1. 先确认这些改动是否可编译（`bk7258.py build --board aidk_ai_toy --boot direct`）
2. **报告给 owner 决定**：提交 / stash / 放弃
3. 若 owner 要求提交，按 `AGENTS.md` 的 Git 发布规则与 `memory/RULES.md` 执行（走 PR）

**验收**：owner 决策后 `git status` 符合预期。

---

## 4. B 类：建议项

### F-05 `AGENTS.md` 子命令清单过时 【无风险】

**证据** `[已复核]`：

- `AGENTS.md` 声明：「`tools/bk7258/bk7258.py` 是**唯一** tracked 公共入口，
  **只暴露** `build`、`sdk`、`package`、`verify`」
- 实际 `tools/bk7258/bk7258.py` 的子命令（`:32-113`）：

| 子命令 | 行号 | AGENTS.md 是否覆盖 |
|---|---|---|
| `build` | `:34` | ✅ |
| `toolchain install/verify` | `:56-61` | ❌ **漏** |
| `sdk list/verify/install/rebuild` | `:63-72` | ✅（但 `verify` 在 AGENTS.md 中被写成顶层命令，实为 sdk 子命令） |
| `package create/extract/flash-contract/materialize` | `:78-105` | ✅ |
| `release ...` | `:107-110` | ❌ **漏** |

**影响**：`AGENTS.md` 是 agent 的行为约束。清单过时会导致后续 agent 误以为
`toolchain install` 不存在（本次评审就差点因此误判，见 §0.2），进而给出错误建议。

**修复动作**：更新 `AGENTS.md` 该条目为实际子命令清单，并把 `verify` 归到 `sdk` 下。

**验收**：`AGENTS.md` 所列命令与 `bk7258.py --help` 输出逐条一致。

---

### F-06 `etc/group`、`etc/passwd` 缺失，`RCRAWS` 未使用 【低风险】

**官方依据**：
- `1445-Vendor_代码仓说明.md:85-89`：`etc` 下应含 `group`、`passwd`（「默认提供示例文件，
  实际使用时厂商需重新定义」）
- `1443-新平台适配指南.md:542-554`：示例用 `RCRAWS += etc/group etc/passwd`

**现状证据** `[已复核]`：

```bash
find boards -path "*etc*" -type f
# 只有 6 个文件：三板各 rc.sysinit + rcS；无 group、无 passwd
grep -rn "RCRAWS" boards/     # 无输出
boards/bk7258/*/src/Make.defs:6 → RCSRCS = etc/init.d/rc.sysinit etc/init.d/rcS
```

**修复动作**（两块板/三块板都要做，模板相同）：
1. 在 `boards/bk7258/<board>/src/etc/` 下新增 `group` 与 `passwd`
2. 在 `boards/bk7258/<board>/src/Make.defs` 中把 `RCRAWS` 补上：

```make
RCSRCS += etc/init.d/rc.sysinit etc/init.d/rcS
RCRAWS += etc/group etc/passwd
```

**⚠️ 先确认再改**：查 `progress/verification/` 中是否有"故意不提供 group/passwd"的决策记录
（`/etc` 走 ROMFS，无多用户场景时可能有意裁剪）。若有，本项降级为 C 类并在 README 声明。

**验收**：clean build 通过；启动后 `ls /etc` 能看到 `group`、`passwd`。

---

### F-07 适配矩阵对 1443/1444/1445 的 ✅ 过度宣称 【无风险】

**证据** `[已复核]`：

| 位置 | 当前判定 | 与本文核对 |
|---|---|---|
| `docs/bk7258-t5ai/openvela-document-adaptation-matrix.md:57` | 1443 新平台适配 = ✅「只做回归维护」 | 存在 F-12（无 nsh）、F-14（无 board_early_initialize）、F-10（头文件）等与官方字面清单的偏差 |
| `:58` | 1444 中断适配 = ✅「保持 IRQ、GPIO 和 SMP 回归」 | 存在 F-11（`up_prioritize_irq` 无条件编译）、F-15（向量表优化未启用） |
| `:59` | 1445 Vendor 仓 = ✅ | 基本成立（团队代码只落在 contest 仓，经 linkfile 消费） |

**风险**：该矩阵是本仓的自评文档，评委很可能据此抽查代码。若自评 ✅ 但官方清单项缺失，
可信度损失大于坦承 🟡。

**修复动作**：把 `:57`、`:58` 改为 🟡，并在「当前证据与边界」列补一句
「CP/AP 双镜像架构下与官方单核模板存在结构性差异，详见
`docs/review/2026-08-28-bk7258-official-guide-conformance-review.md` §5」。
`:59` 可保持 ✅。

**注意**：矩阵第 35-36 行自己也写明「状态升级为 ✅ 至少要满足：当前配置可达、干净构建通过、
产物身份可记录，并按风险完成主机或实板验收」。功能层面确已满足，此处建议改判定是
**针对"官方清单逐条对齐"这一维度**，不是否定功能完成度。措辞要准确，避免自我贬低。

**验收**：人工复核措辞，不出现"未完成""缺陷"等易被误读的词。

---

### F-08 `logs/` 混入硬件证据目录 【无风险】

**证据** `[已复核]`：`logs/` 下除符合规范的 `lijian/`（含 `manifest.json` + 6 个日期目录 +
162 个 `claude-code__<sid>.jsonl`，符合 `logs/README.md:9-18`）外，还有 7 个
`bk7258-*` 目录（约 1.5 MB，`serial.txt/.log/.raw/.ready` 等串口抓取与硬件证据）：

```
logs/bk7258-n14  logs/bk7258-n15  logs/bk7258-n15-normal-restore
logs/bk7258-secureboot-minimal-primary   logs/bk7258-secureboot-minimal-rts
logs/bk7258-secureboot-minimal-negative  logs/bk7258-secureboot-minimal-restored
```

**风险**：官方《AI Coding 日志归集与提交手册》要求 `logs/` 放 AI 对话日志。
混入硬件抓包会让评委在 406 个文件里难辨主次。

**修复动作**：把这 7 个目录迁到 `progress/verification/` 或 `docs/bk7258-t5ai/probe/`，
并在 `logs/README.md` 明确「`logs/` 下仅存放 AI Coding 日志，硬件证据另见
`progress/verification/`」。

**⚠️ 先确认再动**：查这 7 个目录是否已被 `progress/verification/` 下的记录引用为证据路径。
若有引用，迁移需同步更新引用，或改为在 `logs/README.md` 加索引说明（更省事）。

**验收**：`ls logs/` 只剩 `README.md` 与 AI 日志目录。

---

### F-09 残留其他 SoC 历史文档与空目录 【无风险】

**证据** `[已复核]`：
- `docs/rv1126b-nsh-port.md`、`docs/rv1126b-openvela-adaptation-research.md`、
  `docs/rv1126b-sdk-integration.md`、`docs/ai-worklog/` 等 rv1126b 遗留
- 顶层空目录 `round4-ap/`、`round4-cp/`
- `docs/next-stage-prompt-*.md` 6 份历史提示词

**风险**：rv1126b 是另一个 SoC，与 BK7258 无关。评委通读 `docs/` 时会困惑"到底交付的是哪个平台"。

**修复动作**：
1. 删除 `round4-ap/`、`round4-cp/`（确认为空目录后）
2. rv1126b 文档与 `next-stage-prompt-*` 属历史记录，`docs/README.md:11` 已把它们归为
   「历史设计/计划」层。**建议保留**，但在 `docs/README.md` 顶部加一行醒目说明：
   「rv1126b 系列为早期另一 SoC 的历史记录，与本次 BK7258 交付无关。」

**验收**：人工确认 `docs/README.md` 有该说明；`round4-*` 已删。

---

## 5. C 类：仅文档声明，**不要改代码**

> 以下各项均为「与官方模板字面不符，但由 BK7258 三核/双镜像架构或既有设计决定」。
> 改动收益低于回归风险。统一做法是写进 F-01 README「与官方适配模板的差异说明」小节。
> **执行 agent 看到本节时，只写文档，不要动 chips/ 和 boards/ 下的代码。**

### F-10 芯片层三个配套头文件缺失

**官方依据**：`1443:103-121`、`1445:94-108` 的目录骨架列出
`<vendor>_irq.h`、`_lowputc.h`、`_start.h`。

**证据** `[已复核]`：`find chips -name "bk7258_irq.h" -o -name "bk7258_lowputc.h"
-o -name "bk7258_start.h"` → 无输出，三者均不存在。

**为何不改**：接口契约目前由 `.c` 内部声明 + `arm_internal.h` 传递，功能完整且已实板验收。
仅为对齐官方文件名而新增三个头文件，会引入重复声明风险，收益为零。

**文档化措辞**（供 README 引用）：芯片层接口契约集中在 `chips/bk7258/include/`
（`chip.h`、`irq.h`）与 `common/include/`，未采用官方模板的
`<vendor>_irq.h`/`_lowputc.h`/`_start.h` 逐文件命名。

---

### F-11 `up_prioritize_irq` 未按条件编译包裹

**官方依据**：`1444:49-56` 要求 `#ifdef CONFIG_ARCH_IRQPRIO` 包裹。

**证据** `[已复核]`：`chips/bk7258/common/bk7258_irq.c:133` 为无条件定义。
另：`chips/bk7258/Kconfig:282/444/1261` 有 `select ARCH_IRQPRIO`，但实际 defconfig 未落到。

**为何不改**：函数无条件定义不会导致编译错误，且当前无 defconfig 启用
`CONFIG_ARCH_IRQPRIO`。加 `#ifdef` 属于纯形式对齐，改动需重跑 IRQ 回归。

**文档化措辞**：`up_prioritize_irq` 常驻编译（未按 `CONFIG_ARCH_IRQPRIO` 条件化），
当前 profile 未启用该配置项。

---

### F-12 无 `configs/nsh/`，改用 CP/AP 成对 base pair

**官方依据**：`1445:119`「默认包含 `nsh` 配置」；`1443:602` 配置路径示例为 `configs/nsh`。

**证据** `[已复核]`：`find boards -type d -name nsh` → 无输出。三块板均为
`configs/openvela_cp` + `configs/openvela_ap` 配对。

**⚠️ 不要新增 `configs/nsh/`。** 理由（来自 `boards/bk7258/CONFIGS.md:38-41` 的自证）：

> 「BK7258 needs two seeds for one normal system because CP and AP are independently
> linked NuttX images; they form one logical base pair, not two product variants.」
> 且官方规则本身允许 `configs/<purpose>/defconfig` 多个，只要求"每个代表一个真实核心
> 功能且集合保持精简"（`1443:606-608`：「不建议增加太多配置文件」）。

新增第三个 `nsh` profile 反而违反"配置集保持精简"的官方建议，并新增一份需长期维护的种子。

**文档化措辞**：本平台 CP 与 AP 为两个独立链接的 NuttX 镜像，单一 `nsh` defconfig
无法表达完整系统；等价最小基线为 `configs/openvela_cp` + `configs/openvela_ap`
配对（`CONFIG_NSH_ARCHINIT=y`、`CONFIG_ETC_ROMFS=y`），详见 `boards/bk7258/CONFIGS.md`。

---

### F-13 未采用官方优先推荐的 `arch_alarm` 模型

**官方依据**：`1443:295`「arch_alarm … **优先推荐使用**」，`1443:298-322` 给出
`up_timer_initialize` + `oneshot_lowerhalf_s` + `up_alarm_set_lowerhalf` 流程。

**证据** `[已复核]`：
- `grep -rn "up_alarm_set_lowerhalf" chips/` → 无输出
- `chips/bk7258/common/bk7258_timerisr.c:449` 为 `up_timer_initialize`
- `:500` 走 `systick_initialize(false, 32768Hz, -1)` → `:507` `up_timer_set_lowerhalf`
- `:516-521` 在 `!CONFIG_TIMER_ARCH` 分支手工 `irq_attach(NVIC_IRQ_SYSTICK, …)` 并启 SysTick

**为何不改**：arch_timer 路径已实板验收（含 generation 146 的 240 MHz 性能基线）。
切换 alarm 模型要重做时间系统全量回归，风险显著高于收益。

**文档化措辞**：系统时基采用 `arch_timer` + SysTick（`bk7258_timerisr.c:449`），
未采用官方优先推荐的 `arch_alarm` oneshot 模型；周期重载路径存在官方
`1443:291-296` 所述的累计误差考量，已通过时钟切换与溢出回归覆盖
（见 `openvela-document-adaptation-matrix.md:85` 对 1505 Arch Timer 的 ✅）。

---

### F-14 `board_early_initialize` 未实现

**官方依据**：`1443:489`、`1443:506-511` 列为初始化四阶段之首。

**证据** `[已复核]`：全仓无 `board_early_initialize` 定义；
`CONFIG_BOARD_EARLY_INITIALIZE` 在任何 defconfig 中均未启用。
已有说明见 `docs/learning/bk7258-t5ai/30-nuttx-core/30-arch-chip-board-layers.md:12`。

**现状的实际分工** `[已复核]`：

| 阶段 | 函数 | 位置 | 初始化内容 |
|---|---|---|---|
| early | — | 未实现 | — |
| late | `board_late_initialize` | `boards/bk7258/common/src/bk7258_boot.c:27` | AP→`bk7258_ap_platform_prepare()`；CP→`bk7258_cp_bringup_initialize()`（`bk7258_cp_bringup.c:39-103`） |
| app | `board_app_initialize` | `boards/bk7258/common/src/bk7258_appinit.c:33` | → `bk7258_bringup()`（`bk7258_bringup.c:60`）：DVFS procfs、Flash MTD + FTL 块设备 |
| final | `board_app_finalinitialize` | `boards/bk7258/common/src/bk7258_finalinit.c:103` | statfs/stat 校验 ROMFS、procfs、`/data`（LittleFS） |

`boards/bk7258/common/Kconfig:9` 以 `select BOARD_LATE_INITIALIZE` 强制开启 late 阶段。

**文档化措辞**：板级初始化采用三阶段（late/app/final）。`board_early_initialize`
未启用，其职责由芯片层 `__start` 内的早期硬件初始化承担
（`chips/bk7258/cp/bk7258_start.c:253` `arm_earlyserialinit()`、`:269` `bk7258_clock_bringup_240m()`）。

---

### F-15 1444 §四 中断向量表优化未启用

**官方依据**：`1444:270-323`，建议启用 `CONFIG_ARCH_MINIMAL_VECTORTABLE` /
`CONFIG_ARCH_MINIMAL_VECTORTABLE_DYNAMIC` / `CONFIG_ARCH_NUSER_INTERRUPTS`。

**证据** `[已复核]`：全部 10 个 defconfig 中零命中；`NR_IRQS` 为 80
（`chips/bk7258/include/irq.h:64`），即 80 个 `irq_info_s` 全量分配。

**为何不改**：这是内存优化项，非功能缺陷。NR_IRQS=80 的静态表开销在 PSRAM 配置下不敏感，
启用动态映射需重跑全部 IRQ/GPIO/SMP 回归。

**文档化措辞**：中断向量表使用静态全量表（NR_IRQS=80），未启用 `1444` §四 的
精简/动态映射优化；属内存优化项，非功能缺口。

---

### F-16 Kconfig 无芯片型号 `choice`

**官方依据**：`1443:390-421` 示例用 `choice`/`endchoice` 定义芯片型号菜单。

**证据** `[已复核]`：`chips/bk7258/Kconfig:19` 为单一 `config ARCH_CHIP_BK7258`，
无芯片型号 choice。三个 choice 分别是 SWD 目标核（`:63`）、early console（`:92`）、
SMP 诊断门（`:340`）。片内驱动开关齐备（Kconfig 共 2237 行）。

**为何不改**：单 SoC 场景下 choice 无实际选择空间，属模板结构对齐，无功能收益。

**文档化措辞**：`chips/bk7258/Kconfig` 采用单一 `ARCH_CHIP_BK7258` 配置项
（无同系列多型号 choice），芯片内模块开关以独立 config 项组织。

---

### F-17 AP 核 `__start` 无 clock 初始化与 earlyserialinit —— **已确认架构合理，不改**

**证据** `[已复核]`：

```bash
grep -nE "clock|serial" chips/bk7258/ap/bk7258_ap_start.c   # 无输出
```

对照 CP（`chips/bk7258/cp/bk7258_start.c`）：

| 职责 | CP | AP |
|---|---|---|
| 关中断 | `:129` | `ap/bk7258_ap_start.c:210` |
| 拷贝 .data | `:191-195` | `:237-242` |
| 清 BSS | `:199-204` | `:263-269` |
| early serial | `:253` `arm_earlyserialinit()` | **无**（架构合理，见下） |
| clock | `:269` `bk7258_clock_bringup_240m()` | **无**（架构合理，见下） |
| stack limit | `:86,103` | `:52,67` |
| `nx_start()` | `:282` | `:273` |

**为何合理**：AP 由 CP 拉起，系统时钟由 CP 在 `:269` 统一 bringup；AP console 不占物理
UART，走 RPMsg Syslog（CP 为 server、AP 为 client，见
`openvela-document-adaptation-matrix.md:64`）。因此 AP 侧既不需要、也不应重复初始化
clock 与 early serial。

**文档化措辞**：AP 为 CP 拉起的从核，时钟由 CP 的 `__start` 统一 bringup，
AP console 经 RPMsg Syslog 输出而非独占物理 UART，故 AP 的 `__start` 不含
clock 与 earlyserialinit 步骤。这是三核架构下的职责划分，非遗漏。

---

## 6. 已核实无误、无需处理的项（防止执行 agent 重复排查）

| 项 | 核实结果 `[已复核]` |
|---|---|
| `Make.defs` / `CMakeLists.txt` 源文件一致性 | 展开 `foreach` 后 120 = 120，双向差集为空（见 §0.1） |
| 工具链获取 | `bk7258.py toolchain install`，ARM 官方 URL + sha256 锁定，不入仓为正确设计（见 §0.2） |
| 构建入口性质 | `tools/bk7258/_lib/build.py:786-791` 确实调用官方 `build.sh` 并传 `--cmake`，是包装器而非另起炉灶 |
| 链接脚本两条硬性要求 | `boards/bk7258/common/scripts/ld.script:59` `ENTRY(_vectors)` ✓；`:169` 有 `.ARM.exidx` ✓；`ld_ap.script:29` / `:126` 同样满足 |
| `uart_register("/dev/console")` | `chips/bk7258/common/bk7258_serial.c:829` 真实调用；`/dev/ttyS0/1/2` 在 `:816/:819/:822` |
| `up_putc` | `chips/bk7258/common/bk7258_lowputc.c:346`（console）/ `:368`（空实现）；RTT 版 `common/bk7258_rtt_lowputc.c:31` |
| 中断宏 | `chips/bk7258/include/irq.h:61-120`；`NVIC_IRQ_FIRST=16`、`NR_IRQS=80`、优先级 3 bit 占 `[7:5]`；与官方示例取值不同但自洽，且有 `_Static_assert` 编译期校验（`bk7258_irq.c:64-70`） |
| 堆注册 | `chips/bk7258/common/bk7258_allocateheap.c:68`，起点 `g_idle_topstack`、终点 `_eheap` |
| 中断栈 / IDLE 栈 | `CONFIG_ARCH_INTERRUPTSTACK=2048`、`CONFIG_IDLETHREAD_STACKSIZE=2048`，10 个 defconfig 全部一致 |
| 编译产物污染 | `git ls-files` 匹配 `__pycache__\|/build/\|\.o$\|\.a$\|\.bin$\|\.elf$\|\.pyc$` 计数为 **0** |
| 团队代码 TODO/FIXME | `chips/` 下 252 处**全部**来自 78 个 `bk_idk/armino_as_lib/**`（第三方 SDK，被 `.gitignore:28` 忽略、git 跟踪数 0）；团队自研 chips 代码 **0 处**，boards 1 处（`common/include/bk7258_board.h:85`），tools 0 处 |
| AI 日志格式合规 | `logs/lijian/manifest.json` + 6 个日期目录 + 162 个 `claude-code__<sid>.jsonl`，符合 `logs/README.md:9-18`；`.gitignore:23` 显式要求不忽略 `logs/` |
| 外部仓库改动 | `progress/CURRENT.md`：「The external NuttX and apps repositories have zero tracked modifications」——符合 `1443:77`「不得修改核心代码」 |

---

## 7. 建议执行顺序

| 步 | 编号 | 动作 | 阻塞条件 |
|---|---|---|---|
| 1 | F-03 | 删除 13 个空 configs 目录 | 无 |
| 2 | F-05 | 更新 `AGENTS.md` 子命令清单 | 无 |
| 3 | F-02 | 补 README 构建前置步骤 | 需核实 `sdk install` 确切参数 |
| 4 | F-01 | 改写 README 为作品说明 | ⚠️ **需 owner 提供作品名、亮点、AI 协作描述**；并引用 §5 的 C 类差异说明 |
| 5 | F-07 | 下调矩阵 1443/1444 判定为 🟡 | 建议在 F-01 之后，措辞保持一致 |
| 6 | F-06 | 补 `etc/group`、`etc/passwd` | 需先确认无"有意裁剪"决策记录；需 clean build 验证 |
| 7 | F-08 | 迁移 `logs/bk7258-*` | 需先检查 `progress/verification/` 是否引用这些路径 |
| 8 | F-09 | 清理 rv1126b 标注与空目录 | 无 |
| 9 | F-04 | 处理 13 项未提交改动 | ⚠️ **需 owner 决策**，执行 agent 不得自行 commit |

**不要执行**：F-10 ~ F-17 的任何代码改动（见 §5，仅文档声明）。
**不要执行**：§0 三条已撤销项的任何"修复"。

---

## 8. 给 owner 的三个待决问题

1. **F-01 作品信息**：作品正式名称？亮点希望强调哪几项（建议：三核 SMP bringup /
   签名安全启动链 / CP 240 MHz CoreMark 561.576945 较基线 +50.03% / Agent 音视频实板验收）？
2. **F-04 未提交改动**：AIDK camera phase0 + mic 相关 13 项改动，是要提交、stash 还是放弃？
3. **F-06 / F-07 取舍**：`etc/group`+`passwd` 是否确实需要（还是有意裁剪）？
   适配矩阵的 1443/1444 是否接受从 ✅ 下调为 🟡？

---

## 附：评审方法说明

- 全部结论均基于只读检查（`Read` / `Grep` / `Glob` / `Bash` 只读命令），未修改任何文件。
- 芯片层与板级层的初查由两个子代理并行完成，本人对其中**所有高/中危结论**逐条亲自复核。
- 复核过程中推翻了 3 条误报（§0），下调了 2 条判定（F-12 由"缺失"改为"有理偏离"、
  F-17 由"缺陷"改为"架构合理"）。
- 标注 `[待复核]` 的内容目前为空：所有写入本文的结论均已亲自复核。
