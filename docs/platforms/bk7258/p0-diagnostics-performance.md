# BK7258 T5-Board P0 调试、xTS 与性能基线适配（2026-08-27 快照）

> 文件名为避免破坏日期化 verification 的既有引用而保留；本文范围始终只对应 T5-Board。

- 最后复核：2026-08-27
- 目标板：T5-Board V1.0.2
- 当时测试夹具：COM3，BK Loader 2.1.11.15 / UART0 115200 8N1

> 本文是固定日期的专项验收快照；COM 号只标识当时夹具，不是板卡身份。文中的“当前”、
> “现役”和“退出条件”均指 2026-08-27 复核时点，不维护现行支持矩阵或操作流程。
> 现行支持范围见 `boards/bk7258/CONFIGS.md`，操作入口见
> [构建、烧录与调试 SOP](nuttx-port/bk7258-build-flash-debug-sop.md)。

## 1. 结论与边界

P0 采用两张用途互斥的 CP 镜像，不能把诊断镜像的结果当性能基线，也不能把性能
镜像当作产品或长稳镜像。

| Profile | 用途 | 2026-08-27 状态 |
|---|---|---|
| `t5_board/configs/xts` | 系统级诊断、Trace、压力和既有 xTS | generation 143 的诊断专项通过；generation 149 以 64 KiB CP role-local PSRAM system heap 保持标准 16 KiB runner stack，当前代 mm 8/8、sched 16/16、ostest、getprime、mm/ramtest、scanftest、hello/pipe 和冷启动通过 |
| `t5_board/configs/perf` | SDK OPP 240M、`-O3`、低噪声 benchmark | generation 145 的旧 OPP 320M/CPU0 160 MHz 基线保留为历史对照；generation 146 已完成 240 MHz 签名全量下载、回读、冷启动和三项 benchmark 各 10 次 |

该轮已闭环非破坏性诊断路径和三项性能基线。以下内容仍不能宣称完成：

- 受控 fault 后的 crash/backtrace 恢复；
- critmon 超限告警阈值，当前只验收统计可观测性；
- 依赖外部夹具或会改写介质的 xTS：GPIO/UART loopback、AP RTC/timer/RNG、
  `driver_test`、LIBCXX、真实介质 blktest/fstest 和 opus_ramtest；
- Dhrystone、TinyMemBench、CacheSpeed；
- 计划内 12 小时 soak（owner 于 2026-08-27 确认延期，本轮不阻塞）。

`make -C tests/host/bk7258 check` 的 host fixture 已迁移到现役 chip/API，并完成 281 项
cmocka 加公共模块门禁；见
[主机回归记录](../../../docs/verification/bk7258/2026-08-27-bk7258-host-regression-fixture.md)。

generation 143/145 的原始值、哈希和下载证据见
[2026-08-27 P0 实板验证记录](../../../docs/verification/bk7258/2026-08-27-bk7258-p0-diagnostics-performance.md)；
当前 240 MHz 基线见
[generation 146 时钟验证记录](../../../docs/verification/bk7258/2026-08-27-bk7258-sdk-clock-240m-validation.md)。
当前代 xTS 的 generation 147～149 根因、签名、下载和板端证据见
[generation 149 xTS 收口记录](../../../docs/verification/bk7258/2026-08-27-bk7258-p0-xts-completion.md)。
板子在本轮结束时保留 generation 149 xTS 镜像。

本手册对应官网 Backtrace（1623）、Allsyms（1624）、硬件性能（1632）、
irqinfo/critmon（1633）、cpuload（1634）、Dhrystone～Whetstone（1636～1641）、
blktest～opus_ramtest（1643～1646）和自测试框架（1648）。

## 2. 两张镜像的配置契约

### 2.1 诊断/xTS profile

配置目录：`boards/bk7258/t5_board/configs/xts`

