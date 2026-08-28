# NuttX Stage N4-D0 / D0D — 时钟诊断 baseline + runtime SysTick bookkeeping（worklog）

> 历史说明：本文记录早期 raw source/divider 探测，尚未完整解释 CPU0 speed 位；其中
> “320/480 MHz CPU0”结论不再是当前产品契约。固定 SDK v3.1.1.9 的正式映射为
> OPP 320M = CPU0/AP/Bus 160/320/160 MHz，OPP 480M = 240/480/240 MHz。
> 当前实现与验证规则见
> [`../../chips/bk7258/sdk-clock-operating-points.md`](../../../chips/bk7258/sdk-clock-operating-points.md)。

> 板端验证日期：2026-07-18（substage `board-verified`：仅 N4-D0/D0D；N4-D1 及整 N4 尚未板端验证）
> 基线 commit：`4d9198e`（Stage N3 code）→ D0/D0D feature commit：`6f596b7`（3 个 overlay 文件，只读诊断 + runtime SysTick 修正）
> → D0F feature commit：`8dab594`（defconfig 移除 100ms override，生效默认 10ms/100Hz tick）
> 改动范围：D0/D0D — `$CONTEST/board/bk7258/chip/`（`chip/common/bk7258_clockdiag.h` 新增、`chip/cp/bk7258_start.c` 接入、`chip/common/bk7258_timerisr.c` runtime SysTick 选择）；D0F — `$CONTEST/board/bk7258/configs/cp_nsh/defconfig`

> 占位符：`$WORKSPACE`、`$CONTEST`、`$FW` 与 README / next-stage-prompt.md 一致。本 docs 提交 SHA
> 不在本文内写出（见 [`../next-stage-prompt.md`](../next-stage-prompt.md) 命名与维护规则）。

## 1. Scope（与 N4 prompt §2 对齐）

本 worklog 记录 Stage N4 内**第一个 subsection** N4-D0 的执行证据与紧随其后的 runtime SysTick
bookkeeping 修正（记为 N4-D0D），**不是** Stage N4 整体 board-verified。

- **Feature commit**：`6f596b7`，只改 3 个 overlay 文件（`board/bk7258/chip/`）。
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

### 3.1 CP startup attribution（面向小白：loader 路径为什么快、D0D 为什么只修 bookkeeping、D1 为什么 blocked）

**问：为什么 `bk_loader --reboot 1` 后时间变快（`sleep 10` 不到 4 秒）？**

loader 的软复位（`--reboot 1`）把 NuttX 交接到一个**已初始化的时钟状态**，该状态与 Beken SDK CP
早期初始化残留一致（SDK 参考路径：`Reset_Handler_Cpu0` → `sys_drv_early_init()` →
`sys_hal_early_init()`）。但当前 NuttX overlay **不移植也不调用** `sys_drv_early_init` 或
`sys_hal_early_init`；它只**读取继承的寄存器状态**（M1=0x423、M2=0x05000000、dplle=1、csrc=2、
cdiv=3 等），并据此补偿 SysTick bookkeeping。

结果：CPU 实际频率从 26 MHz 变成了约 80 MHz（J-Link DWT 实测），但 NuttX 的 SysTick bookkeeping
仍按 26 MHz 计数，导致 `sleep 10` 的 wall-clock 只有实际的约 1/3（不到 4 秒）。

> 注意：80 MHz 是 J-Link DWT CYCCNT 在 2 秒窗口内的**独立测量值**（§3），不是从 SDK
> `sys_hal_early_init` 片段直接公式推导出来的。SDK 的 `sys_hal_early_init` 包含模拟域批量配置和
> mux/divider 副作用，没有单一的"频率公式"可以从可见片段算出最终频率。

**问：为什么 D0D 只修 SysTick bookkeeping，不动硬件时钟源？**

`6f596b7` 是**只读诊断 + bookkeeping 修正** commit。它读取 DPLL/mux/divider 寄存器，检测到 loader
残留路径（`dplle=1` 且 mux 非 XTALH），然后把 SysTick 的 `RVR`、`EXP`、`SYSTEM_CYCLES_PER_SEC`
和 `CLK` 选择切到 80 MHz 档位。硬件时钟源（DPLL、mux、divider）**不写**——这属于 N4-D1 的范围。

