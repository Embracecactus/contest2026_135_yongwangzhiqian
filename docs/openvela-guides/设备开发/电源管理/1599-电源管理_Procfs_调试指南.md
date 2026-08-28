# 电源管理 Procfs 调试指南

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1599&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:43  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/power_mgt/pm_procfs_debug.md) | 简体中文 \]

本文档指导开发者如何使用 openvela 系统中 Procfs (<span class="reference">/proc</span> 文件系统) 提供的电源管理 (PM) 接口。通过此接口，您可以实时监控每个电源域 (Power Domain) 的功耗状态分布，并诊断 <span class="reference">Wakelock</span> 的使用情况，是功耗优化与问题定位的强大工具。

**前置条件**：系统必须在 Kconfig 中启用 Procfs (<span class="reference">CONFIG\_FS\_PROCFS=y</span>)。

# 一、访问 PM 调试信息

您可以通过两种方式访问 PM 的调试信息：使用便捷的 <span class="reference">pmconfig</span> 命令，或直接读取 Procfs 中的文件。

## 方式 1：使用 <span class="reference">pmconfig</span> 命令 (推荐)

<span class="reference">pmconfig</span> 是一个封装好的命令行工具，它能聚合显示系统中所有电源域的状态和 <span class="reference">Wakelock</span> 信息，是查看 PM 概览的首选方法。

在目标设备的 Shell 中执行：  

    pmconfig

## 方式 2：直接访问 Procfs 文件

您也可以通过 <span class="reference">cat</span> 命令直接读取底层的 Procfs 文件。这种方法在需要脚本化处理或远程访问（如通过 <span class="reference">adb shell</span>）时非常有用。PM 信息文件位于 <span class="reference">/proc/pm/</span> 目录下，并按电源域 (Domain) 索引进行区分。

  - **状态文件**: <span class="reference">/proc/pm/state\<domain\_id\></span>
  - **Wakelock 文件**: <span class="reference">/proc/pm/wakelock\<domain\_id\></span>

**示例：在多核系统中查看各核心的 PM 信息**

假设系统包含一个应用处理器 (AP) 和一个通信处理器 (CP)，您可以通过以下方式查看：  

    # 在模拟器或 QEMU 等无挂载点的环境中，路径可能为 /proc/pm/...
    # 以下示例基于一个将远程核心文件系统挂载到 /mnt 的系统
    
    # 查看本地核心 (AP) 的 Domain 0 和 Domain 1 信息
    cat /mnt/ap/pm/state0
    cat /mnt/ap/pm/wakelock0
    cat /mnt/ap/pm/state1
    cat /mnt/ap/pm/wakelock1
    
    # 查看远程核心 (CP) 的 Domain 0 信息
    cat /mnt/cp/pm/state0
    cat /mnt/cp/pm/wakelock0

# 二、解读 Procfs 输出

本节详细解释 <span class="reference">state</span> 和 <span class="reference">wakelock</span> 文件内容的含义。

## 1、电源状态统计 (<span class="reference">/proc/pm/state\<N\></span>)

此文件展示了自系统启动以来，各个电源状态下所花费的时间。

**示例输出 (<span class="reference">/proc/pm/state0</span>)**：  

    //             执行时间         睡眠时间    该state下执行+睡眠时间
    DOMAIN0           WAKE         SLEEP         TOTAL
    normal         14s 01%       20s 02%       34s 04%
    idle            0s 00%        0s 00%        0s 00%
    standby         0s 00%        0s 00%        0s 00%
    sleep           0s 00%      712s 95%      712s 95%

**字段说明**：

