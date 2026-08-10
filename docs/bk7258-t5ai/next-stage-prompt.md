# BK7258 T5-AI 主 Stage 恢复提示词索引

> **2026-08-10 勘误：**N15/N17 自定义 OTA runtime 和专用脚本已退役并
> 删除。N15 条目只代表历史验证；当前启动架构为完整 BL1 + pinned NuttX
> MCUboot BL2 + signed same-slot CP/AP。恢复工作必须先读
> [`progress/CURRENT.md`](../../progress/CURRENT.md)。

> 用法：`/clear` 后先打开下表标为 **LATEST VERIFIED** 的 Stage 文件恢复最新证据；只有在用户明确选择下一 MAIN Stage 后，才新增 **CURRENT** 项。历史 Stage 提示词只作为证据与上下文，不构成恢复过时方案或越过当前门禁的授权。

## 主 Stage 顺序

| MAIN Stage | 范围 | 状态 | 记录 / 提示词 |
|---|---|---|---|
| N1 | minimal NuttX boot | `board-verified`，commit `40495ca` | [porting report](porting-report.md) |
| N2 | interactive NSH | `board-verified`，code `9f45bc6` + docs `e3ad3e9` | [N2 worklog](nuttx-port/n2-nsh-console.md) |
| N3 | procfs + `ps` | `board-verified`，code `4d9198e` + docs `68badfe` | [N3 worklog](nuttx-port/n3-procfs-ps.md) |
| **N4** | DPLL / 480 MHz clock bring-up | 历史：N4-D0/D0D/D0F substage `board-verified`（D0/D0D `6f596b7` + D0F `8dab594`，2026-07-18）；N4-D1 blocked；DPLL enable / mux 切换 not attempted；整 N4 not board-verified | [N4 recovery prompt](nuttx-port/prompts/04-n4-clock-bringup.md) / [N4-D0 worklog](nuttx-port/n4-d0-clock-diag.md) |
| **N5** | Flash layout / ID / filesystem | N5-D0..D4 board-observed（2026-07-19）；**N5-D5 raw flash r/w board-verified**（2026-07-19）；**N5-D6 MTD board-verified**（方案 A，CONFIG_BK7258_FLASH_MTD）；**N5-D7 LittleFS filesystem board-verified**（/data 挂载，probe 文件重启持久化通过）；D7 版 `all-app.bin` = 192270 B = `0x2EF0E`（< `0x100000`）；全链路 raw flash → MTD → ftl block device → LittleFS board-verified | [N5 worklog](nuttx-port/n5-flash-filesystem.md) |
| **N6** | Beken SDK integration / WDT / IRQ adaptation | 历史：WDT/UART RX/NSH/LittleFS `board-verified`；A0 RAM-vector plumbing **`board-verified`**（2026-07-22）；A1 Task 1-4 complete；**A1 `board-verified`**（2026-07-22）：board-flashed `all-app.bin` = 236028 B (`0x399fc`), SHA-256 `a92352...b5a5`；VTOR=`0x28000800`，flash magic slots 64/65 = `32374B42 00003633`，RAM slots 15/31/64/65 = `0x020108FD`，UART/NSH/WDT/LittleFS/DVFS tier-5 baseline all PASS；SDK bundle local/ignored + checksum-pinned（374 entries, 31 linked libs, fail-closed manifest, deterministic ordering）；precommit build/static verification 23/23 PASS；A1 code/docs 已提交并推送至 `fork/bk7258-n6-ramvectors`（`66b29d1` + `57558eb`）；**Stage B CPU0 SDK IRQ bridge 已获用户授权并开始**；archive/object ownership 已确认：当前 `libdriver.a(interrupt_base.c.obj)` 被 `timer_driver.c.obj` 拉入并提供 `bk_int_isr_register`，SDK source `0..63` 与 NVIC line 一一映射，callback=`void (*)(void)` 且忽略 arg；mapped default priority 为 source 27 (`INT_SRC_LCD`) 特例 0→`0x00`、其余 source 为 6→`0xc0`；source/ELF RED verifier 已加入并对当前 A1 ELF 得到预期 `RED_EXIT=1`；dedicated `bk7258_sdk_irq.c/.h`、pending-clear helper、Kconfig/Make/CMake/defconfig gate 已实现（source `0..63`→NuttX IRQ `16..79`，initial version 曾错误把所有 mapped default 都设为 6→`0xc0`；register/replace/unregister/init/deinit 均由 overlay 接管），fresh distclean/build 已成功且生成 `.config` 确认 `CONFIG_BK7258_SDK_IRQ_BRIDGE=y`、`CONFIG_ARCH_IRQPRIO=y`；fresh post-link verifier 得到 25 PASS / 10 FAIL（`/tmp/bk7258-stageb-verify-fresh.log`）：overlay `libarch.a` 已含五个 bridge 定义且 register/unregister lifecycle disassembly 全 PASS，但最终 owner 仍是 `libdriver.a(interrupt_base.c.obj)`，`arch_interrupt_*`/`icu_int_map_table` 仍在 ELF；根因是 archive/link-order，当前仍为 post-link RED；已在 team linker script 中按 `CONFIG_BK7258_SDK_IRQ_BRIDGE` 条件加入 `EXTERN(bk_int_isr_register)`，使首次 `libarch.a` 扫描必须抽取 bridge object，并新增 verifier `S10` gate；该修复 fresh distclean/build 已成功（`nuttx.bin`=156052 B，`all-app.bin`=235450 B，较 pre-fix 分别减少 552/578 B），post-link verifier 已 GREEN（36 PASS / 0 FAIL，`/tmp/bk7258-stageb-linkfix-green.log`）：五个 lifecycle symbols 均由 `libarch.a(bk7258_sdk_irq.o)` 提供，SDK `interrupt_base.c.obj` 未抽取，direct `arch_interrupt_*`/`icu_int_map_table` 全部从 ELF 消失；Stage B 当前 `build-verified`、尚未 board-test；A1 preserved-invariant check 已 18/18 PASS（`/tmp/bk7258-stageb-a1-invariants.log`）：80-slot vectors、RAM vectors、magic、repair calls、partition/concat、0 team warnings 均保持；fresh `all-app.bin`=235450 B (`0x397ba`)，SHA-256 `45d7b986...b6db9725`（compile/static-only）；focused review 新发现 F1 Important：authoritative `ICU_DEV_MAP` 的 source 27 (`INT_SRC_LCD`) mapped default 为 0，而当前 bridge 对全部 source 硬编码 6；现有 artifact 不得 board-test，Stage B 回到 `blocked`；LCD priority-0 exception 与 verifier S11-S14/E07-E08 已实现，source gates 全 PASS，stale F1 object 被 E08 单点拒绝（41 PASS / 1 FAIL，`/tmp/bk7258-stageb-f1-stale-red.log`）；F1-corrected fresh distclean/build 已成功（`nuttx.bin`=156068 B，`all-app.bin`=235484 B，log=`/tmp/bk7258-stageb-f1-build.log`），但 fresh hardened verifier 仍为 41 PASS / 1 FAIL，仅 E08 失败（`/tmp/bk7258-stageb-f1-green.log`）；fresh object 已确认生产语义正确：`cmp #27` + conditional `#6/#0` + `lsl #5` 后调用 `up_prioritize_irq`；E08 误要求 literal `#192/0xc0`，属于 verifier false negative。E08 compiled-semantic predicate 已完成成对复验：temporary archive 注入 exact historical uniform-priority object 后仅 E08 失败（41/42，`/tmp/bk7258-stageb-e08-stale-red-v2.log`），fresh corrected archive 42/42 PASS（`/tmp/bk7258-stageb-f1-green-v2.log`）；preserved A1 suite 18/18 PASS（`/tmp/bk7258-stageb-f1-a1-invariants.log`），F1-corrected `all-app.bin`=235484 B (`0x397dc`), SHA-256 `5ff2ce00...4cf89ce`；final focused review 新发现 F2 Important：register/unregister/set-priority/deinit 的多步 callback-table + NuttX IRQ lifecycle 未由 bridge-local critical section 序列化，存在 same-source concurrent replacement lost-update；当前 object 无 PRIMASK save/disable/restore。Stage B 回到 `blocked`；F2 source/object RED gates 已安装但未运行：S15 要求 bridge-local lock/helper，E09 分别检查 register/unregister/set-priority/deinit 的 IRQ save/disable/restore；F2 RED 已确认：verifier 42 PASS / 5 FAIL，仅 S15 + register/unregister/set-priority/deinit 四个 E09 失败（`/tmp/bk7258-stageb-f2-lock-red.log`）；F2 production fix 已实现未构建：bridge-local `spinlock_t`，register/unregister/set-priority/deinit 完整序列化，internal locked-unregister helper，dispatch 保持 lock-free/nonblocking；F2 source-pass/stale-object RED 已确认：S15 PASS、四个 E09 FAIL，RESULT 43 PASS / 4 FAIL（`/tmp/bk7258-stageb-f2-stale-object-red.log`）；F2 fresh distclean/build 已成功（`nuttx.bin`=156108 B，`all-app.bin`=235518 B，`/tmp/bk7258-stageb-f2-build.log`）；F2 fresh verifier 为 37 PASS / 10 FAIL（`/tmp/bk7258-stageb-f2-green.log`）：S/ownership 等仍 GREEN；helper refactor 使 public register/unregister 不再直接含 teardown calls，E05/E06 失配，E08/E09 也未识别。fresh object 已证实 production F2/F1 语义正确：internal helper 含 disable/clear/callback-null/barrier/detach，register/unregister relocation 指向 helper；四个 lifecycle 均含 BASEPRI save→mask→restore；E08 为 fused `mov r1,reg,lsl #5`。10 FAIL 均属 verifier shape/call-graph stale。F2 verifier 已适配未运行：E05/E06 helper-aware，E09 识别 BASEPRI，E08 识别 fused shift，新 E10 验证 register/unregister/deinit→locked helper 及 helper teardown callees；总计 48 gates。F2 exact historical/fresh RED→GREEN 已完成：historical unlocked F1 object 42 PASS / 6 FAIL（E09x4+E10+E08，`/tmp/bk7258-stageb-f2-historical-stale-red.log`），fresh F2 object 48/48 PASS（`/tmp/bk7258-stageb-f2-green-v2.log`）；preserved A1 18/18 PASS（`/tmp/bk7258-stageb-f2-a1-invariants.log`）；F2 `all-app.bin`=235518 B (`0x397fe`), SHA-256 `6de61d94...ba7c0b`，bridge 48/48 + A1 18/18 均 GREEN，Stage B F1+F2 为 `build-verified`。final focused bridge review 已完成，无额外 blocker；F1/F2 resolved，Stage B 仍 `build-verified`/not board-verified。timer-test 已实现但未构建：实际 SDK `CONFIG_TIMER_SUPPORT_ID_BITS=7` 排除 TIMER4/5，采用 TIMER_ID1 + `INT_SRC_TIMER`/IRQ19；bridge test-gated snapshot、六路 timer idle fail-closed gate、chip-local runner 与手动 NSH built-in `bkirqtest` 已落地，流程为 callback A→unregister 后硬件 status 存在但 counter 静默→callback B→所有出口恢复原 handler；source review/precheck 已 PASS；timer-test fresh distclean/build exit 0（logs=`/tmp/bk7258-stageb-timer-test-{distclean,build}.log`），defconfig 因 test gate select 自动移除冗余显式 BUILTIN；first post-build gate RED：generated config/bridge 48/48 均 PASS，但 `bkirqtest_main`、runner、snapshot、callbacks 与 builtin registry 全部不在 final ELF/map；timer-test blocked，禁止 board-test。root cause 已定：chip test object/runner/snapshot 均在 libarch；app 缺失因 root `packages/Make.defs` 只 include `packages/*/Make.defs`，而 manifest 链接在 `packages/demos/...` 且 category `packages/demos/Make.defs` 不存在。team-owned demos Make/CMake/Kconfig aggregators + manifest linkfiles 已添加未验证；`repo sync -l` 因 team repo `.git` unsupported checkout state 失败；manifest fix 保留，当前 workspace 三条 exact manifest-owned symlink 已创建并指向 team files；下一步 fresh distclean/build + post-link verify；agent 仍禁止 flash | [N6 worklog](nuttx-port/n6-sdk-integration-worklog.md) |
| **N7** | physical CPU1 independent single-core AP NuttX | `board-verified`（2026-07-28）；CPU0 task-exit HardFault 最小修复于 2026-07-29 `board-verified` | [N7 worklog](nuttx-port/n7-ap-singlecore-bringup.md) / [HardFault worklog](nuttx-port/n7-bug-cpu0-task-exit-hardfault.md) |
| **N8-A** | physical CPU2 freestanding probe on AP logical CPU1 | `board-verified`（2026-07-29，code `7ffd05b`）；独立 PR 已合并，作为 `configs/ap_up/` 回退基线 | [N8-A worklog](nuttx-port/n8-a-cpu2-probe-bringup.md) |
| **N8-B1** | NuttX-aware CPU2 secondary bootstrap, parked before scheduler | `board-verified`（2026-07-29）；CPU2 `SECONDARY_READY`、合法 vector/VTOR/MSP/IDLE stack、`online=0x1`、`calls=0`、restart/stop/start/cycle 与 CPU0 回归通过 | [N8-B1 worklog](nuttx-port/n8-b1-smp-secondary-bootstrap.md) |
| **N8-B2** | AP logical CPU0 ↔ logical CPU1 bidirectional IPI | `board-verified`（2026-07-29）；沿用 SDK mailbox/cross-core wrapper，CPU2 first IRQ NOCP 已由 CPACR/FPCCR 初始化修复，双向 1/100 次、restart、stop/start、三轮 cycle 与最终恢复均通过；CPU2 仍未 scheduler online | [N8-B2 worklog](nuttx-port/n8-b2-bidirectional-ipi.md) |
| **N8-C1** | CPU2 scheduler-online IDLE + wrapper-backed SMP call | `board-verified`（2026-07-30）；scheduler-online、双向 SMP-call、stop/ipitest fail-closed、SMP-safe STAR IRQ context restore 和 AP 周期 sleep 全部闭环；普通 task 仍限制在 CPU0 | [N8-C1 worklog](nuttx-port/n8-c1-scheduler-online-idle.md) |
| **N8-C2** | explicit logical CPU1 affinity one-task gate | `board-verified`（2026-07-30，历史 baseline）；默认 cpuset 保持 `0x1`；唯一 diagnostic pthread requested/observed=`0x2/0x2`、cpu=1、started/completed/pid-released=`1/1/1`；remote SMP tx/rx、CPU1 IRQ/wake、calls 均精确 +1，failure=0 | [N8-C2 worklog](nuttx-port/n8-c2-cpu1-affinity-task.md) |
| **N8-C3** | CPU1-bound single-task block → CPU0 semaphore remote wake | `board-verified` historical baseline（2026-07-30）；同一 `task id=3` 完成 CPU1 dispatch、semaphore wait、CPU0 single post、CPU1 remote wake 与 PID release；BAFF aggregate `+2` / BSEM isolated `+1` 精确符合设计，不是第二个 task | [N8-C3 worklog](nuttx-port/n8-c3-cpu1-semaphore-remote-wake.md) |
| **N8-C4** | same CPU1-bound task fixed 8-cycle semaphore remote wake | `board-verified` historical baseline（2026-07-30）；唯一 task/static semaphore 完成 8 轮 exact waiter + CPU0 single post；BSEM `+1`、BSWL `+8`、BAFF `+9`，failure/coalesced/stale/spurious=0 | [N8-C4 worklog](nuttx-port/n8-c4-cpu1-semaphore-wake-loop.md) |
| **N8-C5** | bidirectional semaphore pingpong (two tasks, two sems, 8 rounds) | `board-verified`（2026-07-30）；explicit `SCHED_FIFO` / controller+1 priority 后两 task 完成 8/8，CPU0→CPU1 `+9`、CPU1→CPU0 `+8`、calls `+17` 精确命中，PID released，coalesced/fail/stale/spurious=0 | [N8-C5 worklog](nuttx-port/n8-c5-bidirectional-semaphore-pingpong.md) |
| **N8-C6** | dual CPU1 local scheduling (two CPU1 tasks, 8+8 rounds) | `board-verified`（2026-07-30）；local-yield correction 后两个 CPU1 task 完成 sequence `8/8`，CPU0→CPU1 `+3`、反向 `+0`、calls `+3` 精确命中 | [N8-C6 worklog](nuttx-port/n8-c6-dual-cpu1-local-scheduling.md) |
| **N8-C7** | controlled migration (8 affinity transitions) | **LATEST VERIFIED：`board-verified`（2026-07-30）**；BMIG `PASSED/error=0`、requested/completed=`8/8`，final CPU0、sequence=`8`、callback=`8/8`、PID released；CPU0→CPU1 `+4`、CPU1→CPU0 `+4`、calls `+8` 精确命中，handler fully delivered，coalesced/fail/stale/spurious=0 | [N8-C7 worklog](nuttx-port/n8-c7-controlled-migration.md) |
| **N8-C8** | CPU1 timed wake (8 sleep cycles) | `board-verified`（2026-07-30）；corrected image generation2 retry 中 BTIM PASSED `8/8`，task CPU1 sequence8/value8/0/aux20000/1，exact initial-dispatch + eight-wake attribution `+9/+0/+9`，handler fully delivered，failure/coalesced/stale/spurious=0 | [N8-C8 worklog](nuttx-port/n8-c8-cpu1-timed-wake.md) |
| **N8-D1** | CPU1 scheduler quiesce/resume foundation | **LATEST VERIFIED：`board-verified`（2026-07-30）**；normal autostart BLCY PASSED `1/1`，entry/exit CPU1、sequence/aux `1/1`、value `0/-138` (`-ENOTSUP`)，exact `+1/+0/+1`，online=`0x3`，handler fully delivered，failure/coalesced/stale/spurious=0；not hot-unplug | [N8-D1 worklog](nuttx-port/n8-d1-smp-lifecycle-quiesce.md) |
| **N9** | CP NuttX UP ↔ AP NuttX SMP RPTUN/OpenAMP/RPMsg | **LATEST VERIFIED：`board-verified`（2026-07-31）**；官方 SDK/NuttX 只读 wrapper；32 KiB carveout、single peer、CPU0 gateway、Name Service、双 producer、generation reconnect、syslog 与 legacy/latest/baseline build 全部闭环 | [N9 completion](nuttx-port/prompts/09-n9-rptun-rpmsg.md) / [source verification](nuttx-port/n9-rptun-source-verification.md) |
| **N10** | AP SMP liveness / RPMsg health supervision | **LATEST VERIFIED：`board-verified`（检测与人工恢复基线，2026-08-01）**；三路健康信号、双向 vring activity、三类独立注入与旧链路 fail-closed 全部实板通过；manual recovery 连续推进到 generation=5，恢复后两轮无注入 full suite 12/12 PASS；自动恢复默认关闭，仍是独立可选门禁 | [N10 worklog](nuttx-port/prompts/10-n10-ap-supervision.md) |
| **N11** | AP access to CP LittleFS through RPMsgFS | **LATEST VERIFIED：`board-verified`（受限 stock RPMsgFS worker 基线，2026-08-02）**；四档文件测试各 20/20、RPMsg 6×100、syslog、LittleFS local probe、故障态 bounded fail-closed 与 generation 1→2 manual recovery 全部通过；在途 stock POSIX 调用仍由 AP restart 回收 | [N11 worklog](nuttx-port/prompts/11-n11-rpmsgfs.md) |
| **N12** | official Beken Bluetooth IPC + AP NuttX HCI wrapper | **LATEST VERIFIED：`board-verified`（2026-08-02）**；CP official controller-only BLE、AP stock NuttX Host、HCI info、SDK-equivalent MAC 持久化、UART lifecycle self-heal、RPMsg/RPMsgFS/SMP 共存以及 Windows legacy advertiser 的真实 RF report 均实板通过 | [N12 worklog](nuttx-port/n12-beken-bt-ipc-wrapper.md) |
| **N13** | BLE GAP/GATT Peripheral end-to-end | `board-verified`（2026-08-03）；四类negative、20/20 uncached重连、BLE 100帧与RPMsg六场景×100/RPMsgFS四档×20主动并发、3/3 cold、最终`25/25/25`与connection ref=0全部闭环 | [N13 completion](nuttx-port/prompts/13-n13-ble-gap-gatt.md) / [source verification](nuttx-port/n13-ble-gap-gatt-source-verification.md) / [evidence](nuttx-port/n13-evidence-index.md) |
| **N14** | 16 MiB PSRAM + SDK software-timer wrapper | `board-verified`（2026-08-03）；CP official PM owner、全容量boot gate、CP/AP private heap、AP CPU0/CPU1 allocator 16/16、timer 256、warm cycle10、physical cold/factory及RPMsg/Bluetooth回归全部闭环 | [N14 completion](nuttx-port/prompts/14-n14-psram.md) / [source verification](nuttx-port/n14-psram-source-verification.md) / [evidence](nuttx-port/n14-evidence-index.md) |
| **N15** | Tier-2 paired CP/AP OTA + rollback | **HISTORICAL / implementation retired**；批准范围曾完成 generation 314/315 双向实板验证，现役自定义 runtime/脚本已删除 | [N15 worklog](nuttx-port/prompts/15-n15-tier2-ota.md) / [physical evidence](../../progress/verification/2026-08-04-n15-physical-symmetric-lifecycle.md) / [ADR-004](../../memory/decisions/ADR-004-n15-official-contiguous-ab-layout.md) |
| **N16** | official Wi-Fi controller + native NuttX STA data plane | **COMPLETE：`board-verified`（2026-08-06）**；runtime STA、wrong-password recovery、native TCP/UDP、active-Wi-Fi RPMsg/RPMsgFS/Bluetooth coexistence、AP restart `-EBUSY` 与 RTS 3/3 全部闭环 | [N16 worklog](nuttx-port/prompts/16-n16-wifi-data-plane.md) / [board evidence](../../progress/verification/2026-08-06-n16-wifi-sta-coexistence.md) / [malloc evidence](../../progress/verification/2026-08-05-n16-wifi-malloc-compatibility.md) / [ADR-007](../../memory/decisions/ADR-007-n16-cp-radio-ap-nuttx-network.md) |

