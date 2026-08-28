# Telephony 电话簿 API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1698&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:37  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/telephony/telephony_phonebook.md) | 简体中文 \]

# Telephony 电话簿 API

SIM 卡电话簿管理接口，支持 ADN（普通电话簿）和 FDN（固定拨号号码）两类条目。

头文件：<span class="reference">\#include \<tapi\_phonebook.h\></span>

# openvela 实现说明

  - **ADN**：普通电话簿（Abbreviated Dialling Numbers），存储在 SIM 卡上的常规号码
  - **FDN**：固定拨号号码（Fixed Dialling Numbers），启用后手机只能拨打 FDN 中的号码，受 PIN2 保护
  - **FDN 操作需要 PIN2**：<span class="reference">insert\_fdn\_entry</span> / <span class="reference">delete\_fdn\_entry</span> / <span class="reference">update\_fdn\_entry</span> 调用时需要传入 PIN2
  - **SIM 卡标识**：所有接口带 <span class="reference">slot\_id</span>
  - **异步回调**：所有操作使用 <span class="reference">tapi\_async\_function</span> 异步返回结果

# ADN 电话簿

## tapi\_phonebook\_load\_adn\_entries

    int tapi_phonebook_load_adn_entries(tapi_context context, int slot_id, int event_id,
                                        tapi_async_function p_handle);

加载 SIM 卡上的 ADN 电话簿条目。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID，用于回调匹配。
  - <span class="reference">p\_handle</span> 异步回调函数，回调时返回 ADN 条目列表。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

# FDN 固定拨号

## tapi\_phonebook\_load\_fdn\_entries

    int tapi_phonebook_load_fdn_entries(tapi_context context, int slot_id, int event_id,
                                        tapi_async_function p_handle);

加载 SIM 卡上的 FDN 条目。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">p\_handle</span> 异步回调函数，回调时返回 FDN 条目列表。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_phonebook\_insert\_fdn\_entry

    int tapi_phonebook_insert_fdn_entry(tapi_context context, int slot_id, int event_id,
                                        char* name, char* number, char* pin2,
                                        tapi_async_function p_handle);

向 FDN 列表插入一条新条目（需要 PIN2 校验）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">name</span> 联系人姓名。
  - <span class="reference">number</span> 电话号码。
  - <span class="reference">pin2</span> SIM 卡 PIN2 码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_phonebook\_update\_fdn\_entry

    int tapi_phonebook_update_fdn_entry(tapi_context context, int slot_id, int event_id,
                                        int fdn_idx, char* new_name, char* new_number,
                                        char* pin2, tapi_async_function p_handle);

更新已有 FDN 条目（需要 PIN2 校验）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">fdn\_idx</span> 要更新的条目索引。
  - <span class="reference">new\_name</span> 新的联系人姓名。
  - <span class="reference">new\_number</span> 新的电话号码。
  - <span class="reference">pin2</span> SIM 卡 PIN2 码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。

## tapi\_phonebook\_delete\_fdn\_entry

    int tapi_phonebook_delete_fdn_entry(tapi_context context, int slot_id, int event_id,
                                        int fdn_idx, char* pin2,
                                        tapi_async_function p_handle);

删除指定 FDN 条目（需要 PIN2 校验）。

**参数**：

  - <span class="reference">context</span> Telephony 上下文句柄。
  - <span class="reference">slot\_id</span> SIM 卡槽 ID。
  - <span class="reference">event\_id</span> 事件 ID。
  - <span class="reference">fdn\_idx</span> 要删除的条目索引。
  - <span class="reference">pin2</span> SIM 卡 PIN2 码。
  - <span class="reference">p\_handle</span> 异步回调函数。

**返回值**：

成功时返回 <span class="reference">0</span>，失败时返回负的错误码。
