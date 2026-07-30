# BK7258 N8-C2 — AP logical CPU1 explicit-affinity one-task gate

日期：2026-07-30

状态：`board-verified`（2026-07-30，真实 T5-AI 板卡；单 explicit-affinity CPU1 task、remote dispatch/wake 与 PID release 全部门禁通过）

## 1. Stage 边界

N8-C2 从已板测的 N8-C1 scheduler-online baseline 继续，只开放一个最小普通任务门禁：

```text
AP logical CPU0 init task
        -> create exactly one diagnostic pthread
        -> initial affinity mask = 0x2
        -> wrapper-backed scheduler IPI
        -> AP logical CPU1 leaves IDLE/WFI
        -> diagnostic pthread runs its body once and releases its PID
```

本 Stage 必须保持：

- `CONFIG_SMP_DEFAULT_CPUSET=0x1`，所有未显式指定 affinity 的普通任务继续只允许在 logical CPU0 运行；
- 只创建一个诊断任务，且在激活前通过 pthread attributes 明确设置 logical CPU1 mask `0x2`；
- 只验证首次 CPU0 remote dispatch / CPU1 idle wake / CPU1 task entry；
- 继续复用 N8-C1 的 NuttX scheduler -> team wrapper -> Beken SDK mailbox -> IRQ79 数据面；
- 不开放默认 cpuset `0x3`，不启用自由迁移、第二个 CPU1 task、负载均衡、spinlock 压力、RPTUN/RPMsg、Wi-Fi 或 BLE；
- scheduler-online 模式仍无 CPU hot-unplug，stop/restart/cycle/ipitest 的 fail-closed 边界不变。

## 2. 实现门禁

计划新增独立 `CONFIG_BK7258_AP_SMP_CPU1_AFFINITY` 和 `configs/ap_smp_affinity/`，保留已板测的 `configs/ap_smp_online/` 作为 N8-C1 fallback。

AP 发布 READY 前按顺序执行：

1. N8-C1 双向 asynchronous SMP-call selftest 继续先通过；
2. 初始化独立 `BAFF` shared-state 诊断记录；
3. 创建一个 detached pthread，并通过 `pthread_attr_setaffinity_np()` 在激活前指定 mask `0x2`；
4. 任务入口记录实际 logical CPU、实际 affinity mask 和 start/complete 次数；
5. CPU0 有界等待任务完成，并检查 CPU0 SMP tx、CPU1 SMP rx、CPU1 mailbox IRQ/wake 和 CPU2 SMP request 计数均增长，send failure 不增长；
6. 只有上述条件全部满足才把 `BAFF` 标记为 `PASSED` 并继续发布 AP READY。

独立记录使用 shared SRAM offset `0x300`，不改变 N8-B2 的 `BIPI` 0x80-byte ABI，也不改变 N8-C1 的 `BSMP` 0x80-byte ABI。

## 3. 当前授权与执行边界

本轮由 Claude 执行：

- team-overlay 最小实现；
- 文档同步；
- 不编译的静态源码验证。

本轮明确不执行：

- 编译、distclean 或 static verifier；
- 下载、烧写或板端命令；
- Git status、diff、commit、push 或 PR。

构建、下载和板测由用户执行。

## 4. 预期板端判据

用户构建时使用：

```bash
cd /home/lijian/project/open-vela

AP_CONFIG_NAME=ap_smp_affinity JOBS=8 \
  contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/build_dual_image.sh
```

下载 factory image 后第一步只执行：

```text
apctl status
```

预期新增核心输出：

```text
AP affinity state=PASSED error=0 ... requested/observed=00000002/00000002
Affinity task cpu=1 started/completed/pid-released=1/1/1
Affinity SMP tx0=<before>-><after> rx1=<before>-><after> fail0=<same>-><same>
Affinity IPI irq1=<before>-><after> wake1=<before>-><after> calls=<before>-><after>
```

通过条件：