| 能力 | 必要配置 | 板端入口 |
|---|---|---|
| ROMFS 启动 | `ETC_ROMFS`、`NSH_SYSINITSCRIPT`、`NSH_INITSCRIPT`、`BOARDCTL_FINALINIT` | `rc.sysinit` → final-init → `rcS` 日志 |
| IRQ 统计 | `FS_PROCFS`、`SCHED_IRQMONITOR` | `irqinfo`、`/proc/irqs` |
| 临界区统计 | `SCHED_CRITMONITOR`、`SYSTEM_CRITMONITOR` | `critmon_start`、`critmon`、`critmon_stop` |
| CPU load | `SCHED_CPULOAD_SYSCLK`、`SYSTEM_CPULOAD` | `ps`、`/proc/cpuload`、`cpuload` |
| 栈回溯 | `SCHED_BACKTRACE`、`UNWINDER_ARM`、`SYSTEM_DUMPSTACK` | `dumpstack [pid]` |
| 符号解析 | `ALLSYMS`、`DEBUG_FEATURES` | 回溯输出函数名而不只是 PC 地址 |
| Trace | Note driver、scheduler instrumentation、`SYSTEM_TRACE` | `trace start/stop/dump` |
| 内存压力 | `TESTING_MEMORY_STRESS` | `memstress` |
| xTS 系统堆 | `BK7258_PSRAM_SYSTEM_HEAP`、size=`0x10000`、`MM_REGIONS=2` | 启动出现 `BPSR SYSTEM HEAP PASS size=65536`，`free` 的 Umem total 增加 64 KiB |
| xTS runner | `TESTS_TESTSUITES_STACKSIZE=16384` | 保持上游标准 stack，mm 8/8、sched 16/16 |
| 退出语义 | `SIG_DEFAULT` | `kill -9 <pid>` 后任务确实消失 |
| 系统基线 | AP/RPTUN/Wi-Fi、WDT automonitor | Agent 双核启动与恢复日志 |

`NSH_MAXARGUMENTS=12` 是 memstress 带完整参数运行所必需。最终 resolved config 的
`SCHED_CRITMONITOR_MAXTIME_THREAD=0`，WQUEUE/PREEMPTION/CSECTION/BUSYWAIT/IRQ/WDOG
六类为 `-1`。本轮只验收 `critmon` 命令实际输出的 task/runtime 可观测性，没有配置或
验收任何非零超限告警预算，不能将结果描述成“告警完成”。

额外 64 KiB 只属于 `t5_board/configs/xts`：它在 CP 的 128 KiB role-local PSRAM 内注册为
第二个 NuttX system-heap region，另一半继续作为 CP 私有 PSRAM heap。生产 profile、
AP 角色分区和 AP Agent system heap 均不改变。不能通过缩小通用 testsuites runner
stack 来替代这项容量契约。

### 2.2 低噪声性能 profile

配置目录：`boards/bk7258/t5_board/configs/perf`

正向契约：

- `BK7258_CLOCK_240M=y` 使用 SDK `PM_CPU_FRQ_240M`，CPU0/AP/Bus 都为 240 MHz；
- SDK 320M/480M 是共享 OPP 名称，分别对应 CPU0/AP/Bus 160/320/160 MHz 和
  240/480/240 MHz，不能作为 CPU0 MHz 解读；
- `DEBUG_OPTLEVEL="-O3"`；
- CoreMark 固定 1 thread、10,000 iterations；
- 启用 CoreMark、Ramspeed、Whetstone 和 DWT performance counter；
- 保留 `BK7258_SDK_IRQ_BRIDGE`，因为 UART0 RX 是中断驱动；它不等于打开 IRQ 统计。

负向契约：

- AP 不自启动，Wi-Fi、RPTUN 和产品服务不运行；
- WDT 和 watchdog automonitor 关闭；
- Trace/instrumentation、IRQ monitor、critmon、Backtrace、Allsyms、memstress 关闭；
- 串口只用于输入命令、读取结果，不并发运行其他任务。

