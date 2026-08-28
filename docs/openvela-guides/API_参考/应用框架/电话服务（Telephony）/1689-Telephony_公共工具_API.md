# Telephony 公共工具 API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1689&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:31  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/telephony/telephony.md) | 简体中文 \]

# Telephony 公共工具 API

TAPI 提供的通用工具函数，涵盖状态字符串转换、modem 路径解析、运营商状态解析等辅助能力。

头文件：<span class="reference">\#include \<tapi.h\></span>

# openvela 实现说明

  - **字符串↔枚举转换**：<span class="reference">\*\_from\_string</span> / <span class="reference">\*\_to\_string</span> 系列把 oFono D-Bus 字符串与 TAPI 枚举互转，便于状态解析
  - **Modem 路径**：<span class="reference">tapi\_utils\_get\_modem\_path</span> 将 <span class="reference">slot\_id</span> 转成 oFono 的 modem 对象路径
  - **工具性质**：本组接口不依赖 tapi\_context，可在任意位置直接调用
  - **适用场景**：实现自定义事件处理、打印调试日志、或扩展 TAPI 能力时使用

# SIM 状态

## tapi\_sim\_state\_to\_string

    const char* tapi_sim_state_to_string(tapi_sim_state state);

将 SIM 状态枚举转为可读字符串。

**参数**：

  - <span class="reference">state</span> SIM 状态枚举值。

**返回值**：

返回状态的字符串表示，失败时返回 <span class="reference">NULL</span> 或占位字符串。

# APN 工具

## tapi\_utils\_apn\_auth\_from\_string

    int tapi_utils_apn_auth_from_string(const char* str);

将认证类型字符串转为 APN 认证枚举值。

**参数**：

  - <span class="reference">str</span> 认证类型字符串（如 <span class="reference">"pap"</span>、<span class="reference">"chap"</span>）。

**返回值**：

返回认证枚举值；无效字符串时返回错误值。

## tapi\_utils\_apn\_auth\_to\_string

    const char* tapi_utils_apn_auth_to_string(int auth);

将 APN 认证枚举值转为字符串。

**参数**：

  - <span class="reference">auth</span> 认证枚举值。

**返回值**：

返回对应字符串。

## tapi\_utils\_apn\_proto\_from\_string

    int tapi_utils_apn_proto_from_string(const char* str);

将协议字符串转为 APN 协议枚举值。

**参数**：

  - <span class="reference">str</span> 协议字符串（如 <span class="reference">"ip"</span>、<span class="reference">"ipv6"</span>、<span class="reference">"dual"</span>）。

**返回值**：

返回协议枚举值。

## tapi\_utils\_apn\_proto\_to\_string

    const char* tapi_utils_apn_proto_to_string(int proto);

将 APN 协议枚举值转为字符串。

**参数**：

  - <span class="reference">proto</span> 协议枚举值。

**返回值**：

返回对应字符串。

## tapi\_utils\_apn\_type\_from\_string

    int tapi_utils_apn_type_from_string(const char* str);

将 APN 类型字符串转为类型枚举值。

**参数**：

  - <span class="reference">str</span> APN 类型字符串（如 <span class="reference">"default"</span>、<span class="reference">"mms"</span>、<span class="reference">"ims"</span>）。

**返回值**：

返回类型枚举值。

## tapi\_utils\_apn\_type\_to\_string

    const char* tapi_utils_apn_type_to_string(int type);

将 APN 类型枚举值转为字符串。

**参数**：

  - <span class="reference">type</span> APN 类型枚举值。

**返回值**：

返回对应字符串。

# 通话工具

## tapi\_utils\_call\_disconnected\_reason

    int tapi_utils_call_disconnected_reason(const char* reason);

将通话断开原因字符串转为 TAPI 断开原因枚举值。

**参数**：

  - <span class="reference">reason</span> 断开原因字符串。

**返回值**：

返回断开原因枚举值。

## tapi\_utils\_call\_status\_from\_string

    int tapi_utils_call_status_from_string(const char* status);

将通话状态字符串转为状态枚举值。

