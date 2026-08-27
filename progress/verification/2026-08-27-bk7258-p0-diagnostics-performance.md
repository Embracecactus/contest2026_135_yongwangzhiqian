# BK7258 T5-Board P0 调试与性能实板验证

> 本文的 generation 145 是 SDK OPP 320M、CP/CPU0 160 MHz 的历史实测，数据保留
> 不改写。当前性能 profile 已按 v3.1.1.9 正式表修正为 OPP 240M、CPU0 240 MHz，
> generation 146 的新结果已另建 verification 记录；OPP 语义见
> [`docs/chips/bk7258/sdk-clock-operating-points.md`](../../docs/chips/bk7258/sdk-clock-operating-points.md)。
>
> 本文证据范围截至 generation 146。后续 generation 147～149 的 CP heap 根因、
> 当前代 xTS、签名全量下载和板端结果见
> [generation 149 xTS 收口记录](2026-08-27-bk7258-p0-xts-completion.md)，不在本文重复展开。

- 日期/时区：2026-08-27，Asia/Shanghai
- 分支：`feat/bk7258-p0-diagnostics`
- 集成起点：`e9ba46119bac46dfc4903a9ab2b4ff9891fa46c9`
- 目标板：T5-Board V1.0.2
- 下载与控制台：COM3，BK Loader 2.1.11.15 / UART0 115200 8N1
- Agent layout identity：`bk7258-2b86faab14dca06e`
- 总结：诊断 generation 143、历史性能 generation 145 和当前 240 MHz 性能
  generation 146 的签名全量下载、启动和目标功能均通过；两代性能镜像都完成
  CoreMark/Ramspeed/Whetstone 各 10 次。按本文截至 generation 146 的范围，完整
  当前代 xTS、其他 benchmark 与 12 小时 soak 未完成。generation 146 完整证据见
  [独立验证记录](2026-08-27-bk7258-sdk-clock-240m-validation.md)。

适配与复现方法见
[`docs/bk7258-t5ai/p0-diagnostics-performance.md`](../../docs/bk7258-t5ai/p0-diagnostics-performance.md)。

## 1. 本轮实现

### 1.1 `t5_board_cp_xts`

保留 ROMFS `rc.sysinit`/`rcS` 双脚本、final-init、AP/RPTUN/Wi-Fi、WDT 与 Agent 基线，
新增或补齐：

- Backtrace、ARM unwinder、dumpstack、Allsyms；
- IRQ monitor、critmon、system-clock CPU load 和负载发生器；
- Trace/Note RAM buffer；
- memstress；
- `DEBUG_FEATURES`、`SIG_DEFAULT` 与 `NSH_MAXARGUMENTS=12`。

Allsyms 表由板级 linker script 固定到 XIP FLASH，避免占满 SRAM；运行期可用 heap 从
约 2.6 KiB 恢复到约 22 KiB。`DEBUG_FEATURES` 使 `_alert` 的符号化回溯真正输出，
`SIG_DEFAULT` 使 `kill -9` 能终止后台压力任务。

### 1.2 `t5_board_cp_perf`

最初新增性能 profile 时选择 SDK `BK7258_CLOCK_320M` 档，CP CPU0 实际只有
160 MHz。后续按 SDK 正式 OPP 表改为 `BK7258_CLOCK_240M`，CPU0/AP/Bus 均为
240 MHz；`-O3`、CoreMark、Ramspeed、Whetstone 和 SDK IRQ bridge 保留。AP
autostart、Wi-Fi、RPTUN、WDT、Trace、调度监控、Backtrace、Allsyms 和 memstress
均关闭。

### 1.3 共用代码修复

- WDT pretimeout 也编译其依赖的 OTA flash helper，Make/CMake 条件保持一致；
- 无 WDT profile 的 fault/NMI fallback 使用 `up_systemreset()`，不伪造 WDT cause；
- WDT 日志参数使用与格式匹配的显式类型转换。

