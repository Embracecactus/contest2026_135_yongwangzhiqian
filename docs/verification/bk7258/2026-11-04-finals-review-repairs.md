# 2026-11-04 三轮源码评审修复

基于官方合入点 `1d972493159c2a99825b68bc0c2ea2719125b5b8`，集中处理三轮评审的
3 项 P1 和 8 项 P2。保留原工作区未提交成果；不在此次修复中重写产品协调器、
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
| 上传中断遗留文件阻塞后续安装 | 新作业取得独占挂载资源、且没有活跃描述符后恢复固定 staging 残留；活动上传和关闭结果未知的作业不清理 | `run-display-upload`、真实 job 中断恢复变体 |
| 取消后同组子进程继续运行 | POSIX 进程组独立确认退出，直属父进程先回收；TERM 后仍存活则有界 KILL，无法确认时返回 `cleanup_unconfirmed` | `test_workbench_task_runner.py` |

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

前两轮的 776 固件以修复提交 `9f18545705f2b03f9c6e9297f52e1fa07fd6db5a` 编译，来源为 clean；
独立工作区按清单固定 NuttX `892be7ba`、apps `d529ec81`、Agent `76c65bc6`、
Media `eee16420`。AIDK `app / openvela_ap` 的 CP、AP、BL1、BL2 编译通过，
744 个源输入摘要为 `a90d2a8442ad156c6622fe3cb754265665e486bb263e967de401b2f2a095ae53`。
当时后续仅为文档更新，未改变该固件源输入；文末更正记录了其后的源码变化。

第三轮新增 2 个存储中断恢复检查、23 个显示作业/worker/控制变体以及 10 个
任务运行器用例通过。真实子进程先安装忽略 SIGTERM 的处理器，再发就绪信号；
覆盖父进程先退出、请求取消和超时，检查子进程最终消失。Windows 保留既有
父进程终止路径，本轮不声称验证了 Windows 子进程树清理。

包含全部十一项修复的最终候选包为 `0.7.131+777`，固件源提交为
`e99d443ad03ac523003f7e818ab7603b0e0d3e1c`，构建时来源为 clean，744 个输入摘要为
`f33f80fb9c571e4e8e0bdb4570f9d6937b07ccc663040961267a6d4c561eb2a6`。
沿用上述依赖清单，增量编译通过；build-manifest 重新哈希、包结构、CP/AP 签名、
板型和版本/计数检查均通过。只生成 CP/AP 签名更新包，沿用设备既有 MCUboot
公钥身份；没有重新签发或部署启动组件。

App 59 从 `9f18545705f2b03f9c6e9297f52e1fa07fd6db5a` 构建；第三轮提交没有改动
Android 输入，因此沿用已通过测试的 APK。以下均为本地生成产物，不表示已发布或已实机验收。

| 产物 | 大小（字节） | SHA256 |
| --- | ---: | --- |
| App 59 debug APK | 16305060 | `7fa83ecb4d0ce52d03f27518ba947098970cf44df59771ce92539875c353c2bc` |
| 777 签名 OTA 候选包（全部修复） | 3417062 | `8a702412ed174a8aa5d329cdb536d2a8752dc624cd09aa8f81d457e61d72824c` |
| 776 中间包（启动回退，保留诊断） | 3417063 | `5579fd6658e543ca02a18611d208421cb8aa0707ffee306f54d89956dc8be670` |

## 设备检查

首次尝试前开发板软件复位通过。设备身份与现有记录一致，775 A 槽已确认；设备报告的
非活动 B 槽范围与包内 CP/AP 镜像大小一致。经包及签名校验后，仅写入这两段，
下载器所有成功标记通过，未写启动区或用户数据。

**776 未通过启动验收**：后续日志显示启动器回到 A 槽，状态仍为
`pair=confirmed version=0.7.129+775 counter=775`。回退原因尚未确认；传输成功
不能作为新固件运行成功。当时用户要求先完成代码和包，设备操作暂停。

随后用户授权全量烧录，并明确不需要新的 Flash 回读或备份。现行根/板 AGENTS、
SOP、工具说明和板级说明已移除对应前置要求，也已同步这些规则到原工作区。
本次使用用户保留的同板原厂 8 MiB 文件，不读取当前 Flash，不更换签名身份。
原厂资料在当前布局的 `usr_config`、`persistent_data` 和 `factory_state` 范围均为
擦除字节；不把它描述成当前产品配置的恢复副本。整片镜像的设备校准尾部与该资料
逐字节相同，未操作 OTP/eFuse，也未触发存储卡格式化。

全量包从 `73812414ac8c07eae8d683dc21bb94322e7d9437` 构建，运行代码仍为上述
十一项修复；当时后续提交只改文档，文末更正不追溯改变这一构建记录。编译的 rollback floor 为 777，版本为 `0.7.131+777`，
沿用既有 BL1/MCUboot 公钥身份。8 MiB BIN 的 SHA256 为
`18c53e13a74022cf126c3b8d1fc717483c5ce1f77ac8e3c7888fe5e413bda102`。

