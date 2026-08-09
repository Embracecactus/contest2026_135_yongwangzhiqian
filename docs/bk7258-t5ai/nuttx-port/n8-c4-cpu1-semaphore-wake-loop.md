# BK7258 N8-C4 — same CPU1-bound task 8-cycle semaphore remote-wake gate

日期：2026-07-30

状态：`board-verified`（2026-07-30，用户真实 T5-AI 当前 download/warm-start path）

## 1. Stage 边界

N8-C4 从已板测的 N8-C3 single block/wake baseline 继续，只把同一个 CPU1-bound task 的 semaphore remote wake 扩展为固定 8 轮：

```text
create exactly one pthread with affinity 0x2
        -> cycle 1: CPU1 block -> CPU0 exact-waiter proof -> single post -> CPU1 return
        -> cycle 2..8: repeat the same bounded handshake on the same task
        -> complete once
        -> release one PID
```

本 Stage 必须保持：

- `CONFIG_SMP_DEFAULT_CPUSET=0x1`；普通任务继续只允许在 AP logical CPU0 运行；
- 仍只有一个 detached diagnostic pthread，create-time affinity 固定为 logical CPU1 mask `0x2`；
- 固定 `requested_cycles=8`，不是压力测试，也不接受运行时可变循环次数；
- 每一轮都必须先确认 exact waiter：目标 task 为 `TSTATE_WAIT_SEM`、`waitobj` 指向同一 static semaphore、semaphore value=`-1`、affinity=`0x2`；
- CPU0 每轮只 post 一次，task 每轮只在 logical CPU1 返回一次；wait/post/wake sequence 必须严格为 `1..8`；
- 第 1 轮继续保留 N8-C3 `BSEM` 独立 `+1` 证据；新增 N8-C4 `BSWL` 记录覆盖完整 8 轮；
- task 只在第 8 轮完成后退出，并继续使用 non-intrusive PID-hash visibility check 确认 PID released；
- 不开放第二个 CPU1 task、重复/无限循环、migration、默认 cpuset `0x3`、负载均衡、spinlock 压力、RPTUN/RPMsg、Wi-Fi 或 BLE。

## 2. 最小实现门禁

1. 新增 `CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE_LOOP`，依赖 N8-C3 `CONFIG_BK7258_AP_SMP_CPU1_SEM_WAKE`；新增 `configs/ap_smp_semwake_loop/`。
2. 继续复用 `bk7258_ap_cpu1_affinity_task()` 和唯一 `pthread_create()`，不新增第二个 task 或第二个 selftest。
3. N8-C3 `BSEM` ABI、offset `0x380` 和 single-wake 语义保持不变；第 1 轮仍形成 `BSEM PASSED`。
4. 新增独立 0x80-byte `BSWL` shared record，offset `0x400`，记录：
   - requested/completed cycles；
   - wait-entered / waiter-observed / post / wait-returned 累计次数；
   - wait/post/wake sequence；
   - last waiter value、post CPU/result、wait result/wake CPU；
   - 完整 8 轮前后的 SMP tx/rx/failure、IPI IRQ/wake 和 CPU2 calls。
5. CPU1 task 每轮 wake 后先发布 `WOKEN`；第 1..7 轮必须等待 CPU0 发布 `CONTINUE` 后才能进入下一轮，防止 CPU0 丢失每轮完成状态。
6. CPU0 每轮有界等待 `WAITING` + exact waiter，记录 sequence 后 single post，再有界等待同 cycle `WOKEN`；只有前 7 轮发布 `CONTINUE`。
7. 第 8 轮完成后等待 task complete + PID released，再统一验收 BAFF/BSEM/BSWL 与 N8-C1 baseline；全部通过后才允许 AP READY。
8. CP generation prepare 清除 stale `BSWL` magic；`apctl status` 只在 ABI 有效时打印 sem-loop 记录，旧配置不增加 unavailable 噪声。

## 3. 预期计数

