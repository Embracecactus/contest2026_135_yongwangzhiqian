# NuttX Stage N4-D0 / D0D — 时钟诊断 baseline + runtime SysTick bookkeeping（worklog）

> 板端验证日期：2026-07-18（substage `board-verified`：仅 N4-D0/D0D；N4-D1 及整 N4 尚未板端验证）
> 基线 commit：`4d9198e`（Stage N3 code）→ 本阶段 feature commit：`6f596b7`（3 个 overlay 文件，只读诊断 + runtime SysTick 修正）
> 改动范围：`$CONTEST/board/bk7258_t5ai/chip/`（`bk7258_clockdiag.h` 新增、`bk7258_start.c` 接入、`bk7258_timerisr.c` runtime SysTick 选择）

> 占位符：`$WORKSPACE`、`$CONTEST`、`$FW` 与 README / next-stage-prompt.md 一致。本 docs 提交 SHA
> 不在本文内写出（见 [`../next-stage-prompt.md`](../next-stage-prompt.md) 命名与维护规则）。

## 1. Scope（与 N4 prompt §2 对齐）

本 worklog 记录 Stage N4 内**第一个 subsection** N4-D0 的执行证据与紧随其后的 runtime SysTick
bookkeeping 修正（记为 N4-D0D），**不是** Stage N4 整体 board-verified。

- **Feature commit**：`6f596b7`，只改 3 个 overlay 文件（`board/bk7258_t5ai/chip/`）。
- **允许动作**：在 overlay 添加最小只读诊断；读取并打印 raw clock / DPLL / mux / divider / voltage /
  flash wait-state / UART clock 寄存器；实施独立测量；runtime SysTick 频率 bookkeeping 修正。
- **明确禁止**：**不写 DPLL / CPU mux / clock-control / voltage / flash wait-state / UART divisor
  寄存器**。DPLL enable、CPU mux 切换**未尝试**（N4-D1 范围）。
- D0D 的 runtime 修正是把已读出的 baseline 频率回填给 SysTick bookkeeping（`SYSTICK_RVR` / `EXP` /
  `SYSTEM_CYCLES_PER_SEC` / `CLK` 选择），**不触碰硬件时钟源**。

## 2. 两条启动路径的 clock baseline（板端 readback）

D0 诊断发现：CPU0 当前频率依赖**复位路径**，存在两条独立的 baseline：

### 2.1 Manual-reset path（手动按 RESET 后冷启动）

`bk7258_clockdiag.h` 读出的 raw 值：

| 字段 | raw 值 | 解读 |
|---|---|---|
| M1（DPLL config lo） | `0x0` | DPLL 未配置 |
| M2（DPLL config hi） | `0x0` | DPLL 未配置 |
| `dplle`（DPLL enable bit） | `0x0` | **DPLL 关闭** |
| `csrc`（CPU mux clock source） | `0x0` | XTALH |
| `cdiv`（CPU divider） | `0x0` | /1 |
| `RVR`（SysTick reload） | `0x0027AC3F` | 默认值（与 `BOARD_CPU_FREQ_HZ=26000000` 对应） |

- 此路径下 CPU0 由约 **26 MHz XTALH** 驱动，DPLL 关、mux 在 XTALH、div /1。
- 板端 `sleep 10`（NSH builtin）的 wall-clock 行为约 **10 s**（`sleep10≈10s`），与 26 MHz baseline
  一致。
- 这是 N4-D0 的 **diagnostic baseline**。

### 2.2 Loader `--reboot 1` pre-D0D residue（bk_loader 软复位后残留）

D0D 修正前，当上位机用 `bk_loader.exe ... --reboot 1` 刷完镜像不复位直接 run，板端读到的不是
manual-reset baseline，而是 loader 预配的残留：

| 字段 | raw 值 | 解读 |
|---|---|---|
| M1 | `0x00000423` | DPLL 已被 loader 预配 |
| M2 | `0x05000000` | DPLL 已被 loader 预配 |
| `csrc` | `0x2` | 非 XTALH（loader 切过 mux） |
| `cdiv` | `0x3` | 非 /1 |
| `fsrc` | `0x1` | — |
| `fdiv` | `0x1` | — |
| `dplle` | `0x1` | **DPLL 已 enable**（loader 预配，非本移植代码所写） |
| `vcre` | `0xB` | — |