## 2. 信任代和候选处置

| Generation | Version | 用途 | 处置 |
|---:|---|---|---|
| 134～142 | 诊断中间版 | 定位 Allsyms SRAM、Backtrace 输出、SIGKILL 与 NSH 参数等问题 | 仅作定位，不作为最终证据；每次发生全量下载时均使用独立新 BL1/MCUboot 密钥 |
| 143 | `1.87.9+143` | 最终诊断/xTS 镜像 | 接受，诊断子集和重启恢复通过 |
| 144 | `1.88.0+144` | 第一版性能镜像 | 拒绝：UART TX/NSH 正常但 RX 不响应；未计入 benchmark |
| 145 | `1.88.1+145` | 修复 SDK IRQ bridge 后的最终性能镜像 | 接受，30 个独立 benchmark session 与重启通过 |
| 146 | `1.88.2+146` | 修正 SDK OPP 语义后的当前 240 MHz 性能镜像 | 接受，新密钥全量下载、稳定回读、冷启动和 30 个独立 benchmark session 通过 |

每个发生全量下载的 generation 均重新生成两套不同的 P-256 密钥对。本文只记录公开
SPKI 指纹；私钥未提交。证据确认后，各代 `fresh-keys` 临时目录均已精确删除；
generation 146 私钥目录也在验收后确认不存在。签名包、operator image、串口证据和
公开指纹保留。

## 3. 全量下载边界与持久数据

权威分区 CSV：
`boards/bk7258/common/partitions/bk7258/bk7258_ab_agent_onchip_persistent.csv`。

| 区域 | 范围 | 本轮规则/校验 |
|---|---|---|
| `usr_config` | `[0x4fc000,0x50a000)` | 必须保留；SHA-256 `5e3d970426c42f666f017097cb09ab036e0e3d9a8bc6f4c10f41f28905ea8114` |
| Agent persistent | `[0x561000,0x7fa000)`，`0x299000` bytes | 必须整体保留；SHA-256 `ef3393a63494d5bb6883ff8455d2803ef94e8d9379c48bc637ad4a0c9f1a347f` |
| immutable/calibration tail | `[0x7fa000,0x800000)` | 不进入 operator image，不读写 |

两张最终镜像都从同一份已验证基底 materialize：

- base：`diagnostics-agent-v1.87.3+137-postread-1500k.bin`；
- 长度：`0x7fa000`；
- SHA-256：`fb7497407ede192848d34f85532e526b980ffac32aad222efb6a0227469e6552`。

package verify、public trust verify、flash-contract verify、materialize 以及两个保留区的
逐字节校验均为 PASS。每次只向 BK Loader 提交一个 `0x7fa000` 文件，起始地址
`0x000000`，不执行 chip erase，也未修改 calibration、OTP/eFuse、lifecycle 或
debug-lock。

## 4. 诊断 generation 143

### 4.1 产物身份

| 字段 | 值 |
|---|---|
| version / MCUboot / BL1 counter | `1.87.9+143` / 143 / 143 |
| BL1 public SPKI SHA-256 | `3e131a2f5850325f750a7369574c288170feb424d3f50a855c5df4fec5fde806` |
| MCUboot public SPKI SHA-256 | `f400de4cfa95c1441d25c0bb7c1e4e28d17b98f54a6fda091821d97a12e3b99b` |
| CP build identity | `bk7258-role-3a2caaf5accf0024` |
| CP resolved config SHA-256 | `f3824ad1f04e2bfb2314cb516b50d0f301e1ab813189b3ea46c15e141b7e7c53` |
| CP ELF SHA-256 | `6aea7e0c91138e248e8d3ccf7aad69598f41b209c4370f281c40b547a4b72106` |
| CP bin | 1,296,300 bytes; `f478af79fcdc8ede6cdc16ceb363ac76ad28f9b2abe60e68a58e6f57aecb7a04` |
| AP build identity | `bk7258-role-d369490f8b418bc4` |
| AP resolved config SHA-256 | `3d40200a0fecbb540bf9d1b0373b403c74a1b879714db7c659bbf6995cff7ad7` |
| signed package SHA-256 | `1d6865a8f2a32a82a4ebfb9135f6544af5e21d4af5e9ba9be1d6b9cd2e572ced` |
| operator image | `0x7fa000` bytes; `ed90e260b0aaa0f0a3588dbe0fad21b5505d08eee0240dfe8d55fe043e461b6c` |

