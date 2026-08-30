# BK7258 N9 RPTUN source verification（2026-07-31 snapshot）

> 日期：2026-07-31
> 状态：`board-verified`
> 边界：官方 NuttX 与 Beken SDK 只读；本文的 `board/...` 路径和 checkout hash 是
> 当时审计坐标，不是现行源码入口。现行架构见 [BK7258 平台文档](../README.md)。
> 实板范围：`board-verified` 仅对应涂鸦 T5AI-Core 首板和冻结镜像；RPTUN lower-half
> 的芯片级设计可复用，但不自动证明其他板卡或 profile。

冻结 checkout：NuttX/OpenAMP/libmetal 均来自该轮 openvela checkout
`e02f581e235fc7b527d57ff62b668ce625d139ab`；构建选择 `open-amp.manual` 与
`libmetal.manual`，二者是构建生成/准备的只读工作目录，不承载 team patch。

## 1. wrapper 边界

本项目采用官方推荐的 wrapper 接入方式：复用 SDK 的 public mailbox channel、AON RTC、
boot/reset 和 checksum-pinned 静态库，不修改 SDK；复用 NuttX RPTUN/OpenAMP/RPMsg core，
不修改 NuttX。team-owned 层只实现 shared-memory ABI、RPTUN lower-half、mailbox deferred
worker、AP CPU0 gateway、测试与 lifecycle policy。

数据路径为：

```text
SDK mailbox channel / shared SRAM
        ↓ board wrapper
BK7258 RPTUN lower-half
        ↓ stock NuttX
RPTUN → OpenAMP virtio → RPMsg → service
```

`mb_ipc` 已经是厂商私有上层协议，不作为 RPTUN substrate。

## 2. role 与 resource table

- `board/.../chip/common/bk7258_rptun.c:88-99` 定义完整 `rptun_ops_s`。
- `bk7258_rptun.c:192-245` 只有 CP 构造 resource table；CP 是 control/resource author。
- `bk7258_rptun.c:298-305` 冻结 CP master、AP remote。
- `bk7258_rptun.c:212-217` 按当前 RPTUN XOR 语义设置 `reserved[0]`，最终 CP 为
  virtio driver、AP 为 virtio device。
- `bk7258_rptun.c:219-236` 冻结 notify ID 0/1、buffer size、CPU name 和 carveout。
- `bk7258_rptun.c:377-457` 校验非零 generation；AP 校验 magic/version/size/generation，
  CP 在新 generation 重建 table 后调用 stock `rptun_boot()`。

当前 NuttX `drivers/rptun/rptun.c:799-930` 的 start path 调用 lower-half resource/role
回调并建立 remoteproc/virtio；`rptun.c:931-1040` 负责 stop/restart。`notify()` 返回值不会
替 wrapper 保存丢失 edge，因此 shared pending level state 必须由 lower-half 自己维护。

## 3. 中断、worker 与锁上下文

- `bk7258_rptun_mbox.c:92-129` 是 SDK logical-channel ISR callback，只复制消息、在
  spinlock 下合并 pending 并唤醒 worker；不进入 OpenAMP。
- `bk7258_rptun_mbox.c:258-324` 的 `bk7258-rptun-rx` worker 固定 AP logical CPU0，
  drain mailbox 后继续轮询 shared pending level state。
- `bk7258_rptun_mbox.c:130-198` 用 TX-complete 唤醒、有界 retry 和 coalescing 处理
  FIFO busy；ACK 不是交付真值。
- `bk7258_rptun.c:137-189` 先校验 generation，再对 pending word 原子 exchange，最后
  从线程上下文调用 NuttX callback。
- `nuttx/drivers/rptun/rptun.c:796` 才进入 `remoteproc_get_notification()`；因此
  remoteproc/virtqueue/RPMsg 的 mutex/workqueue 路径不发生在 hard IRQ 中。
- `nuttx/drivers/rpmsg/rpmsg_virtio.c:557-579` 处理 Name Service；普通 endpoint 使用
  RPMsg workqueue，只有显式 `RPMSG_PRIO_RT` endpoint 可 inline。N9 业务 endpoint 不使用 RT。