## 当前 handoff

> **N14 completed / board-verified（2026-08-03）：**T5-AI实板PSRAM为APS128XXO，
> `id=0x8d08/config=0x8d1a/capacity=16777216`。CP在official PHY/RF首次校准leaf之后、AP
> release之前调用official `bk_pm_module_vote_psram_ctrl(AS_MEM=10, ON=0)`并执行一次全容量
> destructive boot gate；AP不初始化PSRAM硬件。首版保留official低8 MiB ABI：CP heap 128 KiB、
> AP heap 640 KiB、AP section 256 KiB保留；normal profile的upper 8 MiB只boot-tested/reserved，
> N15-F validation profile另有固定volatile transfer窗口但不开放allocator。heap control和
> allocator outer spinlock均在内部SRAM，realloc为bounded allocate-copy-free；AP CPU0/CPU1各
> 16轮全部完成、free稳定。SDK timer callback由`bk-sdk-timer` task执行，queued self-delete
> final-free已在256轮门禁中通过。AP cycle10、RPMsg六场景×100、Bluetooth info、physical
> RESET 3/3、final clean cold、factory首次校准及post-calibration cold全部PASS。official
> NuttX/apps/SDK source和SDK static libraries零改动。N14功能已在N15-M新布局上重新验证。详见
> [N14 completion](nuttx-port/prompts/14-n14-psram.md)、
> [source verification](nuttx-port/n14-psram-source-verification.md)和
> [evidence index](nuttx-port/n14-evidence-index.md)。
>
> **N15 completed（2026-08-04）：**owner接受ADR-004并授权一次性迁移。official v3.1.1.9
> contiguous primary CP/AP + `s_app` 已由team linker/boot/MTD/packer/debug/verifier落地；AP XIP
> 为`0x02150000`，LittleFS为raw `0x600000..0x700000`。迁移后的保留功能与host/source/ELF
> 门禁均PASS。实板generation 314已从A经bank 0 trial/confirm B，generation 315再从B经bank 1
> trial/confirm A；两次2576384-byte inactive write均通过完整read-back/SHA，两槽AP SMP、RPTUN、
> LittleFS/PSRAM、timer、RPMsg/RPMsgFS和Bluetooth回归通过，confirmed-A RTS恢复保持generation
> 315且runtime gates为0。post-confirm同时移除USB/J-Link供电并重连后，capture-only状态仍为
> generation 315 confirmed A，AP/CPU2/RPTUN健康。physical rollback没有在本轮confirm路径中执行。见
> [N15 worklog](nuttx-port/prompts/15-n15-tier2-ota.md)、
> [N15 physical evidence](../../progress/verification/2026-08-04-n15-physical-symmetric-lifecycle.md)、
> [N15-V host evidence](../../progress/verification/2026-08-04-n15-v-host-fault-injection.md)、
> [N15-F evidence](../../progress/verification/2026-08-04-n15-f-host-validation.md)、
> [N15-E evidence](../../progress/verification/2026-08-04-n15-e-host-publication.md)、
> [N15-D evidence](../../progress/verification/2026-08-04-n15-d-host-trial.md)、
> [N15-C evidence](../../progress/verification/2026-08-04-n15-c-host-boot-selection.md)、
> [N15-B evidence](../../progress/verification/2026-08-04-n15-b-host-staging.md)、
> [N15-A evidence](../../progress/verification/2026-08-03-n15-a-host-pair-bundle.md)和
> [N15-M evidence](../../progress/verification/2026-08-03-n15-migration-board-verification.md)。
>
> **N16 completed（2026-08-06）：**owner接受ADR-007。official v3.1.1.9 CP继续拥有
> RF/PHY/MAC/WPA、Wi-Fi vnet controller和DHCP；AP logical CPU0保留official proxy，通过
> team-owned lease/pbuf/netdev adapter接入native NuttX `wlan0`和socket。vendor AP lwIP与SDK
> FreeRTOS未进入最终ELF。runtime STA、wrong-password bounded recovery、gateway ICMP、local
> TCP/UDP、active-Wi-Fi RPMsg/RPMsgFS/Bluetooth、AP restart fail-closed及RTS 3/3均实板通过。
> live VFS object重复分配由profile-scoped 16 KiB NuttX FS heap隔离；official源码未修改。见
> [N16 worklog](nuttx-port/prompts/16-n16-wifi-data-plane.md)、
> [board evidence](../../progress/verification/2026-08-06-n16-wifi-sta-coexistence.md)和
> [ADR-007](../../memory/decisions/ADR-007-n16-cp-radio-ap-nuttx-network.md)。
>
> **N13 completed / board-verified（2026-08-03）：**AP stock NuttX Host仍是唯一Host owner，
> CP运行official v3.1.1.9 Controller；官方NuttX/SDK源码和静态库均未修改。最终根因是stock
> inbound ACL connection reference未释放，旧镜像`ref=19 == HOST conn_rx=19`；board link wrapper
> 现精确配对release，source verifier监测upstream ownership变化并防double release。四类negative
> 全部被真实ATT拒绝，post-reject合法echo仍可用；20轮uncached重连20/20。BLE 100帧分别与
> RPMsg六场景×100、RPMsgFS四档×20主动并发PASS，最终Host/HCI/N13=`25/25/25`、ref=0、
> AP READY、RPTUN CONNECTED、supervisor HEALTHY、CPU2 online且pending 0/0。RPMsg满载下BLE
> 会话45.41秒，在显式90秒deadline内完成，作为性能基线保留。3/3 physical cold、latest/legacy
> rollback、final build/flash/verifier与官方树zero-diff均PASS。下一MAIN Stage尚未批准；用户明确
> 禁止启动会使Windows卡顿的`BLEDebug.EXE`。详见
> [N13 completion](nuttx-port/prompts/13-n13-ble-gap-gatt.md)、
> [source verification](nuttx-port/n13-ble-gap-gatt-source-verification.md)和
> [evidence index](nuttx-port/n13-evidence-index.md)。
>
> **N12 completed / board-verified（2026-08-02）：**保持 SDK/NuttX 源码只读，在 board overlay
> 中接入官方 `MB_CHNL_BT_CMD`：CP 运行 controller-only BLE，AP 通过 `bt_driver_s`
> lower-half 运行 stock NuttX Host。空白板按官方策略生成并双份持久化 base MAC
> `c8:47:8c:47:47:47`，重启后的 BD_ADDR 稳定为 `c8:47:8c:47:47:48`、
> `fallback=0`；sys_rf record 的 magic/CRC 与 sys_net 均经 BKFIL 回读确认。PHY/RF/
> Controller 对 UART1 的异步 reset 已由 wrapper 的最终使用点校验、RX callback 恢复和
> stale FIFO byte drain 修复，冷启动第一条 `bkbttest info` 无乱码直接 PASS。完整
> RPMsg 6×100、RPMsgFS 四档×2，以及最终 patch 后短回归均 PASS，AP heap 稳定。
> 此外两轮独立 COM7 RTS physical reset 均为 `cold_path=yes`/`PASS_NSH`，且每轮未经
> 预热的首条 `bkbttest info` 均无垃圾前缀并 PASS。
> N12-V 使用仓内 Windows/WSL2 advertiser 发出 company ID `0xfffe`、payload
> `4e31325601020304` 的 legacy 广播；`bkbttest n12v 10000 15000` 得到
> `results=2 selected=1 n12v_match=1`，选中 address `01:43:94:1f:ea:e4`、RSSI
> `-49`，完整 AD `0b ff fe ff 4e 31 32 56 01 02 03 04`，`BBTT SUITE PASS
> info=1 scan=1`。精确 filter 已排除同一 Windows 适配器的系统广播，真实 RF gate
> 确定性闭环；随后 RPMsg 6/6 与 RPMsgFS 4/4 回归也 PASS。
> AP-only warm restart 仍禁止，直到 pointer quiesce 协议单独设计并验证。完整证据见
> [N12 worklog](nuttx-port/n12-beken-bt-ipc-wrapper.md)。
>
> **N11 completed（2026-08-02）：**CP 继续独占 flash/MTD/LittleFS `/data` 并使用
> stock RPMsgFS server；AP stock client 挂载 `/cpdata`，文件调用隔离在 logical CPU0
> 专用 worker。BK7258 静态独占对象已按官方 SDK 约束集中到 `0x28000000` 专用 64 KiB，
> CP RAM 相应从 `0x28010000` 开始，NuttX/SDK 均未修改。实板已完成四档 RPMsgFS
> 20/20、RPMsg 6×100、syslog、本地 LittleFS、RPMsg fault bounded fail-closed 和
> generation 1→2 manual recovery；最终为 `READY/CONNECTED/HEALTHY`、CPU2 online、
> fault/recovery=`1/1`、pending=`0/0`。未直接覆盖“故障恰在 stock POSIX 调用进行中”，
> 在途调用仍以 AP restart 为回收边界。下一 MAIN Stage 尚未冻结；禁止修改 NuttX/SDK，
> QEMU 工作继续完全排除。完整证据见
> [N11 worklog](nuttx-port/prompts/11-n11-rpmsgfs.md)。
>
> **N10 completed（2026-08-01）：**已从稳定 N9 基线完成 AP crash supervision。
> 当前 wrapper 监测 AP primary、AP logical CPU1、generation-safe RPMsg probe 与真实
> 双向 vring 进展，并提供 `apctl health/recover`。实板已完成 4 次最大帧 load、两轮
> 6 场景完整套件（累计 run=16）、generation 1→2 warm restart、primary timeout
> 及 generation 2→3 人工恢复；随后 secondary/RPMsg timeout 均准确分类，旧 endpoint
> 均立即以 `-107/ENOTCONN` fail-closed，并分别恢复 generation 3→4、4→5。每次恢复后
> RPMsg 均再次 PASS；generation=5 又连续完成两轮无注入 full suite，12/12 场景、
> 2400 次双路 request/reply 零错误且 heap 无漂移。检测与人工恢复基线整体为
> `board-verified`；自动恢复保持默认关闭，仍需独立设计评审和专项实板门禁。完整证据见
> [N10 worklog](nuttx-port/prompts/10-n10-ap-supervision.md)。
>
> **N9 completed（2026-07-31）：**保持 AP native SMP，不切换 BMP；CP 与整个 AP SMP
> cluster 之间已建立一套 RPTUN/OpenAMP/RPMsg link。实现采用官方 SDK wrapper 模式，
> SDK/NuttX 均只读。AP logical CPU0/physical CPU1 独占 mailbox IRQ 与 OpenAMP TX/RX
> gateway；logical CPU1/physical CPU2 是第二个业务 producer，但不是第二个 RPTUN peer。
> shared pending level state、动态 Name Service、满帧 load 1000 次、generation 2/3/4
> reconnect 和显式 `syslog_rpmsg` 均已实板通过。下一 MAIN Stage 尚未冻结；优先候选是
> heartbeat/AP crash supervision（现为 N10 CURRENT），随后再独立评估 `rpmsgfs`、
> BLE HCI 或 Wi-Fi control。
> 完成记录见 [`nuttx-port/prompts/09-n9-rptun-rpmsg.md`](nuttx-port/prompts/09-n9-rptun-rpmsg.md)。

