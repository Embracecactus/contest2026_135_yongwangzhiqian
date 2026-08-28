# 蓝牙 AVRCP API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1682&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:27  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/bluetooth/bt_avrcp.md) | 简体中文 \]

# 蓝牙 AVRCP API

openvela 蓝牙 AVRCP（音视频远程控制）接口，支持播放控制、曲目信息查询等。

头文件：\#include "bt\_avrcp.h"、\#include "bt\_avrcp\_control.h"、\#include "bt\_avrcp\_target.h"

# openvela 实现说明

  - **双角色支持**：Controller（控制端）和 Target（目标端）
  - **功能**：播放/暂停/切歌、音量控制、曲目信息获取

# 同步接口

## bt\_avrcp\_control\_unregister\_callbacks

    bool bt_avrcp_control_unregister_callbacks(bt_instance_t* ins, void* cookie);

取消注册回调函数，停止接收状态变更通知。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cookie</span> 用户上下文。

**返回值**：

取消注册回调函数。

## bt\_avrcp\_control\_get\_element\_attributes

    bt_status_t bt_avrcp_control_get_element_attributes(bt_instance_t* ins, bt_address_t* addr);

获取媒体元素属性。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_avrcp\_control\_send\_passthrough\_cmd

    bt_status_t bt_avrcp_control_send_passthrough_cmd(bt_instance_t* ins, bt_address_t* addr, uint8_t cmd, uint8_t state);

发送透传命令。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">cmd</span> 命令。
  - <span class="reference">state</span> 状态。

## bt\_avrcp\_control\_get\_unit\_info

    bt_status_t bt_avrcp_control_get_unit_info(bt_instance_t* ins, bt_address_t* addr);

获取远程 AVRCP 设备的单元信息。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 蓝牙地址。

## bt\_avrcp\_control\_get\_subunit\_info

    bt_status_t bt_avrcp_control_get_subunit_info(bt_instance_t* ins, bt_address_t* addr);

获取子单元信息。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

## bt\_avrcp\_control\_get\_playback\_state

    bt_status_t bt_avrcp_control_get_playback_state(bt_instance_t* ins, bt_address_t* addr);

获取远程设备的当前播放状态。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 蓝牙地址。

## bt\_avrcp\_control\_register\_notification

    bt_status_t bt_avrcp_control_register_notification(bt_instance_t* ins, bt_address_t* addr, uint8_t event, uint32_t interval);

注册事件通知。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">event</span> 事件类型。
  - <span class="reference">interval</span> 间隔。

## bt\_avrcp\_target\_unregister\_callbacks

    bool bt_avrcp_target_unregister_callbacks(bt_instance_t* ins, void* cookie);

取消注册回调函数，停止接收状态变更通知。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

  - <span class="reference">cookie</span> 用户上下文。

  - <span class="reference">cookie</span> 用户上下文。

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

成功时返回回调 cookie，失败或已注册时返回 NULL。

## bt\_avrcp\_target\_get\_play\_status\_response

    bt_status_t bt_avrcp_target_get_play_status_response(bt_instance_t* ins, bt_address_t* addr, avrcp_play_status_t status, uint32_t song_len, uint32_t song_pos);

回复播放状态查询。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 蓝牙地址 of the peer device.
  - <span class="reference">status</span> 状态码。
  - <span class="reference">song\_len</span> 歌曲长度（毫秒）。
  - <span class="reference">song\_pos</span> 当前播放位置（毫秒）。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_avrcp\_target\_play\_status\_notify

    bt_status_t bt_avrcp_target_play_status_notify(bt_instance_t* ins, bt_address_t* addr, avrcp_play_status_t status);

通知播放状态变更。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">status</span> 状态码。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。
