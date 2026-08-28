# up\_cpu\_wfi 实现指南

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1602&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:45  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/power_mgt/up_cpu_wfi_guide.md) | 简体中文 \]

函数 <span class="reference">up\_cpu\_wfi()</span> 是平台进入低功耗状态的核心，它由[使用 pm\_idle 标准化 Idle 线程的功耗管理](https://doc.openvela.com/document?id=1601&version=dev-ai-contest-2026&language=cn)示例代码中的 <span class="reference">up\_pm\_idle\_handler</span> 调用。该函数的实现与 CPU 架构紧密相关。本章节提供主流架构 (Cortex-M 和 RISC-V) 的参考实现和关键技术点的解析。

# 一、Cortex-M 架构

在 Cortex-M 架构中，尤其是在启用了零延迟中断 (<span class="reference">CONFIG\_ARCH\_ZOOLATENCY</span>) 的系统中，<span class="reference">up\_cpu\_wfi()</span> 的实现需要特别注意，以确保在进入 <span class="reference">WFI</span> (Wait For Interrupt) 状态前，所有中断（包括高优先级的零延迟中断）都已被正确屏蔽。

标准的 <span class="reference">up\_irq\_save()</span> 函数通常通过设置 <span class="reference">PRIMASK</span> 寄存器来屏蔽所有可屏蔽中断，但它无法屏蔽零延迟中断。因此，必须直接操作 <span class="reference">BASEPRI</span> 寄存器来临时提升中断屏蔽等级。

**参考实现**：[源码链接](https://github.com/FishsemiCode/nuttx/blob/song-u1/arch/arm/src/song/song_idle.c#L192-L225)  

    void up_cpu_wfi(void)
    {
    /* The WFI implementation is architecture-specific */
    #ifdef CONFIG_ARCH_CORTEXM4
    
      /* 
       * This implementation is required for systems that use BASEPRI for interrupt
       * management, especially when zero-latency interrupts are enabled.
       */
      int basepri = 0;
    
      /* Change BASEPRI to the minimal priority
       * value for waking up from PRIMASK == 1
       */
    
      __asm__ __volatile__
        (
    #ifdef CONFIG_ARMV7M_USEBASEPRI
          "\tcpsid i\n"               /* Disable interrupts globally */
    #endif
          "\tmrs %0, basepri\n"       /* Save current BASEPRI value */
          "\tmsr basepri, %1\n"       /* Set BASEPRI to block all maskable IRQs */
          "\tdsb\n"                   /* Ensure all memory accesses complete */
          "\twfi\n"                   /* Enter wait-for-interrupt state */
          "\tmsr basepri, %0\n"       /* Restore original BASEPRI value */
    #ifdef CONFIG_ARMV7M_USEBASEPRI
          "\tcpsie i\n"               /* Re-enable interrupts globally */
    #endif
          : "+r" (basepri)            /* Output/Input: original basepri value */
          : "r" (0xff)
          : "memory"
        );
    #else
      __asm__ __volatile__
        (
          "\tdsb\n"
          "\twfi\n"
        );
    #endif
    }

## 实现解析

1.  **<span class="reference">cpsid i</span>**：全局禁止中断。这是为了防止在修改 <span class="reference">BASEPRI</span> 期间，有中断（即使是低优先级的）被响应，从而造成竞态条件。
2.  **<span class="reference">mrs %0, basepri</span>**：将当前的 <span class="reference">BASEPRI</span> 寄存器值保存到变量 <span class="reference">basepri</span> 中。
3.  **<span class="reference">msr basepri, %1</span>**：设置 <span class="reference">BASEPRI</span> 为 <span class="reference">NVIC\_SYSH\_PRIORITY\_MIN</span>。该值通常是系统支持的最高优先级数值（即最低优先级），确保所有低于该优先级的中断都被屏蔽。这有效地屏蔽了包括零延迟中断在内的所有可屏蔽中断。
4.  **<span class="reference">dsb</span>**：数据同步屏障 (Data Synchronization Barrier)。确保所有在 <span class="reference">WFI</span> 指令之前的内存访问操作都已完成。
5.  **<span class="reference">wfi</span>**：执行 <span class="reference">Wait For Interrupt</span> 指令，使 CPU 进入低功耗状态，直到一个中断事件唤醒它。
6.  **<span class="reference">msr basepri, %0</span>**：CPU 从 <span class="reference">WFI</span> 唤醒后，立即恢复之前保存的 <span class="reference">BASEPRI</span> 值，使中断屏蔽恢复到正常状态。
7.  **<span class="reference">cpsie i</span>**：全局使能中断，与第一步的 <span class="reference">cpsid i</span> 对应。

# 二、RISC-V 架构

相比之下，RISC-V 架构的 <span class="reference">up\_cpu\_wfi</span> 实现通常更为简洁。标准的 <span class="reference">WFI</span> 指令足以使核心 (hart) 进入低功耗状态，等待中断唤醒。

**参考实现：**[源码链接](https://github.com/open-vela/nuttx/blob/31734b0c9ca1d021b03528d17b6d869e2392ff74/arch/risc-v/src/common/riscv_idle.c#L50-L75)  

    void up_cpu_wfi(void)
    {
      __asm__ volatile("wfi");
    }

## 实现解析

在 RISC-V 中，当 <span class="reference">up\_idle</span> 函数被调用时，操作系统已经通过 <span class="reference">up\_irq\_save()</span> 禁用了全局中断（通常通过操作 <span class="reference">mstatus</span> 寄存器的 <span class="reference">MIE</span> 位）。因此，在 <span class="reference">up\_cpu\_wfi()</span> 中，您只需执行 <span class="reference">wfi</span> 指令。处理器将暂停执行，直到一个外部中断、本地中断或调试请求变为挂起状态。