### 4.2 下载与启动

COM3 单文件写入 `[0x000000,0x7fa000)`。BK Loader 输出同时包含：

```text
WriteFlash ->pass
Writing Flash OK
All Finished Successfully
```

WSL 调用 Windows loader 的进程最终返回 1，是该工具已有的退出码异常；由于写入阶段
三条成功文本、后续签名启动和功能门禁全部通过，本轮按成功处理，未因退出码重刷。

冷启动与压力后重启均出现 BL1 primary、BL2 AP OK、`rc.sysinit`、final-init、`rcS`、
AI Agent ready，且没有 Panic/Fault/assert。

| 串口证据 | SHA-256 |
|---|---|
| 冷启动 | `9a25b091cc69e0416855fc523878c3cd42ee1f964f5052c592cda9f316f81464` |
| 压力后重启 | `08513081ddd2807f8a18e95db76bb847aa58e74a165ec6105feb2d9919d7ab73` |

### 4.3 诊断结果

| 项目 | 命令/时序 | 结果 |
|---|---|---|
| 基线 | `help`、`ps`、`free`、`irqinfo`、`dumpstack`、`dumpstack 1` | PASS；当前任务与 PID 1 均符号化，出现 `up_backtrace`、`sched_dumpstack`、`dumpstack_main`、`nsh_main`、`work_thread` |
| IRQ | `irqinfo` | PASS；IRQ 11/15/20/70/79 的有效计数可见 |
| critmon | start → workload → stats → stop | PASS（可观测性）；daemon 启动、统计、停止均成功，未宣称阈值告警 |
| CPU load | `cpuload -p 50 &`，6 秒观察，`kill -9 24`，10 秒复核 | PASS；任务 49%，`/proc/cpuload` 56.5%，停止后 PID 消失并回落到 7.5% |
| memstress | `memstress -m 256 -n 16 -t 10000 -x 1 &`，运行 10 秒，`kill -9 25` | PASS；主 PID 25、pthread 26 均运行，无 error/Panic/Fault；停止 6 秒后均消失 |
| Trace | `trace start` → `hello` → `trace stop` → `trace dump` | PASS；55,920 bytes / 679 行，含 tracer header、hello、IRQ entry/exit |
| 恢复 | reset 后完整启动 | PASS；Agent 再次 ready |

诊断 session 哈希：

| Session | SHA-256 |
|---|---|
| baseline | `75ba494ebf9d610d08e339b42c12078c18c59a53f18e0d3cd375fc3aaf928cf9` |
| cpuload start | `721be1a62738f85ee2e181cbdfd480b67ae3fe9b537827350c053fee15528a4e` |
| cpuload stop | `8931d2d80b7cd61a580c41362dee3a65a0974baf7aba7336c1eecc624cb4cac3` |
| memstress start | `5a29f4a32df39b97f0bed80ac8f97fc19a22062d125fb8ee1f1b1b3478cb59c8` |
| memstress stop | `0d291b3a35ed1eebc31a668d2ee5782cc36936e62d1d2abf5cd547d5aca25b52` |
| critmon / Trace | `d0f9aafbb23d51520178ec89c587b0670c7a44cc9345f1593b51f79dcd13a130` |

## 5. 性能 generation 145

### 5.1 产物身份和低噪声门禁

