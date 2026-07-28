# Stage N4 — DPLL / 480 MHz 时钟 bring-up 恢复提示词

| 字段 | 值 |
|---|---|
| Stage | N4 |
| Status | Current/planned |
| Prerequisite | N3 `board-verified` |
| Master | [`../../next-stage-prompt.md`](../../next-stage-prompt.md) |
| Previous | [`../n3-procfs-ps.md`](../n3-procfs-ps.md) |
| Next | `N5 未定义，N4 完成后生成 05-n5-<slug>.md` |
| Authorization profile | 默认只读研究；overlay mutation、build/menuconfig、package、flash、commit、push 分别受用户明确授权门禁约束；Windows 刷机由用户执行 |

将下面完整的 fenced text 块复制到新会话。

```text
我们正在 openvela 2026 竞赛工作区继续 BK7258 T5-AI 的 NuttX 移植。当前 MAIN Stage 是 N4：DPLL / 480 MHz 时钟 bring-up。N4 尚未开始执行，也没有 build-verified 或 board-verified 证据；先做只读研究和计划，不得把规划写成完成状态。

═══════════════════════════════════════════════════════════════
A. 当前 baseline：N3 board-verified（冻结证据）
═══════════════════════════════════════════════════════════════

阶段与 immutable anchors：
- branch anchor：contest2026-multi-board
- N1 code anchor：40495ca（minimal NuttX boot，board-verified）
- N2 code anchor：9f45bc6；N2 docs anchor：e3ad3e9（interactive NSH，board-verified）
- N3 code anchor：4d9198e；N3 docs anchor：68badfe（procfs + ps，board-verified）
- N3 完成并记录 anchors 时工作树是 clean；这只是历史事实，不是当前会话状态。开局必须重新运行 git status / git log，不能假定 current HEAD、dirty 状态或 push 状态仍相同。

N3 板端证据：
- boot trace 结束于：N2 / DBESITtCAP / NuttShell (NSH) / nsh>
- A = board_app_initialize 入口；P = procfs mount 成功。
- ps 列出 PID 0 / CPU0 / IDLE，以及 PID 1 / CPU0 / nsh_main。
- /proc 包含：0、1、cpuinfo、fs、memdump、meminfo、self、tcbinfo、uptime、version。
- state-C 的 /proc/version 构建时间戳：2026-07-18 15:11:55。

N3 known-good artifacts：
- $FW/all-app.bin：163574 B = 0x27EF6
  SHA-256：8cdc784fba08b931d124376f545f85c538e55626ace2be67b555b34ac7dc08a6
- $FW/nuttx.bin：88388 B
  SHA-256：74b6e5a7bdb8fabe9a30c8fbaa263244e1c861233ede63cfda61a56f55dfd8ef

这些长度与哈希只标识 N3 known-good 镜像。$FW/all-app.bin 是可变路径；任何 N4 rebuild 都必须重新计算长度与哈希，绝不能把 0x27EF6 直接用于新镜像。

═══════════════════════════════════════════════════════════════
B. 路径变量
═══════════════════════════════════════════════════════════════

export WORKSPACE=/home/lijian/project/open-vela
export CONTEST="$WORKSPACE/contest2026_135_yongwangzhiqian"
export FW="$WORKSPACE/nuttx"
export BK7258_SDK=/home/lijian/project/armino/bk_avdk_smp
export BK_IDK=/home/lijian/project/TuyaOpen/bk_idk
export ZEPHYR_PORT=/home/lijian/project/TuyaOpen/zephyr-bk7258-port

除本变量定义块外，持久化 docs、worklog、commit message 与恢复提示词只能使用 $WORKSPACE、$CONTEST、$FW、$BK7258_SDK、$BK_IDK、$ZEPHYR_PORT 等占位符，不写本机绝对路径。文档提交不能把它自身尚未知晓的 commit SHA 写进自身；先用描述性占位，后续只作事实性补记。

═══════════════════════════════════════════════════════════════
C. 硬边界与工作方式
═══════════════════════════════════════════════════════════════

1. Overlay-only：所有竞赛改动只允许落在 $CONTEST，主要范围是 $CONTEST/board/bk7258_t5ai/ 与 $CONTEST/docs/bk7258-t5ai/。manifest/linkfile 生成出来的 vendor 路径不是本阶段直接修改点。
2. 不修改官方 nuttx/apps/packages/vendor checkout；如发现确需上游修改，停止并向用户说明，不把它混入 N4。
3. CodeGraph first：定位或理解 workspace 内代码前，先用 codegraph_explore，projectPath=$WORKSPACE；只有索引没有覆盖时才使用针对性的只读搜索/读取。
4. main 模型负责苏格拉底式澄清、计划、证据审核与授权门禁；广泛搜索、重复检查、机械编辑和验证运行在合适时委派普通 subagent。不要使用 Skill 或 Workflow，除非用户明确要求。
5. 只有真实板端观察才能标记 board-verified。静态推断是 static-only，构建成功最多是 build-verified；两者都不能替代板端证据。
6. 必须保留 N3 known-good 镜像与恢复路径。任何风险步骤都应可通过用户重刷 known-good $FW/all-app.bin 回退；不得先破坏唯一回退副本。
7. Mutation、menuconfig、build、package、flash、commit、push 是独立门禁。每次进入相应动作前列出精确范围并获得用户明确授权；不得把一次授权外推到后续阶段。
8. Windows 刷机由用户执行。AI 只在获得授权后准备并核对镜像、长度、哈希和命令模板，不自行 flash。
9. N4 分段推进，每段都先审查证据再决定是否进入下一段。出现未解释寄存器值、时钟源不明、测量不独立或 baseline 回归时立即停止。

统一状态词：static-only、build-verified、board-verified、skipped、blocked。

═══════════════════════════════════════════════════════════════
D. 官方硬件事实
═══════════════════════════════════════════════════════════════

BK7258 芯片：
- ARM Cortex-M33F，官方最高 480 MHz，核心高频路径由 DPLL 提供。
- 16 KB ITCM + 16 KB DTCM。
- 芯片能力上限：16 MB flash、16 MB PSRAM、640 KB SRAM。
- 集成 Wi-Fi 6 与 BLE 5.4。
- CPU0 是 boot master；CPU1、CPU2 是由 app 唤醒的 AP 副核。

Tuya T5-AI 模组实例：
- 8 MB SiP flash。
- 16 MB SiP PSRAM；当前 NuttX baseline 未使用 PSRAM。
- 本 Stage 只处理 CPU0 时钟。CPU1/CPU2 唤醒和 NuttX SMP 明确排除。

═══════════════════════════════════════════════════════════════
E. 已知时钟事实与待证假设
═══════════════════════════════════════════════════════════════

已知架构关系：26 MHz XTALH → DPLL（320/480 MHz 工作点）→ CPU core。
当前 overlay 的 BOARD_CPU_FREQ_HZ=26000000。自制 Tier-1 bootloader 只建立当前启动/UART 路径，没有配置 DPLL。

工作假设：当前 N3 CPU0 很可能仍由约 26 MHz XTALH 驱动。这个结论尚不能仅凭源码常量或“系统能运行”成立，必须在 N4-D0 用时钟寄存器 readback 加独立时间基准测量共同证明；若证据不支持，应修正假设而不是迁就计划。

/proc/cpuinfo 的 cpu MHz : 0.000 不是 26 MHz 或 480 MHz 的证明，也不是 N4 的主要验收手段。它可能只是 NuttX 频率上报路径未接好。最终 cpuinfo 显示 480 MHz 可以作为可选打磨，但独立测量不可省略。

═══════════════════════════════════════════════════════════════
F. 受保护的 baseline 与主要风险
═══════════════════════════════════════════════════════════════

启动/镜像契约：
- BootROM → Tier-1 bootloader（logical 0x02000000 / physical 0x0）→ NuttX app（logical 0x02010000 / physical 0x11000）。
- flash 物理格式为每 32 B 数据追加 2 B CRC16，控制器向 CPU 提供透明逻辑视图。
- app magic "BK7236\0\0" 位于 app 文件偏移 0x100（向量表槽 64/65）。
- all-app.bin 由 bl_crc.bin 与 nuttx_crc.bin 组成。N4 默认不改变启动链、分区布局、magic 或 CRC 格式。

受保护运行能力：
- UART1 console：460800 8N1，当前 UART clk_div=0x37；TX 轮询，RX 中断；NuttX IRQ 31。
- SysTick 已能驱动调度、NSH 和 /proc/uptime；当前 baseline 可交互且 procfs/ps 正常。

N4 必须显式处理的风险：
1. UART 时钟域：如果 UART1 的源时钟或分频随 CPU/DPLL mux 变化，原 0x37 可能不再对应 460800，表现为乱码或完全失联。必须先证明 UART1 clock source，再决定保持或重算 divisor。
2. SysTick：CPU 频率改变后，reload 或 NuttX 频率 bookkeeping 若仍按 26 MHz，sleep、调度 tick 和 /proc/uptime 会按错误速率运行。必须找到实际初始化/重载调用链并同步更新。
3. 电压与 flash：480 MHz 可能要求先提高电压、设置 flash wait state/cache/divider。顺序错误可能在 mux 切换瞬间取指失败，UART 来不及输出。
4. DPLL 配置：必须有有限时 lock wait 和 lock readback；禁止无限循环等待。
5. mux/divider 顺序：先决条件、DPLL lock、分频器、CPU mux、barrier 的顺序必须来自多源证据，不能凭经验猜。
6. 失败可观测性：在仍安全的时钟点设计单字符 trace/readback；不要依赖切换失败后仍能运行 C printf。每个 trace 字符及其位置都要记录。

═══════════════════════════════════════════════════════════════
G. Stage N4 目标、范围与非目标
═══════════════════════════════════════════════════════════════

唯一主目标：在 CPU0 上安全完成 DPLL / 480 MHz boot-time clock bring-up，并用寄存器 readback + 独立测量证明约 480 MHz；同时保持 UART1、SysTick、NSH、procfs/ps 和启动链回归通过。

范围内：
- N4-R 只读研究。
- 26 MHz 诊断 baseline。
- DPLL enable/lock、CPU mux/divider、所需 voltage/flash wait-state 配置。
- NuttX CPU frequency bookkeeping 与 SysTick 时间基准修正。
- 最小、可审计、overlay-only 的 boot-time 初始化与诊断。
- Windows 用户刷机后的板端证据、5 分钟 soak、3 次 reboot、产物 provenance。

非目标：
- MTD / LittleFS / SmartFS 或其他持久文件系统。
- PSRAM 初始化或把 heap 迁入 PSRAM。
- Tier-2 bootloader、OTA、A/B failover。
- CPU1/CPU2、SMP。
- Wi-Fi / BLE。
- runtime DVFS、动态调频或通用 power-management framework。

═══════════════════════════════════════════════════════════════
H. N4-R 必须回答的问题
═══════════════════════════════════════════════════════════════

1. BK7258 vendor 的准确上电序列是什么：DPLL 配置字段、enable、calibration、lock bit、timeout、CPU mux、core/bus/peripheral divider、barrier 各自的顺序和值？
2. 480 MHz 前是否必须改变 core voltage、LDO/DCDC 档位、flash wait state、flash/cache clock divider；这些要求在哪些芯片/板级源码或规格中被证实？
3. 320 MHz 是否是强制中间工作点、仅 vendor 调试步骤，还是可以跳过？什么证据决定 N4-D2 是否执行？
4. UART1 当前时钟源究竟是 XTALH、独立 peripheral clock、DPLL 分支还是 CPU/bus clock？clk_div=0x37 的公式与 460800 的关系是什么？CPU 切换后是否需改 divisor？
5. NuttX 当前 SysTick 的完整频率调用链是什么：BOARD_CPU_FREQ_HZ 在何处被消费，up_timer_initialize/arch timer 如何计算 reload，是否还有缓存的 SystemCoreClock 或等价变量？
6. 独立频率测量采用哪个不随 CPU/DPLL 同比变化的参考：已知参考 timer/RTC/XTAL、外部逻辑分析仪，或其他可审计方法？原始计数与公式如何记录？
7. 最小正确集成层在哪里：chip clock 初始化、board early init、up_initialize 前后，还是其他 overlay 层？必须既早到保证 tick 正确，又不能破坏 Tier-1 UART/启动契约。
8. 如何设计失败 trace：在哪些安全点发字符、读哪些寄存器、何时停止；如何区分 DPLL 不锁、CPU mux 失败、flash wait-state 错、SysTick 错和 UART 波特率错？

研究必须交叉对照 $BK7258_SDK、$BK_IDK、$ZEPHYR_PORT 与当前 overlay；对每个关键常量/顺序记录文件、symbol、字段和值。不同来源矛盾时列出矛盾，不自行投票。

═══════════════════════════════════════════════════════════════
I. INTERNAL ORDER（全部属于同一个 N4，不拆文件）
═══════════════════════════════════════════════════════════════

1. N4-R — read-only research

Goal：回答 H 的问题，形成可审查的寄存器序列、集成点、测量方法、失败 trace 与分段计划；不改文件、不构建、不刷机。

Allowed actions：
- 重新发现 git 状态和 anchors；CodeGraph first 阅读 workspace。
- 并行只读研究 $BK7258_SDK、$BK_IDK、$ZEPHYR_PORT；读取官方资料与现有 N1/N2/N3 证据。
- 在会话中提交 N4-R evidence matrix：claim、source、exact symbol/field/value、交叉来源、置信度、未决问题。
- 在会话中给出拟修改文件清单和最小 patch 设计，但不落盘。

Stop conditions：
- 找不到确定的 DPLL lock/mux/voltage/flash wait-state 定义。
- 关键来源互相矛盾且无法解释芯片版本/构建配置差异。
- 独立测量方案实际仍与 CPU/DPLL 同源。
- 正确集成层需要修改官方 checkout，或会改变受保护启动/flash 契约。

Evidence：逐项源码引用、寄存器字段表、vendor sequence 对照表、UART/SysTick clock path、测量公式、D0-D3 trace 设计、风险/回退表。所有未确认项明确标 static-only 或 blocked。

Exit criterion：用户审核 N4-R 报告后，关键问题已回答到足以安全设计 N4-D0；否则 N4 保持 blocked，不进入 mutation。

Authorization gate：N4-R 本身严格只读。任何 mutation、menuconfig、build、package、flash、commit 或 push 前，先展示精确动作并取得用户明确授权。

2. N4-D0 — 26 MHz diagnostic baseline

Goal：在不写 DPLL/mux/clock-control 寄存器的前提下，证明当前 clock source/divider/lock 状态，并用独立参考测出约 26 MHz baseline；同时记录 UART1 与 SysTick 的真实基线行为。

Allowed actions（仅在用户授权对应 mutation/build/package 后）：
- 在 overlay 添加最小 read-only 诊断或一次性受控 instrumentation；优先复用现有 early trace/NSH 能力。
- 读取并打印 raw clock、DPLL、mux、divider、voltage、flash wait-state、UART clock 寄存器，不对这些寄存器写值。
- 实施 N4-R 选定的独立测量，记录 raw CPU cycles、raw reference counts、reference frequency、时间窗口和公式。
- 构建候选镜像、重新计算长度/哈希；由用户在 Windows 刷机后采集 boot、UART、uptime/sleep 和测量输出。

Stop conditions：
- 所谓“只读诊断”需要隐式 clock write、复位 peripheral 或改变 UART/SysTick。
- readback 与字段定义不一致，或测量与寄存器结论差异无法解释。
- UART 输入/输出、/proc/uptime、sleep、ps 或 /proc baseline 回归。
- 无法确认当前产物来自预期源码。

Evidence：git diff、精确 register address/name/raw value/decoded field、独立测量原始计数与公式、boot trace、UART1 双向输入、/proc/uptime 与外部时间对照、artifact size/hash、用户板端原始输出。

Exit criterion：clock readback 与独立测量共同证明实际 baseline（预期约 26 MHz；若不是则记录修正后的事实），N3 功能保持通过，并得到用户批准进入 N4-D1。

Authorization gate：诊断代码 mutation、build、package、Windows flash、commit、push 各自单独询问；未经授权不得进入下一动作。D0 不允许 clock switch。

3. N4-D1 — DPLL lock，但 CPU 保持 XTALH

Goal：按已证 vendor sequence 配置 DPLL 及其必要先决条件，让目标 DPLL 稳定 lock，同时 CPU mux 明确保留在 XTALH；把“PLL 能工作”和“CPU 切高频”分开去风险。

Allowed actions（逐项授权后）：
- 在 overlay 实现最小 clock helper/early init step，先设置已经证实必需且在 26 MHz 下安全的 voltage、flash wait-state、divider 前置条件。
- 配置并 enable DPLL，以有限 timeout 等待 lock；失败时记录 trace/readback 并停止。
- 明确 readback CPU mux 仍为 XTALH；禁止在本 subsection 切到 320/480 MHz。
- 用独立测量再次证明 CPU 仍约为 D0 baseline；回归 UART1、SysTick、NSH、procfs/ps。

Stop conditions：
- DPLL 在有限 timeout 内不 lock、lock 抖动或状态位定义不确定。
- CPU mux 意外改变，独立测量不再是 baseline，或 UART 波特率改变。
- 必需电压/flash 前置值没有双源证据。
- 任一 N3 regression 失败。

Evidence：写入序列及来源、每步 trace、DPLL config/lock raw readback、CPU mux/divider raw readback、独立 baseline 频率、UART/tick regression、候选 artifact provenance。

Exit criterion：DPLL 连续稳定 lock，CPU 明确仍在 XTALH 且独立测量保持 baseline，全部回归通过；用户审核后决定 D2 或 D3。

Authorization gate：D1 mutation、build、package、Windows flash、commit、push 各自需明确授权。lock 失败后不得自行继续切 mux；先回退/分析并向用户报告。

4. N4-D2 — OPTIONAL 320 MHz intermediate

Goal：只有 N4-R 证据表明 320 MHz 是 vendor 必需中间点、显著降低风险，且用户明确要求执行时，才切到 320 MHz 验证完整切换链。否则将本 subsection 标为 skipped，并记录来源与理由。

Allowed actions（仅在“需要 D2”结论和用户授权后）：
- 在已满足 voltage/flash/divider 条件下，按 vendor 顺序切到 320 MHz。
- readback DPLL lock、CPU mux 与 divider；用独立参考测量约 320 MHz。
- 校正/验证 SysTick 与 UART1 时钟；运行 N3 regression。
- 若跳过，只在 worklog/会话中记录 skipped + reason，不加入无用的 320 MHz 代码路径。

Stop conditions：
- 研究没有证明 320 MHz 的必要性，却准备把它当惯例执行。
- readback 或独立测量不接近目标，UART 失联、tick 比例错误、flash/取指异常或任何 N3 regression。
- 从 320 MHz 到 480 MHz 的差异仍有未解决的 voltage/wait-state 要求。

Evidence：若执行，记录完整 raw readback、测量计数/公式、UART/tick/N3 回归与 artifact provenance；若跳过，记录支持跳过的 vendor source、风险判断和用户决定。

Exit criterion：执行路径达到稳定、可测的约 320 MHz并回归通过，或以充分证据正式标记 skipped；两种路径都必须经用户审核后才能进入 D3。

Authorization gate：无论执行还是跳过，都先让用户确认。执行时 mutation、build、package、Windows flash、commit、push 分别受门禁约束；不得因“optional”默认获得授权。

5. N4-D3 — 480 MHz switch

Goal：在 D1 已锁定 DPLL、D2 已完成或有证据 skipped 的前提下，把 CPU0 安全切到 480 MHz，修正所有受影响的 frequency bookkeeping、SysTick 和 UART 配置，并保留可诊断失败路径。

Allowed actions（逐项授权后）：
- 只实现 N4-R 已证明的最小 boot-time 480 MHz sequence；不引入 runtime DVFS。
- 在提频前完成所需 voltage、flash wait-state/cache、bus/peripheral divider 配置；确认 DPLL lock 后再切 CPU mux，并使用要求的 barrier。
- 更新 NuttX 实际消费的 CPU frequency 定义/变量和 SysTick reload 路径，不只修改一个展示常量。
- 根据已证 UART1 clock source 决定保留或重算 divisor；在安全点输出短 trace，切换后验证 460800 8N1 双向通信。
- readback DPLL lock、CPU mux、divider、voltage/flash 状态；执行独立约 480 MHz 测量和完整 N3 regression。

Stop conditions：
- 任一 precondition 没有 readback、DPLL lock 不稳定或 mux/divider 值不符。
- 独立测量不支持约 480 MHz，或结果只来自 BOARD_CPU_FREQ_HZ/cpuinfo 等软件自报。
- UART 乱码/失联、/proc/uptime 或 sleep 比例错误、异常/复位、flash 取指不稳定、NSH/procfs regression。
- 只能靠同时大改 bootloader、flash 格式或官方 checkout 才能继续。

Evidence：最终 sequence 与源码来源、pre/post raw registers、所有 trace 字符定义、独立测量 raw counts 与公式、UART1 RX/TX、uptime/sleep 对照、ps 和 /proc 输出、artifact size/hash、用户原始板端日志。

Exit criterion：寄存器与独立测量共同证明 CPU0 约 480 MHz；UART1、SysTick、NSH、procfs/ps 回归通过；没有进入 V 前不得称 N4 board-verified。

Authorization gate：D3 mutation、menuconfig、build、package、Windows flash、commit、push 各自需用户明确授权。若切换后失联，停止连续试错，优先让用户重刷 N3 known-good 镜像并基于 trace 分析。

6. N4-V — final verification and provenance

Goal：按 N3 state-C 方法建立严格的“reviewed feature diff → feature commit → exact-commit build/package → exact artifact → Windows flash → board observation → docs”证据链。只有从已提交 feature source 构建并完成验收的路径，才满足 verified == committed-source；若用户不授权 feature commit，只能记录为 uncommitted candidate。

Allowed actions（逐项授权后，顺序不可颠倒）：
- 记录 current branch/HEAD/status/diff，整理并审查最终 feature-code diff；列出精确 feature 文件，排除无关改动，确认 D3 代码与拟验证范围一致。
- 审查通过后，**单独请求 feature commit 授权**。若获授权，先创建 feature commit，再记录其 SHA、subject 和状态；final state-C build 前确认所有 feature-code 路径与该 commit 完全一致、无未提交 feature 变更。待写 worklog/report 不混入 feature commit。
- 仅在上述 feature commit 已存在后，从该 exact commit 执行 final state-C build/package，计算 artifact 的精确字节长度、hex length 与 SHA-256，并记录 build command、feature commit SHA 和任何不影响 build input 的待写 docs 状态。
- 由用户使用该 exact artifact 刷机；通过 /proc/version 时间戳或等价 provenance 标识确认板上运行的是 state-C artifact。
- 重复寄存器 readback 与独立约 480 MHz 测量，并执行 J 的完整验收：UART1、真实时间、N3 regression、5 分钟 soak、3 次 reboot。
- 板端证据完成后才更新 N4 worklog/porting report；随后单独请求 docs commit 授权。push 始终是 docs commit 之后的另一道独立门禁。
- 若 feature commit 授权被拒绝，可在另行取得 build/package/flash 授权后，从已审查的未提交 diff 构建并上板；必须记录完整 diff/provenance，并把结果明确标为 `uncommitted candidate`，不得称其满足 verified == committed-source。

Stop conditions：
- committed-source 路径试图在 feature commit 之前进行 final state-C build，或 commit 后 feature-code 路径仍有未提交变化。
- 最终重建产物与 feature commit（或明确标记的 uncommitted candidate diff）、板上产物无法建立一一对应关系。
- 任何 reboot 不稳定、soak 期间异常、时钟 readback/测量漂移、UART/tick/N3 regression。
- 未提交候选被描述成 committed-source，或 docs 在板端证据完成前把 N4 写成已验证。

Evidence：final feature diff review、feature commit 授权结果；获授权时记录 feature commit SHA/status 与 feature-code clean 证明，未获授权时记录 uncommitted candidate 的完整 diff/provenance；另记录 build command/result、artifact byte length + hex length + hashes、Windows 命令实参、板端 provenance、3 次完整 boot traces、5 分钟 soak 日志、raw measurement/readback、N3 命令输出和最终状态标签。

Exit criterion：
- committed-source 路径：全部 acceptance criteria 在“由 exact feature commit 构建的 state-C artifact”上板端通过，随后 worklog/report 如实更新；此时才可把 N4 标为 board-verified，并声明 verified == committed-source。
- feature commit 授权被拒绝的路径：板端通过只能记录为 `uncommitted candidate`，明确不满足 verified == committed-source；N4-V 对正式 committed-source closure 仍为 blocked，后续必须在 feature commit 获授权后重新进行 state-C build/package、用户刷机和板端复验。

Authorization gate：先审查 feature diff，再单独请求 feature commit；feature commit 成功后才分别请求 final state-C build/package 与用户 flash。板端证据完成后才请求 docs mutation/docs commit；push 永远单独请求。不得自动 commit 或 push。

═══════════════════════════════════════════════════════════════
J. N4 板端验收标准
═══════════════════════════════════════════════════════════════

以下全部满足才可标记 N4 board-verified：

[ ] DPLL config/lock raw readback 正确，lock 稳定。
[ ] CPU mux 与 core/bus divider raw readback 与 480 MHz 设计一致。
[ ] 使用独立参考测得约 480 MHz；保留 raw CPU count、raw reference count、reference frequency、measurement window 和公式。通用公式为：f_cpu = Δcpu_cycles × f_ref / Δref_counts。若采用外部仪器，记录原始周期/频率、测试循环与每次翻转的已证 cycle 数。
[ ] 测量参考不随 CPU/DPLL 同比变化；仅打印 BOARD_CPU_FREQ_HZ 或 cpuinfo 不合格。
[ ] UART1 460800 8N1 输出可读；键盘输入、回显、Enter、命令解析与 prompt return 全部可用。
[ ] /proc/uptime 与外部时钟一致；sleep 1 的实际墙钟时间合理，连续观察无 tick 加速/减速。
[ ] N3 regression：boot trace 到 NuttShell/nsh>；ps 有 PID 0 IDLE + PID 1 nsh_main；/proc 有 0、1、cpuinfo、fs、memdump、meminfo、self、tcbinfo、uptime、version；cat /proc/version、cat /proc/meminfo、uname -a 正常。
[ ] 连续运行至少 5 分钟，期间 UART/NSH/tick 无异常。
[ ] 至少 3 次 reboot 均完整启动并重复关键 readback/measurement；条件允许时至少一次冷启动。
[ ] exact flashed all-app.bin 的字节长度、SHA-256、来源 commit/diff 与板端 provenance 可追溯。

/proc/cpuinfo 显示约 480 MHz 是 optional；即使仍显示 0.000，只要独立测量合格且报告解释上报缺口，也不阻塞 N4。反之，即使 cpuinfo 显示 480，也不能替代独立测量。

═══════════════════════════════════════════════════════════════
K. Build 与 Windows flash gate
═══════════════════════════════════════════════════════════════

获得 build 授权后，只从 $WORKSPACE 执行：

  cd "$WORKSPACE"
  ./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/cp_nsh -j8

获得 package/artifact 检查授权后，对实际 N4 产物重新计算：

  NEW_LENGTH_DEC=$(stat -c '%s' "$FW/all-app.bin")
  printf -v NEW_LENGTH_HEX '0x%X' "$NEW_LENGTH_DEC"
  printf 'all-app.bin: %s B = %s\n' "$NEW_LENGTH_DEC" "$NEW_LENGTH_HEX"
  stat -c '%n %s B' "$FW/nuttx.bin" "$FW/all-app.bin"
  sha256sum "$FW/nuttx.bin" "$FW/all-app.bin"

把真实输出交给用户核对。不要从 N3 文档复制长度或哈希。

用户在 Windows 的刷机模板：

  bk_loader.exe download ^
    -p <PORT_NUMBER> -b 6000000 --uart-type OTHER ^
    --mainBin-multi <ALL_APP_BIN_WINDOWS_OR_UNC_PATH>@0x0-<NEW_LENGTH_HEX> ^
    --reboot 1 --fast-link 1

<NEW_LENGTH_HEX> 表示 all-app.bin 的字节长度，不是结束地址。start=0 时长度与数值上的 end offset 恰好相同，但命令语义仍是 length。N4 不得硬编码或盲用 N3 的 0x27EF6。

进入下载模式：按住 BOOT 上电，通过 DL_UART0 刷机。运行 console 是 UART1 460800 8N1；两个串口角色不能混淆。AI 不执行 Windows flash，只等待用户回传原始输出。

═══════════════════════════════════════════════════════════════
L. 新会话开局动作
═══════════════════════════════════════════════════════════════

1. 设置/确认 B 中变量，然后运行：

  cd "$CONTEST"
  git status --short --branch
  git log --oneline --decorate -8

记录 runtime current HEAD、ahead/behind、dirty/untracked 状态；不得 reset、checkout、clean 或覆盖用户改动。

2. 对照 immutable anchors，但不要假定 HEAD 必须等于 68badfe。确认 N3 known-good $FW/all-app.bin 是否仍匹配冻结长度/哈希；若需要复制备份，先说明目标路径和副作用并获得用户 mutation 授权。

3. 对 workspace 使用 CodeGraph first，定位当前 clock/startup/SysTick/UART 集成点。随后并行开展对 $BK7258_SDK、$BK_IDK、$ZEPHYR_PORT 的只读研究；不要把大段文件 dump 给 main，只回传结论与精确证据位置。

4. 先在会话中提交 N4-R 报告：vendor sequence、寄存器字段、voltage/flash 条件、320 MHz 判断、UART source、SysTick call path、独立测量方案、集成层、failure trace、最小拟改文件和分段回退计划。

5. 明确列出仍未知或矛盾的事项。若关键安全条件未闭合，将 N4 标 blocked 并停在研究阶段。

6. 等用户审核并明确授权 N4-D0 的精确 mutation/build 范围。确认前不 edit、不 menuconfig、不 build、不 package、不 flash、不 commit、不 push。

═══════════════════════════════════════════════════════════════
M. N4 完成规则
═══════════════════════════════════════════════════════════════

N4-R、D0、D1、D2（执行或有证据 skipped）、D3、V 都是同一个 MAIN Stage N4 的有序 subsection，始终保留在这一份 04-n4-clock-bringup.md 中，不创建 N4 子阶段 prompt 文件。

只有 N4 已在 exact final artifact 上 board-verified 且 worklog 完成后，才根据届时证据定义下一 MAIN Stage：创建恰好一个 05-n5-<slug>.md，并更新 ../../next-stage-prompt.md 的顺序表与 CURRENT 指针。不要现在创建 N5 placeholder，不要预分配 N5 scope，也绝不能把 N5 implementation 追加进本 N4 文件或覆盖 N4 历史 handoff。
```
