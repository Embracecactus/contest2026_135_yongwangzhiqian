# BK7258 N9 计划评审处置记录

> 日期：2026-07-31
> 范围：对 N9 RPTUN/RPMsg 计划的 17 项外部评审逐项进行当前源码复核
> 结论：10 项接受，5 项部分接受/纠正，2 项不接受；同时新增 1 项 mailbox
> 单一硬件 owner 风险

## 1. 总结

评审正确指出了 N9 计划在精确内存计算、mailbox ABI、warm restart、fail-closed、
worker 调度、callback 约束、测试和性能基线方面的缺口，这些内容均已回写权威计划。

但评审把“官方 Armino/FreeRTOS 链接分区”和“当前团队双 NuttX 链接 ABI”当成了同一
套运行时布局，并据此直接判定冲突；还把本版本 RPTUN、RPMsg callback 和
`PWR_MNG` 的语义说得过于绝对。这些部分不能照单全收。

## 2. 内存布局复核

### 2.1 两套布局不是同一个 linker ABI

官方 v3.1.1.9 生成的 `ram_regions.h` 是官方 CP/AP FreeRTOS 镜像的分区：

```text
AP_SPINLOCK  0x28000000..0x2800ffff
AP_RAM       0x28010000..0x28063fff
CP_RAM       0x28064000..0x2809f6ff
PWR_MNG      0x2809f700..0x2809f7ff
SWAP         0x2809f800..0x2809ffff
```

当前团队双 NuttX 镜像由自己的 linker script 明确定义：

```text
CP RAM       0x28000000..0x2804ffff
AP RAM       0x28050000..0x2809efff
shared page  0x2809f000..0x2809ffff
```

没有官方 CP/AP 固件与团队镜像同时运行，因此两套 `AP_RAM/CP_RAM` 分区不同本身不是
地址冲突。真正必须审计的是：

1. SDK archive 中是否存在写死地址；
2. `.sram_spinlock_section`、`.swap_data` 等 vendor section 最终被团队 linker 放到哪里；
3. 团队是否仍调用依赖 `PWR_MNG` 固定地址的 SDK 函数。

`AP_SPINLOCK` 不是硬件保留寄存器区，而是官方 linker 为
`.sram_spinlock_section` 预留的内存。团队 NuttX SMP 不需要照搬该 FreeRTOS 分区；
但只要链接进来的 SDK object 仍带这个 section，就必须由团队 linker 显式映射并在
最终 ELF 审计 owner，不能让 orphan section 落入 CP RAM。

### 2.2 telemetry 没有覆盖 `PWR_MNG/SWAP`

`BK7258_SHARED_RAM_SIZE` 虽然把顶部 4 KiB 排除在两个 heap 之外，但当前已定义的 N7/N8
telemetry record 最后一个是：

```text
BLCY offset 0x680 + sizeof(advanced state) 0x80 = 0x700
```

而代码已有静态断言保证它恰好结束于 shared offset `0x700`。所以实际 ABI 是：

```text
team telemetry records  0x2809f000..0x2809f6ff
vendor PWR_MNG reserve  0x2809f700..0x2809f7ff
vendor SWAP/tail reserve 0x2809f800..0x2809ffff
```

三者相邻但不重叠。原计划把整页统称为 telemetry 的表述不精确，现已修正。

评审所称“`PWR_MNG` 含 flash shared-lock state”没有得到 v3.1.1.9
`pwr_clk.h` 支持。该头文件列出的字段是 PSRAM usage/power、wake counter、reset
reason、exception 协调、deep-wakeup GPIO 和 PM vote；flash 的 SMP lock 在
`.sram_spinlock_section`，跨 CP/AP flash 操作则通过 mailbox IPC prepare/finish
协调。`PWR_MNG` 仍必须保留，因为当前 SDK PM 函数确实访问该固定地址，但不能把
“flash shared-lock”当成已证实字段。

## 3. Mailbox 复核

官方 v3.1.1.9 的 AP SMP `mbox0` 事实如下：

