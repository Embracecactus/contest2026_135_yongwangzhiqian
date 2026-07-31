# BK7258 Stage N9：CP/AP RPTUN/OpenAMP/RPMsg 计划

> 日期：2026-07-31
> 状态：**COMPLETED / `board-verified` / wrapper integration**
> 前置基线：N8 AP NuttX SMP 与 physical-reset closure 已 `board-verified`
> 本文状态边界：N9-R..N9-V 已完成；RPTUN/RPMsg、Name Service、SMP 双 producer、
> reconnect 和 `syslog_rpmsg` 已构建并在真实 T5-AI 板端验证
> 架构调研前置文档：[CP/AP 双 NuttX 与 RPTUN/RPMsg 架构探索](../cp-ap-rptun-architecture-research.md)
> 评审处置记录：[N9 计划评审处置记录](../n9-plan-review-2026-07-31.md)
> 源码复核记录：[N9 RPTUN source verification](../n9-rptun-source-verification.md)

## 1. 结论先行

N9 采用的不是 “SMP 或 OpenAMP” 二选一，而是两者分层组合：

```text
物理 CPU0 / CP
  team-owned NuttX UP
  RPTUN master
        │
        │ shared-memory vring/buffer + 独立 mailbox event
        ▼
物理 CPU1 + CPU2 / AP
  一个 team-owned NuttX SMP 实例
  RPTUN remote
  logical CPU0 = physical CPU1：CP/AP mailbox IRQ 与 RPTUN RX worker owner
  logical CPU1 = physical CPU2：普通 SMP workload，不是第二个 RPTUN peer
```

- **SMP** 解决 AP 镜像内部 physical CPU1/CPU2 的统一调度、IPI、同步与任务迁移；N8
  已经完成，不在 N9 换方案。
- **OpenAMP/RPTUN/RPMsg** 解决两个独立 NuttX 实例之间的生命周期和消息通信；N9
  现在接入。
- CP 只看到一个 AP remote processor / RPMsg peer。AP logical CPU1 不是独立固件，
  不建立第二套 resource table、vring 或 RPTUN instance。
- 不把 CPU0/1/2 合并成三核 SMP，不把 AP 从 SMP 改成 BMP。
- 接入方式遵循官方 SDK 的 **wrapper 模式**：SDK public API/静态库和官方 NuttX
  均保持只读；board overlay 封装 mailbox logical channel、shared-memory ABI、RPTUN
  lower-half、CPU0 gateway 和 lifecycle policy。wrapper 不把厂商 `mb_ipc` 私有协议
  夹在 RPTUN 与 mailbox 之间。

## 2. NuttX 可用的多处理器方案调研

### 2.1 方案对比

| 方案 | NuttX 中的含义 | BK7258 适用位置 | N9 决策 |
|---|---|---|---|
| Native SMP | 一个 OS 实例统一管理同构 CPU、全局调度和共享内核资源 | AP physical CPU1+CPU2 | **保留，N8 已验证** |
| BMP | 一个 OS/共享资源，但应用线程按 CPU 绑定，减少跨核迁移和同步复杂度 | 可作为特殊的固定核调度模型 | 不切换；在 SMP 内对 transport worker 显式 affinity 即可 |
| OpenAMP + RPTUN + RPMsg | 独立 OS/固件实例之间的 remoteproc 生命周期、virtio 和消息通道 | CP NuttX UP ↔ AP NuttX SMP | **N9 接入** |
| 自定义 mailbox IPC / SDK `mb_ipc` | 厂商私有消息协议和服务层 | 可作 doorbell/HAL 参考 | 只复用底层 mailbox 能力，不复制上层私有协议 |
| 三核单实例 SMP | CPU0/1/2 全部归一个 NuttX scheduler | 与当前 CP/AP 双镜像、内存和服务 owner 冲突 | 不采用 |

NuttX 当前树的 `sched/Kconfig` 将 UP、SMP、BMP 作为互斥调度模型。BMP 的价值是固定
CPU 归属，不是 CP/AP 独立固件通信；`CONFIG_RPTUN_BMP` 也不意味着 RPTUN 本身提供
SMP scheduler。当前 AP 需要保留 N8 已验证的 controlled migration、双向 remote wake
和 timed wake，因此切换 BMP 会失去现成基线且没有收益。

### 2.2 可复用的开源参考