这样做是因为：bookkeeping 修正风险极低（只改软件变量），而 DPLL/mux 写入属于硬件 mutation，
需要在干净的 baseline（manual-reset 冷启动）上逐项授权后才能进行。

**问：为什么不能把 `sys_hal_early_init` 直接照搬成 N4-D1？**

`sys_hal_early_init` 是 CP SDK 的启动路径，它不是 NuttX 的 lock-only 时钟序列。它的副作用包括：

- 批量写模拟域寄存器（analog batch writes）
- 切换 mux/divider（side effects on clock domain）
- **没有明确的 DPLL locked bit 正检查**（positive locked-bit assertion）

把这段代码直接搬进 N4-D1 意味着：(1) 无法区分"本移植写的 DPLL enable"与"loader 已有残留"；
(2) 模拟域副作用可能在 manual-reset 冷启动路径上产生未预期行为；(3) 没有 locked-bit 断言，
无法确认 DPLL 已稳定锁定。

因此 N4-D1 需要：先在 manual-reset 冷启动路径上建立干净 baseline（DPLL 全关），再按 vendor
文档的 lock-only sequence 逐项 enable DPLL 并检查 locked bit，最后切 mux。这是独立于 SDK
`sys_hal_early_init` 的受控路径。

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

## 6. D0F — 100Hz SysTick tick-rate 兼容性（`8dab594`）

### 6.1 问题与动机

SysTick 是 24-bit 递减计数器（`RVR` 最大 `0x00FFFFFF`）。D0/D0D 阶段使用旧的默认
`CONFIG_USEC_PER_TICK=100000`（10ms/100Hz），实际是 100ms/10Hz override。在当前 26 MHz /
80 MHz 路径下此值虽可用，但**未来 480 MHz 目标频率**下 10Hz 对应的 reload 值为
`480000000/10-1 = 0x02DC6BFF`，**超过 24-bit 最大值** `0x00FFFFFF`，将导致 SysTick
overflow。因此必须在 N4 进入更高频率前将 tick 率提升到安全范围。

### 6.2 实际 patch

feature commit `8dab594` 的改动是**删除 defconfig 中的 `CONFIG_USEC_PER_TICK=100000` 行**
（100ms override），savedefconfig 同时删除与默认值相同的 `CONFIG_USEC_PER_TICK=10000` 行
（10ms default）。生效配置为 **`CONFIG_USEC_PER_TICK=10000`（10ms / 100Hz）**。

> 注意：defconfig 中**没有显式添加** `CONFIG_USEC_PER_TICK=10000`——该值是 NuttX Kconfig
> default，删除 override 后自动生效。文档表述为 "removed 100ms override; effective default
> 10ms/100Hz"。

480 MHz 安全性验证：`480000000/100-1 = 0x00493DFF`，小于 `0x00FFFFFF`，24-bit 不溢出。

### 6.3 1000Hz 尝试（rejected）

`CONFIG_USEC_PER_TICK=1000`（1ms/1000Hz）曾 build + flash 测试：

- Loader 80 MHz 路径基本可用（`RVR=0x0001387F`）。
- **Manual-reset 26 MHz 路径失败**：SysTick dump 中途 / 第二次进入时出现失败重启。
- 结论：**rejected/skipped**，不提交。100Hz 足够安全且两条路径均稳定。

### 6.4 100Hz 板端证据

#### Loader `--reboot 1` / 80 MHz 路径

```
CSR=00000007  RVR=000c34ff  EXP=000c34ff  HZ=04c4b400  CLK=1
/proc/uptime 73.65 → sleep 10 → 83.72 (delta 10.07s)
/proc/uptime 30.05 → sleep 10 → 40.12 (delta 10.07s)
```

- `RVR=0x000C34FF` = `80000000/100-1` = 799999，与 80 MHz/100Hz 一致。

#### Manual-reset / 26 MHz 路径

```
CSR=00000007  RVR=0003f79f  EXP=0003f79f  HZ=018cba80  CLK=0
/proc/uptime 2.03 → sleep 10 → 12.25 (delta 10.22s)
/proc/uptime 2.36 → sleep 10 → 12.57 (delta 10.21s)
```