> **N8 physical cold-reset/AP SMP closure（2026-07-31）：**最终无 checkpoint
> `cp_nsh + ap_smp_bidir` 镜像连续 warm 3/3、COM7 RTS physical-reset 3/3，
> 全部进入 NSH；cold 均有 `BClk`。AP `READY/error=0`、CPU2
> `SCHEDULER_ONLINE/error=0`、online=`0x3`，BSMP/affinity/semaphore/
> semaphore-loop/BP2P 均 `PASSED/error=0`。最终根因集合包括 UART
> GPIO-before-UART、post-SDK clock/config restore、GPIO0/1 ownership 前
> `TX_EMPTY` handoff，以及 boot/cache/MPU、WDT ownership、CPU2 三阶段 handshake
> 和 AP self-test local scheduling。physical power cut 尚未执行；CPU2
> post-bringup/scheduler-unlock 的底层 WFE handshake 也未做故障注入 recovery。
> 完整复盘见 [`nuttx-port/n8-cold-reset-resolution-report.md`](nuttx-port/n8-cold-reset-resolution-report.md)，
> 入门版见 [`nuttx-port/cold-reset-smp-repair-guide.md`](nuttx-port/cold-reset-smp-repair-guide.md)。

> **最新覆盖状态（2026-07-30）：**N8-C4 已在用户真实 T5-AI 当前 download/warm-start path 闭环。AP 为 `READY(2)/error=0`，CPU2 为 `SCHEDULER_ONLINE(8)/error=0`、ready=`1`、online=`0x3`；`BSMP`、`BAFF`、`BSEM`、`BSWL` 全部 `PASSED/error=0`。唯一 `task id=3` requested/observed affinity=`0x2/0x2`、CPU=`1`、started/completed/pid-released=`1/1/1`。BAFF tx/rx、IRQ/wake 为 `1→10`、calls=`2→11`（`+9`：initial dispatch + 8 wakes）；BSEM 为 `2→3`、calls=`3→4`（首轮 `+1`）；BSWL 为 `2→10`、calls=`3→11`（完整 8 轮 `+8`）。wait/observe/post/return 与 sequence 均为 `8`，sem value=`-1`，CPU0 post、CPU1 return，global SMP tx/rx=`10/1, 1/10`，coalesced/fail/stale/spurious 均为 0。AP heartbeat=`85`、CPU0 SysTick=`1026`、sleep enter/return=`85/84` 证明 gate 后持续运行。

