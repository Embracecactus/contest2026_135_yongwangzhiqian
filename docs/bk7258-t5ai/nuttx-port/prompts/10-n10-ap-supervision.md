# N10 — AP SMP liveness and RPMsg health supervision

> 状态：**CURRENT / board-verified（正常链路、三类故障注入、人工恢复、无注入重复压力）**
>
> 日期：2026-08-01
>
> 基线：`feat/bk7258-ap-smp`，启动时 HEAD `308d42c`，实现提交 `1bda1e8`

## 1. 目标与边界

N10 在 N9 的 CP NuttX UP ↔ AP NuttX SMP 单 RPTUN link 上增加独立管理面：

- CP 能区分 AP primary 管理循环、AP logical CPU1 scheduler 和 RPMsg 数据通路的活性；
- 故障判定有 suspect/fault deadline，不在 ISR 或 RPMsg callback 中做生命周期操作；
- 确认故障后把本 generation 的 RPTUN 链路置为 `FAULTED`，现有板级 RPMsg
  发送入口 fail-closed；
- 默认只检测和锁存，人工通过 `apctl recover` 复用现有 generation-safe restart；
- 自动恢复是独立 Kconfig，默认关闭，必须经过故障注入与实板 soak 才能开启。

永久改动只允许位于本仓库 board/app wrapper。官方 NuttX 与 Beken SDK 源码保持只读。
N10 不扩展固定 0x40-byte RPTUN control ABI，也不占用 shared telemetry
`0x700..` 的厂商 `PWR_MNG/SWAP` 保留尾区。

## 2. 当前设计

### 2.1 三路独立健康信号

1. AP physical CPU1 / logical CPU0 继续推进
   `bk7258_ap_boot_state_s::heartbeat`；
2. AP 上新增永久 pthread，固定到 logical CPU1，推进现有
   `bk7258_cpu2_probe_state_s::heartbeat`；
3. 新增 `bk7258-health` RPMsg endpoint。CP 发带 generation/sequence 的 bounded
   PING，AP callback 用非阻塞 `rpmsg_trysend()` 回 PONG。

共享 RPTUN control 中已有的 `cp_heartbeat/ap_heartbeat` 与
`cp_epoch/ap_epoch` 同步推进；原两个 reserved word 复用为
`cp_rx_sequence/ap_rx_sequence`，让真实双向 vring 进展可以替代繁忙期的主动探测。
control 大小仍为 0x40 bytes，CP supervisor 的完整状态、计数和 fault snapshot
保存在 CP 私有 RAM，不扩展跨镜像 ABI。

### 2.2 CP 状态机

`OFFLINE → ARMING → HEALTHY ↔ SUSPECT → FAULTED → RECOVERING`，自动恢复预算
耗尽时进入 `LOCKOUT`。AP N8 启动 self-test 期间仍由既有 bounded startup deadline
负责，supervisor 只在 AP `READY`、CPU2 `SCHEDULER_ONLINE`、RPTUN
`CONNECTED` 后 arm，避免冷启动假阳性。

默认参数：

- heartbeat 100 ms；
- CP poll 250 ms；
- suspect 1000 ms；
- confirmed fault 2500 ms；
- RPMsg probe interval/timeout 500/750 ms。

### 2.3 用户接口

- `apctl status`：同时输出 supervisor 状态、三路年龄、故障/恢复计数和已保存
  fault registers；
- `apctl health`：仅 `HEALTHY` 返回成功，适合脚本判定；
- `apctl recover [timeout_ms]`：仅从 `SUSPECT/FAULTED/LOCKOUT` 进入人工恢复。

## 3. 已落地文件

- `chip/cp/bk7258_ap_supervisor.c`
- `chip/ap/bk7258_ap_health.[ch]`
- `chip/common/bk7258_rpmsg_health.[ch]`
- `chip/include/bk7258_amp.h`
- `chip/Kconfig`、`Make.defs`、`CMakeLists.txt`
- `chip/common/bk7258_rptun.c`、`bk7258_rpmsg_test.c`
- `src/bk7258_bringup.c`
- `app/hello_app/bk7258_apctl_main.c`
- `configs/cp_nsh_rptun/defconfig`、`configs/ap_smp_rptun/defconfig`

`bkrpmsgtest` 的 endpoint-ready gate 现在还要求当前共享控制块为同一有效 generation
且状态为 `CONNECTED`。Supervisor 锁存 `FAULTED` 后，旧 endpoint 即使 OpenAMP
对象仍短暂存在也不能继续发送。

## 4. 2026-08-01 首轮构建证据

执行：

```sh
CP_CONFIG_NAME=cp_nsh_rptun \
AP_CONFIG_NAME=ap_smp_rptun \
JOBS=8 \
./board/bk7258_t5ai/scripts/build_dual_image.sh
```