**参数**：

  - <span class="reference">status</span> 状态字符串（如 <span class="reference">"active"</span>、<span class="reference">"held"</span>、<span class="reference">"dialing"</span>）。

**返回值**：

返回状态枚举值。

# 小区与网络工具

## tapi\_utils\_cell\_type\_from\_string

    int tapi_utils_cell_type_from_string(const char* str);

将小区类型字符串转为枚举值。

**参数**：

  - <span class="reference">str</span> 小区类型字符串。

**返回值**：

返回小区类型枚举值。

## tapi\_utils\_cell\_type\_to\_string

    const char* tapi_utils_cell_type_to_string(int type);

将小区类型枚举值转为字符串。

**参数**：

  - <span class="reference">type</span> 小区类型枚举值。

**返回值**：

返回对应字符串。

## tapi\_utils\_network\_mode\_from\_string

    int tapi_utils_network_mode_from_string(const char* str);

将网络模式字符串转为枚举值。

**参数**：

  - <span class="reference">str</span> 网络模式字符串（如 <span class="reference">"gsm"</span>、<span class="reference">"lte"</span>）。

**返回值**：

返回网络模式枚举值。

## tapi\_utils\_network\_mode\_to\_string

    const char* tapi_utils_network_mode_to_string(int mode);

将网络模式枚举值转为字符串。

**参数**：

  - <span class="reference">mode</span> 网络模式枚举值。

**返回值**：

返回对应字符串。

## tapi\_utils\_network\_type\_from\_ril\_tech

    int tapi_utils_network_type_from_ril_tech(int tech);

将 RIL 层传来的网络技术值转为 TAPI 网络类型枚举。

**参数**：

  - <span class="reference">tech</span> RIL 网络技术值。

**返回值**：

返回 TAPI 网络类型枚举值。

## tapi\_utils\_network\_operator\_status\_from\_string

    int tapi_utils_network_operator_status_from_string(const char* str);

将运营商状态字符串转为枚举值。

**参数**：

  - <span class="reference">str</span> 运营商状态字符串。

**返回值**：

返回运营商状态枚举值。

## tapi\_utils\_operator\_status\_from\_string

    int tapi_utils_operator_status_from_string(const char* str);

<span class="reference">tapi\_utils\_network\_operator\_status\_from\_string</span> 的简写版本，等价功能。

**参数**：

  - <span class="reference">str</span> 运营商状态字符串。

**返回值**：

返回运营商状态枚举值。

# 注册状态工具

## tapi\_utils\_registration\_mode\_from\_string

    int tapi_utils_registration_mode_from_string(const char* str);

将注册模式字符串转为枚举值。

**参数**：

  - <span class="reference">str</span> 注册模式字符串（如 <span class="reference">"auto"</span>、<span class="reference">"manual"</span>）。

**返回值**：

返回注册模式枚举值。

## tapi\_utils\_registration\_status\_from\_string

    int tapi_utils_registration_status_from_string(const char* str);

将注册状态字符串转为枚举值。

**参数**：

  - <span class="reference">str</span> 注册状态字符串。

**返回值**：

返回注册状态枚举值。

## tapi\_utils\_get\_registration\_status\_string

    const char* tapi_utils_get_registration_status_string(int status);

将注册状态枚举值转为可读字符串。

**参数**：

  - <span class="reference">status</span> 注册状态枚举值。

**返回值**：

返回对应字符串。

# Modem 路径与 Slot

## tapi\_utils\_get\_modem\_path

    const char* tapi_utils_get_modem_path(int slot_id);

根据 SIM 卡槽 ID 获取 oFono 的 modem 对象路径。

**参数**：

  - <span class="reference">slot\_id</span> SIM 卡槽 ID（0 或 1）。

**返回值**：

返回 modem 对象路径字符串（如 <span class="reference">/ril\_0</span>、<span class="reference">/ril\_1</span>）。

## tapi\_utils\_get\_slot\_id

    int tapi_utils_get_slot_id(const char* path);

从 oFono modem 对象路径解析 SIM 卡槽 ID。

**参数**：

  - <span class="reference">path</span> modem 对象路径。

**返回值**：

返回对应的卡槽 ID（0 或 1），无效路径时返回负值。
