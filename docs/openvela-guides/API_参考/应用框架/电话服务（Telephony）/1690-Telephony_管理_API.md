# Telephony 管理 API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1690&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:32  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/telephony/telephony_manager.md) | 简体中文 \]

# Telephony 管理 API

蜂窝通信管理接口，包括初始化、状态查询和事件注册。

头文件：<span class="reference">\#include \<tapi\_manager.h\></span>

# openvela 实现说明

  - **基于 D-Bus**：TAPI Manager 通过 D-Bus 与 Telephony Core Stack（oFono）通信，对外以标准 C 接口封装
  - **SIM 卡标识**：管理器层面不直接涉及 SIM 卡槽选择，涉及特定卡槽的操作在 <span class="reference">tapi\_sim</span> 等子模块中使用 <span class="reference">slot\_id</span> 参数
  - **客户端句柄**：通过 <span class="reference">tapi\_open</span> 获取 <span class="reference">tapi\_context</span>，所有后续调用均以该 context 作为第一个参数
  - **事件订阅**：通过 <span class="reference">tapi\_register</span> 注册事件回调，<span class="reference">tapi\_unregister</span> 取消订阅
  - **同步 vs 异步**：多数接口是异步的（带回调），部分提供 <span class="reference">\*\_sync</span> 变体用于简单场景

# 客户端连接管理

## tapi\_open

    tapi_context tapi_open(const char* client_name, tapi_client_ready_function callback, void* user_data);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">client\_name</span> 客户端名称。
  - <span class="reference">callback</span> 回调函数。
  - <span class="reference">user\_data</span> 用户数据，传递给回调函数。

**返回值**：

成功时返回有效的 <span class="reference">tapi\_context</span> 句柄，失败时返回 <span class="reference">NULL</span>。

## tapi\_open\_service

    tapi_context tapi_open_service(const char* client_name, tapi_client_ready_function callback, void* user_data, unsigned int tapi_service);

打开 Telephony 连接，指定服务类型。

**参数**：

  - <span class="reference">client\_name</span> 客户端名称。
  - <span class="reference">callback</span> 回调函数。
  - <span class="reference">user\_data</span> 用户数据，传递给回调函数。
  - <span class="reference">tapi\_service</span> Telephony 服务类型。

**返回值**：

成功时返回有效的 <span class="reference">tapi\_context</span> 句柄，失败时返回 <span class="reference">NULL</span>。

## tapi\_close

    int tapi_close(tapi_context context);

关闭 Telephony 连接。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# 能力查询

## tapi\_is\_feature\_supported

    bool tapi_is_feature_supported(tapi_feature_type feature);

查询是否支持指定功能。

**参数**：

  - <span class="reference">feature</span> 功能类型枚举值。

**返回值**：

支持时返回 <span class="reference">true</span>，不支持时返回 <span class="reference">false</span>。

# 无线电控制

## tapi\_set\_radio\_power

    int tapi_set_radio_power(tapi_context context, int slot_id, int event_id, bool state, tapi_async_function p_handle);

设置射频功率。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">state</span> 状态。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_set\_radio\_power\_async

    int tapi_set_radio_power_async(tapi_context context, int slot_id, int event_id, bool state, void* user_data, tapi_async_function p_handle);

设置射频功率（异步版本）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">state</span> 状态。
  - <span class="reference">user\_data</span> 用户数据，传递给回调函数。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_get\_radio\_power

    int tapi_get_radio_power(tapi_context context, int slot_id, bool* out);

获取射频功率状态。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# 网络模式

## tapi\_set\_pref\_net\_mode

    int tapi_set_pref_net_mode(tapi_context context, int slot_id, int event_id, tapi_pref_net_mode mode, tapi_async_function p_handle);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">mode</span> 模式。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_get\_pref\_net\_mode

    int tapi_get_pref_net_mode(tapi_context context, int slot_id, tapi_pref_net_mode* out);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_get\_radio\_state

    int tapi_get_radio_state(tapi_context context, int slot_id, tapi_radio_state* out);

获取射频状态。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# Modem 信息

## tapi\_get\_imei

    int tapi_get_imei(tapi_context context, int slot_id, char** out);

获取设备 IMEI。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_get\_imeisv

    int tapi_get_imeisv(tapi_context context, int slot_id, char** out);

