# Hello World

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1611&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:49  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/app_dev/system_apps/hello_world/Hello_World.md) | 简体中文 \]

# 概述

本文档面向开发者，旨在详细介绍如何在 openvela 操作系统中添加、配置和运行一个新的用户应用程序。openvela 基于 NuttX RTOS 构建，其模块化的设计允许开发者方便地集成自定义功能或第三方库。

一个典型的功能模块包含以下部分：

  - **系统应用 (System Application)**：作为系统内置功能的一部分，通常存放于 <span class="reference">apps/</span> 目录下。
  - **第三方库 (Third-Party Library)**：作为外部依赖引入，通常存放于 <span class="reference">external/</span> 目录下。

示例目录结构如下：  

    └── vela
        ├── apps
        │   └── examples
        │       ├── hello_main_1
        │       └── hello_main_2
        └── external
            ├── libs_1
            └── libs_2

本指南将以 <span class="reference">Hello, World\!</span> 示例应用程序为引导，完整演示从代码编写到构建、运行和自启动的全过程。

# 步骤一：查看 Hello World 示例框架

本节介绍如何在 openvela 中添加一个示例应用程序，包括主体框架、文件内容以及相关构建配置。

## 1、主体框架

Hello World 示例应用程序需要包含以下核心文件：

  - <span class="reference">hello\_main.c</span>：应用程序的源代码，包含 <span class="reference">main</span> 函数入口。
  - <span class="reference">Kconfig</span>：构建系统的配置文件，用于在 <span class="reference">menuconfig</span> 中提供可裁剪的编译选项。
  - <span class="reference">CMakeLists.txt</span>：CMake 构建脚本，用于定义源码、依赖和编译规则。

目录结构示例如下，当前 Hello World 已添加完毕：  

    apps
     └── examples
         └── hello
             ├── hello_main.c
             ├── CMakeLists.txt
             ├── Kconfig

## 2、编写源代码 (hello\_main.c)

查看 <span class="reference">hello\_main.c</span> 文件，这是应用程序的执行逻辑入口：  

    #include <stdio.h>
    
    int main(int argc, char *argv[])
    {
        printf("Hello, World!!\n");
        return 0;
    }

如果您需要使用 C++，请确保 <span class="reference">main</span> 函数使用 <span class="reference">extern "C"</span> 声明，以保证其 C 语言链接兼容性，从而能被系统正确调用：  

    #include <iostream>
    
    extern "C" int main(int argc, char *argv[])
    {
        std::cout << "Hello, World!!" << endl;
        return 0;
    }

## 3、创建 Kconfig 配置文件

查看 <span class="reference">Kconfig</span> 文件，用于定义应用程序的编译选项。这些选项将显示在 <span class="reference">menuconfig</span> 图形配置界面中，允许用户按需启用或配置您的应用：  

    config EXAMPLES_HELLO
            tristate "\"Hello, World!\" example"
            default n
            ---help---
                    Enable the \"Hello, World!\" example
    
    # 仅当 EXAMPLES_HELLO 启用时，以下选项才可见
    if EXAMPLES_HELLO
    
    # 定义应用程序在 openvela 中执行的命令名称
    config EXAMPLES_HELLO_PROGNAME
            string "Program name"
            default "hello"
            ---help---
                    This is the name of the program that will be used when the NSH ELF
                    program is installed.
    
    # 定义应用程序任务的优先级
    config EXAMPLES_HELLO_PRIORITY
            int "Hello task priority"
            default 100
    
    # 定义应用程序任务的堆栈大小
    config EXAMPLES_HELLO_STACKSIZE
            int "Hello stack size"
            default DEFAULT_TASK_STACKSIZE
            
    endif

## 4、创建 CMake 构建脚本

