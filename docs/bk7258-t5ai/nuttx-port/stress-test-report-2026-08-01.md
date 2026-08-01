# BK7258 全功能连续压力测试报告

日期：2026-08-01
执行环境：

- 固件基线：`ecea356`（`feat/bk7258-ap-smp`）之上的当前 wrapper 修复工作区，AP 启动代际 `generation=1` 起
- 板级连接：COM11（460800 8N1）＋ Windows interop（powershell.exe）＋ `capture_windows_serial.ps1`
- 测试驱动：`board/bk7258_t5ai/scripts/bk7258_stress_test.sh`
- 原始日志：`logs/stress-20260801-102037/`（首轮）；人工重判复核：`logs/stress-20260801-142356/`（见 §7）；脚本修复后自动判读闭环：`logs/stress-20260801-155637/`（见 §8）

> **2026-08-01 勘误：**本报告初版把 RPMsg 的 `ENOMEM` 归因于 vring
> 通知丢失。随后加入 AP heap 与 pthread 创建阶段诊断并在实板重跑，已确认根因是
> 测试 wrapper 每轮退出的 CPU1 pthread 栈/TCB 未回收。RPTUN 在失败后仍
> `CONNECTED` 且 pending 为 0。下文已按实测结果修正。

## 1. 范围与策略

按用户决策：

1. 初始非 RPMsg soak 暂不运行 `bkrpmsgtest`；随后另开带诊断的 RPMsg 专项复现与修复回归，结果见 §3.5 和 §4。
2. **连续跑、记录 OOM 点**——各阶段之间不插冷复位，暴露长时间运行的资源泄漏/退化。

覆盖除 RPMsg 外的全部板级功能：AP SMP 调度、CPU2 在线、亲和性、信号量唤醒（sem-wake/sem-loop）、调度器生命周期（BLCY）、BP2P/BDUL/BMIG/BTIM、IPI、SDK MBOX0、GPIO/IRQ。

## 2. 结果总览

| 阶段 | 内容 | 迭代 | 结果 |
|---|---|---:|---|
| 0 | 基线 `apctl status` | 1 | 全部 `PASSED` / `RPTUN CONNECTED(4)` / `flags=0x3fff` |
| A | `apctl cycle` AP 生命周期 | 20 代际 | **PASS**（20/20 `READY`，各子系统 `PASSED`，0 真失败） |
| B | IPI 独立 `ipitest` | `apctl ipitest 1000 3000` ×5（online 模式） | **INFO / 非压力向量**：全部返回 `ipitest is disabled while AP scheduler-online mode`（ENOTSUP）。IPI 实际仅由 Phase A/F 的 `cycle` 启动自检覆盖 |
| C | Mailbox（SDK MBOX0） | `apctl mbox 1000 1000` ×5 | **PASS**（累计 5000 条，`MBOX probe passed`，0 耗尽） |
| D | GPIO/IRQ | `bkgpioc0` / `bkgpioirq` / `bkirqtest` | **跳过（交互式）**：`bkgpioc0`/`bkgpioirq` 需在 P29 手动按 USERKEY（见源码 `bk7258_gpio_*.c` 注释），属人工交互测试，不纳入无人值守压测；`bkirqtest` 为定时器 IRQ 可自动化但未编入任何 defconfig。按决策跳过 |
| E | 持续状态轮询 | 10 × ~4s | **稳定**（heartbeat 3370→4378 递增，0 `error=-`） |
| F | 收尾 `apctl cycle` | 10 代际 | **PASS**（10/10 `READY`，0 真失败） |

> 说明：驱动脚本首版把状态行中的 `fail=0/0`、`fail0=0->0` 计数器误判为失败（日志里 `fails=126`/`bad=6`/`err=6` 均为假阳性）。本报告按**真失败标记**（`FAILED`/`error=-[1-9]`/`fault exception`/`transport=-`）重新判定，结论为上述 PASS。

## 3. 关键发现

### 3.1 非 RPMsg 功能完全稳定
在约 **14 分钟**连续压力下（30 次 AP start/stop 代际、5000 条 MBOX0 消息、持续状态轮询），所有被覆盖子系统均无 OOM、无退化、无错误。AP 启动代际从 `generation=1` 推进到 `generation=22+`，各子系统状态始终保持 `PASSED`。

### 3.2 Mailbox 本身不泄漏，但不能据此定位 RPMsg `ENOMEM`

