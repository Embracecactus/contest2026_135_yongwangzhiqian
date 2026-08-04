# Beken BK7258（Tuya T5-AI）openvela / NuttX 移植

把 openvela / NuttX 移植到 Beken BK7258（ARM Cortex-M33 三核、Wi-Fi 6 + BLE 5.4）Tuya T5-AI
模组。BootROM → Tier-1 bootloader → CPU0/CP NuttX、NSH、LittleFS、CPU0 IRQ/GPIO 等既有
阶段已有板端证据。**Stage N14 已于 2026-08-03 完成 `board-verified`**：T5-AI 实板
识别 APS128XXO `id=0x8d08/config=0x8d1a` 16 MiB PSRAM，全容量 boot gate、CP/AP 独立 heap、
AP logical CPU0/CPU1 并发 allocator、SDK deferred timer/self-delete、warm cycle、physical cold、
factory 首次校准与既有 RPMsg/Bluetooth 回归均闭环；官方 NuttX/apps/SDK 及 SDK 静态库保持只读。
**当前 Stage N15 进行中，N15-M 已 `board-verified`、N15-A 已 `host-verified`、N15-B/C/D/E/F
已 `host/source/ELF-verified`、N15-V fault/campaign 已 `host/source/ELF/dry-run-verified`**：项目采用 ADR-004 official-style
连续 CP/AP A/B 布局，AP XIP 迁至 `0x02150000`，LittleFS 迁至
`0x600000..0x700000`。一次性两段式迁移和完整保留功能回归、physical RESET 3/3 均通过；
B 目前只是不可选择 seed，新 staging/publication/trial代码尚未写板；normal image六门为零且无
`bkota`，独立validation profile、目标侧5000 ms health gate（250 ms轮询）和fixed-PSRAM
transport已通过host/ELF门禁；host模型中的1000 ms仅为加速测试参数。
target fault 7/12、format-2双bank A→B→A的16份独立包、独立campaign verifier与16次
loader dry-run均PASS；实板验证需fresh authority后另行定义。ADR-003 sector-swap只保留为历史研究。
Stage N7 已完成物理 CPU1 独立单核 AP NuttX 板级启动，并完成 CPU0
间歇性 task-exit HardFault 的四文件 team-overlay 最小修复。Stage N8-A 已把物理 CPU2 作为
AP logical CPU1 freestanding probe 启动并于 2026-07-29 `board-verified`。Stage N8-B1 也已完成
真实板测：CPU2 通过 `up_cpu_start()` 验证 vector/VTOR/MSP 和 logical CPU1 IDLE stack 后到达
`SECONDARY_READY`，`online_mask=0x1`、`smp_call_requests=0`，restart、stop/start 和三轮 cycle
均通过。**Stage N8-B2** logical CPU0↔logical CPU1 双向 IPI 已于 2026-07-29 在真实 T5-AI
板卡完成闭环：数据面继续沿用 Beken SDK mailbox/cross-core wrapper 和 team-owned NuttX IRQ
bridge；CPU2 first IRQ 的 NOCP 已通过补齐 per-core CPACR/FPCCR 初始化修复；`apitest 1` 两次、
`apitest 100` 两次以及 restart、stop/start、三轮 cycle 和最终恢复均通过。CPU2 仍停留在 WFI
park，`online=0x1`、`calls=0`。**Stage N8-C1** 已于 2026-07-30 完成真实板卡闭环：CPU2
进入 NuttX `nx_idle_trampoline()` 的 scheduler-online IDLE 路径，CPU0→CPU1→CPU0 自动 SMP-call
handshake 双向闭合；AP 缺失的 STAR `arm_doirq`/`nxsched_resume_scheduler` wrapper 已通过
SMP-safe per-core 实现补齐，AP heartbeat、CPU0 SysTick 和周期 sleep-return 持续增长。独立
`ap_smp_online` 配置仍以 `CONFIG_SMP_DEFAULT_CPUSET=0x1` 把普通 task 限制在 logical CPU0。**Stage N8-C2** 已于 2026-07-30 在真实板卡闭环：独立 `ap_smp_affinity` 配置只创建一个在激活前显式绑定 logical CPU1 mask `0x2` 的诊断 pthread；task 实际运行在 CPU1，started/completed/pid-released=`1/1/1`，remote SMP tx/rx、CPU1 IRQ/wake 和 calls 均精确 +1，failure=0；默认 cpuset 仍为 `0x1`。**Stage N8-C3** 已于 2026-07-30 在用户真实 T5-AI 的当前 download/warm-start path 完成 `board-verified` 闭环：AP `READY/error=0`、CPU2 `SCHEDULER_ONLINE/error=0`，`BSMP`、`BAFF`、`BSEM` 全部 `PASSED/error=0`；同一 `task id=3` 在 logical CPU1 首次 dispatch 后进入 zero-count semaphore wait，由 logical CPU0 single post 并在 CPU1 返回，最终 PID released。BAFF aggregate `+2` 与 BSEM isolated `+1` 精确区分同一 single task 的 dispatch 和 semaphore remote wake，不是第二个 task。heartbeat=`751`、CPU0 SysTick=`8329`、sleep enter/return=`751/750` 已证明 gate 后持续运行，不需要追加稳定性 sample。**Stage N8-C4** 已于 2026-07-30 在用户真实 T5-AI 当前 download/warm-start path 完成 `board-verified` 闭环：同一 `task id=3`、同一 semaphore 固定 8 轮 exact block → CPU0 single post → CPU1 wake；BAFF aggregate `+9`、BSEM first-cycle `+1`、BSWL full-loop `+8` 全部精确命中，BSMP/BAFF/BSEM/BSWL 均 `PASSED/error=0`，PID released，failure/coalesced/stale/spurious=0。**Stage N8-C5** 已于 2026-07-30 在真实 T5-AI normal autostart path 完成 `board-verified`：PID5/CPU0 initiator 与 PID4/CPU1 responder 以 explicit `SCHED_FIFO`、priority=controller+1 固定完成 8 轮双向 semaphore pingpong；BP2P `PASSED/error=0`，CPU0→CPU1 `+9`、CPU1→CPU0 `+8`、calls `+17` 精确命中，PID released，coalesced/fail/stale/spurious=0。N8-C5 保留为双向 remote-wake verified baseline。**Stage N8-C6** 首个板端镜像曾在 sequence `2/2` 超时；加入 same-CPU local `sched_yield()`、立即发布 PID 和 exact starter-waiter 证明后，已于 2026-07-30 在真实 T5-AI normal autostart path 完成 `board-verified`：两个 CPU1 task sequence `8/8`，CPU0→CPU1 `+3`、反向 `+0`、calls `+3`。N8-C6 保留为已验证的 CPU1 local-scheduling baseline。**Stage N8-C7** 的 lock-free waiter-poll correction 已于 2026-07-30 在真实 T5-AI normal autostart path 完成 `board-verified`：BMIG `PASSED/error=0`、requested/completed=`8/8`，单 task 最终回到 CPU0、sequence=`8`、callback=`8/8`、PID released；CPU0→CPU1 `10→14`=`+4`、CPU1→CPU0 `1→5`=`+4`、calls `11→19`=`+8` 精确命中，handler call/delivered CPU0=`5/5`、CPU1=`14/14`，coalesced/fail/stale/spurious=0。heartbeat `58→155→258`、CPU0 SysTick `789→1849→2989` 证明 gate 后持续运行。N8-C7 保留为 controlled-migration verified baseline。**Stage N8-C8** 的 corrected image 已于 2026-07-30 在真实 T5-AI same-image generation2 restart path 完成 `board-verified`：prerequisites 全 PASS，BTIM `PASSED/error=0`、requested/completed=`8/8`；PID4/CPU1 started/completed=`1/1`、sequence=`8`、value=`8/0`、aux=`20000/1`。exact attribution 为 initial dispatch + 8 timer wakes：CPU0→CPU1 `10→19`=`+9`、反向 `1→1`=`+0`、calls `11→20`=`+9`，handler fully delivered，failure/coalesced/stale/spurious=0。heartbeat `1→62`、CPU0 SysTick `52→730` 证明 gate 后持续运行。N8-C8 保留为 timed-wake verified baseline。**Stage N8-D1** 已于 2026-07-30 在真实 T5-AI normal autostart path 完成 `board-verified`：BLCY `PASSED/error=0`、requested/completed=`1/1`，callback entry/exit CPU=`1/1`、sequence=`1/1`、value=`0/-138`（`-138 == -ENOTSUP`）、aux=`1/1`；CPU0→CPU1 `10→11`=`+1`、反向 `1→1`=`+0`、calls `11→12`=`+1`，online 始终 `0x3`，handler fully delivered，failure/coalesced/stale/spurious=0。heartbeat=`727`、CPU0 SysTick=`8090`、sleep enter/return=`727/726` 证明 gate 后持续运行。N8-D1 现为 latest verified baseline；已授权的 N8-C5..N8-D1 实现/板测集合全部完成，当前 MAIN Stage 已选择 N9 RPTUN/RPMsg。默认 cpuset 继续保持 `0x1`；不开放非受控 migration、默认 `0x3`、运行时可变/无限循环或 stress test。physical cold-reset 的历史 open issue 已在 2026-07-31 关闭，详见下方状态勘误。

