# N11 — AP access to CP LittleFS through RPMsgFS

> 状态：**LATEST VERIFIED / board-verified（受限 stock RPMsgFS worker 基线）**
>
> 日期：2026-08-01（方案批准）；2026-08-02（构建与实板收口）
>
> 基线：`feat/bk7258-ap-smp`，N10 实现提交 `1bda1e8`

## 1. 问题与目标

CP 已独占 BK7258 flash、MTD 和挂载在 `/data` 的 LittleFS；AP SMP 已通过一条
generation-safe RPTUN/RPMsg link 与 CP 通信。N11 的最小目标是让 AP 使用 NuttX
原生 RPMsgFS 访问 CP 的 `/data`，同时不把文件 I/O 的阻塞传播到 AP 主循环、RPTUN
RX worker 或 CPU2 scheduler liveness 路径。

成功基线应证明：

- CP 是 filesystem/server owner，AP 是 mount/client；不反转 flash ownership；
- AP 把 CP `/data` 挂载到本地 `/cpdata`；
- 通过可重复的 CP 端命令触发 AP worker，完成 create/write/read/seek/stat/readdir/
  rename/unlink/rmdir；
- 重复运行无数据错误、无 heap 漂移，既有 RPMsg、syslog、supervisor 继续健康；
- AP restart/recover 后重新建立新 generation 的 mount/service，并再次通过测试。

## 2. 已确认事实与固定约束

### 2.1 本地 NuttX 源码事实

- `CONFIG_FS_RPMSGFS_SERVER` 由 `fs_initialize()` 自动调用
  `rpmsgfs_server_init()`；`CONFIG_FS_RPMSGFS` 提供 mount client。
- client mount 参数是 `cpu=<remote>,fs=<remote-root>`。BK7258 AP 看到的 remote
  CPU name 是 `cp`，因此目标参数为 `cpu=cp,fs=/data`。
- client endpoint 名称前缀为 `rpmsgfs-`，CP server 通过 Name Service 动态绑定；
  不需要新增静态 endpoint 地址或修改 resource table。
- 当前 RPMsg payload 为 496 bytes。RPMsgFS write header 占 32 bytes，因此单个 write
  frame 最多携带 464 bytes；stock client 会自动把更大的 write 拆帧。
- server 的 endpoint callback 直接执行 `file_open/read/write/...`，也就是运行在当前
  `bk7258-rptun-rx` worker 上。第一版必须限制文件大小、操作次数和底层 flash 延迟，
  并验证不会越过 supervisor deadline。
- stock client 的 `rpmsgfs_send_recv()` 使用无期限 `rpmsg_wait()`。device destroy 只
  destroy endpoint，没有统一追踪并唤醒所有在途 stack cookie；mount 的 `timeout=`
  也不能为已经进入同步请求的调用提供逐请求 deadline。
- server endpoint release 会关闭该 peer 遗留的 file/dir handle，CP 侧资源能在 AP
  RPTUN device destroy 时回收。

### 2.2 项目约束

- 不修改官方 NuttX 或 Beken SDK 源码；永久实现只放在 board/app wrapper。
- 不触碰或纳入并行进行的 `bk7258_qemu`/`qemu-bk7258` 工作。
- CP 保持 LittleFS `/data` 的唯一 owner；AP 不直接访问 raw flash、MTD 或 block device。
- 不扩大 32 KiB carveout，不修改 0x40-byte RPTUN control ABI，不新增第二条 RPMsg link。
- RPTUN IRQ/RX owner 继续固定在 AP logical CPU0；N11 第一版不开放 CPU1 直接执行
  RPMsgFS POSIX 调用。
- 自动 AP recovery 仍保持默认关闭；N11 复用已经验证的人工 generation recovery。

## 3. 范围基线

### 3.1 Actors 与主流程

1. CP 启动、挂载本地 `/data`，同时作为 stock RPMsgFS server。
2. AP 注册 stock RPMsgFS client mount `/cpdata`，但不在启动关键路径执行文件 I/O。
3. CP NSH 用户执行 `bkrpmsgfstest`。
4. CP 端测试 endpoint 只发送有 generation/sequence 的请求并 bounded wait。
5. AP endpoint callback 只校验并投递请求；固定在 logical CPU0 的专用 worker 执行
   `/cpdata` 文件操作，再通过 endpoint 返回结构化结果。
