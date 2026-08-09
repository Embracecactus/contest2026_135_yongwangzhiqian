# BK7258 全功能连续压力测试 SOP

日期：2026-08-01
状态：`usable / 非 RPMsg 连续 30 代际 0 失败 / 自动 verdict=PASS 已实板闭环 / RPMsg worker 修复已越过原失效阈值`

适用项目：

```text
/home/lijian/project/open-vela
/home/lijian/project/open-vela/contest2026_135_yongwangzhiqian
```

依赖与通用约定见 [BK7258 双核自动编译、下载与板端调试 SOP](./bk7258-build-flash-debug-sop.md) 与
[Windows/WSL2 通用串口与 J-Link 调试 SOP](../../../tools/windows-hardware-debug/README.md)。
本 SOP 是功能**压力/稳定性**层面的专用流程，不替代编译/下载/冷复位 SOP。

## 1. 目标

在**不插入冷复位**的前提下，对板子当前所有非 RPMsg 功能做连续压力测试，记录：

- 每个子系统在 repeated AP start/stop、IPI、Mailbox、GPIO/IRQ 下的稳定性；
- 长时间运行是否出现资源泄漏 / `ENOMEM(-12)` / 性能退化 / 状态漂移；
- （可选）RPMsg 长时间并发回归，并核对 AP heap 快照不随 run 数增长。

## 2. 范围与已知排除项

| 项目 | 是否压测 | 说明 |
|---|---|---|
| AP SMP scheduler / 次核启动 | ✅ | `apctl cycle` 每代启动自检 |
| CPU2 scheduler-online | ✅ | 同上 |
| CPU 亲和性 (affinity) | ✅ | 同上 |
| 信号量远程唤醒 (sem-wake) | ✅ | 同上 |
| 信号量唤醒循环 (sem-loop) | ✅ | 同上 |
| 调度器生命周期 (BLCY) | ✅ | 同上 |
| 双向 pingpong (BP2P) / 双任务 (BDUL) | ✅ | 同上 |
| 受控迁移 (BMIG) / 定时等待 (BTIM) | ✅ | 同上 |
| IPI（核间中断） | ✅（仅 `cycle` 自检） | **仅** `apctl cycle` 每代启动自检（BP2P/BDUL/BMIG/BTIM/IPI 自检）。独立 `apctl ipitest` 在本固件是空操作，不能作压测向量（见 §4 注 + §7 实测） |
| Mailbox（SDK MBOX0） | ✅ | `apctl mbox` |
| GPIO / IRQ | ❌ 自动化跳过（交互式） | `bkgpioc0` / `bkgpioirq` 需手动按 P29 USERKEY（源码注释 "requires USERKEY"），属人工交互测试；`bkirqtest` 为定时器 IRQ 可自动化但任何 defconfig 未开启。按决策跳过，不纳入无人值守压测 |
| **RPMsg（bkrpmsgtest / syslog_rpmsg）** | ⚪ 默认不跑、可显式启用 | RPMsg 用例耗时和日志量较大，默认脚本不跑；设置 `STRESS_RPMSG=1` 后执行。原 `ENOMEM(-12)` 已确认是每轮退出的 CPU1 pthread 栈/TCB 未回收，不是 vring 通知泄漏，详见 §6 |

## 3. 前置条件

- Windows interop 可用：`powershell.exe` 存在；
- 串口枚举含 `COM11`（firmware console，460800 8N1）；
- 板子当前非 RPMsg 功能健康（`apctl status` 见 `AP state=READY`、`RPTUN state=CONNECTED(4)`、`flags=0x3fff`）；
- 若要验证 cold boot，物理复位按钮应可用；普通 RPMsg 连续回归不要求每轮复位。

## 4. 命令清单（压力相关）

所有命令在 NSH（CP 侧 console，COM11）下发。

| 命令 | 作用 | 成功标记 |
|---|---|---|
| `apctl status` | 打印全部子系统状态 | `AP state=READY(2)`、`RPTUN state=CONNECTED(4)`、`flags=0x3fff`、各子系统 `PASSED(n)`、`error=0` |
| `apctl cycle [count] [timeout_ms]` | 重复 count 次 AP start/stop 代际，每代跑全部启动自检 | 每次 `AP state=READY`；各子系统 `PASSED` |
| `apctl stop` / `apctl start` | 停 / 起 AP | `AP state=READY` 或停止态 |
| `apctl ipitest [count] [timeout_ms]` | IPI 压测命令（**本固件中空操作，不作为压测向量**） | online 模式返回 `ipitest is disabled ...`；stop 后打印状态但 `requested/completed/runs=0` |
| `apctl mbox [count] [timeout_ms]` | SDK MBOX0 压测 | `MBOX probe passed count=N` |
| `bkgpioc0` / `bkgpioirq` / `bkirqtest` | GPIO / IRQ 测试（**自动化跳过**） | `bkgpioc0`/`bkgpioirq` 需手动按 USERKEY（交互式，不适合无人值守）；`bkirqtest` 未编入任何 defconfig。故不在本 SOP 自动压测范围内。注：项目命令名是 `bkgpioirq`（非 `gpioirq`）；当前镜像另有 NuttX 通用 `gpio` 命令（低层工具，非压测） |

