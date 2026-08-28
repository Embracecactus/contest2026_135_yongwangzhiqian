# 无线网络接口（WAPI）API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1668&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:20  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/network/wapi.md) | 简体中文 \]

# 无线网络接口（WAPI）API

<span class="reference">wapi\_\*</span> 系列接口基于 Linux Wireless Extensions（WEXT）封装，提供无线网络配置、扫描、关联、功率管理、区域代码和 PMKSA 缓存等能力。

头文件：<span class="reference">\#include \<wireless/wapi.h\></span>

# openvela 实现说明

  - **底层机制**：基于 Linux Wireless Extensions（WEXT）的 ioctl 协议与 Wi-Fi 驱动通信
  - **支持场景**：Station（客户端）模式、AP 模式、混杂模式（由驱动支持程度决定）
  - **配置依赖**：需启用 <span class="reference">CONFIG\_WIRELESS\_WAPI</span> 及对应的 Wi-Fi 芯片驱动
  - **典型用法**：
      - <span class="reference">wapi\_set\_ifup</span>/<span class="reference">wapi\_set\_ifdown</span> 控制接口启停
      - <span class="reference">wapi\_set\_essid</span> + <span class="reference">wapi\_set\_mode</span> 配置连接目标
      - <span class="reference">wapi\_scan\_\*</span> / <span class="reference">wapi\_escan\_\*</span> 扫描周围 AP
      - <span class="reference">wapi\_load\_config</span> / <span class="reference">wapi\_save\_config</span> 持久化配置
  - **扩展能力**：通过 <span class="reference">wapi\_extend\_params</span> / <span class="reference">wapi\_set\_pmksa</span> 等接口与驱动私有特性交互

# 无线网络接口

头文件：<span class="reference">\#include \<wireless/wapi.h\></span>

wapi 提供无线网络配置接口，包括 SSID 扫描、连接、频率设置等。

**连接管理**

## wapi\_get\_ifup

    int wapi_get_ifup(int sock, const char *ifname, int *is_up);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称.
  - <span class="reference">is\_up</span> 接口状态，0 表示启用，1 表示禁用。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_ifup

    int wapi_set_ifup(int sock, const char *ifname);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称.

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_ifdown

    int wapi_set_ifdown(int sock, const char *ifname);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称，将被关闭。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_get\_ip

    int wapi_get_ip(int sock, const char *ifname, struct in_addr *addr);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称.
  - <span class="reference">addr</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_ip

    int wapi_set_ip(int sock, const char *ifname, const struct in_addr *addr);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称，其 IP 地址
  - <span class="reference">addr</span> 指向包含新 IP 地址的结构体。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_get\_netmask

    int wapi_get_netmask(int sock, const char *ifname, struct in_addr *addr);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称.
  - <span class="reference">addr</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_netmask

    int wapi_set_netmask(int sock, const char *ifname, const struct in_addr *addr);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称.
  - <span class="reference">addr</span> 指向包含新子网掩码的结构体。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_add\_route\_gw

    int wapi_add_route_gw(int sock, enum wapi_route_target_e targettype, const struct in_addr *target, const struct in_addr *netmask, const struct in_addr *gw);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">targettype</span> 目标类型。
  - <span class="reference">target</span> 指向目标 IP 地址。
  - <span class="reference">netmask</span> 指向目标地址对应的子网掩码。
  - <span class="reference">gw</span> 指向对应的网关（路由器）IP 地址，用于

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_del\_route\_gw

    int wapi_del_route_gw(int sock, enum wapi_route_target_e targettype, const struct in_addr *target, const struct in_addr *netmask, const struct in_addr *gw);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">targettype</span> 目标类型。
  - <span class="reference">target</span> 指向路由的目标 IP 地址
  - <span class="reference">netmask</span> 指向目标地址对应的子网掩码。
  - <span class="reference">gw</span> 指向对应的网关（路由器）IP 地址，用于

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_get\_freq

    int wapi_get_freq(int sock, const char *ifname, double *freq, enum wapi_freq_flag_e *flag);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">freq</span> 输出参数。
  - <span class="reference">flag</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_freq

    int wapi_set_freq(int sock, const char *ifname, double freq, enum wapi_freq_flag_e flag);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">freq</span> 频率值。
  - <span class="reference">flag</span> 频率值。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_freq2chan

    int wapi_freq2chan(int sock, const char *ifname, double freq, int *chan);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">freq</span> 频率（Hz），将被转换为信道编号。
  - <span class="reference">chan</span> 输出参数。