> **2026-07-31 状态勘误：** 上面的阶段叙述截至 N8-D1，当时把 physical cold-reset
> 写成 open issue。该项现已关闭：最终无 checkpoint 镜像 warm 3/3、physical-reset
> 3/3，AP READY/CPU2/SMP gates 全部通过；仅 power cut 未验证。
>
> **2026-08-03 N14 closure：**CP 按 official v3.1.1.9 PM vote成为唯一PSRAM硬件owner，AP只
> 建立role-local heap；normal保留official低8 MiB布局，上8 MiB只做boot-tested/unallocated；
> N15-F另以固定地址做volatile validation transport，不开放allocator。
> AP双核固定16/16分配回归、CP heap 256/256、timer 256、AP cycle10、physical RESET 3/3、
> final clean/factory first-calibration/post-calibration cold均PASS。详见
> [`nuttx-port/prompts/14-n14-psram.md`](nuttx-port/prompts/14-n14-psram.md)。
>
> 详细技术报告（评委请读这份）：**[porting-report.md](porting-report.md)**
> N2 worklog：[`nuttx-port/n2-nsh-console.md`](nuttx-port/n2-nsh-console.md)
> N3 worklog：[`nuttx-port/n3-procfs-ps.md`](nuttx-port/n3-procfs-ps.md)
> N4-D0/D0D worklog：[`nuttx-port/n4-d0-clock-diag.md`](nuttx-port/n4-d0-clock-diag.md)
> N5 flash filesystem worklog（D5 raw flash r/w + D6 MTD + D7 LittleFS，board-verified 2026-07-19）：[`nuttx-port/n5-flash-filesystem.md`](nuttx-port/n5-flash-filesystem.md)
> N7 CPU1/AP 单核启动链 worklog：[`nuttx-port/n7-ap-singlecore-bringup.md`](nuttx-port/n7-ap-singlecore-bringup.md)
> N7 CPU0 task-exit HardFault 根因与最小修复（board-verified 2026-07-29）：[`nuttx-port/n7-bug-cpu0-task-exit-hardfault.md`](nuttx-port/n7-bug-cpu0-task-exit-hardfault.md)
> N8-A 物理 CPU2 freestanding probe bring-up（board-verified 2026-07-29）：[`nuttx-port/n8-a-cpu2-probe-bringup.md`](nuttx-port/n8-a-cpu2-probe-bringup.md)
> N8-B1 AP SMP secondary bootstrap（board-verified 2026-07-29）：[`nuttx-port/n8-b1-smp-secondary-bootstrap.md`](nuttx-port/n8-b1-smp-secondary-bootstrap.md)
> N8-B2 AP 双向 IPI（board-verified 2026-07-29）：[`nuttx-port/n8-b2-bidirectional-ipi.md`](nuttx-port/n8-b2-bidirectional-ipi.md)
> N8-C1 CPU2 scheduler-online IDLE（board-verified Gate 1，2026-07-30）：[`nuttx-port/n8-c1-scheduler-online-idle.md`](nuttx-port/n8-c1-scheduler-online-idle.md)
> N8-C2 logical CPU1 explicit-affinity one-task gate（board-verified，2026-07-30）：[`nuttx-port/n8-c2-cpu1-affinity-task.md`](nuttx-port/n8-c2-cpu1-affinity-task.md)
> N8-C3 CPU1-bound single-task semaphore remote wake（historical baseline，board-verified 2026-07-30）：[`nuttx-port/n8-c3-cpu1-semaphore-remote-wake.md`](nuttx-port/n8-c3-cpu1-semaphore-remote-wake.md)
> N8-C4 same-task fixed 8-cycle semaphore remote wake（historical baseline，board-verified 2026-07-30）：[`nuttx-port/n8-c4-cpu1-semaphore-wake-loop.md`](nuttx-port/n8-c4-cpu1-semaphore-wake-loop.md)
> N8-C5 bidirectional semaphore pingpong（board-verified 2026-07-30）：[`nuttx-port/n8-c5-bidirectional-semaphore-pingpong.md`](nuttx-port/n8-c5-bidirectional-semaphore-pingpong.md)
> N8-C6 dual CPU1 local scheduling（board-verified 2026-07-30）：[`nuttx-port/n8-c6-dual-cpu1-local-scheduling.md`](nuttx-port/n8-c6-dual-cpu1-local-scheduling.md)
> N8-C7 controlled migration（LATEST VERIFIED，board-verified 2026-07-30）：[`nuttx-port/n8-c7-controlled-migration.md`](nuttx-port/n8-c7-controlled-migration.md)
> N8-C8 CPU1 timed wake（board-verified 2026-07-30）：[`nuttx-port/n8-c8-cpu1-timed-wake.md`](nuttx-port/n8-c8-cpu1-timed-wake.md)
> N8-D1 scheduler quiesce/resume foundation（LATEST VERIFIED，board-verified 2026-07-30；not hot-unplug）：[`nuttx-port/n8-d1-smp-lifecycle-quiesce.md`](nuttx-port/n8-d1-smp-lifecycle-quiesce.md)
> N8 physical cold-reset / AP SMP 最终修复复盘（warm 3/3、RESET 3/3）：[`nuttx-port/n8-cold-reset-resolution-report.md`](nuttx-port/n8-cold-reset-resolution-report.md)
> N9 CP/AP RPTUN/OpenAMP/RPMsg wrapper 完成记录（LATEST VERIFIED，`board-verified`）：[`nuttx-port/prompts/09-n9-rptun-rpmsg.md`](nuttx-port/prompts/09-n9-rptun-rpmsg.md)；
> source verification：[`nuttx-port/n9-rptun-source-verification.md`](nuttx-port/n9-rptun-source-verification.md)；
> 17 项评审处置：[`nuttx-port/n9-plan-review-2026-07-31.md`](nuttx-port/n9-plan-review-2026-07-31.md)
> N10 AP health supervision（`board-verified`）：[`nuttx-port/prompts/10-n10-ap-supervision.md`](nuttx-port/prompts/10-n10-ap-supervision.md)
> N11 RPMsgFS（`board-verified`）：[`nuttx-port/prompts/11-n11-rpmsgfs.md`](nuttx-port/prompts/11-n11-rpmsgfs.md)
> N12 official Bluetooth IPC + NuttX HCI wrapper（`board-verified`）：[`nuttx-port/n12-beken-bt-ipc-wrapper.md`](nuttx-port/n12-beken-bt-ipc-wrapper.md)
> N13 BLE GAP/GATT（`board-verified`）：[`nuttx-port/prompts/13-n13-ble-gap-gatt.md`](nuttx-port/prompts/13-n13-ble-gap-gatt.md)；
> 最终证据：[`nuttx-port/n13-evidence-index.md`](nuttx-port/n13-evidence-index.md)；
> source verification：[`nuttx-port/n13-ble-gap-gatt-source-verification.md`](nuttx-port/n13-ble-gap-gatt-source-verification.md)；
> evidence index：[`nuttx-port/n13-evidence-index.md`](nuttx-port/n13-evidence-index.md)
> N14 PSRAM + SDK timer（LATEST VERIFIED，`board-verified`）：[`nuttx-port/prompts/14-n14-psram.md`](nuttx-port/prompts/14-n14-psram.md)；
> source verification：[`nuttx-port/n14-psram-source-verification.md`](nuttx-port/n14-psram-source-verification.md)；
> evidence index：[`nuttx-port/n14-evidence-index.md`](nuttx-port/n14-evidence-index.md)
> N15 paired CP/AP OTA（CURRENT，N15-M layout migration `board-verified`）：[`nuttx-port/prompts/15-n15-tier2-ota.md`](nuttx-port/prompts/15-n15-tier2-ota.md)；
> N15-M evidence：[`../../progress/verification/2026-08-03-n15-migration-board-verification.md`](../../progress/verification/2026-08-03-n15-migration-board-verification.md)；
> N15-A evidence：[`../../progress/verification/2026-08-03-n15-a-host-pair-bundle.md`](../../progress/verification/2026-08-03-n15-a-host-pair-bundle.md)；
> N15-B evidence：[`../../progress/verification/2026-08-04-n15-b-host-staging.md`](../../progress/verification/2026-08-04-n15-b-host-staging.md)；
> N15-C evidence：[`../../progress/verification/2026-08-04-n15-c-host-boot-selection.md`](../../progress/verification/2026-08-04-n15-c-host-boot-selection.md)；
> N15-D evidence：[`../../progress/verification/2026-08-04-n15-d-host-trial.md`](../../progress/verification/2026-08-04-n15-d-host-trial.md)；
> N15-E evidence：[`../../progress/verification/2026-08-04-n15-e-host-publication.md`](../../progress/verification/2026-08-04-n15-e-host-publication.md)；
> N15-F evidence：[`../../progress/verification/2026-08-04-n15-f-host-validation.md`](../../progress/verification/2026-08-04-n15-f-host-validation.md)；
> N15-V host evidence：[`../../progress/verification/2026-08-04-n15-v-host-fault-injection.md`](../../progress/verification/2026-08-04-n15-v-host-fault-injection.md)；
> historical ADR-003 source verification：[`nuttx-port/n15-ota-source-verification.md`](nuttx-port/n15-ota-source-verification.md)
> 小白入门版修复过程：[`nuttx-port/cold-reset-smp-repair-guide.md`](nuttx-port/cold-reset-smp-repair-guide.md)
> SDK v3.1.1.9 迁移、legacy 回退、ABI 与实板证据：[`nuttx-port/sdk-v3.1.1.9-migration-report.md`](nuttx-port/sdk-v3.1.1.9-migration-report.md)
> SDK CP/AP 静态库编译、导入与校验 SOP：[`nuttx-port/sdk-static-library-import.md`](nuttx-port/sdk-static-library-import.md)
> WSL2 自动编译、Windows 下载、COM11 采集与 RESET 调试 SOP：[`nuttx-port/bk7258-build-flash-debug-sop.md`](nuttx-port/bk7258-build-flash-debug-sop.md)
> 可独立开源的 Windows/WSL2 通用 UART/J-Link 工具与 Claude/Codex Skill：[`../../tools/windows-hardware-debug/README.md`](../../tools/windows-hardware-debug/README.md)
> Git worktree 同步与 PR 交接记录：[`nuttx-port/git-worktree-sync-2026-07-27.md`](nuttx-port/git-worktree-sync-2026-07-27.md)
> 主 Stage 索引 / 当前恢复入口：[`next-stage-prompt.md`](next-stage-prompt.md)