> 注：`apctl ipitest` 在 AP 在线（scheduler-online）时返回
> `apctl: ipitest is disabled while AP scheduler-online mode is active`（设计如此，避免打扰在线调度器的 IPI 使用）；
> 即便先 `apctl stop`，该命令也仅打印 IPI 状态、`requested/completed/runs=0`，**压测循环不执行**（2026-08-01 实测）。
> 因此 IPI 压力**只**由 `apctl cycle` 的启动自检覆盖，SOP 不要把独立 `apctl ipitest` 当作 IPI 压测项。

## 5. 一键压测脚本

复用脚本：

```bash
cd /home/lijian/project/open-vela
./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_stress_test.sh
# 含 RPMsg（需板子健康/已物理复位）：
STRESS_RPMSG=1 ./contest2026_135_yongwangzhiqian/board/bk7258/scripts/bk7258_stress_test.sh
```

脚本内部阶段（连续、无阶段间冷复位）：

1. **Phase 0** 基线 `apctl status`；若上一轮 `cycle` 留下 `STOPPED/QUIESCING`，自动执行 `apctl start` 后重新捕获状态，并严格门禁 `READY/CONNECTED/flags=0x3fff`；
2. **Phase A** `apctl cycle 20`（20 次完整 AP 代际，覆盖 SMP/CPU2/affinity/sem/blcy/bp2p/bdul/bmig/btim/IPI 启动自检）；
3. **Phase B** `apctl ipitest 1000 3000`（仅 INFO 记录——验证其为空操作/ENOTSUP；IPI 由 cycle 自检覆盖，不以此门禁）；
4. **Phase C** `apctl mbox 1000 1000` ×5（Mailbox 压力，累计 5000 条）；
5. **Phase D（跳过）** `bkgpioc0` / `bkgpioirq` / `bkirqtest` 为交互式/未编入项，不纳入无人值守压测。脚本仍探测其可用性作为信息记录；若某镜像已编入，会标记 UNAVAILABLE 或（对定时器类）运行，但默认按决策跳过 GPIO/IRQ 人工测试；
6. **Phase E** 持续 `apctl status` 轮询 ×10（每 ~4s，观察慢泄漏/退化，记录 heartbeat 递增与 `error=-`）；
7. **Phase F** `apctl cycle 10` 收尾稳定性；随后显式 `apctl start`，用独立 `apctl status` 验证最终恢复到 `READY/CONNECTED`；
8. **Phase R**（可选）`bkrpmsgtest all 100 60000` ×20，验证原失效阈值之后仍 PASS，并核对 `BRPT HEAP` 首尾一致。

每个阶段原始串口存于 `logs/stress-<时间戳>/<phase>.raw`，汇总写入 `stress-master-summary.txt`。
串口捕获非零退出或 raw 为空时脚本立即记录 `CAPTURE ERROR` 并终止；任一计分阶段失败时
最终退出码非零，只有全部门禁通过才输出 `STRESS DONE verdict=PASS`。

### 结果判读

- **真失败标记**（脚本据此判 PASS/FAIL，避免误判）：非零负 `error`、独立大写 `FAILED`/`FAIL`，以及不区分大小写的 `exception`/`panic`/`assert`/`abort`/`HardFault`/`data abort`。
- **假阳性**（不要当成失败）：状态行中的 `fail=0/0`、`fail0=0->0`、`dup/lost/fail=0/0/0` 是计数器归零；小写 `fault` 出现在零值计数器行里也不是失败。脚本因此采用两组规则：`FAIL`/`FAILED` 与负 `error` 大小写敏感，崩溃文本单独做大小写折叠；不能对整组规则使用 `grep -i`。2026-08-01 14:23 的连续压测曾因 `grep -ciE` 把实际 PASS 的 Phase A/C/E/F 误报为 `CHECK/FAIL/DEGRADED`。仅改成 `grep -cE` 又会漏掉行首 `FAIL` 以及 `PANIC`/`ASSERT`/`HardFault` 等大小写形式，故当前 `genuine_fail()` 使用混合敏感度的 `awk` 判读，每行至多计一次。离线回归已确认：本轮 24 个 `.raw` 均为 0 真失败，零值样本无误报，13 种故障样本和历史 `BRPT FAIL` 均能命中。每当 SUMMARY 出现 `FAIL/DEGRADED`，仍应按本段清单核对对应原始 `.raw`。
- **OOM 判读**：`status=-12` 只表示某次分配返回 `ENOMEM`，不能据此断言 vring 耗尽。必须同时查看 `BRPT SPAWN target/stage/status`、`BRPT HEAP` 和 `apctl status` 的 RPTUN pending/state。

