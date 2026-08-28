# 蓝牙 HFP API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1683&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:28  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/bluetooth/bt_hfp.md) | 简体中文 \]

# 蓝牙 HFP API

openvela 蓝牙 HFP（免提规范）接口，支持蓝牙通话功能。

头文件：\#include "bt\_hfp.h"、\#include "bt\_hfp\_hf.h"、\#include "bt\_hfp\_ag.h"

# openvela 实现说明

  - **双角色支持**：HF（Hands-Free，免提端）和 AG（Audio Gateway，音频网关端）
  - **功能**：接听/挂断电话、音量控制、语音识别、电话簿访问

# 同步接口

## bt\_hfp\_hf\_unregister\_callbacks

    bool bt_hfp_hf_unregister_callbacks(bt_instance_t* ins, void* cookie);

取消注册回调函数，停止接收状态变更通知。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cookie</span> 用户上下文。

**返回值**：

成功时返回 <span class="reference">true</span>，失败时返回 <span class="reference">false</span>。

## bt\_hfp\_hf\_is\_connected

    bool bt_hfp_hf_is_connected(bt_instance_t* ins, bt_address_t* addr);

查询与远程设备的 HFP HF 是否已连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

已连接时返回 <span class="reference">true</span>，未连接时返回 <span class="reference">false</span>。

## bt\_hfp\_hf\_is\_audio\_connected

    bool bt_hfp_hf_is_audio_connected(bt_instance_t* ins, bt_address_t* addr);

查询与远程设备的 HFP 音频通道是否已连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

音频已连接时返回 <span class="reference">true</span>，未连接时返回 <span class="reference">false</span>。

## bt\_hfp\_hf\_get\_connection\_state

    profile_connection_state_t bt_hfp_hf_get_connection_state(bt_instance_t* ins, bt_address_t* addr);

获取与远程设备的 HFP HF 连接状态。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

返回当前连接状态枚举值，参见 <span class="reference">profile\_connection\_state\_t</span>。

## bt\_hfp\_hf\_connect

    bt_status_t bt_hfp_hf_connect(bt_instance_t* ins, bt_address_t* addr);

发起与远程设备的 HFP HF 连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 对端设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_disconnect

    bt_status_t bt_hfp_hf_disconnect(bt_instance_t* ins, bt_address_t* addr);

断开与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_set\_connection\_policy

    bt_status_t bt_hfp_hf_set_connection_policy(bt_instance_t* ins, bt_address_t* addr, connection_policy_t policy);

发起与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">policy</span> 策略值。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_connect\_audio

    bt_status_t bt_hfp_hf_connect_audio(bt_instance_t* ins, bt_address_t* addr);

发起与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_disconnect\_audio

    bt_status_t bt_hfp_hf_disconnect_audio(bt_instance_t* ins, bt_address_t* addr);

断开与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_start\_voice\_recognition

    bt_status_t bt_hfp_hf_start_voice_recognition(bt_instance_t* ins, bt_address_t* addr);

启动远程设备的语音识别功能。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_stop\_voice\_recognition

    bt_status_t bt_hfp_hf_stop_voice_recognition(bt_instance_t* ins, bt_address_t* addr);

停止远程设备的语音识别功能。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_dial

    bt_status_t bt_hfp_hf_dial(bt_instance_t* ins, bt_address_t* addr, const char* number);

发起通话。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">number</span> 号码。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_dial\_memory

    bt_status_t bt_hfp_hf_dial_memory(bt_instance_t* ins, bt_address_t* addr, uint32_t memory);

通过 HFP 拨打内存中存储的号码。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">memory</span> 内存位置编号。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_redial

    bt_status_t bt_hfp_hf_redial(bt_instance_t* ins, bt_address_t* addr);

发起通话。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_accept\_call

    bt_status_t bt_hfp_hf_accept_call(bt_instance_t* ins, bt_address_t* addr, hfp_call_accept_t flag);

通过 HFP 接听来电。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">flag</span> 标志位。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_reject\_call

    bt_status_t bt_hfp_hf_reject_call(bt_instance_t* ins, bt_address_t* addr);

通过 HFP 拒绝来电。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_hold\_call

    bt_status_t bt_hfp_hf_hold_call(bt_instance_t* ins, bt_address_t* addr);