## 当前状态

| 工作项 | 状态 |
|---|---|
| 官方 SDK v3.1.1.9 CP/AP 静态库迁移 | ✅ 唯一active版本；legacy/旧 source/GitHub参考仓只保留，N15完整板测前不参与分析/构建/验证 |
| 两家 bootloader 逆向（涂鸦 + BK 官方） | ✅ 当前 raw NuttX 启动契约已由 v3.1.1.9/Ghidra/板端交叉验证；不宣称官方 52 KB 全功能语义等价 |
| Tier-1 bootloader（asm + C + 硬化跳转） | ✅ 板端验证 |
| 启动核 = CPU0（关键决策） | ✅ 板端坐实 |
| 开源 CRC packer（闭源 `cmake_encrypt_crc` 等价替代） | ✅ 字节等价已证 |
| NuttX Stage N1（bootloader 跳进 NuttX，早期 UART） | ✅ `board-verified` |
| NuttX Stage N2（`nx_start` → 交互式 NSH） | ✅ `board-verified`（2026-07-18，4 RX bug 全修） |
| NuttX Stage N3（procfs + `ps`） | ✅ `board-verified`（2026-07-18） |
| NuttX Stage N4（DPLL / 480 MHz clock bring-up） | 历史：N4-D0/D0D/D0F `board-verified`（substage，D0/D0D `6f596b7`，D0F `8dab594`）；N4-D1 blocked；当前产品路径采用已验证的 320 MHz runtime DVFS，不继续追 480 MHz |
| NuttX Stage N4 — D0/D0D（时钟诊断 baseline + runtime SysTick bookkeeping） | ✅ substage `board-verified`（2026-07-18，feature commit `6f596b7`，3 个 overlay 文件） |
| NuttX Stage N4 — D0F（100Hz SysTick tick-rate 兼容性） | ✅ substage `board-verified`（2026-07-18，feature commit `8dab594`，defconfig 移除 100ms override） |
| NuttX Stage N5（flash layout / ID / filesystem） | **N5-D0..D4 board-observed**（2026-07-19）；**N5-D5 raw flash r/w board-verified**（2026-07-19）；**N5-D6 MTD board-verified**（方案 A，CONFIG_BK7258_FLASH_MTD）；**N5-D7 LittleFS filesystem board-verified**（/data 挂载，probe 文件重启持久化通过）；D7 版 `all-app.bin` = 192270 B = `0x2EF0E`（< `0x100000`，boot/app 区不受影响） |
| NuttX Stage N6（CPU0 SDK IRQ/GPIO） | CPU0 vectors、TIMER1 IRQ 与 GPIO C0/C1/C2 已有板端验证；作为 N7 回归基线保留 |
| **NuttX Stage N7（CPU1 独立单核 AP NuttX）** | ✅ `board-verified`：physical CPU1、AP READY、runtime VTOR/MSP、320 MHz、SysTick、heap 与 heartbeat 已通过 |
| **N7 CPU0 间歇性 task-exit HardFault** | ✅ `board-verified`（2026-07-29）：NULL context restore + 非 HIPRI IRQ 嵌套；官方 NuttX 无修改，最终仅 4 个 team-overlay 文件 |
| **NuttX Stage N8-A（CPU2 freestanding probe）** | ✅ `board-verified`（2026-07-29，pre-N15布局）：physical CPU2 = AP logical CPU1，当时vector/VTOR `0x02200200`；N15-M当前地址为`0x02150200` |
| **NuttX Stage N8-B1（AP SMP secondary bootstrap）** | ✅ `board-verified`（2026-07-29，pre-N15布局）：CPU2 `SECONDARY_READY`、当时vector/VTOR `0x02200200`、合法boot/IDLE stack、`online=0x1`、`calls=0`；N15-M当前地址为`0x02150200` |
| **NuttX Stage N8-B2（AP 双向 IPI）** | ✅ `board-verified`（2026-07-29）：SDK mailbox/cross-core wrapper、IRQ79、CPU2 WFI wake 与双向 PING/PONG 已通过；CPU2 first IRQ NOCP 已由 CPACR/FPCCR 初始化修复；`apitest 1`×2、`apitest 100`×2、restart、stop/start、三轮 cycle 和最终恢复全部通过，`online=0x1`、`calls=0` |
| **NuttX Stage N8-C1（CPU2 scheduler-online IDLE）** | ✅ `board-verified`（2026-07-30）：CPU2 以 `CONTROL=0x2`、独立 MSP interrupt stack 进入 `SCHEDULER_ONLINE`/`online=0x3`；双向 wrapper-backed SMP-call 全部闭合；SMP-safe STAR dispatcher wrapper 修复后 AP heartbeat `46→75→115`、CPU0 SysTick `543→864→1306`、sleep enter/return `46/45→75/74→115/114`，CPU1 SysTick=0，failure/stale/spurious=0；普通 task 仍限制在 CPU0 |
| **NuttX Stage N8-C2（CPU1 explicit-affinity one-task）** | ✅ `board-verified`（2026-07-30）：AP READY/CPU2 scheduler-online/N8-C1 SMP baseline 保持健康；唯一 pthread requested/observed=`0x2/0x2`、cpu=1、started/completed/pid-released=`1/1/1`；CPU0 tx、CPU1 rx、CPU1 IRQ/wake、calls 均精确 +1，failure=0；默认 cpuset 保持 `0x1` |
| **NuttX Stage N8-C3（CPU1-bound block → CPU0 remote wake）** | ✅ `board-verified` historical baseline（2026-07-30）：AP READY、CPU2 scheduler-online、BSMP/BAFF/BSEM 全部通过；同一 `task id=3` wait/post/return/PID-release 闭环；BAFF aggregate `+2` 与 BSEM isolated `+1` 精确证明同一 task 的 dispatch + semaphore remote wake |
| **NuttX Stage N8-C4（same-task fixed 8-cycle semaphore wake loop）** | ✅ `board-verified` historical baseline（2026-07-30）：唯一 detached pthread/affinity `0x2`/同一 static semaphore 完成 8 轮；BAFF `+9`、BSEM `+1`、BSWL `+8`，coalesced/fail/stale/spurious=0 |
| **NuttX Stage N8-C5（bidirectional semaphore pingpong）** | ✅ `board-verified`（2026-07-30，normal autostart path）：PID5/CPU0 initiator 与 PID4/CPU1 responder 使用 explicit `SCHED_FIFO`、priority=controller+1 完成 8/8；BP2P PASSED，CPU0→CPU1 `10→19`=`+9`、CPU1→CPU0 `1→9`=`+8`、calls `11→28`=`+17`，PID released，coalesced/fail/stale/spurious=0 |
| **NuttX Stage N8-C6（dual CPU1 local scheduling）** | ✅ `board-verified`（2026-07-30，normal autostart path）：两个 CPU1-bound task 完成 sequence `8/8`、started/completed=`1/1`、PID released；BDUL PASSED，CPU0→CPU1 `10→13`=`+3`、反向 `1→1`=`+0`、calls `11→14`=`+3`，coalesced/fail/stale/spurious=0 |
| **NuttX Stage N8-C7（controlled migration）** | ✅ **LATEST VERIFIED：`board-verified`**（2026-07-30，normal autostart path）：BMIG PASSED、requested/completed=`8/8`；单 task final CPU0、started/completed=`1/1`、sequence=`8`、callback=`8/8`、PID released；CPU0→CPU1 `10→14`=`+4`、CPU1→CPU0 `1→5`=`+4`、calls `11→19`=`+8`，handler call/delivered fully closed，coalesced/fail/stale/spurious=0；未进行新评审 |
| **NuttX Stage N8-C8（CPU1 timed wake）** | ✅ `board-verified`（2026-07-30，same-image generation2 restart path）：BTIM PASSED、requested/completed=`8/8`；PID4/CPU1 started/completed=`1/1`、sequence=`8`、value=`8/0`、aux=`20000/1`；CPU0→CPU1 `10→19`=`+9`、反向 `1→1`=`+0`、calls `11→20`=`+9`，handler fully delivered，coalesced/fail/stale/spurious=0；generation1 inherited prerequisite stall 保留为非 BTIM failure，未进行新评审 |
| **NuttX Stage N8-D1（scheduler quiesce/resume foundation）** | ✅ **LATEST VERIFIED：`board-verified`**（2026-07-30，normal autostart path）：BLCY PASSED、requested/completed=`1/1`；callback entry/exit CPU=`1/1`、started/completed=`1/1`、sequence=`1/1`、value=`0/-138` (`-ENOTSUP`)、aux=`1/1`；CPU0→CPU1 `10→11`=`+1`、反向 `1→1`=`+0`、calls `11→12`=`+1`，online=`0x3`，handler fully delivered，coalesced/fail/stale/spurious=0；不实现或声称 hot-unplug，未进行新评审 |
| **N8 physical cold-reset 启动覆盖** | ✅ 2026-07-31 最终无 checkpoint 镜像连续 warm 3/3、COM7 RTS physical-reset 3/3，均到 NSH；cold 均有 `BClk`。AP `READY/error=0`、CPU2 `SCHEDULER_ONLINE/error=0`、已启用 SMP gates 全 `PASSED/error=0`。修复覆盖 UART GPIO/clock/TX-empty handoff、boot/cache/MPU、WDT ownership、CPU2 handshake 和 bounded poll scheduling；power cut 尚未执行 |
| **NuttX Stage N9（CP/AP RPTUN/OpenAMP/RPMsg）** | ✅ **LATEST VERIFIED：`board-verified`（2026-07-31）**。官方 SDK/NuttX 只读 wrapper；32 KiB carveout、AP CPU0 gateway、动态 Name Service、双 producer、满帧 load、generation reconnect、syslog 与三类兼容构建闭环 |
| **NuttX Stage N10（AP health supervision）** | ✅ `board-verified`：三路健康信号、三类故障注入、旧链路 fail-closed、人工恢复与 generation=5回归闭环；自动恢复默认关闭 |
| **NuttX Stage N11（RPMsgFS）** | ✅ `board-verified`：CP独占 LittleFS、AP stock RPMsgFS client、四档 payload、故障态有界失败与 generation recovery闭环 |
| **NuttX Stage N12（official Bluetooth IPC + NuttX HCI）** | ✅ **LATEST VERIFIED：`board-verified`（2026-08-02）**：Controller/Host初始化、MAC持久化、真实 RF scan report与RPMsg/RPMsgFS/SMP共存闭环 |
| **NuttX Stage N13（BLE GAP/GATT Peripheral）** | ✅ `board-verified`（2026-08-03）：四类negative、20/20 uncached重连、BLE 100帧与RPMsg六场景×100/RPMsgFS四档×20主动并发、3/3 cold、最终`25/25/25` lifecycle与`bt_conn.ref=0`全部闭环 |
| **NuttX Stage N14（PSRAM + SDK timer wrapper）** | ✅ `board-verified`（2026-08-03）：实板16 MiB识别与全容量boot gate；CP 128 KiB/AP 640 KiB独立heap；AP CPU0/CPU1 `16/16`、free稳定；timer 256、AP cycle10、physical cold/factory校准与RPMsg/Bluetooth回归全部闭环 |
| **NuttX Stage N15（paired CP/AP OTA + rollback）** | **CURRENT / N15-M `board-verified`；ADR-006 format-2双bank A→B→A `host/source/ELF/dry-run-verified`**：16份独立包、独立verifier与loader dry-run均PASS。实板仍A-only；physical OTA需fresh authority |
| MTD / 文件系统 | ✅ board-verified（N5-D6 MTD + N5-D7 LittleFS，/data 挂载） |
| NuttX Stage N6-A1（SDK integration + 80-slot RAM vectors） | ✅ board-verified（VTOR `0x28000800`，magic slots 64/65 与运行期 vector repair 均通过） |
| 4295 秒系统时间折返修复 | ✅ board-verified（`CONFIG_SYSTEM_TIME64=y`，uptime 单调增长到 5834.58 秒，无 HF/WDT 复位） |
| NuttX Stage N6-B（CPU0 SDK IRQ bridge） | ✅ TIMER1/source-3/IRQ19 board-verified（两次独立启动、三次 `bkirqtest` 全 PASS；静态 verifier 48/48 PASS） |
| GPIO foundation C0 | ✅ board-verified：P9 active-high LED + P29 active-low USERKEY，3 个独立 boot/download、5 次 `bkgpioc0` PASS |
| GPIO C1/C2 | ✅ board-verified：GPIO_NS source37/IRQ53 与 CPU0 group2 gate 已验证；`/dev/gpio0`/`/dev/gpio1` lower-half 完成，两次连续 falling-edge 命令通过；保留 `CONFIG_DEV_GPIO_NSIGNALS=2` 规避 upstream unregister 缺陷 |
| 当前门禁 | 新布局已刷板；normal update只能 sparse 写boot/CP/AP并保留B、metadata、LittleFS、`usr_config`、reserved与`0x7fa000`尾区。factory重写需重新授权；只用SDK v3.1.1.9；禁止`BLEDebug.EXE`；官方NuttX/apps/SDK源码与静态库零改动 |
| Tier-2 bootloader（OTA / A-B failover） | N15进行中；N15-M、N15-A..F和N15-V format-2 fault/campaign主机/ELF/dry-run收口完成，normal门关闭、validation独立；下一步ordered physical staging/metadata/remap/trial/rollback与受控完整断电矩阵 |
| 多核后续 | N8 AP SMP、N9 RPTUN/RPMsg与N13 BLE服务层均已板端通过；不切换 BMP、不建立 CPU2第二 peer。Wi-Fi数据面仍未进入已批准范围 |

