# BK7258 N7：CPU0 间歇性 task-exit HardFault 根因与修复

日期：2026-07-29

状态：**BOARD-VERIFIED。最终最小修复不修改官方 `nuttx/` 源码，只修改团队 overlay 中 4 个文件。清理所有错误假设和临时实验后，重新编译、下载并在真实 T5-AI 板卡上验证成功。**

## 1. 一分钟读懂

CPU0/CP 运行 NuttX 和 NSH，CPU1/AP 运行独立的 NuttX 镜像。系统大部分时间工作正常，但在执行完 `apctl status`、退出临时 NSH task，或者连续敲空命令后，CPU0 偶尔进入 HardFault。

最初 UART 输出看起来像是 `svc 0` 后面的 NuttX task-exit 代码取指失败；但 J-Link 读取真正的 Handler 栈后发现，CPU 实际准备跳转的 PC 和 LR 都已经变成：

```text
0xaaaaaaaa
```

最终证据闭合为两层问题：

1. 在板端观察到的 no-switch 路径中，`arm_doirq()` 最终返回了 `NULL`，尽管被选中 TCB 的 `tcb->xcp.regs` 仍是有效地址。
2. ARM 汇编异常恢复代码没有把这个 NULL 当作错误，而是从地址 0 开始恢复寄存器；低地址中恰好是 `0xaaaaaaaa`，于是 LR/PC 也被恢复成 `0xaaaaaaaa`，最终产生指令访问违规 `IACCVIOL`。
3. 第一版 NULL fallback 使用全局临时状态后，又暴露出第二层问题：SysTick 可以抢占优先级较低的 UART1 ISR，而当前配置没有启用 `CONFIG_ARCH_HIPRI_INTERRUPT`，通用 `arm_doirq()` 路径不能安全嵌套。内外两层 IRQ 混用了恢复上下文，产生 `INVPC`。

最终修复只做四件事：

- 在 `nxsched_resume_scheduler()` 清空 `tcb->xcp.regs` 前保存最终有效上下文；
- `arm_doirq()` 返回 NULL 时使用这个已保存的上下文，仍为空则安全停机；
- `arm_doirq()` 执行期间通过 BASEPRI 阻止当前配置不支持的普通 IRQ 嵌套；
- 未启用 HIPRI dispatcher 时，把 SDK device IRQ 固定到与 SysTick 相同的逻辑优先级 4。

最终只保留以下团队文件的修改：

```text
board/bk7258_t5ai/chip/Make.defs
board/bk7258_t5ai/chip/common/bk7258_sdk_irq.h
board/bk7258_t5ai/chip/cp/bk7258_sdk_irq.c
board/bk7258_t5ai/chip/cp/bk7258_vectors.c
```

官方文件 `nuttx/arch/arm/src/arm_m/arm_schedulesigaction.c` 的临时修改已经完全撤销。

---

## 2. 故障发生在什么系统里

### 2.1 CPU0 和 CPU1 分别做什么

本阶段采用两个独立 NuttX 镜像：

```text
CPU0 / CP
  ├─ NuttX
  ├─ NSH console（UART1）
  ├─ Flash / LittleFS
  ├─ SDK IRQ bridge
  └─ apctl：启动、停止和查询 CPU1

CPU1 / AP
  ├─ 独立 NuttX UP 镜像
  ├─ headless，不争用 CPU0 UART1
  ├─ 共享 SRAM 报告 READY 状态
  └─ heartbeat 持续递增
```

故障发生在 CPU0，不是 CPU1。即使故障前执行的是 `apctl status`，也不能据此认定 AP 控制代码就是根因。`apctl` 只是比较容易触发“创建一个 NSH builtin task → 执行 → task 退出 → 调度器恢复其他 task”这条路径。

### 2.2 什么是 task-exit

在 NSH 中输入：

```text
nsh> apctl status
```

NuttX 会运行对应的 builtin 程序。程序打印完状态并返回后，临时 task 需要被删除，调度器再恢复 NSH 或 IDLE 等其他 task。

简化调用关系如下：

```text
apctl 返回
  -> task 退出
  -> 释放/删除当前 TCB
  -> nxsched_switch()
  -> svc 0
  -> SVC 异常处理
  -> arm_doirq()
  -> 恢复下一份 task context
```

因此，`apctl status` 是触发器，但真正故障点在 ARM 异常恢复和 NuttX context 切换边界。

