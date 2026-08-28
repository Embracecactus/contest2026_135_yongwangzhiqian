# openvela VS Code 插件使用指南

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1427&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:50:15  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/quickstart/vscode_plugin_usage.md) | 简体中文 \]

本文档指导开发者在 Ubuntu 环境下安装 openvela VS Code 插件，并完成 openvela 项目的创建、编译、调试及应用开发。

# 一、环境准备

在开始之前，请确保开发环境满足以下软硬件要求。

## 1、硬件配置

  - **硬盘**：至少 **40 GB** 可用空间（用于存放源代码及编译产物）。
  - **内存**：至少 **16** **GB** RAM。

## 2、操作系统

  - **系统版本**：Ubuntu 22.04 (支持 arm64 或 x86\_64 架构)。

# 二、安装 openvela 扩展插件

> **注意**：调试功能依赖 C++ 插件，因此必须在 VS Code 中进行安装和运行。

在 VS Code(版本 \>= 1.99.0)扩展市场搜索并安装 。

  - vela.vs-aiot-ide-vela
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005716075_010.png)

  - vela.vela-preview
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005716170_011.png)

# 三、配置与验证环境

插件安装完成后，需检查开发环境并安装必要的构建工具链和依赖包。

## 1、检查并安装依赖

参考下图，在 VS Code 中执行环境检查。如提示缺少组件，请按照向导提示进行安装。

![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005716284_012.png)

## 2、验证环境就绪

当所有依赖安装成功后，界面将显示如下内容，表明环境准备就绪：

![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005716382_013.png)

# 四、创建 openvela 项目

## 1、初始化项目目录

在文件系统中创建一个新目录（例如 <span class="reference">openvela</span>）。

> **警告**：请确保该目录的**绝对路径**中**不包含中文字符或空格等特殊符号**，否则会导致编译系统（Build System）报错。

## 2、获取源代码

1.  在 VS Code 中，参考下图步骤打开“创建项目”向导。
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005716494_014.png)

2.  配置项目参数：
    
      - 选择源：根据网络情况选择合适的仓库源。
    
      - 选择分支：选择合适分支分支。
        
          - trunk (主干稳定分支)：经全面测试的稳定版本，<span class="reference">dev</span> 分支的稳定功能会合并于此。推荐大多数追求稳定性的用户使用。
          - dev (开发分支)：汇集了最新的功能与修复，可能不稳定。推荐给希望体验新功能或参与贡献的开发者。
    
      - 下载方式：选择 SSH 或 HTTPS。
        
        说明：若选择 SSH 方式，请确保已在对应代码托管平台配置 SSH Key（可点击界面中的蓝色链接查看详细文档）。

3.  选择第一步创建的项目目录 <span class="reference">openvela</span>，单击右上角 **Select**，如下图所示：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005716600_015.png)

4.  等待项目创建完成，下载进度如下图所示：
    
    **注意**：下载源码过程耗时较长，请防止电脑进入休眠状态。
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005716695_016.png)

## 3、配置编译参数

1.  打开创建完成的 <span class="reference">openvela</span> 目录：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005716803_017.png)

2.  点击左侧 openvela **帆船**图标，然后点击**配置 (Configure)** 按钮，如下图所示：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005716908_018.png)

3.  选择相应的 <span class="reference">defconfig</span> 文件及其它可选参数：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005717006_019.png)

# 五、编译与运行

## 1、编译项目

1.  点击**编译（Build）**按钮，等待构建系统完成编译：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005717105_020.png)

2.  编译完成效果如下图所示：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005717211_021.png)

## 2、运行模拟器

1.  点击**运行 (Run)** 按钮，启动模拟器：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005717323_022.png)

2.  在模拟器终端输入 <span class="reference">lvgldemo</span>，启动 openvela 演示应用：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005717438_023.png)
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005717561_024.png)

# 六、调试应用

1.  单击**调试（Debug）**按钮，系统将启动模拟器并挂载调试进程：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005717669_025.png)

2.  打开源代码文件 <span class="reference">apps/system/ping/ping.c</span>，在 <span class="reference">main</span> 函数处设置断点：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005717787_026.png)

3.  在模拟器终端执行 <span class="reference">ping</span> 命令。系统将运行 Ping 应用并自动命中断点，进入调试模式。
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005717888_027.png)

# 七、开发原生应用

## 1、创建应用

1.  在插件界面点击**创建原生应用（Create Native App）：**
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005718025_028.png)

2.  选择应用模板：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005718133_029.png)

3.  输入项目名称（例如 <span class="reference">Whackmole1</span>）：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005718253_030.png)

## 2、编译与运行新应用

1.  创建完成后，VS Code 会自动定位到新应用的源代码目录。
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005718353_031.png)

2.  重新执行**编译 (Build)** -\> **运行 (Run)**。

3.  在模拟器终端输入应用名称（如 <span class="reference">Whackmole1</span>）启动新应用。

# 八、资源管理与可视化预览

openvela 插件提供了强大的可视化预览功能，支持图片、字体和二进制资源，并支持模拟器文件系统挂载。

## 1、挂载数据卷

首次使用 <span class="reference">openvela</span> 仓库时，系统会自动弹出终端执行挂载命令，将 <span class="reference">vela\_data.bin</span> 挂载到本地目录。

开发者可通过右键菜单手动管理挂载状态：

  - 挂载 openvela：执行挂载。
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005718459_032.png)
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005718565_033.png)

  - 重新挂载 openvela：当模拟器中文件发生变化（例如执行了 <span class="reference">adb push</span>）时，需执行此操作以刷新文件系统。
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005718677_034.png)

  - 卸载 openvela：断开挂载连接。

## 2、文件预览

支持普通图片、<span class="reference">.bin</span>、<span class="reference">.ttf</span> 等格式的预览（支持绝对路径与相对路径）。

![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005718780_035.png)

## 3、悬浮 (Hover) 预览

**代码资源预览**：鼠标悬停在资源路径字符串上时，将显示资源缩略图。

![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005718880_036.png)

![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005718971_037.png)

## 4、调试时变量预览

在调试模式下，鼠标悬停在变量上可获取当前值并进行预览。

> **操作技巧**：按住 <span class="reference">Alt</span> 键可在“调试值悬浮显示”和“普通资源悬浮显示”之间切换。

![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005719077_038.png)

![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005719185_039.png)

## 5、配置预览基准目录

1.  点击 VS Code 的**设置 (Settings)** 按钮（或使用快捷键 <span class="reference">Ctrl+,</span>）。

2.  在左侧菜单找到 **Extensions（扩展）并选择 AIoT Image Preview**。

3.  设置 <span class="reference">Base Dir</span> 参数以适配不同环境（如 simulator 版本）。
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005719304_040.png)