**当前 N15 构建产物**：`$FW/bk7258-dual/app.bin`（CP）、`app1.bin`（AP）及
`bk7258-dual-image.json`。正常 sparse 更新必须以本次 manifest 的 `segments[].bkfil`
为准；2026-08-04 normal rebuild 为 `bl_crc.bin@0x0-0x11000`、
`app_crc_flash.bin@0x11000-0xb3000`、`app1_crc_flash.bin@0x165000-0x2e000`，并保留
B/`s_app`、metadata、`usr_config`、LittleFS `0x600000..0x700000`、reserved 区与
`0x7fa000..0x800000` official tail。`all-app-factory.bin` 只用于获得 fresh authority
后的破坏性迁移/恢复，不能替代 normal sparse 更新。root `$FW/all-app.bin` 继续只是
bootloader + CP 的兼容镜像，不包含 AP；builder 已验证它与 root/manifest CP 一致。
`$FW = $WORKSPACE/nuttx`，console UART1 460800 8N1。

## 产物索引

### 主报告
- **[porting-report.md](porting-report.md)** —— 评委可读的详细移植报告（背景 / 芯片事实 / 逆向 /
  Tier-1 bootloader / 板端验证 / CRC packer / NuttX 路线 / AI 协作 / Roadmap）

### Bootloader 逆向（`bootloader/`）
- [full-reverse-synthesis.md](bootloader/full-reverse-synthesis.md) —— 两家 bootloader 逆向综合结论
- [tuya-bootloader-reverse.md](bootloader/tuya-bootloader-reverse.md) —— 涂鸦 65 KB bootloader 逐函数逆向
- [bk-official-bootloader-reverse.md](bootloader/bk-official-bootloader-reverse.md) —— BK 官方 52 KB bootloader 逐函数逆向
- [vendor-bootloader-comparison.md](bootloader/vendor-bootloader-comparison.md) —— 两家 binary 对比

