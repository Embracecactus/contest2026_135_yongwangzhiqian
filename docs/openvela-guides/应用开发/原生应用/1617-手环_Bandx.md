# 手环 Bandx

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1617&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:53  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/demo/Smart_Band_Example.md) | 简体中文 \]

# 简介

该应用是一款智能手环演示，包括手表表盘、启动器、音乐、心率、秒表、睡眠、运动、设置、手电筒，分辨率为 194\*368。可以在 <span class="reference">apps/packages/demos/bandx/</span> 目录中了解有关 bandx 的更多详细信息。

本文介绍如何在模拟器上运行该示例。

# 前提条件

下载源码，请参见[快速入门](https://doc.openvela.com/document?id=1426&version=dev-ai-contest-2026&language=cn)。

# 步骤一 配置项目

1.  切换到 openvela 仓库的根目录，执行如下命令来配置手环 Bandx。
    
    **说明**：模拟器配置文件（defconfig）在 <span class="reference">vendor/openvela/boards/vela/configs/goldfish-armeabi-v7a-ap/</span> 目录下，使用 <span class="reference">build.sh</span> 配置和编译模拟器的代码。  
    
        ./build.sh vendor/openvela/boards/vela/configs/goldfish-armeabi-v7a-ap menuconfig
    
      - build.sh：编译脚本，用来配置和编译 openvela 代码
      - vendor/openvela/boards/vela/configs/goldfish-armeabi-v7a-ap：配置路径
      - menuconfig：打开 menuconfig 页面，修改项目代码的配置。
    
    执行后出现如下界面：
    
    ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005802918_001.png)

2.  按下 <span class="reference">/</span> 键逐个搜索修改如下配置项：  
    
        LV_USE_FRAGMENT = y
        LVX_USE_DEMO_BANDX = y
        BANDX_BASE_PATH = "/data"
    
    以 LV\_USE\_FRAGMENT 为例进行操作，其余配置方式相同。
    
    1.  输入待搜索的配置。
        
        ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005803007_002.png)
    
    2.  按下<span class="reference">Enter</span>进入到配置页面。
        
        ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005803223_003.png)
    
    3.  按下<span class="reference">Enter</span>键打开该配置，<span class="reference">\[ \]</span> 中出现 <span class="reference">\*</span> 表示该配置被打开。
        
        ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005803341_004.png)
    
    4.  按下 <span class="reference">/</span> 键可以继续搜索剩下的配置，并按上述步骤修改其余配置。
    
    5.  按下字母<span class="reference">Q</span>键，弹出如下退出保存界面。
        
        ![](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005803442_005.png)
    
    6.  按下字母<span class="reference">Y</span>键保存配置，并退出修改配置页面。

# 步骤二 编译项目

1.  切换到 openvela 仓库的根目录，在终端内依次执行如下命令：  
    
        # 清理构建产物
        ./build.sh vendor/openvela/boards/vela/configs/goldfish-armeabi-v7a-ap distclean -j8
        
        # 开始构建
        ./build.sh vendor/openvela/boards/vela/configs/goldfish-armeabi-v7a-ap -j8

2.  成功执行后，将得到以下文件：  
    
        ./nuttx
        ├── vela_ap.elf
        ├── vela_ap.bin

# 步骤三 启动模拟器并推送资源

Bandx 中使用的字体和图像资源位于 <span class="reference">apps/packages/demos/bandx/resources/</span> 中，要将这些资源推送到模拟器挂载的相应文件路径，可以按照以下步骤操作。

1.  切换到 openvela 仓库的根目录，启动模拟器：  
    
        ./emulator.sh vela

2.  使用模拟器支持的 <span class="reference">ADB</span> 将资源推送到设备，在 openvela 仓库的根目录下打开一个新的终端，输入 <span class="reference">adb push</span> 后跟文件路径，即可将资源传输到相应位置。  
    
        # 安装adb
        sudo apt install android-tools-adb
        
        # 推送资源
        adb push apps/packages/demos/bandx/resource/font/assets/* /data/font/
        adb push apps/packages/demos/bandx/resource/image/assets /data/image/
    
    如果将 <span class="reference">BANDX\_BASE\_PATH</span> 更改为非默认值，如 <span class="reference">/tmp</span>，则资源文件也必须传输到 <span class="reference">/tmp/font/</span> 和 <span class="reference">/tmp/image/</span> 目录。否则将出现找不到资源的错误。

# 步骤四 启动 Bandx

1.  在模拟器的终端环境 <span class="reference">openvela-ap\></span> 中输入如下命令：  
    
        bandx &
    
    ![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005803564_006.png)

2.  要访问 Launcher 界面，<span class="reference">从右向左</span>快速滑动。单击不同的图标导航到子页面，如下图所示的 Heart Rate 页面。要退出页面，<span class="reference">从左向右</span>快速滑动。
    
    **说明**：music页面只是UI展示，没有接入音频。
    
    ![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005803660_007.png)

3.  打开 settings 中的 <span class="reference">Auto-show</span>，将会自动播放整个应用；关闭 <span class="reference">Auto-show</span>，自动播放就结束。

# 步骤五 退出 Demo

关闭模拟器退出 Demo，如下图所示：

![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005803756_026.png)

# 常见问题

## 1\. adb 命令找不到

### 原因

未安装 <span class="reference">adb</span> 工具。

### 解决方案

安装 <span class="reference">adb</span>，执行以下命令：  

    sudo apt install android-tools-adb

## 2\. 字体显示为乱码

### 原因

未正确加载字体资源。

### 解决方案

请按[步骤三](#步骤三-启动模拟器并推送资源)进行资源推送。
