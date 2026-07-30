# BK7258 N8-C3 — CPU1-bound single-task block → CPU0 semaphore remote-wake gate

日期：2026-07-30

状态：`board-verified`（2026-07-30 用户真实 T5-AI `apctl status`；当前 download/warm-start path；全部 gate 按设计通过）

## 1. Stage 边界

N8-C3 从已板测的 N8-C2 explicit-affinity one-task baseline 继续，只增加一次可归因的阻塞与远端唤醒：

```text
AP logical CPU0 init task
        -> create the same single diagnostic pthread with affinity 0x2
        -> diagnostic pthread runs on logical CPU1
        -> pthread blocks on a zero-count semaphore
        -> CPU0 proves that exact pthread is queued on that semaphore
        -> CPU0 posts exactly once
        -> scheduler IPI wakes logical CPU1
        -> the same pthread returns from wait, completes, and releases its PID
```

本 Stage 必须保持：

- `CONFIG_SMP_DEFAULT_CPUSET=0x1`；未显式指定 affinity 的普通任务继续只允许在 logical CPU0 运行；
- 仍只创建 N8-C2 的一个 detached diagnostic pthread，不允许先跑 N8-C2 task 再创建第二个 N8-C3 task；
- pthread create-time affinity 继续固定为 logical CPU1 mask `0x2`；
- CPU0 只有在源码级确认该 pthread 已进入 `TSTATE_WAIT_SEM`、`waitobj` 指向目标 semaphore 且 semaphore value 为 `-1` 后才允许 post；
- post 前后单独快照 CPU0 SMP tx、CPU1 SMP rx、CPU1 mailbox IRQ/wake、CPU2 SMP request 和 CPU0 send-failure，用于把本次增量与 N8-C2 首次 dispatch 分离；
- task body complete 后继续有界确认 PID released；不宣称最终 TCB free 或 CPU1 已完成最终 exit context switch；
- 不开放默认 cpuset `0x3`、自由迁移、第二个 CPU1 task、重复 wake、压力测试、spinlock 压力、RPTUN/RPMsg、Wi-Fi 或 BLE；
- scheduler-online 模式仍无 CPU hot-unplug，stop/restart/cycle/ipitest 的 fail-closed 边界不变。

## 2. 最小实现计划

1. 新增独立 `CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE` 与 `configs/ap_smp_semwake/`；既有 `ap_smp_affinity` 保持 N8-C2 行为不变。
2. 不新增第二个 selftest pthread；仅在新配置下扩展 `bk7258_ap_cpu1_affinity_task()` 的同一个 task body。
3. 使用 AP 私有静态 zero-count semaphore；`nxsem_init()` 初始化为 count 0。仅当 `CONFIG_PRIORITY_INHERITANCE` 启用时显式设置 `SEM_PRIO_NONE`；PI 关闭时初始化 flags 已为 0，不引入该可选符号的 link dependency。
4. 新增独立 shared SRAM `BSEM` 0x80-byte 记录，offset `0x380`；不修改 `BIPI`、`BSMP`、`BAFF` 的既有 ABI。
5. CPU1 task 在 affinity 判据通过后记录 wait-entered 并执行 uninterruptible semaphore wait；CPU0 在 scheduler critical section 内验证 exact waiter 后记录 post-window baseline，并从 logical CPU0 post 一次。
6. CPU1 task 记录 wait return、return code 和 wake CPU；CPU0 等待 task complete + PID released，再验收 post-window counters。
7. 只有 N8-C1 `BSMP`、N8-C2 `BAFF` 与 N8-C3 `BSEM` 全部通过，AP 才发布 READY。
8. CP 新 generation 初始化清除旧 `BSEM` magic；`apctl status` 仅在 magic/version/size 有效时打印 N8-C3 记录，旧配置不增加 unavailable 噪声。

## 3. 静态门禁

源码验收必须确认：

