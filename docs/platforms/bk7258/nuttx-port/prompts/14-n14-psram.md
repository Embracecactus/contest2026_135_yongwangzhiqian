# BK7258 Stage N14：PSRAM 与 SDK 软件定时器 wrapper

> 日期：2026-08-03
> 状态：**COMPLETED / `board-verified`（2026-08-03）**
> 最新完整实板基线：`cp_nsh_psram + ap_smp_psram`，Beken SDK v3.1.1.9
> 源码复核：[N14 PSRAM source verification](../n14-psram-source-verification.md)
> 证据索引：[N14 evidence index](../n14-evidence-index.md)
> 硬边界：官方 NuttX、apps、Beken SDK 源码和 SDK 静态库只读；永久实现只落在
> board/app/build wrapper、配置、验证器与文档中

## 1. 目标与验收边界

N14 在 N13 完整 BLE/RPMsg/AP SMP 基线上完成以下闭环：

1. 按官方 SDK owner 关系由 CP 初始化并持有 PSRAM，AP 只消费；
2. 识别 T5-AI 实板的 16 MiB PSRAM，并在任何 heap/AP 启动前做一次全容量破坏性门禁；
3. 为 CP 和 AP 建立互不重叠的 role-local NuttX private heap；
4. 证明 AP logical CPU0/CPU1 可并发 malloc/zalloc/realloc/free，且无泄漏、损坏或死锁；
5. 补齐官方 SDK 软件定时器需要的 task-context callback wrapper，并覆盖 callback 内 self-delete；
6. 保持 N8 SMP、N9 RPMsg、N10 supervisor、N12/N13 Bluetooth 基线健康；
7. 保持 official NuttX/apps/SDK tree 零永久改动。