SDK MBOX0 探针连续收发 5000 条消息无任何耗尽/失败，证明该 probe 场景稳定。
它不能证明或反证 OpenAMP vring descriptor 是否滞留；`apctl mbox` 是独立的同步
probe 协议，不是 virtqueue 状态观测器。后续 heap/spawn 诊断已经直接定位到 CPU1
`pthread_create()`，因此不再把这组 mailbox 结果当作 vring 根因证据。

### 3.3 IPI 压测只能经由 `apctl cycle` 启动自检（独立 `ipitest` 在本固件是空操作）
`apctl ipitest` 在本固件**不是可用的 IPI 压力向量**，已实测验证（2026-08-01）：

- **AP scheduler-online 模式**（标准连续流程所处状态）：返回
  `apctl: ipitest is disabled while AP scheduler-online mode is active`（设计如此，ENOTSUP），压测循环不执行。
- **AP 已 `stop` 后**：不再报 ENOTSUP，但仅打印 IPI 状态行，`requested/completed/runs=0`、各方向 `tx/rx/pending=0/0/0` —— 压测循环同样不执行。

因此 IPI 压力**仅由 `apctl cycle` 每代的启动自检覆盖**（BP2P/BDUL/BMIG/BTIM 及 IPI 自检，对应 `AP SMP`/`affinity`/`sem-wake`/`sem-loop`/`BLCY` 状态均 `PASSED`）。标准连续 SOP 中**不要**把独立 `apctl ipitest` 当作 IPI 压测项。

### 3.4 GPIO/IRQ 测试应用全部未编入本镜像
`bkgpioc0` / `bkgpioirq` 两个命令在 NSH 均返回 `command not found`。这三项其实是 `app/hello_app` 中的工程测试程序，分别由 `CONFIG_BK7258_GPIO_FOUNDATION_TEST` / `GPIO_IRQ_TEST` / `SDK_IRQ_TIMER_TEST` 三个开关门控；经核对**所有已提交 defconfig（含本镜像所用）都未开启这三项**，当前 `.config` 也如此。但更关键的是：**`bkgpioc0` 与 `bkgpioirq` 是人工交互测试**——源码注释明确 "requires USERKEY to remain on P29"，需在 P29 物理按键、并目视 P9 LED 脉冲才能跑完。因此它们**不适合无人值守的连续压测**，按决策直接跳过（即便开启 CONFIG 重编，仍需手动按键，无法自动化）。`bkirqtest` 是定时器 IRQ（可自动化），但同样未编入任何 defconfig，故一并跳过。当前镜像里唯一与 GPIO 相关的 NSH 命令是 NuttX 通用 `gpio`（`CONFIG_EXAMPLES_GPIO=y`），它是低层 GPIO 工具、不是本工程的 IRQ 压测。

### 3.5 RPMsg 专项复核

诊断版 result ABI 增加了三处 `mallinfo()` 快照以及 worker 创建 target/stage：

- 每个成功 run 后 `allocated_bytes` 固定增加 `4360`，`allocated_blocks` 固定增加 2；payload、消息 count、idle/load 均不影响斜率。
- `run=53` 的起始已用量精确满足 `26616 + 52 × 4360 = 253336`，与实测 `heap_start used=253336` 完全一致。
- `run=53` 首次失败为 `SPAWN target=3 stage=2 status=-12`，即 CPU1 worker 的 `pthread_create()` 失败；CPU0 worker 已完成，`workers_expected=1 workers_done=1`。
- 当时 `heap_start free=11896 largest=7448`。失败后 `AP READY`、`RPTUN CONNECTED`、`pending cp/ap=0/0`，所以不是 vring/通知链路卡死。
- 动态线程时序也与 heap 一致：CPU0 worker 在下一次快照前已释放；CPU1 detached worker 每轮残留一个 4096 B 栈加管理开销，共 4360 B。

修复只修改板级 wrapper：在 AP 初始化时一次性创建 CPU0、CPU1 与 load 常驻线程，每轮通过信号量 dispatch，不修改 NuttX 或 SDK 源码。

## 4. RPMsg 修复前后对照