> **N8-C5 closure（2026-07-30）：**normal autostart build 已在真实 T5-AI 闭环。AP `READY/error=0`，CPU2 `SCHEDULER_ONLINE/error=0`、online=`0x3`；BP2P `PASSED/error=0`、requested/completed=`8/8`。PID5/CPU0 initiator 与 PID4/CPU1 responder 均 started/completed=`1/1`、sequence=`8`、PID released。隔离窗口 CPU0→CPU1 `10→19`=`+9`、CPU1→CPU0 `1→9`=`+8`、calls `11→28`=`+17`；global coalesced/fail、IPI stale/spurious 均为 0。explicit `SCHED_FIFO` / controller+1 priority 是 exact reverse remote-wake 的板级已验证条件。

> **N8-C6 first board evidence（2026-07-30）：**normal autostart 最终在 CP bounded timeout 后进入 NSH；AP `FAILED/error=6`（startup timeout），但 CPU2 已 `SCHEDULER_ONLINE`、online=`0x3`，前置 gate 全部 PASS。BDUL 保持 `RUNNING/error=0`、requested/completed=`8/0`，两个 task 均在 CPU1 started=`1`，但 sequence 停于 `2/2`、completed=`0/0`。根因已收敛为两个同优先级 CPU1 task 在 peer-state polling 中使用 `up_mdelay(1)`，busy delay 不提供确定的本地调度机会。correction 在每次 missed local poll 后增加 `sched_yield()`，并在 create 后立即发布 PID、CPU0 starter post 前证明 A exact-waiting；未引入 timer wake 或额外 cross-CPU IPI，目标仍为 `+3/+0/+3`。