generation 144 曾因缺少 SDK IRQ bridge 出现“UART TX/NSH 正常、RX 不响应”，因此被
废弃，不能计入性能证据。generation 145 加回 bridge 后，`help` 和三个 benchmark 的
交互均通过。

## 3. 实现中的关键问题与修复

### 3.1 Allsyms 必须留在 XIP Flash

默认链接会把 `g_allsyms`/`g_nallsyms` 作为 `.data` 复制到 SRAM，诊断镜像启动后只剩
约 2.6 KiB heap，`task_spawn(nsh_main)` 会失败。板级链接脚本新增独立 `.allsyms`
输出段，把两个表 `KEEP` 在 FLASH，并检查段非空且位于 FLASH；修复后运行期可用 heap
约 22 KiB。

当前 GNU ld 2.36 不支持该脚本上下文的 `(READONLY)` 标记，因此 ELF section flag 仍
可能显示 WA；判据应是 VMA/物理地址位于 XIP FLASH 且启动代码不复制它，不能只看 flag。

### 3.2 Backtrace 输出和任务终止

`sched_dumpstack()` 使用 `_alert` 输出；只打开 Backtrace 而没有 `DEBUG_FEATURES` 会让
符号化结果被编译掉。本 profile 打开 `DEBUG_FEATURES`，同时关闭高噪声的
`DEBUG_ERROR`。`SIG_DEFAULT` 用于恢复标准 `SIGKILL` 行为，否则 cpuload/memstress
的后台任务不会被 `kill -9` 正常终止。

### 3.3 可选 WDT 组合

WDT pretimeout 使用由板级 partition CSV 声明的专用 `reset_marker` erase sector。
board 的单一不可变 storage binding 提供该 sector 的几何和 Flash guard 串行化策略；
chip 自己拥有原始 Flash 读、擦、写、校验，不再依赖或编译 OTA 专用 Flash helper。
OTA pair 写入期间的 WDT service 也在 OTA 的串行运行时点执行，避免将 watchdog 喂狗
与 Flash mutation 并发交错。WDT 关闭时 fault/NMI 路径改用
`up_systemreset()`，且不伪造 WDT reset cause。WDT 开启时，CP chip
`bk7258_system_reset()` 接受显式
`REBOOT`、`WATCHDOG` 或 `NMI_WDT` 语义，先写 SDK/PMU reason 再走 AON whole-device
reset；AON period 设置失败时回退 `up_systemreset()`。OTA whole-device reset 显式
使用 `REBOOT`。

marker 不在 WDT arm 或正常 feed 时写入。pretimeout timer interrupt 只输出有界崩溃
记录并排队；task-context worker 对 generation 与 elapsed time 二次校验确认 missed
feed 后才写入 marker。随后 reset-cause 以 PMU raw 为主：`POWERON`/`REBOOT` 不可被
stale marker 覆盖，已确认 WDT marker 只佐证 PMU WDT/NMI-WDT，或在未知 raw 值时补充
WDT 原因。当前 confirmed-pretimeout record 为 format v2；旧版 arm-time v1 record
校验失败且不得参与 reset attribution。

## 4. 构建与产物检查

构建中的 ELF 检查需要 `pyelftools` 和 `cxxfilt`。建议用临时 venv，不污染系统
Python：

```bash
python3 -m venv <temporary-venv>
<temporary-venv>/bin/pip install pyelftools==0.33 cxxfilt==0.3.0
```

诊断 direct clean-build 示例：

```bash
env PATH=<temporary-venv>/bin:$PATH \
tools/bk7258/bk7258.py build \
  --cp-config boards/bk7258/t5_board/configs/xts \
  --ap-config boards/bk7258/t5_board/configs/openvela_ap \
  --boot direct \
  --partition boards/bk7258/common/partitions/bk7258/bk7258_ab_agent_onchip_persistent.csv \
  --jobs 8 --clean
```