以已板测的 N8-C1 baseline 为起点：

- N8-C1 callback handshake：CPU0→CPU1 一次、CPU1→CPU0 一次；
- 同一 diagnostic task 首次 dispatch：CPU0→CPU1 一次；
- N8-C4 semaphore wake：CPU0→CPU1 八次。

因此预期：

```text
BAFF aggregate: tx0/rx1/irq1/wake1 1->10, calls 2->11   (+9)
BSEM cycle 1:   tx0/rx1/irq1/wake1 2->3,  calls 3->4    (+1)
BSWL cycles 1-8:tx0/rx1/irq1/wake1 2->10, calls 3->11   (+8)

SMP tx/rx cpu0=10/1 cpu1=1/10
IPI irq/wake cpu0=1/1 cpu1=10/10
callbacks=1/1
```

所有 send failure、coalesced、stale 和 spurious 必须保持 0。

## 4. 当前授权与执行边界

本轮由 Claude 执行：

- team-overlay 最小实现；
- 文档同步；
- 不编译的源码级静态验证。

本轮明确不执行：

- skill；
- 编译、distclean、构建型/static verifier 或测试；
- 下载、烧写或板端命令；
- Git status、diff、commit、push 或 PR；
- 与 N8-C4 无关的源码探索。

构建、下载和真实板卡验证由用户执行。

## 5. 用户构建与板测

实现与静态复核完成后，用户执行：

```bash
cd /home/lijian/project/open-vela

AP_CONFIG_NAME=ap_smp_semwake_loop JOBS=8 \
  contest2026_135_yongwangzhiqian/board/bk7258/scripts/build_dual_image.sh
```

下载 factory image，等待约 3 秒后只执行：

```text
apctl status
```

预期新增核心输出：

```text
AP sem-loop state=PASSED(7) error=0 generation=<n> requested/completed=8/8
Sem-loop waits entered/observed/returned=8/8/8 value=-1
Sem-loop posts cpu/count/result=0/8/0 wait result/cpu=0/1
Sem-loop sequence wait/post/wake=8/8/8
Sem-loop SMP tx0=2->10 rx1=2->10 fail0=0->0
Sem-loop IPI irq1=2->10 wake1=2->10 calls=3->11
```

任意 cycle timeout、sequence 跳号、提前 post、错误 CPU、累计次数不是 8、窗口计数不是精确 `+8`、failure/coalesced/stale/spurious 增长、task 未完成或 PID 未释放，均判定 N8-C4 失败。

## 6. 已落地实现（static-only）

- `bk7258_amp.h` 新增独立 `BSWL` ABI：offset=`0x400`、magic=`"BSWL"`、version=1、fixed cycles=8；32 个 4-byte word 精确组成 `0x80` bytes。静态断言要求 `BSEM 0x380..0x3ff` 与 `BSWL 0x400..0x47f` 首尾相接且不重叠。
- `bk7258_ap_smp.c` 继续只保留一个 `pthread_create()` 和一个 file-static semaphore。唯一 CPU1-affinity task 在同一 semaphore 上执行固定 8 轮 wait；CPU0 每轮先做 exact-waiter proof，再 single post，并以 `WOKEN -> CONTINUE` handshake 串行化下一轮。
- 第 1 轮继续单独冻结 `BSEM` 的 wait/post/wake 与 `+1` 计数窗口；`BSWL` 从第 1 轮 post 前快照到第 8 轮 wake 后，要求完整窗口精确 `+8`；`BAFF` 从 task create 前到 PID release 后要求 dispatch + 8 wakes 精确 `+9`。
- 成功路径继续使用 non-intrusive PID-hash visibility check，不在 detached task exit window 获取 TCB reference。
- 失败路径先发布 loop/BAFF error/state；若首轮尚未认证，同时发布 BSEM failure，首轮已认证后的 later-cycle failure 则保留冻结的 BSEM proof。随后用 recovery `nxsem_post()` + `sev` 释放可能仍在 semaphore 或 WFE 上等待的 task；AP 最终使用 `BK7258_AP_ERROR_CPU2_SEM_WAKE_LOOP` fail-close。
- CP generation prepare 会清除 stale `BSWL` magic；`apctl status` 仅在 magic/version/size 有效时打印 loop record；新增 Kconfig、`ap_smp_semwake_loop` defconfig 和 dual-image builder allowlist，默认 cpuset 仍为 `0x1`。