通过 HFP 保持当前通话。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_terminate\_call

    bt_status_t bt_hfp_hf_terminate_call(bt_instance_t* ins, bt_address_t* addr);

通过 HFP 挂断当前通话。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_control\_call

    bt_status_t bt_hfp_hf_control_call(bt_instance_t* ins, bt_address_t* addr, hfp_call_control_t chld, uint8_t index);

control通话状态。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">chld</span> CHLD 命令类型。
  - <span class="reference">index</span> 索引。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_query\_current\_calls

    bt_status_t bt_hfp_hf_query_current_calls(bt_instance_t* ins, bt_address_t* addr, hfp_current_call_t** calls, int* num, bt_allocator_t allocator);

查询当前所有通话的状态信息（CLCC）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 对端设备蓝牙地址。
  - <span class="reference">allocator</span> 内存分配函数。- <span class="reference">calls</span> 输出参数，存储通话信息数组。
  - <span class="reference">num</span> 输出参数，存储通话数量。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_send\_at\_cmd

    bt_status_t bt_hfp_hf_send_at_cmd(bt_instance_t* ins, bt_address_t* addr, const char* cmd);

发送自定义 AT 命令到远程设备。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">cmd</span> 命令。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_update\_battery\_level

    bt_status_t bt_hfp_hf_update_battery_level(bt_instance_t* ins, bt_address_t* addr, uint8_t level);

向远程设备更新本地电池电量信息。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">level</span> 安全级别。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_volume\_control

    bt_status_t bt_hfp_hf_volume_control(bt_instance_t* ins, bt_address_t* addr, hfp_volume_type_t type, uint8_t volume);

通过 HFP 控制远程设备的音量。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">type</span> 类型。
  - <span class="reference">volume</span> 音量值。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_send\_dtmf

    bt_status_t bt_hfp_hf_send_dtmf(bt_instance_t* ins, bt_address_t* addr, char dtmf);

通过 HFP 发送 DTMF 按键音。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">dtmf</span> DTMF 按键字符。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_hf\_get\_subscriber\_number

    bt_status_t bt_hfp_hf_get_subscriber_number(bt_instance_t* ins, bt_address_t* addr);

获取用户号码。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

## bt\_hfp\_hf\_query\_current\_calls\_with\_callback

    bt_status_t bt_hfp_hf_query_current_calls_with_callback(bt_instance_t* ins, bt_address_t* addr);

查询当前所有通话的状态信息（CLCC），结果通过回调异步返回。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_unregister\_callbacks

    bool bt_hfp_ag_unregister_callbacks(bt_instance_t* ins, void* cookie);

取消注册回调函数，停止接收状态变更通知。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cookie</span> 用户上下文。

**返回值**：

成功时返回 <span class="reference">true</span>，失败时返回 <span class="reference">false</span>。

## bt\_hfp\_ag\_is\_connected

    bool bt_hfp_ag_is_connected(bt_instance_t* ins, bt_address_t* addr);

查询与远程设备的 HFP AG 是否已连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

已连接时返回 <span class="reference">true</span>，未连接时返回 <span class="reference">false</span>。

## bt\_hfp\_ag\_is\_audio\_connected

    bool bt_hfp_ag_is_audio_connected(bt_instance_t* ins, bt_address_t* addr);

查询与远程设备的 HFP AG 音频通道是否已连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

音频已连接时返回 <span class="reference">true</span>，未连接时返回 <span class="reference">false</span>。

## bt\_hfp\_ag\_get\_connection\_state

    profile_connection_state_t bt_hfp_ag_get_connection_state(bt_instance_t* ins, bt_address_t* addr);

获取与远程设备的 HFP AG 连接状态。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

返回当前连接状态枚举值，参见 <span class="reference">profile\_connection\_state\_t</span>。

## bt\_hfp\_ag\_connect

    bt_status_t bt_hfp_ag_connect(bt_instance_t* ins, bt_address_t* addr);

发起与远程设备的 HFP AG 连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 对端设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_disconnect

    bt_status_t bt_hfp_ag_disconnect(bt_instance_t* ins, bt_address_t* addr);

断开与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_connect\_audio

    bt_status_t bt_hfp_ag_connect_audio(bt_instance_t* ins, bt_address_t* addr);

