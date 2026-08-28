# 蓝牙 GAP API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1676&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:23  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/bluetooth/bt_gap.md) | 简体中文 \]

# 蓝牙 GAP API

openvela 蓝牙 GAP（通用访问规范）接口提供蓝牙适配器的管理功能，包括启用/禁用、设备发现、属性配置、配对管理等。

头文件：<span class="reference">\#include "bt\_adapter.h"</span>

# openvela 实现说明

  - **双模支持**：支持经典蓝牙（BR/EDR）和低功耗蓝牙（BLE）独立控制
  - **异步模式**：大部分 API 提供同步和异步两个版本，异步版本以 <span class="reference">\_async</span> 后缀命名，通过回调返回结果
  - **实例管理**：所有 API 的第一个参数为 <span class="reference">bt\_instance\_t\* ins</span>（蓝牙客户端实例），通过 <span class="reference">bt\_open()</span> 获取
  - **状态机**：适配器状态遵循 OFF → BLE\_TURNING\_ON → BLE\_ON → TURNING\_ON → ON 的转换流程

# 适配器控制

### bt\_adapter\_get\_state

    bt_adapter_state_t bt_adapter_get_state(bt_instance_t* ins);

获取适配器状态。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.

**返回值**：

返回当前适配器状态枚举值，参见 <span class="reference">bt\_adapter\_state\_t</span>。

### bt\_adapter\_is\_support\_le

    bool bt_adapter_is_support_le(bt_instance_t* ins);

查询是否支持 BLE。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

支持时返回 <span class="reference">true</span>，不支持时返回 <span class="reference">false</span>。

### bt\_adapter\_is\_support\_leaudio

    bool bt_adapter_is_support_leaudio(bt_instance_t* ins);

查询是否支持 LE Audio。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

支持时返回 <span class="reference">true</span>，不支持时返回 <span class="reference">false</span>。

# 设备发现

### bt\_adapter\_set\_discovery\_filter

    bt_status_t bt_adapter_set_discovery_filter(bt_instance_t* ins);

设置设备发现过滤条件。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.

### bt\_adapter\_start\_discovery

    bt_status_t bt_adapter_start_discovery(bt_instance_t* ins, uint32_t timeout);

开始设备发现。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">timeout</span> 超时时间。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

### bt\_adapter\_cancel\_discovery

    bt_status_t bt_adapter_cancel_discovery(bt_instance_t* ins);

取消设备发现。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

### bt\_adapter\_is\_discovering

    bool bt_adapter_is_discovering(bt_instance_t* ins);

查询是否正在发现设备。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

正在发现时返回 <span class="reference">true</span>，否则返回 <span class="reference">false</span>。

# 属性管理

### bt\_adapter\_get\_type

    bt_device_type_t bt_adapter_get_type(bt_instance_t* ins);

获取设备类型。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.

### bt\_adapter\_set\_name

    bt_status_t bt_adapter_set_name(bt_instance_t* ins, const char* name);

设置设备名称。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">name</span> 名称。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

### bt\_adapter\_get\_name

    void bt_adapter_get_name(bt_instance_t* ins, char* name, int length);

获取设备名称。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">name</span> 输出参数，存储适配器名称。
  - <span class="reference">length</span> 缓冲区长度。

### bt\_adapter\_set\_scan\_mode

    bt_status_t bt_adapter_set_scan_mode(bt_instance_t* ins, bt_scan_mode_t mode, bool bondable);

设置扫描模式。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">mode</span> 扫描模式。
  - <span class="reference">bondable</span> 是否可配对。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

### bt\_adapter\_get\_scan\_mode

    bt_scan_mode_t bt_adapter_get_scan_mode(bt_instance_t* ins);

获取扫描模式。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

返回扫描模式枚举值，参见 <span class="reference">bt\_scan\_mode\_t</span>。  

    bt_status_t bt_adapter_set_device_class(bt_instance_t* ins, uint32_t cod);

设置设备类型（CoD）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cod</span> 设备类型（CoD）。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回负的错误码。。

### bt\_adapter\_get\_device\_class

    uint32_t bt_adapter_get_device_class(bt_instance_t* ins);

