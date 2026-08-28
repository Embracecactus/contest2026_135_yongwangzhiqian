# 蓝牙 BLE 扫描 API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1679&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:25  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/bluetooth/bt_le_scan.md) | 简体中文 \]

# 蓝牙 BLE 扫描 API

openvela 蓝牙 BLE 扫描接口，用于发现周围的 BLE 设备和广播数据。

头文件：<span class="reference">\#include "bt\_le\_scan.h"</span>

# openvela 实现说明

  - **扫描模式**：支持被动扫描和主动扫描
  - **过滤器**：支持按名称、地址、UUID 等条件过滤扫描结果
  - **回调通知**：通过回调函数异步返回扫描结果

# 同步接口

## bt\_le\_stop\_scan

    void bt_le_stop_scan(bt_instance_t* ins, bt_scanner_t* scanner);

停止 BLE 扫描。

**参数**：

  - <span class="reference">scanner</span> 扫描器实例。
  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.

**返回值**：

## bt\_le\_scan\_is\_supported

    bool bt_le_scan_is_supported(bt_instance_t* ins);

查询操作。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

bt\_le\_scan\_is\_supported 操作。

# 异步接口

## bt\_le\_start\_scan\_async

    bt_status_t bt_le_start_scan_async(bt_instance_t* ins, const scanner_callbacks_t* scan_cbs, bt_le_start_scan_cb_t cb, void* userdata);

开始BLE 扫描（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">scan\_cbs</span> 扫描回调函数集合。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

## bt\_le\_start\_scan\_settings\_async

    bt_status_t bt_le_start_scan_settings_async(bt_instance_t* ins, ble_scan_settings_t* settings, const scanner_callbacks_t* scan_cbs, bt_le_start_scan_cb_t cb, void* userdata);

异步版本。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">settings</span> 设置。
  - <span class="reference">scan\_cbs</span> 扫描回调函数集合。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

## bt\_le\_start\_scan\_with\_filters\_async

    bt_status_t bt_le_start_scan_with_filters_async(bt_instance_t* ins, ble_scan_settings_t* settings, ble_scan_filter_t* filter, const scanner_callbacks_t* scan_cbs, bt_le_start_scan_cb_t cb, void* userdata);

开始操作（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">settings</span> 设置。
  - <span class="reference">filter</span> 过滤条件。
  - <span class="reference">scan\_cbs</span> 扫描回调函数集合。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

## bt\_le\_stop\_scan\_async

    bt_status_t bt_le_stop_scan_async(bt_instance_t* ins, bt_scanner_t* scanner, bt_le_stop_scan_cb_t cb, void* userdata);

停止扫描（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">scanner</span> 扫描器实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

## bt\_le\_scan\_is\_supported\_async

    bt_status_t bt_le_scan_is_supported_async(bt_instance_t* ins, bt_bool_cb_t cb, void* userdata);

查询是否支持 BLE 扫描（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。