## wapi\_chan2freq

    int wapi_chan2freq(int sock, const char *ifname, int chan, double *freq);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 信道。
  - <span class="reference">chan</span> 信道编号，将被转换为频率。
  - <span class="reference">freq</span> 输出参数。

## wapi\_get\_essid

    int wapi_get_essid(int sock, const char *ifname, char *essid, enum wapi_essid_flag_e *flag);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">essid</span> 用于存储结果。
  - <span class="reference">flag</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_essid

    int wapi_set_essid(int sock, const char *ifname, const char *essid, enum wapi_essid_flag_e flag);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">essid</span> 指向一个以 <span class="reference">\\0</span> 结尾的 ESSID 字符串
  - <span class="reference">flag</span> 控制标志。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_get\_mode

    int wapi_get_mode(int sock, const char *ifname, enum wapi_mode_e *mode);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">mode</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_mode

    int wapi_set_mode(int sock, const char *ifname, enum wapi_mode_e mode);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">mode</span> 输出参数。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_make\_broad\_ether

    int wapi_make_broad_ether(struct ether_addr *sa);

**参数**：

  - <span class="reference">sa</span> 输出参数。

**返回值**：

返回底层 <span class="reference">wapi\_make\_ether()</span> 调用的结果。

## wapi\_make\_null\_ether

    int wapi_make_null_ether(struct ether_addr *sa);

**参数**：

  - <span class="reference">sa</span> 输出参数。

**返回值**：

返回底层 <span class="reference">wapi\_make\_ether()</span> 调用的结果。

## wapi\_get\_ap

    int wapi_get_ap(int sock, const char *ifname, struct ether_addr *ap);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">ap</span> 要设置的地址。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_ap

    int wapi_set_ap(int sock, const char *ifname, const struct ether_addr *ap);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">ap</span> 接入点的 MAC 地址。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_get\_bitrate

    int wapi_get_bitrate(int sock, const char *ifname, int *bitrate, enum wapi_bitrate_flag_e *flag);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">bitrate</span> 输出参数，用于存储查询到的比特率。
  - <span class="reference">flag</span> 输出参数，用于存储比特率标志位。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_bitrate

    int wapi_set_bitrate(int sock, const char *ifname, int bitrate, enum wapi_bitrate_flag_e flag);

**参数**：

  - <span class="reference">sock</span> 套接字描述符（用于 ioctl 操作）。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">bitrate</span> 比特率 .
  - <span class="reference">flag</span> 比特率 flag.

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_dbm2mwatt

    int wapi_dbm2mwatt(int dbm);

**参数**：

  - <span class="reference">dbm</span> 要转换的 dBm 值。

**返回值**：

转换后的毫瓦值。

## wapi\_mwatt2dbm

    int wapi_mwatt2dbm(int mwatt);

**参数**：

  - <span class="reference">mwatt</span> 毫瓦值。

**返回值**：

转换后的 dBm 值。

## wapi\_get\_txpower

    int wapi_get_txpower(int sock, const char *ifname, int *power, enum wapi_txpower_flag_e *flag);

**参数**：

  - <span class="reference">sock</span> 套接字描述符.
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">power</span> 输出参数，用于存储发射功率值。
  - <span class="reference">flag</span> 输出参数，用于存储发射功率的单位。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_txpower

    int wapi_set_txpower(int sock, const char *ifname, int power, enum wapi_txpower_flag_e flag);

**参数**：

  - <span class="reference">sock</span> 套接字描述符.
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">power</span> 发射功率。
  - <span class="reference">flag</span> 发射功率。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_make\_socket

    int wapi_make_socket(void);

## wapi\_scan\_init

    int wapi_scan_init(int sock, const char *ifname, const char *essid);

**参数**：

  - <span class="reference">sock</span> 套接字描述符.
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">essid</span> 要扫描的 ESSID。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_scan\_channel\_init

    int wapi_scan_channel_init(int sock, const char *ifname, const char *essid, uint8_t *channels, int num_channels);

**参数**：

  - <span class="reference">sock</span> 套接字描述符.
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">essid</span> 要扫描的 ESSID。
  - <span class="reference">channels</span> 要扫描的信道编号数组。
  - <span class="reference">num\_channels</span> 信道。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_escan\_init

    int wapi_escan_init(int sock, const char *ifname, uint8_t scan_type, const char *essid);