6. CP 输出机器可判定的 `BRFS BEGIN/STEP/HEAP/PASS|FAIL` 证据。

### 3.2 In scope

- board Kconfig 统一选择：CP `FS_RPMSGFS_SERVER`，AP `FS_RPMSGFS`；
- AP `/cpdata` mount 与独立、串行的文件服务 worker；
- CP/AP 独立测试 endpoint、bounded control wait 和 generation validation；
- `/data/rpmsgfs-n11/` 下的幂等测试数据，启动时清理同名 stale artifact；
- 1、64、464、1024-byte 数据的 CRC/read-back；
- 重复测试、现有 `bkrpmsgtest` 回归、warm restart 和 supervisor manual recovery。

### 3.3 Out of scope

- AP 任意线程直接调用 RPMsgFS；AP logical CPU1 文件 I/O；
- 为 stock RPMsgFS 增加通用 per-operation timeout 或取消语义；
- 自研文件协议、修改 NuttX RPMsgFS、修改 SDK；
- `rpmsgmtd`、`rpmsgblk`、cache-on、大文件吞吐和零拷贝；
- BLE、Wi-Fi、QEMU、默认自动恢复。

## 4. 关键故障语义

推荐的第一版边界是：RPMsgFS POSIX 调用只允许发生在可丢弃的 AP 专用 worker 中。
CP 端控制命令始终 bounded；如果链路在 stock RPMsgFS 调用进行中断开，CP 命令超时并
报告错误，supervisor/manual recover 重启 AP，卡住的 AP worker 随 generation 一起
回收。不得让 AP main、health worker、RPTUN RX worker 或 logical CPU1 业务线程直接
承担该无期限等待。

这保证 CP 管理面和恢复路径 fail-closed，但**不声称**旧 generation 中的 stock
RPMsgFS POSIX 调用能在 AP 重启前自行返回 `-ENOTCONN`。如果产品要求该调用本身具备
逐请求 deadline，必须改用 board-owned 文件 RPC 或推动上游 NuttX 增加 cancel/timeout；
两者都超出当前最小范围。

## 5. 方案比较

| 方案 | 正确性与恢复 | 实现/维护成本 | 与“不改 NuttX”一致性 | 结论 |
|---|---|---|---|---|
| A. stock RPMsgFS，AP 任意线程直接使用 | 在途断链可能永久阻塞并扩散到业务线程 | 最低 | 一致 | 拒绝 |
| B. stock RPMsgFS + AP 专用 worker + bounded 控制面 | 卡住只影响可随 AP generation 回收的 worker；CP 保持可恢复 | 中 | 一致 | **推荐** |
| C. board-owned 文件 RPC/CP deferred worker | 可自定义完整 timeout/cancel，隔离最好 | 高，重复实现协议/VFS 映射 | 一致但偏离原生服务 | 仅在 B 的边界不可接受时采用 |

推荐 B。反转条件是：必须让 AP 普通调用者在不重启 AP 的情况下获得确定超时，或者
实板证明 CP RX worker 中的 LittleFS 操作会破坏 health/RPMsg latency deadline。

## 6. 生命周期与不变量

AP wrapper 状态：

```text
UNINIT -> MOUNTED -> ENDPOINT_READY -> IDLE <-> RUNNING
                         |              |
                         +-----> STALE/FAILED --(new AP generation)--> UNINIT
```

- 所有 request/result 都携带当前 RPTUN generation 和单调 sequence。
- endpoint callback 不做 mount、VFS I/O、等待 TX buffer 或 lifecycle 操作。
- 同一 generation 只允许一个文件测试运行；并发请求返回 `-EBUSY`。
- worker 开始和结束均检查 control magic/version/generation/state=`CONNECTED`。
- 测试目录限定为 `/cpdata/rpmsgfs-n11`，不读写 `/data/probe.txt`。
- CP server 的同步 VFS callback 必须在小文件门禁中保持低于 supervisor confirmed-fault
  deadline；实板以 health age、RX sequence 和零新增 fault 验证，不靠放宽 timeout。
