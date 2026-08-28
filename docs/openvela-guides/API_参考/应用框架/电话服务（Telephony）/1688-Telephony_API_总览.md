# Telephony API 总览

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1688&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:31  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/telephony/index.md) | 简体中文 \]

# Telephony API

Telephony 提供蜂窝通信能力，<span class="reference">framework/telephony</span> 是 openvela 蜂窝通信对应用层提供的接口层，又称为 TAPI（Telephony API）。封装的接口涵盖了蜂窝通信业务：网络服务、通话、短信、数据、SIM 双卡和 modem 配置管理等。

TAPI 独立于 openvela telephony core stack，内部逻辑基于 DBUS LIB 对 Core Stack 进行业务逻辑封装，屏蔽掉 D-Bus 的复杂操作，对外以标准 C 的方式提供标准化统一的 Telephony API 接口定义，方便 openvela 应用层 APP 的使用，让 openvela APP 实现 openvela 系统版本间复用。

# openvela 实现说明

  - **架构**：TAPI 基于 D-Bus 对 Telephony Core Stack（oFono）进行封装，以标准 C 接口对外提供
  - **SIM 卡标识**：通过 <span class="reference">slot\_id</span> 参数区分不同 SIM 卡槽
  - **异步模型**：大部分操作通过回调函数异步返回结果

# 模块代码介绍

| 模块             | 源码                                                 | API 文档                                                                   | 说明                       |
| :------------- | :------------------------------------------------- | :----------------------------------------------------------------------- | :----------------------- |
| 对外统一头文件        | <span class="reference">tapi.h</span>              | [公共工具](https://doc.openvela.com/document?id=1555&version=dev-ai-contest-2026&language=cn)        | 公共类型定义、字符串/枚举转换 utils    |
| Radio 接口       | <span class="reference">tapi\_manager.c/h</span>   | [管理](https://doc.openvela.com/document?id=1690&version=dev-ai-contest-2026&language=cn)          | Telephony 初始化、状态查询、事件注册  |
| Call 接口        | <span class="reference">tapi\_call.c/h</span>      | [通话](https://doc.openvela.com/document?id=1691&version=dev-ai-contest-2026&language=cn)          | 语音通话控制                   |
| 补充业务           | <span class="reference">tapi\_ss.c/h</span>        | [补充业务 SS](https://doc.openvela.com/document?id=1692&version=dev-ai-contest-2026&language=cn)     | 呼叫转移/呼叫限制/呼叫等待/CLIR/USSD |
| 简化电话服务         | <span class="reference">tapi\_phone.c/h</span>     | [简化电话服务](https://doc.openvela.com/document?id=1693&version=dev-ai-contest-2026&language=cn)      | 轻量客户端封装                  |
| Network 接口     | <span class="reference">tapi\_network.c/h</span>   | [网络](https://doc.openvela.com/document?id=1694&version=dev-ai-contest-2026&language=cn)          | 网络注册、信号、运营商              |
| Data 接口        | <span class="reference">tapi\_data.c/h</span>      | [数据](https://doc.openvela.com/document?id=1695&version=dev-ai-contest-2026&language=cn)          | 蜂窝数据连接                   |
| SIM 接口         | <span class="reference">tapi\_sim.c/h</span>       | [SIM 卡](https://doc.openvela.com/document?id=1696&version=dev-ai-contest-2026&language=cn)       | SIM 卡管理                  |
| SIM Toolkit    | <span class="reference">tapi\_stk.c/h</span>       | [SIM Toolkit](https://doc.openvela.com/document?id=1697&version=dev-ai-contest-2026&language=cn) | STK Agent 与 SIM 卡主动命令    |
| 电话簿            | <span class="reference">tapi\_phonebook.c/h</span> | [电话簿](https://doc.openvela.com/document?id=1698&version=dev-ai-contest-2026&language=cn)         | ADN/FDN 电话簿管理            |
| SMS 接口         | <span class="reference">tapi\_sms.c/h</span>       | [短信](https://doc.openvela.com/document?id=1699&version=dev-ai-contest-2026&language=cn)          | 短信收发                     |
| Cell Broadcast | <span class="reference">tapi\_cbs.c/h</span>       | [小区广播 CBS](https://doc.openvela.com/document?id=1700&version=dev-ai-contest-2026&language=cn)    | 小区广播消息                   |
| IMS 接口         | <span class="reference">tapi\_ims.c/h</span>       | [IMS](https://doc.openvela.com/document?id=1701&version=dev-ai-contest-2026&language=cn)         | VoLTE/VoWiFi             |

# TAPI 配置

完整的 Telephony 业务涉及模块众多，需要所有模块开启完整使用 Telephony 业务。

**DBUS 配置**  

    CONFIG_DBUS_DAEMON=y
    CONFIG_DBUS_MONITOR=y
    CONFIG_DBUS_SEND=y
    CONFIG_LIB_DBUS=y

**GLIB 配置**  

    CONFIG_LIB_GLIB=y

**OFONO 配置**  

    CONFIG_LIB_ELL=y
    CONFIG_OFONO=y
    CONFIG_OFONO_RILMODEM=y  # modem 类型选择，支持 rild 的选择 rilmodem
    CONFIG_OFONO_ATMODEM=y   # 支持串口、USB 的选择 atmodem

**GDBUS 配置**  

    CONFIG_LIB_DBUS=y

**Telephony API 配置**  

    CONFIG_TELEPHONY=y
    CONFIG_TELEPHONY_TOOL=y  # debug 工具，可选

# TAPI 工作使用模型

![TAPIWork](https://vela-open-doc.cnbj1.mi-fds.com/vela-open-doc/1781005809360_TapiWork.png)

# TAPI 函数使用举例

## 获取 TAPI 工作上下文

先声明一个 callback 函数：  

    static void on_tapi_client_ready(const char* client_name, void* user_data)
    {
        if (client_name != NULL)
            syslog(LOG_DEBUG, "tapi is ready for %s\n", client_name);
        ...
    }

再调用 <span class="reference">tapi\_open</span> 函数获取上下文。获取成功需要 oFono、D-Bus 等服务启动成功，当 ready 后会调用 callback 函数。  

    tapi_context context;
    char* dbus_name = "vela.telephony.tool";
    context = tapi_open(dbus_name, on_tapi_client_ready, NULL);

## 释放 TAPI 工作上下文

    tapi_close(context);

## 查询当前的 radio power 状态

    int slot_id = 0;
    bool value = false;
    tapi_get_radio_power(context, slot_id, &value);