[Beken BK7258 官方产品页](https://www.bekencorp.com/index/goods/detail/cid/60.html)
给出的器件能力是“PSRAM 最高 16 MB”；这只是芯片能力上限。N14 的板卡容量结论来自实板
`id=0x8d08`、post-init `config=0x8d1a`、16 MiB anti-alias 检查和全容量读写测试，不能只由产品页推导。

首版明确不做：

- 不把 16 MiB 全部暴露给通用 allocator；
- 不启用 cacheable PSRAM，不定义 cache maintenance/DMA 一致性方案；
- 不实现 media/DMA slab、动态分区或 AP/CP 共享 allocator；
- 不提供运行时破坏性 raw-capacity 命令；
- 不修改 NuttX `mm`、Beken SDK driver/PM/FreeRTOS heap；
- 不把功能测试次数解释成量产性能、寿命或掉电 SLA。

## 2. 冻结架构

### 2.1 CP 唯一硬件 owner

启动顺序冻结为：

```text
CP board_app_initialize()
→ AP control wrapper ready，AP 仍保持 reset
→ official PHY/RF + Bluetooth IPC first-calibration leaf sequence
→ bk_pm_module_vote_psram_ctrl(AS_MEM=10, ON=0)
→ read ID/config + 16 MiB anti-alias gate
→ one-shot full detected-capacity destructive boot test
→ CP role-local PSRAM heap
→ supervisor initialization
→ official CPU1 PM vote
→ AP boot address/reset release
→ AP role-local PSRAM heap
→ AP CPU0+CPU1 concurrent heap gate
→ RPTUN/Bluetooth/AP READY
```

AP 不调用 `bk_psram_init()`，也不进行 PSRAM PM vote。CP PSRAM 失败会阻断 AP release，不能降级为
“打印 warning 后继续 READY”。factory 首次 PHY/RF 校准允许先完成其 analog programming，因此
PSRAM 硬件初始化不能提前到 `__start()`。

### 2.2 容量与首版内存布局

实板物理窗口为 `[0x60000000, 0x61000000)`，共 16 MiB。首版保留官方
`projects/app` 的低 8 MiB ABI：

| 区域 | 地址 | 大小 | N14 策略 |
|---|---:|---:|---|
| official media/reserved | `0x60000000..0x606fffff` | 7 MiB | 保留，不建立通用 allocator |
| CP PSRAM heap | `0x60700000..0x6071ffff` | 128 KiB | CP private NuttX heap |
| AP PSRAM heap | `0x60720000..0x607bffff` | 640 KiB | AP SMP private NuttX heap |
| AP PSRAM section | `0x607c0000..0x607fffff` | 256 KiB | 保留，不纳入 heap |
| upper physical PSRAM | `0x60800000..0x60ffffff` | 8 MiB | boot-tested、首版保留 |

因此 N14 证明的是“板上 16 MiB 全容量可寻址且低 8 MiB ABI 中 768 KiB role-local heap 可用”，
不是“已有 16 MiB 通用 heap”。

### 2.3 MPU 与 cache 策略

CP、AP primary 和 AP secondary 都安装 official-compatible MPU region 6：

- `RBAR=0x60000002`
- `RLAR=0x63ffffe3`
- Normal memory、non-shareable、non-cacheable

MPU region 覆盖范围大于实际器件容量；所有 allocator 和测试边界仍以探测到的 16 MiB 物理容量
与冻结 heap 范围为准。首版不宣称 cacheable PSRAM 或 DMA coherent。

### 2.4 allocator 与 SMP 串行化

每个角色使用独立 `mm_initialize_heap()`，`mm_heap_s` 控制块和板级 lock 留在内部 SRAM，PSRAM
只保存 allocation nodes/payload。原因是 BK7258 PSRAM bus 不能完成 Arm exclusive store；若把
heap control block 放在 PSRAM，第一次原子更新即可永久重试。

AP 双核不能直接并发进入 NuttX private heap 的 sleeping recursive mutex。所有 board PSRAM
wrapper 操作先通过 internal-SRAM `spin_lock_irqsave()` 串行化，使 inner NuttX heap mutex不会在
两个 AP CPU 间形成可睡眠竞争。这与官方 AP SMP `heap_4.c` 的 internal spinlock/critical-section
设计一致，不需要修改 NuttX allocator。

`realloc` 不调用 `mm_realloc()`；它按官方 AP wrapper 的 allocate → copy → free 模式实现，并使用
`mm_malloc_size()` 把 copy 长度限制为 `min(old_size, new_size)`。

### 2.5 SDK 软件定时器 wrapper

官方 SDK 的 software timer callback 运行在 timer daemon task，而不是 watchdog/tick ISR。board
wrapper 因此只在 watchdog expiry 中排队，由固定 `bk-sdk-timer` kthread执行 SDK callback。

对象生命周期冻结为：

- callback 可在自身内部 `rtos_deinit_timer()`；
- SDK handle 立即 detach；
- 若 delete entry 已排队，该 entry 拥有 final free；
- timer service 返回 callback 后才真正释放对象；
- callback context 必须是 CPU0 task context、非 caller task、非 ISR。

`bktimertest` 还用 5 ms period + 20 ms callback 制造 queued self-delete，专门覆盖“下一次 expiry
已排队而当前 callback 正在执行”的竞态。

### 2.6 官方 PM/clock 补充约束

N14 源码复核同时修正了两个会直接影响 AP/PSRAM 稳定性的旧 wrapper 假设：

- physical CPU1 release 必须先调用 official
  `bk_pm_module_vote_power_ctrl(CPU1=17, ON=0)`；单独 `pwr_dw=0` 不会等价地清 halt、恢复 clock
  并等待稳定；
- v3.1.1.9 的 320 tier 使用 `VDDD=0x7`、`VDDIG=0xe`、CPU0 speed bit 0（`/2`），
  CPU1/CPU2 speed bit 1（`/1`）和 exact settle loop `2600`。因此 CP effective clock 是
  160 MHz，AP physical CPU1/CPU2 是 320 MHz。

这些是 board wrapper 对 official SDK 语义的校正，不是 SDK 源码修改。

## 3. 分阶段完成记录

| 子阶段 | 工作 | 退出结果 |
|---|---|---|
| **N14-R** | 产品页、v3.1.1.9 PM/driver/HAL/layout/MPU/AP allocator/timer/clock source复核 | `source-verified` |
| **N14-A** | CP owner、PM vote、ID/capacity、MPU、全容量 boot gate | 实板 `id=8d08/config=8d1a/capacity=16777216/raw=1/1` |
| **N14-B** | CP/AP disjoint private heaps与 SDK allocator wrapper | CP 128 KiB、AP 640 KiB，边界 verifier PASS |
| **N14-C** | AP CPU0/CPU1 concurrent malloc/zalloc/realloc/free | 16/16 + 16/16，CPU=`0/1`、error 0、free前后相同 |
| **N14-D** | deferred SDK timer与 queued self-delete lifecycle | `bktimertest 256` PASS；20 ms long callback、queued delete闭环 |
| **N14-E** | warm cycle、physical cold、RPMsg/Bluetooth/heap回归 | AP cycle 10、physical RESET 3/3、全套回归 PASS |
| **N14-V** | clean rebuild、final sparse flash、factory首次校准、post-calibration cold、官方树边界 | 全部 PASS；N14 `board-verified` |

## 4. 关键故障与根因

### 4.1 AP 未启动

只做 `sys_drv_set_cpu1_pwr_dw(0)` 的旧 wrapper 不能替代 official PM vote。补齐 CPU1 module 17
power-on vote 后，physical CPU1/AP 才稳定进入启动链。

### 4.2 heap control block 放入 PSRAM 时卡死

PSRAM 不完成 Arm exclusive store，把 `mm_heap_s` 放在 PSRAM 会在 allocator 初始化原子操作处
永久重试。改用 `mm_initialize_heap()` 让 control block留在内部 SRAM后恢复。

### 4.3 双核同时停在 realloc

第一步按官方 `psram_realloc()` 改成 allocate-copy-free 后，双核仍可同时停在
`REALLOC_ENTER`。live stage telemetry证明问题不是 payload copy，而是两个 CPU 竞争 NuttX
private heap 的 sleeping recursive mutex。加入 board outer spinlock后，两核固定 16 轮全部完成，
且 warm/cold重复不再复现。

### 4.4 timer callback self-delete 竞态

旧路径会在 callback self-delete时立即 free，而同一对象可能已有 queued service entry，形成
use-after-free。最终由 queued delete entry拥有 final free，并用长 callback回归确认。

## 5. 最终板端结论

最终 `bkpsramtest info`：

```text
BPSR INFO status=0 ready=1 id=8d08 config=8d1a capacity=16777216
BPSR RAW runs=1 passes=1
BPSR APTEST status=0 requested=16 completed=16/16
  active=16/16 stage=12/12 errors=0/0 cpu=0/1
  free=655344->655344
BPSR INFO PASS
```

正式回归还包括：

- `bkpsramtest all 256`：CP heap 256/256，free `131056→131056`；
- `bktimertest 256`：callbacks 256，queued self-delete 1；
- `bkrpmsgtest all 100 60000`：六个 payload/load 场景、CPU0/CPU1 均 100/100；
- `bkbttest info 10000`：真实 BD_ADDR、fallback 0、ACL MTU/buffer正常；
- `apctl cycle 10 60000`：generation 2..11 全部闭环，generation 12重新启动后 PSRAM再通过；
- 3/3 COM7 RTS physical cold、final clean cold、factory首次校准和校准后 cold均到
  `PASS_NSH`，且 PSRAM/AP/RPTUN/supervisor/SMP保持健康。

完整路径、哈希和 raw log见 [N14 evidence index](../n14-evidence-index.md)。

## 6. Handoff

- `cp_nsh_psram + ap_smp_psram` 是当前 latest board-verified baseline；N13 profiles继续作为
  不含 PSRAM 的 BLE service回退基线。
- official NuttX/apps/SDK及 v3.1.1.9 archive保持不可修改；扩展能力继续采用 board wrapper。
- upper 8 MiB仍是 boot-tested/reserved。要启用它，必须单独设计 partition、owner、cache/DMA
  policy和回归，不能只扩大 heap宏。
- 不得移除 CPU1 official PM vote、allocator outer spinlock、allocate-copy-free realloc或
  deferred timer final-free规则，除非重新完成 source verifier和 N14完整板测矩阵。
- 当前没有已批准的下一 MAIN Stage；OTA、Wi-Fi、security和更激进的 PSRAM分配均需先单独讨论。
