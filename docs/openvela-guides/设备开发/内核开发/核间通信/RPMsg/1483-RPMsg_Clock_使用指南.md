# RPMsg Clock 使用指南

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1483&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:50:43  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/kernel/inter_processor_communication/RPMsg/RPMsg_Clock.md) | 简体中文 \]

# 一、概述

RPMsg Clock（Remote Processor Messaging Clock）是一种基于 RPMsg 框架（Remote Processor Messaging Framework）构建的跨核时钟服务，用于实现跨核的时钟控制。

# 二、配置

在使用 RPMsg Clock 时，需要确保以下配置已启用：  

    # server端和client端均需要使能如下配置
    CONFIG_CLK_RPMSG=y

  - Server 端：包含实际时钟系统的核，负责提供时钟资源。
  - Client 端 ：无实际时钟系统，需要访问或控制 Server 端时钟资源的核。

# 三、使用方法

## 1、注册时钟资源

Client 端能够访问或控制 Server 端时钟资源的前提是，Server 端已完成实际时钟子系统的初始化，即通过调用 <span class="reference">clk\_register</span> 完成时钟资源的注册。Client 端无需进行时钟注册。

有关时钟注册的示例代码，请参考以下链接：[nuttx/drivers/clk/song](https://github.com/FishsemiCode/nuttx/tree/song-u1/drivers/clk/song)

## 2、获取时钟实例

在访问或控制时钟资源时，需要通过时钟资源的名称来获取时钟实例。

以下是获取时钟实例的函数定义：  

    /* name为需要控制的clk源的名称 */
    FAR struct clk_s *clk_get(FAR const char *name)；

### 参数说明

  - Server 端：
      - <span class="reference">clk\_get</span> 函数的参数为调用 <span class="reference">clk\_register</span> 时传入的时钟资源名称。
  - Client 端：
      - <span class="reference">clk\_get</span> 函数的参数需要包含 Server 端的 CPU 名称（<span class="reference">cpuname</span>）以及时钟资源名称。
      - 例如：假设 Server 端的 CPU 名称为 <span class="reference">"ap"</span>，需要访问的时钟资源名称为 <span class="reference">"spi\_clk"</span>，则 Client 端调用 <span class="reference">clk\_get</span> 函数时，传入的参数应为 <span class="reference">"ap/spi\_clk"</span>。

# 四、工作原理

## 1、RPMsg 消息处理机制

在 clk driver 中，时钟源通过 <span class="reference">clk\_register</span> 注册到时钟子系统。当需要访问或控制某个时钟源时，可以通过 <span class="reference">clk\_get</span> 获取该时钟源的实例，然后通过该实例关联的 <span class="reference">clk-\>ops</span> 结构体对时钟源进行操作。

在 RPMsg Clk 中，<span class="reference">clk-\>ops</span> 被抽象为以下操作接口：  

    const struct clk_ops_s g_clk_rpmsg_ops =
    {
      .enable = clk_rpmsg_enable,
      .disable = clk_rpmsg_disable,
      .is_enabled = clk_rpmsg_is_enabled,
      .recalc_rate = clk_rpmsg_recalc_rate,
      .round_rate = clk_rpmsg_round_rate,
      .set_rate = clk_rpmsg_set_rate,
      .set_phase = clk_rpmsg_set_phase,
      .get_phase = clk_rpmsg_get_phase,
    };

操作流程

  - Client 端请求转发： <span class="reference">g\_clk\_rpmsg\_ops</span> 中的函数实现会将请求转发给 Server 端。转发的目标由时钟源名称中的 <span class="reference">cpuname</span> 字符串确定。
  - Server 端处理请求： Server 端完成实际的函数调用，并将结果返回给 Client 端。

## 2、启用时钟的处理流程

例如 <span class="reference">clk\_rpmsg\_enable</span> 函数会向 Server 端发送 <span class="reference">CLK\_RPMSG\_ENABLE</span> 请求。以下是 <span class="reference">clk\_rpmsg\_enable</span> 函数的实现，它负责将启用时钟的请求从 Client 端转发到 Server 端：  

    static int clk_rpmsg_enable(FAR struct clk_s *clk)
    {
      FAR struct rpmsg_endpoint *ept;
      FAR struct clk_rpmsg_enable_s *msg;
      FAR const char *name = clk->name;
      uint32_t size;
      uint32_t len;
    
      ept = clk_rpmsg_get_ept(&name);
      if (!ept)
        {
          return -ENODEV;
        }
    
      len = sizeof(*msg) + strlen(name) + 1;
    
      msg = rpmsg_get_tx_payload_buffer(ept, &size, true);
      if (!msg)
        {
          return -ENOMEM;
        }
    
      DEBUGASSERT(len <= size);
    
      strlcpy(msg->name, name, size - sizeof(*msg));
    
      return clk_rpmsg_sendrecv(ept, CLK_RPMSG_ENABLE,
                               (struct clk_rpmsg_header_s *)msg,
                                len);
    }

### 处理逻辑说明

1.  获取通信端点：
      - 函数通过 <span class="reference">clk\_rpmsg\_get\_ept</span> 获取与目标 Server 端的通信端点。如果通信端点不存在，则返回错误代码 <span class="reference">-ENODEV</span>。
2.  构造消息：
      - 函数通过 <span class="reference">rpmsg\_get\_tx\_payload\_buffer</span> 分配消息缓冲区，并将时钟名称复制到消息中。
3.  发送请求并接收响应：
      - 函数调用 <span class="reference">clk\_rpmsg\_sendrecv</span> 将 <span class="reference">CLK\_RPMSG\_ENABLE</span> 请求发送到 Server 端，并等待响应。

## 3、Server 端的请求处理

当 Server 端接收到 <span class="reference">CLK\_RPMSG\_ENABLE</span> 请求时，会调用对应的处理函数 <span class="reference">clk\_rpmsg\_enable\_handler</span>。

以下是消息处理函数的注册表：  

    static const rpmsg_ept_cb g_clk_rpmsg_handler[] =
    {
      [CLK_RPMSG_ENABLE]    = clk_rpmsg_enable_handler,
      [CLK_RPMSG_DISABLE]   = clk_rpmsg_disable_handler,
      [CLK_RPMSG_SETRATE]   = clk_rpmsg_setrate_handler,
      [CLK_RPMSG_SETPHASE]  = clk_rpmsg_setphase_handler,
      [CLK_RPMSG_GETPHASE]  = clk_rpmsg_getphase_handler,
      [CLK_RPMSG_GETRATE]   = clk_rpmsg_getrate_handler,
      [CLK_RPMSG_ROUNDRATE] = clk_rpmsg_roundrate_handler,
      [CLK_RPMSG_ISENABLED] = clk_rpmsg_isenabled_handler,
    };

以下是 <span class="reference">clk\_rpmsg\_enable\_handler</span> 的具体实现，它负责调用实际的时钟启用函数：  

    static int clk_rpmsg_enable_handler(FAR struct rpmsg_endpoint *ept,
                                        FAR void *data, size_t len,
                                        uint32_t src, FAR void *priv)
    {
      FAR struct clk_rpmsg_enable_s *msg = data;
      FAR struct clk_rpmsg_s *clkrp = clk_rpmsg_get_clk(ept, msg->name);
    
      if (clkrp)
        {
          msg->header.result = clk_enable(clkrp->clk);
          if (!msg->header.result)
            {
              clkrp->count++;
            }
        }
      else
        {
          msg->header.result = -ENOENT;
        }
    
      return rpmsg_send(ept, msg, sizeof(*msg));
    }

### 处理逻辑说明：

1.  获取时钟实例：
      - 函数通过 <span class="reference">clk\_rpmsg\_get\_clk</span> 获取指定名称的时钟实例。
      - 如果时钟实例存在，则调用 <span class="reference">clk\_enable</span> 函数启用时钟。
      - 如果时钟实例不存在，则返回错误代码 <span class="reference">-ENOENT</span>。
2.  更新计数器：
      - 如果时钟启用成功（<span class="reference">clk\_enable</span> 返回 0），则增加时钟的引用计数 <span class="reference">clkrp-\>count</span>。
3.  返回结果：
      - 通过 <span class="reference">rpmsg\_send</span> 将操作结果返回给 Client 端。

# 五、相关文档

  - 有关时钟驱动的设计，请参考 [Clock](https://doc.openvela.com/document?id=1603&version=dev-ai-contest-2026&language=cn)。