| 参考 | 可借鉴内容 | 不可直接照搬之处 |
|---|---|---|
| NuttX RP2040 SMP | 双核 MCU 的 `up_cpu_start()`、FIFO/IPI、SMP call 和 shared SRAM 思路 | Cortex-M0+，寄存器与异常模型不同 |
| NuttX ESP32/ESP32-S3 SMP | 双核 scheduler、cross-core interrupt、CPU affinity 的成熟组织方式 | Xtensa/RISC-V SoC，不是 Cortex-M33 |
| NuttX nRF53 RPTUN | 双 M33 域、静态 resource table、ISR→semaphore→kthread 延迟处理 | 内存和 IPC 外设不同 |
| NuttX STM32H7 RPTUN | HSEM doorbell、共享 vring、master/remote lower-half | M7/M4 非同构组合，cache 属性不同 |
| NuttX K230/MX8MP RPTUN | 多 remote/channel 和更复杂 SoC 的 RPTUN 集成 | IPC abstraction 的回调上下文不同 |
| 当前 BK7258 N8 | AP SMP 启动、IRQ79、SDK mailbox wrapper、affinity 与 lifecycle 实测基线 | 尚无 CP/AP RPMsg transport |

当前 checkout 中没有找到一个可直接复制、且同一 defconfig 显式同时开启
`CONFIG_SMP=y` 与 `CONFIG_RPTUN=y` 的板级模板。这不是框架不兼容的证据，而意味着
BK7258 必须组合使用：

1. 当前 N8 AP SMP 实现和 RP2040 等 native SMP 参考；
2. nRF53/STM32H7 的 RPTUN shared-memory 与 deferred-worker 模式；
3. NuttX RPTUN/OpenAMP core 的锁、resource table 和 lifecycle 约束。

官方概念入口：

