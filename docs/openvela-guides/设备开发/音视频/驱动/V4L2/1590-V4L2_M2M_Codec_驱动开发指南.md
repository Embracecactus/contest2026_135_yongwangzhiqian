# V4L2 M2M Codec 驱动开发指南

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1590&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:39  
> 本地拉取日期：2026-08-28

\[[English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/media/v4l2/v4l2_m2m_driver_guide.md) | 简体中文\]

# 一、概述

## 1、目标读者与范围

本指南面向芯片供应商（Vendor）的嵌入式驱动开发工程师。

本文档详细阐述了如何在 <span class="reference">openvela</span> 实时操作系统中，基于 V4L2 M2M (Memory-to-Memory) Codec 框架适配和开发硬件视频编解码器驱动，并介绍了相关的调试方法和最佳实践。

> **说明：** 为保持简洁，本文档将以解码器（Decoder）的适配过程作为主要示例。编码器（Encoder）的适配流程与此高度相似，本文不再赘述共通部分。

## 2、核心代码路径

驱动开发过程主要涉及以下代码文件：

  - **M2M** **框架核心:**
    
      - <span class="reference">drivers/video/v4l2\_m2m.c</span>
      - <span class="reference">drivers/video/v4l2\_core.c</span>
      - <span class="reference">drivers/video/video\_framebuff.c</span>

  - **框架对外头文件:**
    
      - <span class="reference">include/nuttx/video/v4l2\_m2m.h</span>

  - **参考实现:**
    
      - <span class="reference">arch/sim/src/sim/sim\_decoder.c</span> (解码器)
      - <span class="reference">arch/sim/src/sim/sim\_encoder.c</span> (编码器)

# 二、驱动注册与生命周期

V4L2 Codec 驱动通常在系统启动阶段进行注册，其核心流程如下：

![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005755779_006.png)

## 核心步骤

1.  **实现驱动核心逻辑:** 开发者需要根据硬件特性，完整实现 <span class="reference">codec\_ops\_s</span> 结构体中定义的回调函数，并将其封装在 <span class="reference">codec\_s</span> 结构体中。
2.  **注册设备节点:** 在驱动初始化函数中，调用 <span class="reference">codec\_register()</span> 接口。该函数接收设备节点路径（如 <span class="reference">/dev/video0</span>）和已实例化的 <span class="reference">codec\_s</span> 结构体作为参数，向系统注册一个 V4L2 Codec 设备。
3.  **注销设备节点:** 当不再需要该设备时（例如模块卸载时），应调用 <span class="reference">codec\_unregister()</span> 接口来释放资源并移除设备节点。

# 三、驱动实现核心：<span class="reference">codec\_ops\_s</span> 详解

