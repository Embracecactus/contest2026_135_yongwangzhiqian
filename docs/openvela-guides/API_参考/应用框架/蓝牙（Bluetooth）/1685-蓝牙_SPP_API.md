# 蓝牙 SPP API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1685&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:29  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/bluetooth/bt_spp.md) | 简体中文 \]

# 蓝牙 SPP API

openvela 蓝牙 SPP（串口仿真）接口，用于替代物理串口进行数据透传。

头文件：\#include "bt\_spp.h"

# openvela 实现说明

  - **功能**：虚拟串口通信，适用于传统串口设备的蓝牙化

# 同步接口

## bt\_spp\_unregister\_app

    bt_status_t bt_spp_unregister_app(bt_instance_t* ins, void* handle);

取消注册 SPP 应用。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">handle</span> 句柄。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_spp\_server\_start

    bt_status_t bt_spp_server_start(bt_instance_t* ins, void* handle, uint16_t scn, bt_uuid_t* uuid, uint8_t max_connection);

启动 SPP 服务器，监听远程设备的连接请求。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">handle</span> 句柄。
  - <span class="reference">scn</span> 服务器通道号。
  - <span class="reference">uuid</span> 服务 UUID。
  - <span class="reference">max\_connection</span> 最大连接数。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_spp\_server\_stop

    bt_status_t bt_spp_server_stop(bt_instance_t* ins, void* handle, uint16_t scn);

停止 SPP 服务器，不再接受新连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">handle</span> 句柄。
  - <span class="reference">scn</span> 服务器通道号。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_spp\_connect

    bt_status_t bt_spp_connect(bt_instance_t* ins, void* handle, bt_address_t* addr, int16_t scn, bt_uuid_t* uuid, uint16_t* port);

发起与远程设备的连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">handle</span> 句柄。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">scn</span> 服务器通道号。
  - <span class="reference">uuid</span> 服务 UUID。
  - <span class="reference">port</span> 端口号。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_spp\_insecure\_connect

    bt_status_t bt_spp_insecure_connect(bt_instance_t* ins, void* handle, bt_address_t* addr, int16_t scn, bt_uuid_t* uuid, uint16_t* port);

发起与远程设备的非安全连接（不要求加密）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">handle</span> 句柄。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">scn</span> 服务器通道号。
  - <span class="reference">uuid</span> 服务 UUID。
  - <span class="reference">port</span> 端口号。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

## bt\_spp\_disconnect

    bt_status_t bt_spp_disconnect(bt_instance_t* ins, void* handle, bt_address_t* addr, uint16_t port);

断开连接。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">handle</span> 句柄。
  - <span class="reference">addr</span> 对端设备蓝牙地址。
  - <span class="reference">port</span> 端口号。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。
