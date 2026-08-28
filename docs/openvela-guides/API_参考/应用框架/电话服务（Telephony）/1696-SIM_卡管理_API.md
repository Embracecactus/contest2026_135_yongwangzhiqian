# SIM 卡管理 API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1696&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:36  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/telephony/telephony_sim.md) | 简体中文 \]

# SIM 卡管理 API

SIM 卡状态查询和管理。

头文件：<span class="reference">\#include \<tapi\_sim.h\></span>

# openvela 实现说明

  - **SIM 卡管理**：所有接口均带 <span class="reference">slot\_id</span> 参数，用于标识 SIM 卡
  - **PIN 管理**：提供 <span class="reference">enter\_pin</span> / <span class="reference">change\_pin</span> / <span class="reference">reset\_pin</span> / <span class="reference">lock\_pin</span> / <span class="reference">unlock\_pin</span> 完整 PIN/PUK 流程
  - **APDU 通道**：通过 <span class="reference">open\_logical\_channel</span> / <span class="reference">close\_logical\_channel</span> / <span class="reference">transmit\_apdu\_\*</span> 直接向 SIM 卡发送 APDU 命令
  - **UICC 开关**：通过 <span class="reference">get\_uicc\_enablement</span> / <span class="reference">set\_uicc\_enablement</span> 控制 SIM 卡的启用状态
  - **事件订阅**：<span class="reference">tapi\_sim\_register</span> / <span class="reference">tapi\_sim\_unregister</span> 监听 SIM 卡状态变化

# SIM 状态查询

## tapi\_sim\_has\_icc\_card

    int tapi_sim_has_icc_card(tapi_context context, int slot_id, bool* out);

查询是否插入 SIM 卡。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_get\_sim\_state

    int tapi_sim_get_sim_state(tapi_context context, int slot_id, int* out);

获取 SIM 卡状态。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_get\_sim\_operator

    int tapi_sim_get_sim_operator(tapi_context context, int slot_id, int length, char* out);

获取 SIM 卡运营商信息。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">length</span> 数据长度。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_get\_sim\_operator\_name

    int tapi_sim_get_sim_operator_name(tapi_context context, int slot_id, char** out);

获取 SIM 卡运营商信息。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_get\_sim\_iccid

    int tapi_sim_get_sim_iccid(tapi_context context, int slot_id, char** out);

获取 SIM 卡 ICCID。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_get\_subscriber\_id

    int tapi_sim_get_subscriber_id(tapi_context context, int slot_id, char** out);

获取用户标识（IMSI）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# 事件订阅

## tapi\_sim\_register

    int tapi_sim_register(tapi_context context, int slot_id, tapi_indication_msg msg, void* user_obj, tapi_async_function p_handle);

获取用户标识（IMSI）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">msg</span> 消息内容。
  - <span class="reference">user\_obj</span> 用户对象指针。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_unregister

    int tapi_sim_unregister(tapi_context context, int watch_id);

获取用户标识（IMSI）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">watch\_id</span> 监听 ID（用于取消监听）。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# PIN 管理

## tapi\_sim\_change\_pin

    int tapi_sim_change_pin(tapi_context context, int slot_id, int event_id, char* pin_type, char* old_pin, char* new_pin, tapi_async_function p_handle);

获取用户标识（IMSI）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">pin\_type</span> PIN 码类型。
  - <span class="reference">old\_pin</span> 旧 PIN 码。
  - <span class="reference">new\_pin</span> 新 PIN 码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_enter\_pin

    int tapi_sim_enter_pin(tapi_context context, int slot_id, int event_id, char* pin_type, char* pin, tapi_async_function p_handle);

获取用户标识（IMSI）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">pin\_type</span> PIN 码类型。
  - <span class="reference">pin</span> PIN 码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_reset\_pin

    int tapi_sim_reset_pin(tapi_context context, int slot_id, int event_id, char* puk_type, char* puk, char* new_pin, tapi_async_function p_handle);

获取用户标识（IMSI）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">puk\_type</span> PUK 码类型。
  - <span class="reference">puk</span> PUK 码。
  - <span class="reference">new\_pin</span> 新 PIN 码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_lock\_pin

    int tapi_sim_lock_pin(tapi_context context, int slot_id, int event_id, char* pin_type, char* pin, tapi_async_function p_handle);

获取用户标识（IMSI）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">pin\_type</span> PIN 码类型。
  - <span class="reference">pin</span> PIN 码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_unlock\_pin

    int tapi_sim_unlock_pin(tapi_context context, int slot_id, int event_id, char* pin_type, char* pin, tapi_async_function p_handle);

获取用户标识（IMSI）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">pin\_type</span> PIN 码类型。
  - <span class="reference">pin</span> PIN 码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# APDU 逻辑通道

## tapi\_sim\_open\_logical\_channel

    int tapi_sim_open_logical_channel(tapi_context context, int slot_id, int event_id, unsigned char aid[], int len, tapi_async_function p_handle);

打开 SIM 卡逻辑通道。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">aid</span> 应用 ID。
  - <span class="reference">len</span> 长度。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_close\_logical\_channel

    int tapi_sim_close_logical_channel(tapi_context context, int slot_id, int event_id, int session_id, tapi_async_function p_handle);

关闭 SIM 卡逻辑通道。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">session\_id</span> 会话 ID。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_transmit\_apdu\_logical\_channel

    int tapi_sim_transmit_apdu_logical_channel(tapi_context context, int slot_id, int event_id, int session_id, unsigned char pdu[], int len, tapi_async_function p_handle);

通过逻辑通道发送 APDU 命令。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">session\_id</span> 会话 ID。
  - <span class="reference">pdu</span> PDU 数据。
  - <span class="reference">len</span> 长度。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_transmit\_apdu\_basic\_channel

    int tapi_sim_transmit_apdu_basic_channel(tapi_context context, int slot_id, int event_id, unsigned char pdu[], int len, tapi_async_function p_handle);

通过逻辑通道发送 APDU 命令。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">pdu</span> PDU 数据。
  - <span class="reference">len</span> 长度。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# UICC 开关

## tapi\_sim\_get\_uicc\_enablement

    int tapi_sim_get_uicc_enablement(tapi_context context, int slot_id, tapi_sim_uicc_app_state* out);

获取用户标识（IMSI）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_set\_uicc\_enablement

    int tapi_sim_set_uicc_enablement(tapi_context context, int slot_id, int event_id, int state, tapi_async_function p_handle);

获取用户标识（IMSI）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">state</span> 状态。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_sim\_get\_sim\_invalid

    int tapi_sim_get_sim_invalid(tapi_context context, int slot_id, int* out);

获取用户标识（IMSI）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。