- AP 新 generation 重新 mount/注册；CP server 的旧 endpoint release 负责关闭遗留
  file/dir handle。

## 7. 验收矩阵

1. **Build/static**：CP/AP fresh dual build、SDK checksum、ELF/layout、undefined symbol、
   stock RPMsgFS client/server object ownership和配置角色全部 PASS。
2. **Basic**：mkdir、1024-byte split write/read/CRC、seek、stat、rename、readdir、unlink、
   rmdir 全部 PASS，测试目录最终不存在。
3. **Boundary sizes**：1/64/464/1024 bytes，各自 read-back 与 CRC 一致。
4. **Repeat/heap**：固定 worker 连续 20 轮，AP 与 CP heap snapshot 无持续漂移。
5. **Coexistence**：测试前后 `bkrpmsgtest all 100 60000` PASS；supervisor 保持
   `HEALTHY`、pending=0、fault count 不增加。
6. **Warm restart**：generation N→N+1 后重新建立 mount/endpoint，basic test 再次 PASS。
7. **Fault/recover**：在 N11 命令运行或等待期间制造 RPMsg fault，CP 命令在自身 deadline
   内失败；manual recover 后新 generation 测试 PASS，CP 不死锁。
8. **Preserved baseline**：syslog probe、AP/CPU2 heartbeat、N10 health/recover 和
   LittleFS 本地 probe 保持通过。

## 8. 不确定性账本与审批 gate

| ID | 类型 | 内容 | 影响 | 解决条件 |
|---|---|---|---|---|
| N11-U1 | 待确认决策 | 是否接受“在途 stock POSIX 调用由 AP restart 回收，而非逐请求本地超时” | 决定采用方案 B 或 C | 用户明确确认 |
| N11-U2 | 验证任务 | CP RX worker 内 LittleFS 最大操作延迟是否影响 health/其他 endpoint | 决定 B 是否保留 | 小文件实板并发/健康证据 |
| N11-U3 | 可逆假设 | v1 worker 固定 AP logical CPU0，CPU1 direct client 延后 | 决定首版 SMP 覆盖 | v1 收口后单独评估 |
| N11-U4 | 可逆假设 | `/cpdata` 与 `/data/rpmsgfs-n11` 命名可接受 | 只影响 wrapper/测试路径 | 实现前可改名 |

### Baseline approval

- 状态：**APPROVED**
- 批准：用户于 2026-08-01 明确确认方案 B 和 N11-U1 的受限故障语义。
- 实现授权：进入 board/app wrapper 实现、构建和实板 gate；仍禁止修改 NuttX/SDK
  或纳入 QEMU 工作。

## 9. Proposed ADR-N11-1

- **Status：**Accepted
- **Decision：**CP 使用 stock RPMsgFS server；AP 使用 stock client，但仅由
  board-owned logical-CPU0 worker 执行文件操作。CP 控制面 bounded，AP restart 是
  stock client 在途阻塞的回收边界。
- **Positive consequences：**复用 NuttX 原生协议；不改 NuttX/SDK；flash ownership
  清晰；故障隔离不影响 CP 管理面。
- **Negative consequences：**AP 普通线程不能直接使用；在途调用不会在旧 generation
  内保证返回；CP RX worker 仍会同步执行小文件 VFS 操作。
- **Validation：**执行第 7 节全部门禁；任何 CP 管理面死锁、supervisor 假故障或无法
  generation-reconnect 都使该 ADR 回到 Proposed 并评估方案 C。

## 10. 实现结果

N11 按 ADR-N11-1 落在项目自有 board/app wrapper 内，没有修改官方 NuttX 或 Beken
SDK 源码，也没有触碰并行的 QEMU 工作：

- CP profile 选择 stock `CONFIG_FS_RPMSGFS_SERVER`，AP profile 选择 stock
  `CONFIG_FS_RPMSGFS`；`chip/common/bk7258_rpmsgfs.c` 在 AP 把 CP `/data` 挂载为
  `/cpdata`；