---

## 3. 小白需要先知道的 Cortex-M 概念

## 3.1 Thread mode 和 Handler mode

Cortex-M 主要有两种运行模式：

- **Thread mode**：普通 task、NSH、IDLE 等代码运行的模式；
- **Handler mode**：中断和异常处理程序运行的模式，例如 SysTick、UART、SVC、HardFault。

可以把它们理解为：

```text
Thread mode  = 正常业务/task 世界
Handler mode = CPU 暂停正常 task 后进入的异常/中断世界
```

## 3.2 PSP 和 MSP

Cortex-M 有两个栈指针：

- **PSP（Process Stack Pointer）**：通常给普通 task 使用；
- **MSP（Main Stack Pointer）**：通常给异常和中断使用。

NuttX 正常运行时希望形成：

```text
普通 task                  使用 PSP
SVC / SysTick / UART / HF  使用 MSP
```

`CONTROL.SPSEL` 决定 Thread mode 使用 PSP 还是 MSP：

```text
CONTROL bit 1 = 0  Thread mode 使用 MSP
CONTROL bit 1 = 1  Thread mode 使用 PSP
```

## 3.3 异常发生时 CPU 自动保存什么

异常进入时，Cortex-M 会自动在栈上保存 8 个基本寄存器：

```text
SP + 0x00  R0
SP + 0x04  R1
SP + 0x08  R2
SP + 0x0c  R3
SP + 0x10  R12
SP + 0x14  LR
SP + 0x18  PC       <- 最重要，表示被打断时的位置
SP + 0x1c  xPSR
```

如果使用 FPU，还可能在基本帧前面存在扩展浮点帧。因此读取 fault frame 时，必须结合 `EXC_RETURN` 判断帧格式，不能永远假设 SP 直接指向 R0。

## 3.4 EXC_RETURN 是什么

进入异常后，LR 不再只是普通函数返回地址，而是一个特殊值，称为 `EXC_RETURN`。它告诉 CPU：

- 返回 Thread mode 还是 Handler mode；
- 返回后使用 PSP 还是 MSP；
- 当前是基本栈帧还是 FPU 扩展栈帧；
- ARMv8-M Secure/Non-secure 返回属性。

本次最重要的几个值是：

| EXC_RETURN | 简化解释 |
|---|---|
| `0xfffffffd` | 返回 Thread mode，使用 PSP，基本帧 |
| `0xffffffed` | 返回 Thread mode，使用 PSP，FPU 扩展帧 |
| `0xfffffff1` | 返回 Handler mode，使用 MSP，基本帧 |

相关位：

```text
bit 3 = 1  返回 Thread mode
bit 2 = 1  返回时使用 PSP
bit 4 = 1  基本帧
bit 4 = 0  FPU 扩展帧
```

本次修复必须区分 Thread context 和 Handler context。把 Handler context 当成 Thread/PSP context 恢复，会得到非法异常返回，表现为 `INVPC`。

## 3.5 NVIC 优先级为什么容易看反

ARM NVIC 的优先级数值越小，优先级越高：

```text
0x60  比 0x80 高
0x80  比 0xc0 高
```

本平台实际实现 3 个有效优先级位。本次观察到：

```text
SVC     约 0x60
SysTick 约 0x80
UART1   原来约 0xc0（逻辑优先级 6）
```

因此原配置下，SysTick 可以抢占 UART1。

---

## 4. 最初看到的故障

典型 UART fault 信息类似：

```text
HF E=00000003 X=fffffff5 S=28007158
   H=40000000 C=00000001
   P=0201ab6c L=02015b99 Q=21000000
```

字段含义：

| 字段 | 含义 |
|---|---|
| `E` | exception number，3 表示 HardFault |
| `X` | 异常入口 LR，也就是 EXC_RETURN |
| `S` | fault handler 根据 EXC_RETURN 选择的栈指针 |
| `H` | HFSR，HardFault 状态寄存器 |
| `C` | CFSR，MemManage/BusFault/UsageFault 综合状态 |
| `P` | 所读取异常帧中的 PC |
| `L` | 所读取异常帧中的 LR |
| `Q` | 所读取异常帧中的 xPSR |

关键值：

```text
HFSR = 0x40000000
CFSR = 0x00000001
```

解释如下：