> **N8-C6 closure（2026-07-30）：**local-yield correction 已在真实 T5-AI normal autostart path 闭环。AP `READY/error=0`、heartbeat=`148`；CPU2 `SCHEDULER_ONLINE/error=0`、online=`0x3`。BDUL `PASSED/error=0`、requested/completed=`8/8`；PID5/PID4 均运行在 CPU1，started/completed=`1/1`、sequence=`8/8`、PID released。隔离窗口 CPU0→CPU1 `10→13`=`+3`、CPU1→CPU0 `1→1`=`+0`、calls `11→14`=`+3`；coalesced/fail/stale/spurious 均为 0。CPU0 SysTick=`1775`、sleep enter/return=`148/147`，gate 后持续运行正常。

> **N8-C7 first board evidence（2026-07-30）：**AP `FAILED/error=6` 是 CP startup timeout；CPU2 已 `SCHEDULER_ONLINE`、online=`0x3`，BSMP 仍 PASSED。共享 affinity task 已在 CPU1 started，但 BSWL 只完成 exact `3/8`，状态为 `WOKEN/error=0`，BSEM 同样为 `WOKEN/error=0`；全局 calls=`6` 与 baseline 2 + dispatch 1 + three wakes 精确一致。BMIG record 未出现，证明 migration selftest 尚未被调用，因此当前不能归因于 C7 migration 实现。下一最小证据为不重建、不重刷，直接在 failed/held state 执行一次 `apctl start 3000`，随后读取 `apctl status`。

> **N8-C7 direct BMIG evidence（generation 2）：**同镜像 `apctl start 3000` retry 使 affinity/BSEM/BSWL 全部 PASS，并首次进入 BMIG。BMIG `FAILED/error=5`（BAD_CPU）、requested/completed=`8/0`；PID4 在 CPU0 started/completed=`1/1`，sequence=`0`，global calls 保持 11，证明首次 migration IPI 尚未发生。根因是 self `pthread_setaffinity_np()` 已把 mask 改为 `0x2`，但调用返回时 task 仍在 CPU0，源码随即检查 `up_cpu_index()` 并误判 BAD_CPU。correction 在每次 successful affinity update 后立即 `sched_yield()`，以新 mask 排除当前 CPU并强制一次 controlled scheduler handoff；不使用 timer sleep，目标仍为 `+4/+4/+8`。

> **N8-C7 post-yield evidence：**重建后的 yield image 前置 gate 全 PASS，但 BMIG 保持 `RUNNING/error=0`、requested/completed=`8/0`，PID4 CPU0 started=`1`、completed=`0`、sequence=`0`，calls 仍为 11，随后 CP timeout。源码确认 running-task set-affinity 路径只尝试 source-CPU local switch，未向 affinity target 发送 scheduler IPI；CPU1 无 local SysTick，因此 ready task 可被留在 global ready list。official NuttX tree 不修改。新 correction 改为 task 先发送 target SMP callback 后阻塞；callback exact 证明 waiter，在 blocked 状态更新 task affinity，并在 target CPU local post 唤醒。每轮唯一 SMP callback 即迁移 doorbell，目标仍为 `+4/+4/+8`，`value[0]/value[1]` 预期 `8/8`。

> **N8-C7 rendezvous evidence：**新 image 已由 AP 自身发布 `error=18` BMIG failure，不再只是 CP timeout。BMIG `FAILED/error=6`、sequence=`1`、PID4 final CPU1、started/completed=`1/1`、aux0=`1`；callback start/completion=`2/1`。隔离窗口 CPU0→CPU1 `+1`、CPU1→CPU0 `+1`、calls `+2`，证明 cycle1 完整成功且 cycle2 reverse callback 已到 CPU0，但第二轮 rendezvous 未完成。task 仍在 async call 后等待 callback-start ack，形成不必要的双向等待窗口；correction 已删除 ack poll，call 成功后立即 block，callback 继续兼容先到/后到并 exact 等待 semaphore waiter。

> **N8-C7 immediate-block image prerequisite evidence：**generation1 未进入 BMIG。AP `FAILED/error=15` 对应 SEM_WAKE_LOOP；BSWL `FAILED/error=7`（SEQUENCE）、requested/completed=`8/5`，wait/observe/post/return 与 sequence 均精确为 5；BSEM 仍 `WOKEN/error=0`，global calls=`8` 与 baseline2 + dispatch1 + five wakes 一致。Affinity error7 是前置 gate 的 propagated count mismatch。该结果不验证也不否定 immediate-block BMIG correction。下一步只对同镜像执行一次 `apctl start 3000` + status；若再次在 cycle5 同错，才把 prerequisite 作为 reproducible blocker。

> **N8-C7 immediate-block retry evidence：**generation2 前置 gate 全 PASS，BMIG 完成 sequence6；callback start/completion=`7/6`，CPU0→CPU1 `10→14`=`+4`、CPU1→CPU0 `1→4`=`+3`、calls `11→18`=`+7`。CPU1 handler call/delivered=`14/13` 证明 snapshot 时仍停在 cycle7 callback 内。cycles1..6 已完整证明三轮双向 migration，cycle7 forward callback 也已到达。callback 旧实现每次 polling 都在 target IRQ 中进入 critical section，可能阻碍 source 完成 semaphore block；现改为先 lock-free atomic semcount poll 到 `-1`，再仅一次 critical exact-check TCB/waitobj。