| 字段 | 值 |
|---|---|
| version / MCUboot / BL1 counter | `1.88.1+145` / 145 / 145 |
| BL1 public SPKI SHA-256 | `8554fe52101d6c3340896065e7fca07ff88a52f3dbdc5bdd55aaeb082e81bbec` |
| MCUboot public SPKI SHA-256 | `0fe749d21a48269020302834370ce8d39cd2bc39da86e07a47230c121fd6487e` |
| CP build identity | `bk7258-role-a4e6346b8af30201` |
| CP resolved config SHA-256 | `e77adfae2f0e633bc591f08cefc1d32a172114911b446d3959ca348bd9ff5e5d` |
| CP ELF SHA-256 | `d04bc3ce8b8173fb472f45fa96d503c294f9f3c9e7f29d28d0b014a85e4c1c45` |
| CP bin | 215,044 bytes; `0710829f2202d7386038b48ff459752f7f94585d72be9f48c2c069a08884dc13` |
| AP build identity | `bk7258-role-d033a8748a3478c0` |
| AP resolved config SHA-256 | `3d40200a0fecbb540bf9d1b0373b403c74a1b879714db7c659bbf6995cff7ad7` |
| AP ELF SHA-256 | `f9a8af515b64158ebed1ef7688459750145ae74b39199fb84d415ee8729a1c1f` |
| AP bin SHA-256 | `4c1f3b7cf3e3ed4538c09691320ee441cbba717fcb33308758f84403e436b5a9` |
| signed package SHA-256 | `9b2b3356dc60940ab439b92a9f22324bc53f82f416571a66f94ec61fffa1ed53` |
| operator image | `0x7fa000` bytes; `4374ecaa8ee02fca48a812a5958c2d5efcf1be9c9dfa7fda29b30f00c31d2435` |

解析配置确认：IRQ bridge、`BK7258_CLOCK_320M`、`-O3` 和三个 benchmark 开启；
CPU load、Trace、Backtrace、Allsyms、RPTUN、WDT、网络和 AP autostart 关闭。最终 ELF：
`text=208900`、`data=6144`、`bss=10768`；`_sheap=0x28014210`、
`_eheap=0x2804f7fc`，静态 heap 约 243 KiB。

### 5.2 下载、启动和串口

与诊断代相同，COM3 只写一个 `[0x000000,0x7fa000)` 文件，不 chip erase，loader 三条
成功文本齐全。冷启动和 30 次 benchmark 后重启均出现 BL1 primary、BL2 AP OK 和
NuttShell，未出现 AP 产品启动日志、Panic、Fault 或 assert。

| 串口证据 | SHA-256 |
|---|---|
| 冷启动 | `db70a8cf425fc537e3a13883a17d31d9fad17b98598a8bc177a6d27a032eb23f` |
| `help` 交互 | `86b1cc94e680faf8b7a5f19ed2e406075cc0c0e70fe30134f8f64a3c3ebb424b` |
| benchmark 后重启 | `db70a8cf425fc537e3a13883a17d31d9fad17b98598a8bc177a6d27a032eb23f` |

## 6. CoreMark 10 次

命令：`coremark`；1 thread；10,000 iterations。10 次均输出 validation PASS。

| Run | CoreMark | Log SHA-256 |
|---:|---:|---|
| 1 | 374.251497 | `b959975d161093f7d9223e31483835f75f690a2c9188f5e290244010b5cc7e44` |
| 2 | 374.251497 | `1a1df5b2ab7f9855bfd2c58fbfe38b31b23eea2f52626070cd3c318edf15c9e2` |
| 3 | 374.391614 | `106aa97eb6a0dca254474a415c3ed062e62cca1d7204dde9d631a15ca0ff98c7` |
| 4 | 374.391614 | `5ddf3b626157ccef60739054a2d069226bb1e417f9ad0078fd3c802a36291265` |
| 5 | 374.391614 | `c38b0b5032b85d82a5c44e52e8539b9eeaa18e8d5af4485fe4494351e4c01dfe` |
| 6 | 374.251497 | `4eb05b8ea925ee36930b60aed9d978a01967f01da9f128dd45b50b86f479ff57` |
| 7 | 374.251497 | `ff33b98634763f06b7db9377cb7fcb34eec43a9c10f955e08d5052a2ef3a59a7` |
| 8 | 374.251497 | `a2cf3fe18013e049a8fe142eab39ed4a4a8f4818c5f40da19a84966e95d3adc0` |
| 9 | 374.391614 | `2cd7abb0483c4c0506506c350a5dab493d6a35937c4387e699e57956b97bd366` |
| 10 | 374.251497 | `6606dc8e3feef0f79bbde66c0a793b01ac7ff77f3e334b95868860ad6f4f2531` |

