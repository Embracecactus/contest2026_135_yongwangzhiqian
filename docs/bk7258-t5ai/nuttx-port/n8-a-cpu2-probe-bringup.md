# BK7258 N8-A — 物理 CPU2 Probe bring-up

日期：2026-07-29

状态：`board-verified`

代码锚点：`7ffd05b feat(bk7258): bring up physical CPU2 probe`

收口分支：`feat/bk7258-cpu2-probe`

## 1. 本 Stage 边界

N8-A 在已经板测通过的 CPU1/AP 单核 NuttX 基线上加入物理 CPU2 的 freestanding probe：

```text
物理 CPU0 -> CP logical CPU0 -> NuttX + NSH
物理 CPU1 -> AP logical CPU0 -> NuttX UP primary
物理 CPU2 -> AP logical CPU1 -> freestanding probe
```

本 Stage 只验证 CPU2 的独立启动、运行状态和 AP 生命周期联动，不把 CPU2 加入 NuttX scheduler。

明确未实现：

- `CONFIG_SMP=y` 或双核 NuttX scheduler；
- NuttX secondary CPU bootstrap / online mask；
- 双向 IPI、task affinity、task migration 或 SMP locking；
- RPTUN/RPMsg、Wi-Fi、BLE 或复杂电源管理。

因此本文件中的 `board-verified` 只适用于 **CPU1 NuttX UP + CPU2 freestanding probe**，不得解释为 AP SMP 已完成。

## 2. 已收口实现

代码提交 `7ffd05b` 完成以下最小链路：

- AP 镜像在 `0x02200200` 放置 CPU2 secondary vector；
- AP logical CPU0 启动物理 CPU2，并把 AP-local logical ID 固定为 1、physical ID 固定为 2；
- CPU2 使用 AP SRAM 顶部独立 1 KiB probe stack；
- CPU2 向共享页发布 generation、state、error、vector、VTOR、MSP、heartbeat 和 fault 状态；
- CPU0 的 AP start/stop/restart 控制同时持有或释放 CPU2；
- AP stop 先停止 CPU2 probe，再完成 AP 自身停止；
- `apctl status` 输出 CPU2 的共享状态和控制寄存器诊断；
- `configs/ap_up/` 保持为 NuttX UP 配置，没有启用 SMP。

## 3. UART 板测原始输出

采集时间：2026-07-29 05:41:12 UTC

以下内容按本次真实 T5-AI 板卡 UART 会话原样保存；末尾操作指令不属于 UART 输出，未收入代码块。