- `HFSR.FORCED=1`：某个可配置 fault 没有被单独处理，最终升级成 HardFault；
- `CFSR.IACCVIOL=1`：CPU 尝试从不允许执行的地址取指令。

地址映射显示，早期 UART 中的 `P` 常落在 `nxsched_switch()` 的 `svc 0` 后方，`L` 落在 task 删除或等待路径附近。这把排查方向正确地引向了 context switch，但它还不是完整现场。

---

## 5. 为什么 UART 的 P/L 一度具有误导性

### 5.1 一个异常里可能同时存在不止一份栈帧

当系统正在处理 SVC，又在异常返回期间发生 HardFault 时，内存中可能同时存在：

- PSP 上的 task/SVC frame；
- MSP 上的 Handler/HardFault frame；
- NuttX 软件额外保存的完整寄存器 context。

早期 fault recorder 根据 HardFault 入口 LR 的 SP 选择位读取一份 frame。故障升级和非法返回状态下，该值可能让 recorder 读取到 PSP 上仍然合法的 task/SVC frame。

所以 UART 的：

```text
P = svc 0 后面的 NuttX 地址
L = task 退出相关地址
```

并不是伪造数据；它们描述的是“正在被恢复的 task/SVC 上下文”。但是 CPU 真正因为错误恢复而准备执行的 PC，可能已经存在于另一份 Handler frame 中。

### 5.2 J-Link 看到真正的 Handler 现场

在旧故障现场用 J-Link 停住 CPU 后，读取到：

```text
MSP 接近低地址（曾观察到约 0x10）
实际 Handler frame 的 LR = 0xaaaaaaaa
实际 Handler frame 的 PC = 0xaaaaaaaa
xPSR 的 IPSR = 11（SVC）
```

`0xaaaaaaaa` 是内存填充值，不是合法 Thumb 代码地址。这个证据改变了排查方向：

```text
不是正常代码地址 0x0201xxxx 本身不能执行
而是异常恢复过程把 PC/LR 恢复成了 0xaaaaaaaa
```

这也解释了 `CFSR.IACCVIOL`：CPU 最终尝试从 `0xaaaaaaaa` 取指令。

---

## 6. 被真实硬件否定的假设

排查过程中严格以重新编译、下载后的 UART/J-Link 结果为准。下面这些方向有合理性，但都没有通过板端验证，因此最终代码中不保留相应实验。

| 假设 | 实验或证据 | 板端结果 | 最终处理 |
|---|---|---|---|
| `apctl status` 业务逻辑直接破坏内存 | 对比空提示符、重复下载、AP READY 状态 | 不运行复杂 AP 操作也可能延迟故障 | 排除 |
| Flash 时钟太快 | 强制 Flash 使用 XTAL，日志确认时钟切换 | HardFault 仍复现 | 完整撤销 |
| Flash 临界区 BASEPRI 语义错误是直接根因 | 临时把 SDK `rtos_disable_int()` 改成 PRIMASK | 同样故障，fault 时不在该临界区 | 最终恢复原代码 |
| warm-download I-Cache 残留 | bootloader invalidate、CPU0 enable/invalidate 实验 | HardFault 仍复现 | 完整撤销 |
| SecureFault/SAU/安全属性错误 | 读取 SFSR/SFAR，核对 SVC vector | SFSR 为 0，vector 正确 | 排除 |
| RAM vector slot 64/65 未修复 | 同时读取 VTOR 和相关 slots | RAM vector 已正确修复为 `exception_common` | 排除 |
| FPU 是唯一根因 | 检查 `EXC_RETURN=0xffffffed`，准备过禁用实验 | J-Link 已直接发现 PC/LR=`0xaaaaaaaa`，FPU 假设不闭合 | 禁用实验撤销 |
| 2 KiB interrupt stack 溢出 | 临时扩大到 4 KiB | MSP 仍掉到低地址，故障不变 | 恢复 2 KiB |
| interrupt stack 必须移到 CP RAM 顶部 | 使用私有 linker symbols 和顶部栈 | RAM vector/栈布局变化，但故障仍出现 | 最终恢复原布局 |
| 通用 `arm_schedulesigaction.c` 没初始化 EXC_RETURN | 临时修改官方 NuttX FLAT signal context | 后续证据证明 `0xaaaaaaaa` 来自 NULL context restore | 官方修改撤销 |
| SVC 优先级本身太低 | 临时提升 SVC 优先级 | HardFault 仍复现 | 撤销 |

