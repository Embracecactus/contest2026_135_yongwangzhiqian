# radio/modem 命令

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1561&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:23  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/connection/telephony/telephonytool/radio_modem.md) | 简体中文 \]

# 一、概述

在 openvela 的 NSH 命令行中，可以通过进入 telephonytool 命令工具的 Console，来执行所有与调制解调器（modem）和无线电（radio）管理相关的操作。

# 二、前提条件

确保已打开 <span class="reference">telephonytool</span> 工具。  

    ap> telephonytool

执行上述命令后，进入 <span class="reference">telephonytool</span> 控制台，准备执行相关操作。

# 三、命令

## 1、list-modem

### 命令说明

列出所有可用的 modem。

### 命令格式

    list-modem

### 示例

##### 命令输入

    telephonytool> list-modem

##### 输出信息

    telephonytool> [ 1050.782500] [31] [ DEBUG] [ap] modem_list_query_complete :
    [ 1050.782800] [31] [ DEBUG] [ap] result->status : 0
    [ 1050.783000] [31] [ DEBUG] [ap] modem found with path -> /ril_0

## 2、listen-modem

### 命令说明

设置监听特定的 modem 事件。

### 命令格式

    listen-modem [slot_id] [event_id]

  - slot\_id：设置要监听的 slot，目前仅支持 <span class="reference">0</span>。
  - event\_id：要监听的事件 ID。

### 支持的事件 ID 列表

<span class="reference">event\_id</span> 用于指定要监听的事件。以下是支持的事件分类及其对应的事件 ID。

1.  通用事件 (Generic Indication Message)
    
      - <span class="reference">MSG\_RADIO\_STATE\_CHANGE\_IND</span> = 0
      - <span class="reference">MSG\_PHONE\_STATE\_CHANGE\_IND</span>
      - <span class="reference">MSG\_OEM\_HOOK\_RAW\_IND</span>
      - <span class="reference">MSG\_MODEM\_RESTART\_IND</span>
      - <span class="reference">MSG\_DEVICE\_INFO\_CHANGE\_IND</span>
      - <span class="reference">MSG\_AIRPLANE\_MODE\_CHANGE\_IND</span>

2.  呼叫事件 (Call Indication Message)
    
      - <span class="reference">MSG\_CALL\_STATE\_CHANGE\_IND</span>：呼叫状态变化通知
      - <span class="reference">MSG\_CALL\_RING\_BACK\_TONE\_IND</span>：回铃音通知
      - <span class="reference">MSG\_ECC\_LIST\_CHANGE\_IND</span>：紧急呼叫列表变化通知
      - <span class="reference">MSG\_DEFAULT\_VOICECALL\_SLOT\_CHANGE\_IND</span>：默认语音呼叫 slot 变化通知

3.  网络事件 (Network Indication Message)
    
      - <span class="reference">MSG\_NETWORK\_STATE\_CHANGE\_IND</span>
      - <span class="reference">MSG\_VOICE\_REGISTRATION\_STATE\_CHANGE\_IND</span>
      - <span class="reference">MSG\_CELLINFO\_CHANGE\_IND</span>
      - <span class="reference">MSG\_SIGNAL\_STRENGTH\_CHANGE\_IND</span>
      - <span class="reference">MSG\_NITZ\_STATE\_CHANGE\_IND</span>

4.  数据事件 (Data Indication Message)
    
      - <span class="reference">MSG\_DATA\_ENABLED\_CHANGE\_IND</span>
      - <span class="reference">MSG\_DATA\_REGISTRATION\_STATE\_CHANGE\_IND</span>
      - <span class="reference">MSG\_DATA\_NETWORK\_TYPE\_CHANGE\_IND</span>
      - <span class="reference">MSG\_DATA\_CONNECTION\_STATE\_CHANGE\_IND</span>
      - <span class="reference">MSG\_DEFAULT\_DATA\_SLOT\_CHANGE\_IND</span>

5.  SIM 卡事件 (SIM Indication Message)
    
      - <span class="reference">MSG\_SIM\_STATE\_CHANGE\_IND</span>
      - <span class="reference">MSG\_SIM\_UICC\_APP\_ENABLED\_CHANGE\_IND</span>
      - <span class="reference">MSG\_SIM\_ICCID\_CHANGE\_IND</span>

