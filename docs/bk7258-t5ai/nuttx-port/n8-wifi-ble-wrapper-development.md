# Stage N8 — WiFi + BLE Wrapper 适配：开发指南

> **范围**：定义 BK7258 NuttX 适配中 WiFi（802.11ax）和 BLE（5.4）协议栈通过 SDK Wrapper
> 模式接入的完整开发指南——架构设计、API 接口规范、环境配置、部署验证。
> **状态**：框架设计；WiFi Stage 1（扫描验证）已落地，BLE 尚未开始。
> **决策**：WiFi 和 BLE 均使用 SDK 预编译库 + 薄 wrapper 转发，不做寄存器级实现。

> **路径约定**（与 n6-sdk-integration-framework.md 一致）：
> - `$CONTEST` = 本团队 overlay 根（含 `board/bk7258_t5ai/`）
> - `$BK_AVDK` = Beken 官方 SDK（bk_avdk_smp，`cp/` 与 `ap/` 两套独立编译路径）
> - `$SDK_INC` = `$CONTEST/board/bk7258_t5ai/bk_idk/armino_as_lib/cp/include`
> - `$SDK_LIBS` = `$CONTEST/board/bk7258_t5ai/bk_idk/armino_as_lib/cp/libs`

---

## 1. 项目概述

### 1.1 背景

BK7258 NuttX 移植已完成 Stage N1–N7（启动链、NSH console、procfs、时钟/DVFS、
Flash/文件系统、SDK 集成框架、AP 单核 bringup）。Stage N8 的目标是将芯片的两大无线
协议栈——WiFi 6（802.11ax）和 BLE 5.4——通过 SDK Wrapper 模式接入 NuttX。

WiFi 和 BLE 协议栈的复杂度（各百万行级代码、射频认证要求）决定了不能用直接寄存器
方式实现，必须使用 Beken SDK 提供的预编译库。wrapper 层负责将 SDK 的 FreeRTOS 风格
API 桥接到 NuttX 的 lower-half 接口。

### 1.2 芯片无线规格

| 特性 | WiFi | BLE |
|------|------|-----|
| 标准 | IEEE 802.11b/g/n/ax 1x1 | BLE 5.4 |
| 频段 | 2.4 GHz，20/40 MHz 带宽 | 2.4 GHz |
| 关键特性 | DL MU-MIMO、OFDMA、TWT、LDPC | LE Audio、AoA/AoD、2M PHY、广告扩展、长距离 |
| 安全 | WPA/WPA2/WPA3-Personal | LE Secure Connections |
| 工作模式 | STA、SoftAP、STA+SoftAP 共存 | Central、Peripheral、Observer、Broadcaster |
| 发射功率 | +20 dBm | — |
| 接收灵敏度 | -98 dBm | — |
| 共存 | 集成 BT/Wi-Fi PTA（硬件仲裁） | 同左 |

### 1.3 目标

- **WiFi**：STA 连接 + 扫描 + 数据路径（NuttX netdev 桥接）+ SoftAP
- **BLE**：GAP 扫描/广播 + GATT Server + SMP 配对 + 连接管理
- **共存**：WiFi + BLE 同时运行的稳定性验证

### 1.4 当前完成度

| 模块 | 状态 | 说明 |
|------|------|------|
| OS 适配层 | ✅ 完成 | `bk7258_os_adapt.c`（1706 行），FreeRTOS→NuttX 全桥接 |
| SDK IRQ 桥 | ✅ 完成 | `bk7258_sdk_irq.c`（300 行），中断路由 |
| SDK Stubs | ✅ 完成 | `bk7258_sdk_stubs.c`（184 行），空实现 |
| WiFi 控制面 | ✅ Stage 1 | `bk7258_wifi.c`（307 行），扫描验证可用 |
| WiFi 数据路径 | ❌ 未开始 | `ethernetif_input()` 等 12 个函数为空 Stub |
| BLE Wrapper | ❌ 未开始 | 无 wrapper 文件，仅有 stub |
| WiFi/BT 共存 | ❌ 未开始 | `libble_wifi_exchange.a` 已排除 |

### 1.5 固件大小数据

| 构建配置 | 大小 | 说明 |
|---------|------|------|
| 当前 NuttX（无 WiFi/BT） | 233 KB | `all-app.bin` = 238,952 B |
| SDK FreeRTOS 参考（WiFi+BLE） | 978 KB | `app.bin` = 1,001,112 B |
| SDK 协议栈增量 | ~745 KB | WiFi 库 + BLE 库链接后实际占用 |
| Wrapper 层开销 | ~15 KB | OS 适配 + Stubs + IRQ 桥 + Wrapper glue |