### 6.1 为什么必须记录“被否定的假设”

这不是无用过程。它能防止后续维护者再次重复以下错误：

- 看到 `IACCVIOL` 就立刻归因于 Flash/XIP；
- 看到低 MSP 就立刻认定中断栈溢出；
- 看到 `EXC_RETURN=0xffffffed` 就立刻禁用 FPU；
- 看到 fault 在 NuttX 通用文件附近就直接修改官方内核。

每个现象都可能是前一个错误的后果，而不是最初原因。

---

## 7. 第一条真实根因：`arm_doirq()` 返回 NULL

## 7.1 正常异常分发流程

NuttX ARMv8-M 异常处理可以简化为：

```text
硬件异常入口
  -> exception_common 汇编保存寄存器
  -> arm_doirq(irq, regs)
       -> irq_dispatch()
       -> handler / scheduler
       -> 选择最终应该恢复的 TCB context
  -> arm_doirq 返回最终 regs 指针
  -> exception_common 从 regs 恢复寄存器
  -> bx EXC_RETURN
```

这里最重要的契约是：

```text
arm_doirq() 返回值必须指向一份有效的寄存器 context
```

因为后面的汇编会直接把返回值当作地址使用。

## 7.2 D0 诊断得到的关键证据

为避免继续猜测，曾临时在 dispatcher 边界记录：

```text
D0 E=0000000f I=2804ff30
B=280021f0 P=00000000
T=280021f0 R=2804ff30 N=00000000
```

其中最关键的关系是：

```text
IRQ = 15                       -> SysTick
最终 TCB = 0x280021f0          -> 仍是同一个 IDLE TCB
最终 tcb->xcp.regs = 0x2804ff30 -> 有效
arm_doirq 返回值 = 0           -> NULL
```

也就是说：

```text
调度器知道应该恢复哪份 context
TCB 里也还看得到有效 context
但 arm_doirq 的 C 函数返回寄存器最终是 0
```

这解释了为什么问题主要出现在 no-switch 场景：中断处理后仍然运行原 task，但返回给汇编恢复器的指针丢失了。

## 7.3 NULL 如何变成 `0xaaaaaaaa`

`exception_common` 的恢复代码本质上类似：

```asm
ldmia r0!, {r2-r12, lr}
```

如果 `r0` 是有效 context 地址，例如 `0x2804ff30`，这条指令会从合法 SRAM 恢复寄存器。

如果 `r0=0`，它就会从地址 0 开始读取：

```text
[0x00000000] -> R2
[0x00000004] -> R3
...
某个低地址    -> LR
```

板端低地址内容是重复的：

```text
aaaaaaaa aaaaaaaa aaaaaaaa ...
```

于是恢复后：

```text
LR = 0xaaaaaaaa
随后异常返回/跳转目标也变成 0xaaaaaaaa
```

使用 FPU 扩展 context 时，汇编还会跳过或恢复额外约 `0x70` 字节，因此调试器可能看到 MSP 落在 `0x10`、`0x70` 等低地址。关键不是具体低地址是多少，而是共同特征：

```text
恢复起点来自 NULL
低地址被当成寄存器 context
PC/LR 最终变成 0xaaaaaaaa
```

最终故障链为：

```text
arm_doirq 返回 NULL
  -> exception_common 从地址 0 恢复寄存器
  -> LR/PC = 0xaaaaaaaa
  -> CPU 尝试从 0xaaaaaaaa 取指
  -> CFSR.IACCVIOL
  -> HFSR.FORCED HardFault
```

---

## 8. 第一版 fallback 为什么又出现 INVPC

## 8.1 新日志

加入“保存有效 context、NULL 时 fallback”后，原来的 `IACCVIOL` 变化为：

```text
CFSR = 0x00040000
EXC_RETURN = 0xfffffff1
xPSR.IPSR = 31
```

解释：

- `CFSR=0x00040000`：UsageFault `INVPC`，异常返回目标/模式组合无效；
- `EXC_RETURN=0xfffffff1`：返回 Handler mode、使用 MSP 的基本帧；
- `IPSR=31`：外层现场属于 UART1 IRQ。

这说明 NULL fallback 已经改变了故障，但上下文仍被混用。

## 8.2 SysTick 如何抢占 UART1

原优先级关系：