获取设备 IMEI。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_get\_modem\_revision

    int tapi_get_modem_revision(tapi_context context, int slot_id, char** out);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_get\_phone\_state

    int tapi_get_phone_state(tapi_context context, int slot_id, tapi_phone_state* state);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">state</span> 状态。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# 手机号码

## tapi\_get\_msisdn\_number

    int tapi_get_msisdn_number(tapi_context context, int slot_id, char** out);

获取 SIM 卡电话号码（MSISDN）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# Modem 状态与控制

## tapi\_get\_modem\_activity\_info

    int tapi_get_modem_activity_info(tapi_context context, int slot_id, int event_id, tapi_async_function p_handle);

获取 Modem 活动信息。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_invoke\_oem\_ril\_request\_raw

    int tapi_invoke_oem_ril_request_raw(tapi_context context, int slot_id, int event_id, unsigned char oem_req[], int length, tapi_async_function p_handle);

发送 OEM RIL 请求。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">oem\_req</span> OEM 请求数据。
  - <span class="reference">length</span> 数据长度。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_invoke\_oem\_ril\_request\_strings

    int tapi_invoke_oem_ril_request_strings(tapi_context context, int slot_id, int event_id, char* oem_req[], int length, tapi_async_function p_handle);

发送 OEM RIL 请求。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">oem\_req</span> OEM 请求数据。
  - <span class="reference">length</span> 数据长度。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_enable\_modem

    int tapi_enable_modem(tapi_context context, int slot_id, int event_id, bool enable, tapi_async_function p_handle);

启用 Modem。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">enable</span> 是否启用。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_enable\_modem\_abnormal\_event

    int tapi_enable_modem_abnormal_event(tapi_context context, int slot_id, bool enable, int event_id, int module_mask, int from_event_id, int to_event_id, tapi_async_function p_handle);

启用 Modem。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">enable</span> 是否启用。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">module\_mask</span> 模块掩码。
  - <span class="reference">from\_event\_id</span> 源事件 ID。
  - <span class="reference">to\_event\_id</span> 目标事件 ID。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_set\_signal\_report\_threshold

    int tapi_set_signal_report_threshold(tapi_context context, int slot_id, int event_id, int type, tapi_async_function p_handle);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">type</span> 类型。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_suppress\_message\_report

    int tapi_suppress_message_report(tapi_context context, int slot_id, int event_id, bool enable, tapi_async_function p_handle);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">enable</span> 是否启用。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_enable\_modem\_stationary

    int tapi_enable_modem_stationary(tapi_context context, int slot_id, int event_id, bool enable, tapi_async_function p_handle);

启用 Modem。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">enable</span> 是否启用。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_set\_modem\_stationary\_threshold

    int tapi_set_modem_stationary_threshold(tapi_context context, int slot_id, int event_id, int value, tapi_async_function p_handle);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">value</span> 值。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_get\_modem\_status

    int tapi_get_modem_status(tapi_context context, int slot_id, int event_id, tapi_async_function p_handle);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_get\_modem\_status\_sync

    int tapi_get_modem_status_sync(tapi_context context, int slot_id, tapi_modem_state* out);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_set\_fast\_dormancy

    int tapi_set_fast_dormancy(tapi_context context, int slot_id, int event_id, bool state, tapi_async_function p_handle);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">state</span> 状态。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_get\_phone\_number

    int tapi_get_phone_number(tapi_context context, int slot_id, char** out);

获取本机号码。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# 事件订阅

## tapi\_register

    int tapi_register(tapi_context context, int slot_id, tapi_indication_msg msg, void* user_obj, tapi_async_function p_handle);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">msg</span> 消息内容。
  - <span class="reference">user\_obj</span> 用户对象指针。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_unregister

    int tapi_unregister(tapi_context context, int watch_id);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">watch\_id</span> 监听 ID（用于取消监听）。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# 运营商配置

## tapi\_get\_carrier\_config\_bool

    int tapi_get_carrier_config_bool(tapi_context context, int slot_id, char* key, bool* out);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">key</span> 键名。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_get\_carrier\_config\_int

    int tapi_get_carrier_config_int(tapi_context context, int slot_id, char* key, int* out);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">key</span> 键名。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_get\_carrier\_config\_string

    int tapi_get_carrier_config_string(tapi_context context, int slot_id, char* key, char** out);

打开 Telephony 连接，获取上下文句柄。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">key</span> 键名。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。