- 此路径下 `sleep 10` 的 wall-clock **明显小于 4 s**（`sleep10<4s`）—— SysTick 仍按 26 MHz
  bookkeeping，但 CPU 实际频率更高，导致 sleep 提前完成。这是 D0D 要修的现象。
- **关键边界**：这些 DPLL/mux 写入**全部来自上位机 loader**（不是本移植 feature commit `6f596b7`
  的代码）；本移植**未写** DPLL enable 或 mux 切换。`6f596b7` 只读这些位并据此选择 SysTick
  bookkeeping。

## 3. J-Link DWT 独立测频（证明 loader 路径 ≈ 80 MHz）

为独立验证 loader 残留路径下 CPU 的真实频率（而非只信 readback），用 **J-Link + DWT CYCCNT**
计数：

- 配置：DWT `CYCCNT` 在已知 wall-clock 窗口（约 **2 s**）内自由计数。
- 读数：2 s 窗口内 `CYCCNT = 0x098AA02F`。
- 计算：`0x098AA02F = 159,930,415` cycles / 2 s ≈ **79,965,207.5 Hz ≈ 80.04 MHz**。
- 结论：loader `--reboot 1` 路径下 CPU0 真实频率约 **80 MHz**（非 26 MHz，非 480 MHz）；这与
  §2.2 的 `csrc=2 / cdiv=3 / dplle=1` 残留组合在量纲上一致，独立证实 readback 不是噪声。
- Manual-reset 路径（§2.1）下 DPLL 关闭、mux 在 XTALH，按同方法预期 ≈ 26 MHz；本次未对
  manual-reset 路径单独跑 DWT 计数（独立测量只覆盖 loader 路径）。

> 80 MHz ≠ 480 MHz，也不是 N4 的目标频率；它只是 loader 残留带来的副产物。N4-D1 将按 vendor
> sequence 主动 enable DPLL 并切 mux，由用户逐项授权后才进行。

## 4. D0D runtime fix（SysTick bookkeeping 适配 loader 路径）

针对 §2.2 的 sleep 提前现象，`6f596b7` 在 `bk7258_timerisr.c` 增加 runtime SysTick 频率选择：
**当检测到 loader 残留路径（`dplle=1` 且 mux 非 XTALH）时，把 SysTick bookkeeping 切到 80 MHz
档位**，使 `/proc/uptime` 与 `sleep` 重新对齐 wall-clock。硬件时钟源不动。

| 符号 | manual-reset（D0） | loader 残留（D0D） |
|---|---|---|
| `SYSTICK_RVR` | `0x0027AC3F` | `0x007A11FF` |
| `SYSTICK_EXP` | `0x0027AC3F` | `0x007A11FF` |
| `SYSTEM_CYCLES_PER_SEC`（HZ） | `0x018B2080`（26 MHz 档） | `0x04C4B400`（80 MHz 档） |
| `SYSTEM_CYCLES_PER_TICK` 选择 | XTALH 档 | `CLK=1`（80 MHz 档） |

> 注：表中 manual-reset 列的 HZ 值是按 readback 推导的 nominal 值；本 D0D commit 的实际运行配置
> 以源码 `6f596b7` 为准。

### 4.1 D0D 板端 wall-clock 回归（loader 残留路径）

D0D 修正后，在 loader `--reboot 1` 路径下复测：

- `sleep 10` wall-clock deltas：**10.10 s / 11.00 s**（两次），回到与 wall-clock 同量级
  （D0D 前 < 4 s）。
- `ps`：PID 0（IDLE）/ PID 1（nsh_main）正常。
- `/proc/version` 与 `uname -a` 在构建时间戳 **`Jul 18 2026 22:11:54`** 的镜像上 OK。

```
nsh> ps
PID 0   CPU0  IDLE      Kthread  Ready    ...
PID 1   CPU0  nsh_main  Task     Running  ...

nsh> cat /proc/version
NuttX version 0.0.0 ... Jul 18 2026 22:11:54 ...
```