6.  STK 事件 (STK Indication Message)
    
      - <span class="reference">MSG\_STK\_AGENT\_DISPLAY\_TEXT\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_REQUEST\_DIGIT\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_REQUEST\_KEY\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_REQUEST\_CONFIRMATION\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_REQUEST\_INPUT\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_REQUEST\_DIGITS\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_PLAY\_TONE\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_LOOP\_TONE\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_REQUEST\_SELECTION\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_REQUEST\_QUICK\_DIGIT\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_CONFIRM\_CALL\_SETUP\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_DISPLAY\_ACTION\_INFORMATION\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_CONFIRM\_LAUNCH\_BROWSER\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_DISPLAY\_ACTION\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_CONFIRM\_OPEN\_CHANNEL\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_RELEASE\_IND</span>
      - <span class="reference">MSG\_STK\_AGENT\_CANCEL\_IND</span>

7.  短信事件 (SMS Indication Message)
    
      - <span class="reference">MSG\_INCOMING\_MESSAGE\_IND</span>
      - <span class="reference">MSG\_IMMEDIATE\_MESSAGE\_IND</span>
      - <span class="reference">MSG\_STATUS\_REPORT\_MESSAGE\_IND</span>
      - <span class="reference">MSG\_DEFAULT\_SMS\_SLOT\_CHANGED\_IND</span>

8.  CBS 事件 (CBS Indication Message)
    
      - <span class="reference">MSG\_INCOMING\_CBS\_IND</span>
      - <span class="reference">MSG\_EMERGENCY\_CBS\_IND</span>

9.  SS 事件 (SS Indication Message)
    
      - <span class="reference">MSG\_CALL\_BARRING\_PROPERTY\_CHANGE\_IND</span>
      - <span class="reference">MSG\_USSD\_NOTIFICATION\_RECEIVED\_IND</span>
      - <span class="reference">MSG\_USSD\_REQUEST\_RECEIVED\_IND</span>
      - <span class="reference">MSG\_USSD\_PROPERTY\_CHANGE\_IND</span>

10. IMS 事件 (IMS Indication Message)
    
      - <span class="reference">MSG\_IMS\_REGISTRATION\_MESSAGE\_IND</span>

11. Modem 状态变化事件 (Modem State Change Message)
    
      - <span class="reference">MSG\_MODEM\_STATE\_CHANGE\_IND</span>

12. 其他事件
    
      - <span class="reference">MSG\_DATA\_LOGING\_IND</span>
      - <span class="reference">MSG\_MODEM\_ECC\_LIST\_CHANGE\_IND</span> = 61

### 示例

##### 命令输入

    telephonytool> listen-modem 0 0

##### 输出信息

    telephonytool> listen-modem 0 0
    [ 1632.199400] [35] [ DEBUG] [ap] start to watch radio event : 0 , return watch_id : 75

## 3、unlisten-modem

### 命令说明

取消监听指定的 modem 事件。

### 命令格式

    unlisten-modem [watch_id]

  - watch\_id：监听 ID，来源于 <span class="reference">listen-modem</span> 命令的返回值。

### 示例

##### 命令输入

    telephonytool> unlisten-modem 75

##### 输出信息

以下是执行 <span class="reference">unlisten-modem</span> 命令的完整示例：  

    telephonytool> listen-modem 0 0
    [ 1632.199400] [35] [ DEBUG] [ap] start to watch radio event : 0 , return watch_id : 75
    telephonytool> unlisten-modem 75
    [ 2050.331400] [35] [ DEBUG] [ap] stop to watch radio event with watch_id : 75 with return value : 0
    telephonytool>

## 4、get-radio-cap

### 命令说明

查询 modem 的功能支持情况。

### 命令格式

    get-radio-cap [feature_type]

  - feature\_type：指定要查询的功能类型。
      - <span class="reference">0</span>：语音（voice）
      - <span class="reference">1</span>：数据（data）
      - <span class="reference">2</span>：短信（sms）
      - <span class="reference">3</span>：IMS（IP Multimedia Subsystem）

### 示例

##### 命令输入

    telephonytool> get-radio-cap 0

##### 输出信息

