# BK7258 Stage N9：CP/AP RPTUN/OpenAMP/RPMsg 计划

> 日期：2026-07-31
> 状态：**CURRENT / `static-only` planning**
> 前置基线：N8 AP NuttX SMP 与 physical-reset closure 已 `board-verified`
> 本文状态边界：已完成源码、配置和内存布局调查；尚未修改实现、构建或板测
> 架构调研前置文档：[CP/AP 双 NuttX 与 RPTUN/RPMsg 架构探索](../cp-ap-rptun-architecture-research.md)

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
- RPMsg endpoint callback：初始自然运行在该 RX worker，即 AP logical CPU0。
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

### 4.1 当前内存事实

| 区域 | 当前范围 | 说明 |
|---|---|---|
| CP SRAM | `0x28000000..0x2804ffff` | CP image/heap |
| AP SRAM | `0x28050000..0x2809efff` | AP image/heap/CPU2 stack |
| team shared telemetry | `0x2809f000..0x2809ffff` | boot/fault/IPI/SMP/test state，保留 |

现有 4 KiB telemetry page 已有稳定 ABI，不能塞入 resource table、两个 vring 和
RPMsg buffers。N9-A 的**保守候选**是：

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
- resource table、两个 vring、descriptor、buffer pool 的每个 offset 均有编译期断言。
- packer/manifest verifier 检查 CP/AP image 和 shared region 边界。
- 第一版按 non-cacheable 或当前 cache-off 属性工作，同时保留 DMB/DSB 和 cache
  clean/invalidate hook；未来启用 cache 不得悄悄依赖偶然一致性。
- 地址只在上述 gate 全绿后从 “candidate” 改为 “frozen ABI”。

初始参数可参考 nRF53/STM32H7：两个 vring、每环 8 descriptors、512-byte buffer、
8-byte vring alignment；最终值以 N9-R 的 OpenAMP 版本和 resource table 计算为准。

## 5. Resource table、角色和启动顺序

### 5.1 初始角色

- CP：RPTUN master，负责准备/清零 shared resource table、generation 和 vring 状态。
- AP：RPTUN remote，在 AP scheduler 和 SMP logical CPU1 已 online 后注册 lower-half。
- 首版保持 team-owned `apctl` 为 AP reset/restart owner，并配置
  `CONFIG_RPTUN_AUTO_RESET_DISABLE=y`，避免 RPTUN 自动 reset 与既有 lifecycle 冲突。
- virtio role flag、feature bits、notify ID 和 resource table ABI 必须在 N9-R
  对当前 checkout 的 OpenAMP 源码再次追踪后冻结，不能凭别的平台常量猜测。

### 5.2 预期启动序列

1. CP 启动并初始化 CP/AP mailbox 基础设施。
2. CP 初始化 shared resource table、vring、generation，再释放 AP。
3. AP 完成现有 boot、CPU2 secondary bring-up、scheduler online 和 N8 baseline gate。
4. AP 注册 RPTUN remote lower-half，启动 pinned RX worker。
5. CP 注册并启动 RPTUN master。
6. 双方完成 virtio/RPMsg nameservice；第一服务只启用固定次数 `RPMSG_PING`。
7. transport gate 通过后再开启 `RPMSG_CHAR`，最后才接 syslog/heartbeat/rpmsgfs 等服务。

reset/restart 顺序必须显式定义：

```text
block new sends
→ quiesce endpoint workers
→ stop/unregister RPTUN
→ clear stale mailbox/pending notify
→ bump generation and rebuild shared state
→ restart AP
→ recreate transport/endpoints
```

旧 generation 的 notify 和 descriptor 必须被拒绝或安全清理。

## 6. N9 分阶段执行计划