<span class="reference">codec\_ops\_s</span> 结构体是连接 V4L2 M2M 框架与底层硬件编解码器的桥梁。驱动开发者的**首要任务**就是填充此结构体中的函数指针，以响应框架的调用。  

    struct codec_ops_s
    {
      CODE int (*open)(FAR void *cookie, FAR void **priv);
      CODE int (*close)(FAR void *priv);
    
      CODE int (*capture_streamon)(FAR void *priv);
      CODE int (*output_streamon)(FAR void *priv);
      CODE int (*capture_streamoff)(FAR void *priv);
      CODE int (*output_streamoff)(FAR void *priv);
    
      CODE int (*capture_available)(FAR void *priv);
      CODE int (*output_available)(FAR void *priv);
    
      /* VIDIOC_QUERYCAP handler */
    
      CODE int (*querycap)(FAR void *priv,
                           FAR struct v4l2_capability *cap);
    
      /* VIDIOC_ENUM_FMT handlers */
    
      CODE int (*capture_enum_fmt)(FAR void *priv,
                                   FAR struct v4l2_fmtdesc *fmt);
    
      CODE int (*output_enum_fmt)(FAR void *priv,
                                  FAR struct v4l2_fmtdesc *fmt);
    
      /* VIDIOC_G_FMT handlers */
    
      CODE int (*capture_g_fmt)(FAR void *priv,
                                FAR struct v4l2_format *fmt);
      CODE int (*output_g_fmt)(FAR void *priv,
                               FAR struct v4l2_format *fmt);
    
      /* VIDIOC_S_FMT handlers */
    
      CODE int (*capture_s_fmt)(FAR void *priv,
                                FAR struct v4l2_format *fmt);
      CODE int (*output_s_fmt)(FAR void *priv,
                               FAR struct v4l2_format *fmt);
    
      /* VIDIOC_TRY_FMT handlers */
    
      CODE int (*capture_try_fmt)(FAR void *priv,
                                  FAR struct v4l2_format *fmt);
      CODE int (*output_try_fmt)(FAR void *priv,
                                 FAR struct v4l2_format *fmt);
    
      /* Buffer handlers  */
    
      CODE size_t (*capture_g_bufsize)(FAR void *priv);
      CODE size_t (*output_g_bufsize)(FAR void *priv);
    
      /* Stream type-dependent parameter ioctls */
    
      CODE int (*capture_g_parm)(FAR void *priv,
                                 FAR struct v4l2_streamparm *parm);
      CODE int (*output_g_parm)(FAR void *priv,
                                FAR struct v4l2_streamparm *parm);
      CODE int (*capture_s_parm)(FAR void *priv,
                                 FAR struct v4l2_streamparm *parm);
      CODE int (*output_s_parm)(FAR void *priv,
                                FAR struct v4l2_streamparm *parm);
    
      /* Control handlers */
    
      CODE int (*g_ext_ctrls)(FAR void *priv,
                              FAR struct v4l2_ext_controls *ctrls);
      CODE int (*s_ext_ctrls)(FAR void *priv,
                              FAR struct v4l2_ext_controls *ctrls);
    
      /* Crop ioctls */
    
      CODE int (*capture_g_selection)(FAR void *priv,
                                      FAR struct v4l2_selection *clip);
      CODE int (*output_g_selection)(FAR void *priv,
                                     FAR struct v4l2_selection *clip);
      CODE int (*capture_s_selection)(FAR void *priv,
                                      FAR struct v4l2_selection *clip);
      CODE int (*output_s_selection)(FAR void *priv,
                                     FAR struct v4l2_selection *clip);
      CODE int (*capture_cropcap)(FAR void *priv,
                                  FAR struct v4l2_cropcap *cropcap);
      CODE int (*output_cropcap)(FAR void *priv,
                                 FAR struct v4l2_cropcap *cropcap);
    
      /* Event handlers */
    
      CODE int (*subscribe_event)(FAR void *priv,
                                  FAR struct v4l2_event_subscription *sub);
    
      /* Command handlers */
    
      CODE int (*decoder_cmd)(FAR void *priv,
                              FAR struct v4l2_decoder_cmd *cmd);
      CODE int (*encoder_cmd)(FAR void *priv,
                              FAR struct v4l2_encoder_cmd *cmd);
    };

下面将详细说明一下每个接口的核心功能：