## 7. 源码级静态复核

本轮只复核 N8-C4 直接相关源码和配置；未调用 skill，未执行编译、构建型 verifier、测试、Git 或板端命令。

已确认：

- `BSWL` 字段顺序与预定 32-word layout 一致，size=`0x80`；`BSEM`/`BSWL` offset 连续且在 4 KiB shared page 内；
- loop 配置仍复用 N8-C3 的唯一 pthread、static semaphore、exact waiter helper 和 non-intrusive PID-release helper；关闭 LOOP 时原 N8-C3 single-wake 分支不变；
- 每轮必须同时满足 WAITING state、cycle sequence、累计次数、目标 PID、`TSTATE_WAIT_SEM`、同一 wait object、semaphore value=`-1` 和 affinity=`0x2` 后才允许 post；
- 第 1 轮在 CPU0 发布 `CONTINUE` 前完成 BSEM `+1` 验收，因此后 7 轮不会污染 first-cycle record；BSWL/BAFF 分别要求 `+8`/`+9`，send failure 不增长，global coalesced/stale/spurious 保持 0；
- `apctl` 新增格式串与 signed/unsigned 字段逐项匹配；Kconfig symbol、defconfig directory、builder name、CP stale clear 和 AP boot error selection 一致。

静态复核发现并修复 1 个 failure-diagnostic race：CPU1 在 semaphore 已返回后可能先快照到 `POSTED`，随后 CPU0 timeout abort 发布 `FAILED`，CPU1 再把 BSEM/BSWL state 覆盖回 `WOKEN`。修复后，task 在最后一次正常 WOKEN store 后再次读取 CPU0 error-first 发布的 loop error；若 abort 已开始，就重新发布 loop 与 BAFF 的 terminal `FAILED`，并且只在 BSEM 自身 error latch 非零时重新发布 BSEM `FAILED`，从而不会把已经认证并冻结的 first-cycle proof 误判失败；若 abort 在该读取之后才开始，则 CPU0 随后的 `FAILED` store 最终生效。成功路径只增加 error==NONE 读取，不改变 semaphore、scheduler 或 IPI 计数。

两项经上下文复核不构成 blocker：

- CPU1 的 WFE continuation loop 不把自身迭代数作为 authoritative wall-clock；真正的 bounded supervisor 是 CPU0 每轮 `timeout_ms` polling，abort 同时执行 recovery post + `sev`，因此可从 semaphore wait 或 WFE 退出；
- failure path 不等待 detached PID release：selftest 每次 AP boot 只运行一次，scheduler-online restart 被 `-ENOTSUP` 拒绝，startup failure 后 CP 会硬复位两个 AP core；成功路径仍严格等待 task complete + PID released。

板测前源码级结论：N8-C4 实现与配置静态复核通过，当时状态为 `static-only`；随后用户已完成构建、下载和真实板卡验证，最终板测结论见下文。

## 8. 当前进度

- 2026-07-30：N8-C3 已在用户真实 T5-AI 当前 download/warm-start path 收口为 `board-verified`。
- 2026-07-30：用户明确选择 N8-C4，并授权 team-overlay 最小实现、文档同步和不编译静态验证；构建、下载和板测由用户完成。
- 2026-07-30：已完成 same-task 8-cycle、BSEM first-cycle preservation、BSWL full-loop attribution、per-cycle sequence handshake 和 no-expansion 实现。
- 2026-07-30：已完成 focused + adversarial 源码级静态复核，并修复 failure record 被 late WOKEN 覆盖的竞态。
- 2026-07-30：用户完成构建、下载和一次 `apctl status` 真实板测；全部 N8-C4 gate 精确命中，Stage 收口为 `board-verified`。

