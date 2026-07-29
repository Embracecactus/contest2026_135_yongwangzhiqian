# BK7258 N8-B2 — AP logical CPU0 ↔ logical CPU1 bidirectional IPI

日期：2026-07-29

状态：`board-verified`（2026-07-29，真实 T5-AI 板卡）

分支：`feat/bk7258-ap-smp`

## 1. 本 Stage 边界

N8-B2 从已板测的 N8-B1 继续：

```text
physical CPU1 -> AP logical CPU0 -> NuttX SMP primary
physical CPU2 -> AP logical CPU1 -> NuttX-aware secondary bootstrap
```

本 Stage 只建立并验证 AP 内部 logical CPU0 ↔ logical CPU1 的双向 IPI 传输和 IRQ/WFI 唤醒：

- logical CPU0 / physical CPU1 向 logical CPU1 / physical CPU2 发送 IPI；
- logical CPU1 / physical CPU2 在 mailbox IRQ 中接收，并向 logical CPU0 回送 IPI；
- 两个方向各至少 100 次；
- 发送、接收和 sequence 必须闭合，pending、duplicate、lost、send failure 均为 0；
- CPU2 必须由 WFI 被 mailbox IRQ 唤醒；
- CPU2 仍停留在 `SECONDARY_READY`，`online_mask` 仍为 `0x1`。

本 Stage 仍不调用 CPU2 `nx_idle_trampoline()`，不让普通 task 在 logical CPU1 运行，不做
scheduler delivery、SMP call、affinity/migration/spinlock 压力，也不加入 RPTUN/RPMsg、Wi-Fi 或 BLE。

## 2. 架构约束：沿用 SDK wrapper

继续遵守 N6 确立的分层：

```text
NuttX SMP/诊断控制
        ↓
team-owned BK7258 thin wrapper
        ↓
Beken SDK mailbox / cross-core API
        ↓
BK7258 MBOX0 FIFO + source-63 mailbox IRQ
```

N8-B2 不在新 IPI wrapper 中直接编程 mailbox 寄存器。硬件发送、FIFO drain、ack 和 cross-core
消息分发继续使用 AP SDK 已有实现：

- `bk_mailbox_cc_init()`；
- `bk_mailbox_cc_init_on_current_core()`；
- `bk_mailbox_master_send()`；
- SDK `mbox0_drv_isr_handler()`；
- SDK 回调入口 `crosscore_mb_rx_isr()`。

SDK 的 `bk_int_isr_register(INT_SRC_MAILBOX, ...)` 由 team-owned SDK-to-NuttX IRQ bridge 接管，
映射到 NuttX `BK7258_IRQ_MAILBOX` / IRQ 79。CPU2 vector 只把 mailbox slot 79 接入
`exception_common`；其他异常和外设 IRQ 继续 fail-closed 到 CPU2 fault handler。

## 3. 诊断协议

在 shared SRAM offset `0x200` 新增独立的 0x80-byte IPI state，不扩展或破坏：

- AP boot state `0x000..0x07f`；
- AP fault state `0x080..`；
- CP fault state `0x100..`；
- CPU2 N8-A/N8-B1 state `0x180..0x1ff`。

IPI command 使用 SDK cross-core zero-length message 的 32-bit command word，编码：

```text
31..24  magic 0xB2
23..20  type: PING / PONG / WAKE
19..12  AP generation low 8 bits
11..0   sequence 0..4095
```

测试采用单 outstanding ping-pong：logical CPU0 发送 `PING(n)`，logical CPU1 IRQ 收到后立即
回送 `PONG(n)`；logical CPU0 收到对应 PONG 后才发送下一序号。这避免以队列深度或并发 flood
掩盖丢包，并让两个方向都得到精确计数。

共享诊断至少记录：

- 两方向 tx/rx；
- last tx/rx sequence；
- duplicate/lost/send failure；
- 两个 logical CPU 的 IRQ count 和 WFI wake count；
- requested/completed count、test runs、last command；
- stale/spurious command 和最后 error。

## 4. 生命周期边界

- AP primary 在 `up_cpu_start(1)` 释放 CPU2 前初始化 IPI wrapper、SDK mailbox 和 NuttX IRQ bridge。
- CPU2 完成 N8-B1 的 ID/VTOR/MSP/IDLE-stack 验证后，只初始化本核 mailbox IRQ，随后
  `cpsie i` 并在 `WFI` 中 park。
