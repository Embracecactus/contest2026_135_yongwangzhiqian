# PMS API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1715&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:47  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/services/pms.md) | 简体中文 \]

# PMS API

Package Manager Service（PMS）是 openvela XMS 系统中的包管理模块。

# 功能特性

  - 提供包安装功能
  - 提供包信息查询能力
  - 提供包卸载能力

# 示例

**通过命令行进行包管理**

安装包：  

    pm install [packagename]

查询已安装的包：  

    pm list

**通过源码使用包管理工具**

安装包：  

    #include <pm/PackageManager.h>
    
    PackageManager pm;
    InstallParam parms;
    pm.installPackage(parms);

获取所有包信息：  

    #include <pm/PackageManager.h>
    
    PackageManager pm;
    std::vector<PackageInfo> pgInfos;
    pm.getAllPackageInfo(&pgInfos);

卸载包：  

    #include <pm/PackageManager.h>
    
    PackageManager pm;
    UninstallParam parms;
    pm.uninstallPackage(parms);

# 核心类

## PackageManager

头文件：<span class="reference">\#include \<pm/PackageManager.h\></span>

客户端侧访问 PMS 能力的门面类。提供的主要操作：

  - <span class="reference">installPackage(InstallParam)</span> — 安装应用包
  - <span class="reference">uninstallPackage(UninstallParam)</span> — 卸载应用包
  - <span class="reference">getAllPackageInfo(std::vector\<PackageInfo\>\*)</span> — 查询所有已安装包信息
  - <span class="reference">getPackageInfo(packageName, PackageInfo\*)</span> — 查询指定包信息
  - <span class="reference">getAllPackageName(std::vector\<std::string\>\*)</span> — 查询所有已安装包名
  - <span class="reference">getPackageSizeInfo(packageName, ...)</span> — 查询包占用空间
  - <span class="reference">clearAppCache(packageName)</span> — 清理应用缓存
  - <span class="reference">isFirstBoot()</span> — 查询是否首次启动

应用通常构造 <span class="reference">PackageManager</span> 实例后直接调用上述方法，内部通过 Binder 与 <span class="reference">PackageManagerService</span> 通信。

## PackageManagerService

头文件：<span class="reference">\#include \<pm/PackageManagerService.h\></span>

PMS 的服务端实现类，注册为系统服务。负责维护已安装包的元数据、执行实际的安装/卸载动作、处理权限与签名校验。开发者一般不直接使用该类。

## PackageInfo

头文件：<span class="reference">\#include \<pm/PackageInfo.h\></span>

描述单个安装包元数据的结构。主要字段包括：

  - <span class="reference">packageName</span> / <span class="reference">name</span> — 包名与应用名
  - <span class="reference">version</span> / <span class="reference">priority</span> / <span class="reference">appType</span> — 版本、优先级与应用类型
  - <span class="reference">installedPath</span> / <span class="reference">installTime</span> / <span class="reference">size</span> — 安装路径、安装时间与占用大小
  - <span class="reference">execfile</span> / <span class="reference">entry</span> / <span class="reference">manifest</span> — 可执行文件、入口与清单
  - <span class="reference">activitiesInfo</span> / <span class="reference">servicesInfo</span> — 内部 Activity 与 Service 列表
  - <span class="reference">shasum</span> — 签名摘要
  - <span class="reference">userId</span> / <span class="reference">isSystemUI</span> — 用户 ID 与是否系统 UI
  - <span class="reference">windowEnterAnim</span> / <span class="reference">windowExitAnim</span> — 窗口进入/退出动画配置

<span class="reference">PackageManager</span> 的查询接口返回该类型的结果。

## PackageTrace

头文件：<span class="reference">\#include \<PackageTrace.h\></span>

PMS 的 trace 打点宏集合，用于跟踪包管理操作路径，配合 openvela trace 工具分析性能。
