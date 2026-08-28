# 网络工具库（netlib）API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1667&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:19  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/network/netlib.md) | 简体中文 \]

# 网络工具库（netlib）API

openvela 网络工具库（<span class="reference">netlib\_\*</span>）提供了一系列简化 BSD 套接字操作的辅助函数，涵盖 IPv4/IPv6 地址管理、路由、ARP、MAC 地址、MTU、防火墙（iptables/ip6tables）、网络连通性检查等。

头文件：<span class="reference">\#include \<netutils/netlib.h\></span>

# openvela 实现说明

  - **定位**：在 BSD socket API 基础上的便利封装，隐藏 <span class="reference">ioctl</span> + <span class="reference">SIOCGIF\*</span> 等底层细节
  - **覆盖范围**：
      - IPv4/IPv6 地址、网关、子网掩码、DNS、路由
      - MAC 地址读写、接口上/下、MTU 设置
      - VLAN 管理、ARP 表操作
      - iptables/ip6tables 操作
      - 网络连通性检查（<span class="reference">ping</span> / HTTP / 接口可达性）
      - URL 解析工具
  - **配置依赖**：需启用 <span class="reference">CONFIG\_NETUTILS\_NETLIB</span>，部分子接口另需对应模块配置（如 <span class="reference">CONFIG\_NET\_ARP</span>、<span class="reference">CONFIG\_NET\_IPv6</span> 等）
  - **错误处理**：多数接口成功时返回 <span class="reference">0</span> 或 <span class="reference">OK</span>，失败时返回 <span class="reference">ERROR</span>（<span class="reference">-1</span>）并设置 <span class="reference">errno</span>

# 网络工具库

头文件：<span class="reference">\#include \<netutils/netlib.h\></span>

netlib 提供网络配置工具函数，包括接口地址设置、路由管理、ARP 操作等。

**IPv4 地址管理**