- team code 仍只有一个 `pthread_create()`；
- new config 依赖 N8-C2 affinity gate，旧 `ap_smp_online` / `ap_smp_affinity` 不启用它；
- exact waiter proof 同时检查 task state、wait object、semaphore value 和 CPU1 affinity，并严格配对 `nxsched_get_tcb()` / `nxsched_put_tcb()`；
- CPU0 post 发生在 waiter-observed 之后，且 `post_cpu=0`、`post_count=1`；
- task wait return 发生在 logical CPU1，return code 为 0；
- post-window CPU0 tx、CPU1 rx、CPU1 IRQ/wake、CPU2 calls 各精确增加 1，send failure 不增长；
- timeout/failure 路径不会销毁仍可能被 detached task 使用的 semaphore，并会尽力 post 释放潜在 waiter；
- `BSEM` 精确为 0x80 bytes，`BAFF 0x300..0x37f` 与 `BSEM 0x380..0x3ff` 无重叠；
- AP READY 仍晚于 task completion、PID release 和全部 counter 验收。

## 4. 板测收口与执行边界

- 2026-07-30，用户在真实 T5-AI 上下载 N8-C3 image，并从当前 warm-start 路径取得 `apctl status` 证据。
- 本文据该实板证据正式把 N8-C3 收口为 `board-verified`；结论限定于当前 download/warm-start path，不外推为物理 cold-reset 验证。
- 所有 N8-C1 `BSMP`、N8-C2 `BAFF` 与 N8-C3 `BSEM` gate 均按设计通过；gate 后持续运行证据已充分，不需要追加稳定性 sample。
- 本次收口不选择下一 Stage，也不推荐或授权第二个 CPU1 task、重复 wake、migration、默认 cpuset `0x3` 或 stress test；这些扩展继续保持关闭，等待用户另行明确选择。
- 本文只记录用户提供的板端事实，不补写未提供的 compile hash、artifact hash 或 Git 状态。

## 5. 真实板端 `apctl status` 证据

2026-07-30 用户提供的状态可忠实压缩为：

```text
AP READY(2) error=0 generation=1 heartbeat=751
CPU2 SCHEDULER_ONLINE(8) error=0 heartbeat=3 ready=1 online=0x3 calls=4 boots=1
IPI irq/wake cpu0=1/1 cpu1=3/3 stale/spurious=0/0
BSMP PASSED(4) error=0 online=0x3 runs=1 requested/completed=2/2
SMP tx/rx cpu0=3/1 cpu1=1/3 coalesced=0/0 fail=0/0
handler call/delivered cpu0=1/1 cpu1=3/3 callbacks=1/1 lastcpu=0
SysTick cpu0/cpu1=8329/0 sleep enter/return=751/750
BAFF PASSED(4) error=0 requested/observed=0x2/0x2
BAFF task id/cpu=3/1 started/completed/pid-released=1/1/1
BAFF aggregate tx0=1->3 rx1=1->3 fail0=0->0
BAFF aggregate irq1=1->3 wake1=1->3 calls=2->4
BSEM PASSED(6) error=0 task id=3 wait entered/observed/value=1/1/-1
BSEM post cpu/count/result=0/1/0 wait returned/result/cpu=1/0/1
BSEM isolated tx0=2->3 rx1=2->3 fail0=0->0
BSEM isolated irq1=2->3 wake1=2->3 calls=3->4
```

## 6. Gate closure analysis