## 9. 真实板卡证据（board-verified）

用户在真实 T5-AI 当前 download/warm-start path 得到：

```text
AP state=READY(2) error=0 generation=1 heartbeat=85
CPU2 state=SCHEDULER_ONLINE(8) error=0 ready=1 online=00000003 calls=11 boots=1
AP SMP state=PASSED(4) error=0 requested/completed=2/2
SMP tx/rx cpu0=10/1 cpu1=1/10 coalesced=0/0 fail=0/0
IPI irq/wake cpu0=1/1 cpu1=10/10 stale/spurious=0/0

AP affinity state=PASSED(4) error=0
Affinity requested/observed=00000002/00000002
Affinity task id/cpu=3/1 started/completed/pid-released=1/1/1
Affinity SMP tx0=1->10 rx1=1->10 fail0=0->0
Affinity IPI irq1=1->10 wake1=1->10 calls=2->11

AP sem-wake state=PASSED(6) error=0
Sem-wake task id=3 wait entered/observed/value=1/1/-1
Sem-wake post cpu/count/result=0/1/0 wait returned/result/cpu=1/0/1
Sem-wake SMP tx0=2->3 rx1=2->3 fail0=0->0
Sem-wake IPI irq1=2->3 wake1=2->3 calls=3->4

AP sem-loop state=PASSED(7) error=0 requested/completed=8/8
Sem-loop waits entered/observed/returned=8/8/8 value=-1
Sem-loop posts cpu/count/result=0/8/0 wait result/cpu=0/1
Sem-loop sequence wait/post/wake=8/8/8
Sem-loop SMP tx0=2->10 rx1=2->10 fail0=0->0
Sem-loop IPI irq1=2->10 wake1=2->10 calls=3->11
```

板测判读：

- AP `READY/error=0`，CPU2 `SCHEDULER_ONLINE/error=0`、ready=`1`、online=`0x3`；
- 同一 `task id=3` 全程保持 requested/observed affinity=`0x2/0x2`、CPU=`1`，只启动/完成/释放一个 PID；
- BAFF `1->10` / calls `2->11` 精确为 `+9`，即一次 initial dispatch + 八次 semaphore remote wake；
- BSEM `2->3` / calls `3->4` 精确为 `+1`，冻结第 1 轮 remote wake proof；
- BSWL `2->10` / calls `3->11` 精确为 `+8`，八轮 wait/observe/post/return 和 wait/post/wake sequence 全部为 `8`；
- CPU0 每轮 post，CPU1 每轮返回；last semaphore value=`-1`，post/wait result=`0`；
- global SMP tx/rx 精确为 CPU0=`10/1`、CPU1=`1/10`，CPU1 IRQ/wake=`10/10`；coalesced、send failure、stale、spurious、duplicate 和 lost 均为 `0`；
- gate 后 AP heartbeat=`85`、CPU0 SysTick=`1026`、sleep enter/return=`85/84`，持续运行路径正常。

## 10. 收口结论

N8-C4 在当前 download/warm-start path 正式收口为 `board-verified`：同一 CPU1-bound task、同一 static semaphore、固定 8 轮 exact block → CPU0 single post → CPU1 remote wake 全部闭环；BSEM/BSWL/BAFF 的 `+1/+8/+9` 计数精确区分首轮、完整循环和 initial dispatch，不存在第二个 task 或额外 scheduler IPI。

保持边界不变：`CONFIG_SMP_DEFAULT_CPUSET=0x1`；不开放第二个 CPU1 task、migration、默认 cpuset `0x3`、运行时可变/无限循环或 stress test。physical cold-reset 覆盖仍是独立 open issue。N8-C4 之后的下一 MAIN Stage 尚未选择。