## netlib\_get\_ipv4addr

    int netlib_get_ipv4addr(const char *ifname, struct in_addr *addr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 用于存储 IP 地址

## netlib\_set\_ipv4addr

    int netlib_set_ipv4addr(const char *ifname, const struct in_addr *addr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 要设置的地址

## netlib\_set\_dripv4addr

    int netlib_set_dripv4addr(const char *ifname, const struct in_addr *addr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 要设置的地址

## netlib\_get\_dripv4addr

    int netlib_get_dripv4addr(const char *ifname, struct in_addr *addr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 用于存储默认路由地址

## netlib\_set\_ipv4netmask

    int netlib_set_ipv4netmask(const char *ifname, const struct in_addr *addr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 要设置的地址

## netlib\_get\_ipv4netmask

    int netlib_get_ipv4netmask(const char *ifname, struct in_addr *addr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 用于存储子网掩码

## netlib\_ipv4adaptor

    int netlib_ipv4adaptor(in_addr_t destipaddr, in_addr_t *srcipaddr);

**参数**：

  - <span class="reference">destipaddr</span> 目标 IPv4 地址
  - <span class="reference">srcipaddr</span> 用于存储适配器地址

## netlib\_read\_ipv4route

    ssize_t netlib_read_ipv4route(FILE *stream, struct netlib_ipv4_route_s *route);

**参数**：

  - <span class="reference">fd</span> procfs IPv4 路由表的文件描述符
  - <span class="reference">route</span> 用于存储下一条路由表项

## netlib\_ipv4router

    int netlib_ipv4router(const struct in_addr *destipaddr, struct in_addr *router);

**参数**：

  - <span class="reference">destipaddr</span> 目标 IP 地址。
  - <span class="reference">router</span> - 用于存储网关的 IP 地址，即

## netlib\_obtain\_ipv4addr

    int netlib_obtain_ipv4addr(const char *ifname);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称

## netlib\_set\_ipv4dnsaddr

    int netlib_set_ipv4dnsaddr(const struct in_addr *inaddr);

**参数**：

  - <span class="reference">inaddr</span> 要设置的地址

**IPv6 地址管理**

## netlib\_add\_ipv6addr

    int netlib_add_ipv6addr(const char *ifname, const struct in6_addr *addr, uint8_t preflen);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 地址 to add
  - <span class="reference">preflen</span> 前缀长度（位）。

## netlib\_del\_ipv6addr

    int netlib_del_ipv6addr(const char *ifname, const struct in6_addr *addr, uint8_t preflen);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 地址 to delete
  - <span class="reference">preflen</span> 前缀长度（位）。

## netlib\_get\_ipv6addr

    int netlib_get_ipv6addr(const char *ifname, struct in6_addr *addr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 用于存储 IP 地址

## netlib\_set\_ipv6addr

    int netlib_set_ipv6addr(const char *ifname, const struct in6_addr *addr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 要设置的地址

## netlib\_set\_dripv6addr

    int netlib_set_dripv6addr(const char *ifname, const struct in6_addr *addr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 要设置的地址

## netlib\_set\_ipv6netmask

    int netlib_set_ipv6netmask(const char *ifname, const struct in6_addr *addr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">ipaddr</span> 要设置的地址

## netlib\_ipv6adaptor

    int netlib_ipv6adaptor(const struct in6_addr *destipaddr, struct in6_addr *srcipaddr);

**参数**：

  - <span class="reference">destipaddr</span> 目标 IP 地址。
  - <span class="reference">srcipaddr</span> - 用于存储适配器地址

## netlib\_ipv6netmask2prefix

    uint8_t netlib_ipv6netmask2prefix(const uint16_t *mask);

**参数**：

  - <span class="reference">mask</span> 子网掩码。

## netlib\_prefix2ipv6netmask

    void netlib_prefix2ipv6netmask(uint8_t preflen, struct in6_addr *netmask);

**参数**：

  - <span class="reference">preflen</span> 前缀长度（位）。
  - <span class="reference">netmask</span> 用于存储子网掩码.

## netlib\_read\_ipv6route

    ssize_t netlib_read_ipv6route(FILE *stream, struct netlib_ipv6_route_s *route);

**参数**：

  - <span class="reference">fd</span> 路由表项。
  - <span class="reference">route</span> 用于存储下一条路由表项

## netlib\_ipv6router

    int netlib_ipv6router(const struct in6_addr *destipaddr, struct in6_addr *router);

**参数**：

  - <span class="reference">destipaddr</span> 目标 IP 地址。
  - <span class="reference">router</span> - 用于存储网关的 IP 地址，即

## netlib\_obtain\_ipv6addr

    int netlib_obtain_ipv6addr(const char *ifname);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称。

## netlib\_set\_ipv6dnsaddr

    int netlib_set_ipv6dnsaddr(const struct in6_addr *inaddr);

**参数**：

  - <span class="reference">inaddr</span> 要设置的地址

**接口管理**

## netlib\_setmacaddr

    int netlib_setmacaddr(const char *ifname, const uint8_t *macaddr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">macaddr</span> MAC 地址。

## netlib\_getmacaddr

    int netlib_getmacaddr(const char *ifname, uint8_t *macaddr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">macaddr</span> 用于存储 MAC 地址

## netlib\_getessid

    int netlib_getessid(const char *ifname, char *essid, size_t idlen);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">essid</span> 用于存储结果。
  - <span class="reference">idlen</span> ESSID 缓冲区大小。

## netlib\_setessid

    int netlib_setessid(const char *ifname, const char *essid);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">essid</span> ESSID（网络名称）。

## netlib\_getifstatus

    int netlib_getifstatus(const char *ifname, uint8_t *flags);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">flags</span> 接口标志。

## netlib\_ifup

    int netlib_ifup(const char *ifname);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称

## netlib\_ifdown

    int netlib_ifdown(const char *ifname);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称

## netlib\_set\_mtu

    int netlib_set_mtu(const char *ifname, int mtu);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">mtu</span> 最大传输单元（MTU）。

**返回值**：

:

## netlib\_getifstatistics

    int netlib_getifstatistics(const char *ifname, struct netdev_statistics_s *stat);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">stat</span> 用于存储设备统计信息。

## netlib\_check\_ifconflict

    int netlib_check_ifconflict(const char *ifname);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称

**路由管理**

## netlib\_get\_route

    ssize_t netlib_get_route(struct rtentry *rtelist, unsigned int nentries, sa_family_t family);

**参数**：

  - <span class="reference">rtelist</span> 用于存储设备列表。
  - <span class="reference">nentries</span> 数组容量（条目数）。
  - <span class="reference">family</span> - 地址族。 See AF\_\* definitions in

**ARP 管理**

## netlib\_del\_arpmapping

    int netlib_del_arpmapping(const struct sockaddr_in *inaddr, const char *ifname);

**参数**：

  - <span class="reference">inaddr</span> IPv4 地址。
  - <span class="reference">ifname</span> 网络接口名称。

## netlib\_get\_arpmapping

    int netlib_get_arpmapping(const struct sockaddr_in *inaddr, uint8_t *macaddr, const char *ifname);

**参数**：

  - <span class="reference">inaddr</span> IPv4 地址。
  - <span class="reference">macaddr</span> 用于存储对应的以太网 MAC 地址
  - <span class="reference">ifname</span> 网络接口名称。

## netlib\_set\_arpmapping

    int netlib_set_arpmapping(const struct sockaddr_in *inaddr, const uint8_t *macaddr, const char *ifname);

**参数**：

  - <span class="reference">inaddr</span> IPv4 地址。
  - <span class="reference">macaddr</span> MAC 地址。
  - <span class="reference">ifname</span> 网络接口名称。

## netlib\_get\_arptable

    ssize_t netlib_get_arptable(struct arpreq *arptab, unsigned int nentries);

**参数**：

  - <span class="reference">arptab</span> 用于存储 ARP 表副本
  - <span class="reference">nentries</span> 数组容量（条目数）。

## netlib\_ifarp

    int netlib_ifarp(const char *ifname);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称

## netlib\_ifnoarp

    int netlib_ifnoarp(const char *ifname);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称

**DNS 管理**

## netlib\_clear\_dnsaddr

    void netlib_clear_dnsaddr(void);

**VLAN 管理**

## netlib\_add\_vlan

    int netlib_add_vlan(const char *ifname, int vlanid, int prio);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称。
  - <span class="reference">vlanid</span> VLAN 标识符。
  - <span class="reference">prio</span> 默认 VLAN 优先级（PCP）。

## netlib\_del\_vlan

    int netlib_del_vlan(const char *vlanif);

**iptables**

## netlib\_ipt\_commit

    int netlib_ipt_commit(const struct ipt_replace *repl);

**参数**：

  - <span class="reference">repl</span> 要提交的配置。

## netlib\_ipt\_flush

    int netlib_ipt_flush(const char *table, enum nf_inet_hooks hook);

**参数**：

  - <span class="reference">table</span> 表名。
  - <span class="reference">hook</span> 钩子点。

## netlib\_ipt\_policy

    int netlib_ipt_policy(const char *table, enum nf_inet_hooks hook, int verdict);

**参数**：

  - <span class="reference">table</span> 策略。
  - <span class="reference">hook</span> 钩子点。
  - <span class="reference">verdict</span> 判定值。

## netlib\_ipt\_append

    int netlib_ipt_append(struct ipt_replace **repl, const struct ipt_entry *entry, enum nf_inet_hooks hook);

**参数**：

  - <span class="reference">repl</span> 要提交的配置。
  - <span class="reference">entry</span> 要追加的规则条目。
  - <span class="reference">hook</span> 钩子点。

## netlib\_ipt\_insert

    int netlib_ipt_insert(struct ipt_replace **repl, const struct ipt_entry *entry, enum nf_inet_hooks hook, int rulenum);

**参数**：

  - <span class="reference">repl</span> 要提交的配置。
  - <span class="reference">entry</span> 要插入的规则条目。
  - <span class="reference">hook</span> 钩子点。
  - <span class="reference">rulenum</span> 规则编号。

## netlib\_ipt\_delete

    int netlib_ipt_delete(struct ipt_replace *repl, const struct ipt_entry *entry, enum nf_inet_hooks hook, int rulenum);

**参数**：

  - <span class="reference">repl</span> 要提交的配置。
  - <span class="reference">entry</span> 要删除的规则条目。
  - <span class="reference">hook</span> 钩子点。
  - <span class="reference">rulenum</span> 规则编号。

## netlib\_ipt\_fillifname

    int netlib_ipt_fillifname(struct ipt_entry *entry, const char *inifname, const char *outifname);

**参数**：

  - <span class="reference">entry</span> 要填充的规则条目。
  - <span class="reference">inifname</span> 输入设备名称，<span class="reference">NULL</span> 表示不变。
  - <span class="reference">outifname</span> 输出设备名称，<span class="reference">NULL</span> 表示不变。

## netlib\_ip6t\_commit

    int netlib_ip6t_commit(const struct ip6t_replace *repl);

**参数**：

  - <span class="reference">repl</span> 要提交的配置。

## netlib\_ip6t\_flush

    int netlib_ip6t_flush(const char *table, enum nf_inet_hooks hook);

**参数**：

  - <span class="reference">table</span> 表名。
  - <span class="reference">hook</span> 钩子点。

## netlib\_ip6t\_policy

    int netlib_ip6t_policy(const char *table, enum nf_inet_hooks hook, int verdict);

**参数**：

  - <span class="reference">table</span> 策略。
  - <span class="reference">hook</span> 钩子点。
  - <span class="reference">verdict</span> 判定值。

## netlib\_ip6t\_append

    int netlib_ip6t_append(struct ip6t_replace **repl, const struct ip6t_entry *entry, enum nf_inet_hooks hook);

**参数**：

  - <span class="reference">repl</span> 要提交的配置。
  - <span class="reference">entry</span> 要追加的规则条目。
  - <span class="reference">hook</span> 钩子点。

## netlib\_ip6t\_insert

    int netlib_ip6t_insert(struct ip6t_replace **repl, const struct ip6t_entry *entry, enum nf_inet_hooks hook, int rulenum);

**参数**：

  - <span class="reference">repl</span> 要提交的配置。
  - <span class="reference">entry</span> 要插入的规则条目。
  - <span class="reference">hook</span> 钩子点。
  - <span class="reference">rulenum</span> 规则编号。

## netlib\_ip6t\_delete

    int netlib_ip6t_delete(struct ip6t_replace *repl, const struct ip6t_entry *entry, enum nf_inet_hooks hook, int rulenum);

**参数**：

  - <span class="reference">repl</span> 要提交的配置。
  - <span class="reference">entry</span> 要删除的规则条目。
  - <span class="reference">hook</span> 钩子点。
  - <span class="reference">rulenum</span> 规则编号。

## netlib\_ip6t\_fillifname

    int netlib_ip6t_fillifname(struct ip6t_entry *entry, const char *inifname, const char *outifname);

**参数**：

  - <span class="reference">entry</span> 要填充的规则条目。
  - <span class="reference">inifname</span> 输入设备名称，<span class="reference">NULL</span> 表示不变。
  - <span class="reference">outifname</span> 输出设备名称，<span class="reference">NULL</span> 表示不变。

**连接检测**

## netlib\_check\_ipconnectivity

    int netlib_check_ipconnectivity(const char *ip, int timeout, int retry);

**参数**：

  - <span class="reference">ip</span> 要检查的 IPv4 地址。
  - <span class="reference">timeout</span> 超时时间。
  - <span class="reference">retry</span> 重试次数。

## netlib\_check\_ifconnectivity

    int netlib_check_ifconnectivity(const char *ifname, int timeout, int retry);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">timeout</span> 超时时间。
  - <span class="reference">retry</span> 重试次数。

**URL 解析**

## netlib\_parsehttpurl

    int netlib_parsehttpurl(const char *url, uint16_t *port, char *hostname, int hostlen, char *filename, int namelen);

**参数**：

  - <span class="reference">url</span> HTTP 相关参数。
  - <span class="reference">port</span> 指向 <span class="reference">uint16\_t</span>，用于存储解析出的端口号。
  - <span class="reference">hostname</span> 用于存储结果的缓冲区。
  - <span class="reference">hostlen</span> 缓冲区大小。
  - <span class="reference">filename</span> 用于存储结果的缓冲区。
  - <span class="reference">namelen</span> 缓冲区大小。

## netlib\_parseurl

    int netlib_parseurl(const char *str, struct url_s *url);

## netlib\_check\_httpconnectivity

    int netlib_check_httpconnectivity(const char *host, const char *getmsg, int port, int expect_code);

**参数**：

  - <span class="reference">host</span> 远程主机地址。
  - <span class="reference">getmsg</span> HTTP 相关参数。
  - <span class="reference">port</span> 端口号。
  - <span class="reference">expect\_code</span> HTTP 相关参数。

**其他**

## netlib\_get\_devices

    ssize_t netlib_get_devices(struct netlib_device_s *devlist, unsigned int nentries, sa_family_t family);

**参数**：

  - <span class="reference">devlist</span> 用于存储设备列表。
  - <span class="reference">nentries</span> 数组容量（条目数）。
  - <span class="reference">family</span> 地址族。 See AF\_\* definitions in

## netlib\_seteaddr

    int netlib_seteaddr(const char *ifname, const uint8_t *eaddr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">eaddr</span> 新地址。

## netlib\_getpanid

    int netlib_getpanid(const char *ifname, uint8_t *panid);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">panid</span> 用于存储当前 PAN ID

## netlib\_getproperties

    int netlib_getproperties(const char *ifname, struct pktradio_properties_s *properties);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">nodeadd</span> 用于存储节点地址。

## netlib\_setnodeaddr

    int netlib_setnodeaddr(const char *ifname, const struct pktradio_addr_s *nodeaddr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">nodeadd</span> 新地址。

## netlib\_getnodnodeaddr

    int netlib_getnodnodeaddr(const char *ifname, struct pktradio_addr_s *nodeaddr);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称
  - <span class="reference">nodeadd</span> 用于存储节点地址。

## netlib\_get\_nbtable

    ssize_t netlib_get_nbtable(struct neighbor_entry_s *nbtab, unsigned int nentries);

**参数**：

  - <span class="reference">nbtab</span> 用于存储邻居表副本
  - <span class="reference">nentries</span> 数组容量（条目数）。

## netlib\_icmpv6\_autoconfiguration

    int netlib_icmpv6_autoconfiguration(const char *ifname);

**参数**：

  - <span class="reference">ifname</span> 网络接口名称

## netlib\_parse\_conntrack

    int netlib_parse_conntrack(const struct nlmsghdr *nlh, size_t len, struct netlib_conntrack_s *ct);

**参数**：

  - <span class="reference">nlh</span> 要解析的 netlink 消息。
  - <span class="reference">ct</span> 连接跟踪条目。

## netlib\_get\_conntrack

    int netlib_get_conntrack(sa_family_t family, netlib_conntrack_cb_t cb);

**参数**：

  - <span class="reference">family</span> 地址族，用于过滤 conntrack 表项。
  - <span class="reference">cb</span> 连接跟踪条目。

## netlib\_listenon

    int netlib_listenon(uint16_t portno);

**参数**：

  - <span class="reference">portno</span> 端口号。

## netlib\_server

    void netlib_server(uint16_t portno, pthread_startroutine_t handler, int stacksize);

**参数**：

  - <span class="reference">portno</span> 端口号。
  - <span class="reference">handler</span> 任务入口函数。
  - <span class="reference">stacksize</span> 栈大小。

## netlib\_get\_iobinfo

    int netlib_get_iobinfo(struct iob_stats_s *iob);

**参数**：

  - <span class="reference">iob</span> IOB 信息结构体。

## netlib\_ipv4addrconv

    bool netlib_ipv4addrconv(const char *addrstr, uint8_t *addr);

将 IPv4 地址字符串（如 <span class="reference">"192.168.1.1"</span>）转换为 4 字节二进制数组。

**参数**：

  - <span class="reference">addrstr</span> IPv4 地址字符串。
  - <span class="reference">addr</span> 输出缓冲区（4 字节）。

**返回值**：

转换成功返回 <span class="reference">true</span>，格式非法时返回 <span class="reference">false</span>。

## netlib\_ethaddrconv

    bool netlib_ethaddrconv(const char *hwstr, uint8_t *hw);

将以太网 MAC 地址字符串（如 <span class="reference">"aa:bb:cc:dd:ee:ff"</span>）转换为 6 字节二进制数组。

**参数**：

  - <span class="reference">hwstr</span> MAC 地址字符串。
  - <span class="reference">hw</span> 输出缓冲区（6 字节）。

**返回值**：

转换成功返回 <span class="reference">true</span>，格式非法时返回 <span class="reference">false</span>。

## netlib\_saddrconv

    bool netlib_saddrconv(const char *hwstr, uint8_t *hw);

将 IEEE 802.15.4 短地址（2 字节）字符串转换为二进制形式。

**参数**：

  - <span class="reference">hwstr</span> 地址字符串。
  - <span class="reference">hw</span> 输出缓冲区（2 字节）。

**返回值**：

转换成功返回 <span class="reference">true</span>，格式非法时返回 <span class="reference">false</span>。

## netlib\_eaddrconv

    bool netlib_eaddrconv(const char *hwstr, uint8_t *hw);

将 IEEE 802.15.4 扩展地址（8 字节）字符串转换为二进制形式。

**参数**：

  - <span class="reference">hwstr</span> 地址字符串。
  - <span class="reference">hw</span> 输出缓冲区（8 字节）。

**返回值**：

转换成功返回 <span class="reference">true</span>，格式非法时返回 <span class="reference">false</span>。

## netlib\_nodeaddrconv

    bool netlib_nodeaddrconv(const char *addrstr,
                             struct pktradio_addr_s *nodeaddr);

将 pktradio 节点地址字符串转换为 <span class="reference">pktradio\_addr\_s</span> 结构体。

**参数**：

  - <span class="reference">addrstr</span> 节点地址字符串。
  - <span class="reference">nodeaddr</span> 输出结构体指针。

**返回值**：

转换成功返回 <span class="reference">true</span>，格式非法时返回 <span class="reference">false</span>。