COM22 的整片写入从北京时间 17:14:07 至 17:17:28，BK Loader 全部成功标记通过。
随后实际设备报告：

```text
pair=confirmed version=0.7.131+777 counter=777
ap state=2 error=0
cpu2 state=8 error=0 ready=1 online=00000003
supervisor faults=0 recoveries=0
```

设备身份与写入前一致，捕获窗口没有 AP_EXCEPTION、HardFault 或 PANIC。
**777 已完成全量下载与启动确认**；此结果未确定 776 非活动槽回退的具体原因。
当前语音日志仍报告 `configuration unavailable result=-11`，语音配置与完整功能
验收尚未完成。本轮未进行 App OTA 或物理手机安装验收。

回归、固件/APK 构建、签名包验证、实际下载与启动分别记录。主机夹具不证明
真实 Flash 掉电恢复、BLE 射频行为或物理效果；原生应用签名安装仍受设备现有
publisher 配置约束，此次不放开安装信任。

## 补充修复：历史导出与资源发布中断

以下为 `8d03d755` 之后的工作树修复，不包含在上文 777 固件及其启动结果中。

- 历史导出：扩展同一固定版本 Session 补丁，JSON 解析、组装或序列化失败均返回
  错误，不再跳过失败记录或以空历史继续提交。使用产品实际的 `reply=NULL`，对
  本次真实 Session 导出检测到的 67 个 cJSON 分配点逐一注入单次失败；每次均确认
  原持久快照的 revision、长度和密文不变。打开/读取 EIO、最终序列化失败及解除
  故障后的正常提交也通过。官方依赖原文件的 SHA-256 保持不变。
- 资源上传：校验后的 `.upload.bkep` 直接重命名到安装目录，取消具名 staging
  中间状态。旧版本留下的具名 staging 文件不删除、不覆盖，也不阻塞新上传。
  新增两个回归在旧实现失败、修复后通过：旧 staging 残留下重试安装，以及发布
  重命名后进程中断时仍能查询已安装包、重复上传不会覆盖它；默认选择保持独立。

验证：`make -C tests/host/bk7258 check-review-regressions` 通过；资源上传、显示
存储及作业共 21 个用例通过。`verify layers` 扫描 623 个源文件并通过；
`git diff --check` 通过。NuttX `892be7ba7dc` 的 nxstyle 对显示存储文件仍报告
53 条原有诊断，按改动前后行映射核对无新增，本次没有扩大为全文件样式清理。

复用现有 AIDK AP 配置、SDK、工具链及公开构建输入，CMake 重新应用补丁，
`apps`、`apps_ai_agent` 两个受影响目标先完成增量编译；随后按用户要求完成下述
整机编译、签名及实板下载。断电恢复语义仍以主机故障注入为证据，未进行实板断电试验。

## 778 实板下载与手机更新

2026-11-04，使用上述工作树修复生成 `0.7.132+778`，rollback floor 为 778。
来源为 `8d03d7555cee4eaf6361ef79238a10818c1a7a08` 加未提交修复，构建输入树
SHA256 为 `47588472df9412514a8ac4be3b96a80b07ea9f82563de59001a819934dc7aa39`。
CP/AP 与 BL2 构建、构建清单、包结构及 BL1/BL2/CP/AP 签名校验均通过。

沿用已接受的同板原厂镜像及既有签名身份；8 MiB 镜像的四个 immutable 分区与
原厂输入逐字节一致，未重新回读 Flash。镜像 SHA256 为
`5aef6b1db259882b0a2515ec572984f4af2aa5525ed90fa160619a2985fb85c0`。
COM22 下载的全部成功标记齐全，无失败标记；重启后设备身份不变，实际报告
`pair=confirmed version=0.7.132+778 counter=778`，AP error=0、CPU2 ready=1、
online=`00000003`，Supervisor faults=0 / recoveries=0。

已连接 Mi 10 从 App 58 保留数据升级到 59（`0.7.29-shaniu-finals-fixes`）。
安装前后签名一致，安装结果为 Success，手机安装后的 APK SHA256 与候选完全相同：
`7fa83ecb4d0ce52d03f27518ba947098970cf44df59771ce92539875c353c2bc`。
首次安装时间未变，Activity 启动成功，观察期间同一进程无 FATAL EXCEPTION。

完整证据保存在工作区 `out/releases/shaniu-finals-fixes-20261104/verification/repair-device-update-ah7am48b/`；
包含构建来源快照、下载原始日志、重启/运行状态及手机升级结果。上述首次下载时
语音配置报告不可用（`BKVOICE configuration unavailable result=-11`），App 尚未连接。
这一阶段仅验证固件启动和手机安装；后续初始化、连接及用户验收结果如下。

### 手机发现失败：数据卷未初始化