1. **AP 与 scheduler-online baseline 通过。** AP 为 `READY(2)`、`error=0`、`generation=1`；CPU2 为 `SCHEDULER_ONLINE(8)`、`error=0`、`ready=1`、`online=0x3`，且 `boots=1`。N8-C3 没有破坏双核 scheduler-online 生命周期。
2. **N8-C1 gate 继续通过。** `BSMP PASSED(4)`、`error=0`、`requested/completed=2/2`，全局 `callbacks=1/1`、`lastcpu=0` 与既有 callback baseline 一致。
3. **同一个 single task 完成 dispatch、阻塞、远端唤醒与退出。** `BAFF` 与 `BSEM` 都记录 `task id=3`；该 task 以 requested/observed affinity `0x2/0x2` 在 logical CPU1 运行，`started/completed/pid-released=1/1/1`。证据中没有第二个 task。
4. **semaphore 协议逐项精确通过。** CPU0 观察到 exact waiter 后，`wait entered/observed/value=1/1/-1`；随后仅由 CPU0 post 一次，`post cpu/count/result=0/1/0`；同一 task 在 CPU1 返回，`wait returned/result/cpu=1/0/1`。
5. **BAFF aggregate `+2` 与 BSEM isolated `+1` 正是设计结果。** BAFF 从 task 首次 remote dispatch 前计数，因此 CPU0 tx、CPU1 rx、CPU1 IRQ/wake 均为 `1->3`，CPU2 calls 为 `2->4`，覆盖同一 task 的首次 dispatch 与后续 semaphore remote wake 两次 remote scheduling；BSEM 只包围 semaphore post/wake window，因此对应计数均为 `2->3`，calls 为 `3->4`，精确隔离出第二次 remote scheduling。这个 `+2 aggregate / +1 isolated` 非对称证明同一个 single task 先被 dispatch、再被 semaphore 远端唤醒，并不表示创建了第二个 task。
6. **无异常计数。** SMP coalesced=`0/0`、fail=`0/0`，IPI stale/spurious=`0/0`；BAFF 与 BSEM window 的 `fail0` 都保持 `0->0`。每个门禁都按设计精确命中，没有额外 send、coalescing、stale 或 spurious 路径。
7. **gate 后持续运行已由同一份状态证明。** AP heartbeat=`751`、CPU0 SysTick=`8329`、sleep enter/return=`751/750`，说明 gate 完成后 AP 仍持续调度并反复进入/返回 sleep。该样本已满足 N8-C3 的持续运行判据，不需要再采一份稳定性样本。
8. **正式结论。** N8-C3 在当前 download/warm-start path 上为 `board-verified`，且每个 gate 都按设计通过。到此仅收口本 Stage；不据此推荐或授权第二个 CPU1 task、重复 wake、migration、默认 cpuset `0x3` 或 stress test。

## 7. 当前进度

- 2026-07-30：N8-C2 已由用户在真实 T5-AI 板卡验证并收口为 `board-verified`。
- 2026-07-30：用户明确选择 N8-C3；随后完成 team-overlay 实现、文档同步与源码级静态复核，并由用户执行构建、下载和真实板测。
- 2026-07-30：已固定 N8-C3 Stage 边界、single-task 复用策略、exact waiter proof、独立 post-window counter attribution 和板端判据。
- 2026-07-30：team-overlay 第一版实现落地：
  - 新增 `CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE`、`configs/ap_smp_semwake/` 和 build-wrapper config allowlist；
  - 新增 shared SRAM `BSEM` 0x80-byte ABI（offset `0x380`），CP generation prepare 清除 stale magic，`apctl status` 仅在 ABI 有效时打印；
  - 继续复用唯一 N8-C2 pthread；CPU1 affinity 验证后进入 static semaphore wait，CPU0 以 TCB state + waitobj + sem value `-1` + affinity `0x2` 证明 exact waiter，再 single post；
  - BSEM 单独记录 wait/post/wake 状态和 post-window SMP/IPI/calls counters，最终要求各精确 `+1`；
  - AP READY 仍晚于 task complete、PID release、BAFF/BSEM 全部门禁。
