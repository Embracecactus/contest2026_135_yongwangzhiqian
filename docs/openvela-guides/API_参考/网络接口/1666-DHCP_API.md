# DHCP API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1666&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:18  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/network/net_dhcp.md) | 简体中文 \]

# DHCP API

DHCP（Dynamic Host Configuration Protocol）客户端与服务器接口，覆盖 IPv4（<span class="reference">dhcpc\_\*</span> / <span class="reference">dhcpd\_\*</span>）和 IPv6（<span class="reference">dhcp6c\_\*</span>）两套地址分配协议。

头文件：<span class="reference">\#include \<netutils/dhcpc.h\></span>、<span class="reference">\#include \<netutils/dhcp6c.h\></span>、<span class="reference">\#include \<netutils/dhcpd.h\></span>

# openvela 实现说明

  - **IPv4 客户端**：<span class="reference">dhcpc\_\*</span> 系列封装完整的 DHCP 客户端状态机（DISCOVER/OFFER/REQUEST/ACK）
  - **IPv6 客户端**：<span class="reference">dhcp6c\_\*</span> 系列实现 DHCPv6 客户端协议流程
  - **服务器**：<span class="reference">dhcpd\_\*</span> 系列提供简单的 DHCP 服务器能力，可在热点/AP 模式下分配 IP
  - **异步调用**：<span class="reference">\*\_request\_async</span> 接口提供回调式调用，避免阻塞当前线程
  - **配置依赖**：需启用 <span class="reference">CONFIG\_NETUTILS\_DHCPC</span> / <span class="reference">CONFIG\_NETUTILS\_DHCP6C</span> / <span class="reference">CONFIG\_NETUTILS\_DHCPD</span>

# DHCP 客户端

头文件：<span class="reference">\#include \<netutils/dhcpc.h\></span>

## dhcpc\_open

    void *dhcpc_open(const char *interface, const void *mac_addr, int mac_len);

创建 DHCP 客户端会话。

**参数**：

  - <span class="reference">interface</span> 网络接口名称（如 <span class="reference">"eth0"</span>）。
  - <span class="reference">mac\_addr</span> MAC 地址。
  - <span class="reference">mac\_len</span> MAC 地址长度。

**返回值**：

成功时返回会话句柄，失败时返回 <span class="reference">NULL</span>。

## dhcpc\_request

    int dhcpc_request(void *handle, struct dhcpc_state *presult);

执行 DHCP 协商获取 IP 地址（阻塞调用）。

**参数**：

  - <span class="reference">handle</span> 由 <span class="reference">dhcpc\_open()</span> 返回的会话句柄。
  - <span class="reference">presult</span> 存储获取的网络配置（IP、子网掩码、网关、DNS、租约时间）。

**返回值**：

成功时返回 0，失败时返回 -1。

## dhcpc\_request\_async

    int dhcpc_request_async(void *handle, dhcpc_callback_t callback);

异步执行 DHCP 协商，在后台线程中运行，通过回调返回结果。

**参数**：

  - <span class="reference">handle</span> 会话句柄。
  - <span class="reference">callback</span> 结果回调函数。

**返回值**：

成功启动时返回 0，失败时返回 -1。

## dhcpc\_cancel

    void dhcpc_cancel(void *handle);

取消正在进行的 DHCP 协商。

## dhcpc\_close

    void dhcpc_close(void *handle);

关闭 DHCP 客户端会话，释放所有资源。内部会先调用 <span class="reference">dhcpc\_cancel()</span>。

# DHCPv6 客户端

头文件：<span class="reference">\#include \<netutils/dhcp6c.h\></span>

## dhcp6c\_open

    void *dhcp6c_open(const char *interface);

创建 DHCPv6 客户端会话。

**参数**：

  - <span class="reference">interface</span> 网络接口名称。

**返回值**：

成功时返回会话句柄，失败时返回 <span class="reference">NULL</span>。

## dhcp6c\_request

    int dhcp6c_request(void *handle, struct dhcp6c_state *presult);

执行 DHCPv6 协商获取地址（阻塞调用）。

## dhcp6c\_request\_async

    int dhcp6c_request_async(void *handle, dhcp6c_callback_t callback);

异步执行 DHCPv6 协商。

## dhcp6c\_cancel

    void dhcp6c_cancel(void *handle);

取消正在进行的 DHCPv6 协商。

## dhcp6c\_close

    void dhcp6c_close(void *handle);

关闭 DHCPv6 客户端会话。

# DHCP 服务器

头文件：<span class="reference">\#include \<netutils/dhcpd.h\></span>

## dhcpd\_run

    int dhcpd_run(const char *interface);

在当前线程运行 DHCP 服务器（阻塞，直到出错才返回）。

## dhcpd\_start

    int dhcpd_start(const char *interface);

以后台任务启动 DHCP 服务器守护进程。

**返回值**：

成功时返回 0，失败时返回负的错误码。

## dhcpd\_stop

    int dhcpd_stop(void);

停止运行中的 DHCP 服务器守护进程。

## dhcpd\_set\_startip

    int dhcpd_set_startip(in_addr_t startip);

配置 DHCP 服务器分配地址池的起始 IP 地址。

**参数**：

  - <span class="reference">startip</span> 起始 IP 地址（网络字节序）。

**返回值**：

始终返回 <span class="reference">0</span>。

## dhcpd\_set\_routerip

    int dhcpd_set_routerip(in_addr_t routerip);

配置 DHCP 服务器下发给客户端的默认网关地址。

**参数**：

  - <span class="reference">routerip</span> 默认网关 IP（网络字节序）。

**返回值**：

始终返回 <span class="reference">0</span>。

## dhcpd\_set\_netmask

    int dhcpd_set_netmask(in_addr_t netmask);

配置 DHCP 服务器下发给客户端的子网掩码。

**参数**：

  - <span class="reference">netmask</span> 子网掩码（网络字节序）。

**返回值**：

始终返回 <span class="reference">0</span>。

## dhcpd\_set\_dnsip

    int dhcpd_set_dnsip(in_addr_t dnsip);

配置 DHCP 服务器下发给客户端的 DNS 服务器地址。

**参数**：

  - <span class="reference">dnsip</span> DNS 服务器 IP（网络字节序）。

**返回值**：

始终返回 <span class="reference">0</span>。