```text
SysTick = 0x80
UART1   = 0xc0
```

由于 `0x80` 比 `0xc0` 优先级高，可能发生：

```text
UART1 IRQ 进入 arm_doirq
  -> 尚未完成
  -> SysTick 抢占 UART1
       -> 内层再次进入 arm_doirq
       -> 覆盖全局 fallback 状态
       -> 内层返回 Handler context
  -> 回到外层 UART1
  -> 外层错误使用内层保存的 context
```

第一版代码还曾对保存的 context 无条件设置 `CONTROL.SPSEL`。当被保存的是：

```text
EXC_RETURN = 0xfffffff1  -> Handler mode / MSP
```

却又强制：

```text
CONTROL.SPSEL = 1       -> Thread mode 使用 PSP
```

恢复条件互相矛盾，因此产生 `INVPC`。

## 8.3 为什么通用 dispatcher 不能这样嵌套

当前 CP 配置没有启用：

```text
CONFIG_ARCH_HIPRI_INTERRUPT
```

这意味着当前走的是普通 ARMv8-M dispatcher，它维护的 `CURRENT_REGS`、TCB exception context 和调度状态按非嵌套路径设计。不能只通过设置更高的 NVIC 优先级，就假定普通 handler 可以安全重入调度器。

因此最终修复必须同时解决：

1. NULL context fallback；
2. fallback 所在 dispatcher 的非嵌套约束；
3. Thread context 和 Handler context 的区别。

---

## 9. 最终最小修复

## 9.1 链接 wrapper，而不是修改官方 NuttX

文件：

```text
board/bk7258_t5ai/chip/Make.defs
```

CPU0 链接参数：

```make
ifneq ($(CONFIG_BK7258_AP_CORE),y)
LDFLAGS += --wrap=arm_doirq --wrap=nxsched_resume_scheduler
endif
```

作用：

- CPU0 调用 `arm_doirq()` 时先进入 `__wrap_arm_doirq()`；
- CPU0 调用 `nxsched_resume_scheduler()` 时先进入对应 wrapper；
- wrapper 内仍调用 `__real_*` 原始 NuttX 函数；
- AP 镜像不受影响；
- 官方 `nuttx/` checkout 不需要任何修改。

调用关系变成：

```text
exception_common
  -> __wrap_arm_doirq
       -> __real_arm_doirq
            -> __wrap_nxsched_resume_scheduler
                 -> 保存最终 context
                 -> __real_nxsched_resume_scheduler
       -> NULL 时使用保存的 context
  -> exception_common 恢复有效 context
```

## 9.2 在 context 被清空前保存它

文件：

```text
board/bk7258_t5ai/chip/cp/bk7258_vectors.c
```

使用两个最小状态变量：

```c
static volatile uint32_t g_bk7258_doirq_active;
static volatile uint32_t g_bk7258_doirq_resume_regs;
```

`nxsched_resume_scheduler()` wrapper 的核心逻辑：

```c
void __wrap_nxsched_resume_scheduler(struct tcb_s *tcb)
{
  if (g_bk7258_doirq_active != 0)
    {
      uint32_t *regs = tcb != NULL ? tcb->xcp.regs : NULL;

      if (regs != NULL &&
          (regs[REG_EXC_RETURN] & BK7258_EXC_RETURN_THREAD_MODE) != 0)
        {
          regs[REG_CONTROL] |= 1u << 1; /* CONTROL.SPSEL */
#ifdef CONFIG_ARCH_FPU
          if ((regs[REG_EXC_RETURN] & BK7258_EXC_RETURN_BASIC_FRAME) == 0)
            {
              regs[REG_CONTROL] |= 1u << 2; /* CONTROL.FPCA */
            }
#endif
        }

      g_bk7258_doirq_resume_regs = (uint32_t)(uintptr_t)regs;
      __asm volatile ("dmb sy" ::: "memory");
    }

  __real_nxsched_resume_scheduler(tcb);
}
```

为什么在这里保存：

- 此时 scheduler 已经选定最终 TCB；
- `tcb->xcp.regs` 仍指向最终应恢复的 context；
- 调用真实 `nxsched_resume_scheduler()` 后，该字段可能被清空；
- 因此这里是丢失前最后一个可靠位置。

为什么只修改 Thread-mode context：

```c
(regs[REG_EXC_RETURN] & (1u << 3)) != 0
```