- `chip/common/bk7258_rpmsgfs_test.c` 实现 generation/sequence 校验、bounded CP
  控制面和 AP logical CPU0 专用 worker。endpoint callback 只投递请求，不执行文件
  操作；
- `app/hello_app/bk7258_rpmsgfs_test_main.c` 提供 `bkrpmsgfstest`，输出可机器判读的
  `BRFS BEGIN/RESULT/HEAP/PASS|FAIL`；
- 文件门禁覆盖 mkdir、open/write/fsync/seek/read/stat、rename、opendir/readdir、
  unlink/rmdir，并在每轮完成后清理 `/data/rpmsgfs-n11`。

### 10.1 BK7258 exclusive-monitor 内存约束

首轮实板出现 payload=1 PASS、payload=64 以 `transport=-107` 失败。临时 scheduler
trace 与 J-Link 证据把根因收敛到 AP SMP 静态锁的独占访问冲突：CPU0 SysTick 持有
`g_schedlock` 时，CPU1 仍能覆盖/获取该锁。该问题不是 RPMsgFS 协议错误。

官方 v3.1.1.9 SDK 的
`ap/middleware/driver/spinlock/spinlock.c` 明确说明 BK7236/BK7258 SRAM 只有两个
exclusive monitor，静态 spinlock 必须放在 `0x28000000` 起始的专用 64 KiB
`.sram_spinlock_section`。因此 board wrapper 保留：

```text
0x28000000..0x2800ffff  AP exclusive-state / static-lock region
0x28010000..0x2804ffff  CP RAM
0x28050000..             AP/RPTUN/telemetry 既有布局（边界不变）
```

AP linker 新增 `.spinlock_data` 和 `.spinlock_bss`；启动代码在释放 physical CPU2 前
复制初始化段并清零 BSS 段。兼容性 JSON 与 layout verifier 对 14 个对象逐符号检查，
包括 SDK mailbox/critical lock、NuttX scheduler/IRQ/PID/signal/timer/SMP locks、board
RPTUN/SDK IRQ/RAM-vector locks以及 `g_bk7258_ap_smp_pending`。最终 ELF 为：

```text
.spinlock_data  0x28000000  size 0x14
.spinlock_bss   0x28000014  size 0x34
exclusive state total       0x48 bytes
```

诊断用 scheduler trace、linker wrap 和临时 Kconfig 已全部移除；永久改动仅是通用内存
约束、启动初始化和可复验 manifest/verifier。

### 10.2 100 Hz 下的 bounded wait 修正

故障态首次运行暴露第二个独立问题：wrapper 用 `nxsig_usleep(1000)` 后自增“毫秒”
计数。在当前 100 Hz 配置中，1 ms sleep 向上取整为一个 10 ms tick，使名义 3000 ms
的 endpoint gate 实际约为 30 s，500 ms send gate 也同样失真。

board wrapper 已改为记录 `clock_systime_ticks()`，使用 `MSEC2TICK()` 比较真实 elapsed
ticks；不再假定每次 sleep 恰好 1 ms。fresh dual build、layout verifier 与实板故障态
复验均通过。

## 11. 实板证据（2026-08-02）

### 11.1 构建与静态门禁

- `cp_nsh_rptun + ap_smp_rptun` fresh dual build PASS；v3.1.1.9 SDK checksum 与
  `verify_bk7258_rptun_layout.py` PASS；`git diff --check` PASS；
- CP raw/CRC SHA-256：`967b7abe...2aea95` / `28a3a7fe...acabd`；
- AP raw/CRC SHA-256：`a35c1b8e...3211f` / `7ee0edc3...95d7`；
- factory image SHA-256：`cf6c3941...ec848`；
- 稀疏刷写保留 LittleFS，启动日志
  `logs/bk7258-auto-debug/20260802-042914/` 判定 `PASS_NSH`。

### 11.2 正常路径与回归

- 根因最小修复镜像连续完成三轮完整 RPMsgFS suite，12 个 payload group、240 次
  文件事务全部 PASS；证据：
  `logs/n11-rpmsgfs-spinlock-region-20260802-0358/`；
