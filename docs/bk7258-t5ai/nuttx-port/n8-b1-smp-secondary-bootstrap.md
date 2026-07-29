# BK7258 N8-B1 — AP SMP secondary bootstrap

日期：2026-07-29

状态：`board-verified`（2026-07-29；factory image 下载后、未手动按键复位的真实 T5-AI 板测）

分支：`feat/bk7258-ap-smp`

## 1. 本 Stage 边界

N8-B1 从已合并、已板测的 N8-A 回退基线继续：

```text
configs/ap_up/
  physical CPU1 -> AP logical CPU0 -> NuttX UP
  physical CPU2 -> AP logical CPU1 -> freestanding probe
```

本分支只在 `configs/ap_smp/` 启用第一阶段 NuttX SMP 契约：

```text
physical CPU1 -> AP logical CPU0 -> SMP primary
physical CPU2 -> AP logical CPU1 -> NuttX-aware secondary bootstrap
```

CPU2 到达 secondary bootstrap、验证 NuttX 为 logical CPU1 分配的 IDLE stack 后立即进入受控
WFE park。它尚不调用 `nx_idle_trampoline()`，不进入普通 task 调度，诊断 `online_mask` 固定为
`0x1`。双向 IPI 和 CPU2 scheduler online 属于 N8-B2/N8-B3。

## 2. 配置边界

`configs/ap_smp/defconfig` 新增：

```text
CONFIG_BK7258_AP_SMP_BOOTSTRAP=y
CONFIG_SMP=y
CONFIG_SMP_NCPUS=2
CONFIG_SMP_DEFAULT_CPUSET=0x1
```

`CONFIG_SMP_DEFAULT_CPUSET=0x1` 是 N8-B1 的 fail-closed 门禁：普通 task 默认只允许在 logical
CPU0 上运行。`up_cpu_idlestack()` 单独把 logical CPU1 的 IDLE TCB affinity 固定为 CPU1。

`configs/ap_up/` 未修改，继续保持 N8-A 稳定回退配置。

## 3. 已落地代码

### 3.1 NuttX SMP architecture hooks

新增 `chip/ap/bk7258_ap_smp.c`，提供：

- `up_cpu_index()`：读取 AP-local core-ID cell，映射 CPU1→0、CPU2→1；
- `up_get_intstackbase()`：CPU0 使用既有 linker interrupt stack，CPU2 使用 AP SRAM 顶部 boot/interrupt stack；
- `up_cpu_idlestack()`：为 logical CPU1 IDLE TCB 分配独立 NuttX stack；
- `up_cpu_start()`：由 `nx_smp_start()` 调用，准备共享状态、secondary vector、IDLE stack 诊断和 CPU2 control register，然后等待 `SECONDARY_READY`；
- `up_send_smp_sched()` / `up_send_smp_call()`：N8-B1 的 fail-closed IPI 占位；任何对 CPU2 的意外 SMP call 都增加 `smp_call_requests`、记录 `IPI_UNAVAILABLE`，但不伪装 IPI 已工作；
- `bk7258_ap_smp_secondary_stop()`：AP stop/restart 前请求 CPU2 停止，超时后强制 reset hold。

### 3.2 CPU2 secondary contract

CPU2 vector 继续位于：

```text
0x02200200
```

secondary reset entry 完成：

1. 关闭中断；
2. 设置 AP-local logical core ID 为 1；
3. 设置 VTOR 为 `__vector_core1_table`；
4. 使用 AP SRAM 顶部专用 boot MSP；
5. 进入 `bk7258_ap_secondary_bootstrap()`；
6. 验证 shared-state ABI、logical/physical ID、VTOR、MSP 和 NuttX logical CPU1 IDLE stack；
7. 发布 `secondary_ready=1`、`state=SECONDARY_READY`、`online_mask=0x1`；
8. 在中断关闭状态下 WFE park，不进入 `nx_idle_trampoline()`。

### 3.3 SRAM 边界

AP-UP 继续保留 N8-A 的 1 KiB CPU2 probe stack。AP-SMP 则从同一顶部边界
`0x2809f000` 向下预留一个完整的 `CONFIG_ARCH_INTERRUPTSTACK`：

```text
CPU2 AP-SMP boot/interrupt stack:
  0x2809e800..0x2809efff   (CONFIG_ARCH_INTERRUPTSTACK=2048)
```

CPU2 的未来 IDLE stack 由 NuttX 从 AP heap 独立分配，并通过共享状态记录
`idle_stack_base..idle_stack_top`。

## 4. 共享诊断扩展

CPU2 共享状态 ABI 仍保持 `0x80` 字节；原 `reserved[9]` 被定义为：