| 项目 | 修复前 | 常驻 worker 修复后 |
|---|---|---|
| 首次失败 | `run=53`, CPU1 `pthread_create=-ENOMEM` | 运行至 `run=80`，0 FAIL |
| 每 run heap | `used +4360 B`, allocated blocks `+2` | 首尾完全不变 |
| 稳态 heap | 持续下降至 `free=11896`, largest=7448 | `used=39696`, `free=225488`, largest=221816, allocated blocks=48 |
| 原始矩阵 | 第 53 次附近失败 | `bkrpmsgtest all 100 60000` 六场景全部 PASS |
| 失败后链路 | AP READY / RPTUN CONNECTED / pending 0/0 | 始终 CONNECTED |
| 生命周期 | 未形成有效反证 | `apctl cycle 3` 后 generation 5 的 `run=80` PASS，heap不变 |
| 物理冷启动 | 未验证修复版 | COM7 RTS 得到 `cold_path=yes`；generation 1 状态健康，`run=1` PASS，heap不变 |

原始证据位于工作区 `logs/bkrpmsgtest-diagnostic-20260801/`；修复后证据位于
`logs/bkrpmsgtest-fixed-20260801/`。物理冷启动路径本身的证据位于
`logs/bk7258-auto-debug/20260801-131328/summary.txt`（`cold_path=yes`），随后
generation 1 的状态与 RPMsg 回归位于
`logs/bkrpmsgtest-fixed-20260801/physical-cold-status-test.raw`。这些板端日志没有纳入 Git。

## 5. 结论

BK7258 当前固件中**所有可被无人值守压测覆盖的非 RPMsg 功能（AP SMP 生命周期、CPU2 在线、亲和性、信号量唤醒、BLCY、IPI 启动自检、SDK MBOX0）在连续压力下稳定可靠**。RPMsg 原 `ENOMEM` 也已通过直接 heap/spawn 证据定位，并在不改 NuttX/SDK 的前提下由常驻 worker 修复，越过原失效阈值且通过真实消息矩阵与 AP generation 重启回归。GPIO/IRQ 人工项仍按原范围说明未自动覆盖。

## 6. 产物

- SOP：`docs/bk7258-t5ai/nuttx-port/bk7258-stress-test-sop.md`
- 驱动脚本：`board/bk7258_t5ai/scripts/bk7258_stress_test.sh`
- 原始日志：`logs/stress-20260801-102037/`（p0/pA…pF 各阶段 `.raw` + `stress-master-summary.txt`）
- 本次复核运行原始日志：`logs/stress-20260801-142356/`（p0/pA…pF 各阶段 `.raw` + `stress-master-summary.txt`）
- 修复后自动判读闭环：`logs/stress-20260801-155637/`（28 个 `.raw` + `stress-master-summary.txt`，最终 `verdict=PASS`）
- RPMsg 修复前诊断：工作区 `logs/bkrpmsgtest-diagnostic-20260801/`
- RPMsg 修复后回归：工作区 `logs/bkrpmsgtest-fixed-20260801/`
- 物理冷启动路径：工作区 `logs/bk7258-auto-debug/20260801-131328/summary.txt`（`cold_path=yes`）
- 实板修复版 raw CP/AP SHA-256：`a377132e1cc81cf6434d8b741b7841d173658edc10cda5d3a76641ca75027be4` / `5b6e71a157fbfabde3dc42499a4fce543a5b6c0b436edbebc9aba811e38071f1`

## 7. 运行记录：2026-08-01 14:23 连续压测（默认 SOP，RPMsg 排除）

本次在修复后镜像上独立复核运行，驱动脚本 `bk7258_stress_test.sh`，记录目录
`logs/stress-20260801-142356/`（COM11 460800，各阶段之间无冷复位）。结果按原始
`.raw` **大小写敏感重判**后如下：

| 阶段 | 迭代 | 真实结果（raw 大小写敏感重判） |
|---|---:|---|
| 0 基线 `apctl status` | 1 | `AP READY` / `RPTUN CONNECTED(4)` / `flags=0x3fff`，各子系统 `PASSED` |
| A `apctl cycle 20` | 20 代际 | 20/20 `READY`，子系统 `PASSED` 累计 105 次，0 真失败 |
| B `apctl ipitest 1000 3000` | 1 | 仅 INFO：返回 `ipitest is disabled while AP scheduler-online mode`；IPI 由 cycle 自检覆盖，不计分 |
| C `apctl mbox 1000 1000` ×5 | 5000 条 | `MBOX probe passed` ×5（每轮 1000 条），0 真失败 |
| D `bkgpioc0`/`bkgpioirq`/`bkirqtest` | — | 跳过（`UNAVAILABLE`：`CONFIG_BK7258_*` 未编入） |
| E 持续 `apctl status` 轮询 | 10 × ~4s | heartbeat 2594→3600 稳定递增，0 真 `error=-` |
| F `apctl cycle 10` | 10 代际 | 10/10 `READY`，0 真失败 |