- 物理控制器：`SOC_MBOX0_REG_BASE = 0x41000000`；
- 三个 CPU destination channel：CPU0/CPU1/CPU2；
- 硬件总 FIFO 深度为 8；
- SMP 配置的 FIFO 划分为 `2/3/3`；
- `data[1] == 0` 被 `mbox0_drv.c` 解释为 CPU1↔CPU2 SMP command；
- 当前 N8 SMP command 使用 `data[0]` 携带编码后的 magic/type/generation/sequence。

因此评审提出“mailbox channel/notify ID 必须冻结”是正确的，但“零长度消息”应准确
表述为 `mbox0_message_t.data[1] == 0`，不是没有 payload 的抽象 RPMsg 消息。

新增发现：N7 lifecycle 当前以 raw register 方式直接访问 `0x41000000/0x41020000`
的 box0 寄存器，而 N8 SDK FIFO 同样拥有 `0x41000000`。这不是可以长期并存的两套
独立 mailbox。N9-R/N9-B 必须：

1. 冻结唯一的 `MBOX0` 初始化和 IRQ owner；
2. 将 lifecycle control 与 RPTUN notify 统一复用同一 FIFO ABI，或定义严格的
   pre-FIFO handoff；
3. 保留 `data[1] == 0` 给 AP 内部 SMP IPI；
4. 给 CP↔AP control/RPTUN 使用非零 type tag，并在共享 control header 校验完整
   32-bit generation；
5. 对 FIFO full、coalescing、重复 kick、IRQ drain 和 stale event 写出确定策略。

在该 owner/handoff 设计通过前，不进入 N9-B 实现。

## 4. 本版本 RPTUN/OpenAMP 语义复核

### 4.1 `is_master()` 不等于“谁调用 `rptun_init_mem()`”

当前 checkout 的 `drivers/rptun/rptun.c` 没有 `rptun_init_mem()`。两端都会在
`rptun_do_start()` 中调用 lower-half `get_resource()`，随后调用
`remoteproc_set_rsc_table()`。

`is_master()` 在本版本中用于：

- 只允许 master 调 lower-half `config/start/stop`；
- 与 resource table 的 `reserved[0]` 一起异或计算 virtio
  `DRIVER/DEVICE` role；
- 选择 shared reset/status 字段。

参考 lower-half（例如 STM32H7）通常在 master 的 `get_resource()` 内初始化共享表，
remote 则轮询 ready 字段。这是一种板级实现约定，不是 RPTUN core 自动完成的行为。

所以评审第 5 项提出的 stale resource table/warm restart 风险成立，但其
“`is_master()` 决定谁运行 `rptun_init_mem()`”的解释不适用于当前源码。

N9 改为：

- CP 是 boot/control master 的候选，最终 role 在 N9-R 后冻结；
- CP 是共享 control header/resource table 的唯一 author；
- AP 在 `magic/version/size/generation/state/CRC` 全部匹配前不得进入
  `rptun_initialize()`；
- restart 不在旧 vring 上原地续跑，而是 quiesce/remove/rebuild/reprobe。

### 4.2 endpoint callback 不总在 transport RX worker

当前 NuttX `drivers/rpmsg/rpmsg_virtio.c` 的真实路径是：

- transport RX worker 调 `remoteproc_get_notification()`；
- `RPMSG_PRIO_RT` endpoint 可以 inline 执行 callback；
- 其他 endpoint 被投递到 RPMsg workqueue，默认
  `CONFIG_RPMSG_WQUEUE_PRIORITY=224`。

因此原计划“endpoint callback 自然运行在 RPTUN RX worker”是错误表述，评审所称
“所有 callback 阻塞都会卡住 transport RX worker”也过于绝对。

首版约束改为：

- 禁止业务 endpoint 使用 `RPMSG_PRIO_RT`；
- transport RX worker 只 drain notification；
- 普通 callback 只 validate/copy/enqueue/return；
- syslog、filesystem 等潜在慢服务使用独立 queue/worker 和有界 backpressure。