- `apctl stop/restart/cycle` 请求 CPU2 停止时，logical CPU0 发送独立 `WAKE` IPI，使 CPU2 从
  WFI 返回并处理既有 STOP command；超时仍由既有 reset-hold 路径收口。
- AP-UP `configs/ap_up/` 和 N8-A freestanding probe 不修改。

## 5. 手动入口

CP 侧新增：

```text
apctl ipitest [count] [timeout_ms]
```

默认 count 为 100。命令继续通过既有 CP→AP control wrapper 和 boot-state/raw lifecycle doorbell
发起；AP 内部 IPI 数据面则只走 SDK mailbox wrapper。

`apctl status` 新增三行 IPI 证据：

```text
AP IPI state=... error=... generation=... requested=... completed=... runs=...
IPI 0->1 tx/rx/pending=.../.../... seq=.../... dup/lost/fail=.../.../...
IPI 1->0 tx/rx/pending=.../.../... seq=.../... dup/lost/fail=.../.../...
IPI irq/wake cpu0=.../... cpu1=.../... stale/spurious=.../...
```

## 6. 本轮执行边界

按用户要求：

- 由 Claude 修改 team overlay 代码和文档；
- 由用户完成编译、下载和真实板卡验证；
- 本轮未执行 Git 操作；
- 本轮未执行 Git/static verifier 等静态验收；
- 验证以板端实测为准，覆盖双向 IPI、CPU2 mailbox IRQ/WFI wake 和完整生命周期回归；
- LittleFS 不纳入 factory image 的本 Stage 门禁。

## 7. 板测门禁与实测结果

首次下载后：

```text
apctl status
apctl ipitest 100 3000
apctl status
```

必须满足：

```text
AP state=READY error=0
CPU2 state=SECONDARY_READY error=0
CPU2 ready=1 online=00000001 calls=0
IPI state=PASSED error=0 requested=100 completed=100
0->1 tx=100 rx=100 pending=0 duplicate=0 lost=0 fail=0
1->0 tx=100 rx=100 pending=0 duplicate=0 lost=0 fail=0
CPU2 irq >= 100
CPU2 WFI wake >= 100
无 CPU0/CPU2 HardFault、NMI 或 WDT restart
```

随后执行生命周期回归：

```text
apctl restart 3000
apctl ipitest 100 3000
apctl stop 3000
apctl start 3000
apctl ipitest 100 3000
apctl cycle 3 3000
apctl start 3000
apctl ipitest 100 3000
apctl status
```

每个新 generation 的 IPI state 必须重新初始化，三次测试均独立闭合；最终 AP 保持 READY。

2026-07-29 实测结果全部满足门禁：

- `apctl ipitest 1 1000` 连续两次通过，同一 generation 的 `runs=1→2`，方向计数每轮独立保持 `1/1/0`；
- `apctl ipitest 100 3000` 连续两次通过，`runs=3→4`，两方向每轮均为 `tx/rx/pending=100/100/0`；
- 两方向 `duplicate/lost/send failure`、全局 `stale/spurious` 始终为 0；
- CPU2 heartbeat 在两轮 100 次测试中 `3→103→203`，证明每次 mailbox IRQ 均使 CPU2 从 WFI 返回；
- restart 后 generation 2 的 IPI state 从零初始化，首次测试 `runs=1`、heartbeat `1→101`；
- stop 后测试 tx/rx 保持 `100/100`，独立 WAKE IPI 使 CPU2 `irq/wake=101/101`，随后 heartbeat 保持不变；
- stop/start 后 generation 3 首次 100 次测试通过；
- `apctl cycle 3 3000` 从 READY 入口完成 generation `1→2→3→4` 三轮完整 start/stop，最终 STOPPED；
- 再次 start 到 generation 5 后，100 次测试通过，CPU2 heartbeat `1→101`，最终 AP 保持 READY；
- 全程 `online=00000001`、`calls=0`，无 CPU0、AP primary 或 CPU2 HardFault/NMI/WDT restart。

## 8. 当前进展