查看 <span class="reference">CMakeLists.txt</span> 文件。openvela 的构建系统会自动加载 <span class="reference">.config</span> 文件中的所有宏定义作为 CMake 变量，因此您可以直接使用 <span class="reference">Kconfig</span> 中定义的配置。  

    # 检查 'EXAMPLES_HELLO' 是否在 .config 中被启用
    if(CONFIG_EXAMPLES_HELLO) # 如果defconfig使能了该feature则加入编译
      
      # 调用 nuttx_add_application 函数将应用注册为内置 (built-in) 程序
      nuttx_add_application(
        # NAME: 指定应用的唯一名称，通常与 Kconfig 中的 PROGNAME 保持一致
        NAME                                
        ${CONFIG_EXAMPLES_HELLO_PROGNAME}   
        
        # SRCS: 指定源文件列表，main 函数所在文件应为第一个
        SRCS                                
        hello_main.c 
        
        # STACKSIZE: 指定任务堆栈大小                       
        STACKSIZE                           
        ${CONFIG_EXAMPLES_HELLO_STACKSIZE}  
        
        # PRIORITY: 指定任务优先级，不传则为SCHED_PRIORITY_DEFAULT
        PRIORITY                            
        ${CONFIG_EXAMPLES_HELLO_PRIORITY})  
    endif()

### <span class="reference">nuttx\_add\_application()</span> 的函数定义

该 CMake 函数位于 <span class="reference">nuttx/cmake/nuttx\_add\_application.cmake</span> 文件中，用于添加并配置应用程序。  

    nuttx/cmake/nuttx_add_application.cmake
    
     Usage:
       nuttx_add_application( NAME <string> [ PRIORITY <string> ]
         [ STACKSIZE <string> ] [ COMPILE_FLAGS <list> ]
         [ INCLUDE_DIRECTORIES <list> ] [ DEPENDS <string> ]
         [ DEFINITIONS <string> ] [ MODULE <string> ] [ SRCS <list> ] )
    
     Parameters:
       NAME                : unique name of application
       PRIORITY            : priority
       STACKSIZE           : stack size
       COMPILE_FLAGS       : compile flags
       INCLUDE_DIRECTORIES : include directories
       DEPENDS             : targets which this module depends on
       DEFINITIONS         : optional compile definitions
       MODULE              : if "m", build module (designed to received
                             CONFIG_<app> value)
       SRCS                : source files
       NO_MAIN_ALIAS       : do not add a main=<app>_main alias(*)

# 步骤二：验证应用程序

完成文件创建后，您需要通过以下步骤来配置、编译并运行您的应用程序。

## 1、清理构建环境 (可选)

如果您修改了 Kconfig 文件或希望进行全新编译，建议先执行清理操作：  

    # 使用 distclean 清理所有构建产物和配置
    ./build.sh vendor/openvela/boards/vela/configs/goldfish-armeabi-v7a-ap  --cmake distclean -j$(nproc)

或者直接删除cmake产物  

    # 或者直接删除cmake产物
    rm -rf cmake_out/vela_goldfish-armeabi-v7a-ap

## 2、图形化配置 (menuconfig)

启动 <span class="reference">menuconfig</span> 以在图形界面中启用您的新应用：  

    # 启动 menuconfig  
    ./build.sh vendor/openvela/boards/vela/configs/goldfish-armeabi-v7a-ap  --cmake menuconfig -j$(nproc)

在 <span class="reference">menuconfig</span> 界面中，通过以下路径找到并启用您的应用： <span class="reference">Application Configuration</span> ---\> <span class="reference">Examples</span> ---\> <span class="reference">\[\*\] "Hello, World\!" example</span>

![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005758297_001.png)

## 3、编译和运行

保存 <span class="reference">menuconfig</span> 配置后，执行编译。  

    # 编译固件 (-j`nproc` 使用所有 CPU 核心并行编译)
    ./build.sh vendor/openvela/boards/vela/configs/goldfish-armeabi-v7a-ap  --cmake -j$(nproc)
    
    # 拷贝产物
    cp cmake_out/vela_goldfish-armeabi-v7a-ap/nuttx* nuttx/ && 
    cp cmake_out/vela_goldfish-armeabi-v7a-ap/vela_data.bin nuttx/ && 
    cp cmake_out/vela_goldfish-armeabi-v7a-ap/vela_system.bin nuttx/
    
    # 启动模拟器运行固件
    ./emulator.sh vela