- AP 仍为 `READY`，CPU2 仍为 `SCHEDULER_ONLINE`，N8-C1 `BSMP` 仍为 `PASSED`；
- `BAFF` 为 `PASSED` 且 error=0；
- requested/observed mask 均为 `0x2`；
- task cpu=1，started/completed/pid-released 精确为 1/1/1；
- CPU0 SMP tx、CPU1 SMP rx、CPU1 IRQ/wake 和 CPU2 SMP request 至少各增长 1；
- CPU0 send failure 不增长；
- 默认 cpuset 仍为 `0x1`。

任何 timeout、错误 CPU/mask、计数不增长、send failure 增长、AP 未 READY 或 CPU2/SMP 状态退化都判定本 Gate 失败，并停止后续 affinity/migration 扩展。

## 5. 当前进度

- 2026-07-30：用户明确选择 N8-C2，并授权只做 team-overlay 实现与不编译静态验证；编译、下载和板测由用户完成。
- 2026-07-30：team-overlay 实现已落地，尚未构建：
  - 新增 `CONFIG_BK7258_AP_SMP_CPU1_AFFINITY` 和 `configs/ap_smp_affinity/`，build wrapper 已允许 `AP_CONFIG_NAME=ap_smp_affinity`；
  - `CONFIG_SMP_DEFAULT_CPUSET=0x1` 保持不变，C 源码继续以编译期断言 fail-closed；
  - shared SRAM `0x300` 新增独立 0x80-byte `BAFF` ABI，不修改 `BIPI`/`BSMP` 既有 0x80-byte 记录；
  - AP 在 N8-C1 selftest 通过后创建且仅创建一个 detached pthread，通过 create attributes 在激活前设置 mask `0x2`；
  - task 与 CPU0 gate 记录实际 CPU/mask、start/complete/PID-release、SMP tx/rx/failure、CPU1 IRQ/wake 和 CPU2 SMP request 前后值；
  - `apctl status` 只在 `BAFF` magic/version/size 有效时输出 N8-C2 记录，N8-C1 fallback 输出保持不新增 unavailable 噪声；
  - CP 新 generation 初始化会清除旧 `BAFF` magic。
- 当前状态仍为 `static-only`：未编译、未运行 static verifier、未下载、未板测、未执行 Git。
- 2026-07-30 静态复核 F1：第一轮实现把 `task_completed` 作为 detached pthread 完成门禁，但该字段在 pthread entry 返回前写入，只能证明 task body 已执行，不能严格证明 detached TCB 已退出并从 PID 表移除；AP 可能在极小窗口内先发布 READY，与“run once and exit before READY”的 Stage 定义不完全一致。该问题在独立 review agent 报告 PASS 后由主审再次检查 task-exit 时序发现。
- F1 修正已落地：保留 detached/create-time-affinity 设计；CPU0 在看到 `task_completed=1` 后继续有界轮询 `nxsched_get_tcb(thread)`，只有 PID 已不可见时才进入计数验收。这样不引入无界 `pthread_join()`，仍保持单任务和总超时门禁。
- 2026-07-30 静态复核 F2：上游 `pthread_exit()` 先执行 `nxsched_release_pid()` 清 PID hash，再调用 `up_exit()` 完成最终 context switch；因此 `nxsched_get_tcb()==NULL` 严格证明的是 PID 已释放、task body 已返回并进入 exit path，不等价于 TCB 已完全 free 或 CPU1 已完成最终 deschedule。原字段/文档名 `task_exited` 过强，但不影响本 Stage 的 remote dispatch/wake 主门禁。
- F2 语义收紧已落地：ABI 字段、代码判据、`apctl status` 和板测文案均统一为 `pid_released`；Stage 只宣称 task body complete + PID released before READY，不宣称最终 TCB free 或 CPU1 已完成 exit context switch。未引入 scheduler/arch exit instrumentation，也未使用无界 join。
- 2026-07-30 最终源码级静态复核：`PASS`。已确认：
  - NuttX `pthread_create()` 在 `nxtask_activate()` 前应用 attr affinity，mask `0x2` 不存在先在 CPU0 执行的窗口；
  - team 代码只有一个 `pthread_create()`，且 started/completed/pid-released 均要求精确 `1/1/1`；
  - `nxsched_get_tcb()` / `nxsched_put_tcb()` 每轮严格配对，timeout 出口不持有 TCB reference；
  - `pid_released` 语义与上游 `nxsched_release_pid()` -> `up_exit()` 顺序一致；
  - `BAFF` 仍为 0x80 bytes，offset 链 `BSMP 0x280..0x2ff` -> `BAFF 0x300..0x37f` 无重叠；
  - Kconfig、defconfig、build wrapper、prototype/include、CP stale-magic clear 和 `apctl` format 参数一致；
  - `ap_smp_online` 未启用新 symbol，N8-C1 fallback 不创建诊断 pthread，且不会输出额外 affinity unavailable 行。