- `RVR=0x0003F79F` = `26000000/100-1` = 259999，与 26 MHz/100Hz 一致。
- 两条路径的 wall-clock delta 均在 10.0–10.3 s 范围，tick 率正确。

### 6.5 D0F artifact

D0F 镜像（在 D0/D0D 基础上 + defconfig tick 修改）：

| 产物 | 字节 | size（hex） | sha256 |
|---|---|---|---|
| `$FW/all-app.bin` | **164730 B** | `0x2837A` | `c3ca4ae21c4b2c7617bba7c430c7910ce049044d569dbffded0f17f0d8f422eb` |
| `$FW/nuttx.bin` | **89504 B** | — | `e4269b7a8fccc0d6048d9fbe0c4021fd34d39b236601f3873391c9a399d255e7` |

> D0F artifact 大小与 D0/D0D 候选相同（`all-app.bin` 164730 B），因为 defconfig 删行不改变
> 代码段；SHA-256 不同反映构建时间戳变化。

### 6.6 D0F 状态边界

- **D0F `board-verified`（substage）**：两条启动路径（loader 80 MHz / manual-reset 26 MHz）
  均已验证 100Hz tick wall-clock 正确。
- **不改变 N4 整体状态**：D0F 是 D0/D0D 之后的兼容性修正，不涉及 DPLL enable / mux 切换；
  N4-D1 仍 blocked（原因同 §6），整 N4 仍 not board-verified。

## 7. 状态

| Substage | 状态 | 依据 |
|---|---|---|
| **N4-D0** manual-reset baseline | ✅ `board-verified`（substage） | §2.1 readback + sleep10≈10s |
| **N4-D0D** runtime SysTick bookkeeping（loader 路径） | ✅ `board-verified`（substage） | §3 DWT≈80 MHz + §4.1 sleep10 10.10/11.00 s + ps/version OK |
| **N4-D0F** 100Hz tick-rate 兼容性 | ✅ `board-verified`（substage） | §6.4 两条路径 wall-clock delta 10.0–10.3 s；480 MHz reload 0x00493DFF < 0x00FFFFFF；1000Hz rejected |
| **N4-D1** DPLL lock / cold-enable | **blocked** | cold-start BootROM 留 `EN_DPLL=0`；本移植不复刻 SDK 完整 ANA_REG cold-enable 序列（`ANA_REG0/2/3` bias/softstart + `chip_id` 分支未在本板验证），按 RESET 键回退 26 MHz baseline。软复位 loader-residue 路径已 deterministic 切到 320 MHz（见 D1.5） |
| DPLL enable / CPU mux 切换 | **partially board-verified**（substage） | loader-residue 路径：`bk7258_clock_bringup_320m()` 已实现并 board-verified（320 MHz，`M1=0x20`，`csrc=2`, `cdiv=0`，DPLL 已开前提）。cold-enable 未做（D1 blocker） |
| **N4-D1.5** loader-path 320M bring-up（DPLL 已开前提下） | ✅ `board-verified`（substage） | 板端验证 2026-07-19：软复位 `M1=0x00000420` / `A5=0x8407876c(b5=1)` / `A9=0x787cc8a4` / `DXPLL=1` / `SW=1` → 320 MHz 进 NSH + LFS OK；冷启动 `M1=0x00000000` / `A5=0x8407a340(b5=0)` / `A9=0x787ac0a4` / `DXPLL=0` / `SW=0` → 26 MHz baseline 进 NSH + LFS OK；`all-app.bin=189566B=0x2E57E` |
| **整 N4**（D0+D0D+D0F+D1+D1.5+D2+D3+V） | **not board-verified** | D1 cold-enable blocker 未解，480M unreachable。note：deterministic 320M on loader path + runtime SysTick 自适应（D1.5 substage）board-verified |

`board-verified`（N2/N3 全 stage）与 substage `board-verified`（N4-D0/D0D）严格区分：后者只覆盖
subsection 范围，不向上传染到整个 N4。

## 8. 下一步（不预分配编号、不预授权）

- **DPLL cold-enable 序列**：需复刻 SDK `sys_hal_early_init` 完整 analog 配置（`ANA_REG0/2/3` +
  bias/softstart + `chip_id` 分支）并在本板独立验证。本移植未做；按 RESET 键回退 26 MHz baseline。
