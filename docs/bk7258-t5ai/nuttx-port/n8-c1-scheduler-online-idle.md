# BK7258 N8-C1 — AP logical CPU1 scheduler-online IDLE bring-up

日期：2026-07-30

状态：`board-verified`（2026-07-30，真实 T5-AI 板卡；scheduler-online、双向 SMP-call、STAR IRQ context restore 与 AP 周期 sleep 全部闭环）

## 1. Stage 边界

N8-C1 从已板测的 N8-B2 双向 IPI 继续，让 physical CPU2 / AP logical CPU1 从私有 WFI park loop 进入 NuttX `nx_idle_trampoline()`：

```text
physical CPU1 -> AP logical CPU0 -> NuttX SMP primary
physical CPU2 -> AP logical CPU1 -> nx_idle_trampoline -> IDLE
```

本 Stage 仍保持 `CONFIG_SMP_DEFAULT_CPUSET=0x1`，普通 task 默认只在 logical CPU0 运行。当前只验证：

- CPU2 使用已分配的 logical CPU1 IDLE stack；
- CPU2 使用独立 interrupt stack，并进入 NuttX scheduler IDLE 路径；
- CPU2 发布 `SCHEDULER_ONLINE` 和诊断 `online_mask=0x3`；
- `up_send_smp_sched()` / `up_send_smp_call()` 复用 N8-B2 已验证的 SDK mailbox wrapper；
- mailbox IRQ 中执行 `nxsched_smp_call_handler()` 和 `nxsched_process_delivered()`；
- AP READY 前自动完成 CPU0→CPU1→CPU0 双向 asynchronous SMP-call callback；
- 不启用普通 task affinity/migration、默认 cpuset `0x3`、spinlock 压力、RPTUN/RPMsg、Wi-Fi 或 BLE。

## 2. 架构保持不变

scheduler IPI 数据面继续为：

```text
NuttX SMP scheduler/call
        ↓
team-owned BK7258 wrapper
        ↓
Beken SDK bk_mailbox_master_send()
        ↓
SDK MBOX0 FIFO / source-63
        ↓
team-owned SDK IRQ bridge / NuttX IRQ 79
        ↓
crosscore_mb_rx_isr()
        ↓
nxsched_smp_call_handler() + nxsched_process_delivered()
```

不新增直接 mailbox 寄存器数据面。N8-B2 的 PING/PONG/WAKE command ABI 和 0x80-byte IPI state 保持不变；N8-C1 在 shared SRAM offset `0x280` 新增独立 0x80-byte `BSMP` 诊断记录。

## 3. 安全门禁

新增 `CONFIG_BK7258_AP_SMP_SCHED_ONLINE`，只在新的 `configs/ap_smp_online/` 中启用。已板测的 `configs/ap_smp/` 保持 N8-B2 parked fallback。

scheduler-online 模式当前没有 CPU hot-unplug：

- `apctl stop`、`restart`、`cycle` 返回 `-ENOTSUP`；
- `apctl ipitest` 返回 `-ENOTSUP`，避免 scheduler IPI 污染 N8-B2 raw PING/PONG 精确计数；
- 恢复使用重新下载或物理复位，不把强制 reset/powerdown 宣称为 graceful stop。

## 4. 自动 SMP-call gate

AP init task 在发布 READY 前执行一次有超时的 asynchronous handshake：

1. logical CPU0 将 callback 放入 logical CPU1 的 NuttX SMP call queue；
2. `up_send_smp_call()` 通过 SDK wrapper 发送 SMP doorbell；
3. CPU1 mailbox IRQ 调用 `nxsched_smp_call_handler()`，执行 secondary callback；
4. secondary callback asynchronous enqueue reverse callback 到 CPU0；
5. CPU0 mailbox IRQ执行 reverse callback；
6. AP init task检查两个方向 tx/rx、handler、callback 和 send-failure 计数后才发布 READY。

scheduler doorbell 使用 per-target atomic pending bit进行 coalescing。接收端在 IRQ entry 清 pending，再 drain NuttX queues，避免 receiver-exit clear 造成新 work 丢门铃。

## 5. 构建与板测

由用户执行：

```bash
cd /home/lijian/project/open-vela

AP_CONFIG_NAME=ap_smp_online JOBS=8 \
  contest2026_135_yongwangzhiqian/board/bk7258/scripts/build_dual_image.sh
```

下载：

```text
nuttx/bk7258-dual/all-app-factory.bin
```

按既有 BKFIL 流程烧写到物理地址 `0x0`。Claude 不执行编译、下载、Git 或 static verifier。

下载后第一步只执行：

```text
apctl status
```

