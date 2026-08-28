# Telephony SIM Toolkit (STK) API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1697&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:36  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/telephony/telephony_stk.md) | 简体中文 \]

# Telephony SIM Toolkit (STK) API

SIM Application Toolkit（STK / CAT）是运营商在 SIM 卡上预置的交互菜单与事件处理能力，常见用途包括运营商增值菜单、服务密码管理、URL 浏览器启动等。

头文件：<span class="reference">\#include \<tapi\_stk.h\></span>

# openvela 实现说明

  - **Agent 模式**：应用侧作为"STK Agent"注册到 TAPI，SIM 卡主动发起的显示/输入/确认请求通过 Agent 回调触达应用
  - **注册层级**：支持 per-slot Agent（通过 <span class="reference">tapi\_stk\_agent\_register</span>）与 default Agent（系统默认 UI）
  - **主菜单**：<span class="reference">tapi\_stk\_get\_main\_menu\*</span> 查询 SIM 卡提供的主菜单结构
  - **Proactive Command 响应**：<span class="reference">tapi\_stk\_handle\_agent\_\*</span> 系列接口用于将 Agent 对 SIM 卡主动命令的响应回传给 SIM
  - **SIM 卡标识**：所有接口带 <span class="reference">slot\_id</span>

# Agent 注册

## tapi\_stk\_agent\_register

    int tapi_stk_agent_register(tapi_context context, int slot_id,
                                char* agent_id, tapi_async_function p_handle);

为指定 SIM 卡槽注册 STK Agent。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">agent\_id</span> Agent 标识字符串。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_agent\_unregister

    int tapi_stk_agent_unregister(tapi_context context, int slot_id,
                                  char* agent_id, tapi_async_function p_handle);

取消 STK Agent 注册。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">agent\_id</span> Agent 标识字符串。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_default\_agent\_register

    int tapi_stk_default_agent_register(tapi_context context, int slot_id,
                                        char* agent_id, tapi_async_function p_handle);

注册为默认 STK Agent（全局 fallback）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">agent\_id</span> Agent 标识字符串。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_default\_agent\_unregister

    int tapi_stk_default_agent_unregister(tapi_context context, int slot_id,
                                          tapi_async_function p_handle);

取消默认 STK Agent 注册。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_agent\_interface\_register

    int tapi_stk_agent_interface_register(tapi_context context, int slot_id, char* agent_id,
                                          tapi_stk_agent_interface* iface);

在 Agent 层注册具体的接口实现（回调函数集合）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">agent\_id</span> Agent 标识字符串。
  - <span class="reference">iface</span> Agent 接口回调结构体指针。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_agent\_interface\_unregister

    int tapi_stk_agent_interface_unregister(tapi_context context, char* agent_id);

注销 Agent 接口实现。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">agent\_id</span> Agent 标识字符串。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_default\_agent\_interface\_register

    int tapi_stk_default_agent_interface_register(tapi_context context, int slot_id,
                                                  tapi_stk_agent_interface* iface);

为默认 Agent 注册接口实现。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">iface</span> Agent 接口回调结构体指针。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_default\_agent\_interface\_unregister

    int tapi_stk_default_agent_interface_unregister(tapi_context context, int slot_id);

注销默认 Agent 的接口实现。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

# 主菜单与空闲模式

## tapi\_stk\_select\_item

    int tapi_stk_select_item(tapi_context context, int slot_id,
                             int item_idx, tapi_async_function p_handle);

选择主菜单中的某个条目，触发 SIM 卡的业务响应。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">item\_idx</span> 条目索引。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_get\_idle\_mode\_text

    int tapi_stk_get_idle_mode_text(tapi_context context, int slot_id, char** text);

查询 SIM 卡设定的空闲模式显示文本。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">text</span> 输出参数，返回文本字符串。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_get\_idle\_mode\_icon

    int tapi_stk_get_idle_mode_icon(tapi_context context, int slot_id, char** icon);

查询 SIM 卡设定的空闲模式图标标识。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">icon</span> 输出参数，返回图标标识字符串。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_get\_main\_menu

    int tapi_stk_get_main_menu(tapi_context context, int slot_id, int* length,
                               tapi_stk_menu_item out[]);

获取 SIM 卡提供的主菜单条目列表。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">length</span> 输入输出参数：入参表示缓冲区容量，出参返回实际条目数。
  - <span class="reference">out</span> 输出缓冲区，接收菜单条目数组。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_get\_main\_menu\_title

    int tapi_stk_get_main_menu_title(tapi_context context, int slot_id, char** title);

查询主菜单标题。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">title</span> 输出参数，返回标题字符串。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_get\_main\_menu\_icon

    int tapi_stk_get_main_menu_icon(tapi_context context, int slot_id, int* icon);