发起与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_disconnect\_audio

    bt_status_t bt_hfp_ag_disconnect_audio(bt_instance_t* ins, bt_address_t* addr);

断开与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_start\_virtual\_call

    bt_status_t bt_hfp_ag_start_virtual_call(bt_instance_t* ins, bt_address_t* addr);

开始操作。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_stop\_virtual\_call

    bt_status_t bt_hfp_ag_stop_virtual_call(bt_instance_t* ins, bt_address_t* addr);

停止操作。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_start\_voice\_recognition

    bt_status_t bt_hfp_ag_start_voice_recognition(bt_instance_t* ins, bt_address_t* addr);

启动远程设备的语音识别功能。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_stop\_voice\_recognition

    bt_status_t bt_hfp_ag_stop_voice_recognition(bt_instance_t* ins, bt_address_t* addr);

停止远程设备的语音识别功能。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_phone\_state\_change

    bt_status_t bt_hfp_ag_phone_state_change(bt_instance_t* ins, bt_address_t* addr, uint8_t num_active, uint8_t num_held, hfp_ag_call_state_t call_state, hfp_call_addrtype_t type, const char* number, const char* name);

通知远程设备电话状态变更（来电/通话/挂断等）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">num\_active</span> 活跃通话数量。
  - <span class="reference">num\_held</span> 保持中通话数量。
  - <span class="reference">call\_state</span> 通话状态。
  - <span class="reference">type</span> 类型。
  - <span class="reference">number</span> 号码。
  - <span class="reference">name</span> 名称。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_notify\_device\_status

    bt_status_t bt_hfp_ag_notify_device_status(bt_instance_t* ins, bt_address_t* addr, hfp_network_state_t network, hfp_roaming_state_t roam, uint8_t signal, uint8_t battery);

notify设备类型status。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">network</span> 网络信息。
  - <span class="reference">roam</span> 漫游状态。
  - <span class="reference">signal</span> 信号强度。
  - <span class="reference">battery</span> 电池电量。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_volume\_control

    bt_status_t bt_hfp_ag_volume_control(bt_instance_t* ins, bt_address_t* addr, hfp_volume_type_t type, uint8_t volume);

通过 HFP 控制远程设备的音量。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">type</span> 类型。
  - <span class="reference">volume</span> 音量值。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_send\_at\_command

    bt_status_t bt_hfp_ag_send_at_command(bt_instance_t* ins, bt_address_t* addr, const char* at_command);

发送操作。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">at\_command</span> AT 命令字符串。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_send\_vendor\_specific\_at\_command

    bt_status_t bt_hfp_ag_send_vendor_specific_at_command(bt_instance_t* ins, bt_address_t* addr, const char* command, const char* value);

发送操作。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">command</span> 命令。
  - <span class="reference">value</span> 值。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_send\_clcc\_response

    bt_status_t bt_hfp_ag_send_clcc_response(bt_instance_t* ins, bt_address_t* addr, uint32_t index, hfp_call_direction_t dir, hfp_ag_call_state_t state, hfp_call_mode_t mode, hfp_call_mpty_type_t mpty, hfp_call_addrtype_t type, const char* number);

发送操作。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 蓝牙地址。
  - <span class="reference">index</span> 索引。
  - <span class="reference">dir</span> 方向（呼入/呼出）。
  - <span class="reference">state</span> 状态。
  - <span class="reference">mode</span> 模式。
  - <span class="reference">mpty</span> 是否为多方通话。
  - <span class="reference">type</span> 类型。
  - <span class="reference">number</span> 通话号码。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hfp\_ag\_send\_cind\_response

    bt_status_t bt_hfp_ag_send_cind_response(bt_instance_t* ins, bt_address_t* addr, hfp_network_state_t network, hfp_call_t call, hfp_callheld_t call_held, hfp_callsetup_t call_setup, uint8_t signal, hfp_roaming_state_t roam, uint8_t battery);

发送操作。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">network</span> 网络信息。
  - <span class="reference">call</span> 通话信息。
  - <span class="reference">call\_held</span> 保持中通话数量。
  - <span class="reference">call\_setup</span> 通话建立状态。
  - <span class="reference">signal</span> 信号强度。
  - <span class="reference">roam</span> 漫游状态。
  - <span class="reference">battery</span> 电池电量。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。