- N8-B1 板端证据已完整闭环；
- SDK mailbox public API、AP SMP cross-core source、FIFO/channel 路由、source-63 IRQ 路径已重新核对；
- 已确定并实测继续使用 SDK cross-core mailbox wrapper，而不是新增寄存器级 IPI 实现；
- 已新增独立 0x80-byte shared IPI state、generation/sequence command 编码和双向计数；
- 已新增 `bk7258_ap_ipi.c`，使用 `bk_mailbox_cc_init()`、`bk_mailbox_cc_init_on_current_core()`、`bk_mailbox_master_send()` 和 `crosscore_mb_rx_isr()`；
- 已让 AP-SMP 复用 team-owned SDK IRQ bridge，并通过 AP linker `EXTERN(bk_int_isr_register)` 保持 NuttX IRQ ownership；
- CPU2 mailbox vector slot 79 已接入 `exception_common`，其余 slot 继续 fail-closed；CPU2 IPI 配置下进入 interrupt-enabled WFI park；
- CPU2 first mailbox IRQ 的 NOCP 已定位为进入 `exception_common` 后执行 `vmsr fpscr`，而 CPU2 本地 CPACR 尚未启用；补齐 CPU2 CPACR/FPCCR 初始化后板测通过；
- 已加入单 outstanding PING/PONG self-test、每轮 CPU2 heartbeat/WFI 返回门禁和 STOP wake IPI；
- CP control wrapper 与 `apctl ipitest [count] [timeout_ms]`、IPI status 输出已落地；
- `configs/ap_smp/defconfig` 已启用 `CONFIG_BK7258_AP_IPI=y`，dual-image builder 默认 AP 配置已切到 `ap_smp`；显式 `AP_CONFIG_NAME=ap_up` 仍保留历史回归；
- `apctl cycle` 已在进入循环前归一化为 STOPPED，使 READY 入口可执行完整的三轮 start/stop；
- 当前为 `board-verified`：单次、100 次、restart、stop/start、三轮 cycle 和最终恢复均通过；按用户要求未执行 Git 操作或静态 verifier。

## 9. 用户首次编译阻塞与精准修复

日期：2026-07-29

用户首次编译在 CPU2 vector table 停止：

```text
chip/ap/bk7258_ap_smp.c:587:27: error: 'exception_common' undeclared here
  [BK7258_IRQ_MAILBOX] = &exception_common
```

根因是 `exception_common` 是 common ARM 汇编入口，不由 `arm_internal.h` 在该 C translation unit 中声明。
既有 `bk7258_ap_vectors.c` 已使用明确的 `extern void exception_common(void);`；N8-B2 在
`bk7258_ap_smp.c` 新增 CPU2 mailbox vector slot 时漏加了相同声明。精准修复只需补同一 prototype，
不改变 vector 地址、handler 或 IRQ 语义。

同时重新收口 SDK IRQ bridge 的目录职责：它最初只服务 CP，因此历史文件位于 `chip/cp/`；N8-B2
直接从 AP-SMP source group 复用该 CP 路径虽然功能上可行，但目录语义已经不再准确。bridge 本体
只实现 SDK source 0..63 → NuttX IRQ 16..79、callback dispatch 和生命周期序列化，CP/AP 的角色差异
由 Kconfig/source selection 和测试宏控制，因此应移动到 `chip/common/`，而不是复制成两份容易漂移的
AP/CP 实现。此次同步调整 Make/CMake 路径；IRQ 行为不变。

已完成精准修复：

- `bk7258_ap_smp.c` 增加与既有 AP vector 文件一致的 `extern void exception_common(void);`；
- `bk7258_sdk_irq.c` 从 `chip/cp/` 移到 `chip/common/`；
- classic Make 继续按 CP/AP 各自 gate 选择同一个 common source；
- CMake 的 CP 与 AP 路径同步改为 `common/bk7258_sdk_irq.c`；
- CP Kconfig help 明确它只是 CP activation option，AP 由 `BK7258_AP_IPI` 选择同一实现。

修复后仍由用户重新编译；Claude 未执行编译、Git 或静态 verifier。

## 10. 用户第二次编译阻塞与精准修复

日期：2026-07-29

第二次编译继续到 `bk7258_sdk_stubs.c` 后停止：

```text
chip/common/bk7258_sdk_stubs.c:39:6: error: conflicting types for 'shell_assert_out'
```