**驱动脚本判读偏差（本次运行暴露并修复）**：本运行首跑产出的 `stress-master-summary.txt`
把 Phase A/C/E/F 误报为 `CHECK/FAIL/DEGRADED`。根因是脚本 `genuine_fail()` 当时用
`grep -ciE`（大小写不敏感），把状态行零值计数器 `dup/lost/fail=0/0/0`、`fail0=0->0`、
`fail=0/0` 的小写 `fail` 匹配进了 `[^A-Za-z]FAIL[^A-Za-z]`。对 `.raw` 用大小写敏感正则
（`grep -E`，无 `-i`）重判后，各阶段均为 0 真失败、全部 PASS/OK。该 bug 已在
`bk7258_stress_test.sh` 修复。最终实现不再简单地把 `grep -ciE` 改成 `grep -cE`：
`FAIL`/`FAILED` 与负 `error` 使用大小写敏感规则，崩溃文本则单独做大小写折叠匹配，
因此既不会误报小写 `fail=0`，也不会漏掉行首 `FAIL`、`PANIC`、`ASSERT`、`HardFault`。
离线回归已覆盖本轮全部 24 个 `.raw`、零值计数器样本、13 种故障大小写/行首样本及
历史真实 `BRPT FAIL`；修复后的完整实板自动判读记录见 §8。

结论与 §2 一致：非 RPMsg 功能在约 14 分钟连续压力（30 次 AP 代际、5000 条 MBOX0 消息、
10 次状态轮询）下稳定；本轮同时确认了压测脚本自身的假阳性判读 bug 并完成了修复。

## 8. 运行记录：2026-08-01 15:56 修复后自动判读闭环（默认 SOP，RPMsg 排除）

最终验收记录位于 `logs/stress-20260801-155637/`。本轮直接使用修复后的驱动脚本运行，
未对 SUMMARY 做任何人工改写或重判，脚本以退出码 0 结束并输出：

```text
STRESS DONE verdict=PASS logdir=/home/lijian/project/open-vela/logs/stress-20260801-155637
```

| 阶段 | 自动判读结果 |
|---|---|
| 0 基线 | 检测到上一轮 `cycle` 留下的 `STOPPED/QUIESCING` 后，自动执行 `apctl start`；独立状态复核为 generation 64、AP `READY`、RPTUN `CONNECTED(4)`、`flags=0x3fff`，PASS |
| A 生命周期 | 20/20 `READY`、子系统 `PASSED` 105 次、`genuine_fails=0`，PASS |
| B IPI 信息项 | 按设计返回 scheduler-online 模式禁用信息，不计分 |
| C MBOX0 | `MBOX probe passed` 5/5、累计 5000 条、每轮 `genuine_fails=0`，PASS |
| D GPIO/IRQ | 三个交互式/未编入命令均为 `UNAVAILABLE`，按既定范围跳过 |
| E 状态轮询 | 10/10 `OK`，heartbeat 2596→3605 单调递增，全部 `genuine_err=0` |
| F 收尾生命周期 | 10/10 `READY`、子系统 `PASSED` 55 次、`genuine_fails=0`，PASS |
| F 最终恢复 | generation 96、AP `READY`、RPTUN `CONNECTED(4)`、CPU2 online、`flags=0x3fff`，PASS |

交叉复算确认：28 个 raw 均存在且非空，`CAPTURE ERROR=0`，SUMMARY 中
`CHECK/FAIL/DEGRADED=0`。本轮同时实测闭环了以下脚本保护：

- Windows 串口捕获非零退出或 raw 为空时立即终止，不再在缺失证据时继续判读；
- Phase 0 自动规范化并严格门禁 `READY/CONNECTED/flags=0x3fff`；
- A/F 校验 READY 次数、子系统 PASSED 数和真失败数，C/D/E/R 失败进入总结果；
- `apctl cycle` 完成后显式恢复 AP/RPTUN，并用独立 `apctl status` 做最终门禁；
- 任一计分阶段失败时脚本最终返回非零，全部通过才输出 `verdict=PASS`。

因此，§7 中由旧 SUMMARY 假阳性导致的人工重判问题已经通过新一轮完整实板运行闭环；
后续默认 SOP 无需再依赖人工修正汇总结果。