结果：exit 0，CP/AP 都完成 fresh configure、compile、link、pack，最终恢复 CP build
tree。SDK v3.1.1.9 CP/AP checksum 检查均通过。

- CP raw：219,664 bytes，SHA-256
  `e4bb14a30e738ab0135032406e5e5c2593d91ab3055d544510b7ca32db53d6a7`
- AP raw：107,360 bytes，SHA-256
  `1ba9cf1d1610e1c9142b0794d9418ebd558154cf5d482bc449f0aacb7ab16f68`
- RPTUN layout：`PASS`
- resource table：264 bytes
- vring entries：222 / 224
- carveout：`0x7e80`
- spare：`0x4cc0`

构建期间出现宿主机亚秒级 clock-skew warning，但三个实际 link/pack 步骤均成功，最终
manifest、根 CP artifact 一致性和 ELF layout verifier 全部通过。该 warning 不作为
实板正确性证据，也不掩盖后续必须执行的 clean rebuild。

### 4.1 首轮 focused review 修正

首轮可编译实现继续 review 后发现并修正：

- 旧逻辑把 `ready_tick` 同时当启动时间和运行期 RPTUN 异常起点。系统运行超过
  fault deadline 后，单次 shared/RPTUN 抖动会跳过 `SUSPECT` 直接 `FAULTED`。
  现已增加独立连续异常计时；启动连接 deadline 与运行期断连 deadline 分离。
- all-zero boot record 只在尚未 arm 时代表正常未启动。已 arm generation 若丢失
  header，必须按 `BAD_SHARED_STATE` 收敛，不能静默降成 `OFFLINE`。
- AP 首次尚未成功 PONG 和一次偶发 probe 失败原本会过早进入 `SUSPECT`。现改为以
  “距最后一次成功 PONG 的年龄”作为 authoritative deadline；成功历史不会被单次失败
  清除。
- `QUIESCING` 是 CP 主动生命周期状态，supervisor 在普通 `apctl stop/restart`
  中明确 disarm，避免把正常 teardown 误报为 crash。
- health callback registration 调整到 `rptun_initialize()` 前。若 callback allocation
  失败，不会留下当前 NuttX 版本无法 unregister 的半初始化 RPTUN 实例；CP semaphore
  失败路径也补齐 destroy。

同时新增 `CONFIG_BK7258_AP_SUPERVISOR_FAULT_INJECTION`（默认 n），并在当前
`cp_nsh_rptun` 验证 profile 显式开启。它只在用户运行以下命令时 CP-local 抑制一种
观察，不写 AP shared state：

```text
apctl inject primary
apctl inject secondary
apctl inject rpmsg
apctl inject clear
```

注入会由普通 lifecycle 或 `apctl recover` 清除；确认 `FAULTED` 后必须 recover，
不能用 clear 绕过 fail-closed。

上述修正已通过第二轮 fresh 双镜像构建：CP raw 220,564 bytes，AP raw 107,368
bytes，打包与 ELF layout verifier 均通过，layout 仍为 `rsc=264`、`vring=222/224`、
`carveout=0x7e80`、`spare=0x4cc0`。但随后的第二轮源码复核又发现下节问题，因此这组
镜像只证明首轮修正可编译，不作为最终待刷写镜像。

### 4.2 第二轮 focused review 修正

第二轮构建后的源码级状态机/并发复核发现：

- 同一 generation 已经 arm 后，若 AP boot state 意外从 `READY` 退回
  `STARTING/STOPPING`，旧分支会每轮 disarm，无法收敛到 confirmed fault。现改为
  连续 invalid-streak 计时，经过 suspect/fault deadline 后以
  `BAD_SHARED_STATE` fail-closed；只有尚未 arm 的正常启动或新 generation 才允许
  进入无故障的 `ARMING`。
- CPU2 状态不再是 `SCHEDULER_ONLINE` 时，旧分支统一误报为
  `RPTUN_DISCONNECTED`。现按 CPU2 `FAILED`/其他掉线状态分别归为 AP reported
  failure 或 secondary timeout，只有 CPU2 正常而 transport state 异常才归为
  RPTUN disconnected。
- 普通 `apctl start/stop/restart` 与 supervisor worker 原先没有共同 lifecycle gate。
  在 RPTUN 共享区清零/换 generation 的窄窗口，worker 可能依据旧快照继续更新
  `cp_epoch/cp_heartbeat`。现由 CP 私有 lifecycle begin/end gate 串行化：启停操作先
  等待 supervisor 离开临界区，进行中的 bounded probe 会被 endpoint teardown 唤醒，
  且返回结果在 lifecycle active 时被丢弃。该 gate 不修改共享 ABI，也不进入
  NuttX/SDK 源码。

