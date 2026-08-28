# 使用 VSCode 调试 SIM 环境

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1621&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:55  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/debugging_tools/GDB/VSCODE_debugging.md) | 简体中文 \]

# 一、概述

本指南详细阐述了如何在 Visual Studio Code (VSCode) 中配置和使用 GDB，以实现对 **openvela** <span class="reference">sim</span> 仿真环境的图形化调试。通过 VSCode，您可以获得现代化的调试体验，包括设置断点、查看调用栈、监视变量和内存，从而显著提升开发与排错效率。

**核心流程包括：**

1.  **环境准备**：安装必要的 VSCode 扩展。
2.  **项目配置**：创建并配置 <span class="reference">launch.json</span> 文件，以告知 VSCode 如何启动调试会话。
3.  **实战调试**：通过一个实际案例，演示如何启动调试、设置断点并分析程序状态。
4.  **高级主题**：解决在特定场景（如 SMP、网络功能）下可能遇到的问题。

# 二、准备工作

在开始调试前，请确保您的开发环境满足以下要求。

## 1、环境要求

  - **Visual Studio Code**：已安装。
  - **C/C++ 扩展**：这是 VSCode 提供 C/C++ 语言支持和调试能力的核心插件。
  - **已编译的 <span class="reference">sim</span> 目标**：已成功编译 openvela 的 <span class="reference">sim</span> 版本，并确保生成了包含调试信息的可执行文件 (<span class="reference">nuttx</span>)。编译时必须包含 <span class="reference">-g</span> 或 <span class="reference">-g3</span> 标志。

## 2、VSCode 环境设置

### 步骤 1：安装 C/C++ 扩展

在 VSCode 的扩展市场中搜索 <span class="reference">C/C++</span>（由 Microsoft 发布），并单击安装。

### 步骤 2：打开项目工作区

启动 VSCode，通过菜单 <span class="reference">File \> Add Folder to Workspace...</span>，将您的 openvela 项目根目录添加进来。这能确保 VSCode 正确解析 <span class="reference">launch.json</span> 中的 <span class="reference">${workspaceFolder}</span> 变量。

# 三、调试配置 (<span class="reference">launch.json</span>)

<span class="reference">launch.json</span> 文件是 VSCode 调试功能的核心，它定义了如何启动和附加到您的程序。

## 1、创建 launch.json

1.  切换到 VSCode 的 "Run and Debug" 视图（快捷键 <span class="reference">Ctrl+Shift+D</span>）。
2.  单击 "create a launch.json file" 链接。
3.  在弹出的选择框中，选择 **C++** **(GDB/LLDB)**。
4.  VSCode 将会自动生成一个 <span class="reference">launch.json</span> 模板文件，并保存在项目根目录的 <span class="reference">.vscode</span> 文件夹下。

## 2、配置 launch.json

将 <span class="reference">launch.json</span> 的内容替换为以下配置。此配置专门为调试 openvela <span class="reference">sim</span> 环境定制。  

    {
        "version": "0.2.0",
        "configurations": [
            {
                "name": "Debug openvela (sim)",
                "type": "cppdbg",
                "request": "launch",
                "program": "${workspaceFolder}/nuttx/nuttx",
                "stopAtEntry": false,
                "cwd": "${workspaceFolder}/nuttx",
                "environment": [],
                "console": "externalTerminal",
                "MIMode": "gdb",
                "setupCommands": [
                    {
                        "description": "Enable pretty-printing for gdb",
                        "text": "-enable-pretty-printing",
                        "ignoreFailures": true
                    }
                ]
            }
        ]
    }

### 配置项解析

