# Audio Driver 测试

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1584&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:36  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/media/audio/Audio_Driver_Testing_Guide.md) | 简体中文 \]

# 一、nxplayer 使用指南

## 1、简介

<span class="reference">nxplayer</span> 是一个命令行测试程序，用于测试 openvela audio driver 的播放功能。它支持播放 PCM 文件和压缩格式文件，代码位于 <span class="reference">apps/system/nxplayer</span> 目录。

## 2、nxplayer 命令说明

运行 <span class="reference">nxplayer</span> 后，可通过输入 <span class="reference">help</span> 查看支持的命令。以下是常用命令及其功能说明。  

    nxplayer> help
    NxPlayer commands
    ================
      balance d%        : Set balance percentage (< 50% means more left)
      bass d%           : Set bass level percentage
      device devfile    : Specify a preferred audio device
      h                 : Display help for commands
      help              : Display help for commands
      mediadir path     : Change the media directory
      play filename     : Play a media file
      playraw filename  : Play a raw data file
      pause             : Pause playback
      resume            : Resume playback
      stop              : Stop playback
      tone freq secs    : Produce a pure tone
      treble d%         : Set treble level percentage
      q                 : Exit NxPlayer
      quit              : Exit NxPlayer
      volume d%         : Set volume to level specified

  - <span class="reference">balance</span>: 设置左右声道的平衡百分比，小于 50% 表示更多的声音在左边。
  - <span class="reference">bass</span>: 设置低音的百分比。
  - <span class="reference">device</span>: 指定首选的音频设备。
  - <span class="reference">mediadir</span>: 更改媒体文件的目录。
  - <span class="reference">play</span>: 播放指定的媒体文件。
  - <span class="reference">playraw</span>: 播放指定的原始数据文件。
  - <span class="reference">pause</span>: 暂停播放。
  - <span class="reference">resume</span>: 恢复播放。
  - <span class="reference">stop</span>: 停止播放。
  - <span class="reference">tone</span>: 产生指定频率和时长的纯音。
  - <span class="reference">treble d%</span> : 设置高音的百分比。
  - <span class="reference">q</span>/<span class="reference">quit</span>: 退出 <span class="reference">NxPlayer</span>。
  - <span class="reference">volume d%</span>：设置音量百分比。

## 3、PCM 文件播放

播放 PCM 文件时，需要指定声道数、采样深度和采样率。以下是具体操作步骤：  

    nxplayer
    nxplayer> device pcm0p
    nxplayer> playraw /data/audio.pcm 1 16 44100

播放结束后，可通过以下命令停止播放并关闭设备：  

    nxplayer> stop
    nxplayer> close

## 4、压缩格式文件播放

以下以 <span class="reference">sim</span> 平台为例，演示如何播放 MP3 文件。

1.  安装依赖。
    
    在主机上安装必要的依赖库：  
    
        sudo apt install libmad0-dev:i386

2.  运行 <span class="reference">nxplayer</span>。
    
    播放 MP3 文件的具体命令如下：  
    
        nxplayer
        nxplayer> device pcm1p
        nxplayer> play /data/audio.mp3

3.  停止播放。
    
    播放结束后，使用以下命令停止播放并关闭设备：  
    
        nxplayer> stop
        nxplayer> close

# 二、nxrecorder 使用指南

## 1、简介

<span class="reference">nxrecorder</span> 是一个命令行测试程序，用于测试 openvela audio driver 的录音功能。它支持录制 PCM 文件和压缩格式文件，代码位于 <span class="reference">apps/system/nxrecorder</span> 目录。

## 2、nxrecorder 命令说明