## 6. RPMsg `ENOMEM` 的复核与修复（重要）

CodeBuddy 初版把 `status=-12` 推断成 vring 通知丢失。2026-08-01 的带堆/线程创建阶段诊断否定了该结论：

- 修复前每个 test run 后，AP heap 的 `allocated_bytes` 固定增加 `4360`，`allocated_blocks` 固定增加 2；该斜率与 payload、消息 count、idle/load 均无关。
- 首次失败精确发生在 `run=53`：`BRPT SPAWN target=3 stage=2 status=-12`，即 CPU1 worker 的 `pthread_create()` 返回 `ENOMEM`；此前 CPU0 worker 已完成，`workers_expected=1 workers_done=1`。
- 失败时 `heap_start free=11896 largest=7448`；失败后 `apctl status` 仍为 `AP READY / RPTUN CONNECTED`，`pending cp/ap=0/0`，所以 vring/mailbox 链路没有卡死，也不需要为恢复链路专门做物理复位。
- 原因是测试 wrapper 每轮创建 detached CPU0/CPU1 pthread；CPU1 线程退出后其 4 KiB 栈/TCB 没有在当前 AP SMP 生命周期路径中完全回收。这个问题位于测试线程生命周期，不位于 RPTUN 数据通路。

修复保持 NuttX 与 SDK 源码不变：AP 初始化时在 wrapper 内一次性创建 CPU0、CPU1 和 load 常驻线程，每轮仅通过信号量 dispatch。修复后：

- 连续运行到 `run=79`，越过原第 53 次失效点，0 FAIL；
- `bkrpmsgtest all 100 60000` 的 6 个 idle/load × payload 场景全部 PASS；
- 首尾 heap 恒为 `used=39696 free=225488 largest=221816 allocated_blocks=48`；
- `apctl cycle 3` 后在 generation 5 的 `run=80` 再次 PASS，heap仍不变。
- COM7 RTS 物理复位捕获到 `cold_path=yes`（`logs/bk7258-auto-debug/20260801-131328/summary.txt`）；冷启动 generation 1 的 AP/RPTUN/CPU2 均健康，随后 `run=1` PASS 且 heap仍不变（`logs/bkrpmsgtest-fixed-20260801/physical-cold-status-test.raw`）。

因此不要通过增大 vring 数量或修改 `bk7258_rptun_mbox.c` 来处理这个 `ENOMEM`；二者既没有证据支持，也会掩盖真正的线程资源问题。

## 7. 验收矩阵（本次 2026-08-01 实测）

| 阶段 | 迭代 | 结果 |
|---|---:|---|
| 基线状态 | 1 | 全部 `PASSED` / `CONNECTED(4)` / `flags=0x3fff` |
| AP 生命周期 cycle | 20 代际 | 20/20 `READY`，各子系统 `PASSED`，0 真失败 |
| IPI（仅 cycle 启动自检） | 20+10 代 | `PASSED`（自检覆盖；独立 ipitest 已验证为空操作，不计分） |
| Mailbox MBOX0 | 5000 条 | `MBOX probe passed` ×5，0 耗尽 |
| GPIO/IRQ | bkgpioc0/bkgpioirq/bkirqtest | **跳过（交互式 / 未编入）**：`bkgpioc0`/`bkgpioirq` 需手动按 USERKEY，不适合无人值守压测；`bkirqtest` 未编入任何 defconfig |
| 持续轮询 | 10 × ~4s | heartbeat 3370→4378 稳定递增，0 `error=-` |
| 收尾 cycle | 10 代际 | 10/10 `READY`，0 真失败 |
| RPMsg 修复前定位 | 53 runs | `run=53` CPU1 `pthread_create` / `ENOMEM`；每 run 泄漏 4360 B；RPTUN仍 CONNECTED |
| RPMsg 修复后阈值回归 | 80 runs | 越过原阈值，0 FAIL；`all 100` 六场景 PASS；heap 首尾完全一致；cycle 3 后仍 PASS |
| RPMsg 修复后物理冷启动 | 1 次 | `cold_path=yes`；AP READY / RPTUN CONNECTED / CPU2 online；冷启动 run 1 PASS |

### 7.1 连续压测记录：2026-08-01 14:23（默认 SOP，RPMsg 排除）

独立复核运行，记录目录 `logs/stress-20260801-142356`。覆盖与 §7 主表相同的非 RPMsg 向量，在修复后镜像上做一轮连续压力。下面结果为对原始 `.raw` **大小写敏感重判**后的真值（脚本首跑因 `genuine_fail` 用了 `grep -i` 把 PASS 误报成 `CHECK/FAIL/DEGRADED`，见 §5）。

