# BK7258 T5-AI 主 Stage 恢复提示词索引

> 用法：`/clear` 后打开下表标为 **CURRENT** 的 Stage 文件，复制其中完整的 fenced prompt 到新会话。历史 Stage 提示词只作为证据与上下文，不构成恢复过时方案或越过当前门禁的授权。

## 主 Stage 顺序

| MAIN Stage | 范围 | 状态 | 记录 / 提示词 |
|---|---|---|---|
| N1 | minimal NuttX boot | `board-verified`，commit `40495ca` | [porting report](porting-report.md) |
| N2 | interactive NSH | `board-verified`，code `9f45bc6` + docs `e3ad3e9` | [N2 worklog](nuttx-port/n2-nsh-console.md) |
| N3 | procfs + `ps` | `board-verified`，code `4d9198e` + docs `68badfe` | [N3 worklog](nuttx-port/n3-procfs-ps.md) |
| **N4** | DPLL / 480 MHz clock bring-up | **CURRENT**：N4-D0/D0D substage `board-verified`（feature commit `6f596b7`，2026-07-18）；N4-D1 blocked；DPLL enable / mux 切换 not attempted；整 N4 not board-verified | [N4 recovery prompt](nuttx-port/prompts/04-n4-clock-bringup.md) / [N4-D0 worklog](nuttx-port/n4-d0-clock-diag.md) |
| N5+ | 暂不分配范围 | N4 板端验证后再确定 | 生成 `05-n5-<slug>.md`，追加本表并更新 CURRENT 指针；不得覆盖 N4 文件 |

## 当前 handoff

- **Current Stage：N4**
- **Current prompt：**[`nuttx-port/prompts/04-n4-clock-bringup.md`](nuttx-port/prompts/04-n4-clock-bringup.md)
- **Prerequisite：**N3 已 `board-verified` ✅
- **Execution evidence：**N4-D0/D0D（时钟诊断 baseline + runtime SysTick bookkeeping，feature commit
  `6f596b7`）已 **substage `board-verified`**（2026-07-18）；**N4-D1（DPLL lock）blocked**；DPLL enable /
  mux 切换 not attempted；**整 N4 not board-verified**。详见 [N4-D0 worklog](nuttx-port/n4-d0-clock-diag.md)。
  剩余收口：`6f596b7` 精确 commit 的 state-C 重编/重刷复验尚未完成。

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

## 目录图

```text
docs/bk7258-t5ai/
├── next-stage-prompt.md
└── nuttx-port/
    └── prompts/
        └── 04-n4-clock-bringup.md
```
