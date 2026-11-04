# 2026-11-04 两轮源码评审修复

基于官方合入点 `1d972493159c2a99825b68bc0c2ea2719125b5b8`，集中处理两轮评审的
3 项 P1 和 6 项 P2。保留原工作区未提交成果；不在此次修复中重写产品协调器、
更换 BLE 会话架构或修改设备签名身份。Android 修复版本为 code 59 /
`0.7.29-shaniu-finals-fixes`。

## 行为变化

| 问题 | 修复与失败语义 | 回归入口 |
| --- | --- | --- |
| 原生应用子目录被 RPMsgFS 拒绝 | 存储消费者和白名单共用目录定义，仅允许 schedule 和格式合法的存档路径 | `test_shaniu_nfc_reset_path.py` |
| Skill 首次保存中断锁住普通语音 | 没有已发布 `config.bin` 的目录视为未初始化；损坏的已发布记录仍报错 | `run-runtime-skills`、`run-runtime-skills-store` |
| 卸载留下定时槽位 | 先持久取消关联定时，再删除应用；取消失败不卸载；启动时清理历史孤儿任务 | `native_apps` |
| Android 下载堵住设备控制 | 文件工作与唯一 BLE 控制队列分离；商店下载增加 30 秒整体期限 | `NativeFeatureStoreTest`、App 构建 |
| 构建来源遗漏 Media | 记录实际 Media commit、未提交状态和源输入摘要，纳入构建前后比较 | `test_bk7258_build_workspace.py` |
| 分层检查遗漏 `.inc` | 将实际编译的 `.inc` 纳入扫描；违规包含夹具必须失败 | `test_bk7258_layers.py` |
| 历史读取失败覆盖旧快照 | 固定版本 Agent 的 Session 源在构建目录应用受校验补丁；仅 ENOENT 代表无历史，其他打开/读取/关闭错误向上传递 | `run-agent-session-history-memory` |
| 原生目录超过 4 KiB 断连 | kind 28 同步到正文 8191、读取偏移 8192、总长 8207；其他协议类型保持各自限制 | `DeviceControlProtocolTest`、`AgentDataControllerTest` |
| 31 字节应用 ID 超过目标 VFS 限制 | 使用 `state/<完整ID>`；可存在的旧短 ID 存档在新记录持久化成功后迁移，失败保留旧记录 | `native_apps`、真实 NuttX 路径检查夹具 |

卸载中若定时取消成功、随后目录保存失败，应用仍安装，但定时已取消；重试卸载安全。
提交结果不确定时暂停调度，重启装载持久记录后再决定。长 ID 不截断、不复用其他应用目录。

Agent 补丁保留官方实现及许可证，共享 `packages/ai_agent` checkout 不变。
构建校验原文件 SHA256；依赖源码发生漂移会停止配置，要求重新评审补丁。
本次编译使用清单锁定的 Media，未带入开发工作区中其他未提交录音修改。

## 验证边界

统一修复后通过：12 项文件系统/路径、分层及构建来源 Python 回归；原生应用、
Runtime Skills 及其存储、记忆事实、104 个头文件的 CP/AP/C11/C++17 检查；
真实 Agent Session 与加密持久快照联测。后者注入打开 EIO、读中 EIO 后检查
revision、长度、密文字节均未变化，解除故障后正常提交使 revision 加一。
前批次已通过且输入未变的其他主机检查沿用原结果，未重复全量清理构建。

Android `testDebugUnitTest`：292 通过、0 失败、9 跳过（外部 peer 条件）；
`assembleDebug`、`assembleDebugAndroidTest`、`lintDebug` 通过，lint 为 0 error /
90 warning。主机测试的源码补丁在隔离输出目录应用，避免 Git 在仓库子目录
静默忽略补丁；未修改官方依赖 checkout。

回归、固件/APK 构建、签名包验证、实际下载与启动分别记录。主机夹具不证明
真实 Flash 掉电恢复、BLE 射频行为或物理效果；原生应用签名安装仍受设备现有
publisher 配置约束，此次不放开安装信任。