### 板端验证探针（`probe/`）
- [probe/README.md](probe/README.md) —— 最小裸探针说明（烧 @ `0x02010000`，读 core/CPUID/VTOR）
- [probe/probe.c](probe/probe.c) · [probe/probe.ld](probe/probe.ld)

### Tier-1 Bootloader 源码（`board/`）
- [board/bk7258_t5ai/bootloader/README.md](../../board/bk7258_t5ai/bootloader/README.md) —— Tier-1 bootloader 说明
- [start.S](../../board/bk7258_t5ai/bootloader/start.S) · [boot_main.c](../../board/bk7258_t5ai/bootloader/boot_main.c) ·
  [bootloader.ld](../../board/bk7258_t5ai/bootloader/bootloader.ld) ·
  [bk7236_pack_min_bootloader.py](../../board/bk7258_t5ai/bootloader/bk7236_pack_min_bootloader.py)

### NuttX 移植 worklog / prompts（`nuttx-port/`）
- [nuttx-port/git-worktree-sync-2026-07-27.md](nuttx-port/git-worktree-sync-2026-07-27.md) —— 主检出目录、clean worktree、构建链接与 PR 分支同步记录
- [nuttx-port/n7-ap-singlecore-bringup.md](nuttx-port/n7-ap-singlecore-bringup.md) —— Stage N7：物理 CPU1 独立单核 AP NuttX 启动链与板端 READY/heartbeat 闭环
- [nuttx-port/n7-bug-cpu0-task-exit-hardfault.md](nuttx-port/n7-bug-cpu0-task-exit-hardfault.md) —— CPU0 间歇性 task-exit HardFault：从误导性的 PSP frame、J-Link `0xaaaaaaaa` 到 NULL context restore 和 IRQ 嵌套的完整复盘
- [nuttx-port/n8-a-cpu2-probe-bringup.md](nuttx-port/n8-a-cpu2-probe-bringup.md) —— Stage N8-A：物理 CPU2 freestanding probe、UART 原始板测证据与 `board-verified` 收口
- [nuttx-port/n8-b1-smp-secondary-bootstrap.md](nuttx-port/n8-b1-smp-secondary-bootstrap.md) —— Stage N8-B1：NuttX SMP 配置、CPU2 secondary bootstrap、park 边界和构建/板测门禁
- [nuttx-port/n8-b2-bidirectional-ipi.md](nuttx-port/n8-b2-bidirectional-ipi.md) —— Stage N8-B2：SDK mailbox wrapper、IRQ79、双向 sequence/counter、CPU2 WFI 与板测门禁
- [nuttx-port/n8-c1-scheduler-online-idle.md](nuttx-port/n8-c1-scheduler-online-idle.md) —— Stage N8-C1：CPU2 `nx_idle_trampoline()`、wrapper-backed scheduler IPI 和双向 SMP-call 自动门禁
- [nuttx-port/n8-c2-cpu1-affinity-task.md](nuttx-port/n8-c2-cpu1-affinity-task.md) —— Stage N8-C2：保持默认 cpuset `0x1` 的 logical CPU1 explicit-affinity one-task gate
- [nuttx-port/n8-c3-cpu1-semaphore-remote-wake.md](nuttx-port/n8-c3-cpu1-semaphore-remote-wake.md) —— Stage N8-C3：复用同一 CPU1-bound task 的 semaphore block → CPU0 single remote-wake gate
- [nuttx-port/n8-c4-cpu1-semaphore-wake-loop.md](nuttx-port/n8-c4-cpu1-semaphore-wake-loop.md) —— Stage N8-C4：同一 CPU1-bound task / static semaphore 的固定 8 轮 exact block → remote wake gate（historical baseline，board-verified 2026-07-30）
- [nuttx-port/n8-c5-bidirectional-semaphore-pingpong.md](nuttx-port/n8-c5-bidirectional-semaphore-pingpong.md) —— Stage N8-C5：CPU0/CPU1 两 task 固定 8 轮双向 semaphore pingpong（board-verified 2026-07-30）
- [nuttx-port/n8-c6-dual-cpu1-local-scheduling.md](nuttx-port/n8-c6-dual-cpu1-local-scheduling.md) —— Stage N8-C6：两个 CPU1-bound task 的固定本地调度交接（board-verified 2026-07-30）
- [nuttx-port/n8-c7-controlled-migration.md](nuttx-port/n8-c7-controlled-migration.md) —— Stage N8-C7：单 task 固定 8 次受控 CPU0↔CPU1 migration（LATEST VERIFIED，board-verified 2026-07-30）
- [nuttx-port/n8-c8-cpu1-timed-wake.md](nuttx-port/n8-c8-cpu1-timed-wake.md) —— Stage N8-C8：CPU1-bound task 固定 8 次 timer-driven wake（board-verified 2026-07-30）
- [nuttx-port/n8-d1-smp-lifecycle-quiesce.md](nuttx-port/n8-d1-smp-lifecycle-quiesce.md) —— Stage N8-D1：CPU1 scheduler quiesce/resume foundation，不实现 hot-unplug（LATEST VERIFIED，board-verified 2026-07-30）
- [nuttx-port/cold-reset-smp-repair-guide.md](nuttx-port/cold-reset-smp-repair-guide.md) —— 面向新手的完整修复过程：boot、UART、cache/MPU、WDT、SMP 握手、打包和验证
- [nuttx-port/n8-cold-reset-resolution-report.md](nuttx-port/n8-cold-reset-resolution-report.md) —— physical cold-reset/AP SMP 的完整证据：逐轮定位、正式镜像和 warm/reset 3/3
- [nuttx-port/bk7258-build-flash-debug-sop.md](nuttx-port/bk7258-build-flash-debug-sop.md) —— WSL2 构建、Windows COM7 下载、COM11 raw capture、手动/J-Link RESET、NSH 命令和日志判读 SOP
- [../../tools/windows-hardware-debug/README.md](../../tools/windows-hardware-debug/README.md) —— 不含芯片私有参数和烧录动作的通用 Windows/WSL2 UART/J-Link 证据采集工具，可作为 Claude/Codex Agent Skill 使用
- [nuttx-port/n8-cold-reset-nsh-hang-investigation.md](nuttx-port/n8-cold-reset-nsh-hang-investigation.md) —— 详细原始调查 worklog 和逐轮证据
- [nuttx-port/n8-cold-reset-automation.md](nuttx-port/n8-cold-reset-automation.md) —— 自动化脚本实现说明与设备映射
- [nuttx-port/n8-cold-reset-diagnostic-checkpoints.md](nuttx-port/n8-cold-reset-diagnostic-checkpoints.md) —— raw UART 诊断路标完整代码、判读表以及后续清理步骤
- [nuttx-port/cp-ap-rptun-architecture-research.md](nuttx-port/cp-ap-rptun-architecture-research.md) —— CP NuttX UP + AP NuttX SMP 双镜像、官方 SDK wrapper、RPTUN/RPMsg、Wi-Fi/BLE 与 mailbox 复用边界
- [nuttx-port/prompts/09-n9-rptun-rpmsg.md](nuttx-port/prompts/09-n9-rptun-rpmsg.md) —— Stage N9 完成记录：32 KiB shared-memory、CPU0 gateway、Name Service、reconnect、syslog 和板端验收
- [nuttx-port/n9-rptun-source-verification.md](nuttx-port/n9-rptun-source-verification.md) —— N9 RPTUN/OpenAMP callback、role、resource、worker、ABI 与 wrapper 边界源码复核
- [nuttx-port/n9-plan-review-2026-07-31.md](nuttx-port/n9-plan-review-2026-07-31.md) —— N9 外部评审 17 项逐条源码复核、接受/反驳结论和计划修订依据
- [nuttx-port/prompts/14-n14-psram.md](nuttx-port/prompts/14-n14-psram.md) —— Stage N14：16 MiB PSRAM、CP/AP role-local heap、AP SMP allocator、SDK timer及完整板端收口
- [nuttx-port/n14-psram-source-verification.md](nuttx-port/n14-psram-source-verification.md) —— N14 official SDK owner/PM/layout/allocator/MPU/clock与board wrapper源码复核
- [nuttx-port/n14-evidence-index.md](nuttx-port/n14-evidence-index.md) —— N14 build、artifact hash、warm/cold/factory及回归原始证据索引
- [nuttx-port/prompts/15-n15-tier2-ota.md](nuttx-port/prompts/15-n15-tier2-ota.md) —— N15 paired OTA stage、门禁、候选布局和当前handoff
- [nuttx-port/n15-ota-source-verification.md](nuttx-port/n15-ota-source-verification.md) —— exact v3.1.1.9 RBL/AB/Ghidra/remap/layout源码复核
- [../../progress/verification/2026-08-03-n15-a-host-pair-bundle.md](../../progress/verification/2026-08-03-n15-a-host-pair-bundle.md) —— N15-A deterministic pair bundle、官方golden vector、真实clean build与负例证据
- [../../progress/verification/2026-08-04-n15-b-host-staging.md](../../progress/verification/2026-08-04-n15-b-host-staging.md) —— N15-B CP-only staging、2/21故障注入、final ELF与完整构建证据
- [../../progress/verification/2026-08-04-n15-c-host-boot-selection.md](../../progress/verification/2026-08-04-n15-c-host-boot-selection.md) —— N15-C metadata ABI、A/B selector 5/28、exact remap source/binary与final boot ELF证据
- [../../progress/verification/2026-08-04-n15-d-host-trial.md](../../progress/verification/2026-08-04-n15-d-host-trial.md) —— N15-D one-trial、confirm/rollback、4/113与48 reset boundaries
- [../../progress/verification/2026-08-04-n15-e-host-publication.md](../../progress/verification/2026-08-04-n15-e-host-publication.md) —— N15-E publication/reclamation、5/142与mutation边界
- [../../progress/verification/2026-08-04-n15-f-host-validation.md](../../progress/verification/2026-08-04-n15-f-host-validation.md) —— N15-F health、validation profile与volatile PSRAM transport
- [../../progress/verification/2026-08-04-n15-format2-symmetric-host.md](../../progress/verification/2026-08-04-n15-format2-symmetric-host.md) —— format-2双bank A→B→A、16份独立包与独立verifier
- [nuttx-port/n6-bug-4295s-timer-wrap.md](nuttx-port/n6-bug-4295s-timer-wrap.md) —— 约 4295 秒后 `HF` + WDT 重启根因及修复（`CONFIG_SYSTEM_TIME64=y`；源码、ELF 与 5834.58 秒板测均已验证）
- [nuttx-port/n5-flash-filesystem.md](nuttx-port/n5-flash-filesystem.md) —— Stage N5 flash filesystem worklog（D5 raw flash r/w + D6 MTD + D7 LittleFS，board-verified 2026-07-19）
- [nuttx-port/n2-nsh-console.md](nuttx-port/n2-nsh-console.md) —— Stage N2 会话记录（boot trace、
  4 个 UART RX bug 现象/定位/修法、板端 `uname -a` 证据）
