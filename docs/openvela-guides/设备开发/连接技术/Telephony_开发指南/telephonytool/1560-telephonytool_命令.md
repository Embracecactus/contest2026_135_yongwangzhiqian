# telephonytool 命令

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1560&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:23  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/connection/telephony/telephonytool/telephonytool_cmd_desc.md) | 简体中文 \]

# 一、概述

<span class="reference">telephonytool</span> 是一个在 openvela 的 NSH 命令行中执行的工具，用于进入 Telephony 命令工具的控制台（Console）。在控制台中，可以执行 <span class="reference">telephonytool</span> 工具内集成的特定子命令。

# 二、语法

以下是命令行语法的规则说明：

| **表示法**      | **说明**          | **示例**                                                                                                             |
| :----------- | :-------------- | :----------------------------------------------------------------------------------------------------------------- |
| 不含方括号或大括号的文本 | 需要按所显示内容原样键入。   | <span class="reference">hold\_and\_answer</span> 命令中的 <span class="reference">hold\_and\_answer</span> 部分必须原样键入。   |
| \[方括号内的文本\]  | 表示占位符，需要用实际值替换。 | <span class="reference">hangup-all \[slot\_id\]</span> 命令中的 <span class="reference">\[slot\_id\]</span> 需要替换为实际的值。 |

# 三、示例

以下示例展示如何在 NSH 命令行中打开 <span class="reference">telephonytool</span> 工具。

## 1、命令输入

    ap> telephonytool

## 2、输出信息

执行命令后，终端会显示 <span class="reference">telephonytool\></span> 提示符，表示已成功进入 <span class="reference">telephonytool</span> 控制台。以下是示例输出：  

    goldfish-armv7a-ap> telephonytool
    [  177.780000] [31] [  WARN] [ap] Successfully connected to unix socket /var/run/dbus/system_bus_socket
    [  177.847300] [31] [ DEBUG] [ap] [async_queue:85]uv_async_queue_init
    telephonytool> [  178.167200] [31] [  INFO] [ap] enable_modem_abnormal_event_done:0
    [  178.246700] [31] [  INFO] [ap] on_modem_property_change - from 0 to 1
    [  178.258900] [31] [ DEBUG] [ap] tapi is ready for vela.telephony.tool