- 该时点状态为 `static-only`：源码复核通过；Claude 未编译、未运行编译型/static verifier、未下载、未板测、未执行 Git。随后由用户完成构建、下载和真实板卡验证。

## 6. 板端闭环结果

2026-07-30，用户构建并下载 `ap_smp_affinity` image 后执行首次 `apctl status`，得到：

```text
AP state=READY(2) error=0 generation=1 heartbeat=274
CPU2 state=SCHEDULER_ONLINE(8) error=0 generation=1 heartbeat=2
CPU2 ready=1 online=00000003 calls=3 boots=1
AP IPI state=READY(2) error=0
IPI irq/wake cpu0=1/1 cpu1=2/2 stale/spurious=0/0
AP SMP state=PASSED(4) error=0 online=00000003 boots=1 runs=1 requested/completed=2/2
SMP tx/rx cpu0=2/1 cpu1=1/2 coalesced=0/0 fail=0/0
SMP handler call/delivered cpu0=1/1 cpu1=2/2 callbacks=1/1 lastcpu=0
SMP SysTick cpu0/cpu1=3047/0 sleep enter/return=274/273
AP affinity state=PASSED(4) error=0 generation=1 runs=1 timeout=3000
Affinity requested/observed=00000002/00000002 task id/cpu=3/1
Affinity started/completed/pid-released=1/1/1
Affinity SMP tx0=1->2 rx1=1->2 fail0=0->0
Affinity IPI irq1=1->2 wake1=1->2 calls=2->3
```

门禁逐项闭合：

1. AP 保持 `READY/error=0`，physical CPU2 保持 `SCHEDULER_ONLINE/error=0`、ready=1、online=`0x3`；
2. N8-C1 `BSMP` 仍为 `PASSED`，双向 callback 基线保持 `1/1`，没有 coalesced/send-failure/stale/spurious；
3. 诊断 pthread requested/observed mask 精确为 `0x2/0x2`，实际只在 logical CPU1 运行；
4. task started/completed/pid-released 精确为 `1/1/1`，没有创建第二个诊断任务；
5. 该 task 引起 CPU0 SMP tx `1->2`、CPU1 SMP rx `1->2`、CPU1 IRQ/wake `1->2`、CPU2 SMP request `2->3`，每项精确增加 1；
6. CPU0 send failure 保持 `0->0`，证明首次 remote dispatch 沿既有 wrapper-backed mailbox 数据面成功；
7. AP heartbeat=274、CPU0 SysTick=3047、sleep enter/return=274/273，说明 one-task gate 后 N8-C1 周期 sleep/wake 仍持续稳定；CPU1 SysTick 保持 0，符合本 Stage 不验证 CPU1 timer wake 的边界；
8. 编译期与 defconfig 的默认 cpuset 仍为 `0x1`；本结果只开放显式 mask `0x2` 的单 task，不代表默认 CPU1 placement 或自由迁移已开放。

因此 N8-C2 在当前下载/warm-start 路径收口为 **`board-verified`**。本 Stage 不需要再运行额外压力、第二个 CPU1 task 或 migration 命令。physical cold-reset 覆盖仍属于既有独立 open issue，不由本结果扩张。

下一 MAIN Stage 尚未选择；在用户明确选择前继续保持默认 cpuset `0x1`，不开始默认 `0x3`、自由迁移、重复 CPU1 task、spinlock 压力或 RPTUN/RPMsg。