<table>
<colgroup>
<col style="width: 50%" />
<col style="width: 50%" />
</colgroup>
<thead>
<tr class="header">
<th style="text-align: left;"><strong>接口名称</strong></th>
<th style="text-align: left;"><strong>核心职责与调用时机</strong></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td style="text-align: left;"><span class="reference">open</span></td>
<td style="text-align: left;">当应用层调用 <span class="reference">open()</span> 打开设备节点时，框架调用此函数。开发者应在此处完成单实例资源的初始化。<br />
<strong>参数说明：</strong><br />
<span class="reference">cookie</span>: M2M 框架层维护的会话句柄，用于后续调用框架提供的 API (如 <span class="reference">codec_*_get_buf</span>)。<br />
<span class="reference">priv</span>: 由驱动分配并返回的私有数据指针，用于存储该实例的上下文。框架会将其透传给后续的其他回调函数。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">close</span></td>
<td style="text-align: left;">当应用层调用 <span class="reference">close()</span> 关闭设备节点时，框架调用此函数。<br />
开发者应在此处释放 <span class="reference">open</span> 时分配的私有资源。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">output_streamon</span></td>
<td style="text-align: left;">响应 <span class="reference">VIDIOC_STREAMON</span> (<span class="reference">OUTPUT</span> 队列)。<br />
此时输入格式已确定，驱动可以获取到解码图像格式，完成 Decoder 的初始化工作。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">capture_streamon</span></td>
<td style="text-align: left;">响应 <span class="reference">VIDIOC_STREAMON</span> (<span class="reference">CAPTURE</span> 队列)。<br />
此时缓冲区已准备就绪，可以启动工作队列（<span class="reference">work_queue</span>）开始数据处理。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">output_streamoff</span></td>
<td style="text-align: left;">响应 <span class="reference">VIDIOC_STREAMOFF</span> (<span class="reference">OUTPUT</span> 队列)。<br />
停止接收新的输入数据。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">capture_streamoff</span></td>
<td style="text-align: left;">响应 <span class="reference">VIDIOC_STREAMOFF</span> (<span class="reference">CAPTURE</span> 队列)。<br />
停止处理和输出数据，并确保硬件内部缓存的数据被清空（flush）。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">output_available</span></td>
<td style="text-align: left;">当应用层通过 <span class="reference">QBUF</span> 向 <span class="reference">OUTPUT</span> 队列提供一帧待处理数据（如H.264码流）时，框架调用此函数。<br />
通常在此触发一次工作队列以处理新数据，底层解码器可以准备解码。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">capture_available</span></td>
<td style="text-align: left;">当应用层通过 <span class="reference">QBUF</span> 将一个空的 <span class="reference">CAPTURE</span> 缓冲区归还给驱动时，框架调用此函数。<br />
通常在此触发一次工作队列以填充此缓冲区。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">querycap</span></td>
<td style="text-align: left;">响应 <span class="reference">VIDIOC_QUERYCAP</span>。<br />
填充 <span class="reference">v4l2_capability</span> 结构体，向应用层报告驱动的能力，如设备类型、是否支持流控等。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">output_enum_fmt</span></td>
<td style="text-align: left;">响应 <span class="reference">VIDIOC_ENUM_FMT</span> (<span class="reference">OUTPUT</span> 队列)。<br />
枚举驱动支持的输入数据格式：<br />
解码器：<span class="reference">V4L2_PIX_FMT_H264</span> 等。<br />
编码器：<span class="reference">V4L2_PIX_FMT_YUV420</span> 等。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">capture_enum_fmt</span></td>
<td style="text-align: left;">响应 <span class="reference">VIDIOC_ENUM_FMT</span> (<span class="reference">CAPTURE</span> 队列)。<br />
枚举驱动支持的输出数据格式：<br />
解码器：<span class="reference">V4L2_PIX_FMT_YUV420</span> 等。<br />
编码器：<span class="reference">V4L2_PIX_FMT_H264</span> 等。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">output_s_fmt</span> / <span class="reference">capture_s_fmt</span></td>
<td style="text-align: left;">响应 <span class="reference">VIDIOC_S_FMT</span>。<br />
设置输入/输出队列的像素格式、分辨率等参数。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">output_g_fmt</span> / <span class="reference">capture_g_fmt</span></td>
<td style="text-align: left;">响应 <span class="reference">VIDIOC_G_FMT</span>。<br />
获取当前输入/输出队列的格式。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">output_try_fmt</span> / <span class="reference">capture_try_fmt</span></td>
<td style="text-align: left;">响应 <span class="reference">VIDIOC_TRY_FMT</span>。<br />
校验并调整应用层尝试设置的格式。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">output_g_bufsize</span></td>
<td style="text-align: left;">返回 <span class="reference">OUTPUT</span> 队列中单个缓冲区的建议大小。<br />
对于解码器，应设置为能容纳的最大压缩帧（如最大 I 帧）的 size。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">capture_g_bufsize</span></td>
<td style="text-align: left;">返回 <span class="reference">CAPTURE</span> 队列中单个缓冲区的建议大小。<br />
对于解码器，这是一帧解码后原始图像（如 YUV）的大小 (w * h * 3 / 2)。<br />
对于编码器，size 大小为编码后压缩数据的最大帧大小。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">alloc_buf</span>/<span class="reference">free_buf</span></td>
<td style="text-align: left;"><strong>可选。</strong><br />
当前 M2M mmap buffer 模式内部使用 <span class="reference">kumm_memalign(align:32)</span> 内存分配接口分配内存。<br />
若硬件对内存（如物理连续）有特殊要求，则实现这两个函数以覆盖框架默认的内存分配行为。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">decoder_cmd</span></td>
<td style="text-align: left;"><span class="reference">VIDIOC_DECODER_CMD</span>：处理解码控制命令，如 <span class="reference">START</span>, <span class="reference">STOP</span>, <span class="reference">PAUSE</span>, <span class="reference">FLUSH</span>。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">encoder_cmd</span></td>
<td style="text-align: left;"><span class="reference">VIDIOC_ENCODER_CMD</span>：处理编码控制命令，如 <span class="reference">START</span>, <span class="reference">STOP</span>, <span class="reference">PAUSE</span>。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">g_ext_ctrls</span>/<span class="reference">s_ext_ctrls</span></td>
<td style="text-align: left;"><span class="reference">VIDIOC_G_EXT_CTRLS</span> / <span class="reference">VIDIOC_S_EXT_CTRLS</span>：批量获取或设置扩展控制参数，如编码器的 GOP、码率、Profile 等。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">output_g/s_parm</span> <span class="reference">capture_g/s_parm</span></td>
<td style="text-align: left;"><span class="reference">VIDIOC_G_PARM</span> / <span class="reference">VIDIOC_S_PARM</span>：获取或设置流参数，如帧率和场格式。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">output_g/s_selection</span> <span class="reference">capture_g/s_selection</span></td>
<td style="text-align: left;">获取或设置输入/输出端处理的区域。</td>
</tr>
<tr class="odd">
<td style="text-align: left;"><span class="reference">capture_cropcap</span>/<span class="reference">output_cropcap</span></td>
<td style="text-align: left;"><span class="reference">VIDIOC_CROPCAP</span>：获取输入/输出端裁剪参数。</td>
</tr>
<tr class="even">
<td style="text-align: left;"><span class="reference">subscribe_event</span></td>
<td style="text-align: left;"><span class="reference">VIDIOC_SUBSCRIBE_EVENT</span>：允许应用层订阅驱动事件，如 <span class="reference">V4L2_EVENT_EOS</span> 。</td>
</tr>
</tbody>
</table>

