# VirtIO 框架

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1480&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:50:42  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/kernel/inter_processor_communication/VirtIO/VirtIO_Framework.md) | 简体中文 \]

# 一、简介

openvela 基于 OpenAMP 实现了完整的 VirtIO 框架。该框架在上层支持实现与 VirtIO 标准兼容的多种 VirtIO 驱动，例如 VirtIO-Net 和 VirtIO-Block 等；在下层支持不同的 VirtIO 传输层实现，包括 VirtIO-MMIO 和 VirtIO-PCI 等。

# 二、架构图

## 1、框架图

下图展示了 openvela VirtIO 框架的整体结构，可以分为以下三部分：

1.  驱动层：
    
    驱动层负责将 VirtIO 与 openvela 驱动框架对接。驱动层通过调用 VirtIO 提供的统一接口，完成设备的初始化和数据交互。

2.  VirtIO 层：
    
    VirtIO 层为驱动提供统一的接口，支持 Driver 和 Device 的注册、卸载以及匹配机制。

3.  传输层：
    
    传输层提供对不同传输方式的支持，包括 MMIO、RemoteProc 和 PCI 等。

![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005735422_011.svg)

## 2、流程图

![img](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005735511_012.svg)

上图展示了 VirtIO Device 和 VirtIO Driver 的匹配流程及调用关系：

1.  Driver 注册。
    
    在 openvela 初始化时，调用 <span class="reference">virtio\_register\_drivers()</span> 将所有已支持的 VirtIO Drivers 注册到 VirtIO 总线中。

2.  Device 注册。由传输层发起注册流程：
    
      - MMIO 传输层调用 <span class="reference">virtio\_register\_mmio\_device()</span>。
      - REMOTEPROC 传输层调用 <span class="reference">rptun\_register\_device()</span>。
      - PCI 传输层调用 <span class="reference">virtio\_pci\_probe()</span>。 传输层完成初始化后，调用 <span class="reference">virtio\_register\_device()</span> 将 VirtIO Device 注册到 VirtIO 总线中。

3.  Driver 和 Device 匹配。
    
    在设备注册到总线时，系统会尝试匹配 Driver 和 Device。如果匹配成功，执行 Driver 实现的 <span class="reference">probe</span> 函数。在 <span class="reference">probe</span> 函数中，驱动会对 VirtIO Device 进行初始化、配置、特性协商（feature negotiation）等操作。根据设备的复杂度和类型，可能还需要初始化私有结构或执行额外操作。

4.  注册 openvela 驱动。
    
    调用 openvela 驱动框架提供的 API，将驱动注册到虚拟文件系统（VFS）中，供用户使用。

5.  运行。
    
    在运行过程中，Driver 会通过调用 OpenAMP 提供的 <span class="reference">virtqueue</span> 通用接口，按照 VirtIO 标准格式进行数据交换和通知，从而实现驱动功能。

# 三、代码目录

    |--- nuttx
    |    |--- drivers
    |    |    |--- virtio
    |    |         |--- virtio.c       # VirtIO框架核心实现
    |    |--- include
    |    |    |--- nuttx
    |    |         |--- virtio
    |    |              |--- virtio.h  # VirtIO头文件
    |    |--- openamp
    |    |    |--- open-amp            # OpenAMP仓库

# 四、API 说明

本章节对 VirtIO 驱动适配过程中需要调用的接口进行说明

## 1、openvela Log 接口

  - <span class="reference">vrtinfo(...)</span>
    
    描述：INFO 级别的 VirtIO 系统日志接口。

  - <span class="reference">vrtwarn(...)</span>
    
    描述：WARNING 级别的 VirtIO 系统日志接口。

  - <span class="reference">vrterr(...)</span>
    
    描述：ERROR 级别的 VirtIO 系统日志接口。

## 2、openvela VirtIO 框架接口

  - <span class="reference">int virtio\_register\_driver(FAR struct virtio\_driver \*driver)</span>
    
    描述：注册一个 VirtIO Driver 到 VirtIO 总线。当总线中已经存在对应的设备时，会立即匹配并调用驱动实现的 <span class="reference">probe</span> 函数。如果总线中没有对应的设备，则在有对应的 VirtIO 设备注册到 VirtIO 总线后，回调驱动的 <span class="reference">probe</span> 函数以完成驱动初始化。

## 3、OpenAMP 接口

### 前置知识

  - Driver TX virtqueue：
    
    驱动的发送队列。从 <span class="reference">txvq</span> 的 <span class="reference">used ring</span> 中获取 buffer，填充需要发送的数据后，再将其添加到 <span class="reference">txvq</span> 的 <span class="reference">avail ring</span> 中，完成数据发送流程。

  - Driver RR virtqueue：
    
    驱动的接收队列。从 <span class="reference">rxvq</span> 的 <span class="reference">used ring</span> 中获取 buffer，读取其中的数据后，再将其返回到 <span class="reference">rxvq</span> 的 <span class="reference">avail ring</span> 中，完成数据接收流程。

### 接口说明

  - <span class="reference">void \*virtqueue\_get\_buffer(struct virtqueue \*vq, uint32\_t \*len, uint16\_t \*idx)</span>
    
    描述：从 virtqueue 的 <span class="reference">used ring</span> 中获取一个 buffer。
    
    参数：
    
      - <span class="reference">vq</span>：指向 virtqueue 的指针。
      - <span class="reference">len</span>：获取的 buffer 的长度。
      - <span class="reference">idx</span>：获取的 buffer 在 <span class="reference">used ring</span> 中的索引。

  - <span class="reference">int virtqueue\_add\_buffer(struct virtqueue \*vq, struct virtqueue\_buf \*buf\_list, int readable, int writable, void \*cookie)</span>
    
    描述：将一个 buffer 添加到 <span class="reference">virtqueue</span> 的 <span class="reference">avail ring</span> 中。
    
    参数：
    
      - <span class="reference">vq</span>：指向 virtqueue 的指针。
      - <span class="reference">buf\_list</span>：需要添加的 buffer 数组。
      - <span class="reference">readable</span>：<span class="reference">buf\_list</span> 中可读 buffer 的数量，表示希望设备（Device）读取的部分。
      - <span class="reference">writable</span>：<span class="reference">buf\_list</span> 中可写 buffer 的数量，表示希望设备（Device）填充的部分。
      - <span class="reference">cookie</span>：缓存指针，在调用 <span class="reference">virtqueue\_get\_buffer</span> 获取 buffer 时会返回该值。

  - <span class="reference">void virtqueue\_kick(struct virtqueue \*vq)</span>
    
    描述：通知设备（Device）。通常在向设备端发送数据或将 buffer 返回给设备端后，调用此函数通知设备可以进行下一步操作。
    
    参数：
    
      - <span class="reference">vq</span>：指向 virtqueue 的指针。

  - <span class="reference">virtqueue\_enable\_cb(struct virtqueue \*vq)</span> 和 <span class="reference">virtqueue\_disable\_cb(struct virtqueue \*vq)</span>
    
    描述：使能或关闭 virtqueue 的中断。
    
    参数：
    
      - <span class="reference">vq</span>：指向 virtqueue 的指针。

# 五、相关文档

  - [virtio: Towards a De-Facto Standard For Virtual I/O Devices](https://ozlabs.org/~rusty/virtio-spec/virtio-paper.pdf)
  - [Virtual I/O Device (VIRTIO) Version 1.2](https://docs.oasis-open.org/virtio/virtio/v1.2/csd01/virtio-v1.2-csd01.pdf)