- 这只覆盖 **D0D loader 残留路径** 的 wall-clock 回归；**不**等价于 DPLL enable / mux 切换被
  验证（那些动作未发生）。

## 5. Artifact provenance（候选镜像长度 + 哈希）

D0D 候选镜像（构建时间戳 `Jul 18 2026 22:11:54`，对应 §4.1 板端观察）：

| 产物 | 字节 | size（hex） | sha256 |
|---|---|---|---|
| `$FW/all-app.bin` | **164730 B** | `0x2837A` | `9e1d5f19b194c039521611ea495c7ba28a8c9fb90f027979d087d48b7b9b29b6` |
| `$FW/nuttx.bin` | **89500 B** | — | `c13745eb2c86b3426b4cd41d24a17663fc1b3755e96e92cc1204077f9640d999` |

- `all-app.bin` 较 N3 的 `163574 B (0x27EF6)` 增加，对应 clockdiag 头 + start/timerisr 修改。
- NuttX 把 `__DATE__`/`__TIME__` 烤进版本串，`/proc/version` 时间戳变化即板上跑该镜像的证据。

> **Caveat（exact-commit rebuild）**：上述 artifact 对应**当时构建的候选镜像**（功能 +
> wall-clock 回归已板上验证）。**最终以精确 commit `6f596b7` 重编 + 重刷一次的 state-C 复验尚未
> 完成**——这是 N4-D0 整体收口的剩余项；当前 D0/D0D 的 `board-verified` 以候选镜像证据为依据。
> 在完成 state-C 复验并确认 SHA-256 匹配前，不应将整 N4 标记为 board-verified。

## 6. 状态

| Substage | 状态 | 依据 |
|---|---|---|
| **N4-D0** manual-reset baseline | ✅ `board-verified`（substage） | §2.1 readback + sleep10≈10s |
| **N4-D0D** runtime SysTick bookkeeping（loader 路径） | ✅ `board-verified`（substage） | §3 DWT≈80 MHz + §4.1 sleep10 10.10/11.00 s + ps/version OK |
| **N4-D1** DPLL lock（CPU 保持 XTALH） | **blocked**（未执行） | 与 §2.1 manual-reset 路径冲突：当前 loader 路径已 `dplle=1` 残留，无法干净区分“本移植写的 DPLL enable”与“loader 残留”；需先在 manual-reset 冷启动路径上重做测量并固定 baseline，再申请 D1 mutation 授权 |
| DPLL enable / CPU mux 切换 | **not attempted** | 本 commit `6f596b7` 不写这些寄存器 |
| **整 N4**（D0+D0D+D1+D2+D3+V） | **not board-verified** | D1 blocked，D2/D3/V 未开始 |

`board-verified`（N2/N3 全 stage）与 substage `board-verified`（N4-D0/D0D）严格区分：后者只覆盖
subsection 范围，不向上传染到整个 N4。

## 7. 下一步（不预分配编号、不预授权）

- 完成 N4-D0 的 **state-C 精确 commit 复验**（重编 `6f596b7` + 重刷 + 重新计算长度/哈希），把
  D0/D0D 收口到"verified == 提交源码镜像"。
- 解 N4-D1 blocker：在 manual-reset 冷启动路径上重做 baseline 测量，确认 DPLL/mux 全 0 的干净
  起点，再按 vendor sequence 申请 D1 mutation 授权（逐项 gate）。
- DPLL enable、CPU mux 切换、320/480 MHz 目标**均属 N4-D1/D2/D3 范围**，本 worklog 不构成对其
  的授权。

## 8. 参考

- 主 Stage 索引 / current handoff：[`../next-stage-prompt.md`](../next-stage-prompt.md)
- N4 完整恢复提示词：[`prompts/04-n4-clock-bringup.md`](prompts/04-n4-clock-bringup.md)（N4-D0
  见 §2，N4-D1 见 §3）
- N3 baseline worklog：[`n3-procfs-ps.md`](n3-procfs-ps.md)
- 详细移植报告 §9.6（N4 handoff）：[`../porting-report.md`](../porting-report.md)