只有返回 Thread mode 的 task context 才应确保：

```text
CONTROL.SPSEL = 1
```

如果是 FPU 扩展帧，还应确保：

```text
CONTROL.FPCA = 1
```

Handler-mode `0xfffffff1` 不进入该分支，从而避免再次产生 `INVPC`。

## 9.3 在进入通用 dispatcher 前禁止普通 IRQ 嵌套

`arm_doirq()` wrapper 首先执行：

```c
uint32_t basepri = NVIC_SYSH_DISABLE_PRIORITY;

__asm volatile
  (
    "msr basepri, %0\n"
    "dsb sy\n"
    "isb sy\n"
    :
    : "r" (basepri)
    : "memory"
  );
```

BASEPRI 不会像 PRIMASK 那样关闭所有异常。它会屏蔽达到阈值的普通可屏蔽中断，同时仍允许 NMI、HardFault 等最高级异常工作。

`exception_common` 已经把异常进入前的 BASEPRI 保存到 context 中，并会在异常返回前恢复。因此 wrapper 不需要在 C 函数尾部猜测原值。

这段代码的目的不是“提高性能”或“改变调度策略”，而是在当前未启用 HIPRI dispatcher 的配置下明确维护：

```text
同一时刻只能有一层普通 arm_doirq 拥有 TCB exception context
```

## 9.4 `arm_doirq()` 返回 NULL 时恢复已保存指针

核心逻辑：

```c
g_bk7258_doirq_resume_regs = 0;
g_bk7258_doirq_active = 1;

regs = __real_arm_doirq(irq, regs);

g_bk7258_doirq_active = 0;

if (regs == NULL)
  {
    regs = (uint32_t *)(uintptr_t)g_bk7258_doirq_resume_regs;
  }
```

正常 switch/no-switch 路径返回有效指针时，不改变原返回值。

只有真实函数返回 NULL 时，才使用 scheduler 清空前保存的最终 context。

## 9.5 最终仍为空时必须 fail closed

最终防线：

```c
if (regs == NULL)
  {
    __asm volatile ("cpsid i" ::: "memory");
    bk7258_fault_stop_watchdogs();
    /* UART: HF D=00000000 */

    for (; ; )
      {
        __asm volatile ("wfe");
      }
  }
```

这条分支非常重要。即使以后出现新的 scheduler 边界问题，也不能再允许汇编从地址 0 恢复寄存器。

错误会被收敛为可识别日志：

```text
HF D=00000000
```

而不是继续发展成难以理解的：

```text
MSP = 0x10
PC  = 0xaaaaaaaa
LR  = 0xaaaaaaaa
```

## 9.6 统一 SDK device IRQ 优先级

文件：

```text
board/bk7258_t5ai/chip/common/bk7258_sdk_irq.h
```

默认逻辑优先级从 6 调整为 4：

```c
#define BK7258_SDK_IRQ_DEFAULT_PRIORITY 4
```

STAR 实现 3 个优先级位，逻辑优先级 4 编码后为：

```text
4 << 5 = 0x80
```

它与当前 SysTick 优先级一致。同优先级普通 IRQ 不会互相抢占。

文件：

```text
board/bk7258_t5ai/chip/cp/bk7258_sdk_irq.c
```

未启用 HIPRI dispatcher 时：

```c
#ifndef CONFIG_ARCH_HIPRI_INTERRUPT
  priority = BK7258_SDK_IRQ_DEFAULT_PRIORITY;
#endif
```

这意味着 SDK 即使请求 LCD 等更高优先级，在当前普通 dispatcher 配置下也会被限制到安全级别。未来若真正启用并适配 `CONFIG_ARCH_HIPRI_INTERRUPT`，该限制会自动取消，恢复 SDK 请求的优先级语义。

## 9.7 为什么 BASEPRI 和统一优先级都要保留

两者作用不同：

- **统一优先级**：从 NVIC 配置源头避免 SysTick、UART 和其他 SDK device IRQ 形成普通嵌套；
- **BASEPRI 临界窗口**：在 `arm_doirq()` 真正拥有全局/TCB context 的时间段提供运行期保护。

只改 UART 默认优先级不够，因为 SDK 其他模块还可能请求特殊高优先级。只设置 BASEPRI也不够，因为优先级 0 等高优先级请求可能绕过普通阈值。当前非 HIPRI 配置下，两层约束共同形成明确边界。