以下是执行 <span class="reference">get-radio-cap</span> 命令的完整示例：  

    telephonytool> get-radio-cap 0
    [ 2145.490400] [35] [ DEBUG] [ap] radio feature type : 0 is supported ? 1
    telephonytool> get-radio-cap 1
    [ 2164.658700] [35] [ DEBUG] [ap] radio feature type : 1 is supported ? 1

## 5、set-radio-power

### 命令说明

设置指定 slot 的无线电（radio）电源状态，对应飞行模式的关闭和开启。

### 命令格式

    set-radio-power [slot_id][state]

  - slot\_id：指定要设置的 slot，目前仅支持 <span class="reference">0</span>。
  - state：无线电电源状态：
      - <span class="reference">0</span>：关闭无线电（radio off）
      - <span class="reference">1</span>：开启无线电（radio on）

### 示例

##### 命令输入

    telephonytool>  set-radio-power 0 0

##### 输出信息

以下是执行 <span class="reference">set-radio-power</span> 命令的完整示例：  

    telephonytool> set-radio-power 0 0
    [ 2322.660700] [35] [ DEBUG] [ap] telephonytool_cmd_set_radio_power, slotId : 0 target_state: 0
    telephonytool> set-radio-power 0 1
    [ 2324.918200] [35] [ DEBUG] [ap] telephonytool_cmd_set_radio_power, slotId : 0 target_state: 1

## 6、get-radio-power

### 命令说明

获取指定 slot 的无线电（radio）电源状态。

### 命令格式

    get-radio-power [slot_id]

### 示例

##### 命令输入

    telephonytool> get-radio-power 0

##### 输出信息

以下是执行 <span class="reference">get-radio-power</span> 命令的完整示例：  

    telephonytool> get-radio-power 0
    [ 2480.612100] [35] [ DEBUG] [ap] telephonytool_cmd_get_radio_power, slotId : 0 value : 1

## 7、set-rat-mode

### 命令说明

设置指定 slot 的无线接入技术（RAT，Radio Access Technology）模式。

### 命令格式

    set-rat-mode [slot_id] [mode]

  - slot\_id：指定要设置的 slot，目前仅支持 <span class="reference">0</span>。
  - mode：目标网络模式，支持以下值：
      - <span class="reference">0</span>：UMTS
      - <span class="reference">1</span>：GSM only
      - <span class="reference">2</span>：WCDMA only
      - <span class="reference">9</span>：LTE/GSM/WCDMA
      - <span class="reference">11</span>：LTE only
      - <span class="reference">12</span>：LTE/WCDMA

### 示例

##### 命令输入

    telephonytool> set-rat-mode 0 9

##### 输出信息

    telephonytool> set-rat-mode 0 11
    [   48.155000] [35] [ DEBUG] [ap] telephonytool_cmd_set_rat_mode, slotId : 0 target_state: 11
    [   53.549600] [21] [  INFO] [ap] [0,0059]> RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE (11)
    [   54.717700] [21] [  INFO] [ap] [0,0059]< RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE

## 8、get-rat-mode

### 命令说明

获取指定 slot 的无线接入技术（RAT，Radio Access Technology）模式。

### 命令格式

    get-rat-mode [slot_id]

  - slot\_id：指定要查询的 slot，目前仅支持 <span class="reference">0</span>。

### 示例

##### 命令输入

    telephonytool> get-rat-mode 0

##### 输出信息

    telephonytool> get-rat-mode 0
    [  184.550000] [35] [ DEBUG] [ap] telephonytool_cmd_get_rat_mode, slotId : 0 value :11

## 9、get-imei

### 命令说明

获取设备的 IMEI（国际移动设备识别码）信息。

### 命令格式

    set-rat-mode [slot_id]
    slot_id:设置要监听的slot,当前只支持0

  - slot\_id：指定要查询的 slot，目前仅支持 <span class="reference">0</span>。

### 示例

##### 命令输入

    telephonytool> get-imei 0

##### 输出信息

以下是执行 <span class="reference">get-imei</span> 命令的完整示例：  

    telephonytool> get-imei 0
    [  236.301900] [35] [ DEBUG] [ap] telephonytool_cmd_get_imei, slotId : 0 imei : 8674000******7199

## 10、get-imeisv

### 命令说明

获取设备的 IMEISV（国际移动设备识别码软件版本）信息。

### 命令格式

    get-imeisv [slot_id]

  - slot\_id：指定要查询的 slot，目前仅支持 <span class="reference">0</span>。