- 移除诊断并推广全部 exclusive-state 对象后，1/64/464/1024 bytes 各 20/20，
  `BRFS SUITE PASS`，随后 `BRPT SUITE PASS runs=6`；证据：
  `logs/n11-rpmsgfs-final-20260802-0417/`；
- 最新 timeout-fix 镜像在 generation=2 恢复后，1/64/464 各 20/20，单独补录的
  1024-byte 档也是 20/20、written/read=`20480/20480`；AP 与 CP heap snapshot
  前后相同；
- 原验收参数 `bkrpmsgtest all 100 60000` 六场景全部 PASS。每个 logical CPU 在
  payload 1/64/464 × idle/load 中均 `sent=received=100`、`errors=0`，所有 heap
  snapshot 稳定；syslog probe 也在 generation=2 回传；
- CP 本地 `cat /data/probe.txt` 仍输出 `BK7258LFS-OK`，证明原 LittleFS ownership 与
  持久化基线未被破坏。

最新一轮完整原始日志位于：
`logs/n11-rpmsgfs-timeoutfix-20260802-0430/`。

### 11.3 故障、快速失败与恢复

1. 健康起点为 generation=1：AP `READY`、RPTUN `CONNECTED`、supervisor
   `HEALTHY`、CPU2 `SCHEDULER_ONLINE`；
2. `apctl inject rpmsg` 后准确收敛到 RPTUN `FAULTED/error=110` 和 supervisor
   `FAULTED/RPMSG_TIMEOUT`，同时 AP primary/CPU2 heartbeat 继续推进；
3. 在已收敛的 FAULTED 状态执行 `bkrpmsgfstest run 1 64 5000`，10 s 采集窗内得到
   `BRFS FAIL transport=-107` 并重新出现 NSH prompt。旧实现同场景 20 s 内不返回；
4. `apctl recover 30000` 完成 generation 1→2，随后恢复为
   `READY/CONNECTED/HEALTHY`，fault/recovery=`1/1`；
5. 恢复后再次通过四档 RPMsgFS、100-count 六场景 RPMsg、syslog 与本地 LittleFS
   probe。最终 pending=`0/0`、consecutive=0、injection=`NONE`，全部 SMP
   fail/coalesced/stale/spurious 计数为 0。

关键日志分别是 `fault-status.raw`、`rpmsgfs-fail-closed.raw`、`recover.raw`、
`recovered-status.raw`、`rpmsgfs-after-recover.raw`、
`rpmsgfs-1024-after-recover.raw`、`rpmsg-regression-100-after-recover.raw`、
`syslog-after-recover.raw`、`littlefs-local-probe.raw` 和
`final-status-after-all.raw`。

## 12. 验收结论与剩余边界

| 门禁 | 结果 |
|---|---|
| Build/static、SDK checksum、ELF/layout | **PASS** |
| 文件语义与 1/64/464/1024 边界 | **PASS** |
| 20 轮 repeat、AP/CP heap 稳定 | **PASS** |
| `bkrpmsgtest` 6×100、syslog coexistence | **PASS** |
| generation 1→2 manual recovery 后重新 mount/service | **PASS** |
| 已故障链路上的 CP 命令 bounded fail-closed | **PASS** |
| AP/CPU2 liveness、N10 health、CP local LittleFS | **PASS** |

N11 的受限方案 B 基线据此标记为 `board-verified`。仍需保留两项边界：

- 本轮没有把故障精确注入到一个正在执行的 stock RPMsgFS POSIX 调用中；验证的是故障
  已收敛后 CP 命令能够 bounded fail-closed，以及 manual recovery 后新 generation
  完整恢复；
- stock `rpmsgfs_send_recv()` 仍无 per-operation cancel/timeout。在途 AP POSIX 调用
  断链时仍以 AP restart 作为回收边界，不声称它能在旧 generation 自行返回。

若后续产品要求“正在阻塞的普通 AP 调用者不重启也必须按 deadline 返回”，应新开
Stage 评审 board-owned 文件 RPC 或上游 NuttX timeout/cancel，而不能扩大本 N11 的
已验证结论。