> **N8-C7 closure（2026-07-30）：**lock-free waiter-poll correction 已在真实 T5-AI normal autostart path 闭环。AP `READY/error=0`，CPU2 `SCHEDULER_ONLINE/error=0`、online=`0x3`；全部 prerequisite gate PASS。BMIG `PASSED/error=0`、requested/completed=`8/8`；PID4 final CPU0、started/completed=`1/1`、sequence=`8`、callback=`8/8`、aux=`1/0`。隔离窗口 CPU0→CPU1 `10→14`=`+4`、CPU1→CPU0 `1→5`=`+4`、calls `11→19`=`+8`；handler call/delivered CPU0=`5/5`、CPU1=`14/14`，coalesced/fail/stale/spurious=0。heartbeat `58→155→258`、CPU0 SysTick `789→1849→2989`、sleep enter/return `58/57→155/154→258/257` 证明 gate 后持续运行。READY 状态下额外 `apctl start 3000` 返回 `-16` (`-EBUSY`)，后续状态保持健康，不是 selftest failure。

> **N8-C8 first board attempt（2026-07-30）：**`ap_smp_timedwait` generation1 在 BTIM 前被 CP startup timeout 终止。AP `FAILED/error=6`，但 CPU2 `SCHEDULER_ONLINE/error=0`、online=`0x3`，automatic SMP `PASSED 2/2`。Affinity requested/observed=`0x2/0x2`、PID3 已在 CPU1 started，但未 completed/released；BSEM `WAITING` 且 wait entered/observed/value=`1/0/0`，BSWL requested/completed=`8/0`、wait/post/wake=`1/0/0`。Global calls=3 精确等于 baseline2 + initial affinity dispatch1，handler call/delivered CPU0=`1/1`、CPU1=`2/2`，failure/coalesced/stale/spurious=0。BTIM record 未发布，因此该结果没有执行或否定 timed-wake 实现。下一步只对同镜像执行 `apctl start 3000` + status；running gate 的未完成 before/after snapshot 不得解释为负 delta。

> **N8-C8 direct BTIM evidence（generation2）：**same-image retry 使 affinity/BSEM/BSWL 全 PASS，并完整执行 timed-wake task。AP error19 对应 BTIM selftest rejection；BTIM `FAILED/error=6` 是 COUNT_MISMATCH，不是 sleep/CPU/timeout failure。PID4 在 CPU1 started/completed=`1/1`，sequence=`8`、value=`8/0`、aux=`20000/1`，证明 8 次 20 ms sleep 全部返回 0 且 PID released。隔离窗口 CPU0→CPU1 `10→19`=`+9`、CPU1→CPU0 `1→1`=`+0`、calls `11→20`=`+9`，handler CPU1 call/delivered=`19/19`，failure/coalesced/stale/spurious=0。before snapshot 位于 `pthread_create()` 前，所以 exact attribution 必须包含 initial CPU1 dispatch + 8 timer wakes。源码 terminal expectation 已由固定 `+8` 改为 `BK7258_AP_ADV_CYCLES + 1`；correction 为 static-only，下一步重建/下载/板测。

> **N8-C8 closure（2026-07-30）：**corrected image generation1 再次停在 inherited first-waiter prerequisite，但同镜像 `apctl start 3000` generation2 使全部 prerequisites PASS，并完成 BTIM 板端闭环。AP `READY/error=0`、CPU2 `SCHEDULER_ONLINE/error=0`、online=`0x3`；BTIM `PASSED/error=0`、requested/completed=`8/8`。PID4/CPU1 started/completed=`1/1`、sequence=`8`、value=`8/0`、aux=`20000/1`。隔离窗口 CPU0→CPU1 `10→19`=`+9`、CPU1→CPU0 `1→1`=`+0`、calls `11→20`=`+9`；handler CPU0=`1/1`、CPU1=`19/19`，coalesced/fail/stale/spurious=0。heartbeat `1→62`、CPU0 SysTick `52→730`、sleep enter/return `1/0→62/61` 证明 gate 后持续运行。N8-C8 以 same-image generation2 restart path `board-verified`；generation1 stall 是 inherited prerequisite evidence，不是 BTIM failure。

> **N8-D1 closure（2026-07-30）：**`ap_smp_lifecycle` normal autostart 在真实 T5-AI 一次闭环。AP `READY/error=0`、heartbeat=`727`；CPU2 `SCHEDULER_ONLINE/error=0`、online=`0x3`。BLCY `PASSED/error=0`、requested/completed=`1/1`；callback entry/exit CPU=`1/1`、started/completed=`1/1`、quiesce/resume sequence=`1/1`、value=`0/-138`（该 NuttX 配置的 `-ENOTSUP`）、aux=`1/1`。隔离窗口 CPU0→CPU1 `10→11`=`+1`、CPU1→CPU0 `1→1`=`+0`、calls `11→12`=`+1`；handler CPU0=`1/1`、CPU1=`11/11`，coalesced/fail/stale/spurious=0。CPU0 SysTick=`8090`、sleep enter/return=`727/726` 证明 gate 后持续运行。CPU1 全程保持 online=`0x3`，没有 CPU2 reset/power transition；这是 bounded scheduler quiesce/resume foundation，不是 CPU hot-unplug。

- **Current Stage：**recoverable BL1/BL2/MCUboot 启动链已实现；旧 N15/N17
  update lifecycle 已退役。下一阶段以 `progress/CURRENT.md` 为准。
- **Authorized implementation set：**N8-C5..N8-D1 与 N9-R..N9-V 全部完成并实板
  闭环；N10 wrapper、重复满载、warm restart、primary/secondary/RPMsg 三类注入、
  fail-closed、三次人工恢复与 generation=5 无注入重复 full suite 均已实板通过；N11
  stock RPMsgFS wrapper、exclusive-state 内存修复、故障态 bounded wait 与 generation
  recovery 也已实板通过；N12/N13 Bluetooth与N14 PSRAM/timer全套wrapper均已完成。
- **Latest board-verified runtime baseline：**当前板端运行`cp_nsh_wifi + ap_smp_wifi`并已runtime关联。STA、gateway ICMP、local TCP/UDP、
  active-Wi-Fi RPMsg/RPMsgFS/Bluetooth、AP restart `-EBUSY`及COM7 RTS 3/3通过；AP READY、
  CPU2 online、RPTUN connected、Supervisor healthy、AP SMP passed。
- **Latest worklog：**[`nuttx-port/prompts/16-n16-wifi-data-plane.md`](nuttx-port/prompts/16-n16-wifi-data-plane.md)
- **Source verification：**[`nuttx-port/n15-ota-source-verification.md`](nuttx-port/n15-ota-source-verification.md)；
  N14完成记录见[`nuttx-port/n14-psram-source-verification.md`](nuttx-port/n14-psram-source-verification.md)；
  最终证据见 [`nuttx-port/n14-evidence-index.md`](nuttx-port/n14-evidence-index.md)；N13 BLE复核见
  [`nuttx-port/n13-ble-gap-gatt-source-verification.md`](nuttx-port/n13-ble-gap-gatt-source-verification.md)；
  N9 transport复核见 [`nuttx-port/n9-rptun-source-verification.md`](nuttx-port/n9-rptun-source-verification.md)；
  N8 closure 见 [`nuttx-port/n8-cold-reset-resolution-report.md`](nuttx-port/n8-cold-reset-resolution-report.md)；
  新手说明见 [`nuttx-port/cold-reset-smp-repair-guide.md`](nuttx-port/cold-reset-smp-repair-guide.md)；
  执行流程见 [`nuttx-port/bk7258-build-flash-debug-sop.md`](nuttx-port/bk7258-build-flash-debug-sop.md)。