| 阶段 | 迭代 | 结果（raw 大小写敏感重判） |
|---|---:|---|
| 基线状态 | 1 | `AP READY` / `RPTUN CONNECTED(4)` / `flags=0x3fff`，各子系统 `PASSED` |
| AP 生命周期 cycle | 20 代际 | 20/20 `READY`，子系统 `PASSED` 累计 105 次，0 真失败 |
| IPI（仅 cycle 启动自检） | 20 代 | `PASSED`（自检覆盖；独立 `ipitest` 仍为空操作，不计分） |
| Mailbox MBOX0 | 5000 条 | `MBOX probe passed` ×5（每轮 1000 条），0 真失败 |
| GPIO/IRQ | bkgpioc0/bkgpioirq/bkirqtest | 跳过（`UNAVAILABLE`：`CONFIG_BK7258_*` 未编入） |
| 持续轮询 | 10 × ~4s | heartbeat 2594→3600 稳定递增，0 真 `error=-` |
| 收尾 cycle | 10 代际 | 10/10 `READY`，0 真失败 |

**脚本判读偏差（已修复）**：本运行首次产出的 `stress-master-summary.txt` 把 Phase A/C/E/F 误报为 `CHECK/FAIL/DEGRADED`。根因是 `genuine_fail()` 当时用 `grep -ciE`（大小写不敏感），把状态行里零值计数器 `dup/lost/fail=0/0/0`、`fail0=0->0`、`fail=0/0` 的小写 `fail` 匹配进了大写 `FAIL` 规则。对 `.raw` 用大小写敏感规则重判后，各阶段均为 0 真失败、全部 PASS/OK。最终修复采用混合敏感度 `awk` 判读：状态/`FAIL` 标记大小写敏感，崩溃文本大小写折叠，同时补齐行首边界。离线回归覆盖本轮 24 个 `.raw`、零值样本、13 种故障样本和历史真实 `BRPT FAIL`；修复后的完整实板自动判读结果见 §7.2。

**结论：非 RPMsg 功能在约 14 分钟连续压力（30 次 AP 代际、5000 条 MBOX 消息）下稳定；RPMsg 的原 `ENOMEM` 也已在 wrapper 层定位、修复并越过失效阈值。2026-08-01 14:23 的独立连续压测（§7.1）暴露了脚本假阳性；混合敏感度判读、捕获 fail-fast、基线/收尾状态门禁和最终退出码已经由 §7.2 的完整实板运行闭环验证。**

### 7.2 自动判读闭环：2026-08-01 15:56（默认 SOP，RPMsg 排除）

记录目录：`logs/stress-20260801-155637/`。脚本未经过人工重判，以退出码 0 和
`STRESS DONE verdict=PASS` 完成。

| 阶段 | 自动判读结果 |
|---|---|
| 基线规范化 | 初始 `STOPPED/QUIESCING` 被识别并执行 `apctl start`；generation 64 的有效基线为 AP `READY` / RPTUN `CONNECTED(4)` / `flags=0x3fff`，PASS |
| AP 生命周期 | 20/20 `READY`、105 次子系统 `PASSED`、0 真失败，PASS |
| MBOX0 | `MBOX probe passed` 5/5、累计 5000 条、0 真失败，PASS |
| 持续轮询 | 10/10 `OK`，heartbeat 2596→3605 单调递增 |
| 收尾生命周期 | 10/10 `READY`、55 次子系统 `PASSED`、0 真失败，PASS |
| 最终恢复 | generation 96、AP `READY` / RPTUN `CONNECTED(4)` / CPU2 online / `flags=0x3fff`，PASS |

交叉复核：28 个 raw 文件均存在且非空、`CAPTURE ERROR=0`、SUMMARY 中
`CHECK/FAIL/DEGRADED=0`。这条记录是脚本修复后的正式自动化验收证据。

## 8. 一页式执行清单

```text
[ ] powershell.exe 可用，COM11 枚举存在
[ ] apctl status 健康（READY / CONNECTED(4) / flags=0x3fff）
[ ] 运行 bk7258_stress_test.sh（默认排除 RPMsg）
[ ] 确认无 CAPTURE ERROR，最终行为 STRESS DONE verdict=PASS 且进程退出码为 0
[ ] 逐阶段核对 stress-master-summary.txt 的 PASS/FAIL/OOM
[ ] 注意假阳性标记（fail=0 计数器不算失败）
[ ] 确认收尾状态恢复到 AP READY / RPTUN CONNECTED / flags=0x3fff
[ ] 如需 RPMsg：设置 STRESS_RPMSG=1；核对所有 suite PASS、无 BRPT FAIL，且 BRPT HEAP 首尾一致
[ ] 产出报告（见 stress-test-report-*.md）
```