- physical RESET 现场证明 `kthread_create()` 会立即激活更高优先级线程：AP init task 原为
  100，board RX worker 为 225、stock RPTUN worker 为 224。AP main 因此仅在 N8 gates 与
  logical RPTUN 初始化窗口临时提升当前 init coordinator 到 226，AP READY 后恢复；logical
  transport 仍保持在 N8 gates 之后，避免 transport worker 饿死 N8 测试。

AP logical CPU1 不直接调用 OpenAMP。`bk7258_rpmsg_test.c:310` 的 CPU0 TX gateway 是
唯一业务发送入口；两个 producer 的完成状态用 shared atomic state 表达，semaphore 只作提示。

## 4. Name Service、服务与 lifecycle

- `bk7258_rpmsg_test.c:1045-1121`：AP 用 ANY/ANY 创建并公告
  `bk7258-smp-test`；CP 用 `ns_match/ns_bind` 动态创建 endpoint，不使用两端静态地址绕过 NS。
- CP `ns_bind` 成功是 remote 已消费 resource table 的首个双向证据；此处通过 CAS 将共享
  state 从 `CONNECTING` 推进到 `CONNECTED`，若 lifecycle 已进入 `QUIESCING/FAULTED` 则
  保持原状态。
- `bk7258_rpmsg_test.c:1123-1257`：device destroy/create callback 负责 reconnect 后 endpoint
  生命周期；注册只执行一次。
- `bk7258_rptun.c:377-460`：CP lower-half 跨 AP reset 保留注册实例，新 generation 先由
  `apctl` quiesce，再重建共享状态并 `rptun_boot()`；AP 拒绝 stale generation。
- `CONFIG_RPTUN_AUTO_RESET_DISABLE=y` 保持 `apctl` 为唯一 reset/restart owner。
- 上游 `drivers/syslog/syslog_rpmsg.c:206,297,321,353,378` 固定使用 `HPWORK`；因此
  `ap_smp_rptun` 必须启用 `CONFIG_SCHED_HPWORK=y`。最终 ELF 已验证
  `work_start_highpri`、`g_hpwork` 存在。

## 5. ABI、cache 与容量

- control header 使用 `uint32_t generation`，0 保留，比较采用 equality；resource 发布前后
  使用 DMB，mailbox 只传 edge。
- 当前 AP `CCR.DC=0`，共享 SRAM MPU 属性为 Inner Shareable Normal Non-cacheable；N9
  仍保留 barrier，cache-on 是后续独立 gate。
- ELF/header-driven verifier 实测 `struct rptun_rsc_s=264`、两个 vring 为 222/224 B；
  32 KiB carveout 剩余 `0x4cc0`，且与 AP image/heap/CPU2 stack、telemetry、PWR_MNG、SWAP
  无重叠。
- RPMsg buffer 为 512 B，当前最大 wire frame 496 B、测试 payload 464 B；descriptor 数和
  buffer size若变化必须重新跑 verifier。

## 6. 验证结论

- 最新 SDK v3.1.1.9、legacy SDK 和非 RPTUN baseline 均 fresh build PASS。
- 最终 Name Service/HPWORK build 的六组 100 次矩阵、满帧 load 1000 次、两轮 lifecycle
  cycle、generation 4 reconnect 和显式 syslog probe 全部板端 PASS。
- cold-race 修复连续三次 physical RESET PASS；最终正式产物另一次 physical RESET 得到
  `cold_path=yes`、RPTUN `CONNECTED(4)`/flags `0x3fff`，generation 1 双 CPU 各 100/100。
  warm restart 后 generation 2 再次 CONNECTED、双 CPU 各 100/100 且 syslog probe 到达 CP。
- 官方 NuttX/SDK 无永久源码改动；这个结论只覆盖当前 non-cacheable、固定 32 KiB
  carveout 和单 CP↔AP link，不自动覆盖 cache-on、无线或大共享数据面。