获取设备类型（CoD）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

返回 24 位 Class of Device 值。  

    bt_status_t bt_adapter_set_debug_mode(bt_instance_t* ins, bt_debug_mode_t mode, uint8_t operation);

设置蓝牙适配器的调试模式，用于工厂测试和射频认证。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">mode</span> 调试模式。
  - <span class="reference">operation</span> 调试操作。

### bt\_adapter\_set\_le\_address

    bt_status_t bt_adapter_set_le_address(bt_instance_t* ins, bt_address_t* addr);

设置本地 BLE 地址。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">addr</span> BLE 身份地址。

### bt\_adapter\_set\_le\_appearance

    bt_status_t bt_adapter_set_le_appearance(bt_instance_t* ins, uint16_t appearance);

设置 BLE 外观值。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">appearance</span> BLE 外观值。

### bt\_adapter\_le\_add\_whitelist\_with\_type

    bt_status_t bt_adapter_le_add_whitelist_with_type(bt_instance_t* ins, bt_address_t* addr, ble_addr_type_t type);

BLE 连接添加BLE 白名单（指定类型）特征值（签名写入）type。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">addr</span> 设备地址。
  - <span class="reference">type</span> 地址类型。

# 配对与安全

### bt\_adapter\_set\_io\_capability

    bt_status_t bt_adapter_set_io_capability(bt_instance_t* ins, bt_io_capability_t cap);

设置 IO 能力。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">cap</span> IO 能力值。

### bt\_adapter\_get\_bonded\_devices

    bt_status_t bt_adapter_get_bonded_devices(bt_instance_t* ins, bt_transport_t transport, bt_address_t** addr, int* num, bt_allocator_t allocator);

获取已配对设备列表。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">transport</span> Transport type, 参见 bt\_transport\_t.
  - <span class="reference">allocator</span> 内存分配函数。
  - <span class="reference">addr</span> 输出参数，存储已配对设备地址数组。
  - <span class="reference">num</span> 输出参数，存储设备数量。

### bt\_adapter\_disconnect\_all\_devices

    void bt_adapter_disconnect_all_devices(bt_instance_t* ins);

断开所有已连接设备。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.

### bt\_adapter\_get\_le\_io\_capability

    uint32_t bt_adapter_get_le_io_capability(bt_instance_t* ins);

获取 BLE IO 能力。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

返回 BLE IO 能力值。

# BLE 管理

### bt\_adapter\_enable

    bt_status_t bt_adapter_enable(bt_instance_t* ins);

启用蓝牙适配器。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

### bt\_adapter\_disable

    bt_status_t bt_adapter_disable(bt_instance_t* ins);

禁用蓝牙适配器。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.

**返回值**：

成功时返回 BT\_STATUS\_SUCCESS，失败时返回错误码。

### bt\_adapter\_disable\_safe

    bt_status_t bt_adapter_disable_safe(bt_instance_t* ins);

安全禁用蓝牙适配器，等待所有连接断开后再关闭。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.

### bt\_adapter\_disable\_le

    bt_status_t bt_adapter_disable_le(bt_instance_t* ins);

禁用低功耗蓝牙（BLE）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.

### bt\_adapter\_is\_le\_enabled

    bool bt_adapter_is_le_enabled(bt_instance_t* ins);

查询 BLE 是否已启用。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。

**返回值**：

已启用时返回 <span class="reference">true</span>，未启用时返回 <span class="reference">false</span>。

### bt\_adapter\_le\_enable\_key\_derivation

    bt_status_t bt_adapter_le_enable_key_derivation(bt_instance_t* ins, bool brkey_to_lekey, bool lekey_to_brkey);

启用 BLE 密钥派生。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">brkey\_to\_lekey</span> 是否启用 BR→LE 密钥派生。
  - <span class="reference">lekey\_to\_brkey</span> 是否启用 LE→BR 密钥派生。

### bt\_adapter\_le\_remove\_whitelist

    bt_status_t bt_adapter_le_remove_whitelist(bt_instance_t* ins, bt_address_t* addr);