16 MB Flash 下 978 KB 仅占 6%，有充足余量。瓶颈在 640 KB SRAM 的运行时分配。

---

## 2. 架构设计

### 2.1 分层架构

```
┌──────────────────────────────────────────────────────────────┐
│ NuttX 应用 / NSH (wscan, blescan, bleadv, ...)               │
├──────────────────────────────────────────────────────────────┤
│ NuttX upper-half                                             │
│   netdev (WiFi)          BLE stack (NuttX native or SDK)    │
├──────────────────────────────────────────────────────────────┤
│ Wrapper Layer（chip/cp/bk7258_wifi.c, chip/cp/bk7258_ble.c）  │
│   WiFi: bk_wifi_* / bk_event_*    BLE: bk_ble_* / bk_dm_*  │
├──────────────────────────────────────────────────────────────┤
│ Beken SDK 预编译库 ($SDK_LIBS/*.a)                           │
│   libbk_wifi.a  libwifi.a  libwpa_supplicant-2.10.a         │
│   libbluetooth_controller_ble.a  libbluetooth_host_ble.a    │
│   libbk_bluetooth.a  libbk_coex.a  libcom_phy.a             │
├──────────────────────────────────────────────────────────────┤
│ OS 适配层（chip/common/bk7258_os_adapt.c）                          │
│   rtos_create_thread → kthread_create                        │
│   rtos_init_semaphore → nxsem_init                           │
│   rtos_init_queue → file_mq_open                             │
│   rtos_init_timer → wd_start                                 │
│   os_malloc → kmm_malloc                                     │
├──────────────────────────────────────────────────────────────┤
│ SDK IRQ 桥（chip/cp/bk7258_sdk_irq.c）                          │
│   bk_int_isr_register → irq_attach → NuttX NVIC             │
├──────────────────────────────────────────────────────────────┤
│ NuttX Kernel                                                 │
└──────────────────────────────────────────────────────────────┘
```

### 2.2 Wrapper 五条核心规范

所有 WiFi/BLE wrapper 模块遵循与 WDT/Serial/GPIO 相同的设计规范：

1. **零寄存器操作** — 不包含任何 `#define` 寄存器地址宏，不调用 `putreg32/getreg32`
2. **SDK 头文件引用** — 引用 `$SDK_INC/modules/wifi.h`、`$SDK_INC/components/bluetooth/*.h`
3. **私有状态结构体** — 保存 SDK 句柄和初始化守卫，不暴露内部状态
4. **输入校验在 wrapper 做** — 参数检查后再调用 SDK API
5. **Kconfig 条件编译** — 受 `CONFIG_BK7258_WIFI` / `CONFIG_BK7258_BLE` 控制

### 2.3 WiFi 事件链路

```
WiFi Hardware
  → SDK Internal ISR (libwifi.a)
    → bk_event_post(EVENT_MOD_WIFI, event_id, data)
      → bk_event_register_cb 注册的回调
        ├─ EVENT_WIFI_SCAN_DONE → sem_post(scan_done_sem)
        ├─ EVENT_WIFI_STA_CONNECTED → netdev_carrier_on()
        ├─ EVENT_WIFI_STA_DISCONNECTED → netdev_carrier_off()
        └─ EVENT_WIFI_AP_STA_CONNECTED → 更新 STA 列表
```

### 2.4 BLE 事件链路

```
BLE Controller (libbluetooth_controller_ble.a)
  → HCI Event → BLE Host (libbluetooth_host_ble.a)
    → bk_ble_set_event_callback 注册的回调
      ├─ BLE_5_CREATE_DB → GATT DB 就绪
      ├─ BLE_5_INIT_CONN → 连接建立
      ├─ BLE_5_DISCONN → 连接断开
      └─ BLE_5_RECV_WRITE_REQ → GATT 写请求
    → bk_ble_gap_register_callback 注册的回调
      ├─ BK_BLE_GAP_SCAN_RES_EVT → 扫描结果
      ├─ BK_BLE_GAP_CONN_STATE_EVT → 连接状态变化
      └─ BK_BLE_GAP_AUTH_CMPL_EVT → 配对完成
```

### 2.5 WiFi 数据路径（Stage 2 目标）

```
RX: WiFi Firmware → SDK → ethernetif_input(iface, buf)
                           → netdev_input(&buf) → NuttX TCP/IP

TX: NuttX TCP/IP → netdev TX queue → wrapper txavail callback
                                      → bk_wifi_sta_send_8023_raw()
                                      → SDK → WiFi Firmware
```