这些第二轮修正已经通过第三轮 fresh 双镜像构建：CP raw 220,852 bytes、AP raw
107,368 bytes，CP/AP link、CRC pack、split-image manifest 及 ELF layout verifier 均
通过，layout 继续保持 `spare=0x4cc0`。

### 4.3 Kconfig 反向组合编译门禁

默认验证 profile 为 `AUTO_RECOVER=n`、`FAULT_INJECTION=y`。为覆盖被预处理器排除的
另一侧代码，又临时只修改生成态 NuttX `.config`，完成一次
`AUTO_RECOVER=y`、`FAULT_INJECTION=n` 的 CP-only 编译，结果 exit 0。随后执行
`distclean` 并通过仓库标准 `build.sh` 恢复默认 defconfig；未修改 NuttX 或 SDK
受跟踪源码。

因为重新生成的构建元数据会改变固件哈希，即使代码和大小相同，恢复默认配置后的根 CP
产物也可能不再与旧 dual manifest 位级一致。因此在恢复默认配置后又执行了一次标准
双镜像脚本作为最终一致性构建，结果 exit 0：

- CP raw：220,852 bytes，SHA-256
  `736ff977dc6fc4a1cdb2df860862a9fb0638faa45f53c92ac05cc695c81c1d4c`；
- CP CRC：234,668 bytes，SHA-256
  `cea061337d94b16e895b19eeec429f55482309da439f5af8d37f0fb5952ecc32`；
- AP raw：107,368 bytes，SHA-256
  `a8e5360e3e75f20638ba615467e04a5cea7ae4cf12a392f89ceb21c4ae293a4f`；
- AP CRC：114,104 bytes，SHA-256
  `6b5fa8a4846e1cb37456df973c43467acae5511e9aa9be13fe33ef2be97e1a43`。

脚本确认根目录 `app.bin/app_crc.bin` 与 manifest 中 CP 镜像逐字节一致；RPTUN ELF
layout verifier 再次 `PASS`：resource table 264 bytes、vring 222/224、carveout
`0x7e80`、spare `0x4cc0`。最终生成配置为 supervisor=y、auto-recover=n、
fault-injection=y。CP/AP ELF 均无 undefined symbol，supervisor、health probe 和 AP
secondary heartbeat 的关键符号均已进入对应 ELF；`git diff --check` 通过。

### 4.4 实板压力修复与闭环证据

2026-08-01 在真实 T5-AI 上完成 build→稀疏烧录→串口判定闭环。首轮重复压力暴露两类
不是扩大 timeout 能解决的问题，均只在 board wrapper 修复：

- AP health callback 在 TX buffer 暂缺时不再阻塞 RPTUN RX，而是交给固定 CPU0 的
  高优先级 worker bounded retry；
- RPTUN RX 在保留的 control word 中发布 CP/AP 双向 sequence。业务流量已经证明双向
  活性时，supervisor 延后 best-effort PING，避免健康探测与满帧业务争抢 buffer；
- AP primary 管理循环保持 priority 200，测试 TX gateway 降为 190；CPU0 load worker
  删除只会旋转同优先级任务的 `sched_yield()`，避免形成 SMP scheduler/IPI storm；
- 长时间重复测试最终定位到 logical CPU1 每帧 TX/reply semaphore 跨核唤醒可触发 AP
  SMP 调度死锁。测试 wrapper 改为固定请求缓冲、release/acquire 原子状态、AON RTC
  timeout 与 `WFE/SEV` dispatch；CPU0 仍保留标准 semaphore 路径。未修改 NuttX/SDK。

最终 fresh 双镜像构建 exit 0：CP raw 221,412 bytes、AP raw 108,172 bytes；SDK
v3.1.1.9 checksum、split manifest 与 ELF layout verifier 全部通过，layout 保持
`rsc=264`、`vring=222/224`、`carveout=0x7e80`、`spare=0x4cc0`。

同一冷启动 generation=1 的实板结果：

- `bkrpmsgtest run 100 464 load 60000` 连续 4 次 PASS；
- `bkrpmsgtest all 100 60000` 连续 2 套、每套 6/6 PASS，累计运行到 run=16；
- 每个双核发送端均 `sent=received=100`、`errors=0`，所有 heap snapshot 前后相同；
- 结束状态为 AP `READY`、CPU2 `SCHEDULER_ONLINE`、RPTUN `CONNECTED`、supervisor
  `HEALTHY`、pending=0、faults=0，CP/AP RX sequence 持续增长。

生命周期与 fail-closed 结果：

- `apctl restart 30000` 使 generation 1→2；异步重连收敛到
  `CONNECTED/HEALTHY`，随后双核 `20×464/load` PASS；
- `apctl inject primary` 后约 2.52 s 收敛到 supervisor
  `FAULTED/PRIMARY_TIMEOUT`，RPTUN 同步 `FAULTED/error=110`，而 AP/CPU2 live counter
  证明这是 CP-local synthetic injection；
