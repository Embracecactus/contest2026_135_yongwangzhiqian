# Feature Main Export API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1722&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:51  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/feature/feature_framework_main_export.md) | 简体中文 \]

# Feature Main Export API

Feature 管理器（Feature Manager）的生命周期管理与全局配置接口。主要用于快应用框架初始化、绑定运行时事件循环、注册 Feature 以及管理权限。

头文件：<span class="reference">\#include \<feature\_main\_exports.h\></span>

# openvela 实现说明

  - **使用场景**：这组 API 主要由快应用框架实现者（Runtime 整合层）使用，Feature 插件开发者一般不直接调用
  - **与 Feature 管理器的关系**：一个 Feature 管理器对应一个独立的快应用实例，通过 <span class="reference">FeatureCreateManager</span> 创建，使用结束后必须调用 <span class="reference">FeatureFreeManager</span> 释放
  - **事件循环集成**：通过 <span class="reference">FeatureSetUVLoop</span> 绑定 libuv 事件循环，实现异步任务的调度。必须在 <span class="reference">FeatureCreateInstance</span> 之前完成绑定
  - **权限回调机制**：通过 <span class="reference">FeatureSetPermissionsCallback</span> 注册统一的权限检查入口，所有需要权限的 Feature 调用都会触发回调，调用方需显式 <span class="reference">Grant</span> 或 <span class="reference">Reject</span>

# 快应用框架示例代码

    #ifdef CONFIG_FEATURE_FRAMEWORK
        FeatureManagerCreateInfo ft_info;
        ft_info.raw_ctx = (FeatureRawContextHandle)(qrt->env.ctx);
        ft_info.release_cb = nullptr;
        ft_info.manager_type = FEATURE_MANAGER_JS;
        ft_info.package_name = app->packageName();
        qrt->pFeatureMgr = FeatureCreateManager(&ft_info);
        FeatureSetArgsErrorCb(qrt->pFeatureMgr, on_feature_args_error, qrt);
        FeatureSetManagerUserData(qrt->pFeatureMgr, "app", app);
        FeatureSetUVLoop(qrt->pFeatureMgr, qrt->loop);
    #endif

# 管理器生命周期

## FeatureCreateManager

    FeatureManagerHandle FeatureCreateManager(FeatureManagerCreateInfo* pinfo);

根据给定的配置信息创建一个 Feature 管理器实例。

**参数**：

  - <span class="reference">pinfo</span> Feature 管理器的创建配置，包含原始运行时上下文、释放回调、管理器类型和快应用包名。详见 <span class="reference">FeatureManagerCreateInfo</span>。

**返回值**：

成功时返回有效的 <span class="reference">FeatureManagerHandle</span> 句柄；失败时返回 <span class="reference">NULL</span>。

## FeatureFreeManager

    void FeatureFreeManager(FeatureManagerHandle handle);

释放 Feature 管理器。释放前应先调用 <span class="reference">FeatureUnsetUVLoop</span> 解绑事件循环。

**参数**：

  - <span class="reference">handle</span> 待释放的 Feature 管理器句柄。

## FeatureUninit

    void FeatureUninit(FeatureManagerHandle handle);

对 Feature 管理器执行反初始化操作。清理内部状态但不释放句柄本身。

**参数**：

  - <span class="reference">handle</span> Feature 管理器句柄。

# 全局配置

## FeatureSetArgsErrorCb

    void FeatureSetArgsErrorCb(FeatureManagerHandle handle, ArgsErrorCb cb, void* data);

为 Feature 管理器注册参数错误回调。当任一 Feature 调用的参数类型不匹配时，会触发该回调。

**参数**：

  - <span class="reference">handle</span> Feature 管理器句柄。
  - <span class="reference">cb</span> 参数错误回调，签名为 <span class="reference">bool (\*)(void\* data, ArgsErrorInfo\* args\_info)</span>。
  - <span class="reference">data</span> 传递给回调的用户数据。

## FeatureSetPackageVersion

    void FeatureSetPackageVersion(FeatureManagerHandle handle, const char* package_version);

设置当前管理器对应快应用的包版本号。版本号可通过 <span class="reference">FeatureGetPackageVersion</span> 查询。

**参数**：

  - <span class="reference">handle</span> Feature 管理器句柄。
  - <span class="reference">package\_version</span> 快应用版本号字符串。

## FeatureSetUVLoop

    void FeatureSetUVLoop(FeatureManagerHandle handle, uv_loop_t* loop);

为 Feature 管理器绑定 libuv 事件循环。所有 <span class="reference">FeaturePost</span>、<span class="reference">FeatureWorker\*</span> 等异步任务都会在该 loop 上调度。

**参数**：

  - <span class="reference">handle</span> Feature 管理器句柄。
  - <span class="reference">loop</span> libuv 事件循环指针。

**注意**：

  - 必须在 <span class="reference">FeatureCreateInstance</span> 之前调用。
  - 同一个 <span class="reference">uv\_loop\_t</span> 可以被多个 Feature 管理器共享，但通常建议每个快应用实例独占一个 loop。

## FeatureUnsetUVLoop

    void FeatureUnsetUVLoop(FeatureManagerHandle handle);

解绑 Feature 管理器的 libuv 事件循环。解绑后所有未完成的异步任务将失效。

**参数**：

  - <span class="reference">handle</span> Feature 管理器句柄。

**注意**：

  - 必须在 <span class="reference">FeatureFreeManager</span> 之前调用。

# 运行时访问

## FeatureManagerGetContext

    ft_context_ref FeatureManagerGetContext(FeatureManagerHandle handle);

从 Feature 管理器获取对应的 Feature 上下文引用，可用于 <span class="reference">ft\_value\_t</span> 相关操作。

**参数**：

  - <span class="reference">handle</span> Feature 管理器句柄。