- **Verified implementation：**一个 asynchronous SMP callback 在 CPU1 call-handler context 发布 quiesce handshake，bounded `wfe` 等待 CPU0 sequence + `sev`，随后仍在 CPU1 恢复并返回；scheduler-online secondary-stop 继续 fail-closed 为 `-ENOTSUP`。
- **Verified counter attribution：**BLCY CPU0→CPU1 `10→11`=`+1`，CPU1→CPU0 `1→1`=`+0`，calls `11→12`=`+1`；entry/exit CPU=`1/1`、sequence=`1/1`、value=`0/-138`、aux=`1/1`。
- **Verified lifecycle：**AP `READY/error=0`；CPU2 `SCHEDULER_ONLINE/error=0`、ready=`1`、online=`0x3`；BSMP/BAFF/BSEM/BSWL/BLCY 全部 `PASSED/error=0`，global coalesced/fail、IPI stale/spurious、duplicate/lost 均为 0。CPU2 未 reset/power-down，D1 不构成 CPU hot-unplug。
- **Verified liveness：**AP heartbeat=`727`、CPU0 SysTick=`8090`、sleep enter/return=`727/726`；N8-D1 gate 后持续运行正常。
- **Preserved boundary：**`CONFIG_SMP_DEFAULT_CPUSET=0x1`；C5/C6/C7/C8/D1 仅执行各自固定次数、显式 affinity 和互斥配置的诊断流程；不开放运行时可变/无限循环、默认 CPU1 placement、非受控迁移、负载均衡或 stress test。
- **Architecture：**NuttX semaphore/scheduler → team wrapper → Beken SDK `bk_mailbox_cc_*` / `bk_mailbox_master_send` → MBOX0 FIFO/source63 → NuttX IRQ79；不新增寄存器级 IPI 数据面。
- **Lifecycle boundary：**N8-D1 只通过 asynchronous SMP callback + WFE/SEV 实现 bounded scheduler quiesce/resume，CPU1 始终保持 online=`0x3`，不 reset/power-down CPU2，也不声称 CPU hot-unplug；`apctl stop/restart/cycle/ipitest` 和 scheduler-online secondary-stop 继续 fail-closed 为 `-ENOTSUP`。
- **Historical N8 exclusion：**N8 当时未授权 RPTUN/RPMsg；该项现已由 N9 计划取代。
  仍未选择默认 cpuset `0x3`、非受控迁移/负载均衡、运行时可变/无限循环、stress test、
  spinlock 压力、Wi-Fi 或 BLE。

### 历史 N6 handoff（保留证据，非 CURRENT）

> **N6 最终补充状态（2026-07-26）：**`CONFIG_SYSTEM_TIME64=y`、Stage B TIMER1/source-3 IRQ bridge、GPIO C0、GPIO C1 和 GPIO C2 均已 `board-verified`。最终 GPIO 映射为 GPIO_S=source55/IRQ71、GPIO_NS=source37/IRQ53；C2 两次连续 stock `gpio -w 5 /dev/gpio1` 通过，生产配置保留 `CONFIG_DEV_GPIO_NSIGNALS=2` workaround。生产调试内容已清理，clean artifact=251532 B，SHA-256 `987ca15ffe2c8fb167b46c0f06d306a4545c20126f68404fa285a78dc82077d6`。

> **历史覆盖状态（2026-07-23）：**Stage B TIMER1/source-3 手动 IRQ bridge 测试已完成全量构建和 post-link 静态验收。`bkirqtest_main`、runner、snapshot、callback A/B 及 built-in registry 均进入最终 ELF/map；production bridge verifier 为 **48/48 PASS**。当前待用户执行 Windows fast-download 多轮板测，不再受“命令 absent / 禁止 flash”的旧状态约束。测试固件为 `/home/lijian/project/open-vela/nuttx/all-app.bin`，240618 B（`0x3abea`），SHA-256 `21a4f281cccf87500bd7c67a31d6aa097cfe0bb175ab9730d5a0bf5f44f589e9`。每轮下载后连续运行三次 `bkirqtest`；至少完成三轮独立下载/启动。最后一轮保留运行并继续完成 `>4400 s` 系统时间验证。完整证据与判据见 N6 worklog 最新条目。

- **Historical Stage：N6 — Beken SDK integration / WDT / IRQ adaptation**
- **Historical branch：**`bk7258-n6-sdk-irq-bridge`（从已推送的 A1 `57558eb` 分出）
- **Historical worklog：**[`nuttx-port/n6-sdk-integration-worklog.md`](nuttx-port/n6-sdk-integration-worklog.md)
- **Historical verified point：**WDT automonitor feeding（`ABWTK`）、UART RX、NSH命令和LittleFS读取已`board-verified`（baseline不变）。A0 RAM-vector plumbing **`board-verified`**（2026-07-22）。A1 Task 1-4 complete。**A1 `board-verified`**（2026-07-22）：board-flashed `all-app.bin` = 236028 B (`0x399fc`), SHA-256 `a92352eeea5ebbab4eb4a7a95fd97a73d950816a3620242be8e8fc141030b5a5`（user confirmed 16:04 rebuild; Task 3 static artifact `33f68aa...ebc2293` is historical evidence only）；VTOR=`0x28000800`，flash magic slots 64/65 = `32374B42 00003633`，RAM slots 15/31/64/65 = `0x020108FD`，UART/NSH/WDT automonitor/LittleFS `BK7258LFS-OK`/DVFS tier-5 baseline all PASS。SDK bundle local/ignored + checksum-pinned（374 manifest entries: 341 headers + 2 config + 31 linked libs; 50 excluded libs; 4 .obj not linked; fail-closed validation; deterministic LC_ALL=C ordering; --install parent-creation fix; 6/6 tests pass）。**Precommit build/static verification complete**（Task 14, 2026-07-22）：distclean+build exit 0; 23/23 verifier gates PASS; negative G23 test PASS; `git diff --check` exit 0; 0 team-file warnings; precommit `all-app.bin` = 236028 B, SHA-256 `d7b73c7fdf1275a90621b54e0343d6de31d343f115eb6acd0173fb971a2cf1b0`（compile/static-only, NOT board-tested; board-flashed hash `a92352...b5a5` remains board-verified）。A1 已以 code `66b29d1` + docs `57558eb` 推送至 `fork/bk7258-n6-ramvectors`。Stage B CPU0 SDK IRQ bridge 已获用户授权，当前分支 `bk7258-n6-sdk-irq-bridge`；dedicated bridge、pending-clear helper、Kconfig/Make/CMake/defconfig gate 与 RED→GREEN verifier 均已实现。2026-07-23 fresh distclean/build 成功，`nuttx.bin`=156604 B、`all-app.bin`=236028 B，生成配置含 `CONFIG_BK7258_SDK_IRQ_BRIDGE=y` / `CONFIG_ARCH_IRQPRIO=y`；但 post-link verifier 为 25 PASS / 10 FAIL：最终五个 lifecycle symbol owner 仍是 SDK `libdriver.a(interrupt_base.c.obj)`，direct `arch_interrupt_*` 与 `icu_int_map_table` 仍在 ELF，故 Stage B 仍为 post-link RED。已在 team linker script 中配置条件 `EXTERN(bk_int_isr_register)`，让 `libarch.a` 在 SDK archive 前抽取 bridge object，并新增 verifier `S10` gate；该 link-order fix 已 fresh distclean/build 成功：`nuttx.bin`=156052 B、`all-app.bin`=235450 B，较 pre-fix 分别减少 552/578 B；post-link verifier 已 36/36 PASS：五个 lifecycle symbols 均由 overlay `libarch.a(bk7258_sdk_irq.o)` 提供，SDK `interrupt_base.c.obj` 未抽取，direct `arch_interrupt_*` 与 `icu_int_map_table` 均从 final ELF 消失。A1 preserved-invariant check 已 18/18 PASS：80-slot vector/RAM vector/magic/repair calls/partition/concat 均保持，team-source warnings=0；fresh Stage B `all-app.bin`=235450 B (`0x397ba`)，SHA-256 `45d7b9868365265830508067e1c26e5bf210b21e9a2dda43b00eedf5b6db9725`（compile/static-only）。Focused review 新发现 F1 Important：SDK map 中 source 27 (`INT_SRC_LCD`) default priority 明确为 0，其他 source 为 6；当前 bridge 对所有 source 硬编码 6，故 lifecycle semantics 尚不正确，当前 artifact 禁止 board-test，Stage B 状态回到 `blocked`。LCD priority-0 exception 已实现；verifier 新增 S11-S14/E07-E08，source/config 全 PASS，stale F1 object 被 E08 正确拒绝（41/42 PASS，exit 1）。F1-corrected fresh distclean/build 已成功：`nuttx.bin`=156068 B、`nuttx_crc.bin`=165852 B、`all-app.bin`=235484 B；fresh hardened bridge verifier 已运行但仍为 41/42 PASS，仅 E08 失败，其他 source/config/ownership/lifecycle gates 全 PASS。Preserved A1 invariant suite 尚未重跑。No agent-performed flash/download。fresh bridge object 已确认 F1 生产语义正确；E08 因只接受 literal `#192/0xc0`、未识别 GCC 的 `#6` 后 `lsl #5` 而 false-negative。E08 exact stale/fresh RED→GREEN 已完成：stale 41/42（仅 E08 FAIL），fresh 42/42 PASS。Preserved A1 invariant checks 18/18 PASS；F1-corrected artifacts：ELF `6786c645...dad3a`、`nuttx.bin` `f736d4f2...6c2c`、`all-app.bin` 235484 B / `5ff2ce00...4cf89ce`（compile/static-only）。Final focused review 发现 F2 Important：bridge lifecycle 无完整序列化，concurrent same-source replace/custom-priority/deinit 可 lost-update；Stage B 回到 `blocked`。Lifecycle-lock RED gates 已安装未运行：S15 source lock/helper + 四个 E09 object IRQ-serialization checks。F2 RED 已确认：既有 42 gates PASS，新增 S15 + 四个 E09 共 5 FAIL。F2 bridge-local lifecycle lock + internal locked-unregister helper 已实现未构建；dispatch 未加锁。F2 stale gate 已得到 S15 GREEN / 四个 E09 RED（43/47 PASS）。F2 fresh distclean/build 已成功：`nuttx.bin`=156108 B，`all-app.bin`=235518 B。Fresh verifier 37/47 PASS：helper refactor 导致 E05/E06 direct-call gates 失配，E08/E09 也未识别。Fresh object 已确认 internal helper call graph、BASEPRI serialization 与 fused shift 均正确，当前 10 FAIL 为 verifier stale。Verifier helper-aware/BASEPRI/fused-shift 适配已完成，并新增 E10 helper call-graph gate（总 48）。F2 historical/fresh pair 已完成：stale 42/48（E09x4+E10+E08 FAIL），fresh 48/48 PASS。Preserved A1 18/18 PASS；F2 `all-app.bin`=235518 B / SHA-256 `6de61d94...ba7c0b`（compile/static-only）。Bridge 48/48 + A1 18/18 GREEN。Final focused review 已收口且无新 blocker。Timer-test 已实现但未构建：TIMER_ID1 + `INT_SRC_TIMER`/IRQ19，六路 timer idle fail-closed gate，snapshot/restore 原 top-level handler，chip-local runner + 手动 `bkirqtest` 均已落地；流程证明 A callback→unregister 后 status 有效但 counter 静默→B re-register，且所有 touched-source 出口恢复原 handler。Source review/precheck 已 PASS；timer-test fresh distclean/build exit 0。First post-build gate RED：generated config 与 bridge 48/48 PASS，但 `bkirqtest` registry、runner/snapshot/callback symbols 全 absent；timer-test blocked，禁止 board-test。Root cause 已定：chip object 在 libarch，缺失点是 manifest 的 nested `packages/demos/...` 没有 category Make.defs，root wildcard 无法进入 app。下一步加 team-owned demos Make/CMake/Kconfig aggregators + manifest linkfiles，local refresh 后重建；不得 flash。
- **Execution evidence：**N4-D0/D0D（时钟诊断 baseline + runtime SysTick bookkeeping，feature commit
  `6f596b7`）+ D0F（100Hz tick 兼容性，feature commit `8dab594`）已 **substage `board-verified`**
  （2026-07-18）。D0F defconfig 移除旧 100ms override，生效默认 10ms/100Hz；`CONFIG_USEC_PER_TICK=1000`
  （1000Hz）manual-reset 路径失败重启，已 rejected。**N4-D1（DPLL lock）blocked**；DPLL enable /
  mux 切换 not attempted；**整 N4 not board-verified**。详见 [N4-D0 worklog](nuttx-port/n4-d0-clock-diag.md)。
  剩余收口：`6f596b7` 精确 commit 的 state-C 重编/重刷复验尚未完成。
