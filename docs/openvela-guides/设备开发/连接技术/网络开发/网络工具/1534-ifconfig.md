# ifconfig

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1534&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:51:08  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/device_dev_guide/connection/network/network_tools/ifconfig.md) | 简体中文 \]

# 一、概述

本命令主要用于对网卡进行如下配置：

  - 设置 ip 地址
  - 设置网络掩码
  - 设置网关地址
  - 设置 mac 地址
  - 查看网络接口的状态

# 二、前提条件

使用 ifconfig 命令需要在编译时开启网络支持和 proc 文件系统功能，可按如下步骤打开配置：

1.  切换到 openvela 仓库的根目录，编译时使用 <span class="reference">menuconfig</span> 打开图形化配置界面，执行如下命令：  
    
        ./build.sh vendor/openvela/boards/vela/configs/goldfish-armeabi-v7a-ap menuconfig

2.  打开界面后输入 <span class="reference">/</span>，分别搜索以下五个配置项并修改为如下配置：  
    
        CONFIG_NET=y
        CONFIG_NETDEV_STATISTICS=y
        CONFIG_FS_PROCFS=y
        CONFIG_FS_PROCFS_EXCLUDE_NET=n
        CONFIG_NSH_DISABLE_IFCONFIG=n

# 三、参数说明

> **说明**：openvela 的 ifconfig 和 Linux 的 ifconfig 有部分差异，暂时不支持 \[-v\] \[-a\] \[-s\] 等参数。  
> 
>     ifconfig interface [[inet|inet6] [<ip-address>|dhcp]] [dr|gw|gateway <dr-address>] [netmask <net-mask>|prefixlen <len>] [dns <dns-address>] [hw <hw-mac>]]

<table>
<colgroup>
<col style="width: 50%" />
<col style="width: 50%" />
</colgroup>
<thead>
<tr class="header">
<th>参数</th>
<th>描述</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>interface</td>
<td>接口的名称。<br />
这通常是一个驱动程序名，后面跟着一个单元号。例如用于第一个以太网接口的 eth0，Wi-Fi 类型的网卡一般为 wlan0。</td>
</tr>
<tr class="even">
<td>inet/inet6</td>
<td>选定地址族，与地址分配联合使用。</td>
</tr>
<tr class="odd">
<td>\&lt;ip-address&gt;|dhcp</td>
<td>直接指定网卡静态 IP 地址或通过 DHCP 获取动态地址。</td>
</tr>
<tr class="even">
<td>dr|gw|gateway \&lt;dr-address&gt;</td>
<td>设置网关地址。</td>
</tr>
<tr class="odd">
<td>netmask \&lt;net-mask&gt;|prefixlen \&lt;len&gt;</td>
<td>设置此接口的 IP 网络掩码。<br />
此值默认为通常的 A、B 或 C 类网络掩码（从接口 IP 地址派生），但可以设置为任何值。</td>
</tr>
<tr class="even">
<td>dns \&lt;dns-address&gt;</td>
<td>设置 DNS。</td>
</tr>
<tr class="odd">
<td>hw \&lt;hw-mac&gt;</td>
<td>如果设备驱动程序支持此操作，则设置此接口的硬件地址。</td>
</tr>
</tbody>
</table>

# 四、常用命令

  - ifup & ifdown 命令通常与 ifconfig 搭配使用，用以使能或关闭网卡。  
    
        # 使能eth0网卡
        ifup eth0  
        
        # 关闭eth0网卡  
        ifdown eth0

  - 不带选项的 ifconfig 命令将显示所有接口的配置。  
    
        ifconfig

  - 显示指定（如 eth0 ）接口的配置。  
    
        ifconfig eth0

  - 配置 eth0 静态 ip 为 10.0.1.3。  
    
        ifconfig eth0 10.0.1.3

  - 配置 eth0 静态 ip 为10.0.1.3，网关为 10.0.1.1 ，掩码地址为 255.255.255.0，dns 为 8.8.8.8。  
    
        ifconfig eth0 10.0.1.3 gateway 10.0.1.1 netmask 255.255.255.0 dns 8.8.8.8

  - 配置 eth0 静态 ipv6 地址为 2001:db8::，掩码 32 位。  
    
        ifconfig eth0 inet6 add 2001:db8::/32