# 四、M2M 框架辅助 API

V4L2 M2M 框架为下层驱动提供了一系列辅助 API，用于简化设备管理、缓冲区交互和事件通知。

## 1、设备注册与注销

    /* 注册 V4L2 M2M Codec 设备 */
    int codec_register(FAR const char *devpath, FAR struct codec_s *codec);
    
    /* 注销 V4L2 M2M Codec 设备 */
    int codec_unregister(FAR const char *devpath);

## 2、缓冲区交互

驱动通过以下 API 与 M2M 框架进行数据缓冲区的获取与归还，这是实现数据流处理的核心。  

    // 从m2m获取buffer
    // 解码场景：
    // - output queue存放视频压缩数据，output_get_buf即从m2m output queue获取一帧压缩数据。
    // - capture queue存放解码后的视频帧，capture_get_buf即从m2m capture queue中获取一个空闲buffer等待被填充解码后的数据。
    
    // 编码场景：
    // - output queue存放视频Raw数据，output_get_buf即从m2m output queue获取一帧Raw数据。
    // - capture queue存放编码压缩数据，capture_get_buf即从m2m capture queue中获取一个空闲buffer,等待被填充编码后的数据。
    
    // 从 OUTPUT 队列获取一个待处理的缓冲区
    FAR struct v4l2_buffer *codec_output_get_buf(FAR void *cookie);
    
    // 从 CAPTURE 队列获取一个用于填充结果的空闲缓冲区
    FAR struct v4l2_buffer *codec_capture_get_buf(FAR void *cookie);
    
    // 将已处理完的 OUTPUT 缓冲区归还给框架
    int codec_output_put_buf(FAR void *cookie, FAR struct v4l2_buffer *buf);
    
    // 将已填充数据的 CAPTURE 缓冲区归还给框架，使其对应用层可见
    int codec_capture_put_buf(FAR void *cookie, FAR struct v4l2_buffer *buf);

**使用场景示例 (解码器):**

1.  在工作队列中，调用 <span class="reference">codec\_output\_get\_buf()</span> 获取一帧待解码的压缩数据（如 H.264）。
2.  调用 <span class="reference">codec\_capture\_get\_buf()</span> 获取一个用于存放解码结果（如 YUV）的空闲缓冲区。
3.  将压缩数据送入硬件解码，并将解码后的 YUV 数据直接填充到指定的缓冲区。
4.  调用 <span class="reference">codec\_output\_put\_buf()</span> 归还已使用的压缩数据缓冲区。
5.  调用 <span class="reference">codec\_capture\_put\_buf()</span> 归还已填充解码结果的缓冲区。

## 3、事件通知

驱动可以使用此 API 主动向应用层发送异步事件。  

    // driver发送事件给应用，比如：编解码结束的时候，发送EOS事件。
    int codec_queue_event(FAR void *cookie, FAR struct v4l2_event *evt);

# 五、关键开发注意事项与最佳实践

## 1、压缩数据缓冲区大小设置