运行 <span class="reference">nxrecorder</span> 后，可通过输入 <span class="reference">help</span> 查看支持的命令。以下是常用命令及其功能说明：  

    nxrecorder> help
    NxRecorder commands
    ================
      device devfile      : Specify a preferred audio device
      h                   : Display help for commands
      help                : Display help for commands
      recordraw filename  : Record a pcm raw file
      record filename     : Record a media file
      pause               : Pause record
      resume              : Resume record
      stop                : Stop record
      q                   : Exit NxRecorder
      quit                : Exit NxRecorder

  - <span class="reference">device devfile</span>：指定首选的音频设备。
  - <span class="reference">recordraw filename</span>：录制一个 PCM 原始文件。
  - <span class="reference">record filename</span>：录制一个媒体文件。
  - <span class="reference">pause</span>：暂停录制。
  - <span class="reference">resume</span>：恢复录制。
  - <span class="reference">stop</span>：停止录制。
  - <span class="reference">q</span> / <span class="reference">quit</span>：退出 nxrecorder。

## 3、PCM 文件录制

录制 PCM 文件时，需要指定声道数、采样深度和采样率。以下是具体操作步骤：  

    nxrecorder
    nxrecorder> device pcm0c
    nxrecorder> recordraw /data/d_1ch_16bit_44100.pcm 1 16 44100
    nxrecorder> recordraw /data/test.pcm 1 16 48000

录制结束后，可通过以下命令停止录制并关闭设备：  

    nxrecorder> stop
    nxrecorder> close

## 4、压缩格式文件录制

以下以 <span class="reference">sim</span> 平台为例，演示如何录制 MP3 文件。

1.  安装依赖。
    
    在主机上安装必要的依赖库：  
    
        sudo apt-get install libmp3lame-dev:i386

2.  运行 nxrecorder。
    
    录制 MP3 文件的具体命令如下：  
    
        nxrecorder
        nxrecorder> device pcm1c
        nxrecorder> record /stream/100.mp3 2 16 44100

3.  结束录制。
    
    录制结束后，使用以下命令停止录制并关闭设备：  
    
        nxrecorder> stop
        nxrecorder> close

# 三、nxlooper 使用指南

## 1、简介

<span class="reference">nxlooper</span> 是一个命令行音频回环测试程序，用于测试音频设备的回环功能。代码位于 <span class="reference">apps/system/nxlooper</span> 目录。

## 2、nxlooper 命令说明

运行 <span class="reference">nxlooper</span> 后，可通过输入 <span class="reference">help</span> 查看支持的命令。以下是常用命令及其功能说明：  

    nxlooper> help
    NxLooper commands
    ================
      device devfile                                  : Specify a preferred play/record device
      h                                               : Display help for commands
      help                                            : Display help for commands
      loopback channels bpsamp samprate format chmap  : Audio loopback test
      pause                                           : Pause loopback
      resume                                          : Resume loopback
      stop                                            : Stop loopback
      q                                               : Exit NxLooper
      quit                                            : Exit NxLooper
      volume d%                                       : Set volume to level specified

  - <span class="reference">device devfile</span>：指定一个首选的播放/录制设备。
  - <span class="reference">h</span> / <span class="reference">help</span>：显示命令帮助。
  - <span class="reference">loopback channels bpsamp samprate format chmap</span>：执行音频回环测试，其中 <span class="reference">bpsamp</span> 表示采样位宽（bit width）。
  - <span class="reference">pause</span>：暂停回环测试。
  - <span class="reference">resume</span>：恢复回环测试。
  - <span class="reference">stop</span>：停止回环测试。
  - <span class="reference">q</span> / <span class="reference">quit</span>：退出 NxLooper。
  - <span class="reference">volume d%</span>：将音量设置为指定的百分比。

## 3、PCM 格式回环测试

以下是 PCM 格式音频回环测试的具体操作步骤：  

    nxlooper
    nxlooper> device pcm0p
    nxlooper> device pcm0c
    nxlooper> loopback 2 16 48000

  - 参数说明：
      - <span class="reference">channels</span>：设置声道数为 2。
      - <span class="reference">bpsamp</span>：设置采样精度为 16 位。
      - <span class="reference">samprate</span>：设置采样率为 48000 Hz。

测试结束后，可通过以下命令停止回环并关闭设备：  

    nxlooper> stop
    nxlooper> close

