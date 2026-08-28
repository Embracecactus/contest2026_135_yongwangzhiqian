# IMS 服务 API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1701&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:39  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/telephony/telephony_ims.md) | 简体中文 \]

# IMS 服务 API

IP 多媒体子系统（VoLTE/VoWiFi）管理。

头文件：<span class="reference">\#include \<tapi\_ims.h\></span>

# openvela 实现说明

  - **IMS 开关**：通过 <span class="reference">turn\_on</span> / <span class="reference">turn\_off</span> 控制 IMS 服务的启用状态
  - **注册状态**：查询 IMS 是否已注册到网络，订阅注册状态变化事件
  - **业务开关**：<span class="reference">set\_service\_status</span> 控制具体业务（如语音、视频）的启用
  - **VoLTE 支持**：通过 <span class="reference">is\_volte\_available</span> 查询当前网络是否支持 VoLTE
  - **SIM 卡标识**：所有接口带 <span class="reference">slot\_id</span> 参数

# IMS 开关

## tapi\_ims\_turn\_on

    int tapi_ims_turn_on(tapi_context context, int slot_id);

开启 IMS 服务（VoLTE/VoWiFi）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_ims\_turn\_off

    int tapi_ims_turn_off(tapi_context context, int slot_id);

关闭 IMS 服务。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# 服务状态配置

## tapi\_ims\_set\_service\_status

    int tapi_ims_set_service_status(tapi_context context, int slot_id, int capability);

开启 IMS 服务（VoLTE/VoWiFi）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">capability</span> 能力值。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# 注册状态与事件

## tapi\_ims\_get\_registration

    int tapi_ims_get_registration(tapi_context context, int slot_id, tapi_ims_registration_info* ims_reg);

开启 IMS 服务（VoLTE/VoWiFi）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">ims\_reg</span> IMS 注册状态。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_ims\_register\_registration\_change

    int tapi_ims_register_registration_change(tapi_context context, int slot_id, void* user_obj, tapi_async_function p_handle);

开启 IMS 服务（VoLTE/VoWiFi）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">user\_obj</span> 用户对象指针。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_ims\_is\_registered

    int tapi_ims_is_registered(tapi_context context, int slot_id, bool* out);

开启 IMS 服务（VoLTE/VoWiFi）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

# VoLTE 与业务查询

## tapi\_ims\_is\_volte\_available

    int tapi_ims_is_volte_available(tapi_context context, int slot_id, bool* out);

开启 IMS 服务（VoLTE/VoWiFi）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_ims\_get\_subscriber\_uri\_number

    int tapi_ims_get_subscriber_uri_number(tapi_context context, int slot_id, char** out);

开启 IMS 服务（VoLTE/VoWiFi）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## tapi\_ims\_get\_enabled

    int tapi_ims_get_enabled(tapi_context context, int slot_id, bool* out);

查询 IMS 是否启用。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。
  - <span class="reference">out</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。
