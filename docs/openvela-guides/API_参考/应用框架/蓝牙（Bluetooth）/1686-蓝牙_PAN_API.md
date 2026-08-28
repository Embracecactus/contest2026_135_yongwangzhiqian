# 蓝牙 PAN API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1686&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:30  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/bluetooth/bt_pan.md) | 简体中文 \]

# 蓝牙 PAN API

openvela 蓝牙 PAN（个人局域网）接口，支持通过蓝牙实现网络共享。

头文件：\#include "bt\_pan.h"

# openvela 实现说明

  - **功能**：网络共享（Tethering）、蓝牙组网

# 同步接口

## bt\_pan\_unregister\_callbacks

    bool bt_pan_unregister_callbacks(bt_instance_t* ins, void* cookie);

取消注册回调函数，停止接收状态变更通知。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cookie</span> 用户上下文。

**返回值**：

成功时返回 <span class="reference">true</span>，失败时返回 <span class="reference">false</span>。

## bt\_pan\_connect

    bt_status_t bt_pan_connect(bt_instance_t* ins, bt_address_t* addr, uint8_t dst_role, uint8_t src_role);

发起与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">dst\_role</span> 目标设备角色。
  - <span class="reference">src\_role</span> 本地设备角色。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_pan\_disconnect

    bt_status_t bt_pan_disconnect(bt_instance_t* ins, bt_address_t* addr);

断开与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。