为压缩数据队列（解码器的 <span class="reference">OUTPUT</span> 队列，编码器的 <span class="reference">CAPTURE</span> 队列）设置一个合理的缓冲区大小至关重要。该大小通过实现 <span class="reference">output\_g\_bufsize</span> 或 <span class="reference">capture\_g\_bufsize</span> 来定义，并在应用层调用 <span class="reference">VIDIOC\_REQBUFS</span> 时生效。

  - **解码器 (<span class="reference">output\_g\_bufsize</span>)**: 输入的压缩帧大小不固定。建议将缓冲区大小设置为一个安全的上限值，例如目标分辨率下原始图像大小的一半。
    
      - **示例**: 对于 <span class="reference">640x480</span> 的 <span class="reference">YUV420P</span> 格式，原始图像大小为 <span class="reference">640 \* 480 \* 3 / 2 = 460800</span> 字节。可将输入缓冲区大小设置为 <span class="reference">230400</span> 字节。

  - **编码器 (<span class="reference">capture\_g\_bufsize</span>)**: 输出的压缩帧大小同样不固定。建议将其设置为硬件可能产生的最大I帧（I-frame）的大小。

## 2、实现零拷贝（Zero-Copy）数据流

<span class="reference">openvela</span> V4L2 M2M 框架的设计旨在促进零拷贝数据流，以最大化性能。驱动应避免在内部进行不必要的数据拷贝，并让框架管理缓冲区的生命周期。

### 默认内存管理模式

如果硬件对内存没有特殊要求，驱动应完全依赖 M2M 框架进行内存管理。

1.  **内存分配**: 应用层调用 <span class="reference">VIDIOC\_REQBUFS</span> 时，M2M 框架会根据驱动提供的 <span class="reference">g\_bufsize</span> 回调，使用 <span class="reference">kumm\_memalign(32, ...)</span> 统一分配所有缓冲区。

2.  **数据处理 (以解码器为例)**:
    
      - **输入**: 驱动通过 <span class="reference">codec\_output\_get\_buf()</span> 获取的缓冲区地址，直接传递给硬件进行解码。
      - **输出**: 硬件将解码结果直接写入从 <span class="reference">codec\_capture\_get\_buf()</span> 获取的缓冲区地址。

3.  **内存释放**: 应用层关闭设备时，框架自动释放所有缓冲区。

### 自定义内存分配模式

如果硬件要求使用特殊的内存（如物理连续、特定地址范围等），驱动需要适配 <span class="reference">alloc\_buf</span> 和 <span class="reference">free\_buf</span> 回调。

1.  **实现接口**: 在 <span class="reference">codec\_ops\_s</span> 中提供 <span class="reference">alloc\_buf</span> 和 <span class="reference">free\_buf</span> 的具体实现，内部调用芯片平台专用的内存分配器。
2.  **数据流**: 缓冲区交互流程与默认模式完全相同，驱动依然通过 <span class="reference">get\_buf</span>/<span class="reference">put\_buf</span> API 与框架交互，实现了零拷贝。

# 六、实践案例：Simulator 驱动

<span class="reference">openvela</span> 提供了一套基于 openH264 (解码) 和 x264 (编码) 的模拟器驱动。它们是学习和开发 V4L2 M2M 驱动的最佳参考。

## 1、环境配置

在 <span class="reference">menuconfig</span> 中启用以下配置项，即可在 i386 模拟器环境中使用编解码能力。

### Video Decoder 配置

    CONFIG_SIM_VIDEO_DECODER=y
    CONFIG_SIM_VIDEO_DECODER_DEV_PATH="/dev/video1"
    CONFIG_VIDEOUTILS_OPENH264=y

### Video Encoder 配置

    CONFIG_SIM_VIDEO_ENCODER=y
    CONFIG_SIM_ENCODER_DEV_PATH="/dev/video2"
    CONFIG_VIDEOUTILS_LIBX264=y

### 通用视频依赖项

    CONFIG_VIDEO=y
    CONFIG_DRIVERS_VIDEO=y
    CONFIG_VIDEO_STREAM=y

## 2、Simulator Decoder 详解

### 初始化流程

<span class="reference">sim\_decoder</span> 驱动在系统启动阶段通过 <span class="reference">sim\_decoder\_initialize</span> 函数调用 <span class="reference">codec\_register</span>，从而在 VFS 中创建设备节点 <span class="reference">/dev/video1</span>。当应用层 <span class="reference">open</span> 该节点时，会触发 <span class="reference">codec\_open</span> 函数，进而调用驱动的 <span class="reference">open</span> 回调，完成实例的创建和缓冲区初始化。

![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005755876_007.png)

### 缓冲区处理流程