从 BLE 白名单移除设备。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">addr</span> 要移除的设备地址。

### bt\_adapter\_set\_page\_scan\_parameters

    bt_status_t bt_adapter_set_page_scan_parameters(bt_instance_t* ins, bt_scan_type_t type, uint16_t interval, uint16_t window);

设置页面扫描参数。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例, 参见 bt\_instance\_t.
  - <span class="reference">type</span> 扫描类型。
  - <span class="reference">interval</span> 扫描间隔。
  - <span class="reference">window</span> 扫描窗口。

# 异步接口

### bt\_adapter\_register\_callback\_async

    bt_status_t bt_adapter_register_callback_async(bt_instance_t* ins, const adapter_callbacks_t* adapter_cbs, bt_register_callback_cb_t cb, void* userdata);

异步版本。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">adapter\_cbs</span> 适配器回调函数集合。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_unregister\_callback\_async

    bt_status_t bt_adapter_unregister_callback_async(bt_instance_t* ins, void* cookie, bt_bool_cb_t cb, void* userdata);

取消注册回调函数（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cookie</span> 用户上下文。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_enable\_async

    bt_status_t bt_adapter_enable_async(bt_instance_t* ins, bt_status_cb_t cb, void* userdata);

适配器状态变更回调（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_disable\_async

    bt_status_t bt_adapter_disable_async(bt_instance_t* ins, bt_status_cb_t cb, void* userdata);

禁用蓝牙适配器.（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_enable\_le\_async

    bt_status_t bt_adapter_enable_le_async(bt_instance_t* ins, bt_status_cb_t cb, void* userdata);

启用低功耗蓝牙（BLE）.（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_disable\_le\_async

    bt_status_t bt_adapter_disable_le_async(bt_instance_t* ins, bt_status_cb_t cb, void* userdata);

禁用低功耗蓝牙（BLE）.（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_state\_async

    bt_status_t bt_adapter_get_state_async(bt_instance_t* ins, bt_adapter_get_state_cb_t get_state_cb, void* userdata);

获取当前适配器状态（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">get\_state\_cb</span> 获取状态的回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_is\_le\_enabled\_async

    bt_status_t bt_adapter_is_le_enabled_async(bt_instance_t* ins, bt_bool_cb_t cb, void* userdata);

Check if BLE is enabled（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_type\_async

    bt_status_t bt_adapter_get_type_async(bt_instance_t* ins, bt_device_type_cb_t get_dtype_cb, void* userdata);

获取适配器设备类型（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">get\_dtype\_cb</span> 获取设备类型的回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_discovery\_filter\_async

    bt_status_t bt_adapter_set_discovery_filter_async(bt_instance_t* ins, bt_status_cb_t cb, void* userdata);

设置发现过滤器（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_start\_discovery\_async

    bt_status_t bt_adapter_start_discovery_async(bt_instance_t* ins, uint32_t timeout, bt_status_cb_t cb, void* userdata);

开始设备发现.（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">timeout</span> 超时时间。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_cancel\_discovery\_async

    bt_status_t bt_adapter_cancel_discovery_async(bt_instance_t* ins, bt_status_cb_t cb, void* userdata);

取消设备发现.（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_is\_discovering\_async

    bt_status_t bt_adapter_is_discovering_async(bt_instance_t* ins, bt_bool_cb_t cb, void* userdata);

查询适配器是否正在发现设备（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_address\_async

    bt_status_t bt_adapter_get_address_async(bt_instance_t* ins, bt_address_cb_t cb, void* userdata);

读取蓝牙控制器地址（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_name\_async

    bt_status_t bt_adapter_set_name_async(bt_instance_t* ins, const char* name, bt_status_cb_t cb, void* userdata);

设置适配器本地名称（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">name</span> 名称。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_name\_async

    bt_status_t bt_adapter_get_name_async(bt_instance_t* ins, bt_string_cb_t get_name_cb, void* userdata);

获取适配器本地名称（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">get\_name\_cb</span> 获取名称的回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_uuids\_async

    bt_status_t bt_adapter_get_uuids_async(bt_instance_t* ins, bt_uuids_cb_t get_uuids_cb, void* userdata);

