# 蓝牙 BLE 广播 API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1680&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:26  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/bluetooth/bt_le_advertiser.md) | 简体中文 \]

# 蓝牙 BLE 广播 API

openvela 蓝牙 BLE 广播接口，用于发送 BLE 广播数据和管理广播实例。

头文件：<span class="reference">\#include "bt\_le\_advertiser.h"</span>

# openvela 实现说明

  - **广播类型**：支持可连接广播、不可连接广播、扫描响应等
  - **广播数据**：支持自定义广播数据和扫描响应数据
  - **多实例**：支持同时运行多个广播实例

# 同步接口

## bt\_le\_stop\_advertising

    void bt_le_stop_advertising(bt_instance_t* ins, bt_advertiser_t* adver);

停止 BLE 广播。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">adver</span> 广播器实例。

**返回值**：

## bt\_le\_stop\_advertising\_id

    void bt_le_stop_advertising_id(bt_instance_t* ins, uint8_t adv_id);

停止指定 ID 的 BLE 广播实例。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">adv\_id</span> 广播实例 ID。

## bt\_le\_advertising\_is\_supported

    bool bt_le_advertising_is_supported(bt_instance_t* ins);

广播数据查询supported。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

bt\_le\_advertising\_is\_supported 操作。

# 异步接口

## bt\_le\_start\_advertising\_async

    bt_status_t bt_le_start_advertising_async(bt_instance_t* ins, ble_adv_params_t* params, uint8_t* adv_data, uint16_t adv_len, uint8_t* scan_rsp_data, uint16_t scan_rsp_len, advertiser_callback_t* adv_cbs, bt_le_start_adv_callback_cb_t cb, void* userdata);

开始 BLE 广播（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">params</span> 参数结构体。
  - <span class="reference">adv\_data</span> 广播数据。
  - <span class="reference">adv\_len</span> 广播数据长度。
  - <span class="reference">scan\_rsp\_data</span> 扫描响应数据。
  - <span class="reference">scan\_rsp\_len</span> 扫描响应数据长度。
  - <span class="reference">adv\_cbs</span> 广播回调函数集合。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

## bt\_le\_stop\_advertising\_async

    bt_status_t bt_le_stop_advertising_async(bt_instance_t* ins, bt_advertiser_t* adver, bt_status_cb_t cb, void* userdata);

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">adver</span> 广播器实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

## bt\_le\_stop\_advertising\_id\_async

    bt_status_t bt_le_stop_advertising_id_async(bt_instance_t* ins, uint8_t adv_id, bt_status_cb_t cb, void* userdata);

停止BLE 广播（指定 ID）（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">adv\_id</span> 广播实例 ID。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

## bt\_le\_advertising\_is\_supported\_async

    bt_status_t bt_le_advertising_is_supported_async(bt_instance_t* ins, bt_bool_cb_t cb, void* userdata);

查询是否支持 BLE 广播（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。