| 子阶段 | 工作 | 退出条件 |
|---|---|---|
| **N9-R** | 冻结当前 OpenAMP/RPTUN/libmetal 版本；追踪 master/remote role、resource table、notify context；整理 nRF53/STM32H7/RP2040 参考矩阵 | 角色、锁、线程上下文和 ABI 均 source-verified |
| **N9-A** | 32 KiB carveout 候选评审；CP/AP linker、CPU2 stack/heap 上界、静态断言、map/packer verifier | fresh 双镜像构建通过且零 overlap；地址才可冻结 |
| **N9-B** | 实现独立 CP↔AP mailbox event；AP IRQ→sem→CPU0-pinned worker；只做 raw notify/counter | 双向固定次数 notify、无丢失/重复/stale，N8 SMP gates 保持 |
| **N9-C** | 接入 RPTUN/OpenAMP；static resource table、两个 vring、nameservice、固定次数 ping/echo | CP/AP endpoint 建立，双向 payload/sequence 完整，超时 fail-closed |
| **N9-D** | 与 `apctl` 集成 stop/restart/reconnect；generation 和 stale-event 清理 | warm restart 固定轮数恢复 endpoint，无旧消息串入 |
| **N9-E** | SMP 组合验证：AP logical CPU0/1 分别发消息；RX 仍固定 CPU0；并发与 teardown 竞态 | CPU1 send 可用、RX affinity 不漂移、无死锁/泄漏/越界 |
| **N9-F** | 按价值逐个接服务：syslog/heartbeat，随后评估 `rpmsgfs`/`rpmsg_char` | 每个服务独立 Kconfig、独立回退、独立证据 |
| **N9-V** | focused driver review、静态 verifier、双版本构建、warm/RESET 回归 | NuttX 官方树零改动；latest SDK 与 legacy 回退边界清楚；板端证据归档 |

每一子阶段先更新本文 worklog 再进入下一个技术动作。失败时保留最小可复现证据，
不跨过未通过的 gate。

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
├── scripts/verify_bk7258_rptun.py
└── configs/{cp_nsh_rptun,ap_smp_rptun}/defconfig
```

具体文件名可在 N9-R 后微调，但角色边界不得混成大量 `#ifdef`。现有
`build_dual_image.sh` 仍是权威构建入口；只有 opt-in 配置通过后才扩展 allowlist 和
manifest。官方 `nuttx/` 只读，所有永久实现留在 contest overlay。

拟启用/审计的上游配置包括：

- `CONFIG_RPTUN`
- `CONFIG_RPMSG_PING`（第一 transport gate）
- `CONFIG_RPMSG_CHAR`（ping 之后）
- `CONFIG_RPMSG_PROCFS`（诊断）
- `CONFIG_RPTUN_AUTO_RESET_DISABLE`

team Kconfig 至少要区分 `BK7258_RPTUN`、master/remote role、shared-memory 参数和
test-only gate，并 fail-closed 检查错误组合。

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
- AP logical CPU1 发起固定次数 send 成功；endpoint RX callback 仍在 logical CPU0。
- timeout、peer absent、重复 notify、stale generation 均 fail-closed。
- N8 AP READY、online=`0x3`、SMP/IPI/affinity 基线无回归。

### 8.3 lifecycle gate

- same-image restart、stop/start 和固定轮数 cycle 后 endpoint 自动重建。
- warm reset 与 physical RESET 分别固定轮数通过；power cut 单独记录，不冒充已测。
- 所有错误路径可重复执行，不泄漏 worker、semaphore、endpoint 或 vring state。

## 9. 本 Stage 非目标

- 不修改官方 NuttX 源码。
- 不切换 AP 到 BMP，不开放默认 cpuset `0x3` 或无界 stress/load balancing。
- 不在首轮接 Wi-Fi、BLE、rpmsgfs 或大吞吐服务。
- 不同时改 cache policy、PSRAM、Tier-2 OTA 或 CPU hot-unplug。
- 不宣称 OpenAMP 替代 SMP，也不宣称 RPTUN 已经板端可用。

## 10. 评审规则

N9 lower-half 按比赛提供的 `.claude/skills/nuttx-driver-development` 思路组织：
NuttX upper/lower-half、Kconfig/build、registration 和 lifecycle 都必须完整。完成每个
driver 子阶段后，按 `driver-code-reviewer` 的内存安全、并发、资源生命周期、错误路径、
整数/边界、DMA/cache/ISR/RPMsg/PM 维度做 focused review。通用 skill 中与本项目实际
冲突的 generic build 命令不采用，构建仍以项目脚本和双镜像 manifest 为准。

## 11. 下一最小动作

从 **N9-R + N9-A** 开始，不先写 mailbox 或 endpoint 功能：

1. 将当前 checkout 的 resource table/virtio role 逐符号冻结；
2. 生成 32 KiB carveout 的精确 layout 表；
3. 修改 team-owned linker/config/verifier，先让 build/map gate 变绿；
4. 保持 N8 代码路径和默认配置不变；
5. N9-A 通过后再进入 N9-B mailbox transport。
