# 蓝牙 A2DP API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1681&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:26  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/bluetooth/bt_a2dp.md) | 简体中文 \]

# 蓝牙 A2DP API

openvela 蓝牙 A2DP（高级音频分发）接口，支持音频流的发送（Source）和接收（Sink）。

头文件：\#include "bt\_a2dp.h"、\#include "bt\_a2dp\_sink.h"、\#include "bt\_a2dp\_source.h"

# openvela 实现说明

  - **双角色支持**：Source（音频发送端）和 Sink（音频接收端）
  - **编解码器**：支持 SBC 和 AAC
  - **传输模式**：支持硬件卸载（Offloading）和非卸载模式

# 连接状态机

A2DP 连接建立、流传输以及断开过程中的状态转换如下图所示：

![A2DP 状态机](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005808970_a2dp.png)

各状态含义：

  - **Idle**：空闲，未建立 A2DP 连接。
  - **Opening**：正在建立 A2DP 连接（本端发起 <span class="reference">A2DP connect</span> 之后）。
  - **Opened**：A2DP 信令连接已建立，可准备音频流。
  - **Started**：音频流已启动，正在传输音频数据。
  - **Closing**：正在断开 A2DP 连接，直至对端确认 <span class="reference">A2DP disconnected</span>。

# 同步接口

## bt\_a2dp\_sink\_unregister\_callbacks

    bool bt_a2dp_sink_unregister_callbacks(bt_instance_t* ins, void* cookie);

取消注册回调函数，停止接收状态变更通知。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cookie</span> 用户上下文。

**返回值**：

成功时返回 <span class="reference">true</span>，失败时返回 <span class="reference">false</span>。

## bt\_a2dp\_sink\_is\_connected

    bool bt_a2dp_sink_is_connected(bt_instance_t* ins, bt_address_t* addr);

查询指定设备的 A2DP Sink 是否已连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 对端设备蓝牙地址。

**返回值**：

已连接时返回 <span class="reference">true</span>，未连接时返回 <span class="reference">false</span>。

## bt\_a2dp\_sink\_is\_playing

    bool bt_a2dp_sink_is_playing(bt_instance_t* ins, bt_address_t* addr);

查询指定设备的 A2DP Sink 是否正在播放音频流。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 对端设备蓝牙地址。

**返回值**：

正在播放时返回 <span class="reference">true</span>，未播放时返回 <span class="reference">false</span>。

## bt\_a2dp\_sink\_get\_connection\_state

    profile_connection_state_t bt_a2dp_sink_get_connection_state(bt_instance_t* ins, bt_address_t* addr);

获取指定设备的 A2DP Sink 连接状态。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

返回当前连接状态枚举值，参见 <span class="reference">profile\_connection\_state\_t</span>。

## bt\_a2dp\_sink\_connect

    bt_status_t bt_a2dp_sink_connect(bt_instance_t* ins, bt_address_t* addr);

发起与远程设备的 A2DP Sink 连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 对端设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_a2dp\_sink\_disconnect

    bt_status_t bt_a2dp_sink_disconnect(bt_instance_t* ins, bt_address_t* addr);

断开与远程设备的 A2DP Sink 连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 对端设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_a2dp\_source\_unregister\_callbacks

    bool bt_a2dp_source_unregister_callbacks(bt_instance_t* ins, void* cookie);

取消注册回调函数，停止接收状态变更通知。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cookie</span> 用户上下文。

**返回值**：

成功时返回 <span class="reference">true</span>，失败时返回 <span class="reference">false</span>。

## bt\_a2dp\_source\_is\_connected

    bool bt_a2dp_source_is_connected(bt_instance_t* ins, bt_address_t* addr);

查询指定设备的 A2DP Source 是否已连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 对端设备蓝牙地址。

**返回值**：

已连接时返回 <span class="reference">true</span>，未连接时返回 <span class="reference">false</span>。

## bt\_a2dp\_source\_is\_playing

    bool bt_a2dp_source_is_playing(bt_instance_t* ins, bt_address_t* addr);

查询指定设备的 A2DP Source 是否正在播放音频流。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 对端设备蓝牙地址。

**返回值**：

正在播放时返回 <span class="reference">true</span>，未播放时返回 <span class="reference">false</span>。

## bt\_a2dp\_source\_get\_connection\_state

    profile_connection_state_t bt_a2dp_source_get_connection_state(bt_instance_t* ins, bt_address_t* addr);

获取指定设备的 A2DP Source 连接状态。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

返回当前连接状态枚举值，参见 <span class="reference">profile\_connection\_state\_t</span>。

## bt\_a2dp\_source\_connect

    bt_status_t bt_a2dp_source_connect(bt_instance_t* ins, bt_address_t* addr);

发起与远程设备的 A2DP Source 连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 对端设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_a2dp\_source\_disconnect

    bt_status_t bt_a2dp_source_disconnect(bt_instance_t* ins, bt_address_t* addr);

断开与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_a2dp\_source\_set\_silence\_device

    bt_status_t bt_a2dp_source_set_silence_device(bt_instance_t* ins, bt_address_t* addr, bool silence);

设置静音设备。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">silence</span> 是否设为静音模式（true 为静音）。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_a2dp\_source\_set\_active\_device

    bt_status_t bt_a2dp_source_set_active_device(bt_instance_t* ins, bt_address_t* addr);

设置活跃设备。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。