获取适配器支持的 UUID 列表（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">get\_uuids\_cb</span> 获取 UUID 列表的回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_scan\_mode\_async

    bt_status_t bt_adapter_set_scan_mode_async(bt_instance_t* ins, bt_scan_mode_t mode, bool bondable, bt_status_cb_t cb, void* userdata);

设置适配器扫描模式（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">mode</span> 模式。
  - <span class="reference">bondable</span> 是否可配对。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_scan\_mode\_async

    bt_status_t bt_adapter_get_scan_mode_async(bt_instance_t* ins, bt_adapter_get_scan_mode_cb_t get_scan_mode_cb, void* userdata);

获取适配器扫描模式（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">get\_scan\_mode\_cb</span> 获取扫描模式的回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_device\_class\_async

    bt_status_t bt_adapter_set_device_class_async(bt_instance_t* ins, uint32_t cod, bt_status_cb_t cb, void* userdata);

设置适配器设备类型（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cod</span> 设备类型（CoD）。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_device\_class\_async

    bt_status_t bt_adapter_get_device_class_async(bt_instance_t* ins, bt_u32_cb_t get_cod_cb, void* userdata);

获取适配器设备类型（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">get\_cod\_cb</span> 获取设备类型的回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_io\_capability\_async

    bt_status_t bt_adapter_set_io_capability_async(bt_instance_t* ins, bt_io_capability_t cap, bt_status_cb_t cb, void* userdata);

设置 BR/EDR 适配器 IO 能力（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cap</span> IO 能力值。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_io\_capability\_async

    bt_status_t bt_adapter_get_io_capability_async(bt_instance_t* ins, bt_adapter_get_io_capability_cb_t get_ioc_cb, void* userdata);

获取 BR/EDR 适配器 IO 能力（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">get\_ioc\_cb</span> 获取 IO 能力的回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_inquiry\_scan\_parameters\_async

    bt_status_t bt_adapter_set_inquiry_scan_parameters_async(bt_instance_t* ins, bt_scan_type_t type, uint16_t interval, uint16_t window, bt_status_cb_t cb, void* userdata);

设置查询扫描参数（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">type</span> 类型。
  - <span class="reference">interval</span> 间隔。
  - <span class="reference">window</span> 扫描窗口（时间槽数）。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_page\_scan\_parameters\_async

    bt_status_t bt_adapter_set_page_scan_parameters_async(bt_instance_t* ins, bt_scan_type_t type, uint16_t interval, uint16_t window, bt_status_cb_t cb, void* userdata);

设置页面扫描参数（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">type</span> 类型。
  - <span class="reference">interval</span> 间隔。
  - <span class="reference">window</span> 扫描窗口（时间槽数）。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_le\_io\_capability\_async

    bt_status_t bt_adapter_set_le_io_capability_async(bt_instance_t* ins, uint32_t le_io_cap, bt_status_cb_t cb, void* userdata);

设置 BLE 适配器 IO 能力（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">le\_io\_cap</span> BLE IO 能力值。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_le\_io\_capability\_async

    bt_status_t bt_adapter_get_le_io_capability_async(bt_instance_t* ins, bt_u32_cb_t get_le_ioc_cb, void* userdata);

获取 BLE 适配器 IO 能力（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">get\_le\_ioc\_cb</span> 获取 BLE IO 能力的回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_le\_address\_async

    bt_status_t bt_adapter_get_le_address_async(bt_instance_t* ins, bt_adapter_get_le_address_cb_t cb, void* userdata);

获取 BLE 适配器地址（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_le\_address\_async

    bt_status_t bt_adapter_set_le_address_async(bt_instance_t* ins, bt_address_t* addr, bt_status_cb_t cb, void* userdata);

设置 BLE 私有地址（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_le\_identity\_address\_async

    bt_status_t bt_adapter_set_le_identity_address_async(bt_instance_t* ins, bt_address_t* addr, bool is_public, bt_status_cb_t cb, void* userdata);