当前 Stage 1 中 `ethernetif_input()` 为空 Stub，帧被丢弃。

---

## 3. WiFi API 接口规范

### 3.1 初始化

头文件：`$SDK_INC/modules/wifi.h`、`$SDK_INC/modules/wifi_types.h`

| SDK 函数 | 功能 | Wrapper 映射 |
|---------|------|-------------|
| `bk_wifi_init(const wifi_init_config_t *config)` | 初始化 WiFi 驱动 | `bk7258_wifi_init_once()`，静态守卫 `g_wifi_inited` |
| `bk_wifi_deinit(void)` | 反初始化（SDK 未实现） | 保留接口 |

初始化配置宏：

```c
#define WIFI_DEFAULT_INIT_CONFIG() {  \
  .features = 0,                      \
  .os_funcs = &g_wifi_os_funcs,       \
  .os_val = &g_wifi_os_variable,      \
}
```

`g_wifi_os_funcs` 和 `g_wifi_os_variable` 由 `libbk_wifi.a` 内部导出，
需通过 `extern unsigned char[]` 不透明声明。

### 3.2 扫描

| SDK 函数 | 功能 | 说明 |
|---------|------|------|
| `bk_wifi_scan_start(const wifi_scan_config_t *config)` | 启动扫描 | `NULL` = 全信道主动扫描 |
| `bk_wifi_scan_stop(void)` | 停止扫描 | |
| `bk_wifi_scan_get_result(wifi_scan_result_t *result)` | 获取结果 | 内部分配 `result->aps` |
| `bk_wifi_scan_free_result(wifi_scan_result_t *result)` | 释放结果 | 必须配对调用 |
| `bk_wifi_scan_dump_result(const wifi_scan_result_t *result)` | 打印结果 | 调试用 |

扫描完成通过 `bk_event_register_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE, cb, arg)`
注册回调通知。

### 3.3 STA 模式

| SDK 函数 | 功能 | 说明 |
|---------|------|------|
| `bk_wifi_sta_set_config(const wifi_sta_config_t *config)` | 配置 STA | 在 `bk_wifi_sta_start()` 前调用 |
| `bk_wifi_sta_get_config(wifi_sta_config_t *config)` | 获取 STA 配置 | |
| `bk_wifi_sta_start(void)` | 启动 STA 并自动连接 | 需先 `set_config` |
| `bk_wifi_sta_stop(void)` | 停止 STA | |
| `bk_wifi_sta_connect(void)` | 手动连接 | STA 已启动后调用 |
| `bk_wifi_sta_disconnect(void)` | 断开连接 | |
| `bk_wifi_sta_get_link_status(wifi_link_status_t *status)` | 获取链路状态 | 含 RSSI、AID、连接状态 |
| `bk_wifi_sta_get_linkstate_with_reason(wifi_linkstate_reason_t *info)` | 获取状态+原因码 | |

STA 配置结构体关键字段：

```c
wifi_sta_config_t config = {
  .ssid     = "your_ssid",        // 最长 32 字符
  .password = "your_password",    // 最长 64 字符
  .security = WIFI_SECURITY_AUTO, // 或 WIFI_SECURITY_WPA3_SAE
  .channel  = 0,                  // 0 = 自动
};
```

### 3.4 SoftAP 模式

| SDK 函数 | 功能 | 说明 |
|---------|------|------|
| `bk_wifi_ap_set_config(const wifi_ap_config_t *config)` | 配置 AP | |
| `bk_wifi_ap_get_config(wifi_ap_config_t *config)` | 获取 AP 配置 | |
| `bk_wifi_ap_start(void)` | 启动 AP | 可与 STA 共存 |
| `bk_wifi_ap_stop(void)` | 停止 AP | |
| `bk_wifi_ap_get_sta_list(wlan_ap_stas_t *stas)` | 获取已连接 STA 列表 | |
| `bk_wifi_ap_get_mac(uint8_t *mac)` | 获取 AP MAC 地址 | base MAC byte[5] += 1 |

### 3.5 事件系统

| SDK 函数 | 功能 |
|---------|------|
| `bk_event_init(void)` | 初始化事件子系统 |
| `bk_event_register_cb(event_module_t mod, int event_id, event_cb_t cb, void *arg)` | 注册事件回调 |
| `bk_event_unregister_cb(event_module_t mod, int event_id, event_cb_t cb)` | 注销回调 |

