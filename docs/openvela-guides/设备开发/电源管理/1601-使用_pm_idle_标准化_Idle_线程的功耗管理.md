# 使用 pm\_idle 标准化 Idle 线程的功耗管理

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1601&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:45  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/power_mgt/pm_idle_guide.md) | 简体中文 \]

# 一、概述

本文档为嵌入式系统开发者提供在 openvela 实时操作系统中，使用 <span class="reference">pm\_idle</span> 接口实现标准化空闲 (Idle) 线程功耗管理的方法。

openvela 提供 <span class="reference">pm\_idle</span> 接口，旨在为**单核** (Uniprocessor, UP) 和**对称多处理** (Symmetric Multiprocessing, SMP) 架构提供统一、标准的 Idle 线程处理流程。该接口封装了复杂的电源状态决策和多核同步逻辑，可显著简化特定于平台的 (Platform-specific) <span class="reference">up\_idle</span> 函数实现，降低开发风险。

**目标读者：** 负责实现或维护平台底层电源管理逻辑的嵌入式软件工程师。

**前置阅读**： 在开始之前，我们强烈建议您首先阅读以下文档，以了解 openvela 电源管理的基础概念：

  - [电源管理框架指南](https://doc.openvela.com/document?id=1596&version=dev-ai-contest-2026&language=cn)
  - [在 IDLE 线程中实现电源管理](https://doc.openvela.com/document?id=1600&version=dev-ai-contest-2026&language=cn)

# 二、API 参考

<span class="reference">pm\_idle</span> 接口定义根据系统是否启用 SMP (<span class="reference">CONFIG\_SMP</span>) 而有所不同。

## 1、单核 (UP) 场景

在单核场景下，系统只管理一个全局的电源状态（System State）。开发者仅需提供一个回调函数来响应此状态。

### 函数指针： <span class="reference">pm\_idle\_handler\_t</span>

定义一个回调函数，用于处理系统进入不同功耗状态前的操作。  

    typedef void (*pm_idle_handler_t)(enum pm_state_e systemstate);

  - <span class="reference">systemstate</span>： <span class="reference">enum pm\_state\_e</span> 类型，表示系统将要进入的目标电源状态，例如 <span class="reference">PM\_SLEEP</span>。

### 核心函数： <span class="reference">pm\_idle</span>

在系统的 Idle 循环 (<span class="reference">up\_idle</span>) 中调用此函数。它会计算当前系统可进入的最低功耗状态，并调用您提供的 <span class="reference">handler</span>。  

    void pm_idle(pm_idle_handler_t handler);

  - <span class="reference">handler</span>：<span class="reference">pm\_idle\_handler\_t</span> 类型的函数指针，指向平台相关的电源状态处理回调函数。

## 2、多核 (SMP) 场景

在 SMP 场景下，每个 CPU Core 拥有独立的电源状态（CPU State），同时整个系统也存在一个共享的电源状态（System State）。<span class="reference">pm\_idle</span> 接口扩展了其功能以协调多核行为。

### 函数指针： <span class="reference">pm\_idle\_handler\_t</span>

回调函数的定义增加了 <span class="reference">cpu</span> 和 <span class="reference">cpustate</span> 参数，以处理特定核心的状态，并返回一个布尔值，用于标识该核心是否为最后一个唤醒的核心。  

    typedef bool (*pm_idle_handler_t)(int cpu,
                                      enum pm_state_e cpustate,
                                      enum pm_state_e systemstate);

  - <span class="reference">cpu</span>：当前执行此回调函数的 CPU 核心 ID。
  - <span class="reference">cpustate</span>：<span class="reference">enum pm\_state\_e</span> 类型，表示当前核心将要进入的电源状态。
  - <span class="reference">systemstate</span>：<span class="reference">enum pm\_state\_e</span> 类型，表示当所有核心都进入 Idle 后，系统将进入的共享电源状态。
  - 返回值：<span class="reference">bool</span> 类型。如果当前核心是第一个从 <span class="reference">WFI</span> 状态唤醒并负责恢复系统级资源的核心，则返回 <span class="reference">true</span>；否则返回 <span class="reference">false</span>。

下图展示了 <span class="reference">pm\_idle</span> 如何与平台代码（<span class="reference">chip\_idle\_...</span>）和用户回调（<span class="reference">pm\_idle\_handler\_cb</span>）协作，共同完成一次完整的 SMP Idle 流程。

![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005757298_011.png)

### 核心函数： <span class="reference">pm\_idle</span>

与单核版本类似，在每个核心的 Idle 循环中调用。  

    void pm_idle(pm_idle_handler_t handler);

  - <span class="reference">handler</span>：<span class="reference">pm\_idle\_handler\_t</span> 类型的函数指针，指向平台相关的电源状态处理回调函数。

### 多核同步接口

在 SMP 场景中，为确保各核心能安全地进入和退出低功耗状态，<span class="reference">pm\_idle</span> 框架内部管理着核心间的同步锁。但在平台相关的回调函数 (<span class="reference">handler</span>) 中，您必须在精确的时机手动调用 <span class="reference">pm\_idle\_unlock</span> 和 <span class="reference">pm\_idle\_lock</span> 来配合框架完成同步。

其核心机制是：

1.  <span class="reference">pm\_idle</span> 框架在调用您的 <span class="reference">handler</span> **之前**获取锁。
2.  您的 <span class="reference">handler</span> 在进入 <span class="reference">WFI</span> **之前**调用 <span class="reference">pm\_idle\_unlock()</span> 释放锁。
3.  您的 <span class="reference">handler</span> 在从 <span class="reference">WFI</span> **唤醒后**调用 <span class="reference">pm\_idle\_lock()</span> 重新获取锁，并借此判断自己是否为第一个唤醒的核心。
4.  <span class="reference">pm\_idle</span> 框架在 <span class="reference">handler</span> **返回后**最终释放锁。

-----

<span class="reference">pm\_idle\_unlock</span>

在进入 <span class="reference">WFI</span> (Wait For Interrupt) 指令**之前**调用。此函数释放核心间的同步锁，允许其他核心继续其 <span class="reference">pm\_idle</span> 流程。调用此函数后，不应再执行任何依赖多核同步的操作（例如访问共享资源）。  

    void pm_idle_unlock(void);

<span class="reference">pm\_idle\_lock</span>

在从 <span class="reference">WFI</span> 指令唤醒**之后**立即调用。此函数重新获取核心间的同步锁，并判断当前核心是否为第一个被唤醒的核心。  

    bool pm_idle_lock(int cpu);

# 三、实现指南

参考[在 IDLE 线程中实现电源管理](https://doc.openvela.com/document?id=1600&version=dev-ai-contest-2026&language=cn)的实现，<span class="reference">pm\_idle.c</span> 中将流程进行了标准化，只暴露了 <span class="reference">handler</span> 作为参考IDLE 线程中 <span class="reference">switch</span> 部分的处理。

## 1、单核 (UP) 场景实现

在单核系统中，<span class="reference">up\_idle</span> 的实现非常直接。您只需将平台相关的低功耗指令（如 <span class="reference">WFI</span>）封装在回调函数 <span class="reference">up\_pm\_idle\_handler</span> 中，并将其传递给 <span class="reference">pm\_idle</span>。  

    /*
     * 定义平台相关的电源状态处理函数。
     * 在所有支持的低功耗状态下，都执行 WFI 指令使 CPU 等待中断。
     */
    static void up_pm_idle_handler(enum pm_state_e state)
    {
      switch (state)
        {
          case PM_NORMAL:
          case PM_IDLE:
          case PM_STANDBY:
          case PM_SLEEP:
          default:
            /* 执行让 CPU 进入低功耗等待状态的指令 */
            up_cpu_wfi();
            break;
        }
    }
    
    /*
     * 实现 OS 的 Idle 线程主函数。
     * 在循环中调用 pm_idle，将电源管理逻辑委托给 PM 框架。
     */
    void up_idle(void)
    {
      pm_idle(up_pm_idle_handler);
    }

## 2、多核 (SMP) 场景实现

在 SMP 系统中，<span class="reference">handler</span> 的实现更为复杂，因为它必须同时处理 CPU Domain 和 System Domain 的电源状态转换，并正确使用 <span class="reference">lock</span>/<span class="reference">unlock</span> 接口进行同步。

### 工作流程

下图详细展示了 <span class="reference">pm\_idle</span> 在 SMP 场景下的内部逻辑，以及 <span class="reference">pm\_idle</span> 与平台回调函数 <span class="reference">handler</span> 之间的交互时序。

![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005757405_012.png)

### 代码示例

以下示例演示了典型的 SMP <span class="reference">handler</span> 实现结构，其步骤与上述工作流程图一一对应。  

    static bool up_pm_idle_handler(int cpu,
                                   enum pm_state_e cpu_state,
                                   enum pm_state_e system_state)
    {
      bool first = false;
      switch (cpu_state)
        {
          case PM_NORMAL:
          case PM_IDLE:
          case PM_STANDBY:
          case PM_SLEEP:
    
            /*
             * 步骤 1: 执行 CPU Domain 的低功耗准备操作。
             * 例如：关闭该核心的特定时钟或调节其电压。
             * 此时多核同步锁仍被持有。
             */
            /* do cpu domain pm enter operations */
            asm("NOP");
    
    
            /* 
             * 步骤 2: 如果系统状态有效，执行System Domain的低功耗准备操作。
             * pm_idle 内部机制确保此部分逻辑通常仅由最后一个进入 idle 的核心执行。
             */
            if (system_state >= PM_NORMAL)
              {
                switch (system_state)
                  {
                    case PM_NORMAL:
                    case PM_IDLE:
                    case PM_STANDBY:
                    case PM_SLEEP:
    
                      /* do system domain pm enter operations */
    
                      asm("NOP");
    
                      break;
                    default:
                      break;
                  }
              }
    
            /*
             * 步骤 3: 释放多核同步锁，准备进入 WFI。
             * 此后不能再执行需要多核同步的操作。
             */
            pm_idle_unlock();
    
            /*
             * 步骤 4: 执行 WFI 指令，CPU 将在此处暂停，直到中断发生。
             */
            up_cpu_wfi();
            
            /*
             * 步骤 5: 从 WFI 唤醒后，立即获取多核同步锁。
             * 函数返回 true 表示本核心是第一个唤醒的。
             */
            first = pm_idle_lock(cpu);
            
            /*
             * 步骤 6: 如果是第一个唤醒的核心，执行恢复系统级共享资源的操作。
             *
             */
            if (first)
              {
                /* do system domain pm leave operations */
    
                asm("NOP");
              }
    
            /*
             * 步骤 7: 执行 CPU 域的恢复操作。
             * 此时多核同步锁已重新持有。
             */
            /* do cpu domain pm leave operations */
    
            asm("NOP");
    
            break;
          default:
            break;
        }
    
      /* 返回唤醒状态，通知 pm_idle 框架本核心是否为第一个唤醒者 */
      return first;
    }
    
    void up_idle(void)
    {
      pm_idle(up_pm_idle_handler);
    }

# 四、驱动适配指南

当驱动程序需要响应电源状态变化时，您需要将其回调函数注册到正确的电源域 (Power Domain)。

## System Domain (<span class="reference">PM\_IDLE\_DOMAIN</span>)

  - **行为**: <span class="reference">system\_state</span> 的状态变化会通知注册到 <span class="reference">PM\_IDLE\_DOMAIN</span> 的驱动。
  - **兼容性**: 为了与单核用法保持兼容，标准的 <span class="reference">pm\_register</span> 和 <span class="reference">pm\_unregister</span> 接口默认将回调注册到此域。
  - **用途**：适用于需要响应系统级（所有核心共享的）电源状态变化的驱动，例如操作主内存控制器或共享总线。

**注意**：如果您希望驱动回调 (<span class="reference">struct pm\_callback\_s</span>) 能接收来自其他特定域的状态变化通知，必须使用 <span class="reference">pm\_domain\_register</span> / <span class="reference">pm\_domain\_unregister</span> 接口，并明确指定 <span class="reference">domain</span> ID。

## CPU Domain

  - **行为**: 如果驱动程序或其控制的硬件与某个特定的 CPU Core 强相关，您应将其注册到该核心对应的 CPU Domain。

  - **获取 Domain ID**: 使用 <span class="reference">PM\_SMP\_CPU\_DOMAIN(cpu)</span> 宏来获取指定核心的 Domain ID。  
    
        #  define PM_SMP_CPU_DOMAIN(cpu) (CONFIG_PM_NDOMAINS - CONFIG_SMP_NCPUS + (cpu))
            
        /* 获取当前核心的 Domain ID */
        int domain = PM_SMP_CPU_DOMAIN(this_cpu());
            
        /* 使用 domain ID 注册回调 */
        pm_domain_register(domain, &my_driver_pm_cb);

  - **用途**: 适用于管理仅由单个核心使用的外设，例如核心私有的定时器 (per-core timer) 或中断控制器。
