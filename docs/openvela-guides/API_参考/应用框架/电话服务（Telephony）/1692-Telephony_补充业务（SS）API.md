# Telephony 补充业务（SS）API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1692&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:33  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/telephony/telephony_ss.md) | 简体中文 \]

# Telephony 补充业务（SS）API

Supplementary Services（补充业务）是 3GPP 蜂窝标准定义的增值通话能力，包括呼叫限制（Call Barring）、呼叫转移（Call Forwarding）、主叫识别（CLIR/CLIP）、呼叫等待、USSD 等。

头文件：<span class="reference">\#include \<tapi\_ss.h\></span>

# openvela 实现说明

  - **呼叫限制 Call Barring**：<span class="reference">tapi\_ss\_\*\_call\_barring\*</span> 系列控制拨出/拨入的号码范围
  - **呼叫转移 Call Forwarding**：<span class="reference">tapi\_ss\_\*\_call\_forwarding\*</span> 系列配置无条件/忙/无应答/不可达四种转移
  - **CLIR/CLIP**：主叫号码显示与限制，通过 <span class="reference">calling\_line\_restriction</span> 和 <span class="reference">calling\_line\_presentation\_info</span> 接口
  - **USSD**：<span class="reference">tapi\_ss\_send\_ussd</span> 发送 <span class="reference">\*\#xxxx\#</span> 命令，<span class="reference">tapi\_ss\_cancel\_ussd</span> 取消会话
  - **FDN**：固定拨号开关通过 <span class="reference">tapi\_ss\_enable\_fdn</span> / <span class="reference">tapi\_ss\_query\_fdn</span>
  - **SIM 卡标识**：所有接口带 <span class="reference">slot\_id</span>
  - **异步回调**：所有操作使用 <span class="reference">tapi\_async\_function</span>

# 呼叫限制

## tapi\_ss\_request\_call\_barring

    int tapi_ss_request_call_barring(tapi_context context, int slot_id, int event_id,
                                     char* fac, char* pin2,
                                     tapi_async_function p_handle);

请求某类呼叫限制（按 FAC 编码）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">fac</span> 呼叫限制 FAC 码（如 <span class="reference">"OI"</span>、<span class="reference">"IR"</span> 等）。
  - <span class="reference">pin2</span> SIM 卡 PIN2 码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_set\_call\_barring\_option

    int tapi_ss_set_call_barring_option(tapi_context context, int slot_id, int event_id,
                                        char* facility, char* pin2,
                                        tapi_async_function p_handle);

设置呼叫限制选项。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">facility</span> 限制类型字符串。
  - <span class="reference">pin2</span> SIM 卡 PIN2 码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_get\_call\_barring\_option

    int tapi_ss_get_call_barring_option(tapi_context context, int slot_id,
                                        const char* service_type, char** out);

查询当前呼叫限制配置。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">service\_type</span> 服务类型字符串。
  - <span class="reference">out</span> 输出参数，返回配置字符串。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_change\_call\_barring\_password

    int tapi_ss_change_call_barring_password(tapi_context context, int slot_id, int event_id,
                                             char* old_pin, char* new_pin,
                                             tapi_async_function p_handle);

修改呼叫限制服务的密码。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">old\_pin</span> 旧密码。
  - <span class="reference">new\_pin</span> 新密码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_disable\_all\_call\_barrings

    int tapi_ss_disable_all_call_barrings(tapi_context context, int slot_id, int event_id,
                                          char* passwd, tapi_async_function p_handle);

关闭所有呼叫限制。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">passwd</span> 服务密码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_disable\_all\_incoming

    int tapi_ss_disable_all_incoming(tapi_context context, int slot_id,
                                     int event_id, char* passwd,
                                     tapi_async_function p_handle);

关闭所有入呼限制。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">passwd</span> 服务密码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_disable\_all\_outgoing

    int tapi_ss_disable_all_outgoing(tapi_context context, int slot_id,
                                     int event_id, char* passwd,
                                     tapi_async_function p_handle);

关闭所有出呼限制。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">passwd</span> 服务密码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

# 呼叫转移

## tapi\_ss\_query\_call\_forwarding\_option

    int tapi_ss_query_call_forwarding_option(tapi_context context, int slot_id, int event_id,
                                             int cf_reason, tapi_async_function p_handle);