性能版只把 `--cp-config` 换为 `boards/bk7258/t5_board/configs/perf`。direct build 只作为编译/链接门禁；
实际全量板测必须重新使用 `--boot mcuboot`、本代新公钥和严格递增 counter 构建。

每次记录 layout identity、CP/AP build identity、resolved `.config` SHA-256、ELF/bin
大小与 SHA-256。必须检查最终生成的 `.config` 和 ELF/NSH 入口，不能只检查 defconfig。

## 5. 每次全量下载的强制规则

这是仓库的长期规则，不是本轮特例：

1. 每一次全量下载都在新的 0700 临时目录中分别生成全新的 BL1 P-256 和 MCUboot
   P-256 密钥对；两者不得共用，也不得复用任何已下载 generation 的私钥。
2. 私钥权限为 0600；不打印 PEM、不写普通日志、不提交。只保留公开 SPKI SHA-256。
3. 用新公钥执行 mcuboot clean build，以匹配私钥创建签名包；version、MCUboot
   security counter 和 BL1 counter 必须严格递增。
4. 包必须依次通过结构、公开信任链、flash contract 和 materialize 校验。
5. 使用当前 Agent 分区 CSV：
   `bk7258_ab_agent_onchip_persistent.csv`。全量 operator image 必须是一个文件，
   地址 `0x000000`、长度等于 CSV 声明的完整 Flash（当前为 `0x800000`）。
6. materialize 时保留 `usr_config [0x4fc000,0x50a000)` 以及整个 Agent persistent
   `[0x561000,0x7fa000)`；基底必须是同一设备的一次完整连贯读回，不能只保留旧版
   1 MiB persistent，也不能混用两次读回。
7. 只用 COM3 与 BK Loader 全量写入，不执行 chip erase。operator 中
   `[0x7fa000,0x800000)` 必须与该设备基底逐字节相同，禁止跨设备复制或修改工厂
   校准、OTP/eFuse、lifecycle 和 debug lock。
8. `WriteFlash ->pass`、`Writing Flash OK`、`All Finished Successfully` 只是下载器门禁；
   仍须验证签名启动链和目标功能。
9. 包、下载和板端证据确认后删除本代临时私钥目录；签名包、公钥指纹和哈希可留存。

完整 CLI 和边界说明见
[BK7258 build/package/hardware evidence SOP](nuttx-port/bk7258-build-flash-debug-sop.md)。

## 6. 诊断板测方法和已验证结果

所有命令在 COM3 的 CP NSH 中运行。本轮 generation 143 结果如下：

| 项目 | 命令/条件 | 已验证结果 |
|---|---|---|
| 启动 | 冷启动及压力后重启 | BL1 primary、BL2 AP OK、`rc.sysinit`、final-init、`rcS`、AI Agent ready；无 Panic/Fault/assert |
| Backtrace/Allsyms | `dumpstack`、`dumpstack 1` | 当前任务与 PID 1 均输出符号；包含 `up_backtrace`、`sched_dumpstack`、`dumpstack_main`、`nsh_main`、`work_thread` |
| IRQ | `irqinfo` | IRQ 11/15/20/70/79 可见且计数有效 |
| critmon | start → workload → stats → stop | daemon 可启动、输出统计并停止；仅验证可观测性 |
| CPU load | `cpuload -p 50 &`，6 秒后观察，`kill -9`，10 秒后复核 | task 49%，`/proc/cpuload` 56.5%；停止后 PID 消失并回落到 7.5% |
| memstress | `memstress -m 256 -n 16 -t 10000 -x 1 &` | 主 PID 25 / pthread 26 连续运行 10 秒，无校验错误和复位；`kill -9 25` 后两者均消失 |
| Trace | start → `hello` → stop → dump | 55,920 bytes、679 行，含 tracer header、hello、IRQ entry/exit |
| 恢复 | 压力结束后重启 | 完整 Agent 启动链再次通过 |

