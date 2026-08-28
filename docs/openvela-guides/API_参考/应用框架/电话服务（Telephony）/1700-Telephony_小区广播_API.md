# Telephony 小区广播 API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1700&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:38  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/telephony/telephony_cbs.md) | 简体中文 \]

# Telephony 小区广播 API

Cell Broadcast Service（CBS）是蜂窝网络的小区广播能力，常用于接收政府紧急警报（地震、海啸）和运营商公告。

头文件：<span class="reference">\#include \<tapi\_cbs.h\></span>

# openvela 实现说明

  - **开关控制**：通过 <span class="reference">set\_cell\_broadcast\_power\_on</span> 启用/禁用小区广播接收
  - **主题订阅**：通过 <span class="reference">set\_cell\_broadcast\_topics</span> 配置要接收的广播主题范围（按频道 ID）
  - **事件回调**：通过 <span class="reference">tapi\_cbs\_register</span> 注册事件回调，接收到的广播消息
  - **SIM 卡标识**：所有接口带 <span class="reference">slot\_id</span>，支持多 SIM 卡设备
  - **相关协议**：底层对应 3GPP TS 23.041 定义的 Cell Broadcast 流程

# 开关控制

## tapi\_sms\_set\_cell\_broadcast\_power\_on

    int tapi_sms_set_cell_broadcast_power_on(tapi_context context, int slot_id, bool enabled);

启用或禁用小区广播接收。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">enabled</span> <span class="reference">true</span> 表示启用，<span class="reference">false</span> 表示禁用。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_sms\_get\_cell\_broadcast\_power\_on

    int tapi_sms_get_cell_broadcast_power_on(tapi_context context, int slot_id, bool* enabled);

查询小区广播接收开关状态。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">enabled</span> 输出参数，返回当前开关状态。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

# 主题订阅

## tapi\_sms\_set\_cell\_broadcast\_topics

    int tapi_sms_set_cell_broadcast_topics(tapi_context context, int slot_id, char* topics);

配置小区广播的主题范围（频道 ID 列表）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">topics</span> 主题字符串，典型格式为逗号分隔的频道 ID 或范围（如 <span class="reference">"4352-4356,919"</span>）。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_sms\_get\_cell\_broadcast\_topics

    int tapi_sms_get_cell_broadcast_topics(tapi_context context, int slot_id, char** topics);

查询当前配置的小区广播主题。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">topics</span> 输出参数，返回主题字符串（调用方负责释放）。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

# 事件订阅

## tapi\_cbs\_register

    int tapi_cbs_register(tapi_context context, int slot_id, tapi_indication_msg msg,
                          void* user_obj, tapi_async_function p_handle);

注册小区广播事件回调。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">msg</span> 要监听的事件类型。
  - <span class="reference">user\_obj</span> 用户数据，将回传给回调函数。
  - <span class="reference">p\_handle</span> 事件回调函数。

**返回值**：

成功时返回事件订阅 watch ID，失败时返回负的错误码。