查询呼叫转移配置。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">cf\_reason</span> 转移类型（无条件/忙/无应答/不可达，详见 <span class="reference">tapi\_call\_forward\_option</span>）。
  - <span class="reference">p\_handle</span> 异步回调函数，回调时返回 <span class="reference">tapi\_call\_forwarding\_info</span>。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_set\_call\_forwarding\_option

    int tapi_ss_set_call_forwarding_option(tapi_context context, int slot_id, int event_id,
                                           tapi_call_forwarding_info* info,
                                           tapi_async_function p_handle);

设置呼叫转移配置。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">info</span> 呼叫转移配置结构体。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

# USSD 会话

## tapi\_ss\_initiate\_service

    int tapi_ss_initiate_service(tapi_context context, int slot_id, int event_id,
                                 char* command, tapi_async_function p_handle);

发起 SS 服务命令（USSD/SS 字符串形式，如 <span class="reference">\*\#06\#</span>）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">command</span> 命令字符串。
  - <span class="reference">p\_handle</span> 异步回调函数，回调返回 <span class="reference">tapi\_ss\_initiate\_info</span>。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_get\_ussd\_state

    int tapi_get_ussd_state(tapi_context context, int slot_id, char** out);

查询当前 USSD 会话状态。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">out</span> 输出参数，返回状态字符串（如 <span class="reference">"idle"</span>、<span class="reference">"user-response"</span>）。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_send\_ussd

    int tapi_ss_send_ussd(tapi_context context, int slot_id, int event_id, char* reply,
                         tapi_async_function p_handle);

发送 USSD 回复消息。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">reply</span> 回复字符串。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_cancel\_ussd

    int tapi_ss_cancel_ussd(tapi_context context, int slot_id, int event_id,
                           tapi_async_function p_handle);

取消当前 USSD 会话。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

# 呼叫等待

## tapi\_ss\_set\_call\_waiting

    int tapi_ss_set_call_waiting(tapi_context context, int slot_id, int event_id, bool enable,
                                 tapi_async_function p_handle);

启用或禁用呼叫等待功能。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">enable</span> <span class="reference">true</span> 启用，<span class="reference">false</span> 禁用。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_get\_call\_waiting

    int tapi_ss_get_call_waiting(tapi_context context, int slot_id, int event_id,
                                 tapi_async_function p_handle);

查询呼叫等待开关状态。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">p\_handle</span> 异步回调函数，回调返回当前状态。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

# CLIR / CLIP（主叫号码显示与限制）

## tapi\_ss\_get\_calling\_line\_presentation\_info

    int tapi_ss_get_calling_line_presentation_info(tapi_context context, int slot_id,
                                                   int event_id, tapi_async_function p_handle);

查询主叫号码显示（CLIP）状态。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_set\_calling\_line\_restriction

    int tapi_ss_set_calling_line_restriction(tapi_context context, int slot_id, int event_id,
                                             tapi_clir_status status,
                                             tapi_async_function p_handle);

设置主叫号码限制（CLIR）状态。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">status</span> CLIR 状态枚举值（<span class="reference">CLIR\_DEFAULT</span> / <span class="reference">CLIR\_INVOCATION</span> / <span class="reference">CLIR\_SUPPRESSION</span>）。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_get\_calling\_line\_restriction\_info

    int tapi_ss_get_calling_line_restriction_info(tapi_context context, int slot_id,
                                                  int event_id, tapi_async_function p_handle);

查询主叫号码限制（CLIR）状态。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

# FDN（固定拨号）开关

## tapi\_ss\_enable\_fdn

    int tapi_ss_enable_fdn(tapi_context context, int slot_id, int event_id,
                          bool enable, char* pin2, tapi_async_function p_handle);

启用或禁用 FDN 模式（需要 PIN2）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">enable</span> <span class="reference">true</span> 启用 FDN，<span class="reference">false</span> 禁用。
  - <span class="reference">pin2</span> SIM 卡 PIN2 码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_ss\_query\_fdn

    int tapi_ss_query_fdn(tapi_context context, int slot_id, int event_id,
                         tapi_async_function p_handle);

查询 FDN 开关状态。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

# 事件订阅

## tapi\_ss\_register

    int tapi_ss_register(tapi_context context, int slot_id, tapi_indication_msg msg,
                        void* user_obj, tapi_async_function p_handle);

注册 SS 相关事件回调。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">msg</span> 要监听的事件类型。
  - <span class="reference">user\_obj</span> 用户数据。
  - <span class="reference">p\_handle</span> 事件回调函数。

**返回值**：

成功时返回 watch ID，失败时返回负的错误码。

## tapi\_ss\_unregister

    int tapi_ss_unregister(tapi_context context, int watch_id);

取消 SS 事件订阅。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">watch\_id</span> 订阅时返回的 watch ID。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。
