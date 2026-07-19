# BK7258 T5-AI 主 Stage 恢复提示词索引

> 用法：`/clear` 后打开下表标为 **CURRENT** 的 Stage 文件，复制其中完整的 fenced prompt 到新会话。历史 Stage 提示词只作为证据与上下文，不构成恢复过时方案或越过当前门禁的授权。

## 主 Stage 顺序

| MAIN Stage | 范围 | 状态 | 记录 / 提示词 |
|---|---|---|---|
| N1 | minimal NuttX boot | `board-verified`，commit `40495ca` | [porting report](porting-report.md) |
| N2 | interactive NSH | `board-verified`，code `9f45bc6` + docs `e3ad3e9` | [N2 worklog](nuttx-port/n2-nsh-console.md) |
| N3 | procfs + `ps` | `board-verified`，code `4d9198e` + docs `68badfe` | [N3 worklog](nuttx-port/n3-procfs-ps.md) |
| **N4** | DPLL / 480 MHz clock bring-up | **CURRENT**：N4-D0/D0D/D0F substage `board-verified`（D0/D0D `6f596b7` + D0F `8dab594`，2026-07-18）；N4-D1 blocked；DPLL enable / mux 切换 not attempted；整 N4 not board-verified | [N4 recovery prompt](nuttx-port/prompts/04-n4-clock-bringup.md) / [N4-D0 worklog](nuttx-port/n4-d0-clock-diag.md) |
| **N5** | Flash layout / ID / filesystem | N5-D0..D4 board-observed（2026-07-19）；**N5-D5 raw flash r/w board-verified**（2026-07-19）；**N5-D6 MTD board-verified**（方案 A，CONFIG_BK7258_FLASH_MTD）；**N5-D7 LittleFS filesystem board-verified**（/data 挂载，probe 文件重启持久化通过）；D7 版 `all-app.bin` = 192270 B = `0x2EF0E`（< `0x100000`）；全链路 raw flash → MTD → ftl block device → LittleFS board-verified | [N5 worklog](nuttx-port/n5-flash-filesystem.md) |
| N6+ | 暂不分配范围 | N4 板端验证后再确定（N5 filesystem 已 board-verified） | 生成 `06-nX-<slug>.md`，追加本表并更新 CURRENT 指针 |

## 当前 handoff

- **Current Stage：N4**
- **Current prompt：**[`nuttx-port/prompts/04-n4-clock-bringup.md`](nuttx-port/prompts/04-n4-clock-bringup.md)
- **Prerequisite：**N3 已 `board-verified` ✅
- **Execution evidence：**N4-D0/D0D（时钟诊断 baseline + runtime SysTick bookkeeping，feature commit
  `6f596b7`）+ D0F（100Hz tick 兼容性，feature commit `8dab594`）已 **substage `board-verified`**
  （2026-07-18）。D0F defconfig 移除旧 100ms override，生效默认 10ms/100Hz；`CONFIG_USEC_PER_TICK=1000`
  （1000Hz）manual-reset 路径失败重启，已 rejected。**N4-D1（DPLL lock）blocked**；DPLL enable /
  mux 切换 not attempted；**整 N4 not board-verified**。详见 [N4-D0 worklog](nuttx-port/n4-d0-clock-diag.md)。
  剩余收口：`6f596b7` 精确 commit 的 state-C 重编/重刷复验尚未完成。