随后按用户反馈检查手机发现流程：蓝牙开启、SCAN/CONNECT 权限已授予，扫描服务
UUID 与固件一致。板端实错为 `BKFACTORY storage ready=0 result=-14`，`/data`
仍是伪文件系统（type=`00009fa0`，非 LittleFS），`bkprov status` 返回 `-19`。
普通同板恢复镜像保留了原厂输入中全擦除态的 1 MiB `persistent_data` 及 8 KiB
`factory_state`；没有首次初始化 journal，启动流程仅尝试普通挂载而不格式化。
身份存储因此不可用，产品 BLE 广播保持关闭。这是先前版本/双核启动检查遗漏的
产品初始化失败，不能归结为单纯缺少云配置。

复用现有 `tools/bk7258/_lib/factory.py:create`（`release full --factory-init`
调用的相同实现）生成初始化候选，保留已验证 778 的所有签名固件及校准字节。
实测仅 journal 内 127 字节变化，候选 SHA256 为
`3cdf4f9f09db61333c1d93316fd1d158787e83e17b0a667cc3500fb26a2226f9`。
手机仍有旧绑定，本机保留的身份记录均不匹配其公开证书指纹。用户明确允许初始化
及重新认领后，已写入该候选；没有修改签名固件或校准资料。设备回读 LittleFS 已挂载、
`identity=present bytes=539`，运行版本仍为 `0.7.132+778`，Supervisor faults=0 /
recoveries=0。App 第 6 次实际扫描首次匹配设备，结束时 `unique_matches=1`，
与此前五次扫描的 `unique_matches=0` 对照确认发现恢复。
用户扫描圆屏新码并确认认领成功。随后核对手机持久化的新绑定及证书指纹均存在；
认领与控制连接的 TLS 握手成功，联网设置页显示已认证、状态新鲜的设备回读分支，
不再报告旧绑定的证书指纹不匹配。板端仍为 778，身份可读，CPU2 在线且故障/恢复计数为 0。

### 本轮实机验收

用户随后明确反馈“验收成功，对话拍照成功”。本轮对话、拍照按用户实际使用确认
记为通过；自动化证据覆盖下载、初始化、手机发现、认领持久化、认证连接及状态回读。
未把用户确认写成自动化语音/相机测试，也未将本轮结果扩展为 App OTA 或实板断电
故障恢复验收。相关本地证据保存在
`out/releases/shaniu-finals-fixes-20261104/verification/ble-discovery-qeef8qmv/`，
原始日志、绑定资料及本机专用镜像不随源码发布。

### 发布前复审

再次静态复审当前树相对官方 `1d972493` 的固件/框架、Android 与主机工具变更，
未发现可证实的合并阻断问题。上述源码修复及测试文件与 778 构建时记录的 SHA256
一致；后续仅更新验收记录和首次初始化操作说明，复用已通过的回归与实机证据。

已知非阻断边界：功能商店下载与 OTA 临时 HTTPS 来源关闭仍共用后台工作队列，
并发下载时实际关闭可能排队（单次下载有 30 秒期限）。此串行关闭行为在本次改动前
已存在；BLE 控制已独立，不受该队列占用。本轮未验收这一 App OTA 并发场景。

### CI 收集清单补充修正

PR #148 的首次冷构建通过固件、Android 与配网回归，契约运行实际收集 999 PASS，
断言失败、环境错误和未运行均为 0。收集门禁因新增的
`RES-01.upload-interrupted-recover`、`RES-01.upload-active-recover` 与
`RES-01.job-interrupted-recover` 未登记在固定清单而拒绝通过。

已将这三项同时加入 `required-units.v1.json` 的 `ids` 和 `added_ids`；
原有 63 项 `baseline_ids` 完全保留，未放宽收集门禁。使用该次 CI 的原始结果调用
真实 `collection_errors`，修正前复现三项 unexpected，修正后为 0；
999 项选择集恰为 63 项基线与 936 项新增项的互斥并集。
`test_shaniu_runner_gate.py` 的 30 项自测通过。本次仅补充测试登记和验证记录，
固件与 App 运行源码保持不变。

## 后续源码状态更正（2026-11-05）

本节只说明 778 实板验收之后的源码状态，不改写上述带日期的构建、下载和实板证据。
PR #154（`95a4dfefd2abf9cdfc22b377db2a662d24d0f7e3`）修改
`app/bk7258/bk7258_native_apps.c` 的状态目录清理，因此当前 HEAD 不再与
`0.7.132+778` 的构建输入相同。

该改动刻意在发布 catalog 前清理已删除应用和新安装应用遗留的状态目录；清理完成后若
catalog 持久化失败，旧应用仍可能保留在 catalog 中但其状态已被清除。两个独立存储之间
不承诺回滚；重试以当时持久化结果为准。该源码改动的本地主机回归与
[CI 冷构建 41868851257](https://github.com/open-vela/contest2026_135_yongwangzhiqian/actions/runs/41868851257)通过；PR #154 本身尚无 CI 原生应用运行时回归。后续工作流已将真实
`native_apps` Make 目标纳入冷构建，当次运行结果以 Actions 为准；未下载固件、未新增任何实板或手机验收证据。