### 示例

##### 命令输入

    telephonytool> get-imeisv 0

##### 输出信息

    telephonytool> get-imeisv 0  
    [  401.567800] [35] [ DEBUG] [ap] telephonytool_cmd_get_imeisv, slotId : 0 imeisv : 8674000******7901

## 11、get-phone-state

### 命令说明

获取设备的电话状态信息。

### 命令格式

    get-phone-state [slot_id]

  - slot\_id：指定要查询的 slot，目前仅支持 <span class="reference">0</span>。

### 示例

##### 命令输入

    telephonytool> get-phone-state 0

##### 输出信息

    telephonytool> get-phone-state 0
    [ 9427.739300] [35] [ DEBUG] [ap] telephonytool_cmd_get_phone_state, slotId : 0 state : 0

## 12、send-modem-power

### 命令说明

控制 Modem 模块的开关状态。

### 命令格式

    send-modem-power[slot_id] [on]

  - slot\_id：指定要操作的 slot，目前仅支持 <span class="reference">0</span>。
  - on：设置 Modem 的目标状态：
      - <span class="reference">0</span>：关闭 Modem
      - <span class="reference">1</span>：开启 Modem

### 示例

##### 命令输入

    telephonytool> send-modem-power 0 0

##### 输出信息

以下是执行 <span class="reference">send-modem-power</span> 命令的完整示例：  

    telephonytool> send-modem-power 0 0
    [ 9461.379300] [35] [ DEBUG] [ap] telephonytool_cmd_send_modem_power, slotId : 0 target_state: 0
    telephonytool> [ 9461.415300] [21] [  INFO] [ap] modem_change_state, old state: 2, new state: 0
    [ 9461.415900] [21] [  INFO] [ap] flush_atoms
    [ 9461.421100] [21] [  INFO] [ap] free_contexts

## 13、get-radio-state

### 命令说明

获取设备的无线电（Radio）状态信息。

### 命令格式

    get-radio-state [slot_id]

  - slot\_id：指定要查询的 slot，目前仅支持 <span class="reference">0</span>。

### 示例

##### 命令输入

    telephonytool> get-radio-state 0

##### 输出信息

以下是执行 <span class="reference">get-radio-state</span> 命令的完整示例：  

    telephonytool> get-radio-state 0
    [ 9486.517900] [35] [ DEBUG] [ap] telephonytool_cmd_get_radio_state, slotId : 0 state : 1

## 14、get-modem-revision

### 命令说明

获取 Modem 的基带版本信息。

### 命令格式

    get-modem-revision [slot_id]

  - slot\_id：指定要查询的 slot，目前仅支持 <span class="reference">0</span>。

### 示例

##### 命令输入

    telephonytool> get-modem-revision 0

##### 输出信息

以下是执行 <span class="reference">get-modem-revision</span> 命令的完整示例：  

    telephonytool> get-modem-revision 0
    [ 9505.417900] [35] [ DEBUG] [ap] telephonytool_cmd_get_modem_revision, slotId : 0 value : 1.0.*.*

## 15、get-msisdn

### 命令说明

获取本地电话号码信息

### 命令格式

    get-msisdn [slot_id]

  - slot\_id：指定要查询的 slot，目前仅支持 <span class="reference">0</span>。

### 示例

##### 命令输入

    telephonytool> get-msisdn 0

##### 输出信息

以下是执行 <span class="reference">get-msisdn</span> 命令的完整示例：  

    telephonytool> get-msisdn 0
    [ 9529.024200] [35] [  INFO] [ap] get phone number from UICC.
    [ 9529.025200] [35] [ DEBUG] [ap] telephonytool_cmd_get_phone_number, slotId : 0  number : +1555******67

## 16、get-modem-activity-info

### 命令说明

获取 Modem 的活动信息。

### 命令格式

    get-modem-activity-info [slot_id]

  - slot\_id：指定要查询的 slot，目前仅支持 <span class="reference">0</span>。

### 示例

##### 命令输入

    telephonytool> get-modem-activity-info 0

##### 输出信息

以下是执行 <span class="reference">get-modem-activity-info</span> 命令的完整示例：  

    telephonytool> get-modem-activity-info 0
    [ 9743.317300] [35] [ DEBUG] [ap] telephonytool_cmd_get_modem_activity_info, slotId : 0