- FAULTED 状态下 `inject clear` 返回 `-EBUSY`，符合“不能 clear 绕过 fail-closed”的
  设计；`apctl recover 30000` 原子清除 injection 并使 generation 2→3；
- generation=3 最终为 `READY/CONNECTED/HEALTHY`，fault/recovery=`1/1`、
  consecutive=0、injection=`NONE`，恢复后的双核 `20×464/load` 再次 PASS。

### 4.5 三类故障注入收口与恢复后 soak

同一已刷写镜像继续完成剩余的 secondary/RPMsg 分类、fail-closed、人工恢复和无注入
重复压力门禁。原始串口日志保存在工作区
`logs/n10-closure-20260801/`（主仓库外，不纳入提交）。

Secondary 路径从健康的 generation=3 开始：

- `apctl inject secondary` 后收敛到 supervisor
  `FAULTED/SECONDARY_TIMEOUT`，secondary age=`2710 ms`，primary age=`0`；RPTUN 同步
  `FAULTED/error=110`，AP primary 与 CPU2 live heartbeat 仍持续推进，证明注入仅抑制
  CP 侧的一路观察；
- FAULTED 后执行 `bkrpmsgtest run 1 1 idle 5000` 立即返回
  `BRPT FAIL transport=-107`，没有等待命令的 5 s deadline，旧 generation 确实
  fail-closed；
- `apctl recover 30000` 使 generation 3→4。收敛后 AP/RPTUN/supervisor 分别为
  `READY/CONNECTED/HEALTHY`，fault/recovery=`2/2`、consecutive=0、
  injection=`NONE`；随后双核 `20×464/load` 两路均 `20/20`、`errors=0`。

RPMsg 路径从健康的 generation=4 开始：

- `apctl inject rpmsg` 后收敛到 supervisor `FAULTED/RPMSG_TIMEOUT`，transport
  age=`2520 ms`，primary/secondary age 均为 `0`；RPTUN 为
  `FAULTED/error=110`，AP 与 CPU2 heartbeat 正常推进；
- 旧链路上的 `bkrpmsgtest run 1 1 idle 5000` 同样立即返回
  `BRPT FAIL transport=-107`；
- `apctl recover 30000` 使 generation 4→5。收敛后为
  `READY/CONNECTED/HEALTHY`，fault/recovery=`3/3`、consecutive=0、
  injection=`NONE`；恢复后的双核 `20×464/load` 两路均 `20/20`、`errors=0`，
  heap snapshot 前后相同。

在 generation=5 上不再注入故障，连续执行两轮
`bkrpmsgtest all 100 60000`。每轮均覆盖 payload `1/64/464` × idle/load 六种组合，
两轮分别为 run=21..26 和 run=27..32：

- 12/12 场景 PASS，每个场景的两个发送端均 `sent=received=100`、`errors=0`，
  合计完成 2400 次双路 request/reply；
- 12 组 heap 的 start/spawn/report 均保持 used=`44352`、free=`219696`、
  largest=`216024`、alloc/free blocks=`53/2`，没有泄漏或碎片漂移；
- 最终状态保持 generation=5：AP `READY`、RPTUN `CONNECTED`、supervisor
  `HEALTHY`、CPU2 `SCHEDULER_ONLINE`、pending=`0/0`；fault/recovery=`3/3`、
  consecutive=0、injection=`NONE`。CP/AP RX sequence=`6026/3212`，AP/CPU2
  heartbeat=`3124/6280`，证明数据面和两路 scheduler liveness 均继续推进。

## 5. 当前门禁与下一最小动作

当前检测与人工恢复基线可整体标记为 `board-verified`。Focused source review、
默认/反向 Kconfig、fresh 双镜像、ELF symbol/layout、重复满载、warm restart、三路
独立故障分类、旧链路 fail-closed、连续三次 generation-safe manual recovery，以及
恢复后的两轮无注入 full-suite soak 均已闭环。

自动恢复仍是独立的可选能力，当前默认关闭且不属于上述已验证基线。后续若决定启用，
必须先单独评审恢复预算、退避和 `LOCKOUT` 语义，再使用专用 profile 重跑三类注入、
重试耗尽和无注入 soak；更长时间的小时级 soak 可作为发布前运行可靠性门禁继续扩展。

## 6. 禁止事项

- 不修改 `nuttx/` 或官方 SDK bundle；
- 不把 supervisor callback 放进 mailbox ISR；
- 不在 RPMsg callback 中阻塞、restart 或等待 TX buffer；
- 不把 CPU2 heartbeat 与 primary heartbeat 合并成单一“AP 活着”结论；
- 不在自动恢复预算/退避/lockout 专项评审和实板验证前启用默认自动恢复。