**返回值**：

返回 <span class="reference">ft\_context\_ref</span>，失败时返回 <span class="reference">NULL</span>。

## FeatureSetManagerUserData

    void FeatureSetManagerUserData(FeatureManagerHandle handle, const char* name, void* data);

按名称在 Feature 管理器上挂载用户数据。可用于在各个 Feature 实例之间共享信息。

**参数**：

  - <span class="reference">handle</span> Feature 管理器句柄。
  - <span class="reference">name</span> 用户数据名称（键）。
  - <span class="reference">data</span> 用户数据指针。

## FeatureHasFeature

    bool FeatureHasFeature(FeatureManagerHandle handle, FtString feature_method);

判断给定名称的 Feature 是否已注册到当前管理器。

**参数**：

  - <span class="reference">handle</span> Feature 管理器句柄。
  - <span class="reference">feature\_method</span> 要查询的 Feature 名称。

**返回值**：

Feature 已注册时返回 <span class="reference">true</span>，否则返回 <span class="reference">false</span>。

# Feature 操作

## FeatureRequire

    ft_value_t FeatureRequire(FeatureManagerHandle handle,
                              ft_value_t binding_obj, const char* name);

按名称向 Feature 管理器请求一个 Feature 实例。等价于 JS 层的 <span class="reference">require('@system.xxx')</span>。

**参数**：

  - <span class="reference">handle</span> Feature 管理器句柄。
  - <span class="reference">binding\_obj</span> 绑定对象（通常是 Feature 所在的 JS 全局对象）。
  - <span class="reference">name</span> Feature 名称。

**返回值**：

返回封装了 Feature 实例的 <span class="reference">ft\_value\_t</span>。失败时返回 undefined 类型的 <span class="reference">ft\_value\_t</span>。

**注意**：

  - 每次 <span class="reference">FeatureRequire</span> 都会产生一个独立的 Feature 实例。

## FeatureFindFeature

    ft_value_t FeatureFindFeature(FeatureManagerHandle handle, const char* name);

查找已创建的 Feature 实例而不会新建实例。

**参数**：

  - <span class="reference">handle</span> Feature 管理器句柄。
  - <span class="reference">name</span> Feature 名称。

**返回值**：

返回 Feature 实例对应的 <span class="reference">ft\_value\_t</span>；若未找到，返回 undefined。

## FeatureCreateFeature

    ft_value_t FeatureCreateFeature(FeatureManagerHandle handle,
                                    ft_value_t prototype, ft_value_t binding_obj);

根据原型创建一个 Feature 实例。用于需要直接操作原型对象的高级场景。

**参数**：

  - <span class="reference">handle</span> Feature 管理器句柄。
  - <span class="reference">prototype</span> Feature 原型对象。
  - <span class="reference">binding\_obj</span> 绑定对象。

**返回值**：

成功时返回新建 Feature 实例的 <span class="reference">ft\_value\_t</span>；失败时返回 undefined。

# 内存诊断

## FeatureDumpMemory

    void FeatureDumpMemory(FeatureManagerHandle feature_manager,
                           FeatureMemoryDump* dump, void* userdata);

回调式的 Feature 框架内存占用诊断接口，便于上层整合自定义的内存统计能力。

**参数**：

  - <span class="reference">feature\_manager</span> Feature 管理器句柄。
  - <span class="reference">dump</span> 内存诊断回调结构体，包含 <span class="reference">count</span>、<span class="reference">count\_meta</span>、<span class="reference">sub</span> 三类回调，详见 <span class="reference">FeatureMemoryDump</span>。
  - <span class="reference">userdata</span> 透传给各回调的用户数据。

# 权限管理

## FeatureSetPermissionsCallback

    void FeatureSetPermissionsCallback(FeatureManagerHandle hmanager,
                                       FeaturePermissionsCb cb, void* data);

注册权限检查回调。当某个 Feature API 需要权限时，框架会触发此回调，由业务层决定授予或拒绝。

**参数**：

  - <span class="reference">hmanager</span> Feature 管理器句柄。
  - <span class="reference">cb</span> 权限检查回调，签名为 <span class="reference">void (\*)(FeaturePermissionsHandle, const FeaturePermissionsInfo\*, void\*)</span>。
  - <span class="reference">data</span> 透传给回调的用户数据。

**注意**：

  - 回调内必须调用 <span class="reference">FeatureGrantPermissions</span> 或 <span class="reference">FeatureRejectPermissions</span> 之一，否则对应的 Feature 调用会一直挂起。

## FeatureGrantPermissions

    void FeatureGrantPermissions(FeatureManagerHandle hmanager,
                                 FeaturePermissionsHandle handle);

授予一次权限请求。调用后，对应的 Feature API 调用会继续执行。

**参数**：

  - <span class="reference">hmanager</span> Feature 管理器句柄。
  - <span class="reference">handle</span> 权限请求句柄（由权限回调传入）。

## FeatureRejectPermissions

    void FeatureRejectPermissions(FeatureManagerHandle hmanager,
                                  FeaturePermissionsHandle handle,
                                  FeaturePermsRejectReason reason);

拒绝一次权限请求。调用后，对应的 Feature API 调用会返回权限错误。

**参数**：

  - <span class="reference">hmanager</span> Feature 管理器句柄。
  - <span class="reference">handle</span> 权限请求句柄。
  - <span class="reference">reason</span> 拒绝原因，详见 <span class="reference">FeaturePermsRejectReason</span>：
      - <span class="reference">FEATURE\_PERMS\_DENIED</span>：权限被拒绝
      - <span class="reference">FEATURE\_PERMS\_ERROR</span>：权限检查错误
      - <span class="reference">FEATURE\_PERMS\_NO\_BG</span>：不允许后台调用