WiFi 事件模块为 `EVENT_MOD_WIFI`，关键事件 ID：

| 事件 ID | 说明 |
|---------|------|
| `EVENT_WIFI_SCAN_DONE` | 扫描完成 |
| `EVENT_WIFI_STA_CONNECTED` | STA 已连接 |
| `EVENT_WIFI_STA_DISCONNECTED` | STA 断开 |
| `EVENT_WIFI_AP_STA_CONNECTED` | 有 STA 连入 AP |
| `EVENT_WIFI_AP_STA_DISCONNECTED` | STA 从 AP 断开 |

### 3.6 数据路径（Stage 2）

以下函数由 SDK 内部调用，wrapper 需提供实现（当前为 Stub）：

| 函数 | 方向 | 当前状态 | Stage 2 目标 |
|------|------|---------|-------------|
| `ethernetif_input(int iface, void *buf)` | RX | 丢弃帧 | `netdev_input()` 桥接 |
| `net_wlan_add_netif(uint8_t *mac)` | 初始化 | 返回 0 | 注册 NuttX netdev |
| `net_wlan_remove_netif(uint8_t *mac)` | 清理 | 返回 0 | 注销 netdev |
| `sta_ip_start(void)` | IP 事件 | 打印日志 | DHCP 启动 |
| `sta_ip_down(void)` | IP 事件 | 打印日志 | IP 释放 |
| `sta_ip_is_start(void)` | 查询 | 返回 0 | 返回真实状态 |
| `net_get_sta_handle(void)` | 查询 | 返回 NULL | 返回 netdev 句柄 |

### 3.7 MAC 地址服务

由 wrapper 直接实现（替代被排除的 `libbk_system.a`）：

| 函数 | 功能 | 派生规则 |
|------|------|---------|
| `bk_set_base_mac(const uint8_t *mac)` | 设置 base MAC | 存储到 `g_base_mac[6]` |
| `bk_get_mac(uint8_t *mac, mac_type_t type)` | 获取 MAC | STA=base, AP=base+1, BT=base+2 |

---

## 4. BLE API 接口规范

### 4.1 蓝牙栈初始化（DM 层）

头文件：`$SDK_INC/components/bluetooth/bk_dm_bluetooth.h`

| SDK 函数 | 返回类型 | 功能 |
|---------|---------|------|
| `bk_bluetooth_init(void)` | `bt_err_t` | 初始化蓝牙栈（Controller + Host） |
| `bk_bluetooth_deinit(void)` | `bt_err_t` | 反初始化 |
| `bk_bluetooth_get_status(void)` | `bk_bluetooth_status_t` | 查询蓝牙状态 |
| `bk_bluetooth_get_address(uint8_t *addr)` | `bt_err_t` | 获取 BD_ADDR |

### 4.2 BLE 广播（Peripheral 角色）

头文件：`$SDK_INC/components/bluetooth/bk_dm_ble.h`

| SDK 函数 | 功能 | 回调 |
|---------|------|------|
| `bk_ble_set_advertising_params(ble_adv_parameter_t *param, ble_cmd_cb_t cb)` | 设置广播参数 | `BLE_CREATE_ADV` |
| `bk_ble_set_advertising_data(uint8_t len, uint8_t *data, ble_cmd_cb_t cb)` | 设置广播数据 | `BLE_SET_ADV_DATA` |
| `bk_ble_set_scan_response_data(uint8_t len, uint8_t *data, ble_cmd_cb_t cb)` | 设置扫描响应 | `BLE_SET_RSP_DATA` |
| `bk_ble_set_advertising_enable(uint8_t enable, ble_cmd_cb_t cb)` | 开启/停止广播 | `BLE_START_ADV` / `BLE_STOP_ADV` |
| `bk_ble_set_random_addr(bd_addr_t *addr, ble_cmd_cb_t cb)` | 设置随机地址 | `BLE_SET_RANDOM_ADDR` |

**注意**：每个 API 都是异步的，通过 `ble_cmd_cb_t` 回调返回结果（status=0 表示成功）。
调用顺序必须为：`set_advertising_params` → `set_advertising_data` → `set_advertising_enable`。

### 4.3 BLE 扫描（Observer/Central 角色）

头文件：`$SDK_INC/components/bluetooth/bk_dm_gap_ble.h`

