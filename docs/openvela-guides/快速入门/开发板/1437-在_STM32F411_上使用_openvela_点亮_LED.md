# 在 STM32F411 上使用 openvela 点亮 LED

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1437&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:50:21  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/quickstart/development_board/STM32F411.md) | 简体中文 \]

# 一、概述

本指南将引导您在 <span class="reference">STM32F411</span> 微控制器上，基于 <span class="reference">openvela</span> 实时操作系统，完成一个基础的 LED 闪烁示例。您将掌握 <span class="reference">openvela</span> 的基本使用流程，包括开发环境搭建、项目编译、固件烧录以及 <span class="reference">LED</span> 驱动的注册与使用。

# 二、最终效果

![stsw-link007](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005724237_leds.gif)

# 三、准备工作

## 1、硬件准备

  - 开发板：一块基于 STM32F411 的开发板。
  - 调试器：ST-Link V2 调试器（或开发板板载的 ST-Link）。
  - 串口工具：一个 USB 转 TTL 串口模块，用于查看日志输出。
  - 连接线：USB 数据线和若干杜邦线。

## 2、软件与工具链准备

请在您的开发主机（Ubuntu）上完成以下软件的安装与配置。

1.  搭建 openvela 开发环境。
    
    请首先参照官方文档[快速入门](https://doc.openvela.com/document?id=1426&version=dev-ai-contest-2026&language=cn)，完成 <span class="reference">openvela</span> 基础开发环境的搭建。

2.  安装 ARM GCC 编译工具链。
    
    该工具链用于编译目标平台的固件。打开终端，执行以下命令：  
    
        sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi

3.  安装 STM32CubeProgrammer。
    
    此工具用于将编译好的固件烧录到 STM32 微控制器中。
    
      - 下载地址：请从 [ST 官网](https://www.st.com/en/development-tools/stm32cubeprog.html)下载与您操作系统匹配的版本并完成安装。
    
      - 安装依赖库：为确保 STM32CubeProgrammer 能够正常识别 USB 设备，请安装 <span class="reference">libusb</span>。  
        
            sudo apt-get install libusb-1.0.0-dev

4.  安装 ST-Link 驱动。
    
    为使 Linux 系统能以普通用户权限访问 ST-Link 设备，需要安装 udev 规则。
    
      - 下载地址：请从 [ST 官网](https://www.st.com/en/development-tools/stsw-link007.html#get-software) 下载 <span class="reference">stsw-link007</span> 软件包并解压。
        
        ![stsw-link007](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005724592_029.png)
    
      - 执行安装脚本：根据压缩包内 <span class="reference">readme.txt</span> 的指引，执行以下命令安装 udev 规则。  
        
            sudo sh st-stlink-udev-rules-*-linux-noarch.sh

5.  启动后界面如下：
    
    ![STM32CubeProgrammer](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005724699_030.png)

## 3、硬件连接与验证

1.  查看原理图准备连接硬件。
    
    ![Schematic](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005724830_031.png)

2.  请按照下表所示，将各部件连接起来。
    
    | 连接起点             | 连接终点                  | 说明    |
    | ---------------- | --------------------- | ----- |
    | PC USB 端口        | ST-Link 调试器 USB 口     | 供电与调试 |
    | USB 转 TTL 模块 TX  | 开发板 PA10 (USART1\_RX) | 串口通信  |
    | USB 转 TTL 模块 RX  | 开发板 PA9 (USART1\_TX)  | 串口通信  |
    | USB 转 TTL 模块 GND | 开发板 GND               | 共地    |
    

    实物连接参考如下：
    
    ![Physical Connection](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005725023_032.jpg)

3.  验证连接。
    
    请通过 STM32CubeProgrammer 工具验证硬件连接是否成功。
    
    1.  启动 **STM32CubeProgrammer。**
    
    2.  在右上角的 **ST-LINK Configuration** 区域，单击**刷新**按钮。如果 **Serial number** 下拉框中出现了设备序列号，则表明调试器已被成功识别。
        
        ![Serial number](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005725123_033.png)
    
    3.  在左侧导航栏选择 **Erasing & Programming**，如下图所示：
        
        ![Erasing Programming](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005725231_034.jpeg)
    
    4.  选择 **J-Link** 或 **STLINK** 方式，单击绿色 **Connect** 按钮。当按钮变为 **Disconnect** 时，表示开发板已成功连接，如下图所示：
        
        ![Connect](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005725478_035.jpg)

# 四、运行 Demo

本章节将指导您如何基于 <span class="reference">openvela</span> 源码，为 <span class="reference">STM32F411-minimum</span> 开发板编译并运行一个 LED 闪烁示例。

## 1、获取示例代码

请确保您已根据官方[下载 openvela 源代码](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/zh-cn/quickstart/Download_Vela_sources_zh-cn.md)文档，在本地准备好了完整的 <span class="reference">openvela</span> 源代码。

## 2、(可选) 理解项目结构

<span class="reference">openvela</span> 的代码遵循分层设计。了解关键目录有助于您进行后续的定制开发。

  - **应用层 (Examples)**：[nuttx-apps/examples/leds](https://github.com/../../../open-vela/nuttx-apps/tree/dev-ai-contest-2026/examples/leds)
  - **板级支持包 (BSP)**：[nuttx/boards/arm/stm32/stm32f411-minimum](https://github.com/../../../open-vela/nuttx/tree/dev-ai-contest-2026/boards/arm/stm32/stm32f411-minimum)

下表简述了核心目录的功能：

| 目录路径                                                                        | 功能说明                                                         |
| --------------------------------------------------------------------------- | ------------------------------------------------------------ |
| <span class="reference">nuttx/boards/arm/stm32/stm32f411-minimum/src</span> | 存放 STM32F411 板级的特定驱动实现，如 GPIO 初始化、设备注册（例如 /dev/userleds）等。   |
| <span class="reference">nuttx/arch/arm/src/stm32</span>                     | 存放 STM32 芯片家族通用的片上外设（SoC-level）驱动，如 I2C、SPI、GPIO 的底层读写和时钟配置。 |
| <span class="reference">nuttx/drivers</span>                                | 存放符合 openvela 标准驱动模型的上层驱动接口。应用程序通过调用这些标准接口与底层硬件交互。           |

## 3、加载并配置项目

1.  执行以下命令打开 <span class="reference">menuconfig</span> 配置界面：  
    
        ./build.sh nuttx/boards/arm/stm32/stm32f411-minimum/configs/rgbled menuconfig

2.  选择目标开发板。
    
    1.  按回车键进入 **Board Selection**，打开开发板选择菜单。
        
        ![Board Selection](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005725569_036.png)
    
    2.  按回车键选择 **Select target board**，选择要编译的目标平台:
        
        ![Select target board](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005725677_037.png)
    
    3.  按回车键选择 **STM32F411CEU6 Minimum ARM Development Board**，为 STM32F411CEU6 最小系统板编译固件:
        
        ![STM32F411CEU6](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005725789_038.png)

3.  启用 LED 驱动。
    
    1.  按两次 Esc 键返回到第一级目录，选择 **Device Drives**:
        
        ![Device Drives](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005725886_039.png)
    
    2.  按 Enter 键进入 <span class="reference">LED support</span>：
        
        ![LED support](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005725981_040.png)
    
    3.  启用以下配置项：
        
          - **LED driver**
        
          - **Generic Lower Half LED Driver**
            
            ![LED Driver](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005726104_041.png)

4.  启用示例程序。
    
    1.  按 **/** 键搜索并启用 **EXAMPLES\_LEDS**。
        
        ![EXAMPLES\_LEDS](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005726213_042.png)
    
    2.  按 **/** 键搜索并禁用 **ARCH\_LEDS**（避免冲突）。
        
        ![ARCH\_LEDS](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005726328_043.png)

5.  保存配置，按 **Q** 键退出菜单，选择 **Y** 键保存配置。

## 4、编译项目

1.  返回到 <span class="reference">openvela</span> 根目录，使用以下命令编译 LED 示例程序：  
    
        ./build.sh nuttx/boards/arm/stm32/stm32f411-minimum/configs/rgbled -j8

2.  编译成功后，将在 <span class="reference">nuttx</span> 目录下生成 <span class="reference">nuttx.hex</span> 和 <span class="reference">nuttx.bin</span> 固件文件。

## 5、烧录固件

1.  启动 **STM32CubeProgrammer** 工具并连接开发板。

2.  单击 **Browse**，选择上一步生成的固件文件 **<span class="reference">openvela/nuttx/nuttx.hex</span>**，并勾选 **Skip flash erase before programming**，如下图所示：
    
    ![Browse](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005726427_044.png)

3.  单击 **Start Programming** 开始下载。下载完成后，会弹出提示框，并且日志窗口会输出相关信息。
    
    ![Start Programming](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005726524_045.png)

4.  如果下载过程中出现 **Error**，请单击 **Full chip erase** 按钮擦除芯片数据，然后重新下载固件，即可恢复正常。
    
    ![Full chip erase](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005726624_046.png)

## 6、连接串口

1.  使用 **Minicom** 或其他串口工具连接到开发板。  
    
        # 注意：您的设备号可能是 ttyACM1，请根据实际情况修改
        sudo minicom -D /dev/ttyACM0 -b 115200

2.  （可选）如果首次使用 Minicom 的无法接收键盘输入，请参考如下步骤修改 Minicom 的相关配置：
    
    1.  使用以下命令打开 Minicom 配置界面：  
        
            sudo minicom -s
        
        ![Serial port setup](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005726724_047.png)
    
    2.  进入 **Serial port setup** 配置目录，确保以下两项配置值为 **No**：
        
          - Hardware Flow Control: No
        
          - Software Flow Control: No
            
            ![Hardware Flow Control](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005726830_048.png)
    
    3.  选择 <span class="reference">Save setup as dfl</span> 保存为默认配置，然后 <span class="reference">Exit</span>。
    
    4.  重新连接。若仍有问题，请按开发板的 <span class="reference">Reset</span> 键。

## 7、运行 LED 示例

1.  重新打开 minicom，连接成功后，在 Minicom 终端中按回车，您会看到 <span class="reference">nsh\></span> 提示符。
    
    ![nsh](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005726933_049.png)

2.  运行 LED 示例，请输入以下命令：  
    
        leds

3.  您将看到开发板上的用户 LED 开始闪烁，同时串口终端会输出运行日志。

# 五、如何新增 Demo

本章将指导您如何在 <span class="reference">openvela</span> 的 <span class="reference">packages/demos</span> 目录下添加、配置并运行一个自定义的 LED 控制应用。

## 1、核心步骤概述

在 <span class="reference">openvela</span> 中集成一个新应用，主要遵循以下流程：

1.  **创建应用文件**：在 <span class="reference">packages/demos/</span> 下创建新目录，并编写应用的 C 源码、<span class="reference">Kconfig</span> 和 <span class="reference">Makefile</span>。
2.  **注册应用到编译系统**：修改上层的 <span class="reference">Make.defs</span> 文件，让构建系统能够发现您的新应用。
3.  **配置与编译**：通过 <span class="reference">menuconfig</span> 启用新应用，并编译生成包含新功能的固件。
4.  **运行与验证**：烧录固件，并通过终端命令运行您的 Demo。

## 2、创建 Demo 源码及配置文件

1.  创建 <span class="reference">led</span> 目录：  
    
        # 确保当前位于 openvela 源码根目录
        mkdir -p packages/demos/led

2.  创建 C 语言源文件 (<span class="reference">packages/demos/led/led\_main.c</span>)： 此文件是 Demo 的核心逻辑，它通过 <span class="reference">ioctl</span> 系统调用与底层 LED 驱动进行交互。  
    
        /****************************************************************************
        * Included Files
        ****************************************************************************/
        #include <nuttx/config.h>
        #include <nuttx/leds/userled.h>
        #include <sys/ioctl.h>
        #include <stdio.h>
        #include <stdlib.h>
        #include <fcntl.h>
        #include <unistd.h>
        /****************************************************************************
        * Public Functions
        ****************************************************************************/
        int main(int argc, char *argv[])
        {
            int fd;
            int ret;
            userled_set_t ledset; // 定义一个LED状态集合
            printf("Starting LED Demo...\n");
            // 1. 打开 userled 设备驱动
            fd = open("/dev/userleds", O_WRONLY);
            if (fd < 0)
            {
                printf("Failed to open /dev/userleds, please ensure USERLED is enabled.\n");
                return -1;
            }
            // 2. 点亮第一个LED (通常是LED 0)
            printf("Turning ON LED 0.\n");
            ledset = 1 << 0; // 使用位掩码选择第一个LED
            ret = ioctl(fd, ULEDIOC_SETALL, (unsigned long)ledset);
            if (ret < 0)
            {
                printf("ioctl(ULEDIOC_SETALL) failed: %d\n", ret);
                close(fd);
                return -1;
            }
            sleep(2); // 保持点亮 2 秒
            // 3. 熄灭所有LED
            printf("Turning OFF all LEDs.\n");
            ledset = 0;
            ioctl(fd, ULEDIOC_SETALL, (unsigned long)ledset);
            // 4. 关闭设备
            close(fd);
            printf("LED Demo finished.\n");
            return 0;
        }

3.  创建 <span class="reference">Kconfig</span> 文件。此文件用于在 <span class="reference">menuconfig</span> 中生成一个独立的配置选项来控制是否编译此 Demo。
    
    <span class="reference">packages/demos/led/Kconfig</span>:  
    
        # Defines the configuration option for the custom LED Demo.
        # This will appear under "Application Configuration -> Demos".
        config APP_DEMOS_LED
            bool "Custom LED control demo"
            default n
            depends on USERLED  # This demo requires the base USERLED driver
            ---help---
                Enable this to build the custom LED control demo, which
                demonstrates how to turn an LED on and off via ioctl.
    
    **说明**：我们创建了一个独立的 <span class="reference">APP\_DEMOS\_LED</span> 选项，而不是复用 <span class="reference">EXAMPLES\_LEDS</span>，这使得新增的 Demo 逻辑清晰，配置简单，避免了不必要的依赖和混淆。说明：

4.  创建 <span class="reference">Makefile</span> 文件： 此文件定义了本应用的编译规则，如程序名、优先级和堆栈大小。
    
    <span class="reference">packages/demos/led/Makefile</span>:  
    
        include $$(APPDIR)/Make.defs
        # Application details, linked to the Kconfig option
        PROGNAME  = led_demo
        PRIORITY  = 100
        STACKSIZE = 2048
        # Source file for the application
        MAINSRC = led_main.c
        include $$(APPDIR)/Application.mk

## 3、注册新 Demo 到编译系统

编辑 <span class="reference">packages/demos/</span> 目录下的 <span class="reference">Make.defs</span> 文件，将我们的 <span class="reference">led</span> Demo 加入编译列表。

1.  打开 <span class="reference">packages/demos/Make.defs</span> 文件。

2.  在文件末尾添加以下代码：  
    
        # ... (other demo configurations might be here) ...
        # Add our custom LED demo to the build if it's enabled in Kconfig.
        ifneq ($$(CONFIG_APP_DEMOS_LED),)
        CONFIGURED_APPS += $$(APPDIR)/packages/demos/led
        endif

## 4、配置与编译

### 配置项目

返回 <span class="reference">openvela</span> 根目录，运行 <span class="reference">menuconfig</span>。  

    ./build.sh nuttx/boards/arm/stm32/stm32f411-minimum/configs/rgbled menuconfig

### 启用所需配置

在 <span class="reference">menuconfig</span> 界面中，确保以下两个选项被启用（可使用 <span class="reference">/</span> 键搜索）：

  - 启用底层 LED 驱动 (依赖项)
    
      - 路径: <span class="reference">Device Drivers ---\> LED Driver Support ---\> \[\*\] User LED Support</span>
      - 确保勾选: <span class="reference">CONFIG\_USERLED</span>

  - 启用 LED Demo
    
      - 路径: <span class="reference">Application Configuration ---\> Demos ---\> \[\*\] Custom LED control demo</span>
      - 确保勾选: <span class="reference">CONFIG\_APP\_DEMOS\_LED</span> 完成后，保存配置并退出。

完成上述配置后，保存配置并退出。

### 编译项目

切换到 <span class="reference">openvela</span> 的根目录，<span class="reference">distclean</span> 之后 <span class="reference">build</span>:  

    # (Optional but recommended) Clean previous build artifacts
    ./build.sh nuttx/boards/arm/stm32/stm32f411-minimum/configs/rgbled distclean
    # Build the project with the new configuration
    ./build.sh nuttx/boards/arm/stm32/stm32f411-minimum/configs/rgbled -j8

## 5、运行 Demo

请参考[第四章](#四运行-demo)的指导运行 Demo。

1.  烧录固件：参考[烧录固件](#5烧录固件)的指导，烧录新生成的 <span class="reference">nuttx/nuttx.hex</span> 文件。

2.  运行命令：连接串口终端，在 <span class="reference">nsh\></span> 提示符下输入 <span class="reference">Makefile</span> 中定义的程序名 <span class="reference">led\_demo</span>。  
    
        nsh> led_demo

3.  观察结果：
    
      - 您将在终端看到 **Starting LED Demo...** 等日志输出。
      - 同时，开发板上的用户 LED 将**点亮 2 秒钟，然后熄灭**。

# 六、FAQ

## 1、编译时出现 <span class="reference">undefined reference to board\_userled...</span> 错误

### 问题现象

在配置完 <span class="reference">menuconfig</span> 并执行编译后，链接阶段出现类似以下的 **undefined reference** 错误。

![undefined reference](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005727039_050.png)

### 原因分析

这是 <span class="reference">USERLED</span> 通用驱动和 <span class="reference">ARCH\_LEDS</span> 板级特定驱动之间的配置冲突。

  - <span class="reference">CONFIG\_USERLED=y</span>：启用了通用的 <span class="reference">/dev/userleds</span> 驱动框架。此框架依赖于底层的 <span class="reference">board\_userled...</span> 函数来实现具体硬件操作。
  - <span class="reference">CONFIG\_ARCH\_LEDS=y</span>：启用了板级 LED 驱动。在这种模式下，系统认为 LED 由架构代码直接控制，因此不会编译和链接 <span class="reference">board\_userled...</span> 相关函数，导致 <span class="reference">USERLED</span> 驱动找不到实现，从而产生链接错误。

### 解决方案

禁用板级 <span class="reference">ARCH\_LEDS</span> 驱动，以允许 <span class="reference">USERLED</span> 通用驱动正确工作。

1.  进入 <span class="reference">menuconfig</span>：  
    
        ./build.sh nuttx/boards/arm/stm32/stm32f411-minimum/configs/rgbled menuconfig

2.  关闭 <span class="reference">ARCH\_LEDS</span>。

3.  清理并重新编译：  
    
        # 清理旧的配置和构建产物
        ./build.sh nuttx/boards/arm/stm32/stm32f411-minimum/configs/rgbled distclean
        # 重新编译项目
        ./build.sh nuttx/boards/arm/stm32/stm32f411-minimum/configs/rgbled -j8

## 2、程序运行时提示 <span class="reference">Failed to open /dev/userleds</span>

### 问题现象

在终端中运行 Demo，程序输出错误信息，无法打开设备文件。

![userleds](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005727128_051.png)

### 原因分析：

此问题表示 <span class="reference">/dev/userleds</span> 设备节点未被创建。这通常是因为 openvela 的 <span class="reference">USERLED</span> 通用驱动程序未在配置中启用，导致相关代码没有被编译进固件。

### 解决方案

按照 Demo 中的 Kconfig 文件，打开 **EXAMPLES\_LEDS** 和 **USERLED** 配置。

![userleds](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005727223_052.png)

## 3、<span class="reference">packages/demos</span> 和 <span class="reference">nuttx/apps/examples</span> 目录有什么区别

  - https://github.com/open-vela/nuttx-apps/tree/dev-ai-contest-2026/examples
    
      - 来源: NuttX 官方社区。
      - 内容: 包含由 NuttX 社区维护的、用于演示其核心功能的各种示例。
      - 性质: 通用、与上层应用无关。

  - https://github.com/open-vela/packages\_demos
    
      - 来源: openvela 项目。
      - 内容: 包含为展示 openvela 特定功能或集成方案而创建的示例。
      - 性质: openvela 定制、与项目强相关。

  - 开发建议： 当您为 openvela 项目新增自定义 Demo 或应用时，强烈建议您在 **<span class="reference">packages/demos</span>** 目录下创建。

# 七、参考资料

  - **[在 STM32H750 上部署 openvela](https://doc.openvela.com/document?id=1436&version=dev-ai-contest-2026&language=cn)**
    
      - 一个具体的、端到端的硬件部署实践案例。

  - **[NuttX 官方文档](https://nuttx.apache.org/docs/latest/)**
    
      - 理解 NuttX 底层机制的权威指南，特别是其**构建系统 (Build System)** 和 **Kconfig 配置**相关的章节，对于添加新组件至关重要。

  - **[STM32CubeProgrammer 软件指南](https://www.st.com/en/development-tools/stm32cubeprog.html)**
    
      - 学习如何使用 ST 官方工具进行固件烧录、配置选项字节等操作。