- [nuttx-port/n3-procfs-ps.md](nuttx-port/n3-procfs-ps.md) —— Stage N3 会话记录（procfs 挂载、
  `ps` / `/proc` 与 state-C 板端证据）
- [nuttx-port/n4-d0-clock-diag.md](nuttx-port/n4-d0-clock-diag.md) —— Stage N4-D0/D0D/D0F 会话记录
  （manual-reset 26 MHz baseline、loader 残留 ≈80 MHz、J-Link DWT、runtime SysTick bookkeeping、
  100Hz tick 兼容性、N4-D1 blocker）
- [nuttx-port/n5-flash-filesystem.md](nuttx-port/n5-flash-filesystem.md) —— Stage N5 flash filesystem
  （D0 layout、D1 flash ID、D2 content dump、D3 magic scan、D4 emptiness scan、D5 raw flash r/w、
  D6 MTD lower-half、D7 LittleFS；全链路 board-verified 2026-07-19）
  - **Current CP/AP Stage handoff：** [nuttx-port/prompts/15-n15-tier2-ota.md](nuttx-port/prompts/15-n15-tier2-ota.md)（N15-M `board-verified`，N15-A `host-verified`，N15-B/C/D/E/F `host/source/ELF-verified`，N15-V fault/campaign `host/source/ELF/dry-run-verified`，physical board gate next）；N14功能基线见 [nuttx-port/prompts/14-n14-psram.md](nuttx-port/prompts/14-n14-psram.md)

### 参考
- [git-worktree-guide.md](git-worktree-guide.md) —— Git worktree 入门、本项目 clean worktree 与 openvela 构建工作区的关系
- [sdk-context-index.md](sdk-context-index.md) —— BK ARMINO SDK (`bk_avdk_smp`) 上下文索引

## 外部资源（不在本仓内）

| 资源 | 路径 |
|---|---|
| Beken ARMINO SDK | `$BK7258_SDK`（= `bk_avdk_smp`） |
| Tuya SDK | `$TUYA_SDK`（= `TuyaOpen`） |
| 已有 Zephyr port（含已验证最小 bootloader） | `$TUYA_SDK/zephyr-bk7258-port` |
| 涂鸦 bootloader（65 KB） | `$TUYA_SDK/zephyr-bk7258-port/tools/t5ai_bootloader.bin` |
| BK 官方 bootloader（52 KB） | `$BK7258_SDK/cp/components/bk_libs/bk7258/bootloader/normal_bootloader/bootloader.bin` |