| SDK 函数 | 功能 |
|---------|------|
| `bk_ble_gap_register_callback(bk_ble_gap_cb_t cb)` | 注册 GAP 事件回调 |
| `bk_ble_gap_set_scan_params(const bk_ble_ext_scan_params_t *params)` | 设置扫描参数 |
| `bk_ble_gap_start_scan(uint32_t duration, uint16_t period)` | 启动扫描 |
| `bk_ble_gap_stop_scan(void)` | 停止扫描 |

扫描参数结构体 `bk_ble_ext_scan_params_t` 包含 PHY（1M/2M/Coded）、
扫描窗口/间隔、扫描类型（主动/被动）等。

### 4.4 BLE 连接管理

| SDK 函数 | 功能 |
|---------|------|
| `bk_ble_init_start_conn(uint8_t con_idx, ble_cmd_cb_t cb)` | 发起连接（Central） |
| `bk_ble_init_stop_conn(uint8_t con_idx, ble_cmd_cb_t cb)` | 停止连接发起 |
| `bk_ble_disconnect_connection(bd_addr_t *addr, ble_cmd_cb_t cb)` | 断开连接 |
| `bk_ble_update_connection_params(ble_update_conn_param_t *params)` | 更新连接参数 |
| `bk_ble_gap_read_rssi(bk_bd_addr_t remote_addr)` | 读取 RSSI |

### 4.5 GATT Server

头文件：`$SDK_INC/components/bluetooth/bk_ble.h`

| SDK 函数 | 功能 | 时序要求 |
|---------|------|---------|
| `bk_ble_set_event_callback(ble_event_cb_t cb)` | 注册 BLE 事件回调 | **必须在 GATT DB 创建前** |
| `bk_ble_gatt_db_add_service(GATT_DB_SERVICE_INFO *info, uint16_t num, uint16_t *handle)` | 添加服务 | |
| `bk_ble_gatt_db_add_characteristic(...)` | 添加特征 | 需 `service_handle` |
| `bk_ble_gatt_db_add_characteristic_descriptor(...)` | 添加描述符 | 需 `char_handle` |
| `bk_ble_gatt_db_add_completed(void)` | 完成数据库构建 | 最后调用 |
| `bk_ble_gatt_db_set_callback(ble_gatt_db_callback_t cb)` | 注册 GATT 读写回调 | |
| `bk_ble_send_notify(uint8_t conn, uint16_t svc, uint16_t chr, uint8_t *data, uint16_t len)` | 发送通知 | |
| `bk_ble_gatt_read_resp(uint8_t conn, uint8_t *value, uint16_t len)` | 响应读请求 | |
| `bk_ble_gatt_get_char_val(GATT_DB_HANDLE *handle, ATT_VALUE *val)` | 读取特征值 | |

### 4.6 GAP 通用

| SDK 函数 | 功能 |
|---------|------|
| `bk_ble_gap_set_device_name(const char *name)` | 设置设备名 |
| `bk_ble_gap_get_device_name(char *name, uint32_t *size)` | 获取设备名 |
| `bk_ble_gap_get_local_used_addr(bk_bd_addr_t addr, uint8_t *type)` | 获取当前使用地址 |
| `bk_ble_gap_config_local_privacy(bool enable)` | 启用/禁用隐私 |
| `bk_ble_gap_config_local_icon(uint16_t icon)` | 设置外观图标 |
| `bk_ble_gap_update_whitelist(bool add, bk_bd_addr_t addr, bk_ble_wl_addr_type_t type)` | 管理白名单 |
| `bk_ble_gap_clear_whitelist(void)` | 清空白名单 |
| `bk_ble_gap_set_pkt_data_len(bk_bd_addr_t dev, uint16_t len)` | 设置最大数据包长度 |

### 4.7 SMP 安全

| SDK 函数 | 功能 | 条件编译 |
|---------|------|---------|
| `bk_ble_gap_set_security_param(bk_ble_sm_param_t type, void *val, uint8_t len)` | 设置安全参数 | `SMP_INCLUDED` |
| `bk_ble_gap_security_rsp(bk_bd_addr_t addr, bool accept)` | 响应安全请求 | `SMP_INCLUDED` |
| `bk_ble_set_encryption(bk_bd_addr_t addr, bk_ble_sec_act_t act)` | 启动加密 | `SMP_INCLUDED` |
| `bk_ble_passkey_reply(bk_bd_addr_t addr, bool accept, uint32_t passkey)` | 回复配对码 | `SMP_INCLUDED` |
| `bk_ble_confirm_reply(bk_bd_addr_t addr, bool accept)` | 回复数值比较 | `SMP_INCLUDED` |
| `bk_ble_remove_bond_device(bk_bd_addr_t addr)` | 删除绑定设备 | `SMP_INCLUDED` |
| `bk_ble_get_bond_device_num(void)` | 获取绑定设备数 | `SMP_INCLUDED` |