- [NuttX SMP](https://nuttx.apache.org/docs/latest/reference/os/smp.html)
- [NuttX RPTUN overview](https://nuttx.apache.org/docs/latest/components/drivers/special/rptun/index.html)
- [NuttX RPTUN architecture](https://nuttx.apache.org/docs/latest/components/drivers/special/rptun/architecture.html)
- [NuttX OpenAMP](https://nuttx.apache.org/docs/latest/components/openamp.html)

## 3. SMP 与 OpenAMP 的并发边界

### 3.1 可以组合，但职责不同

NuttX SMP scheduler 不会替 RPTUN 做跨镜像通信；RPTUN 也不会给 AP 两个 CPU
做负载均衡。OpenAMP/libmetal 在 NuttX 下使用 mutex、spinlock、atomic 和 memory
barrier，能够为同一 OS 实例内的并发调用提供基础保护，但 BK7258 的组合仍需通过
专门门禁验证，不能只凭源码锁就标记为板端支持。

初始线程归属固定为：

- AP CP↔AP mailbox IRQ：只路由到 AP logical CPU0 / physical CPU1。
- IRQ handler：只完成硬件确认、读取/合并 pending event、原子记账和
  `nxsem_post()`；不调用 OpenAMP。
- `bk7258-rptun-rx` kthread：显式 affinity `0x1`，被唤醒后调用 RPTUN callback。
- RPTUN notification callback 在该 worker 运行；普通 RPMsg endpoint callback 由当前
  NuttX RPMsg workqueue 执行，只有 `RPMSG_PRIO_RT` endpoint 可能 inline。
- 首版禁止业务 endpoint 使用 `RPMSG_PRIO_RT`；普通 callback 只能
  validate/copy/enqueue/return，慢服务另建有界 queue/worker。
- AP logical CPU1 / physical CPU2：后续允许普通任务调用 RPMsg send API，但不拥有
  CP/AP mailbox IRQ，也不建立第二个 remote peer。

### 3.2 禁止在 hard IRQ 中直接调用 RPTUN callback

NuttX RPTUN callback 最终进入 `remoteproc_get_notification()`；OpenAMP/libmetal 和
NuttX RPMsg 路径中存在 mutex/rwsem 等可阻塞同步。BK7258 lower-half 必须采用 nRF53/
STM32H7 已使用的 deferred-worker 模式：

```text
mailbox IRQ
  └─ ack/capture/atomic pending/nxsem_post
       └─ pinned kthread
            └─ lower-half callback
                 └─ remoteproc_get_notification()
                      └─ virtqueue/RPMsg endpoint callback
```

任何“直接从 ISR 调 callback、因为当前测试看起来能跑”的实现都不得通过 N9 review。

## 4. 共享内存候选与硬门禁

### 4.1 当前内存事实与官方兼容尾区

| 区域 | 当前范围 | 说明 |
|---|---|---|
| CP SRAM | `0x28000000..0x2804ffff` | CP image/heap |
| AP SRAM | `0x28050000..0x2809efff` | AP image/heap/CPU2 stack |
| team telemetry records | `0x2809f000..0x2809f6ff` | N7/N8 boot/fault/IPI/SMP/test ABI；静态断言止于 offset `0x700` |
| vendor `PWR_MNG` compatibility | `0x2809f700..0x2809f7ff` | SDK 固定地址 PM state，必须保留 |
| vendor `SWAP`/tail compatibility | `0x2809f800..0x2809ffff` | 首版保留，不并入 heap/telemetry |

团队 linker 是明确的 team-owned 双 NuttX 重分区，不沿用官方 FreeRTOS
`AP_SPINLOCK/AP_RAM/CP_RAM` 划分。分区不同本身不是冲突，但 SDK archive 内所有
固定地址以及 `.sram_spinlock_section`、`.swap_data` 必须按最终 ELF 逐项审计。

顶部 4 KiB 兼容页不能塞入 resource table、两个 vring 和 RPMsg buffers。N9-A
的**保守候选**仍是：

```text
CP_AP_RPMSG carveout candidate:
  0x28097000..0x2809efff
  size = 32 KiB
```

这个地址尚未冻结。当前 AP ELF 的 `_ebss` 约为 `0x28051ffc`，AP heap 上界约为
`0x2809e7fc`，CPU2 2 KiB interrupt/probe stack 位于顶部，因此从 AP 高地址收缩 heap、
下移 CPU2 stack 并留出 32 KiB 在容量上可行，但必须先经过 linker map 和静态断言。

### 4.2 N9-A 通过条件

- CP/AP linker script 对同一 carveout 地址、长度、alignment 得到一致结果。
- AP image、BSS、heap、CPU2 MSP/IDLE stack、shared telemetry 和 RPMsg carveout 无重叠。
- CP 能映射 carveout，但不得把它并入 CP heap。
- `AP_SPINLOCK` 不作为硬件保留区照搬；但任何残留 `.sram_spinlock_section` 必须被
  team linker 显式放入 AP-owned 区域或从 archive 依赖中移除，禁止 orphan 到 CP RAM。
- `PWR_MNG`/`SWAP` 兼容尾区和全部 SDK absolute-address 引用有 machine-readable
  allowlist；出现未审计 literal/section 即构建失败。
- resource table、control header、两个 vring、descriptor、buffer pool 的每个
  offset 均有编译期断言。
- packer/manifest verifier 检查 CP/AP image 和 shared region 边界。
- 第一版继承当前已验证 AP contract：`CCR.DC=0`，MPU region 15 将
  `0x28000000..0x3fffffff` 配为 Inner Shareable Normal Non-cacheable；CP 侧也必须
  在 N9-A 输出等价的 cache/MPU 证据。保留 DMB/DSB；cache clean/invalidate hook
  只为未来 cache-on 准备，首版不把两种模式混写。
- 地址只在上述 gate 全绿后从 “candidate” 改为 “frozen ABI”。

N9-R/A 必须提供由当前 C headers 驱动的 layout calculator，而不是手算：

```text
sizeof(struct bk7258_rptun_control_s)
+ ALIGN_UP(sizeof(struct rptun_rsc_s), align)
+ 2 * ALIGN_UP(vring_size(num_desc, align), align)
+ num_desc * h2r_buf_size
+ num_desc * r2h_buf_size
+ allocator/header/padding（按当前 rptun.c 实际路径）
```

calculator 输出每段起止、alignment、used/spare，并用同一常量生成 C
`static_assert` 和 verifier 输入。候选参数仍为两个 vring、每环 8 descriptors、
512-byte buffer、8-byte alignment；32 KiB 只有在精确计算通过且保留增长余量后才能
冻结。syslog 大消息本身不会自动增大 carveout，它会受 RPMsg payload 限制；真正改变
容量的是 descriptor 数、单 buffer 大小和 allocator overhead。

## 5. Resource table、角色和启动顺序

### 5.1 角色冻结规则

- CP：boot/control master **candidate**，负责准备 shared control header、resource table、
  generation 和 vring 状态；N9-R 完成 role XOR 验证后才改为 frozen。
- AP：单一 remote candidate，在 AP scheduler 和 logical CPU1 已 online 后注册
  lower-half。
- 首版保持 team-owned `apctl` 为 AP reset/restart owner，并配置
  `CONFIG_RPTUN_AUTO_RESET_DISABLE=y`，避免 RPTUN 自动 reset 与既有 lifecycle 冲突。
- 当前 RPTUN core 的两端都会调用 `get_resource()`；`is_master()` 只控制
  lower-half `config/start/stop`，并与 resource table role flag 异或得到 virtio
  DRIVER/DEVICE。计划不得再把它描述为“谁调用 `rptun_init_mem()`”。
- CP 是 resource/control memory 的唯一 author。AP `get_resource()` 只能在
  `magic/version/size/generation/state/CRC` 全匹配后返回，等待必须有 deadline。
- virtio role flag、feature bits、notify ID 和 resource table ABI 必须在 N9-R
  对当前 checkout 的 OpenAMP 源码逐符号冻结，不能凭别的平台常量猜测。

### 5.2 预期启动序列

1. CP 启动并初始化 CP/AP mailbox 基础设施。
2. CP 将 control state 置 `PREPARING`，递增 generation（跳过 0），清理 carveout，
   初始化 resource table/vring，发布 CRC 和 `TABLE_READY`，再释放 AP。
3. AP 完成现有 boot、CPU2 secondary bring-up、scheduler online 和 N8 baseline gate。
4. AP 带 deadline 轮询并校验 control header；成功后注册 RPTUN remote lower-half，
   启动 pinned RX worker。旧 generation 或半初始化 table 一律拒绝。
5. CP 注册并启动 RPTUN master。
6. 双方完成 virtio/RPMsg nameservice；第一服务只启用固定次数 `RPMSG_PING`。
7. transport gate 通过后再开启 `RPMSG_CHAR`，最后才接 syslog/heartbeat/rpmsgfs 等服务。

reset/restart 顺序必须显式定义：

```text
block new sends
→ quiesce endpoint workers
→ `RPTUNIOC_STOP` 移除 virtio/RPMsg device 和 endpoint，注销 callback
→ clear stale mailbox/pending notify
→ bump generation and rebuild shared state
→ restart AP
→ `RPTUNIOC_START` 重新 probe transport/endpoints
```

旧 generation 的 notify 和 descriptor 必须被拒绝或安全清理。

### 5.3 Mailbox 物理 ABI 硬门禁

当前源码已经确认：

```text
controller = MBOX0 @ 0x41000000
physical destination channels = CPU0 / CPU1 / CPU2
total FIFO entries = 8
SMP FIFO split = 2 / 3 / 3
AP internal SMP command discriminator = mbox0_message_t.data[1] == 0
```

N7 lifecycle raw-register path和 N8 SDK FIFO path 实际拥有同一控制器。N9 不得在其上
再叠加第三套独立 owner。N9-R 必须输出并冻结：

- 唯一 device init、FIFO layout、IRQ attach/enable 和 ISR-drain owner；
- pre-FIFO boot/fault handoff，或将 lifecycle control 迁移到统一 FIFO ABI；
- `data[1] == 0` 永久保留给 AP 内部 SMP IPI；
- CP↔AP control/RPTUN 的非零 message type、vqid 编码和 generation 校验；
- FIFO full 时的 shared pending-bit/coalescing 和有界 kick-retry 策略。

RPTUN core 当前忽略 lower-half `notify()` 的返回值，所以 lower-half 不能在 FIFO full
时简单返回错误并丢 kick。首版使用 shared pending bitmask 作为 truth，mailbox 只作
edge trigger；worker drain 后再次检查 pending，直到稳定为空。

### 5.4 Worker 调度与 callback policy

- CP/AP transport RX kthread 显示名统一为 `bk7258-rptun-rx`；源文件名保持
  `bk7258_rptun_cp.c` / `bk7258_rptun_ap.c`。
- AP worker：`SCHED_FIFO`、affinity `0x1`、初始 priority
  `CONFIG_BK7258_RPTUN_RX_PRIORITY=225`；高于普通应用/NSH 和默认 224 的 RPTUN/RPMsg
  worker，但单次 drain 有 quota，禁止忙循环。
- RPTUN core thread 与普通 RPMsg workqueue 的当前默认 priority 均为 224；N9-R
  必须把实际生成配置记录在证据中。
- 业务 endpoint 不使用 `RPMSG_PRIO_RT`；普通 callback 不做 UART/FS/blocking I/O，
  只入有界队列。syslog/rpmsgfs 等慢服务各自定义 worker、queue depth 和
  drop/backpressure policy。
- N9-E 在 CPU0 固定负载下测量 latency percentile 和 timeout，不以“功能还能跑”
  代替调度验证。

### 5.5 Fail-closed 和 send contract

`CONFIG_RPTUN_AUTO_RESET_DISABLE=y` 只关闭自动 reset，不保证 blocking
`rpmsg_send()` 在 peer crash 后按总 deadline 返回。首版 team service API 必须：

1. send 前校验 peer state/generation；
2. 使用 try-send/nonblocking buffer 获取配合有界 retry，或由协议层设置总 deadline；
3. peer heartbeat/generation 失效后拒绝新 request，返回 `-ENOTCONN`；
4. pending request 在 `CONFIG_BK7258_RPTUN_PEER_TIMEOUT_MS` 内以
   `-ETIMEDOUT/-ENOTCONN` 结束；
5. N9-C 即验证 absent/crashed peer，不等到 N9-D 才首次处理错误。

## 6. N9 分阶段执行计划

| 子阶段 | 工作 | 退出条件 |
|---|---|---|
| **N9-R** | Review/Role/Resource 严格前置：冻结版本；逐符号追踪 role/resource/notify/stop；冻结 MBOX0 owner/FIFO/type；审计官方 issue/PR | checklist 全绿并产出 source-verification 文档；顺序只能 `R→A→B` |
| **N9-A** | 精确 layout calculator；32 KiB carveout 候选；官方/团队内存 compatibility matrix；CP/AP linker、CPU2 stack/heap、静态断言、map/packer verifier | fresh 双镜像构建零 overlap；PWR/SWAP/vendor section allowlist 全绿；地址才冻结 |
| **N9-B** | 统一 MBOX0 owner；迁移/交接 lifecycle event；AP IRQ→sem→CPU0-pinned worker；raw notify/counter | channel/type/vqid 无 SMP/lifecycle 冲突；双向固定次数无丢失/重复/stale；N8 gates 保持 |
| **N9-C** | 接入 RPTUN/OpenAMP；static resource table/control header、两个 vring、nameservice、固定次数 ping/echo；absent/crash fail-closed | endpoint 建立，双向 payload/sequence 完整；所有 request 在配置 deadline 内结束 |
| **N9-D** | 与 `apctl` 集成 stop/restart/reconnect；AP/RPTUN 双状态机；generation/stale-event/endpoints 重建 | warm restart 固定轮数恢复 endpoint，pending request 正确失败，无旧消息串入 |
| **N9-E** | SMP 组合验证：AP logical CPU0/1 分别发消息；RX 仍固定 CPU0；CPU0 fixed-load、并发 send/teardown、性能基线 | CPU1 send 可用、RX affinity 不漂移、latency 有界、无死锁/泄漏/越界 |
| **N9-F** | 按价值逐个接服务：syslog/heartbeat，随后评估 `rpmsgfs`/`rpmsg_char` | 每个服务独立 Kconfig、独立回退、独立证据 |
| **N9-V** | focused driver review、静态 verifier、双版本构建、warm/RESET 回归 | NuttX 官方树零改动；latest SDK 与 legacy 回退边界清楚；板端证据归档 |

每一子阶段先更新本文 worklog 再进入下一个技术动作。失败时保留最小可复现证据，
不跨过未通过的 gate。

### 6.1 N9-R source-verification checklist

已产出 `nuttx-port/n9-rptun-source-verification.md`，逐项记录：

- [x] 当前 NuttX/OpenAMP/libmetal commit、generated/manual source 选择和配置；
- [x] `rptun_ops_s` 每个回调的 caller、线程/IRQ context、返回值是否被使用；
- [x] `is_master()`、resource table role flag 与 virtio DRIVER/DEVICE 的异或结果；
- [x] 两端 `get_resource()`、table author、ready polling 和 startup ordering；
- [x] `rptun_dev_start/stop` 的 state gate、callback unregister、device remove/reprobe；
- [x] `remoteproc_get_notification()` 到 virtqueue/RPMsg callback 的完整路径；
- [x] `rdev->lock`、remoteproc lock、virtqueue lock、service lock 的持有和禁止嵌套；
- [x] `vring_size()`、`struct rptun_rsc_s`、`RPMSG_BUFFER_SIZE`、RPMsg header、
      descriptor 数、alignment、feature bits 和 notify ID；
- [x] avail/used index 的 barrier/cache 语义；
- [x] blocking send、try-send、TX buffer wait 和总 deadline 的差异；
- [x] `RPMSG_PRIO_RT` inline callback 与普通 RPMsg workqueue context；
- [x] MBOX0 base、channel/FIFO 2/3/3、IRQ、zero/nonzero message discriminator；
- [x] NuttX/OpenAMP 官方 issues/PRs 与当前 checkout 是否已包含对应修复。

“source-verified”指上述每项都有文件/行号、当前结论和对应 build/board test，不是只
grep 到符号名。

### 6.2 N9-D reconnect 状态机

N9-D 在实现前必须提交状态机文档，冻结如下语义：

```text
AP:     OFFLINE → BOOTING → READY → QUIESCING → OFFLINE
                    │         │
                    └─────────┴→ CRASHED → RESTARTING → BOOTING

RPTUN:  UNINIT → PREPARING → CONNECTING → CONNECTED → QUIESCING → OFFLINE
                                  │              │
                                  └──────────────┴→ FAULTED
```

- generation 使用 `uint32_t`，0 保留；每次 rebuild 前加一，wrap 时跳过 0。
- 只做 equality 比较，不用有符号大小关系判断新旧；2^32 次重启不作为可持续 session。
- reconnect 保留已注册的 lower-half 和 `/dev/rptun/<cpu>`，通过 STOP 移除旧
  virtio/RPMsg device/endpoints，再 START 重新 probe；不在旧 vring 上原地续跑。
- service 通过 device-destroy/device-create callback 重建 endpoint；restart 期间
  pending request 返回 `-ENOTCONN/-ETIMEDOUT`，不透明重放。
- AP_READY 只有在 table generation、RPTUN CONNECTED、nameservice 和必需 endpoint
  都完成后才能发布。

## 7. 配置和文件组织计划

不直接改已验证的 `cp_nsh` / `ap_smp_bidir` baseline，新增 opt-in 配置：

```text
configs/cp_nsh_rptun/
configs/ap_smp_rptun/
```

计划中的 overlay 文件边界：

```text
board/bk7258_t5ai/bk_idk/
├── chip/include/bk7258_rptun.h
├── chip/common/bk7258_rptun_common.c   # resource ABI / notify common code
├── chip/cp/bk7258_rptun_cp.c           # master / CP mailbox endpoint
├── chip/ap/bk7258_rptun_ap.c           # remote / pinned RX worker
├── Kconfig / Make.defs / CMakeLists.txt
├── scripts/calc_bk7258_rptun_layout.c   # 用当前 headers/sizeof/vring_size
├── scripts/verify_bk7258_rptun.py       # map/ELF/ABI/absolute-address verifier
└── configs/{cp_nsh_rptun,ap_smp_rptun}/defconfig

app/hello_app/
└── bk7258_rpmsg_test_main.c             # NSH built-in: bkrpmsgtest
```

具体文件名可在 N9-R 后微调，但角色边界不得混成大量 `#ifdef`。现有
`build_dual_image.sh` 仍是权威构建入口；只有 opt-in 配置通过后才扩展 allowlist 和
manifest。官方 `nuttx/` 只读，所有永久实现留在 contest overlay。

测试程序不是只靠人工看 log：

- `bkrpmsgtest ping/echo/throughput/restart/failclosed` 提供固定次数子命令；
- CP UART 是权威输出通道，格式同时含可读摘要和机器可解析 `BRPT key=value` 行；
- AP headless 证据通过 shared control counters 和 RPMsg response 返回，不能只经
  被测 RPMsg 链路输出；
- `cp_nsh_rptun` 和 `ap_smp_rptun` defconfig 明确列出各自 built-in/Kconfig；
- 每个 case 输出 generation、CPU index、payload、sequence、deadline 和结果。

拟启用/审计的上游配置包括：

- `CONFIG_RPTUN`
- `CONFIG_RPMSG_PING`（第一 transport gate）
- `CONFIG_RPMSG_CHAR`（ping 之后）
- `CONFIG_RPMSG_PROCFS`（诊断）
- `CONFIG_RPTUN_AUTO_RESET_DISABLE`

team Kconfig 至少要区分 `BK7258_RPTUN`、master/remote role、shared-memory 参数和
test-only gate，并 fail-closed 检查错误组合；还需包含 RX worker
priority/stack/affinity、peer timeout、FIFO retry quota 和 performance sample count。

## 8. 验收矩阵

### 8.1 静态与构建

- `git -C nuttx diff --exit-code`；不提交 NuttX 官方源码改动。
- team overlay `git diff --check` 通过。
- CP/AP fresh distclean + build；ELF/map/manifest/CRC 产物一致。
- symbol owner、resource offsets、vring bounds、IRQ owner 和 worker affinity verifier 全绿。
- `cp_nsh`、`ap_smp_bidir` 原 baseline 仍可独立构建。

### 8.2 首轮板端 transport gate

- CP/AP 都打印唯一 generation、role、resource table/vring 地址和长度。
- nameservice endpoint 建立；固定 1、8、100 次双向 ping/echo sequence 全闭合。
- mailbox IRQ 和 RX worker 只在 AP logical CPU0；无 OpenAMP-in-ISR。
- AP logical CPU1 发起固定次数 send 成功；transport RX worker 仍在 logical CPU0；
  endpoint callback 所在 RPMsg workqueue/CPU 被显式记录，不再误写为必然在 RX worker。
- timeout、peer absent、重复 notify、stale generation 均 fail-closed。
- N8 AP READY、online=`0x3`、SMP/IPI/affinity 基线无回归。

### 8.3 性能与调度基线

功能 gate 通过后才测性能，首轮不凭空设“漂亮数字”，而是生成可复现基线并在 N9-E
前冻结后续 non-regression threshold：

- payload：1、64、496 bytes（512-byte buffer 扣除 RPMsg header 后的当前上界）；
- 每组预热后固定 1000 次 echo，记录 min/p50/p95/p99/max、timeout/loss/duplicate；
- 单向持续发送固定总字节数，记录有效 payload throughput；
- 分别从 AP logical CPU0、logical CPU1 发起；
- 分别测 idle CPU0 和固定 CPU0 load；load task priority 低于 transport worker；
- 记录 `bk7258-rptun-rx`、RPMsg worker、NSH 的 priority/policy/affinity 和 CPU
  accounting；若当前配置无 CPU load accounting，至少记录 cycle/idle delta；
- 每个单次请求仍受 `CONFIG_BK7258_RPMSG_TEST_TIMEOUT_MS` 约束，任何测试都不得无限等。

N9-E 的验收是：零静默丢包、所有请求在配置 deadline 内结束、CPU0 load 下无
priority inversion/worker starvation，并且相对首次冻结基线无未解释退化。精确产品
latency/throughput target 留待有首轮硬件数据后确定。

### 8.4 lifecycle gate

- same-image restart、stop/start 和固定轮数 cycle 后 endpoint 自动重建。
- warm reset 与 physical RESET 分别固定轮数通过；power cut 单独记录，不冒充已测。
- 所有错误路径可重复执行，不泄漏 worker、semaphore、endpoint 或 vring state。
- restart 中的 pending request 必须在 peer deadline 内以预期 errno 结束；旧
  generation endpoint、notify、descriptor 和 response 均不得被新 session 接受。

## 9. 本 Stage 非目标

- 不修改官方 NuttX 源码。
- 不切换 AP 到 BMP，不开放默认 cpuset `0x3` 或无界 stress/load balancing。
- 不在首轮接 Wi-Fi、BLE、rpmsgfs 或大吞吐服务。
- 不同时改 cache policy、PSRAM、Tier-2 OTA 或 CPU hot-unplug。
- 不宣称 OpenAMP 替代 SMP；N9 只证明当前固定资源、cache-off wrapper transport
  已板端可用，不外推到 Wi-Fi/BLE、大数据面或 cache-on 场景。

## 10. 评审规则

N9 lower-half 按比赛提供的 `.claude/skills/nuttx-driver-development` 思路组织：
NuttX upper/lower-half、Kconfig/build、registration 和 lifecycle 都必须完整。完成每个
driver 子阶段后，按 `driver-code-reviewer` 的内存安全、并发、资源生命周期、错误路径、
整数/边界、DMA/cache/ISR/RPMsg/PM 维度做 focused review。通用 skill 中与本项目实际
冲突的 generic build 命令不采用，构建仍以项目脚本和双镜像 manifest 为准。

## 11. 完成状态与下一最小动作

N9 已完成，不再把 N9-R/A/B 当作待办。下一 MAIN Stage 应从稳定的
`cp_nsh_rptun + ap_smp_rptun` 基线选择一个独立服务接入；优先建议 N10 heartbeat / AP
crash supervision，再评估 `rpmsgfs`、BLE HCI 或 Wi-Fi control plane。每项服务保持独立
Kconfig、独立 deadline/backpressure 和独立回退，不把大数据面直接塞入当前 32 KiB carveout。

## 12. N9 实现与验收闭环（2026-07-31）

### 12.1 wrapper 落地

- 永久改动全部位于 contest board/app overlay；官方 `nuttx/` 和外部 SDK 源码零改动。
- SDK v3.1.1.9 是默认 checksum-pinned bundle，legacy bundle 保留并完成同配置构建回归。
- CP 是唯一 resource/control author 和 RPTUN master；AP 是一个 RPTUN remote。
- resource table 含 Name Service、ACK、buffer-size 和 CPU-name feature；vring notify ID 为
  0/1。ELF 驱动的 layout verifier 得到 `rsc=264`、vring=`222/224`、32 KiB carveout
  spare=`0x4cc0`。
- mailbox ISR 只 copy/coalesce/post semaphore；固定在 AP logical CPU0 的 worker 执行
  RPTUN callback。shared pending word 是 level truth，mailbox edge 失败时由 TX-complete、
  retry 和周期 poll 补偿。
- AP logical CPU0/1 都是业务 producer，但只有 logical CPU0 的 TX gateway 进入
  OpenAMP；CPU1→CPU0 completion 以 shared atomic counter 为 truth、semaphore 为 fast hint。
- AP 动态发布 `bk7258-smp-test` Name Service，CP 用 `ns_match/ns_bind` 建立 endpoint。

### 12.2 板端数据面与调度证据

- 最终 endpoint build 的 1/64/464-byte payload、idle/load 共六组，每组 CP-facing
  controller 驱动 AP logical CPU0/1 各 100 次，全部 `sent=received`、`errors=0`、
  callback mask=`0x1`，suite PASS。
- 满帧 `frame=496`（payload 464）+ CPU0 load 下，两核各 1000 次全部闭合；CPU0/CPU1
  p99 分别 54.094/47.906 ms，近似 wire throughput 50,148 B/s。
- 早期 transport build 另有六组各 1000 次完整矩阵和满帧重复运行证据；最终 Name
  Service/HPWORK build 以上述六组 100 次与满帧 1000 次作最终回归。

### 12.3 lifecycle 与服务证据

- `apctl cycle 2` 后 generation 2/3 都恢复 READY，AP SMP、affinity、semaphore 与
  transport gate 全部通过；再次启动 generation 4 后 endpoint 自动重建。
- generation 4 上显式 `bkrpmsgtest syslog` 得到 RPMsg PASS，随后 CP UART 收到
  `BK7258 AP RPMsg syslog probe gen=4 run=9`，证明 reconnect 后 `syslog_rpmsg` 可用。
- `syslog_rpmsg` 首次无输出的根因是上游客户端固定投递 `HPWORK`，AP 配置只有通用
  workqueue、没有 high-priority worker。仅在 AP opt-in defconfig 加
  `CONFIG_SCHED_HPWORK=y` 后闭环；最终 AP ELF 含 `work_start_highpri` 和 `g_hpwork`。
- 真实 RTS physical RESET 曾稳定出现 CP NSH 可用但 RPMsg `-ENOTCONN`。共享控制块细分
  progress flags 将现场收敛到 AP 在 `kthread_create("bk7258-rptun-rx")` 中被新建的高优先级
  worker 抢占：启动协调线程优先级 100，而 RX/RPTUN worker 为 225/224。最终仅在 board
  wrapper 中把 AP init coordinator 临时提升到 226，保持 N8 gates 先于 logical RPTUN
  初始化，发布 AP READY 后恢复原优先级；不修改 NuttX 或 SDK。
- 修复后连续三次独立 physical RESET 均 `cold_path=yes` 且 RPMsg 双 AP CPU 各 100/100
  零错误；最终正式产物又完成一次 physical RESET，`apctl status` 为 AP READY、全部 N8
  gates PASS、RPTUN flags=`0x00003fff`、state=`CONNECTED(4)`。
- `CONNECTED` 只由 CP 成功处理 AP Name Service 并绑定 endpoint 时从 `CONNECTING` 原子
  迁移；CAS 不会覆盖并发的 `QUIESCING/FAULTED`。最终 warm restart generation 2 再次
  进入 `CONNECTED`，双 CPU 各 100/100 和 `syslog_rpmsg` probe 均 PASS。

### 12.4 构建与兼容性证据

- latest v3.1.1.9 `cp_nsh_rptun + ap_smp_rptun` fresh build PASS：CP 214,256 B、
  AP 105,704 B，layout verifier PASS；2026-07-31 21:55 最终打包产物的 raw SHA-256
  分别为
  `0943a0a48e25046819d278ae2ce10c3cfe5fe99c6506776085d384de227f6b18` 和
  `7c359e044fc45791ff4a60ea46ef880dbd79171678f74d7ac8eff22fcad03d23`。
  CP image 含 NuttX 构建元数据，相同源码重复构建的 raw hash 可变化；验收以同次 manifest/
  `artifacts.sha256` 为准，不把这个值宣称为源码级 reproducible hash。
- legacy SDK 同一 RPTUN 配置 fresh build PASS。
- 非 RPTUN `cp_nsh + ap_smp_bidir` baseline fresh build PASS，证明 opt-in wrapper
  未破坏既有配置。

### 12.5 最终硬件证据索引

- physical RESET：`logs/bk7258-auto-debug/20260731-215631/`，判定
  `PASS_NSH`、`cold_path=yes`。
- cold status：`logs/n9-final-packaged-apctl-status.raw`，RPTUN
  `CONNECTED(4)`、generation 1、flags `0x3fff`，N8 gates 全 PASS。
- cold data：`logs/n9-final-packaged-rpmsg-run100.raw`，AP logical CPU0/1 各
  100/100、errors 0、`BRPT PASS`。
- warm reconnect：`logs/n9-final-connected-warm-post-status.raw` 与
  `logs/n9-final-connected-warm-rpmsg-run100.raw`，generation 2 CONNECTED 且各
  100/100 PASS。
- service：`logs/n9-final-connected-warm-syslog.raw`，generation 2 收到
  `BK7258 AP RPMsg syslog probe gen=2 run=3`。