系统启动后，在 NSH 命令行中输入您在 <span class="reference">Kconfig</span> 中设置的程序名称（默认为 <span class="reference">hello</span>）并回车，即可看到程序输出：

![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005758401_002.png)

# 步骤三：配置应用自启动

openvela 支持在系统启动时自动运行指定脚本，您可以通过编辑启动脚本来实现应用的自启动。

## 1、自启动机制与配置

openvela 的启动脚本存放在 <span class="reference">/etc</span> 目录下，该目录以 <span class="reference">romfs</span> 的形式与 openvela 的二进制文件链接在一起。在系统启动后会自动被 <span class="reference">nshlib</span> 挂载，相关配置如下。

确保您的板级配置启用了以下 <span class="reference">Kconfig</span> 选项：  

    CONFIG_FS_ROMFS=y
    CONFIG_ETC_ROMFS=y
    CONFIG_ETC_ROMFSMOUNTPT="/etc"
    CONFIG_NSH_SYSINITSCRIPT="init.d/rc.sysinit"
    CONFIG_NSH_INITSCRIPT="init.d/rcS"

## 2、启动脚本位置

默认的用户启动脚本位于板级配置目录中：  

    vendor/openvela/boards/vela/src/etc/init.d/rc.sysinit   # 系统初始化脚本 
    vendor/openvela/boards/vela/src/etc/init.d/rcS          # 用户脚本

## 3、编辑启动脚本

打开 <span class="reference">rcS</span> 文件，在其中添加您应用的执行命令。  

    #ifdef CONFIG_FS_HOSTFS
    mount -t hostfs -o fs=vendor/openvela/boards/vela/resource /host
    #endif
    
    hello &

添加后效果如下图所示：

![alt text](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005758494_003.png)

## 4、重新编译和运行

    # 编译固件 (-j`nproc` 使用所有 CPU 核心并行编译)
    ./build.sh vendor/openvela/boards/vela/configs/goldfish-armeabi-v7a-ap  --cmake -j$(nproc)
    
    # 拷贝产物
    cp cmake_out/vela_goldfish-armeabi-v7a-ap/nuttx* nuttx/ && 
    cp cmake_out/vela_goldfish-armeabi-v7a-ap/vela_data.bin nuttx/ && 
    cp cmake_out/vela_goldfish-armeabi-v7a-ap/vela_system.bin nuttx/
    
    # 启动模拟器运行固件
    ./emulator.sh vela

启动后效果如下图所示：

![alt text](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005758602_004.png)

**注意：**

  - **使用 POSIX 线程**：在应用程序内部，推荐使用 <span class="reference">pthread\_create()</span> 创建和管理子线程，而不是直接调用底层的 <span class="reference">task\_create()</span>。这能保证更好的可移植性和兼容性。
  - **守护主线程**：如果您的主线程创建了子线程，请确保主线程在所有子线程安全退出后才结束。否则，主线程的退出可能导致整个进程被回收，子线程被强制终止。
  - **创建后台服务**：对于需要长期运行的服务，可以在 <span class="reference">rcS</span> 脚本中使用 <span class="reference">&</span> 将其置于后台运行。应用内部通常会进入一个循环（如 <span class="reference">while(1)</span>）来处理事件或执行周期性任务。

# 参考资料

为帮助您更好地理解和添加 <span class="reference">CMakeLists.txt</span>，下面是参考资料和工具信息：

  - openvela CMake 编译系统请参考 [CMake 快速入门](https://doc.openvela.com/document?id=1448&version=dev-ai-contest-2026&language=cn)。