<span class="reference">sim\_decoder</span> 的核心解码任务在一个工作队列 (<span class="reference">sim\_decoder\_work</span>) 中异步执行。该任务由 <span class="reference">sim\_decoder\_output\_available</span> 和 <span class="reference">sim\_decoder\_capture\_available</span> 回调触发。

![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005755967_008.png)

### Ops 实现解析 (<span class="reference">g\_sim\_decoder\_ops</span>)

<span class="reference">g\_sim\_decoder\_ops</span> 是 <span class="reference">sim\_decoder</span> 驱动对 <span class="reference">codec\_ops\_s</span> 接口的具体实现。实现的 API 如下：

  - **流控制接口 (<span class="reference">streamon</span>/<span class="reference">streamoff</span>)**
    
      - <span class="reference">sim\_decoder\_output\_streamon</span>: 此回调被触发时，初始化 openH264 解码器实例，并配置相关参数。
      - <span class="reference">sim\_decoder\_capture\_streamon</span>: 此回调被触发时，表明 M2M 层的缓冲区已准备就绪，此时调度工作队列开始解码。
      - <span class="reference">sim\_decoder\_output\_streamoff</span>: 设置 flush 状态，并启动工作队列，以处理解码器中所有剩余的缓冲帧。
      - <span class="reference">sim\_decoder\_capture\_streamoff</span>: 关闭并释放 openH264 解码器实例。

  - **数据可用性接口 (<span class="reference">available</span>)**
    
      - <span class="reference">sim\_decoder\_output\_available</span> / <span class="reference">sim\_decoder\_capture\_available</span>: 当有新的输入数据或可用的输出缓冲区时，M2M 通用层调用这些回调。它们通常只做一件事：触发工作队列执行实际的解码工作。

  - **<span class="reference">g\_bufsize</span> 接口(openvela 扩展)**
    
      - <span class="reference">capture\_g\_bufsize</span> / <span class="reference">output\_g\_bufsize</span>: 这两个接口是 <span class="reference">openvela</span> 的特定扩展，用于让下层驱动根据当前格式（分辨率、像素格式等）计算并返回精确的缓冲区大小。M2M 通用层在分配内存时会使用这个返回值。这与 Linux V4L2 通过 <span class="reference">S\_FMT</span> 协商大小的方式有所不同，是 <span class="reference">openvela</span> 实现的一个特点。

  - **格式协商接口 (<span class="reference">xxx\_fmt</span>)**
    
      - 这些接口（如 <span class="reference">capture\_enum\_fmt</span>, <span class="reference">output\_g\_fmt</span> 等）的实现与标准 Linux V4L2 驱动类似，负责查询和设置设备支持的像素格式、分辨率等。

## 3、Simulator Encoder

<span class="reference">sim\_encoder</span> 的驱动实现与 <span class="reference">sim\_decoder</span> 在结构上高度相似，主要区别在于数据流方向相反，并调用 x264 库进行编码。开发者可直接参考其源码进行学习。

<span class="reference">openvela</span> 在 <span class="reference">arch/sim/src/sim/sim\_decoder.c</span> 中提供了一个功能完整的解码器驱动范例。我们强烈建议开发者在开始适配前，详细研究此文件的实现。

其处理逻辑和设计模式可以参考[V4L2 M2M 框架介绍](https://doc.openvela.com/document?id=1589&version=dev-ai-contest-2026&language=cn)。

# 七、驱动调试与测试

驱动开发完成后，<span class="reference">openvela</span> 提供了多种工具进行功能验证和性能调试。

## 1、<span class="reference">nxcodec</span> 测试工具

<span class="reference">nxcodec</span> 是一个命令行工具，专门用于直接测试 V4L2 Codec 驱动的 <span class="reference">ioctl</span> 接口和基本编解码功能。对于驱动开发初期的功能验证，此工具是首选。使用说明请参考 [nxcodec 用户指南](https://doc.openvela.com/document?id=1592&version=dev-ai-contest-2026&language=cn)。

## 2、FFmpeg 测试工具

在真实应用场景中，上层多媒体应用通常通过 <span class="reference">FFmpeg</span> 来调用 V4L2 M2M 驱动。因此，通过 <span class="reference">FFmpeg</span> 和 <span class="reference">mediatool</span> 进行集成测试是确保驱动稳定性和兼容性的关键一步。使用说明请参考 [FFmpeg V4L2 M2M 使用指南](https://doc.openvela.com/document?id=1591&version=dev-ai-contest-2026&language=cn)。