### 4.3 send timeout 不能靠 `CONFIG_RPTUN_AUTO_RESET_DISABLE`

该选项只是关闭 reboot/panic notifier 对 peer 的自动 reset，不会自动赋予
`rpmsg_send()` peer-crash timeout。OpenAMP 的 blocking send 会等待 TX buffer；
当前 NuttX wait 路径只有局部 20 ms wait slice，不等于整个业务请求有总 deadline。

N9-C 首版必须使用 nonblocking/try-send 加 team-owned deadline，或在服务协议层提供
有界 request timeout；peer generation/heartbeat 失效后立即拒绝新 send。不能把
“底层可能反复等待”误报成 fail-closed。

## 5. 对 17 项意见的处置

| # | 处置 | 结论 |
|---|---|---|
| 1 | 部分接受 | 两套 linker 分区不同不是立即冲突；接受 fixed-address/vendor-section compatibility gate |
| 2 | 部分接受 | active telemetry 止于 `0x2809f6ff`，与 PWR/SWAP 不重叠；接受尾部显式保留 |
| 3 | 接受 | 增加使用当前 C headers/`sizeof`/`vring_size()` 的 layout calculator 和 build assert |
| 4 | 接受并升级 | 必须冻结 MBOX0 owner、FIFO 2/3/3、message type、notify ID 和 busy/coalescing |
| 5 | 部分接受 | stale/generation 风险成立；`rptun_init_mem()`/`is_master()` 解释不适用于当前版本 |
| 6 | 接受 | 明确 FIFO priority、CPU0 affinity 和 CPU0 load latency gate；N8 并非所有测试只跑 CPU0 |
| 7 | 接受 | N9-C 增加 peer crash/absent 的有界 fail-closed gate |
| 8 | 部分接受 | 非 RT callback 在 RPMsg workqueue；仍禁止慢 callback 占用共享 worker |
| 9 | 接受 | N9-D 增加 AP/RPTUN 双状态机、generation 和 endpoint 重建语义 |
| 10 | 接受 | N9-R 改为逐文件、逐回调、逐锁、逐 ABI checklist |
| 11 | 部分接受 | 增加官方 issue/PR 审计；不接受无出处的三个问题作为既定事实 |
| 12 | 接受 | 增加 payload-size、latency percentile、throughput、CPU load 基线 |
| 13 | 不接受 | kthread 名 `bk7258-rptun-rx` 与文件名 `bk7258_rptun_ap.c` 是正常命名约定 |
| 14 | 接受 | 明确 opt-in configs、NSH built-in、UART/机器可解析结果和源码目录 |
| 15 | 不接受 | 两处相对链接均从各自文件目录正确解析，不存在不一致 |
| 16 | 接受 | 改为当前 AP 的明确 contract：D-cache disabled + MPU SRAM non-cacheable |
| 17 | 接受澄清 | 保留 N9-R 名称，但定义为严格前置 Review/Role/Resource gate，顺序 `R→A→B` |

## 6. 官方问题审计

评审列出的三个“已知问题”不能在没有 issue/commit 的情况下写成事实。当前已核实的
相关上游记录包括：

- Apache NuttX PR
  [#11132](https://github.com/apache/nuttx/pull/11132)：初始化阶段 stop 的历史竞态；
- Apache NuttX PR
  [#11719](https://github.com/apache/nuttx/pull/11719)：vring `da==0/-1` 初始化；
- OpenAMP PR
  [#647](https://github.com/OpenAMP/open-amp/pull/647)：remote-ready 等待改用
  `metal_sleep_usec()`，避免低优先级任务饥饿。

当前 checkout 的 remote-ready loop 已包含 `metal_sleep_usec(1000)`，RPTUN stop 也有
状态检查和完整 `rpmsg_virtio_remove()` 路径。因此这些历史问题应转化为回归测试和
版本差异审计，不应直接宣称当前版本必然存在。