```text
idle_stack_base
idle_stack_top
secondary_entry
secondary_ready
online_mask
smp_call_requests
boot_count
reserved[2]
```

新增状态：

```text
BOOTSTRAP
SECONDARY_READY
```

`apctl status` 在 AP-SMP 镜像下新增一行：

```text
CPU2 bootstrap entry=........ idle=................. ready=1 online=00000001 calls=0 boots=1
```

## 5. 构建方式

本轮由用户负责实际编译。dual-image builder 已支持选择 AP 配置：

```bash
cd /home/lijian/project/open-vela
AP_CONFIG_NAME=ap_smp \
  contest2026_135_yongwangzhiqian/board/bk7258_t5ai/scripts/build_dual_image.sh
```

预期 builder 仍执行：CP → AP-SMP → CP restore，并把 AP-SMP 产物保存到：

```text
nuttx/bk7258-dual/app1.bin
nuttx/bk7258-dual/app1_crc.bin
nuttx/bk7258-dual/nuttx-ap.elf
nuttx/bk7258-dual/nuttx-ap.map
```

## 6. 编译后静态门禁

在烧录前必须确认：

```text
CONFIG_BK7258_AP_SMP_BOOTSTRAP=y
CONFIG_SMP=y
CONFIG_SMP_NCPUS=2
CONFIG_SMP_DEFAULT_CPUSET=0x1
```

AP ELF 必须包含：

```text
up_cpu_index
up_get_intstackbase
up_cpu_idlestack
up_cpu_start
up_send_smp_sched
up_send_smp_call
bk7258_ap_secondary_bootstrap
__vector_core1_table
```

并确认：

```text
__vector_core1_table = 0x02200200
CPU2 boot stack      = 0x2809e800..0x2809f000
```

代码落地、尚未构建时完成过以下静态检查：

```text
git diff --check                         PASS
build_dual_image.sh bash -n              PASS
focused SMP contract review              PASS
AP-UP/AP-SMP source groups mutually exclusive
CPU2 shared-state ABI remains 0x80 bytes
```

以上结果在当时只能称为 `static-only`；后续 fresh build 与板测结果见 §9。

### 6.1 首次 fresh build 阻塞与修复

用户首次构建在 `chip/include/irq.h` 的旧 N7 门禁停止：

```text
#error "N7 gate: CP and CPU1 AP images require CONFIG_SMP=n"
```

根因是该门禁仍把所有 `CONFIG_SMP=y` 配置视为非法，没有区分新的 N8-B1 AP-SMP 配置。现已精准
收窄条件：CP 和 `ap_up` 仍禁止 SMP，只有同时满足
`CONFIG_BK7258_AP_CORE=y` 与 `CONFIG_BK7258_AP_SMP_BOOTSTRAP=y` 的构建允许继续。用户随后
重新编译成功，并进入 §9 板端验证。

## 7. 第一轮板端门禁

```text
AP state=READY
AP logical CPU0 NuttX heartbeat 持续增长
CPU2 state=SECONDARY_READY
CPU2 error=0
CPU2 local=1 physical=2
CPU2 vector/VTOR=0x02200200
CPU2 MSP 位于 0x2809e800..0x2809f000
CPU2 idle stack 位于 AP heap，且不与 boot stack 重叠
secondary_ready=1
online_mask=0x1
smp_call_requests=0
boot_count=1
CPU2 heartbeat 可被 AP 的 SEV 周期唤醒并增长
连续空提示符和 apctl status 不触发 CPU0 HardFault
apctl restart 后 AP/CPU2 generation 同时递增
```

若 `smp_call_requests != 0` 或 error=`IPI_UNAVAILABLE`，说明 NuttX 已尝试向尚无 IPI 的 CPU2
发送调度请求，本轮不得继续烧录压力测试，也不得把状态升级为 `board-verified`。

## 8. 暂不实现

- CPU2 `nx_idle_trampoline()`；
- scheduler online mask `0x3`；
- 双向 IPI；
- 普通 task 在 logical CPU1 上运行；
- task affinity/migration；
- spinlock/semaphore 压力；
- RPTUN/RPMsg、Wi-Fi、BLE。

## 9. N8-B1 板端闭环

日期：2026-07-29

状态：`board-verified`

### 9.1 验证条件

用户使用本轮 `all-app-factory.bin` 下载，下载结束后没有额外执行按键复位。由于 factory image 会
覆盖数据区，本轮按用户决定不把 LittleFS 作为门禁，只验证 CPU0/AP/CPU2 secondary bootstrap、
生命周期和 CPU0 task-exit 回归。

### 9.2 首次启动