N8-B2 为 weak `crosscore_mb_rx_isr(mailbox_data_t *)` 引入 `driver/mailbox_types.h`。该 SDK header
链进一步进入 `components/log.h` → `components/shell_task.h`，从而让此前未显式可见的正式声明进入
当前 translation unit：

```c
int shell_assert_out(bool bContinue, char *format, ...);
```

历史 stub 使用的是不匹配的旧声明：

```c
void shell_assert_out(const char *fmt, ...);
```

此前因为没有包含 SDK 声明而未在编译期暴露。已将 stub 精确改为 SDK ABI：

```c
int shell_assert_out(bool bContinue, char *format, ...)
```

空实现忽略两个固定参数并返回 0，同时显式包含 `<stdbool.h>`。该修复不改变 mailbox/IPI 数据面，
也不新增寄存器操作，仍属于 wrapper 依赖的 SDK symbol stub 收口。修复后等待用户再次编译；
Claude 未执行编译、Git 或静态 verifier。

## 11. 用户首次板测阻塞：CPU2 first IRQ NOCP

首次 `apctl ipitest 100 3000` 在第一个 PING 后失败：

```text
CPU2 fault exception=3 HFSR/CFSR=40000000/00080000
PC/LR/xPSR=022014c8/fffffff9/0900004f
IPI 0->1 tx/rx/pending=1/0/1
IPI irq/wake cpu0=0/0 cpu1=0/0
```

证据链完整指向 CPU2 exception entry 的 FPU 初始化缺口：

- `CFSR=0x00080000` 是 UsageFault `NOCP`；
- `xPSR & 0x1ff = 79`，证明 CPU2 已进入 source-63 对应的 NuttX mailbox IRQ 79；
- `PC=0x022014c8` 位于 `exception_common` 的 `vmsr fpscr, r0`；
- fault 发生在 `arm_doirq()` 和 SDK callback 之前，因此测试发送计数已为 1，而 IRQ/wake 仍为 0；
- AP primary 启动路径已初始化 CPACR/FPCCR，但 CPU2 secondary bootstrap 当时没有执行对应的 per-core 初始化。

精准修复是在 CPU2 开中断前镜像 AP primary 的序列：先关闭 CP10/CP11，清除
`FPCCR.ASPEN/LSPEN/LSPENS`，再授予 CP10/CP11 full access，并使用 DSB/ISB 保证生效。
修复没有改变 mailbox wrapper、MBOX0 FIFO、source-63 IRQ 或 PING/PONG 协议。

同轮还修复了两个板测入口问题：

- dual-image builder 默认 AP 配置原为 `ap_up`，会生成 READY(2) legacy probe 且没有 IPI state；默认已改为 `ap_smp`，同时保留显式 `AP_CONFIG_NAME=ap_up` 回归入口；
- `apctl cycle` 原本从 `bk7258_ap_start()` 开始，在 READY 入口返回 `-EBUSY`；现在先 stop 归一化，再执行每轮完整 start/stop。

## 12. 最终板端验证结果

2026-07-29 用户在真实 T5-AI 板卡完成以下闭环：

- `apitest 1` 连续两次通过；
- 同一 generation 的 `apitest 100` 连续两次通过；
- restart 后新 generation 首次 `apitest 100` 通过；
- 独立 stop/status/start 后新 generation 首次 `apitest 100` 通过；
- 修复后的 `cycle 3` 从 READY 完成 generation `1→2→3→4`，三轮启动均到达 `SECONDARY_READY`，最终 STOPPED；
- 最终 start 到 generation 5，`apitest 100` 通过，CPU2 heartbeat `1→101`，最终 AP READY；
- 所有已测试 generation 中，两方向 tx/rx/sequence 精确闭合，pending、duplicate、lost、send failure、stale、spurious 全为 0；
- STOP 独立 WAKE IPI 只增加 CPU2 IRQ/wake，不污染测试 tx/rx；
- 全程 `online=00000001`、`calls=0`，无 CPU0、AP primary 或 CPU2 HardFault/NMI/WDT restart。

因此 N8-B2 状态收口为 `board-verified`。该结论只覆盖 AP 内部双向 IPI 和生命周期；CPU2
`scheduler online_mask=0x3`、普通 task、SMP call delivery、affinity/migration/spinlock 压力、
RPTUN/RPMsg、Wi-Fi 和 BLE 仍未启用或验证。
