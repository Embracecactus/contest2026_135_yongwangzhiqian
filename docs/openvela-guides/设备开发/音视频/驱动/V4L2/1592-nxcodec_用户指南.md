# nxcodec 用户指南

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1592&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:40  
> 本地拉取日期：2026-08-28

\[[English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/media/v4l2/nxcodec.md) | 简体中文\]

# 一、概述

<span class="reference">nxcodec</span> 是一个命令行测试工具，用于验证 V4L2 (Video4Linux2) M2M (Memory-to-Memory) Codec 驱动的功能。它支持对视频流进行编码 (Encode) 和解码 (Decode) 操作。

# 二、准备工作

在运行 <span class="reference">nxcodec</span> 测试前，您需要完成系统配置并准备好测试文件。

## 1、构建配置

您必须在 <span class="reference">menuconfig</span> 中启用 <span class="reference">nxcodec</span> 及其依赖项。

1.  启用 <span class="reference">nxcodec</span> 工具：
    
    在 <span class="reference">menuconfig</span> 中，使能以下选项：  
    
        CONFIG_SYSTEM_NXCODEC=y

2.  确保依赖项已启用：
    
    <span class="reference">nxcodec</span> 依赖 V4L2 框架和相关的驱动。请确保以下核心配置已启用：  
    
        # V4L2 核心支持
        CONFIG_VIDEO=y
        CONFIG_DRIVERS_VIDEO=y
        CONFIG_VIDEO_STREAM=y
            
        # 示例 Codec 驱动 (可按需替换为您的硬件驱动)
        CONFIG_SIM_VIDEO_DECODER=y
        CONFIG_SIM_VIDEO_ENCODER=y
            
        # Codec 算法库
        CONFIG_VIDEOUTILS_OPENH264=y
        CONFIG_VIDEOUTILS_LIBX264=y

3.  启用主机文件系统 (可选)：
    
    当您在模拟器 (<span class="reference">sim</span>) 环境中测试时，为了方便地访问宿主机上的测试文件，请启用 <span class="reference">hostfs</span>：  
    
        CONFIG_FS_HOSTFS=y

## 2、准备文件系统 (可选)

如果在模拟器环境中运行，请使用以下命令将宿主机存放测试文件的目录挂载到 openvela 的文件系统中。  

    # 将宿主机的/path/from/your/host/machine目录挂载到openvela的/stream目录
    nsh> mount -t hostfs -o fs=/path/from/your/host/machine /stream

# 三、命令参考

## 1、命令格式

<span class="reference">nxcodec</span> 的基本命令格式如下：  

    nsh> nxcodec -d <device> -s <wxh> -f <in_format> -i <infile> -f <out_format> -o <outfile>

  

    # 请求帮助信息
    ap> nxcodec -h
    NxCodec Version: 1.00
    Usage: nxcodec -d <devname> [options] -i <infile> [options] -o <outfile>
    
    Options:
      -d | --device    Video device name
      -s | --size      Size of stream
      -f | --format    Format of stream
      -i | --infile    Input filename for M2M devices
      -o | --outfile   Outputs stream to filename
      -h | --help      Print this help message

## 2、参数说明

| **短命令**                           | **长命令**                                  | **描述**                                                                                                                                 |
| :-------------------------------- | :--------------------------------------- | :------------------------------------------------------------------------------------------------------------------------------------- |
| <span class="reference">-d</span> | <span class="reference">--device</span>  | 指定 V4L2 Codec 设备节点路径，例如 <span class="reference">/dev/video1</span>。                                                                    |
| <span class="reference">-s</span> | <span class="reference">--size</span>    | 指定视频流的分辨率，格式为 <span class="reference">宽x高</span>，例如 <span class="reference">256x144</span>。                                            |
| <span class="reference">-f</span> | <span class="reference">--format</span>  | **\[重要\]** 指定输入或输出流的格式。 此参数的位置决定其作用域： 它定义了紧随其后的 <span class="reference">-i</span> (输入) 或 <span class="reference">-o</span> (输出) 文件的格式。 |
| <span class="reference">-i</span> | <span class="reference">--infile</span>  | 指定输入文件的路径。                                                                                                                             |
| <span class="reference">-o</span> | <span class="reference">--outfile</span> | 指定输出文件的路径。                                                                                                                             |
| <span class="reference">-h</span> | <span class="reference">--help</span>    | 显示帮助信息并退出。                                                                                                                             |

# 四、测试示例

以下示例演示如何使用 <span class="reference">nxcodec</span> 执行解码和编码操作。

## 1、解码测试 (H.264 -\> YUV)

此操作使用解码器设备 (<span class="reference">/dev/video1</span>) 将一个 H.264 编码的视频文件解码为原始的 YUV 格式文件。

解码命令：  

    nsh> nxcodec -d /dev/video1 -s 256x144 -f H264 -i /stream/256x144.h264 -f YU12 -o /stream/256x144-yuv420p-out.yuv

命令解析：

  - <span class="reference">-d /dev/video1</span>：指定解码器设备路径为 <span class="reference">/dev/video1</span>。
  - <span class="reference">-s 256x144</span>：指定输出视频流的分辨率为 <span class="reference">256x144</span>。
  - <span class="reference">-f H264</span>：指定输入视频流的编码格式为 H.264。
  - <span class="reference">-i /stream/256x144.h264</span>：指定输入视频流的文件路径为 <span class="reference">/stream/256x144.h264</span>。
  - <span class="reference">-f YU12</span>：指定输出视频流的 pixformat 格式为 YU12。
  - <span class="reference">-o /stream/256x144-yuv420p-out.yuv</span>：指定输出 YUV 文件路径为 <span class="reference">/stream/256x144-yuv420p-out.yuv</span>。

简化命令: 如果驱动程序支持自动识别输入流的格式和分辨率，您可以省略 <span class="reference">-f</span> (输入格式) 和 <span class="reference">-s</span> 参数。  

    nsh> nxcodec -d /dev/video1 -i /stream/256x144.h264 -o /stream/256x144-yuv420p-out.yuv

## 2、编码测试 (YUV -\> H.264)

此操作使用编码器设备 (/dev/video2) 将一个原始的 YUV 格式视频文件编码为 H.264 码流文件。

mount host文件系统（可选项，在 simulator 环境下需要）：  

    nsh> mount -t hostfs -o fs=/path/from/ /stream

编码命令：  

    nsh> nxcodec -d /dev/video2 -s 256x144 -f YU12 -i /stream/256x144-yuv420p.yuv -f H264 -o /stream/256x144-out.h264

这个命令的作用是使用 nxcodec 工具对输入的视频流YUV文件进行编码，并将编码后的视频流输出到指定的文件中。

具体参数的含义如下：

  - <span class="reference">-d /dev/video2</span>：指定输入视频流的设备文件路径。
  - <span class="reference">-s 256x144</span>：指定输入视频流的分辨率为 256x144。
  - <span class="reference">-f YU12</span>：指定输入视频流的像素格式为 YU12。
  - <span class="reference">-i /stream/256x144-yuv420p.yuv</span>：指定输入视频流的文件路径。
  - <span class="reference">-f H264</span>：指定输出视频流的编码格式为 H264。
  - <span class="reference">-o /stream/256x144-out.h264</span>：指定输出视频流的文件路径。