<table>
<colgroup>
<col style="width: 50%" />
<col style="width: 50%" />
</colgroup>
<thead>
<tr class="header">
<th style="text-align: left;"><strong>字段</strong></th>
<th style="text-align: left;"><strong>描述</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;"><span class="reference">DOMAIN&lt;N&gt;</span></td>
<td style="text-align: left;">表头，指明这是哪个电源域的统计数据。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">normal</span>, <span class="reference">idle</span>, ...</td>
<td style="text-align: left;">系统支持的各个电源状态。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">WAKE</span></td>
<td style="text-align: left;"><strong>活动时间</strong>：CPU 在此状态下<strong>执行代码</strong>的总时间。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">SLEEP</span></td>
<td style="text-align: left;"><strong>休眠时间</strong>：CPU 在此状态下<strong>处于低功耗（如 WFI）等待</strong>的总时间。<br />
对于 <span class="reference">sleep</span> 状态，此值是衡量系统节能效果的关键指标。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">TOTAL</span></td>
<td style="text-align: left;"><strong>总时间</strong>：<span class="reference">WAKE</span> 时间与 <span class="reference">SLEEP</span> 时间之和，即在该状态下停留的总时长。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><strong>(百分比)</strong></td>
<td style="text-align: left;">该状态的总时间占系统运行总时间的百分比。</td>
</tr>
</tbody>
</table>

**分析示例**： 以上输出显示，<span class="reference">DOMAIN0</span> 自启动以来，有 95% 的时间都成功地进入了 <span class="reference">PM\_SLEEP</span> 状态，这表明系统的电源管理策略运行良好，实现了有效的节能。

## 2、Wakelock 统计 (<span class="reference">/proc/pm/wakelock\<N\></span>)

此文件列出了指定电源域中所有已注册的 <span class="reference">Wakelock</span> 及其当前状态和历史数据。

示例输出 (<span class="reference">/proc/pm/wakelock0</span>)：  

    //wakelock   state   当前stay次数  总stay时间
    DOMAIN0      STATE     COUNT      TIME
    system       normal        0       10s
    system       idle          0       10s
    system       standby       0       10s
    system       sleep         0       10s
    rptun-tee    idle          0        0s
    i2c          normal        0        1s
    rptun-cp     idle          0        0s
    rptun-sensor idle          0        1s
    rptun-audio  idle          0        0s
    gpu          normal        0        8s

**字段说明**：

<table>
<colgroup>
<col style="width: 50%" />
<col style="width: 50%" />
</colgroup>
<thead>
<tr class="header">
<th style="text-align: left;"><strong>字段</strong></th>
<th style="text-align: left;"><strong>描述</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;"><span class="reference">DOMAIN&lt;N&gt;</span></td>
<td style="text-align: left;">表头，指明这是哪个电源域的 <span class="reference">Wakelock</span> 列表。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">wakelock</span></td>
<td style="text-align: left;"><span class="reference">Wakelock</span> 的名称，在调用 <span class="reference">pm_wakelock_init</span> 时指定。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">STATE</span></td>
<td style="text-align: left;">此 <span class="reference">Wakelock</span> 生效时，将系统维持的<strong>最低功耗状态</strong>。例如，<span class="reference">normal</span> 表示它会阻止系统进入任何低功耗状态。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">COUNT</span></td>
<td style="text-align: left;"><strong>当前引用计数</strong>。<br />
如果此值<strong>大于 0</strong>，表示该锁<strong>当前正被持有</strong>，正在阻止系统进入更深的休眠状态。<br />
这是排查耗电问题的关键。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">TIME</span></td>
<td style="text-align: left;"><strong>累计持有时间</strong>。<br />
自系统启动以来，此 <span class="reference">Wakelock</span> 被持有的总时长。<br />
此值有助于识别哪些模块是历史上最主要的<strong>耗电大户</strong>。</td>
</tr>
</tbody>
</table>

**分析示例**： 以上输出表明，在 <span class="reference">DOMAIN0</span> 中：

  - 当前**没有**任何 <span class="reference">Wakelock</span> 处于活动状态（所有 <span class="reference">COUNT</span> 均为 0）。
  - 从历史上看，<span class="reference">system</span>、<span class="reference">i2c</span> 和 <span class="reference">gpu</span> 驱动或模块是主要的 <span class="reference">Wakelock</span> 使用者，累计持有时间分别为 10s、1s 和 8s。如果系统无法休眠，应首先检查这些模块的 <span class="reference">COUNT</span> 值。