### 4.8 BLE 库选择

| 库文件 | 大小 | 用途 | 链接状态 |
|-------|------|------|---------|
| `libbluetooth_controller_ble.a` | 3.3 MB | BLE 专用控制器 | **应链接**（当前被排除） |
| `libbluetooth_host_ble.a` | 3.1 MB | BLE Host 协议栈 | **应链接**（当前被排除） |
| `libbk_bluetooth.a` | 367 KB | Beken 蓝牙驱动封装 | 已链接，保留 |
| `libbk_coex.a` | 40 KB | PTA 共存接口 | 已链接，保留 |
| `libbluetooth_controller.a` | 3.3 MB | 通用/双模控制器 | **应排除**（当前已链接，错误） |
| `libble_wifi_exchange.a` | 5.5 KB | BLE/WiFi 信道交换 | 建议链接（当前被排除） |

---

## 5. 环境配置

### 5.1 Kconfig 新增选项

需在 `$CONTEST/board/bk7258_t5ai/chip/Kconfig` 中添加：

```
config BK7258_WIFI
    bool "BK7258 WiFi (802.11ax) SDK wrapper"
    default n
    depends on ARCH_CHIP_BK7258 && !BK7258_AP_CORE
    select BK7258_SDK_IRQ_BRIDGE
    ---help---
        Enable WiFi control plane via SDK prebuilt libraries.
        Stage 1: scan verification.  Stage 2: netdev data path.

config BK7258_BLE
    bool "BK7258 BLE 5.4 SDK wrapper"
    default n
    depends on ARCH_CHIP_BK7258 && !BK7258_AP_CORE
    select BK7258_SDK_IRQ_BRIDGE
    ---help---
        Enable BLE 5.4 via SDK prebuilt libraries.
        Requires libbluetooth_controller_ble.a + libbluetooth_host_ble.a.
```

### 5.2 Make.defs 库配置

需在 `$CONTEST/board/bk7258_t5ai/scripts/Make.defs` 中修改：

**WiFi**（已有条件逻辑）：

```makefile
# 当前已有：CONFIG_BK7258_WIFI=y 时保留 WiFi 库
ifneq ($(CONFIG_BK7258_WIFI),y)
BK_EXCLUDE_LIBS += \
  libbk_wifi.a libwifi.a libwpa_supplicant-2.10.a libcom_phy.a
endif
```

**BLE**（需新增）：

```makefile
# BLE：CONFIG_BK7258_BLE=y 时保留 BLE 库，排除错误的通用控制器
ifneq ($(CONFIG_BK7258_BLE),y)
BK_EXCLUDE_LIBS += \
  libbluetooth_controller_ble.a \
  libbluetooth_host_ble.a
else
# BLE 启用时：排除通用/双模控制器，使用 BLE 专用
BK_EXCLUDE_LIBS += \
  libbluetooth_controller.a
# 可选：BLE/WiFi 信道交换
# BK_EXCLUDE_LIBS 中移除 libble_wifi_exchange.a
endif
```

### 5.3 chip/Make.defs 源文件配置

```makefile
ifeq ($(CONFIG_BK7258_WIFI),y)
CHIP_CSRCS += bk7258_wifi.c
endif

ifeq ($(CONFIG_BK7258_BLE),y)
CHIP_CSRCS += bk7258_ble.c
endif
```

### 5.4 sdkconfig.h 关键宏

以下宏已在 `$SDK_INC/../config/sdkconfig.h` 中预定义（SDK 编译时固化）：

| 宏 | 值 | 说明 |
|---|---|------|
| `CONFIG_BLUETOOTH` | 1 | 蓝牙启用 |
| `CONFIG_BLE` | 1 | BLE 启用 |
| `CONFIG_BTDM_5_2` | 1 | 蓝牙 5.2（SDK 版本标记，芯片硬件支持 5.4） |
| `CONFIG_BTDM_CONTROLLER_ONLY` | 1 | Controller-only 模式 |
| `CONFIG_WIFI_CLI_ENABLE` | 1 | WiFi CLI 命令 |
| `CONFIG_STA_PS` | 1 | STA 节能 |

### 5.5 链接脚本注意事项

WiFi + BLE 库链接后，代码段和数据段将增大 ~650 KB。需确认链接脚本
（`$CONTEST/board/bk7258_t5ai/scripts/ld.script`）中：