早期 BKSDIO CMD1 timeout 后 TF 能正常挂载，以及 LVGL channel 未就绪后 UI/Agent 最终
ready，是当前已知的非阻塞启动日志；若最终挂载或 ready 缺失，仍应判失败。

后续扩大 memstress 参数前必须按当前 free heap 计算；真实块设备测试只允许作用于
专用测试介质或可恢复分区，不得覆盖 Agent 持久数据和 TF 用户数据。

### 6.1 Generation 149 当前代 xTS 核心

generation 147 在标准 16 KiB runner stack 下暴露 CP Umem 容量不足；generation 148
用 8 KiB runner 验证 mm 后，又让 scheduler 的并发 stack 分配因 `ENOMEM` 失败。
最终 generation 149 恢复标准 runner，只给 xTS profile 增加 64 KiB CP PSRAM system
heap，冷启动 Umem total 从 117,656 增到 183,192 bytes。

当前代结果：`cmocka_mm_test` 8/8、`cmocka_sched_test` 16/16、`ostest` 状态 0、
`getprime`、`mm`、4 KiB `ramtest`、RAM tmpfs 上 `scanftest` 164/0、`hello` 和完整
FIFO/PIPE 均 PASS，最终冷启动和 Agent ready 再次通过。`ostest` 的最终文本落在 capture
切换间隙，判据使用完整执行输出、SWD idle 样本和同一 NSH 的退出状态 0；不得改写为
“直接捕获最终行”。详细 generation 身份、哈希和每项 UART raw SHA-256 见
[generation 149 xTS 收口记录](../../../docs/verification/bk7258/2026-08-27-bk7258-p0-xts-completion.md)。

## 7. 性能板测方法和基线

以下 generation 145 是**历史基线**：当时选择 SDK OPP 320M，CPU0 有效 160 MHz，
`-O3`、AP/Wi-Fi/RPTUN/WDT/Trace/监控关闭，
ELF `text=208900`、`data=6144`、`bss=10768`，静态 heap 约 243 KiB。每次 benchmark
均为独立串行 session；用于调参数的并发 pilot 没有纳入正式结果。

当前 profile 已按 SDK 修正为 OPP 240M/CPU0 240 MHz，并由 generation 146 实板验收。
generation 145 数值只能用于历史对照，不能冒充当前 240 MHz 基线。
OPP 细节见 [BK7258 SDK 时钟 OPP 与每核频率契约](../../chips/bk7258/sdk-clock-operating-points.md)。

### 7.1 CoreMark

命令：`coremark`，1 thread，10,000 iterations，10 次 validation 均通过。

| 统计 | CoreMark（generation 145） | CoreMark/MHz（历史 160 MHz） |
|---|---:|---:|
| mean | 374.307544 | 2.339422 |
| median | 374.251497 | 2.339072 |
| min | 374.251497 | 2.339072 |
| max | 374.391614 | 2.339948 |
| population stddev | 0.068643 | 0.000429 |

### 7.2 Ramspeed

命令：`ramspeed -a -s 65536 -n 1000`，中断保持开启。表中单位沿用程序输出。

| 64 KiB 项目 | mean | median | min | max | population stddev |
|---|---:|---:|---:|---:|---:|
| system memcpy | 103858.084 | 103856.231 | 103856.146 | 103861.371 | 2.355 |
| internal memcpy | 165939.601 | 165936.374 | 165935.944 | 165947.561 | 5.026 |
| system memset | 155758.918 | 155760.965 | 155751.868 | 155765.514 | 4.896 |
| internal memset | 355251.026 | 355251.618 | 355249.646 | 355251.618 | 0.904 |

### 7.3 Whetstone

命令：`whetstone 1000`。10 次时长为 4373～4374 ms，mean 4373.2 ms、median 4373 ms、
population stddev 0.4 ms。