<table>
<colgroup>
<col style="width: 33%" />
<col style="width: 33%" />
<col style="width: 33%" />
</colgroup>
<thead>
<tr class="header">
<th><strong>属性</strong></th>
<th><strong>值</strong></th>
<th><strong>说明</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><span class="reference">name</span></td>
<td><span class="reference">Debug openvela (sim)</span></td>
<td>调试配置的名称，将显示在 VSCode 的调试下拉菜单中。</td>
</tr>
<tr class="even">
<td><span class="reference">type</span></td>
<td><span class="reference">cppdbg</span></td>
<td>指定使用 C/C++ 扩展进行调试。</td>
</tr>
<tr class="odd">
<td><span class="reference">request</span></td>
<td><span class="reference">launch</span></td>
<td>表示这是一个 "启动" 型的调试会话，VSCode 将负责启动程序。</td>
</tr>
<tr class="even">
<td><span class="reference">program</span></td>
<td><span class="reference">${workspaceFolder}/nuttx/nuttx</span></td>
<td><strong>关键配置项</strong>。<br />
指定要调试的可执行文件的路径。<span class="reference">${workspaceFolder}</span> 代表您在 VSCode 中打开的项目根目录。</td>
</tr>
<tr class="odd">
<td><span class="reference">stopAtEntry</span></td>
<td><span class="reference">false</span></td>
<td>如果设为 <span class="reference">true</span>，程序会在入口点（如 <span class="reference">_start</span>）处自动暂停。<br />
通常设为 <span class="reference">false</span>，让程序直接运行到我们设置的断点。</td>
</tr>
<tr class="even">
<td><span class="reference">cwd</span></td>
<td><span class="reference">${workspaceFolder}/nuttx</span></td>
<td>设置被调试程序的工作目录。<br />
对于 <span class="reference">sim</span> 环境，这通常是包含 <span class="reference">nuttx</span> 可执行文件的目录。</td>
</tr>
<tr class="odd">
<td><span class="reference">console</span></td>
<td><span class="reference">externalTerminal</span></td>
<td>指定在一个<strong>外部终端</strong>中运行程序。<br />
这对于需要与 NuttShell (NSH) 交互的 <span class="reference">sim</span> 环境至关重要，您可以在该终端中输入命令。</td>
</tr>
<tr class="even">
<td><span class="reference">MIMode</span></td>
<td><span class="reference">gdb</span></td>
<td>指定使用的调试器后端为 GDB。</td>
</tr>
<tr class="odd">
<td><span class="reference">setupCommands</span></td>
<td><span class="reference">[...]</span></td>
<td>GDB 启动后、程序运行前执行的命令。<br />
这里默认启用了 "pretty-printing"，以更友好的格式显示 STL 等复杂数据结构。</td>
</tr>
</tbody>
</table>

# 四、调试实战

下面，我们以调试 <span class="reference">ping</span> 命令为例，走完整个流程。

## 步骤 1：打开源码并设置断点

在 VSCode 中，打开文件 <span class="reference">apps/system/ping/ping.c</span>。在 <span class="reference">ping\_main</span> 函数的入口处（或任意您感兴趣的行），点击行号左侧的空白区域，设置一个红点断点。

## 步骤 2：启动调试会话

按下 <span class="reference">F5</span> 键或点击 "Run and Debug" 视图中的绿色启动按钮。VSCode 将：

  - 启动 GDB。
  - 打开一个新的外部终端窗口。
  - 在该终端中运行 <span class="reference">nuttx</span> 程序，您会看到 NSH 的启动提示符 <span class="reference">nsh\></span>。

## 步骤 3：触发断点

在 <span class="reference">nsh\></span> 提示符所在的**外部终端**中，输入触发断点的命令：  

    nsh> ping 127.0.0.1

## 步骤 4：分析程序状态

当程序执行到 <span class="reference">ping\_main</span> 时，您会看到：

  - VSCode 窗口自动获得焦点。
  - 代码视图中的断点行高亮显示。
  - 左侧的调试面板中填充了实时信息：
      - **VARIABLES**：显示当前作用域内的局部变量和全局变量的值。
      - **WATCH**：您可以添加表达式来持续监视其值的变化。
      - **CALL STACK**：清晰地展示了函数调用栈，帮助您理解程序的执行路径。
      - **BREAKPOINTS**：管理您设置的所有断点。

# 五、高级主题与常见问题