- FLASH 区域有足够空间（16 MB Flash 下 978 KB 仅占 6%，无问题）
- SRAM 区域（640 KB）需为 WiFi/BLE 运行时预留足够堆空间
- 建议将 WiFi 帧缓冲区和 BLE L2CAP 缓冲区定向到 PSRAM（16 MB）

---

## 6. 部署步骤

### 6.1 编译

```bash
cd $NUTTX_ROOT

# 配置（在现有 nsh defconfig 基础上启用 WiFi/BLE）
./tools/configure.sh -l contest2026_135_yongwangzhiqian/board/bk7258_t5ai/configs/cp_nsh

# 启用 WiFi（可选）
kconfig-tweak --enable CONFIG_BK7258_WIFI

# 启用 BLE（可选）
kconfig-tweak --enable CONFIG_BK7258_BLE

# 刷新配置
make olddefconfig

# 编译
make -j$(nproc)
```

### 6.2 烧录

```bash
# 使用项目自带的 postbuild + flash 脚本
# all-app.bin 生成后，通过 UART 或 J-Link 烧录到 0x0 物理地址
# 具体烧录方式参考 docs/bk7258-t5ai/jlink-swd-debug-guide.md
```

### 6.3 NSH 验证

**WiFi 扫描验证**（Stage 1 已实现）：

```
nsh> wscan
[bk-wifi] starting scan...
[bk-wifi] found 5 APs:
   0: ch= 6 rssi= -42 MyHomeWiFi
   1: ch= 1 rssi= -67 OfficeAP
   2: ch=11 rssi= -71 Neighbor
```

**BLE 扫描验证**（待实现）：

```
nsh> blescan
[bk-ble] starting scan...
[bk-ble] found 3 devices:
   0: addr=aa:bb:cc:dd:ee:01 rssi=-55 name=HeartRate
   1: addr=aa:bb:cc:dd:ee:02 rssi=-68 name=TempSensor
```

**BLE 广播验证**（待实现）：

```
nsh> bleadv "BK7258-TEST"
[bk-ble] advertising started, name=BK7258-TEST
```

---

## 7. 已知风险与限制

### 7.1 高风险

**事件标志模拟不足**

SDK WiFi/BT 栈大量使用 FreeRTOS 事件组（多 bit 独立等待/设置/清除）。当前
`rtos_wait_for_event_flags()` 将多 bit 退化为二值信号量，任何 `rtos_set_event_flags()`
都会唤醒等待者。WiFi Supplicant 连接状态机和 BLE SMP 配对状态机可能在错误的时间点
推进，导致**连接失败或安全密钥状态不一致**。

影响模块：`bk7258_os_adapt.c` 的 `rtos_init_event_flags()`、`rtos_wait_for_event_flags()`、
`rtos_set_event_flags()`。

**蓝牙库链接配置错误**

当前 `scripts/Make.defs` 链接了 `libbluetooth_controller.a`（通用/双模控制器），
排除了 `libbluetooth_controller_ble.a`。根据芯片规格（BLE 5.4 only），
当前链接的库是**错误的**，可能包含 BR/EDR 控制器代码导致运行时异常。

### 7.2 中风险

**SRAM 压力**

640 KB 共享 SRAM 需同时承载：NuttX 内核堆、WiFi 栈运行时（Supplicant 线程栈、
帧缓冲区）、BLE Host 栈（GATT DB、L2CAP 缓冲区）、BLE Controller 固件运行时。
WiFi 6 的 40 MHz 帧（最大 ~11 KB MSDU）和 BLE 5.4 的扩展广告数据同时存在时
可能不足。需将大缓冲区引导到 16 MB PSRAM。

**消息队列队首插入缺失**

`rtos_push_to_queue_front()` 降级为普通入队。BLE HCI 层的高优先级事件
（连接完成、断开完成）需要队首插入以确保优先处理。

**`libble_wifi_exchange.a` 排除**

BLE 无法感知 WiFi 活跃信道，WiFi 密集传输时 BLE 连接可能频繁断连。
硬件 PTA 提供基本共存保障，但信道级协调需要此库。

### 7.3 低风险

**BLE 回调注册时序依赖**

`bk_ble_set_event_callback()` 必须在 `bk_ble_gatt_db_add_service()` 之前调用，
否则无法收到 `BLE_5_CREATE_DB` 事件。wrapper 初始化序列需严格保证此顺序。

**TCM 未利用**