设置 BLE 身份地址（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">is\_public</span> 是否使用公共地址。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_le\_appearance\_async

    bt_status_t bt_adapter_set_le_appearance_async(bt_instance_t* ins, uint16_t appearance, bt_status_cb_t cb, void* userdata);

设置 BLE 适配器外观值（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">appearance</span> 外观值。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_le\_appearance\_async

    bt_status_t bt_adapter_get_le_appearance_async(bt_instance_t* ins, bt_u16_cb_t cb, void* userdata);

获取 BLE 适配器外观值（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_le\_enable\_key\_derivation\_async

    bt_status_t bt_adapter_le_enable_key_derivation_async(bt_instance_t* ins, bool brkey_to_lekey, bool lekey_to_brkey, bt_status_cb_t cb, void* userdata);

启用或禁用跨传输密钥派生（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">brkey\_to\_lekey</span> 是否启用 BR 密钥派生 LE 密钥。
  - <span class="reference">lekey\_to\_brkey</span> 是否启用 LE 密钥派生 BR 密钥。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_le\_add\_whitelist\_async

    bt_status_t bt_adapter_le_add_whitelist_async(bt_instance_t* ins, bt_address_t* addr, bt_status_cb_t cb, void* userdata);

添加设备到 BLE 白名单（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_le\_remove\_whitelist\_async

    bt_status_t bt_adapter_le_remove_whitelist_async(bt_instance_t* ins, bt_address_t* addr, bt_status_cb_t cb, void* userdata);

从 BLE 白名单移除设备（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">addr</span> 远程设备蓝牙地址。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_bonded\_devices\_async

    bt_status_t bt_adapter_get_bonded_devices_async(bt_instance_t* ins, bt_transport_t transport, bt_adapter_get_devices_cb_t get_bonded_cb, void* userdata);

获取已配对设备列表（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">transport</span> 传输类型（BR/EDR 或 BLE）。
  - <span class="reference">get\_bonded\_cb</span> 获取已配对设备列表的回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_get\_connected\_devices\_async

    bt_status_t bt_adapter_get_connected_devices_async(bt_instance_t* ins, bt_transport_t transport, bt_adapter_get_devices_cb_t get_connected_cb, void* userdata);

获取已连接设备列表（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">transport</span> 传输类型（BR/EDR 或 BLE）。
  - <span class="reference">get\_connected\_cb</span> 获取已连接设备列表的回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_afh\_channel\_classification\_async

    bt_status_t bt_adapter_set_afh_channel_classification_async(bt_instance_t* ins, uint16_t central_frequency, uint16_t band_width, uint16_t number, bt_status_cb_t cb, void* userdata);

设置 AFH 自适应跳频信道分类（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">central\_frequency</span> 中心频率（MHz）。
  - <span class="reference">band\_width</span> 带宽（MHz）。
  - <span class="reference">number</span> 号码。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_set\_auto\_sniff\_async

    bt_status_t bt_adapter_set_auto_sniff_async(bt_instance_t* ins, bt_auto_sniff_params_t* params, bt_status_cb_t cb, void* userdata);

设置自动 Sniff 模式参数（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">params</span> 参数结构体。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_disconnect\_all\_devices\_async

    bt_status_t bt_adapter_disconnect_all_devices_async(bt_instance_t* ins, bt_status_cb_t cb, void* userdata);

断开所有已连接设备（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_is\_support\_bredr\_async

    bt_status_t bt_adapter_is_support_bredr_async(bt_instance_t* ins, bt_bool_cb_t cb, void* userdata);

Check if BR/EDR is supported（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_is\_support\_le\_async

    bt_status_t bt_adapter_is_support_le_async(bt_instance_t* ins, bt_bool_cb_t cb, void* userdata);

Check if BLE is supported（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。

### bt\_adapter\_is\_support\_leaudio\_async

    bt_status_t bt_adapter_is_support_leaudio_async(bt_instance_t* ins, bt_bool_cb_t cb, void* userdata);

查询是否支持 LE Audio（异步版本）。

**参数**：

  - <span class="reference">ins</span> 蓝牙客户端实例。
  - <span class="reference">cb</span> 回调函数。
  - <span class="reference">userdata</span> 用户数据。