- **480 MHz unreachable**（SDK guard，§10.2），不追求。
- 其余 N4 子项（独立测频回归等）可选。

## 10. 频率阶梯证据与 SDK guard 分析（loader-residue mux/div probes）

N4-D0/D0D 确认 loader 残留路径约 80 MHz 后，进一步对 loader 预配的各频率档位进行 mux/div
组合探测。以下是**全部 loader-residue 探测结果**（NuttX 未主动写 DPLL/mux/div 寄存器，只读取
loader 已配置的残留并测量）：

### 10.1 频率阶梯表

| 目标 | M1 | csrc | cdiv | RVR | HZ | sleep10 | J-Link CYCCNT 2s | 实测频率 | 状态 |
|---|---|---|---|---|---|---|---|---|---|
| 80 MHz（loader residue） | `0x423` | 2 | 3 | — | — | < 4 s | `0x098AA02F` | ≈ 80.04 MHz | ✅ D0/D0D verified |
| 120 MHz | `0x433` | 3 | 3 | `0x00124F7F` | `0x07270E00` | ≈ 10 s | `0x0E503CC6` | ≈ 120.07 MHz | ✅ board-verified |
| 160 MHz | `0x432` | 3 | 2 | `0x001869FF` | `0x09896800` | ≈ 10 s | `0x1315D658` | ≈ 160.10 MHz | ✅ board-verified |
| 240 MHz | `0x431` | 3 | 1 | `0x00249EFF` | `0x0E4E1C00` | ≈ 10 s | `0x1C9F6A83` | ≈ 240.10 MHz | ✅ board-verified |
| 320 MHz | `0x420` | 2 | 0 | `0x0030D3FF` | `0x1312D000` | ≈ 10 s | `0x262A8696` | ≈ 320.16 MHz | ✅ board-verified（最高） |
| 480 MHz direct | `0x430` | 3 | 0 | — | — | — | — | — | ❌ failed / stalled |

- M1 低 bits 的编码：`csrc` 和 `cdiv` 直接对应寄存器 bit field；`csrc=2` = 320M 源，
  `csrc=3` = 480M（DPLL）源；`cdiv` = 分频系数（0 = /1，1 = /2，2 = /3，3 = /4）。
- 80 MHz → 120 MHz → 160 MHz → 240 MHz → 320 MHz 均在 J-Link DWT CYCCNT 独立测量下确认，
  wall-clock `sleep 10` 均约 10 s，SysTick bookkeeping 已适配。
- **480 MHz direct（M1=0x430, csrc=3, cdiv=0）失败**：板端在 `N4D0:480S` 后 stall，
  未到达 `480M` readback / NSH 提示符。

### 10.2 SDK guard 证据勘误（2026-08-27）

本节早期记录曾误写为“SDK 明确拒绝 480M/1”。固定 v3.1.1.9 SDK 中不存在该
guard；`sys_hal_core_bus_clock_ctrl()` 的真实限制是：当 core source 为 320M 且
`clkdiv_core=0` 时，CPU0 必须使用 `cpu0_speed=0`（`/2`）：

```c
if ((cksel_core == PM_CLKSEL_CORE_320M) &&
    (ckdiv_core == PM_CLKDIV_CORE_0) &&
    (ckdiv_cpu0 != PM_CLKDV_CPU0_0))
  {
    return BK_FAIL;
  }
```

因此正式 OPP 的结论是：

- OPP 320M 调用 `(2, 0, 0, 0, 1)`，CPU0/AP/Bus=`160/320/160 MHz`；
- OPP 480M 调用 `(3, 0, 0, 0, 1)`，CPU0/AP/Bus=`240/480/240 MHz`；
- 历史 raw 320M `/1` 与 raw 480M `/1` 都绕过了正式 OPP 的 CPU0 分频/电压契约。
  前者的探针测量成功不代表是产品支持档，后者 stall 也不代表 SDK 拒绝正式
  OPP 480M。

### 10.3 结论

- **480M 源存在，但 raw CPU0 480M `/1` 不属于 SDK 的任何正式 OPP**；板端
  stall 只否定这条绕过 CPU0 `/2` 的历史实验。