## 4、压缩格式回环测试

以下以 <span class="reference">sim</span> 平台为例，演示 MP3 格式音频回环测试的具体操作步骤：  

    nxlooper
    nxlooper> device pcm1p
    nxlooper> device pcm1c
    
    nxlooper> loopback 2 16 44100 8

参数说明：

  - <span class="reference">channels</span>：设置声道数为 2。
  - <span class="reference">bpsamp</span>：设置采样精度为 16 位。
  - <span class="reference">samprate</span>：设置采样率为 44100 Hz。
  - <span class="reference">format</span>：设置格式为 <span class="reference">8</span>，表示 MP3 格式。

<span class="reference">format</span> 参数的值定义在 <span class="reference">nuttx/include/nuttx/audio/audio.h</span> 文件中。以下是部分格式的定义：  

    #define AUDIO_FMT_UNDEF             0x00
    #define AUDIO_FMT_OTHER             0x01
    #define AUDIO_FMT_MPEG              0x02
    #define AUDIO_FMT_AC3               0x03
    #define AUDIO_FMT_WMA               0x04
    #define AUDIO_FMT_DTS               0x05
    #define AUDIO_FMT_PCM               0x06
    #define AUDIO_FMT_WAV               0x07
    #define AUDIO_FMT_MP3               0x08
    #define AUDIO_FMT_MIDI              0x09
    #define AUDIO_FMT_OGG_VORBIS        0x0a
    #define AUDIO_FMT_FLAC              0x0b
    #define AUDIO_FMT_SBC               0x0c
    #define AUDIO_FMT_AAC               0x0d
    #define AUDIO_FMT_MSBC              0x0e
    #define AUDIO_FMT_CVSD              0x0f
    #define AUDIO_FMT_AMR               0x10
    #define AUDIO_FMT_OPUS              0x11

测试结束后，可通过以下命令停止回环并关闭设备：  

    nxlooper> stop 
    nxlooper> close

**说明**：<span class="reference">nxlooper</span> 工具默认处于启用状态。具体配置可参考各自目录下的 <span class="reference">Kconfig</span> 文件。

# 四、CMocka 使用指南

## 1、简介

CMocka 是一个轻量级的 C 语言单元测试框架，提供了一组 API 和工具，用于编写和运行 C 语言单元测试。其主要特点包括：

  - 简单易用：API 简单直观，便于快速编写测试用例。
  - 轻量级：代码库小，易于集成到项目中。
  - 支持多种测试风格：包括传统的 <span class="reference">assert</span> 风格和新的 <span class="reference">expect</span> 风格。
  - 跨平台支持：可在 Linux、Windows、macOS 等多种平台上运行。

## 2、代码位置

CMocka 测试代码位于以下目录：  

    apps/testing/drivertest

相关代码配置如下：  

    ifneq ($(CONFIG_AUDIO),)
    MAINSRC  += drivertest_audio.c
    PROGNAME += cmocka_driver_audio
    endif

## 3、配置说明

在使用 CMocka 测试框架之前，需要确保以下配置已启用：  

    CONFIG_TESTING_CMOCKA=y
    CONFIG_AUDIO=y
    CONFIG_TESTING_DRIVER_TEST=y

## 4、测试程序：cmocka\_driver\_audio

<span class="reference">cmocka\_driver\_audio</span> 是用于测试音频驱动的测试程序。以下是其使用说明：  

    usage:
    
    1.cmocka_driver_audio need two required parameters:
       -a 2: only playback
       -a 1: only capture
       -a 3: first capture and then playback.
       -p  : filename.  e.g: -p /data/1.pcm
    
    
    2.default audio device is /dev/audio/pcm0p, /dev/audio/pcm0c.
       use -i change record audio device. e.g. -i /dev/audio/pcm1c.
       use -i change playback audio device. e.g. -i /dev/audio/pcm1p.
    3.default format is AUDIO_FMT_PCM
       use -f change format. e.g -f mp3