当前只读上游 `apps/benchmarks/whetstone/whetstone.c` 把毫秒又乘了 1000，板端因而打印
错误的 `0.0 KIPS`。没有篡改上游源码，也没有把该值当成结果。按源码注释所要求的
秒制公式换算：

```text
corrected MIPS = 100 * loops * iterations / duration_ms
```

换算结果 mean 22.866551、median 22.867597、min 22.862369、max 22.867597、population
stddev 0.002091 MIPS。该值必须同时携带“上游单位换算缺陷”说明。

### 7.4 Generation 146 当前 240 MHz 基线

generation 146 使用本代新密钥和 counter 146 完成签名全量下载、460800 bit/s 稳定
回读与最终冷启动，三项各 10 个独立 session 全部通过。

| 项目 | generation 145（CPU0 160 MHz） | generation 146（CPU0 240 MHz） | 变化 |
|---|---:|---:|---:|
| CoreMark mean | 374.307544 | 561.576945 | +50.030891% |
| CoreMark/MHz | 2.339422 | 2.339904 | 基本不变 |
| Ramspeed system memcpy 64 KiB mean | 103858.084 | 155827.220 | 1.500386× |
| Ramspeed internal memcpy 64 KiB mean | 165939.601 | 248972.890 | 1.500383× |
| Ramspeed system memset 64 KiB mean | 155758.918 | 233699.378 | 1.500392× |
| Ramspeed internal memset 64 KiB mean | 355251.026 | 533009.086 | 1.500373× |
| Whetstone corrected MIPS mean | 22.866551 | 30.395137 | +32.924011% |

CoreMark 与四项 Ramspeed 都按 240/160 线性增长，构成实际 CPU0/Bus 240 MHz 的板端
证据。Whetstone 混合浮点和库实现，不单独用于反推时钟。每次值、统计、UART raw
SHA-256、签名身份和回读差异见
[generation 146 完整记录](../../../docs/verification/bk7258/2026-08-27-bk7258-sdk-clock-240m-validation.md)。

## 8. 验收时未覆盖边界

| 官网项 | 截至复核日的边界 | 若另行验收所需前提 |
|---|---|---|
| Dhrystone | 构建会下载未固定的 GitHub `master.zip` | 固定版本、哈希和许可证来源后启用 |
| TinyMemBench | 同样存在未锁定下载，Kconfig/官网命名需统一 | 锁定来源并验证 Armv8-M 非 NEON 路径 |
| CacheSpeed | BK7258 端尚未暴露通用 `ARCH_ICACHE && ARCH_DCACHE` capability | 实现并验证 NuttX cache API 契约，不能强行 select |
| opus_ramtest | 内存与日志扰动较大 | 独立压力 profile、RAM 预算和限时门禁 |
| 完整 blktest | 真实块设备测试有破坏数据风险 | 专用测试介质/分区与可恢复镜像 |
| 剩余 xTS | generation 149 已完成非破坏性核心；LIBCXX、GPIO/UART loopback、AP RTC/timer/RNG、`driver_test`、受控 fault 与破坏性介质用例未覆盖 | 按夹具和破坏性边界拆分执行，不能用 host 或历史记录升级当前实板状态 |
| 12h soak | owner 于 2026-08-27 确认延期 | 后续恢复时使用产品 profile、独立日志和明确健康判据 |
| 主机测试 | 现役 chip/API、当前 Agent 分区生成契约和全部分层目标已执行 | `make -C tests/host/bk7258 check` exit 0 且出现 `BK7258_HOST_TEST_PASS`；见主机回归记录 |

因此在 2026-08-27 适配快照中，P0 工作包记录为“部分完成”：本轮代码、当前代非破坏性 xTS 核心、
诊断专项、主机回归和三项性能基线已经闭环；但不能用它们替代仍需夹具的外设用例、
破坏性介质测试和已延期长稳。
