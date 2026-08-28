# AMS API

> 官方来源：[doc.openvela.com](https://doc.openvela.com/document?id=1714&version=dev-ai-contest-2026&language=cn)  
> 版本：dev-ai-contest-2026（中文）  
> 官方更新时间：2026-06-09 19:52:46  
> 本地拉取日期：2026-08-28

\[ [English](https://github.com/open-vela/docs/tree/dev-ai-contest-2026//https:/github.com/open-vela/docs/blob/dev/en/api/framework/services/ams.md) | 简体中文 \]

# AMS API

Activity Manager Service（AMS）是 openvela XMS 系统中的活动管理服务模块，负责管理应用的生命周期，以及任务和活动的调度。

# 功能特性

  - **Activity 生命周期管理**：AMS 负责管理应用内 Activity 的生命周期，包括创建、启动、暂停、恢复和销毁。
  - **任务管理**：AMS 管理应用任务和任务栈，包括任务切换和调度，确保流畅的用户体验。
  - **进程管理**：AMS 负责启动、停止和监控应用进程，确保系统资源的有效利用。
  - **Intent 处理**：AMS 处理应用间的 Intent 通信，允许不同应用启动 Activity 和 Service。
  - **权限管理**：AMS 参与权限检查，确保应用在启动 Activity 时满足系统安全要求。
  - **应用状态跟踪**：AMS 跟踪应用状态（如前台、后台、已停止），并据此分配资源。
  - **多窗口支持**：AMS 提供多窗口模式下的 Activity 管理，允许多个应用同时显示。
  - **后台任务限制**：AMS 对后台任务和服务施加限制，以优化系统性能和电池使用。
  - **Service 和 Broadcast 管理**：AMS 还负责管理 Service 和 BroadcastReceiver 的生命周期，确保系统的响应性和稳定性。

# 示例

以下是使用 openvela AMS 模块的示例代码，通常通过 <span class="reference">ActivityManager</span> 类来管理 Activity 和控制任务。

**启动新 Activity**  

    Intent intent;
    makeIntent(intent);
    intent.setFlag(intent.mFlag | Intent::FLAG_ACTIVITY_NEW_TASK);
    android::sp<android::IBinder> token = new android::BBinder();
    ActivityManager am;
    am.startActivity(token, intent, -1);

**停止 Activity**  

    Intent intent;
    makeIntent(intent);
    ActivityManager am;
    am.stopActivity(intent, intent.mFlag);

# 核心类

## ActivityManager

头文件：<span class="reference">\#include \<app/ActivityManager.h\></span>

客户端侧访问 AMS 能力的门面类。提供的主要方法：

  - <span class="reference">startActivity()</span> / <span class="reference">stopActivity()</span> / <span class="reference">finishActivity()</span> — Activity 启停
  - <span class="reference">startService()</span> / <span class="reference">stopService()</span> / <span class="reference">stopServiceByToken()</span> / <span class="reference">bindService()</span> / <span class="reference">unbindService()</span> — Service 操作
  - <span class="reference">publishService()</span> / <span class="reference">getService()</span> — 服务发布与获取
  - <span class="reference">sendBroadcast()</span> / <span class="reference">registerReceiver()</span> / <span class="reference">unregisterReceiver()</span> — 广播与接收器
  - <span class="reference">attachApplication()</span> / <span class="reference">stopApplication()</span> — Application 绑定与终止
  - <span class="reference">moveActivityTaskToBackground()</span> — Activity 任务切换到后台
  - <span class="reference">reportActivityStatus()</span> / <span class="reference">reportServiceStatus()</span> — 状态上报（由应用向 AMS 回报生命周期状态）
  - <span class="reference">postIntent()</span> — 向指定组件投递 Intent

## ActivityManagerService

头文件：<span class="reference">\#include \<am/ActivityManagerService.h\></span>

AMS 的服务端实现类，注册为系统服务，接收各应用通过 Binder 发来的调用并执行调度。开发者一般不直接使用该类。

## Activity

头文件：<span class="reference">\#include \<app/Activity.h\></span>

应用开发的 UI 单元基类。应用通过继承该类并重写 <span class="reference">onCreate</span> / <span class="reference">onStart</span> / <span class="reference">onResume</span> / <span class="reference">onPause</span> / <span class="reference">onStop</span> / <span class="reference">onDestroy</span> / <span class="reference">onRestart</span> 等生命周期回调来实现一个界面。还提供 <span class="reference">finish</span> / <span class="reference">setResult</span> / <span class="reference">getWindow</span> / <span class="reference">moveToBackground</span> / <span class="reference">onBackPressed</span> / <span class="reference">onActivityResult</span> / <span class="reference">onNewIntent</span> 等操作与扩展点。

## Application

头文件：<span class="reference">\#include \<app/Application.h\></span>

应用进程的全局单例基类。应用通常继承 <span class="reference">Application</span> 来放置进程级资源。主要方法包括：

  - 生命周期：<span class="reference">onCreate</span> / <span class="reference">onDestroy</span> / <span class="reference">onForeground</span> / <span class="reference">onBackground</span> / <span class="reference">onReceiveIntent</span>
  - 组件管理：<span class="reference">createActivity</span> / <span class="reference">createService</span> / <span class="reference">addActivity</span> / <span class="reference">addService</span> / <span class="reference">findActivity</span> / <span class="reference">findService</span> / <span class="reference">deleteActivity</span> / <span class="reference">deleteService</span>
  - 元信息：<span class="reference">getPackageName</span> / <span class="reference">getUid</span> / <span class="reference">isSystemUI</span> / <span class="reference">getMainLoop</span> / <span class="reference">getWindowManager</span>

## ApplicationThread

头文件：<span class="reference">\#include \<app/ApplicationThread.h\></span>

Application 侧的调度线程抽象，承接来自 AMS 的调度请求并在应用进程内派发执行。属于框架内部协作类，应用开发者一般不直接调用。

## AppMain

头文件：<span class="reference">\#include \<app/AppMain.h\></span>

应用进程入口辅助类。定义应用进程从启动到接入 AMS 的基础流程，封装主事件循环与初始化步骤。

## Context

头文件：<span class="reference">\#include \<app/Context.h\></span>

最核心的上下文基类，提供系统能力访问入口。典型方法包括：

  - <span class="reference">getPackageName()</span> / <span class="reference">getApplication()</span> / <span class="reference">getComponentName()</span> — 应用与组件信息
  - <span class="reference">startActivity()</span> / <span class="reference">startActivityForResult()</span> / <span class="reference">stopActivity()</span> — Activity 启停
  - <span class="reference">startService()</span> / <span class="reference">stopService()</span> / <span class="reference">bindService()</span> / <span class="reference">unbindService()</span> — Service 操作
  - <span class="reference">sendBroadcast()</span> / <span class="reference">registerReceiver()</span> / <span class="reference">unregisterReceiver()</span> — 广播与接收器
  - <span class="reference">getActivityManager()</span> / <span class="reference">getWindowManager()</span> — 系统服务访问
  - <span class="reference">getMainLoop()</span> / <span class="reference">getCurrentLoop()</span> — 事件循环获取

## ContextImpl

头文件：<span class="reference">\#include \<app/ContextImpl.h\></span>

<span class="reference">Context</span> 基类的默认实现，由框架在 Application / Activity / Service 创建时装配。应用开发者通常不直接构造 <span class="reference">ContextImpl</span>，而是通过 <span class="reference">Activity::getContext()</span> 等方式获取实例。

## Intent

头文件：<span class="reference">\#include \<app/Intent.h\></span>

承载组件间通信意图的数据结构。包含 action、data、target、bundle、flag 等字段，以及 <span class="reference">FLAG\_ACTIVITY\_\*</span> 等启动标志。提供 <span class="reference">setAction</span> / <span class="reference">setData</span> / <span class="reference">setTarget</span> / <span class="reference">setBundle</span> / <span class="reference">setFlag</span> / <span class="reference">readFromParcel</span> / <span class="reference">writeToParcel</span> 等读写方法。

## Service

头文件：<span class="reference">\#include \<app/Service.h\></span>

无界面的长生命周期组件基类。开发者通过继承 <span class="reference">Service</span> 并重写 <span class="reference">onCreate</span> / <span class="reference">onStartCommand</span> / <span class="reference">onBind</span> / <span class="reference">onUnbind</span> / <span class="reference">onDestroy</span> / <span class="reference">onReceiveIntent</span> 来实现后台服务。

## ServiceConnection

头文件：<span class="reference">\#include \<app/ServiceConnection.h\></span>

<span class="reference">bindService</span> 的连接回调接口。包含 <span class="reference">onServiceConnected</span> / <span class="reference">onServiceDisconnected</span> 两个回调方法，用于在绑定成功或断开时通知客户端。

## BroadcastReceiver

头文件：<span class="reference">\#include \<app/BroadcastReceiver.h\></span>

广播接收器基类。应用通过继承该类并重写 <span class="reference">onReceive(Intent)</span> 来处理匹配到的系统或应用广播。

## MessageService

头文件：<span class="reference">\#include \<app/MessageService.h\></span>

面向消息通信的服务辅助类，封装基于 Intent 的请求—响应模式，便于应用构建基于消息分发的后台服务。主要方法：<span class="reference">sendMessage</span> / <span class="reference">receiveMessage</span> / <span class="reference">receiveMessageAndReply</span> / <span class="reference">reply</span> / <span class="reference">onBind</span> / <span class="reference">onBindExt</span> / <span class="reference">onReply</span>。

## Dialog

头文件：<span class="reference">\#include \<app/Dialog.h\></span>

对话框组件基类。提供 <span class="reference">show</span> / <span class="reference">hide</span> / <span class="reference">setLayout</span> / <span class="reference">setRect</span> / <span class="reference">getLayout</span> / <span class="reference">getRoot</span> / <span class="reference">createDialog</span> 等操作，应用可继承实现自定义对话框。

## UvLoop

头文件：<span class="reference">\#include \<app/UvLoop.h\></span>

基于 libuv 的事件循环封装，供应用主线程以及其他框架组件复用。提供定时器、IO 事件、工作队列等能力。

## Logger

头文件：<span class="reference">\#include \<app/Logger.h\></span>

AMS/应用侧通用日志宏定义，封装分级日志输出（<span class="reference">APP\_LOGI</span> / <span class="reference">APP\_LOGW</span> / <span class="reference">APP\_LOGE</span> 等）。

## ActivityTrace

头文件：<span class="reference">\#include \<ActivityTrace.h\></span>

Activity 生命周期的 trace 打点宏集合，配合 openvela trace 分析工具可视化应用启动与切换路径。
