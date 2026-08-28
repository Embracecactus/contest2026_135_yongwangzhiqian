# FTP 服务器 API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1669&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:20  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/network/net_ftp.md) | 简体中文 \]

# FTP 服务器 API

简单的 FTP 服务器接口，提供用户管理和会话处理能力。

头文件：<span class="reference">\#include \<netutils/ftpd.h\></span>

# openvela 实现说明

  - **适用场景**：IoT 设备调试、固件上传、文件下载等轻量 FTP 应用
  - **配置依赖**：需启用 <span class="reference">CONFIG\_NETUTILS\_FTPD</span>
  - **用户管理**：通过 <span class="reference">ftpd\_adduser</span> 添加用户与权限
  - **会话模型**：<span class="reference">ftpd\_session</span> 为一个客户端连接提供会话处理，通常在独立线程中调用

# FTP 服务器

头文件：<span class="reference">\#include \<netutils/ftpd.h\></span>

## ftpd\_open

    FTPD_SESSION ftpd_open(int port, sa_family_t family);

创建 FTP 服务器会话。

**参数**：

  - <span class="reference">port</span> 监听端口（通常为 21）。
  - <span class="reference">family</span> 地址族（<span class="reference">AF\_INET</span> 或 <span class="reference">AF\_INET6</span>）。

**返回值**：

成功时返回会话句柄。

## ftpd\_adduser

    int ftpd_adduser(FTPD_SESSION handle, uint8_t accountflags,
                     const char *user, const char *passwd, const char *home);

添加 FTP 用户。

**参数**：

  - <span class="reference">handle</span> 由 <span class="reference">ftpd\_open()</span> 返回的句柄。
  - <span class="reference">accountflags</span> 用户属性标志（参见 <span class="reference">FTPD\_ACCOUNTFLAGS\_\*</span>）。
  - <span class="reference">user</span> 用户名（<span class="reference">NULL</span> 表示无需登录）。
  - <span class="reference">passwd</span> 密码（<span class="reference">NULL</span> 表示无需密码）。
  - <span class="reference">home</span> 用户主目录。

## ftpd\_session

    int ftpd_session(FTPD_SESSION handle, int timeout);

运行 FTP 服务器会话，等待并处理一个客户端连接。

**参数**：

  - <span class="reference">handle</span> 会话句柄。
  - <span class="reference">timeout</span> 等待连接的超时时间（毫秒），0 表示无限等待。

## ftpd\_close

    void ftpd_close(FTPD_SESSION handle);

关闭 FTP 服务器会话。
