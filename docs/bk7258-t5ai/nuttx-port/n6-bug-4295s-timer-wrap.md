# BK7258 N6：约 4295 秒后 `HF` + 重启根因

日期：2026-07-23

状态：**根因已由源码与旧 ELF 反汇编确认；团队 overlay 已启用 `CONFIG_SYSTEM_TIME64=y`，干净构建及最终 ELF 反汇编均已验证通过；尚未烧录及完成越过 4400 秒的板测。**

## 现象

系统正常启动并进入 NSH，运行约 4200 秒以上后输出：

```text
HFu_bootloader enter
```

随后重新启动。多次运行均在相近时间复现。

## 日志解释

源码中的 bootloader 字符串实际是：

```text
u_bootloader enter
```

位于 `board/bk7258_t5ai/bootloader/boot_main.c:187`。

`HF` 来自 `board/bk7258_t5ai/chip/bk7258_vectors.c:243-249` 的临时故障处理器。该处理器同时占用向量槽 2（NMI）和槽 3（HardFault），因此当前日志不能区分 NMI 与 HardFault。结合后述喂狗链，最可能是 APB WDT 超时产生的 NMI；`F` 与下一次启动的 `u_bootloader enter` 相连，形成 `Fu_bootloader enter`。

## 已确认根因

触发问题时（本次修改前）的配置为：

```text
CONFIG_TIMER_ARCH=y
CONFIG_USEC_PER_TICK=10000
CONFIG_SYSTEM_TIME64 未启用
```

因此 `clock_t` 是 32 位。NuttX 通用 arch timer 在 `nuttx/drivers/timers/arch_timer.c:97-110` 中计算微秒时间：

```c
return TICK2USEC(timebase) + (status.timeout - status.timeleft);
```

而 `nuttx/include/nuttx/clock.h:168` 定义：

```c
#define TICK2USEC(tick) ((tick) * USEC_PER_TICK)
```

`timebase` 为 32 位 `clock_t`，所以乘法在 32 位中完成后才扩展为函数返回的 `uint64_t`。溢出点为：

```text
2^32 us = 4294967296 us = 4294.967296 s
```

修复前 `nuttx` ELF 的 `current_usec()` 反汇编也直接证明了这一点：地址 `0x0201537a` 使用单个 32 位 `mla` 完成 `timebase * 10000 + sub_tick_usec`，返回值高 32 位被置零。

## 为什么最终会重启

1. `nuttx/drivers/timers/arch_timer.c:265-275` 的 `up_timer_gettick()` 使用 `current_usec() / USEC_PER_TICK`。
2. `CONFIG_TIMER_ARCH=y` 时，`nuttx/sched/clock/clock_systime_ticks.c:89-95` 的 `clock_systime_ticks()` 调用 `up_timer_gettick()`。
3. 每个系统 tick 中，`nuttx/sched/sched/sched_processtimer.c:179-200` 把该值传给 `wd_timer()`，用于处理 NuttX watchdog/软件定时器绝对到期时间。
4. 到 4294.967296 秒时，系统对外报告的 tick 从约 `429496` 突然折返到 `0`，而活动 watchdog 节点的绝对到期值仍在约 `429xxx`。
5. 软件 watchdog 队列因此把这些节点误判为“还需等待约 4295 秒”，其中包括硬件 WDT automonitor 的下一次喂狗回调。
6. BK7258 配置为 APB WDT 8 秒超时、每 2 秒自动喂一次：
   - `CONFIG_WATCHDOG_AUTOMONITOR_TIMEOUT=8`
   - `CONFIG_WATCHDOG_AUTOMONITOR_PING_INTERVAL=2`
7. automonitor 停止执行后，APB WDT 在约 8 秒内触发 NMI/复位，形成 `HF` 后重启。

因此预计实际复位时间约为 4295 秒再加最后一次喂狗到超时的 6～8 秒，和板端“超过 4200 秒后稳定复现”完全一致。

## 已实施的最小 overlay 修复

未修改官方 `nuttx/` 树，已在团队板级 `board/bk7258_t5ai/configs/nsh/defconfig` 中启用：

```text
CONFIG_SYSTEM_TIME64=y
```

该配置会使 `clock_t` 变为 64 位，`TICK2USEC(timebase)` 的乘法也会在 64 位中执行，从而消除 4295 秒折返。

## 修复原理

### 返回值是 64 位，不代表表达式会自动按 64 位计算

`current_usec()` 虽然返回 `uint64_t`，但 C 语言会先根据运算数类型计算右侧表达式，最后才转换为函数返回类型。修复前 `timebase` 是 32 位 `clock_t`，所以：

```c
TICK2USEC(timebase)  /* timebase * 10000 */
```

先在 32 位中完成。结果超过 `0xffffffff` 后，高位已经在乘法或随后加上 tick 内微秒数时丢失；再把结果扩展为 `uint64_t` 也无法恢复。以相邻 tick 为例：

```text
429496 * 10000 = 4294960000
429497 * 10000 = 4294970000 = 2^32 + 2704
```

因此跨过 `2^32 us` 时，计算值会从约 4294967295 折回到 0 附近。这不是应用传入了一个过大的延时参数，而是系统正常运行到该时刻后，基础时钟必然触发的中间表达式溢出。

### `CONFIG_SYSTEM_TIME64` 如何消除折返

启用 `CONFIG_SYSTEM_TIME64=y` 后，`clock_t` 和 `timebase` 变为 64 位。按照 C 的通常算术转换规则，`timebase * 10000` 会以 64 位计算，tick 内微秒数的加法也会保留向高 32 位的进位。随后 `up_timer_gettick()` 再用完整的 64 位微秒值除以 10000，得到持续单调增长的 64 位系统 tick。

