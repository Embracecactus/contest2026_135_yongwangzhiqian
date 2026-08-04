> **最后更新**：2026-08-04（Asia/Shanghai）
> **权威来源**：[CURRENT](../../../progress/CURRENT.md)、[N15 physical evidence](../../../progress/verification/2026-08-04-n15-physical-symmetric-lifecycle.md)、[ROADMAP](../../../progress/ROADMAP.md)
> **证据边界**：这是本教程唯一动态快照。阶段细节以verification记录为准；本页的完整掉电结论已有恢复后的UART状态支持。

# 11 当前状态与下一步

## 1. 一句话结论

Boot/N1～N14已完成各自批准范围内的板端验证；N15的连续A/B布局、双bank metadata、一轮完整physical A→B→A trial/confirm，以及confirmed A后的完整断电恢复均已通过。批准的N15最小实板范围已经闭环。

## 2. 当前仓库和不可越过的边界

| 项 | 当前值 |
|---|---|
| branch | `feat/bk7258-n15-ota` |
| 已发布基线 | `68b943615838eea6a79b25431183c154fee73727`；本轮结果尚未提交 |
| 唯一active SDK | official Beken v3.1.1.9 |
| official NuttX/apps/SDK | read-only；正式适配只放team-owned wrapper、board、app、script和doc |
| normal profile | `cp_nsh_psram + ap_smp_psram`，Boot/CP OTA gates全部为0 |
| validation profile | `cp_nsh_ota + ap_smp_psram`，仅显式授权验证使用 |
| deployed layout | ADR-004连续CP/AP A/B；LittleFS raw `0x600000..0x700000` |

共享host build tree和开发板都已恢复normal profile。恢复下载只写Boot、CP A和AP A三个segment；B、双metadata、LittleFS、`usr_config`、reserved和校准尾区均不在写集合中。板端复验AP/CPU2/RPTUN、LittleFS探针和PSRAM通过，`bkota`命令不存在。

当前worktree还包含其他任务留下的dirty/untracked文件。后续提交必须按路径审查和选择，不能把Windows debug、QEMU、日志等内容顺手带入。

## 3. 从最开始到现在

| 阶段 | 给小白的含义 | 当前证据 |
|---|---|---|
| Boot/N1～N6 | 让芯片从自研Tier-1进入CP NuttX，具备UART/NSH/时钟/Flash/LittleFS/SDK wrapper/IRQ/GPIO | board-verified |
| N7～N8 | 启动AP物理CPU1和CPU2，把AP变成NuttX SMP系统，并验证IPI、调度、迁移、定时唤醒 | board-verified |
| N9～N11 | 建立CP↔AP RPTUN/RPMsg、健康监督和RPMsgFS，使两个NuttX系统可通信和共享CP文件服务 | board-verified |
| N12～N13 | CP运行official Bluetooth Controller，AP运行stock Host，并完成BLE GAP/GATT和压力回归 | board-verified |
| N14 | 启用16 MiB PSRAM、CP/AP私有heap和SDK deferred timer | board-verified |
| N15-M | 一次迁移到连续A/B布局，清空/迁移LittleFS并重跑保留功能 | board-verified |
| N15 host | package、inactive staging、selector、trial/rollback、publish、health、双bank fault matrix | host/source/ELF/dry-run-verified |
| N15 physical | generation 314 A→B confirmed B，再由generation 315 B→A confirmed A，随后完整掉电恢复 | board-verified |

这里的“physical board-verified”只覆盖实际执行过的双向stage、trial、服务回归和confirm。physical run没有专门触发未confirm rollback；rollback由host reset-boundary/fault模型验证，不能写成实板rollback已跑。

## 4. 已完成的双向实板生命周期

```text
confirmed A
  -> generation 314写inactive B（2576384 bytes，全量read-back/SHA通过）
  -> publish metadata bank 0
  -> trial B -> 保留服务PASS -> confirmed B
  -> generation 315写inactive A（2576384 bytes，全量read-back/SHA通过）
  -> publish metadata bank 1
  -> trial A -> 保留服务PASS -> confirmed A
  -> COM7 RTS -> 仍为generation 315 confirmed A
  -> 同时移除USB/J-Link供电 -> 重连 -> 仍为generation 315 confirmed A
```

两个trial槽都验证了：

- AP SMP和physical CPU2 online；
- RPTUN connected；
- LittleFS、PSRAM和SDK timer；
- RPMsg 6×5、RPMsgFS 4×1；
- Bluetooth controller info。

完整掉电恢复后最后观测状态是generation 315、bank 1、`CONFIRMED_A(7)`，stable/active均为A，secondary mapping关闭，stage/metadata runtime gates为0，AP/CPU2/RPTUN健康。恢复前没有执行reset、J-Link Commander或Flash命令。完整原始日志和artifact hash只在[N15 physical evidence](../../../progress/verification/2026-08-04-n15-physical-symmetric-lifecycle.md)维护，避免在多处复制后失真。

## 5. 这轮实板测试修复了什么

1. Tier-1同时初始化APB_WDT和AON_WDT，却只喂前者：team-owned Boot现在同时feed两路，verifier防回退。
2. J-Link一次写大文件、一个进程连续写多个块不稳定：PSRAM loader改为64 KiB chunk，每块新进程、`noreset`并立即`verifybin`。
3. publish需要重新hash base/candidate，实测约33.8～37.9秒：campaign timeout由10秒改为180秒；旧10秒尝试以`mutation=0`失败关闭。
4. 最长`bkota stage`命令超过NSH默认参数数：validation config设置`CONFIG_NSH_MAXARGUMENTS=10`。
5. 实际console是460800 8N1；115200乱码不再作为崩溃证据。

这些修复都在项目自有文件中；official NuttX、apps、SDK源码和静态库没有被修改。

## 6. 构建收口

validation和normal两个完整构建都已通过official v3.1.1.9 checksum及host/source/ELF验证。最后normal ELF SHA-256为：

- Boot：`04e193c0db43f8c8ee5d361f3e91c8036aa5d5f4f78871eff2bd611e2d43a793`；
- CP：`76f17d1a68f5ffb3b89c249c2a8a2a16232c3c9399cb80cd0c6fc78cbbc4c272`；
- AP：`6b8e102870e82d971a028cc560f18e67fc9a10d429fd33a555485fbb9086e5cc`。

## 7. 下一步最小计划

1. 只审查并提交本轮N15代码、证据、CURRENT/INDEX和教程同步；不混入其他dirty文件。
2. 由owner另行选择下一MAIN Stage；不要自动开始legacy SDK验证或新的破坏性板端流程。

## 8. 仍然明确延后

- Flash erase/program模拟脉冲中随机analog brownout资格认证；
- publisher signature、key provisioning和hardware anti-rollback；
- network OTA transport和bootloader self-update；
- legacy SDK验证；
- Wi-Fi数据面、BLE security/bond/multi-peer；
- product级wear、温压、长时间SLA和QEMU。

以上都不属于已闭环的N15最小实板范围，不能因A→B→A和完整掉电恢复通过就被写成已完成。