- **N5 flash filesystem（board-verified 2026-07-19）：**N5-D0..D4 board-observed（layout candidate、flash ID、content dump、magic scan、emptiness scan）；N5-D5 raw flash erase/write/read-back/re-erase board-verified（`0x00100000`，2026-07-19）；N5-D6 MTD lower-half board-verified（方案 A：每次 op 临时清/恢复 SR0 块保护，CONFIG_BK7258_FLASH_MTD，`chip/cp/bk7258_flash_mtd.[ch]`）；N5-D7 LittleFS filesystem board-verified（CONFIG_BK7258_FLASH_LITTLEFS，ftl 注册 `/dev/mtdblock0`，mount 到 `/data`，autoformat 仅首次，probe 文件重启持久化通过）。全链路：raw flash → MTD → ftl block device → LittleFS。D7 版 `all-app.bin` = 192270 B = `0x2EF0E`（< `0x100000`，boot/app 区不受影响）。详见 [N5 worklog](nuttx-port/n5-flash-filesystem.md)。
- **频率阶梯 / 480 MHz recovery note：**
  - **不要重试 `M1=0x430`（480M/1）**：Beken SDK `sys_hal_core_bus_clock_ctrl()` 中
    `PM_CLKSEL_CORE_480M` + `PM_CLKDIV_CORE_0` 组合被 guard 明确拒绝（返回 `BK_FAIL`）；板端
    探测在 `N4D0:480S` 后 stall，与 SDK 政策一致。
  - **当前最高板端/J-Link 验证的 loader-residue 操作点为 320 MHz**（M1=0x420, csrc=2, cdiv=0）。
  - 240 MHz（480M/2）和 320 MHz（320M/1）均为 SDK 支持的操作点，已板端验证。
  - **下一步只在有新证据表明安全路径时才调查 480 MHz 操作点**；当前无此证据。
  - 详细频率阶梯表与 SDK guard 分析见 [N4-D0 worklog §10](nuttx-port/n4-d0-clock-diag.md#10-频率阶梯证据与-sdk-guard-分析loader-residue-muxdiv-probes)。

## 冻结的 N3 baseline

以下是恢复会话时的不可变证据锚点，不代表未来会话的永久 current HEAD：

- branch anchor：`contest2026-multi-board`
- N3 code immutable anchor：`4d9198e`
- N3 docs immutable anchor：`68badfe`
- boot trace 结束于：`N2 / DBESITtCAP / NuttShell (NSH) / nsh>`；`A` = `board_app_initialize` 入口，`P` = procfs mount 成功
- `ps`：PID 0 / CPU0 / `IDLE`，PID 1 / CPU0 / `nsh_main`
- `/proc`：`0`、`1`、`cpuinfo`、`fs`、`memdump`、`meminfo`、`self`、`tcbinfo`、`uptime`、`version`
- state-C `/proc/version` 时间戳：`2026-07-18 15:11:55`
- known-good `$FW/all-app.bin`：163574 B = `0x27EF6`，SHA-256 `8cdc784fba08b931d124376f545f85c538e55626ace2be67b555b34ac7dc08a6`
- known-good `$FW/nuttx.bin`：88388 B，SHA-256 `74b6e5a7bdb8fabe9a30c8fbaa263244e1c861233ede63cfda61a56f55dfd8ef`

> `$FW/all-app.bin` 是可变构建产物。N4 只要重建，就必须重新计算实际字节长度与哈希；不得盲目复用 N3 的 `0x27EF6`。

## 命名与维护规则

- **每有一项实质性新进展，必须先更新当前Stage的`docs/` worklog，再进入下一项技术动作。** 实质性进展包括代码/配置修改、构建或板端结果、假设证实/证否、回滚、根因结论、阻塞项和下一步变化；记录日期、证据、状态标签、当前诊断代码及下一最小步骤。
- 每个 MAIN Stage 恰好一个提示词文件，命名为 `NN-nX-<slug>.md`；两位序号必须与 MAIN Stage 编号一致。
- 一个 Stage 内的诊断/实现/验证步骤只是有序 subsection，不拆成独立 Stage 文件。
- Stage 完成后，其文件成为不可变历史 handoff；仅允许事实性纠错，不在原文件里续写下一 Stage 实现。
- 本 master 只维护 Stage 顺序、CURRENT 指针与冻结 baseline，不复制完整 Stage prompt。
- 新会话必须运行时重新发现 current HEAD、工作树状态、push 状态和产物长度/哈希；不得把锚点误当永久现状。
- 文档提交不得把它自身尚未知晓的 docs commit SHA 写进自身；先用描述性占位，提交后再由后续事实性更新记录。
- 统一状态词：`static-only`、`build-verified`、`board-verified`、`skipped`、`blocked`。
- **CP startup attribution 已由用户确认**：Beken SDK 中存在 `Reset_Handler_Cpu0` →
  `sys_drv_early_init()` → `sys_hal_early_init()` 调用链（CP SDK early-init 路径）；loader
  `--reboot 1` 的 80 MHz residue 与此链的执行结果一致。**当前 NuttX overlay 不移植也不调用该链**；
  后续会话**不要把它说成 NuttX 已移植/执行**，只按 inherited loader residue 处理。遇到具体
  blocker（如 D1 的 DPLL locked-bit 断言、analog batch 副作用范围）时再定向查看 SDK 片段。

## 目录图

```text
docs/bk7258-t5ai/
├── next-stage-prompt.md
└── nuttx-port/
    └── prompts/
        └── 04-n4-clock-bringup.md
```