## 17、enable-modem

### 命令说明

启用或关闭 Modem。

### 命令格式

    enable-modem[slot_id] [state]

  - slot\_id：指定要操作的 slot，目前仅支持 <span class="reference">0</span>。
  - state：设置 Modem 的目标状态：
      - <span class="reference">0</span>：关闭 Modem
      - <span class="reference">1</span>：开启 Modem

### 示例

##### 命令输入

    telephonytool> enable-modem 0 1

##### 输出信息

以下是执行 <span class="reference">enable-modem</span> 命令的完整示例：  

    telephonytool> enable-modem 0 1
    [   15.700700] [28] [ DEBUG] [ap] telephonytool_cmd_enable_modem, slotId : 0 target_state: 1

## 18、get-modem-status

### 命令说明

获取 Modem 的状态信息。

### 命令格式

    get-modem-status [slot_id]

  - slot\_id：指定要查询的 slot，目前仅支持 <span class="reference">0</span>。

### 示例

##### 命令输入

    telephonytool> get-modem-status 0

##### 输出信息

以下是执行 <span class="reference">get-modem-status</span> 命令的完整示例：  

    telephonytool> get-modem-status 0
    [  782.186200] [28] [ DEBUG] [ap] telephonytool_cmd_get_modem_status, slotId : 0

## 19、oem-req-raw

### 命令说明

直接发送格式化的 16 进制字符给 Modem，用于 eSIM 文件下载、eSIM 文件内容读取等操作。

### 命令格式

    oem-req-raw [slot_id][request_data][data_length]

  - slot\_id：指定要操作的 slot，目前仅支持 <span class="reference">0</span>。
  - request\_data：16 进制字符串，表示要发送的原始数据。
  - data\_length：<span class="reference">request\_data</span> 的字节数。

### 示例

##### 命令输入

    telephonytool> oem-req-raw 0 01A0B023 4

##### 输出信息

    telephonytool> oem-req-raw 0 01A0B023 4
    [  854.969700] [28] [ DEBUG] [ap] telephonytool_cmd_oem_ril_req_raw, slot_id: 0 oem_req: 01A0B023 length: 4

## 20、oem-req-strings

### 命令说明

直接发送字符串到 Modem，例如 AT 命令。

### 命令格式

    oem-req-strings [slot_id][request_data][data_length]

  - slot\_id：指定要操作的 slot，目前仅支持 <span class="reference">0</span>。
  - request\_data：要发送的字符串，例如 AT 命令。
  - data\_length：<span class="reference">request\_data</span> 的字节数。

### 示例

##### 命令输入

    telephonytool> oem-req-strings 0 AT+CPIN? 1

##### 输出信息

    telephonytool> oem-req-strings 0 AT+CPIN? 1
    [  870.751200] [28] [ DEBUG] [ap] telephonytool_cmd_oem_ril_req_strings, slot_id: 0 length: 1

## 21、send-command

### 命令说明

直接发送内部的 RIL（Radio Interface Layer）消息。

### 命令格式

    send-command [slot_id][atom id][ril request id]

  - slot\_id：指定要操作的 slot，目前仅支持 <span class="reference">0</span>。
  - atom\_id：Atom ID 信息，用于标识目标模块。
  - ril\_request\_id：内部的 Request ID 信息，用于指定请求类型。

### 示例

##### 命令输入

    telephonytool> send-command 0 16 57

##### 输出信息

    telehonytool>
    telephonytool> send-command 0 16 57
    [  882.733000] [28] [ DEBUG] [ap] telephonytool_cmd_send_command, slot_id: 0 atom: 16  command: 57

## 22、send-screen-state

### 命令说明

设置屏幕开关状态信息给modem

### 命令格式

    send-screen-state [slot_id][][screen_state]

  - slot\_id：指定要操作的 slot，目前仅支持 <span class="reference">0</span>。
  - screen\_state：屏幕状态：
      - <span class="reference">0</span>：屏幕关闭状态
      - <span class="reference">1</span>：屏幕开启状态

### 示例

##### 命令输入

    telephonytool> send-screen-state 0 1

##### 输出信息

    telephonytool>
    telephonytool> send-screen-state 0 1
    telephonytool> [  927.719000] [21] [  INFO] [ap] Set fast_dormancy: 1