预期核心门禁：

```text
AP state=READY error=0
CPU2 state=SCHEDULER_ONLINE error=0
CPU2 ready=1 online=00000003 boots=1
AP IPI state=READY error=0
AP SMP state=PASSED error=0 online=00000003 boots=1 runs=1 requested/completed=2/2
SMP tx/rx cpu0=1/1 cpu1=1/1 coalesced=0/0 fail=0/0
SMP handler call/delivered cpu0=1/1 cpu1=1/1 callbacks=1/1 lastcpu=0
```

CPU2 heartbeat 应至少为 1，证明 CPU1 scheduler IPI 已从 IDLE/WFI 唤醒 CPU2。`calls` 预计至少为 2，表示 forward 和 reverse 两次 wrapper-backed SMP IPI request。

若 AP 未 READY、CPU2 fault、SMP state FAILED 或任何 send failure 非零，停止后续动作并保留完整 `apctl status` 输出。当前不要执行普通 CPU1 affinity task。

## 6. Gate 1 板端结果

2026-07-30 用户下载 `ap_smp_online` factory image 后，首次 `apctl status` 全部门禁通过：

```text
AP state=READY(2) error=0 generation=1 heartbeat=1
CPU2 state=SCHEDULER_ONLINE(8) error=0 generation=1 heartbeat=1
CPU2 MSP(init/run)=2809f000/2809f000 control=00000002
CPU2 ready=1 online=00000003 calls=2 boots=1
AP IPI state=READY(2) error=0
IPI irq/wake cpu0=1/1 cpu1=1/1 stale/spurious=0/0
AP SMP state=PASSED(4) error=0 online=00000003 boots=1 runs=1 requested/completed=2/2
SMP tx/rx cpu0=1/1 cpu1=1/1 coalesced=0/0 fail=0/0
SMP handler call/delivered cpu0=1/1 cpu1=1/1 callbacks=1/1 lastcpu=0
```

该结果证明：

- CPU2 已使用 PSP thread mode（`CONTROL.SPSEL=1`）和独立 MSP interrupt stack；
- CPU2 已进入 NuttX secondary IDLE/scheduler-online 边界；
- CPU0→CPU1→CPU0 两个方向均真实经过 wrapper-backed mailbox IRQ；
- 两核均执行 NuttX SMP call handler、delivered handler 和目标 callback；
- 没有 coalescing、send failure、stale 或 spurious；
- 普通 task 默认 cpuset 仍为 `0x1`，本结果不包含 CPU1 普通任务执行、affinity 或 migration。

N8-C1 Gate 1 因此收口为 `board-verified`。下一步先实测 scheduler-online 模式下 stop/ipitest 的 fail-closed 拒绝，再决定是否进入重复 SMP-call 压力或显式 CPU1 affinity Stage。

## 7. Gate 1 后续：AP 首次 sleep 不返回与 STAR wrapper 修复

后续板测确认 stop/ipitest 的 fail-closed 拒绝均符合预期，但在等待 2 秒后，AP 主任务 heartbeat 仍停留在 `1`：

```text
AP state=READY(2) error=0 generation=1 heartbeat=1
CPU2 state=SCHEDULER_ONLINE(8) error=0 generation=1 heartbeat=1
AP SMP state=PASSED(4) error=0
SMP tx/rx cpu0=1/1 cpu1=1/1
SMP handler call/delivered cpu0=1/1 cpu1=1/1 callbacks=1/1
```

源码路径明确为：

```text
bk7258_ap_main()
  -> heartbeat++
  -> nxsig_usleep(100 ms)
  -> watchdog timeout
  -> SysTick IRQ / nxsched_process_timer()
  -> nxsig_timeout()
  -> nxsched_add_readytorun()
  -> interrupt-driven context restore
```

自动 SMP-call gate 使用 `up_mdelay(1)` 轮询，只证明 mailbox IRQ 和两个 callback；它没有覆盖普通 task 阻塞、watchdog 超时和 IRQ context restore。因此 Gate 1 通过与 heartbeat 卡住并不矛盾。

当前最强源码定位是 AP image 仍被排除在 BK7258/STAR 的 `arm_doirq()` / `nxsched_resume_scheduler()` wrapper 之外。CP 已有的 wrapper 用于：

- 用 BASEPRI 阻止 common dispatcher 的非 HIPRI 嵌套；
- 在 `nxsched_resume_scheduler()` 清理 TCB context 前保留返回 frame；
- 修复 Thread-mode frame 的 `CONTROL.SPSEL`；
- 在 STAR no-switch 返回意外得到 NULL 时恢复已选 context。