连续三次 `apctl status` 的关键原始证据：

```text
AP state=READY(2) error=0 generation=1 heartbeat=837
AP core local=0 physical=1 VTOR(init/run)=02200000/28050800 MSP(init/run)=28050800/28050800
AP heap=280526dc..2809e7fc test=28054158 doorbells cp/ap=0/1
CPU2 state=SECONDARY_READY(7) error=0 generation=1 heartbeat=840
CPU2 core local=1 physical=2 vector=02200200 VTOR=02200200 MSP(init/run)=2809f000/2809eff8
CPU2 bootstrap entry=022008c5 idle=28052b98..28053380 ready=1 online=00000001 calls=0 boots=1

AP heartbeat:   837 -> 877 -> 916
CPU2 heartbeat: 840 -> 880 -> 919
```

证据闭合：

- AP primary 继续为 local 0 / physical 1；
- CPU2 为 local 1 / physical 2；
- CPU2 vector/VTOR 均为 `0x02200200`；
- runtime MSP `0x2809eff8` 位于 `0x2809e800..0x2809f000` boot stack；
- NuttX 分配的 logical CPU1 IDLE stack 为 `0x28052b98..0x28053380`，位于 AP heap 且不与 boot stack 重叠；
- `secondary_ready=1`、`online_mask=0x1`、`smp_call_requests=0`、`boot_count=1`；
- AP 与 CPU2 heartbeat 同步持续增长；
- 连续空提示符和多次 `apctl status` 未触发 CPU0 HardFault。

CPU0 `ps` 继续正常返回；本输出属于 CP NuttX，不作为 AP logical CPU1 scheduler online 证据。N8-B1
的权威边界仍是 CPU2 已完成 secondary bootstrap、但 `online_mask=0x1`。

### 9.3 restart

`apctl restart` 后：

```text
AP state=READY(2) error=0 generation=2 heartbeat=1
CPU2 state=SECONDARY_READY(7) error=0 generation=2 heartbeat=4
CPU2 core local=1 physical=2 vector=02200200 VTOR=02200200 MSP(init/run)=2809f000/2809eff8
CPU2 bootstrap entry=022008c5 idle=28052b98..28053380 ready=1 online=00000001 calls=0 boots=1
```

随后 heartbeat 增长为：

```text
AP:   83 -> 232
CPU2: 86 -> 235
```

AP/CPU2 generation 同时从 1 变为 2，CPU2 每个新 generation 的 `boots` 重新为 1。

### 9.4 stop/start

stop 后：

```text
AP state=STOPPED(4) error=0 generation=2 heartbeat=392
CPU2 state=STOPPED(4) error=0 generation=2 heartbeat=395
CPU2 control=00000000 SYS(before/after)=02200230/02200230
CPU2 bootstrap entry=022008c5 idle=28052b98..28053380 ready=0 online=00000001 calls=0 boots=1
```

再次 start 后：

```text
AP state=READY(2) error=0 generation=3 heartbeat=1
CPU2 state=SECONDARY_READY(7) error=0 generation=3 heartbeat=4
CPU2 bootstrap entry=022008c5 idle=28052b98..28053380 ready=1 online=00000001 calls=0 boots=1
```

heartbeat 随后增长到 AP `135`、CPU2 `138`。

### 9.5 三轮 cycle

`apctl cycle 3 3000` 完成 generation 4、5、6 的连续 start/stop，最终 generation 6 正常进入：

```text
AP state=STOPPED(4) error=0
CPU2 state=STOPPED(4) error=0
CPU2 ready=0 online=00000001 calls=0 boots=1
```

日志中的：

```text
apctl: start failed: -16
```

来自 cycle 尚在执行时又排队输入了额外 `apctl start`，该命令在 AP 已处于 READY 时得到预期
`-EBUSY`；它没有改变 cycle 的三代启动/停止结果，也不是 CPU2 bootstrap、timeout 或 fault。
UART 中部分行交错同样来自连续命令输入过快。

### 9.6 最终结论

N8-B1 全部门禁通过：

```text
AP primary READY
CPU2 SECONDARY_READY
logical/physical ID = 1/2
vector/VTOR = 0x02200200
boot MSP 合法
NuttX logical CPU1 IDLE stack 合法
secondary_ready = 1
online_mask = 0x1
smp_call_requests = 0
restart generation 联动通过
stop/start/cycle 通过
无 CPU0/CPU2 HardFault
```

N8-B1 正式标记为 `board-verified`。下一阶段是 N8-B2 双向 IPI；在 IPI 通过前，仍不得让 CPU2
调用 `nx_idle_trampoline()` 或进入普通 NuttX task 调度。
