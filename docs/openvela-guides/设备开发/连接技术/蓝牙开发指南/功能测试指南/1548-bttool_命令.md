# bttool 命令

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1548&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:16  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/connection/bluetooth/functionality_test/bttool_cmd.md) | 简体中文 \]

# 一、简介

在 openvela的 <span class="reference">NSH</span> 命令行中执行，用于进入蓝牙命令工具的 Console。在 Console 中，可以执行 <span class="reference">bttool</span> 工具内集成的特有的子命令。

# 二、语法

<table>
<colgroup>
<col style="width: 33%" />
<col style="width: 33%" />
<col style="width: 33%" />
</colgroup>
<thead>
<tr class="header">
<th style="text-align: left;"><strong>语法元素</strong></th>
<th style="text-align: left;"><strong>说明</strong></th>
<th style="text-align: left;"><strong>示例</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;">不含方括号或大括号的文本</td>
<td style="text-align: left;">必须按所显示键入的项。</td>
<td style="text-align: left;"><span class="reference">cd</span><br />
命令中的 <span class="reference">cd</span> 部分就是必须原样键入的。</td>
</tr>
<tr class="even">
<td style="text-align: left;">&lt;尖括号内的文本&gt;</td>
<td style="text-align: left;">必须为其提供值的占位符。</td>
<td style="text-align: left;"><span class="reference">mkdir &lt;directory_name&gt;</span><br />
命令中的 <span class="reference">&lt;directory_name&gt;</span> 需要被替换成实际的目录名。</td>
</tr>
<tr class="odd">
<td style="text-align: left;">[方括号内的文本]</td>
<td style="text-align: left;">可选项。</td>
<td style="text-align: left;"><span class="reference">ls [-l]</span><br />
命令中的 <span class="reference">[-l]</span> 是一个可选项，表示是否以长列表格式显示文件。</td>
</tr>
<tr class="even">
<td style="text-align: left;">{花括号内的文本}</td>
<td style="text-align: left;">一组必需的项， 必须选择一个。</td>
<td style="text-align: left;"><span class="reference">git reset { --soft | --mixed | --hard }</span><br />
必须选择这三个选项中的一个，例如 <span class="reference">git reset --soft</span>。</td>
</tr>
<tr class="odd">
<td style="text-align: left;">竖线 |</td>
<td style="text-align: left;">互斥项的分隔符，必须选择一个。</td>
<td style="text-align: left;"><span class="reference">git reset { --soft | --mixed | --hard }</span><br />
<span class="reference">--soft</span>, <span class="reference">--mixed</span>, <span class="reference">--hard</span>只能选其中一个。</td>
</tr>
<tr class="even">
<td style="text-align: left;">省略号 …</td>
<td style="text-align: left;">可重复使用多次的项。</td>
<td style="text-align: left;"><span class="reference">cp &lt;file1&gt; &lt;file2&gt; … &lt;destination&gt;</span><br />
省略号表示可以复制多个文件到目的地。</td>
</tr>
</tbody>
</table>

# 三、示例

本示例介绍在 <span class="reference">NSH</span> 命令行打开 <span class="reference">bttool</span>。

## 前提条件

已进入 <span class="reference">NSH</span> 操作界面。

## 命令输入

    ap> bttool

## 输出信息

终端显示 <span class="reference">bttool\></span> 提示符，进入 bttool Console。  

    [    8.232900] [51] [ DEBUG] [ap] thread_schedule_loop:0xf0b10580, async:0xf1b0c740
    [    8.233900] [51] [ DEBUG] [ap] set_ready
    [    8.234400] [50] [ DEBUG] [ap] bt_client_50 loop running now !!!
    bttool>