- 历史最高 J-Link 测量值 320 MHz 来自 raw 320M `/1`，同样不是正式 OPP 320M。
- 固定 SDK 的 CPU0 正式上限是 240 MHz；AP 在 OPP 480M 的正式上限是 480 MHz。
- **全冷启动 DPLL enable 仍 blocked**：NuttX 未主动 enable DPLL；上述频率阶梯均为 loader-residue
  mux/div 探测，依赖 loader 已预配的 DPLL 状态。
- AP 480 MHz 必须通过官方 OPP 480M vote 获得，不能直接改 CPU0 divider。

### 10.4 320 MHz deterministic bring-up（历史实现，已被 SDK OPP 适配取代）

以下内容保留 2026-07-19 的 raw loader-path 实验事实，不能作为当前产品时钟
配置说明。现行实现见 [SDK OPP 契约](../../../chips/bk7258/sdk-clock-operating-points.md)：CP 性能档为
OPP 240M/CPU0 240 MHz，AP 的 320/480 MHz 通过共享 PM vote 获取。

**新增文件**（`$CONTEST/board/bk7258/chip/`）：

- `bk7258_clock.c` + `bk7258_clock.h`：`bk7258_clock_bringup_320m()` 实现，gated by `CONFIG_BK7258_CLOCK_320M`。
- `bk7258_start.c`：在 `arm_earlyserialinit()` 之后、`nx_start()` 之前调用 `bk7258_clock_bringup_320m()`。

**bring-up 函数行为**（镜像 Armino SDK 早期 init 时钟路径）：

- 仅当 DPLL 已使能（`ENA_REG5.EN_DPLL=1`，loader residue）时执行：
  1. 重跑 SDK SPI 重校准；
  2. 拉 `VDDDIG→0xC`（0.9V）/ `VDDD→0x6`（1.0V）（`ANA_REG9`，`spi_latch1v` 门控）；
  3. 切 core mux：`cpu0_speed=0→ckdiv_core=0→cksel_core=2`，M1 低 6 位 = `0x20`。
- 当 DPLL 未使能（`EN_DPLL=0`，cold-start / 按 RESET 键）时：跳过全部操作，保持 XTAL 26 MHz baseline 安全进 NSH。

**板端两路径验证**（probe 行，测后已关 `CONFIG_BK7258_CLOCK_320M_PROBE`）：

- 软复位（`u_bootloader enter`）：`N4Clk M1=00000420 A5=8407876c(b5=1) A9=787cc8a4 DXPLL=1 SW=1` → 320 MHz 进 NSH，`cat /data/probe.txt=BK7258LFS-OK`
- 冷启动（按 RESET 键）：`N4Clk M1=00000000 A5=8407a340(b5=0) A9=787ac0a4 DXPLL=0 SW=0` → 26 MHz baseline 进 NSH，`cat /data/probe.txt=BK7258LFS-OK`
- 两路径 `sleep10` 均约 10 s（SysTick runtime 检测 `bk7258_clockdiag_current_cpu_hz` 自适应 26/320M）。
- `all-app.bin = 189566 B = 0x2E57E`（< `0x100000`）。

**cold-enable blocker（诚实声明）**：

冷启动 BootROM 留 `EN_DPLL=0`。SDK cold-enable 需完整 `ANA_REG0/2/3` bias + softstart + `chip_id`
分支序列，未在本板复刻/验证。初次尝试写 DPLL cold-enable（`ANA_REG` 复杂序列）导致按 RESET 键
stall；已 fallback 为"仅 DPLL 已开才切 320M"，cold-enable 留作 N4-D1 blocker。

**fallback 决策**：仅在 loader-residue 路径（DPLL 已开）下 deterministic 切到 320 MHz；
cold-start 保持 26 MHz baseline 安全进 NSH。板端验证 2026-07-19。

## 11. 参考

- 主 Stage 索引 / current handoff：[`../next-stage-prompt.md`](../next-stage-prompt.md)
- N4 完整恢复提示词：[`prompts/04-n4-clock-bringup.md`](prompts/04-n4-clock-bringup.md)（N4-D0
  见 §2，N4-D1 见 §3）
- N3 baseline worklog：[`n3-procfs-ps.md`](n3-procfs-ps.md)
- 详细移植报告 §9.6（N4 handoff）：[`../porting-report.md`](../porting-report.md)