**参数**：

  - <span class="reference">sock</span> 套接字描述符.
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">scan\_type</span> 扫描类型。
  - <span class="reference">essid</span> 要扫描的 ESSID。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_escan\_channel\_init

    int wapi_escan_channel_init(int sock, const char *ifname, uint8_t scan_type, const char *essid, uint8_t *channels, int num_channels);

**参数**：

  - <span class="reference">sock</span> 套接字描述符.
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">scan\_type</span> 扫描类型。
  - <span class="reference">essid</span> 要扫描的 ESSID。
  - <span class="reference">channels</span> 要扫描的信道编号数组。
  - <span class="reference">num\_channels</span> 信道。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_scan\_stat

    int wapi_scan_stat(int sock, const char *ifname);

**参数**：

  - <span class="reference">sock</span> 套接字描述符.
  - <span class="reference">ifname</span> 网络接口名称。

## wapi\_scan\_coll

    int wapi_scan_coll(int sock, const char *ifname, struct wapi_list_s *aps);

**参数**：

  - <span class="reference">sock</span> 套接字描述符.
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">aps</span> 收集的扫描结果列表。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_scan\_coll\_free

    void wapi_scan_coll_free(struct wapi_list_s *aps);

**参数**：

  - <span class="reference">aps</span> 要释放的扫描结果列表。

## wapi\_set\_country

    int wapi_set_country(int sock, const char *ifname, const char *country);

**参数**：

  - <span class="reference">sock</span> 套接字描述符.
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">country</span> 指向双字符字符串，表示要设置的

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_get\_country

    int wapi_get_country(int sock, const char *ifname, char *country);

**参数**：

  - <span class="reference">sock</span> 套接字描述符.
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">country</span> 指向调用方提供的缓冲区，用于接收

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_get\_sensitivity

    int wapi_get_sensitivity(int sock, const char *ifname, int *sense);

**参数**：

  - <span class="reference">sock</span> 套接字描述符.
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">sense</span> 指向调用方提供的整型变量，用于接收

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_load\_config

    void *wapi_load_config(const char *ifname, const char *confname, struct wpa_wconfig_s *conf);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">confname</span> 路径。
  - <span class="reference">conf</span> 指向调用方提供的结构体，用于填入

## wapi\_unload\_config

    void wapi_unload_config(void *load);

**参数**：

  - <span class="reference">load</span> 配置资源句柄。

## wapi\_save\_config

    int wapi_save_config(const char *ifname, const char *confname, const struct wpa_wconfig_s *conf);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">confname</span> 路径。
  - <span class="reference">conf</span> 指向包含配置信息的结构体

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_pta\_prio

    int wapi_set_pta_prio(int sock, const char *ifname, enum wapi_pta_prio_e pta_prio);

**参数**：

  - <span class="reference">sock</span> 文件描述符。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">pta\_prio</span> PTA 优先级。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_get\_pta\_prio

    int wapi_get_pta_prio(int sock, const char *ifname, enum wapi_pta_prio_e *pta_prio);

**参数**：

  - <span class="reference">sock</span> 文件描述符。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">pta\_prio</span> 指向用于接收当前 PTA 优先级的变量

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_pmksa

    int wapi_set_pmksa(int sock, const char *ifname, const uint8_t *pmk, int len);

**参数**：

  - <span class="reference">sock</span> 文件描述符。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">pmk</span> 指向包含 PMKSA 数据的缓冲区
  - <span class="reference">len</span> 长度。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_get\_pmksa

    int wapi_get_pmksa(int sock, const char *ifname, uint8_t *pmk, int len);

**参数**：

  - <span class="reference">sock</span> 文件描述符。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">pmk</span> 指向用于接收查询到的 PMKSA 数据的缓冲区
  - <span class="reference">len</span> 缓冲区大小。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_extend\_params

    int wapi_extend_params(int sock, int cmd, struct iwreq *wrq);

**参数**：

  - <span class="reference">sock</span> 文件描述符。
  - <span class="reference">cmd</span> 私有 ioctl 命令码。
  - <span class="reference">wrq</span> 指向 <span class="reference">iwreq</span> 结构体，调用方需预先

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_set\_power\_save

    int wapi_set_power_save(int sock, const char *ifname, bool on);

**参数**：

  - <span class="reference">sock</span> 文件描述符。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">on</span> 控制标志。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## wapi\_get\_power\_save

    int wapi_get_power_save(int sock, const char *ifname, bool *on);

**参数**：

  - <span class="reference">sock</span> 文件描述符。
  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">on</span> 指向用于接收当前状态的布尔变量

**返回值**：

成功时返回 0，失败时返回负的错误码。