16 KB ITCM + 16 KB DTCM 可用于关键中断处理代码，消除 Flash 等待延迟。
当前链接脚本未做 TCM 段分配。

---

## 8. 后续路线

```
Phase 1: 修正链接配置（前置条件）
  ├─ 将 libbluetooth_controller.a 替换为 libbluetooth_controller_ble.a
  ├─ 将 libbluetooth_host_ble.a 从排除列表移除
  └─ 添加 CONFIG_BK7258_WIFI / CONFIG_BK7258_BLE Kconfig 选项

Phase 2: BLE 基础 wrapper
  ├─ 创建 chip/cp/bk7258_ble.c
  ├─ bk_bluetooth_init() + bk_ble_set_event_callback()
  ├─ GAP 扫描（Observer 角色验证）
  └─ NSH 测试命令（blescan）

Phase 3: BLE GATT Server wrapper
  ├─ GATT DB 构建 + 广播 + 连接管理
  └─ NSH 测试命令（bleadv / blenotify）

Phase 4: WiFi STA 数据路径
  ├─ ethernetif_input() → netdev_input() 桥接
  ├─ NuttX netdev TX → SDK 发送 API
  ├─ STA 连接/断开管理（事件驱动）
  └─ NSH 测试命令（wconnect / wstatus）

Phase 5: WiFi/BT 共存
  ├─ 引入 libble_wifi_exchange.a
  └─ 验证 WiFi + BLE 同时运行的稳定性

Phase 6: 高级特性（按需）
  ├─ WPA3 连接测试
  ├─ BLE 长距离（Coded PHY）验证
  ├─ TWT 低功耗验证
  ├─ SoftAP + STA 共存验证
  └─ AoA/AoD 定位（需天线阵列硬件）
```

---

## 附录 A：SDK 预编译库完整清单

`$SDK_LIBS/` 下共 81 个 `.a` 文件。以下按功能分组列出与 WiFi/BLE 相关的库：

### WiFi 相关

| 库 | 大小 | 说明 |
|---|------|------|
| `libbk_wifi.a` | 1.5 MB | WiFi 控制面 + OS 函数表 |
| `libwifi.a` | 5.9 MB | WiFi MAC/UMAC 协议栈 |
| `libwpa_supplicant-2.10.a` | 5.6 MB | WPA 认证/密钥协商 |
| `libcom_phy.a` | 1.0 MB | 通用 PHY 校准 |
| `libbk_phy.a` | 150 KB | PHY 底层操作 |
| `libbk_netif.a` | 23 KB | 网络接口抽象 |

### BLE 相关

| 库 | 大小 | 说明 |
|---|------|------|
| `libbk_bluetooth.a` | 367 KB | Beken 蓝牙驱动封装 |
| `libbluetooth_controller_ble.a` | 3.3 MB | BLE 专用控制器 |
| `libbluetooth_host_ble.a` | 3.1 MB | BLE Host 协议栈 |
| `libbk_coex.a` | 40 KB | PTA 共存接口 |
| `libble_wifi_exchange.a` | 5.5 KB | BLE/WiFi 信道交换 |
| `libcontroller_if.a` | 249 KB | 控制器接口 |

### 已排除（与 WiFi/BLE 无关）

`libcmsis.a`、`libcoredump.a`、`libunity.a`、`libat_server.a`、`liblwip_intf_v2_1.a`、
`libiperf.a`、`libhmac_sha_256.a`、`libpsa_mbedtls.a`、`libwifi_csi*.a`、
`libbk_rtos.a`、`libbk_system.a`、`libos_source.a` 等。

---

## 附录 B：OS 适配层 FreeRTOS→NuttX 映射表

| FreeRTOS 原语 | NuttX 实现 | 完备度 |
|--------------|-----------|-------|
| `xTaskCreate` / `vTaskDelete` | `kthread_create` / `task_delete` | 完备 |
| `xSemaphoreCreateBinary` 系列 | `nxsem_init/wait/post/destroy` | 完备 |
| `xSemaphoreCreateMutex` | `nxmutex_init` | 完备（含递归） |
| `xQueueCreate` 系列 | `file_mq_open/send/receive` | 基本完备 |
| `xEventGroupCreate` 系列 | 信号量模拟 | **部分完备** |
| `xTimerCreate` 系列 | NuttX `wd_start` | 完备 |
| `pvPortMalloc` / `vPortFree` | `kmm_malloc` / `kmm_free` | 完备 |
| `portENTER_CRITICAL` | `enter_critical_section` | 完备 |
| `vTaskSuspendAll` | `sched_lock` | 完备 |
