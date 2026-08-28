# 电源管理 Wakelock 使用指南

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1598&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:43  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/power_mgt/pm_wakelock_guide.md) | 简体中文 \]

本文档为 openvela 开发者详细介绍电源管理 (PM) 中的 <span class="reference">Wakelock</span> 机制。与在驱动层使用 <span class="reference">prepare</span> 回调的被动方式不同，<span class="reference">Wakelock</span> 允许应用层或中间件在预知系统需要保持活动状态时，**主动地**请求系统维持在指定的功耗水平，从而避免不必要的休眠。

**前置阅读**： 在开始之前，我们强烈建议您首先阅读[电源管理框架指南](https://doc.openvela.com/document?id=1596&version=dev-ai-contest-2026&language=cn)，以充分理解 PM 框架的核心概念。

# 一、核心思想与适用场景

在嵌入式开发中，某些任务（如文件写入、网络数据传输、固件升级）需要在一段时间内阻止系统进入低功耗状态，以确保任务的完整性和实时性。

传统方法是在设备驱动中实现 <span class="reference">prepare</span> 回调来否决休眠请求。然而，这种方式存在两个主要问题：

1.  **性能开销**：<span class="reference">prepare</span> 回调会在系统每次尝试进入低功耗状态时被调用，即使在非关键任务期间也会触发，增加了系统在空闲循环（Idle Loop）中的中断禁用时间和整体开销。
2.  **休眠失败率**：这是一种**事后**检查。PM 框架已经决定要休眠，才去询问驱动是否同意。如果驱动否决，会导致一次无效的功耗状态切换尝试和状态回退，影响电源管理的效率。

<span class="reference">Wakelock</span> 机制通过**主动请求**的方式解决了这些问题。当一个任务开始时，它获取一个 <span class="reference">Wakelock</span>；当任务结束时，它释放该 <span class="reference">Wakelock</span>。只有当系统中不存在任何活动的 <span class="reference">Wakelock</span> 时，PM 框架才会尝试让系统进入更深的休眠状态。

**简而言之，如果您能在业务逻辑层面预知何时需要阻止系统休眠，请优先使用 <span class="reference">Wakelock</span>。**

# 二、API 参考

Wakelock 功能通过 <span class="reference">pm.h</span> 中的一系列 <span class="reference">pm\_wakelock\_\*</span> 接口提供。

## <span class="reference">pm\_wakelock\_init</span>

初始化一个 <span class="reference">Wakelock</span> 实例。  

    void pm_wakelock_init(FAR struct pm_wakelock_s *wakelock,
                          FAR const char *name, int domain,
                          enum pm_state_e state)

  - **用途**：在一个模块或驱动的初始化阶段，调用此函数来创建一个具名的 <span class="reference">Wakelock</span>。

  - **参数**：
    
      - <span class="reference">wakelock</span>：指向用户定义的 <span class="reference">pm\_wakelock\_s</span> 结构体实例的指针。
      - <span class="reference">name</span>：锁的名称字符串。此名称将用于在 Procfs 中进行识别和调试。
      - <span class="reference">domain</span>：此 <span class="reference">Wakelock</span> 作用的目标电源域 (Power Domain)。
      - <span class="reference">state</span>：当此 <span class="reference">Wakelock</span> 被持有时，系统所能进入的**最低功耗状态**。例如，如果设置为 <span class="reference">PM\_IDLE</span>，则持有该锁会阻止系统进入 <span class="reference">PM\_STANDBY</span> 或 <span class="reference">PM\_SLEEP</span>。

## <span class="reference">pm\_wakelock\_stay</span>

锁定指定的 <span class="reference">Wakelock</span>。  

    void pm_wakelock_stay(FAR struct pm_wakelock_s *wakelock)

  - **用途**：在关键操作开始前调用此函数，通知 PM 框架系统需要保持活动。
  - **说明**：此函数内部维护一个引用计数。您可以多次调用 <span class="reference">pm\_wakelock\_stay</span>，但必须有对应次数的 <span class="reference">pm\_wakelock\_relax</span> 调用才能完全释放该锁。

## <span class="reference">pm\_wakelock\_relax</span>

解锁指定的 <span class="reference">Wakelock</span>。  

    void pm_wakelock_relax(FAR struct pm_wakelock_s *wakelock)

  - **用途**：在关键操作完成后调用此函数，通知 PM 框架系统可以正常进入休眠。
  - **说明**：此函数会使 <span class="reference">Wakelock</span> 的引用计数减一。只有当引用计数归零时，该 <span class="reference">Wakelock</span> 才会被彻底释放。它必须与 <span class="reference">pm\_wakelock\_stay</span> **成对使用**。

## <span class="reference">pm\_wakelock\_staytimeout</span>

获取一个带超时自动释放功能的 <span class="reference">Wakelock</span>。  

    void pm_wakelock_staytimeout(FAR struct pm_wakelock_s *wakelock, int ms);

  - **用途**：适用于那些需要保持系统活动一段固定时间的操作。
  - **说明**：调用此函数后，<span class="reference">Wakelock</span> 将被持有 <span class="reference">ms</span> 毫秒，之后自动释放。如果在超时前再次调用此函数，计时器将重置。

## <span class="reference">pm\_wakelock\_staycount</span>

获取 <span class="reference">Wakelock</span> 的当前引用计数。  

    int pm_wakelock_staycount(FAR struct pm_wakelock_s *wakelock);

  - **用途**：主要用于调试，可以在调用 <span class="reference">pm\_wakelock\_relax</span> 之前检查锁的当前持有次数，以避免非对称的解锁操作。

# 三、主要优势

采用 <span class="reference">Wakelock</span> 机制能为您的系统带来以下显著好处：

## 1、降低系统开销

通过主动锁定，避免了在每次系统空闲时遍历和调用所有驱动的 <span class="reference">prepare</span> 回调，从而有效缩短了关中断时间，提升了系统响应性。

## 2、提高电源管理效率

<span class="reference">Wakelock</span> 的状态检查在 PM 框架的早期阶段完成。这可以显著减少因驱动在 <span class="reference">prepare</span> 阶段否决而导致的功耗状态回退，使电源状态切换更加平滑和高效。

## 3、增强可调试性

如果开启了 Procfs (<span class="reference">CONFIG\_FS\_PROCFS</span>)，您可以通过访问 <span class="reference">/proc/pm/wakelock</span> 文件系统节点，实时查看系统中所有 <span class="reference">Wakelock</span> 的状态，包括其名称、持有计数以及累计维持时长，极大地简化了功耗问题的调试。

更多详情请参见 [PM procfs usage](https://doc.openvela.com/document?id=1599&version=dev-ai-contest-2026&language=cn)。