- **N5 flash filesystem（board-verified 2026-07-19）：**N5-D0..D4 board-observed（layout candidate、flash ID、content dump、magic scan、emptiness scan）；N5-D5 raw flash erase/write/read-back/re-erase board-verified（`0x00100000`，2026-07-19）；N5-D6 MTD lower-half board-verified（方案 A：每次 op 临时清/恢复 SR0 块保护，CONFIG_BK7258_FLASH_MTD，`chip/bk7258_flash_mtd.[ch]`）；N5-D7 LittleFS filesystem board-verified（CONFIG_BK7258_FLASH_LITTLEFS，ftl 注册 `/dev/mtdblock0`，mount 到 `/data`，autoformat 仅首次，probe 文件重启持久化通过）。全链路：raw flash → MTD → ftl block device → LittleFS。D7 版 `all-app.bin` = 192270 B = `0x2EF0E`（< `0x100000`，boot/app 区不受影响）。详见 [N5 worklog](nuttx-port/n5-flash-filesystem.md)。
- **频率阶梯 / 480 MHz recovery note：**
  - **不要重试 `M1=0x430`（480M/1）**：Beken SDK `sys_hal_core_bus_clock_ctrl()` 中
    `PM_CLKSEL_CORE_480M` + `PM_CLKDIV_CORE_0` 组合被 guard 明确拒绝（返回 `BK_FAIL`）；板端
    探测在 `N4D0:480S` 后 stall，与 SDK 政策一致。
  - **当前最高板端/J-Link 验证的 loader-residue 操作点为 320 MHz**（M1=0x420, csrc=2, cdiv=0）。
  - 240 MHz（480M/2）和 320 MHz（320M/1）均为 SDK 支持的操作点，已板端验证。
  - **下一步只在有新证据表明安全路径时才调查 480 MHz 操作点**；当前无此证据。
  - 详细频率阶梯表与 SDK guard 分析见 [N4-D0 worklog §10](nuttx-port/n4-d0-clock-diag.md#10-频率阶梯证据与-sdk-guard-分析loader-residue-muxdiv-probes)。

## 冻结的 N3 baseline

以下是恢复会话时的不可变证据锚点，不代表未来会话的永久 current HEAD：

- branch anchor：`contest2026-multi-board`
- N3 code immutable anchor：`4d9198e`
- N3 docs immutable anchor：`68badfe`
- boot trace 结束于：`N2 / DBESITtCAP / NuttShell (NSH) / nsh>`；`A` = `board_app_initialize` 入口，`P` = procfs mount 成功
- `ps`：PID 0 / CPU0 / `IDLE`，PID 1 / CPU0 / `nsh_main`
- `/proc`：`0`、`1`、`cpuinfo`、`fs`、`memdump`、`meminfo`、`self`、`tcbinfo`、`uptime`、`version`
- state-C `/proc/version` 时间戳：`2026-07-18 15:11:55`
- known-good `$FW/all-app.bin`：163574 B = `0x27EF6`，SHA-256 `8cdc784fba08b931d124376f545f85c538e55626ace2be67b555b34ac7dc08a6`
- known-good `$FW/nuttx.bin`：88388 B，SHA-256 `74b6e5a7bdb8fabe9a30c8fbaa263244e1c861233ede63cfda61a56f55dfd8ef`

> `$FW/all-app.bin` 是可变构建产物。N4 只要重建，就必须重新计算实际字节长度与哈希；不得盲目复用 N3 的 `0x27EF6`。

## 命名与维护规则

- 每个 MAIN Stage 恰好一个提示词文件，命名为 `NN-nX-<slug>.md`；两位序号必须与 MAIN Stage 编号一致。
- 一个 Stage 内的诊断/实现/验证步骤只是有序 subsection，不拆成独立 Stage 文件。
- Stage 完成后，其文件成为不可变历史 handoff；仅允许事实性纠错，不在原文件里续写下一 Stage 实现。
- 本 master 只维护 Stage 顺序、CURRENT 指针与冻结 baseline，不复制完整 Stage prompt。
- 新会话必须运行时重新发现 current HEAD、工作树状态、push 状态和产物长度/哈希；不得把锚点误当永久现状。
- 文档提交不得把它自身尚未知晓的 docs commit SHA 写进自身；先用描述性占位，提交后再由后续事实性更新记录。
- 统一状态词：`static-only`、`build-verified`、`board-verified`、`skipped`、`blocked`。
- **CP startup attribution 已由用户确认**：Beken SDK 中存在 `Reset_Handler_Cpu0` →
  `sys_drv_early_init()` → `sys_hal_early_init()` 调用链（CP SDK early-init 路径）；loader
  `--reboot 1` 的 80 MHz residue 与此链的执行结果一致。**当前 NuttX overlay 不移植也不调用该链**；
  后续会话**不要把它说成 NuttX 已移植/执行**，只按 inherited loader residue 处理。遇到具体
  blocker（如 D1 的 DPLL locked-bit 断言、analog batch 副作用范围）时再定向查看 SDK 片段。

## 目录图

```text
docs/bk7258-t5ai/
├── next-stage-prompt.md
└── nuttx-port/
    └── prompts/
        └── 04-n4-clock-bringup.md
```