统计：mean 374.307544、median 374.251497、min 374.251497、max 374.391614、
population stddev 0.068643；以有效 160 MHz 计算 mean 2.339422 CoreMark/MHz。

## 7. Ramspeed 10 次

命令：`ramspeed -a -s 65536 -n 1000`；中断开启。每个 session 都完成全部 48 行输出；
下表记录官网基线使用的 64 KiB 行，顺序为 system memcpy / internal memcpy /
system memset / internal memset。

| Run | System memcpy | Internal memcpy | System memset | Internal memset | Log SHA-256 |
|---:|---:|---:|---:|---:|---|
| 1 | 103856.315 | 165947.561 | 155757.933 | 355251.618 | `bca5d8c31f0b0d50022d091b0602e70a05c47a7eca1c14e14ea2fd66354ae398` |
| 2 | 103860.528 | 165935.944 | 155765.514 | 355251.618 | `d80ccc42c270d155e7b50b4667ed0e09f23aeac83772af9eac13ee3a486f0738` |
| 3 | 103856.146 | 165936.374 | 155751.868 | 355249.646 | `48f85689f14daa04bd599e6df74fd2a1cda772073928b28f98ed5f8454a0b377` |
| 4 | 103856.146 | 165936.374 | 155752.247 | 355249.646 | `4c92ceab0c691f45c10a60f1eecbeac44f7747685c8e789855842766eac45bf3` |
| 5 | 103856.146 | 165947.130 | 155762.482 | 355251.618 | `dbcde7a052ef975a9c8ababaf2c5a3d5f95fb4f12f5eeeaf512ca50333a41a52` |
| 6 | 103856.146 | 165936.374 | 155751.868 | 355251.618 | `f864c4d016312cf8fb5d316a34a3112d40b5b9602b26d037a40919350efe536b` |
| 7 | 103856.146 | 165947.130 | 155761.344 | 355251.618 | `9ec4bab5ac78ac37b23d1dba399334b2a0c9ec2cb09a0d6502e049d3a7c54435` |
| 8 | 103861.371 | 165936.374 | 155761.724 | 355251.618 | `0110556df9e85d28ea15c570a52a36af1899dc068400a79780e18c4216c1f927` |
| 9 | 103861.371 | 165936.374 | 155763.619 | 355249.646 | `9d4b78cb501e6df86fc0a4a0fe4c905263fde88653065c30b8f5da22bf1e476e` |
| 10 | 103860.528 | 165936.374 | 155760.586 | 355251.618 | `b906de27806449da13a83ee895546b79e65e66e0cce7a02e98d258a4e3bd3022` |

| 统计 | System memcpy | Internal memcpy | System memset | Internal memset |
|---|---:|---:|---:|---:|
| mean | 103858.084 | 165939.601 | 155758.918 | 355251.026 |
| median | 103856.231 | 165936.374 | 155760.965 | 355251.618 |
| min | 103856.146 | 165935.944 | 155751.868 | 355249.646 |
| max | 103861.371 | 165947.561 | 155765.514 | 355251.618 |
| population stddev | 2.355 | 5.026 | 4.896 | 0.904 |

## 8. Whetstone 10 次