N8-C1 的第一次 `nxsig_usleep()` 正好是 AP 首次完整覆盖 block -> idle -> SysTick wake -> IRQ scheduler restore 的路径。

已做待板测修复：

1. `chip/Make.defs` 只为 `CONFIG_BK7258_AP_SMP_SCHED_ONLINE` 增加既有的两个 linker wrap；`ap_up` 和 N8-B2 parked fallback 保持不变。
2. `bk7258_ap_vectors.c` 增加 AP wrapper 实现。与 CP 的单核标量不同，AP 版本使用 logical CPU0/CPU1 独立 bookkeeping，避免两个 AP 核同时处理中断时互相覆盖。
3. wrapper 无法恢复 context 时 fail-closed：发布 AP/SMP/CPU2 FAILED 状态后关中断 park，不静默继续。
4. 保留 SDK mailbox wrapper 数据面，不改 `up_send_smp_sched()`、`up_send_smp_call()` 或 mailbox command ABI。

同时把 `BSMP` 记录末尾原 `reserved[4]` 命名为同尺寸诊断字段：

```text
systick_irq_count[2]
sleep_enter_count
sleep_return_count
```

`arm_ack_irq()` 在 SysTick dispatch entry 增加 per-CPU 计数；AP heartbeat loop 在 `nxsig_usleep()` 前后分别增加 enter/return；`apctl status` 新增：

```text
SMP SysTick cpu0/cpu1=<n0>/<n1> sleep enter/return=<enter>/<return>
```

板测判据：

- 修复通过：AP heartbeat、CPU0 SysTick 和 sleep enter/return 均持续增长；enter 与 return 最多相差 1；CPU1 SysTick 可保持 0；原 SMP tx/rx、handler、callback 继续闭合且 failure 为 0。
- `SysTick cpu0=0`：CPU0 未进入 SysTick dispatch，继续查 NVIC/SysTick enable。
- `SysTick cpu0=1` 且停止、sleep enter=1/return=0：首个 SysTick/调度恢复仍卡住，wrapper 路径继续查证。
- SysTick 持续增长但 return 不增长：watchdog/ready-list 路径仍有独立问题。

### 7.1 板端闭环结果

2026-07-30 用户重新构建并下载 `ap_smp_online` factory image 后，连续三次状态采样得到：

```text
AP heartbeat:                         46 -> 75 -> 115
SMP SysTick cpu0/cpu1:              543/0 -> 864/0 -> 1306/0
SMP sleep enter/return:              46/45 -> 75/74 -> 115/114
CPU2 heartbeat:                       1 -> 1 -> 1
SMP tx/rx cpu0:                       1/1
SMP tx/rx cpu1:                       1/1
SMP handler call/delivered cpu0:      1/1
SMP handler call/delivered cpu1:      1/1
SMP callbacks:                        1/1
SMP coalesced/fail:                   0/0, 0/0
IPI stale/spurious:                   0/0
```

第三次完整状态仍为：

```text
AP state=READY(2) error=0 generation=1 heartbeat=115
CPU2 state=SCHEDULER_ONLINE(8) error=0 generation=1 heartbeat=1
CPU2 ready=1 online=00000003 calls=2 boots=1
AP SMP state=PASSED(4) error=0 online=00000003
SMP SysTick cpu0/cpu1=1306/0 sleep enter/return=115/114
```

该结果证明：

- AP logical CPU0 的 SysTick IRQ 持续进入 NuttX dispatcher；
- watchdog timeout 能把 AP init task 从 `nxsig_usleep()` 唤醒；
- IRQ-driven scheduler restore 返回正确 Thread/PSP context；
- sleep enter/return 始终只差 1，符合采样时 task 正处于下一轮 sleep；
- CPU2 没有额外 scheduler IPI 时 heartbeat 保持 1，符合 N8-C1 定义；
- 双向 SMP-call 的 tx/rx、handler、callback 计数保持精确闭合，修复没有污染 SDK mailbox wrapper 数据面；
- 没有 send failure、coalescing、stale、spurious、HardFault 或状态退化。

旧 image 在首次 `nxsig_usleep()` 后 heartbeat 永久停于 1；仅为 `ap_smp_online` 补上 SMP-safe per-core STAR dispatcher wrapper 后，heartbeat、SysTick 和 sleep-return 同时恢复，因此 AP 缺失 `arm_doirq()` / `nxsched_resume_scheduler()` wrapper 的根因完成板端 A/B 闭环。

N8-C1 至此完整收口为 `board-verified`。普通 CPU1 affinity/migration 仍不属于本 Stage；只有用户明确选择下一 Stage 后才启用。