```text
 apctl status
apctl [4:100]
nsh> AP state=READY(2) error=0 generation=1 heartbeat=493
AP core local=0 physical=1 VTOR(init/run)=02200000/28050800 MSP(init/run)=28050800/28050800
AP clock=320000000 SysTick ctrl/load/current=00000007/0030d3ff/00239687
AP heap=28052488..2809ebfc test=280536e8 doorbells cp/ap=0/1
CPU2 state=READY(2) error=0 generation=1 heartbeat=496
CPU2 core local=1 physical=2 vector=02200200 VTOR=02200200 MSP(init/run)=2809f000/2809eff8
CPU2 control=00000000 SYS(before/after)=00000010/02200231

nsh> apctl status
apctl [5:100]
nsh> AP state=READY(2) error=0 generation=1 heartbeat=564
AP core local=0 physical=1 VTOR(init/run)=02200000/28050800 MSP(init/run)=28050800/28050800
AP clock=320000000 SysTick ctrl/load/current=00000007/0030d3ff/00239687
AP heap=28052488..2809ebfc test=280536e8 doorbells cp/ap=0/1
CPU2 state=READY(2) error=0 generation=1 heartbeat=567
CPU2 core local=1 physical=2 vector=02200200 VTOR=02200200 MSP(init/run)=2809f000/2809eff8
CPU2 control=00000000 SYS(before/after)=00000010/02200231

nsh>
nsh>
nsh> apctl status
apctl [6:100]
nsh> AP state=READY(2) error=0 generation=1 heartbeat=606
AP core local=0 physical=1 VTOR(init/run)=02200000/28050800 MSP(init/run)=28050800/28050800
AP clock=320000000 SysTick ctrl/load/current=00000007/0030d3ff/00239687
AP heap=28052488..2809ebfc test=280536e8 doorbells cp/ap=0/1
CPU2 state=READY(2) error=0 generation=1 heartbeat=609
CPU2 core local=1 physical=2 vector=02200200 VTOR=02200200 MSP(init/run)=2809f000/2809eff8
CPU2 control=00000000 SYS(before/after)=00000010/02200231

nsh> apctl restart
apctl [7:100]
nsh> AP state=READY(2) error=0 generation=2 heartbeat=1
AP core local=0 physical=1 VTOR(init/run)=02200000/28050800 MSP(init/run)=28050800/28050800
AP clock=320000000 SysTick ctrl/load/current=00000007/0030d3ff/000f1f03
AP heap=28052488..2809ebfc test=280536e8 doorbells cp/ap=0/1
CPU2 state=READY(2) error=0 generation=2 heartbeat=4
CPU2 core local=1 physical=2 vector=02200200 VTOR=02200200 MSP(init/run)=2809f000/2809eff8
CPU2 control=00000000 SYS(before/after)=02200230/02200231

nsh> apctl status
apctl [8:100]
nsh> AP state=READY(2) error=0 generation=2 heartbeat=93
AP core local=0 physical=1 VTOR(init/run)=02200000/28050800 MSP(init/run)=28050800/28050800
AP clock=320000000 SysTick ctrl/load/current=00000007/0030d3ff/000f1f03
AP heap=28052488..2809ebfc test=280536e8 doorbells cp/ap=0/1
CPU2 state=READY(2) error=0 generation=2 heartbeat=96
CPU2 core local=1 physical=2 vector=02200200 VTOR=02200200 MSP(init/run)=2809f000/2809eff8
CPU2 control=00000000 SYS(before/after)=02200230/02200231
```

## 4. 板端证据解读

| 门禁 | 板端结果 |
|---|---|
| CPU2 身份 | `local=1 physical=2` |
| CPU2 secondary vector | `vector=0x02200200` |
| CPU2 runtime VTOR | `VTOR=0x02200200` |
| CPU2 stack | initial MSP `0x2809f000`，runtime MSP `0x2809eff8`，落在预留 probe stack 内 |
| CPU1/AP 健康度 | generation 1 中 heartbeat `493 -> 564 -> 606` |
| CPU2 健康度 | generation 1 中 heartbeat `496 -> 567 -> 609` |
| restart 联动 | `apctl restart` 后 AP 与 CPU2 generation 同时从 1 变为 2 |
| restart 后存活 | generation 2 中 AP heartbeat `1 -> 93`，CPU2 heartbeat `4 -> 96` |
| stop/start/restart 生命周期 | 同轮板测确认 CPU2 随 AP 完成 stop/start/restart；上方原始输出直接保存 restart 及 generation rollover 证据 |
| CPU0 task-exit 回归 | 连续 `apctl status` 和空提示符均正常，未出现 `HF`；已修复的 CPU0 task-exit HardFault 未回归 |

控制寄存器在首次启动和 restart 后分别记录：

```text
SYS(before/after)=00000010/02200231
SYS(before/after)=02200230/02200231
```

这与 CPU2 被 hold、设置 secondary vector 后重新 release 的生命周期一致。

## 5. N8-A 最终结论

N8-A 状态正式标记为：

```text
board-verified
```

已建立的稳定回退基线为：

```text
configs/ap_up/
  physical CPU1 -> AP logical CPU0 -> NuttX UP
  physical CPU2 -> AP logical CPU1 -> freestanding probe
```

后续 AP SMP 必须从独立分支和 `configs/ap_smp/` 开始，不能覆盖该 `ap_up` 回退基线，也不能把 SMP 改动混入本次 N8-A PR。