最终 ELF 反汇编与这一过程一致：`ldrd` 读取 64 位 `timebase`，`umull` 产生乘积的高低双字，`mla` 累加原 `timebase` 的高半部分，最后通过 `adds` 和 `adc` 把 tick 内微秒数及进位合入 64 位返回值。

### 为什么表现为 WDT 复位

软件 watchdog 队列保存的是绝对到期 tick。基础 tick 突然从约 `429496` 回到 0 后，已经排队在 `429xxx` 附近的 automonitor 喂狗回调被误认为还远未到期，因此每 2 秒一次的 APB WDT 喂狗停止。硬件 WDT 仍在正常工作，并按 8 秒超时触发 NMI/复位；所以 WDT 是这个系统时间错误的最终执行者，而不是根因。

禁用 WDT 或另加一条独立喂狗路径只能掩盖重启，其他软件定时器仍会在同一时刻失效，因此不是正确修复。

### 为什么验证要越过 4400 秒

旧故障的算术折返点是 `4294.967296 s`，预计 WDT 会在最后一次喂狗后的约 6～8 秒，即大约 4301～4303 秒复位。运行到 4400 秒可多覆盖约 105 秒，相当于超过 50 个 2 秒喂狗周期和 13 个 8 秒 WDT 超时窗口。若 `/proc/uptime` 在 4280、4310、4400 秒附近持续单调增长，且没有 `HFu_bootloader enter`，即可完成这条故障链的板级验证。

## 构建级验证（2026-07-23）

构建前检查未发现正在运行的 openvela/NuttX build、distclean、烧录或占用同一输出目录的进程。先执行干净构建：

```sh
cd /home/lijian/project/open-vela
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh distclean  # exit 0
./build.sh vendor/openvela/boards/contest2026_135_bk7258/configs/nsh -j8       # 首次 exit 2
```

首次链接失败是 IRQ timer test 只包含 `<nuttx/irq.h>`，导致 `enter_critical_section()` / `leave_critical_section()` 被隐式声明为外部函数。最小修复仅把团队 overlay 文件 `board/bk7258_t5ai/chip/bk7258_sdk_irq_timer_test.c` 的包含头替换为 `<nuttx/spinlock.h>`，使这两个 NuttX API 在当前单核配置下正确展开为 `up_irq_save()` / `up_irq_restore()`。随后执行同一构建命令，退出码为 0，完整日志保存在 `/tmp/bk7258-4295s-build-after-irq-fix.log`。

最终 `/home/lijian/project/open-vela/nuttx/.config` 实测为：

```text
CONFIG_SYSTEM_TIME64=y
CONFIG_TIMER_ARCH=y
CONFIG_USEC_PER_TICK=10000
```

最终 ELF 的 `current_usec` 位于 `0x0201562c`，关键指令为：

```text
02015632: ldrd  r4, r6, [r5, #8]   ; 读取 64 位 timebase，r6:r4
02015656: umull r4, r1, r4, r3     ; timebase 低 32 位 * 10000 -> r1:r4
0201565a: mla   r1, r3, r6, r1     ; 累加 timebase 高 32 位 * 10000
0201565e: adds  r0, r0, r4          ; 加入当前 tick 内微秒数的低 32 位
02015660: adc   r1, r1, #0          ; 向返回值高 32 位传播进位
```

这已不再是旧 ELF 中“单条 32 位 `mla` 后把返回高 32 位清零”的实现。最终 ELF 中也没有未解析的 `enter_critical_section` / `leave_critical_section` 符号。

生成产物：

```text
/home/lijian/project/open-vela/nuttx/nuttx
/home/lijian/project/open-vela/nuttx/nuttx.bin
/home/lijian/project/open-vela/nuttx/nuttx_crc.bin
/home/lijian/project/open-vela/nuttx/all-app.bin
```

`all-app.bin` 大小为 240618 字节，SHA-256 为 `21a4f281cccf87500bd7c67a31d6aa097cfe0bb175ab9730d5a0bf5f44f589e9`。静态检查 `git diff --check` 退出码为 0。尚未执行烧录和超过 4400 秒的板测。

建议同时在 BK7258 Kconfig/构建门禁中约束：使用 `CONFIG_TIMER_ARCH` 时必须启用 `CONFIG_SYSTEM_TIME64`，防止以后 defconfig 回退。

## 上游正确修复

NuttX 通用实现应在乘法前显式扩展：

```c
return (uint64_t)timebase * USEC_PER_TICK +
       (uint64_t)(status.timeout - status.timeleft);
```

但本项目规则是不直接修改官方 `nuttx/` checkout；如需上游修复，应走独立社区 PR。

## 相关但不是本次直接根因的问题

`bk7258_os_adapt.c` 中还存在：

```c
nxsig_usleep(milliseconds * 1000);
```

其 32 位毫秒转微秒也会在大参数时溢出，应后续单独修复。但本次无需任何“大延时调用”：系统基础 tick 转微秒本身就会在 4294.967296 秒必然折返，因此它才是当前稳定重启的直接根因。

## 待验证

1. 烧录新生成的 `nuttx/all-app.bin`。
2. 连续运行越过 4400 秒；在 4280、4310、4400 秒附近读取 `/proc/uptime`，确认单调递增。
3. 确认不再出现 `HFu_bootloader enter`，且 WDT、LittleFS、DVFS、UART/NSH 无回归。
4. 后续把 NMI 与 HardFault 日志拆分，避免 `HF` 二义性。