---

## 10. 为什么不修改 `arm_schedulesigaction.c`

排查中曾临时修改：

```text
nuttx/arch/arm/src/arm_m/arm_schedulesigaction.c
```

尝试在 FLAT build 的 signal context 中强制写入 `REG_EXC_RETURN` 和 `REG_CONTROL`。最终重新审查后已撤销，原因有三点。

### 10.1 它不是本次 `0xaaaaaaaa` 的来源

J-Link 和 D0 已经闭合：

```text
0xaaaaaaaa 来自 arm_doirq 返回 NULL 后从地址 0 恢复 context
```

不是 signal trampoline 创建 context 时漏写某个字段。

### 10.2 这是所有 Cortex-M 共用代码

`arm_schedulesigaction.c` 不是 BK7258 专属文件。如果确实存在通用缺陷，应先构造与芯片无关的复现，再走 NuttX 上游补丁，而不是把板级集成问题直接写进官方 checkout。

### 10.3 强制覆盖可能破坏正确 context

临时补丁读取 `getcontrol()` 时，当前运行 task 不一定就是目标 `tcb`。此外，强制设置一个固定 `EXC_RETURN` 可能覆盖目标 task 原有的：

- PSP/MSP 选择；
- FPU 基本帧/扩展帧状态；
- ARMv8-M 安全返回属性。

因此该修改既没有根因证据，也可能引入新的 context 错误。最终版本保持官方 NuttX 不变。

---

## 11. 最终清理结果

在第一次完整修复板测成功后，又执行了一轮“只保留被证据要求的修改”的重新审查。

已恢复的文件：

```text
board/bk7258_t5ai/bootloader/start.S
board/bk7258_t5ai/chip/common/bk7258_allocateheap.c
board/bk7258_t5ai/chip/common/bk7258_os_adapt.c
board/bk7258_t5ai/chip/cp/bk7258_start.c
board/bk7258_t5ai/configs/cp_nsh/defconfig
board/bk7258_t5ai/scripts/ld.script
nuttx/arch/arm/src/arm_m/arm_schedulesigaction.c
```

同时从最终 `bk7258_vectors.c` / `Make.defs` 中删除：

- 顶部私有 interrupt stack；
- `up_get_intstackbase()` wrapper；
- 新 naked reset wrapper；
- 无用的 `exc_return.h` include；
- 临时 D0 和多组上下文历史字段。

最终修改范围只剩 4 个团队 overlay 文件，和 §9 完全一致。

---

## 12. 板端验证

## 12.1 完整修复后的第一轮验证

用户在真实 T5-AI 板卡上确认：

- 反复下载后 CPU0 正常进入 NSH；
- 连续输入空提示符不再触发故障；
- 多次执行 `apctl status` 正常返回；
- AP 持续保持 `READY`；
- AP heartbeat 持续增长；
- 不再出现原来的 `IACCVIOL`；
- 不再出现 fallback 初版的 `INVPC`。

## 12.2 删除所有无关修改后的最终验证

清理官方 NuttX 修改、Cache/FPU/idle 实验、PRIMASK 实验、4 KiB 顶部中断栈和 reset wrapper 实验后，用户重新编译并下载最终最小版本，确认：

```text
验证成功
```

这一步非常重要，因为它证明成功不是由多个未解释实验“碰巧叠加”得到的，而是由最终保留的四文件修复直接实现。

最终状态标记为：

```text
BOARD-VERIFIED（2026-07-29，清理后的最小 overlay 修复）
```

---

## 13. 推荐的回归步骤

后续修改 CPU0 IRQ、scheduler adapter、UART 或 SysTick 后，建议执行：

1. 成套构建 CP/AP 镜像；
2. 多次重新下载，覆盖 warm-download 差异；
3. 等待 NSH 完整启动；
4. 连续输入多次空命令；
5. 连续运行：

   ```text
   apctl status
   ```

6. 确认每次都显示：

   ```text
   AP state=READY
   error=0
   ```

7. 间隔读取状态，确认 heartbeat 持续增加；
8. 观察 UART 中不能出现：

   ```text
   HF E=...
   HF D=00000000
   ```

`HF D=00000000` 比旧的 `0xaaaaaaaa` 崩溃更安全、更可诊断，但它仍代表 dispatcher 没有得到任何有效 context，不能当作通过。

---

## 14. 给初学者的调试经验