## 1、处理 SMP 调试中的 <span class="reference">SIGUSR1</span> 信号

### 问题现象

如果您的 openvela 配置启用了对称多处理（SMP），在调试时程序可能会在启动后不久就因 <span class="reference">SIGUSR1</span> 信号而意外暂停。

### 原因分析

openvela 在 SMP 模式下使用 <span class="reference">SIGUSR1</span> 信号进行核间任务调度和通信。默认情况下，GDB 会捕获所有信号并暂停程序，这干扰了系统的正常运行。

### 解决方案

您可以通过创建 GDB 的全局初始化脚本，让它忽略此信号。

1.  创建一个文件：<span class="reference">\~/.gdbinit</span>（位于您的用户主目录下）。

2.  在该文件中添加以下命令：  
    
        # Instruct GDB to not stop or print a message for SIGUSR1
        handle SIGUSR1 nostop noprint

3.  保存文件。GDB 在每次启动时都会自动加载并执行此文件中的命令，从而解决了该问题。

## 2、为 <span class="reference">sim</span> 获取 Root 权限

### 问题场景

<span class="reference">sim</span> 环境的某些高级功能，特别是网络相关的（如使用 TAP 设备与主机系统通信），需要 root 权限才能正常工作。直接使用 <span class="reference">sudo F5</span> 是不可行的。

### 解决方案

推荐的方案是配置 <span class="reference">sudo</span>，允许您的用户账户在不输入密码的情况下以 root 身份运行 <span class="reference">gdb</span>。

1.  配置免密 <span class="reference">sudo</span>。
    
    为了安全和规范，我们通过在 <span class="reference">/etc/sudoers.d/</span> 目录下创建特定配置文件来实现。这种方法比直接修改主 <span class="reference">sudoers</span> 文件更安全。
    
    在您的 Linux 终端中执行以下命令，将 <span class="reference">your\_username</span> 替换为您的实际用户名：  
    
        # 使用您的用户名替换 your_username
        echo "your_username ALL=(ALL) NOPASSWD: /usr/bin/gdb" | sudo tee /etc/sudoers.d/gdb-nopasswd

2.  创建 GDB 脚本。
    
    在您的项目根目录下（例如 <span class="reference">openvela/</span>），创建一个名为 <span class="reference">sudo-gdb.sh</span> 的文件，并填入以下内容：  
    
        #!/bin/bash
        # This script acts as a wrapper to launch gdb with sudo.
        sudo /usr/bin/gdb "$@"
    
    然后，赋予此脚本可执行权限：  
    
        chmod +x sudo-gdb.sh

3.  修改 <span class="reference">launch.json</span>。
    
    修改 <span class="reference">.vscode/launch.json</span> 文件，在您的调试配置中添加 <span class="reference">"miDebuggerPath"</span> 属性，使其指向我们刚刚创建的脚本。  
    
        {
            "name": "Debug openvela (sim) with Root",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/nuttx/nuttx",
            "miDebuggerPath": "${workspaceFolder}/sudo-gdb.sh", // <-- 添加此行
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}/nuttx",
            "environment": [],
            "console": "externalTerminal",
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ]
        }

完成以上步骤后，选择新的调试配置并按 <span class="reference">F5</span> 启动，您的 <span class="reference">sim</span> 程序就会以 root 权限运行。

# 六、参考资料

  - [Visual Studio Code Docs: Debugging](https://code.visualstudio.com/docs/editor/debugging)
  - [MCU on Eclipse: VS Code for C/C++ with ARM Cortex-M](https://mcuoneclipse.com/2021/05/01/visual-studio-code-for-c-c-with-arm-cortex-m-part-1/)
  - [MCU on Eclipse: VS Code Data Breakpoints and Watchpoints](https://mcuoneclipse.com/2023/11/14/vs-code-data-breakpoints-and-watchpoints/)
  - [Gunnar Peipman's Blog: Browse WSL files with Windows Explorer](https://gunnarpeipman.com/browse-wsl-with-explorer/)