查询主菜单图标编号。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">icon</span> 输出参数，返回图标编号。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

# Agent 响应处理

以下接口由 Agent 实现使用，用于向 SIM 卡回传 proactive command 的响应。所有接口成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_request\_selection

    int tapi_stk_handle_agent_request_selection(tapi_context context, int slot_id,
                                                char* agent_id, int selection,
                                                tapi_async_function p_handle);

处理 SIM 卡的菜单项选择请求，回传用户选中的条目索引。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">agent\_id</span> Agent 标识字符串。
  - <span class="reference">selection</span> 用户选中的条目索引。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_display\_text

    int tapi_stk_handle_agent_display_text(tapi_context context, int slot_id,
                                           char* agent_id, int result,
                                           tapi_async_function p_handle);

响应 SIM 卡的文本显示请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">result</span> 显示操作结果（用户是否确认等）。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_request\_input

    int tapi_stk_handle_agent_request_input(tapi_context context, int slot_id,
                                            char* agent_id, char* input,
                                            tapi_async_function p_handle);

响应 SIM 卡的字符串输入请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">input</span> 用户输入的字符串。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_request\_digits

    int tapi_stk_handle_agent_request_digits(tapi_context context, int slot_id,
                                             char* agent_id, char* digits,
                                             tapi_async_function p_handle);

响应 SIM 卡的数字序列输入请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">digits</span> 用户输入的数字序列。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_request\_key

    int tapi_stk_handle_agent_request_key(tapi_context context, int slot_id,
                                          char* agent_id, char key,
                                          tapi_async_function p_handle);

响应 SIM 卡的单键输入请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">key</span> 用户输入的按键字符。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_request\_digit

    int tapi_stk_handle_agent_request_digit(tapi_context context, int slot_id,
                                            char* agent_id, char digit,
                                            tapi_async_function p_handle);

响应 SIM 卡的单数字输入请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">digit</span> 用户输入的单个数字字符。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_request\_quick\_digit

    int tapi_stk_handle_agent_request_quick_digit(tapi_context context, int slot_id,
                                                  char* agent_id, char digit,
                                                  tapi_async_function p_handle);

响应 SIM 卡的快速数字输入请求（无需回显）。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">digit</span> 用户输入的单个数字字符。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_request\_confirmation

    int tapi_stk_handle_agent_request_confirmation(tapi_context context, int slot_id,
                                                   char* agent_id, bool confirmed,
                                                   tapi_async_function p_handle);

响应 SIM 卡的确认/取消类请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">confirmed</span> 用户是否确认。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_confirm\_call\_setup

    int tapi_stk_handle_agent_confirm_call_setup(tapi_context context, int slot_id,
                                                 char* agent_id, bool confirmed,
                                                 tapi_async_function p_handle);

响应 SIM 卡发起的 call-setup 确认请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">confirmed</span> 用户是否确认拨出。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_play\_tone

    int tapi_stk_handle_agent_play_tone(tapi_context context, int slot_id,
                                        char* agent_id, int result,
                                        tapi_async_function p_handle);

响应 SIM 卡的播放提示音请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">result</span> 播放结果。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_loop\_tone

    int tapi_stk_handle_agent_loop_tone(tapi_context context, int slot_id,
                                        char* agent_id, int result,
                                        tapi_async_function p_handle);

响应 SIM 卡的循环提示音请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">result</span> 播放结果。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_display\_action\_information

    int tapi_stk_handle_agent_display_action_information(tapi_context context, int slot_id,
                                                         char* agent_id, int result,
                                                         tapi_async_function p_handle);

响应 SIM 卡的动作进度信息显示请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">result</span> 显示操作结果。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_confirm\_launch\_browser

    int tapi_stk_handle_agent_confirm_launch_browser(tapi_context context, int slot_id,
                                                     char* agent_id, bool confirmed,
                                                     tapi_async_function p_handle);

响应 SIM 卡的浏览器启动确认请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">confirmed</span> 用户是否确认启动浏览器。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_display\_action

    int tapi_stk_handle_agent_display_action(tapi_context context, int slot_id,
                                             char* agent_id, int result,
                                             tapi_async_function p_handle);

响应 SIM 卡的动作状态更新请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">result</span> 操作结果。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_stk\_handle\_agent\_confirm\_open\_channel

    int tapi_stk_handle_agent_confirm_open_channel(tapi_context context, int slot_id,
                                                   char* agent_id, bool confirmed,
                                                   tapi_async_function p_handle);

响应 SIM 卡发起的打开数据通道确认请求。

**参数**：

  - <span class="reference">context</span> / <span class="reference">slot\_id</span> / <span class="reference">agent\_id</span> 同上。
  - <span class="reference">confirmed</span> 用户是否确认打开通道。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。