### 14.1 不要把“最后执行的命令”直接当根因

`apctl status` 触发了 task 创建和退出，所以更容易走到问题路径，但 AP 状态读取本身没有破坏 CPU0。

应继续追问：

```text
命令返回后，系统还执行了哪些 task-exit、SVC 和 context-restore 操作？
```

### 14.2 fault 中的 PC 不一定只有一份

异常嵌套时可能同时存在 PSP frame、MSP frame 和 NuttX 软件 context。UART recorder 读到的 PC 必须结合：

- 当前 EXC_RETURN；
- MSP；
- PSP；
- xPSR.IPSR；
- J-Link 实际 Handler frame；

一起解释。

### 14.3 低 MSP 不一定是栈溢出

本次 MSP 掉到 `0x10/0x70`，第一反应是中断栈耗尽。但真正过程是恢复 context pointer 为 NULL，随后汇编把低地址计算成了新 SP。

判断栈溢出至少需要同时验证：

- 正常中断栈边界；
- 栈使用量/填充水位；
- fault 前 SP 的变化过程；
- 恢复汇编是否可能直接写 MSP/PSP。

### 14.4 `0xaaaaaaaa` 是强证据

重复填充值通常表示：

- 未初始化内存；
- 栈染色区；
- poison pattern；
- 错误指针读到了不应作为结构体的数据。

看到 PC/LR 同时为 `0xaaaaaaaa` 时，应优先检查“寄存器从哪里恢复”，而不是继续围绕那个地址做 Flash 映射分析。

### 14.5 NVIC 数值越小，优先级越高

```text
0x80 会抢占 0xc0
```

这是本次第二层 `INVPC` 的关键。如果只看十进制大小，很容易把方向理解反。

### 14.6 普通 dispatcher 不等于可嵌套 dispatcher

硬件允许高优先级 IRQ 抢占，不代表操作系统的通用 IRQ 分发、scheduler 和 `CURRENT_REGS` 状态也支持嵌套。使用高优先级 IRQ 前必须确认对应架构配置和专用路径已经启用。

### 14.7 修改官方内核必须有通用证据

如果问题只在一个新板级移植中出现，应首先检查：

- reset contract；
- context layout；
- IRQ priority；
- SDK adapter；
- linker/栈边界；
- board wrapper。

只有能在通用 NuttX 配置中独立复现时，才应考虑修改官方共享文件并提交上游。

### 14.8 每轮实验必须能撤销

本次最终能够收敛到 4 个文件，是因为每个实验都记录了：

- 为什么做；
- 修改了什么；
- 板端输出是什么；
- 假设是否成立；
- 最终保留还是撤销。

“编译通过”只能证明语法和链接；真实板卡 UART/J-Link 才能证明硬件问题是否解决。

---

## 15. 最终根因链总图

```text
CPU0 SysTick/UART/SVC
        |
        v
 exception_common 保存 context
        |
        v
 arm_doirq() 进入调度/IRQ 分发
        |
        +---- no-switch 路径最终 TCB regs 有效
        |                 但 C 返回值观察为 NULL
        v
 exception_common 收到 r0=0
        |
        v
 从地址 0 执行 LDMIA 恢复寄存器
        |
        v
 LR/PC 被恢复为 0xaaaaaaaa
        |
        v
 IACCVIOL -> FORCED HardFault

第一版 fallback：
UART1 arm_doirq
        |
        +---- 被更高优先级 SysTick 抢占
                    |
                    v
             内层覆盖全局 context
                    |
                    v
外层错误恢复 Handler EXC_RETURN=0xfffffff1
并混入 Thread/PSP CONTROL
        |
        v
 INVPC

最终修复：
保存清空前的最终 regs
+ NULL 时 fallback
+ Thread/Handler 分开处理
+ BASEPRI 禁止普通 dispatcher 重入
+ SDK IRQ 与 SysTick 统一安全优先级
        |
        v
CPU0 task-exit / apctl / heartbeat 回归通过
```

---

## 16. 相关文档

- [N7 CPU1 单核 AP NuttX 启动链](n7-ap-singlecore-bringup.md)
- [BK7258 J-Link SWD 调试指南](../jlink-swd-debug-guide.md)
- [N6：约 4295 秒后 HF/WDT 重启根因](n6-bug-4295s-timer-wrap.md)
- [BK7258 文档主索引](../README.md)