命令：`whetstone 1000`。上游程序用 `duration_ms * 1000` 计算 KIPS，和其注释要求的
秒制不符，导致板端 10 次都打印 `0.0 KIPS`。本轮没有修改只读 `apps/`；保留原始
duration，并用 `100 * loops * iterations / duration_ms` 透明换算 MIPS。

| Run | Duration (ms) | Corrected MIPS | Log SHA-256 |
|---:|---:|---:|---|
| 1 | 4374 | 22.862369 | `797e923c854f63a761e8fb4e81fc1e58bf7415232e47be4d75849b9112c29282` |
| 2 | 4373 | 22.867597 | `983df82c7d1be5a5d13d6bf2f5621e0917fd45115780074ecd1da343ebcb8808` |
| 3 | 4373 | 22.867597 | `8ffcbd75e07047b2a75e1853c31b7331b45e5ff375d50a93aab4b8465cd6f6c4` |
| 4 | 4373 | 22.867597 | `20022cbdece2125899ebb1937b9d0386986bb9bc7fe42bc814ad94e43718cb07` |
| 5 | 4373 | 22.867597 | `d0012f3992b49c7db06e5b70980237c51ece3d170711575bc5f4ba3b0f808ec0` |
| 6 | 4374 | 22.862369 | `7a0af5de3892958b07252143d61003e7cfcf5f1d19ecf80ea4456b1a56ff669b` |
| 7 | 4373 | 22.867597 | `6cd5a05c91d51f48cdf274dc6ce601c4001cf8a4dabf96451217fc776e53de98` |
| 8 | 4373 | 22.867597 | `409d5e36c8dae2dd477c59146eb0cb10d633065e4dd07a53268bb6ca71e225c8` |
| 9 | 4373 | 22.867597 | `760941a64118101d61988cc61052f721ad5b028ae0c167fdac74217fbb636920` |
| 10 | 4373 | 22.867597 | `81c5840babc5f19486f08fe2d21c761d16dcf921041342082b709b916cf87b1a` |

duration 统计：mean 4373.2 ms、median 4373、min 4373、max 4374、population stddev
0.4 ms。换算统计：mean 22.866551、median 22.867597、min 22.862369、max 22.867597、
population stddev 0.002091 MIPS。

## 9. 明确未覆盖项

- 诊断：受控 fault、critmon 非零告警阈值；当前代 xTS 后续状态见页首 g149 记录；
- 压力：真实介质 blktest、opus_ramtest、长时 memstress；12 小时 soak 经 owner 于
  2026-08-27 确认延期，本轮不阻塞；
- 性能：Dhrystone/TinyMemBench 的可复现源码锁定，BK7258 cache API 契约与 CacheSpeed；
- 硬件：GPIO/UART loopback 和比赛要求的其他外设矩阵；
- 主机：后续同日已迁移 `tests/bk7258/run_tests.sh` 到现役 chip/API，core 与 281 项
  cmocka 全部 PASS；见
  [主机回归记录](2026-08-27-bk7258-host-regression-fixture.md)。该结果不替代板端 xTS。

已观察到但不阻塞本轮门禁的启动日志：早期 BKSDIO CMD1 timeout 后 TF 正常挂载；
早期 LVGL channel not ready 后 UI/Agent 最终 ready。若最终状态缺失，应重新判为失败。

## 10. 最终状态

- 诊断/Allsyms/Backtrace/IRQ/critmon/cpuload/Trace/memstress 子集：PASS；
- CoreMark/Ramspeed/Whetstone 10 次基线：PASS；
- 现役 host fixture：PASS（core + 281 项 cmocka）；
- 新密钥、签名包、COM3 单文件全量下载、Agent 持久数据与写入边界：PASS；
- 完整 P0：PARTIAL，等待 §9 与 g149 记录列出的剩余项目；12 小时长稳已延期；
- 本文最后验证的性能镜像：generation 146（`1.88.2+146`，CP/CPU0 240 MHz）；
  板上当前镜像已由后续 xTS generation 149（`1.88.5+149`）替代。