- 2026-07-30 静态复核 F1：第一版沿用 N8-C2 的 `nxsched_get_tcb()` / `nxsched_put_tcb()` PID-release 轮询。若 CPU0 在 CPU1 进入 `nxsched_release_pid()` 前持有额外 TCB reference，CPU1 会阻塞在 `tcb->exit_sem`，CPU0 put reference 时可能再触发一次 CPU0→CPU1 scheduler IPI，污染 N8-C3 要求精确 `+1` 的 post-window。计划在 N8-C3 配置下改为持有 `g_pidhashlock` 的只读 PID-hash visibility 检查，不增加 TCB reference；N8-C2 配置路径保持不变。
- 2026-07-30 静态复核 F2：AP startup failure paths 先发布 `state=FAILED` 再写 `error`，CP polling 理论上可在 error store 前观察 FAILED 并复位 AP，丢失 N8-C3 专用错误码。计划统一改为 error-first + barrier + FAILED-last publication。
- 2026-07-30 静态复核 F3：`nxsem_set_protocol()` 只在 `CONFIG_PRIORITY_INHERITANCE` 下提供实现，而当前 AP defconfig 不启用该默认关闭选项；无条件调用会形成潜在 link failure。`nxsem_init()` 在 PI 关闭时已把 `sem->flags` 初始化为 `SEM_PRIO_NONE(0)`，因此不为本 Stage 全局开启 PI。
- F1 修正已落地：仅在 N8-C3 配置下使用 `g_pidhashlock` 保护的只读 PID-hash visibility check；不取得 TCB reference，因此不会通过 `exit_sem` 注入第二次 scheduler IPI。既有 N8-C2 配置继续保留原 `nxsched_get_tcb()` / `nxsched_put_tcb()` 路径，历史行为不变。
- F2 修正已落地：新增统一 startup failure publisher，按 `error` → `dmb` → `FAILED` → `dmb` → mailbox event 顺序发布；所有启动失败出口均使用该路径，CP 不会先观察 FAILED 后丢失错误码。
- F3 修正已落地：`nxsem_set_protocol(..., SEM_PRIO_NONE)` 仅在 `CONFIG_PRIORITY_INHERITANCE` 下编译；PI 关闭的 `ap_smp_semwake` 路径只调用 `nxsem_init()`，nested preprocessor 与 affinity-only fallback 已独立复核通过。
- 2026-07-30 最终源码级静态复核：`PASS`。已确认：
  - team AP SMP source 仍只有一个 `pthread_create()`；N8-C3 复用同一 task，没有第二个 selftest task；
  - create-time affinity、exact waiter proof、CPU0 single post、CPU1 wait return、task complete 与 non-intrusive PID-release 判据顺序闭合；
  - BSEM post-window 不被 PID polling 污染，tx/rx、IRQ/wake、calls 精确 `+1`，failure unchanged 的源码判据完整；
  - timeout/failure 路径不 destroy live semaphore，pre-post 失败会 best-effort post，且当前 call graph 每个 AP generation 只运行一次该 boot gate；
  - `BSEM` 为 0x80 bytes，`BAFF 0x300..0x37f` → `BSEM 0x380..0x3ff` 连续无重叠；BIPI/BSMP/BAFF ABI 未修改；
  - Kconfig、`ap_smp_semwake` defconfig、build wrapper、CP stale-magic clear、AP error publication 与 `apctl` format/arguments 一致；
  - `ap_smp_online` / `ap_smp_affinity` 不启用新 symbol；旧配置不创建 semaphore waiter，也不输出额外 sem-wake unavailable 行。
- 2026-07-30 真实 T5-AI 板测：`AP READY/error=0`、CPU2 `SCHEDULER_ONLINE/error=0`，`BSMP`、`BAFF`、`BSEM` 全部 `PASSED/error=0`；同一 `task id=3` 的 BAFF aggregate `+2` 与 BSEM isolated `+1` 精确符合设计。
- 当前状态正式收口为 `board-verified`（当前 download/warm-start path）。heartbeat=`751`、CPU0 SysTick=`8329`、sleep enter/return=`751/750` 已证明 gate 后持续运行，不需要追加稳定性 sample；下一 Stage 尚未选择，所有多任务、重复唤醒、迁移、默认 cpuset `0x3` 与压力扩展继续关闭。
