# 蓝牙 HID API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1684&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:28  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/bluetooth/bt_hid.md) | 简体中文 \]

# 蓝牙 HID API

openvela 蓝牙 HID（人机接口设备）接口，支持键盘、鼠标、游戏手柄等输入设备。

头文件：\#include "bt\_hid\_device.h"

# openvela 实现说明

  - **设备角色**：HID Device（输入设备端）

# 同步接口

## bt\_hid\_device\_unregister\_callbacks

    bool bt_hid_device_unregister_callbacks(bt_instance_t* ins, void* cookie);

取消注册回调函数，停止接收状态变更通知。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">cookie</span> 用户上下文。

**返回值**：

成功时返回 <span class="reference">true</span>，失败时返回 <span class="reference">false</span>。

## bt\_hid\_device\_register\_app

    bt_status_t bt_hid_device_register_app(bt_instance_t* ins, hid_device_sdp_settings_t* sdp_setting, bool le_hid);

注册操作。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">sdp\_setting</span> SDP 设置。
  - <span class="reference">le\_hid</span> LE HID 实例。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回负的错误码。

## bt\_hid\_device\_unregister\_app

    bt_status_t bt_hid_device_unregister_app(bt_instance_t* ins);

取消注册 HID 设备应用。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hid\_device\_connect

    bt_status_t bt_hid_device_connect(bt_instance_t* ins, bt_address_t* addr);

发起与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回负的错误码。

## bt\_hid\_device\_disconnect

    bt_status_t bt_hid_device_disconnect(bt_instance_t* ins, bt_address_t* addr);

断开与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回负的错误码。

## bt\_hid\_device\_send\_report

    bt_status_t bt_hid_device_send_report(bt_instance_t* ins, bt_address_t* addr, uint8_t rpt_id, uint8_t* rpt_data, int rpt_size);

向已连接的主机发送 HID 输入报告。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">rpt\_id</span> HID 报告 ID。
  - <span class="reference">rpt\_data</span> HID 报告数据。
  - <span class="reference">rpt\_size</span> 报告数据大小（字节）。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回负的错误码。

## bt\_hid\_device\_response\_report

    bt_status_t bt_hid_device_response_report(bt_instance_t* ins, bt_address_t* addr, uint8_t rpt_type, uint8_t* rpt_data, int rpt_size);

回复主机的 HID 报告请求。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">rpt\_type</span> HID 报告类型（输入/输出/特性）。
  - <span class="reference">rpt\_data</span> HID 报告数据。
  - <span class="reference">rpt\_size</span> 报告数据大小（字节）。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hid\_device\_report\_error

    bt_status_t bt_hid_device_report_error(bt_instance_t* ins, bt_address_t* addr, hid_status_error_t error);

向主机报告 HID 错误。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">error</span> 错误码，参见 <span class="reference">hid\_status\_error\_t</span>。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_hid\_device\_virtual\_unplug

    bt_status_t bt_hid_device_virtual_unplug(bt_instance_t* ins, bt_address_t* addr);

发送虚拟拔出请求，断开 HID 连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。
