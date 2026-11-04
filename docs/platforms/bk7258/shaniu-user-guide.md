<!-- SPDX-License-Identifier: Apache-2.0 -->
# 傻妞上手、演示与评委复核教程

[项目入口](../../../README.md) · [功能与证据清单](shaniu-feature-inventory.md) ·
[Android App](../../../android/shaniu-companion/README.md) · [构建 CLI](../../../tools/bk7258/README.md)

本页把“能跟着做”的使用路线与“评委能独立复核”的构建路线放在一起。每一步均以
设备读回、终端回执或保存的文件为完成条件；屏幕截图、模拟器画面和历史录像各自只
说明其标出的范围。当前源码没有作者签名发布和整机验收，不能把本页的构建成功写成
任何板子的功能验收。

## 先选对板与材料

傻妞只对应 AIDK AI Toy（构建标识 `aidk_ai_toy`）。T5-Board 与 T5AI-Core 共享芯片
适配但不是同一 PCB，不能互刷。日常演示准备 USB 供电、已认证 Android 手机、2.4 GHz
Wi-Fi、自己的云服务凭据；电脑工作台还需要已配对的电脑与原生 Type-C 通道。不要在
录屏中暴露 Wi-Fi 密码、API Key、认领码、电脑私钥、设备身份或个人照片。

## A. 开箱与手机认领

1. 对**未认领**设备，打开 App，授予蓝牙和附近设备所需系统权限，选择“添加傻妞 · 扫码连接”。
2. 等设备双眼显示认领码后扫描，保持设备通电直到 App 显示认证结果。当前源码中，已证明无 owner 的首认领窗口会自动开放；它不是“按键打开”的流程。按键普通短按仍是 PTT，旧的按住约 8 秒再松开只打开只读回执恢复窗口，不能用来重新认领。
3. 认证结果不确定时，选择“核对认领结果”，读取设备状态；不要反复提交认领。
4. 在“设置”让设备扫描 Wi-Fi，输入密码、云端地址/API Key 和模型（源码默认聊天模型为
   `mimo-v2.6-flash`；实际板端以设备回读为准），保存后等设备
   回读。密码与 Key 不会回读；若 App 只显示本地草稿，不代表设备已配置。

预期结果是 App 读到已认领和已保存的设置。若没有码，先确认设备确为未认领且处于空闲，不要靠反复按键或重复认领来“刷新”窗口；已有绑定设备应直接连接认证。11-04 当前
`0.7.132+778` 与 App 59（`0.7.29-shaniu-finals-fixes`）已完成数据卷初始化后的发现、
重新认领、认证控制连接和状态回读；用户确认对话、拍照成功。这不构成 App OTA、
断电恢复、长期 BLE 生命周期或当前模型身份验收。认领、配网的真机历史记录见
[637/641 与 11-01 记录](../../verification/bk7258/)。换手机、清 App 数据或卸载会丢失 Android Keystore 中的认领凭据，
先按 [App 的认领与设备身份说明](../../../android/shaniu-companion/README.md#认领与设备身份)
处理，不能把 debug APK 覆盖到作者签名 App 上。安装或演示时从 App 的“设备信息”和
设备回读记录版本；不要把仓库源码的 Gradle 版本、历史模拟器版本或别人的 APK 版本
当作当前受验产物。

## B. 语音、追问与停止

1. 确认 Wi-Fi 与云服务已由设备读回保存，在安静环境中说“你好，openvela”。
2. 预期双眼进入聆听，播放预置“我在”；问一个短问题，等待回答。
3. 回答播完后，在静默超时前直接追问，预期无需再唤醒。
4. 不要在播放中说“停止说话”或“取消回答”来期待打断：设备是半双工的。只有回答播完、设备重新进入聆听并取得 ASR 文本后，这些短语才会作为本地意图被解析；它们不是当前播报的声学打断。
5. 当前版本没有已验收的面向用户的“播放中按键/App 取消”操作可照做：不要在演示里承诺能打断正在播报的声音；需要停止时先等播放排空并回到聆听，或仅在实际 App 明确显示且实机已验证取消控件时使用该控件。
6. 举起明亮物体说“看看这个，这是什么？”，设备拍一帧并把图像交给云端模型。

离线时本地唤醒、眼睛、按键、动作、NFC、本地内容和专注计时仍可运行；ASR、回答、
TTS 和识物必须如实失败，不能以本地占位话术冒充回答。完整语音链路的历史实板证据是
`18.6.401+638`；11-03 设备因缺少网络/云配置报 `ENETDOWN`，只证明当时失败边界。11-04
778/App 59 的对话成功为用户确认，不替代可重复的声学/时延验收，也不识别当前板端模型。

## C. 外观、专注、NFC 与小功能

**眼睛资源。** 在 App“定制”页先上传/选择 `.bkep`，等设备返回 `pack_id`、`revision`
和 `source_sha256`，再试用或设默认。受理、上传完成和设备安装完成是三件事。本轮接收的
电脑工作台作者粗剪可看到拿起设备时的眼睛变化，但未记录完整版本、也未显示
眼睛包安装闭环，所以不能替代本步骤的读回。

**专注。** 可由 App、语音、NFC 或电脑开始/暂停/继续/取消。开始“专注 25 分钟”后，
预期眼睛显示进度环；重启不会恢复进行中的计时。模拟器和主机测试已覆盖流程，完整
实板计时/完成反馈尚待验收。

**NFC。** 使用 ISO14443A 卡，在 App 把卡登记到场景槽后移开再碰；预期一次触发，
持续贴住不重复触发。实卡登记和触发尚无验收，扫描 `-110` 只表示当时无卡，不等于
模块损坏。

**Runtime Skill 与原生功能。** Runtime Skill 要由明确的发布者 P-256 私钥签包，手机
导入 DER 公钥后核对指纹再安装，首次保持禁用；详细命令见
[CLI 说明](../../../tools/bk7258/README.md#runtime-skill-packages)。原生小功能 ABI 是
协作式接口，不是安全沙箱；当前产品配置的发布者指纹为空，正式固件会拒绝安装，
不要为了演示绕过该信任根。

## D. 电脑工作台与可选审批

先由已认证手机把 `tasks`、`camera`、`resources` 或 `scenes` 权限授予电脑。电脑只在
原生 USB CDC + TLS 上工作；CH340 是调试口，不能替代产品通道。

```bash
# 先查看已授权连接，随后才按实际 profile 与端口操作
python3 tools/bk7258/bk7258.py workbench status --port <native-type-c-port> --profile <pc-profile>

# 无害任务示例；--exec 后是直接 argv，不经 shell
python3 tools/bk7258/bk7258.py workbench task-run \
  --port <native-type-c-port> --profile <pc-profile> --run-timeout 120 \
  --exec python3 -c "print('SHANIU_TASK_OK')"

# 浏览器工作台采一帧；保存/取消/重拍均应从返回的 job/receipt 继续查询
python3 tools/bk7258/bk7258.py workbench camera-capture \
  --port <native-type-c-port> --profile <pc-profile> --workbench-dir <new-empty-dir>
```

任务以 `device_result=confirmed` 为送达条件；超时或 Ctrl+C 应明确返回取消。采一帧
只保存已显示的那一帧，保存、取消、重拍后都要查询回执。11-03 实板记录了任务成功/
超时与两帧的哈希一致性；[55 秒取景录屏](../../verification/bk7258/2026-11-03-feature-validation/videos/board/camera-capture-cancel-recapture.mp4)
画面偏暗但为原速实录。作者另有 31 秒粗剪展示浏览器采一帧，未作为公开版本/身份
证据发布。

单次审批不是日常电脑任务的前置条件。它只用于需要 owner 逐字确认的操作：

```bash
python3 tools/bk7258/bk7258.py workbench approval-run \
  --port <native-type-c-port> --profile <pc-profile> --agent demo \
  --target <affected-directory> --approval-timeout 120 \
  --exec python3 -c "print('APPROVED_RUN')"
```

手机 owner 批准前，命令不会执行；退出码 2 是未批准。11-04 `0.7.129+775` 实板有一次
等待批准时双眼感叹号、批准后清除的人工确认，终端为 `decision=approved`、
`authenticated=true` 和返回码 0。拒绝、超时和多轮稳定性未在 775 复测；语音也不能发起电脑操作。

## E. OTA 与同机恢复

App OTA 只接受信任根、目标板和回滚计数都匹配的 `.bkpack`。在 App 选择包后保持前台；
重启后读取版本、rollback counter、active slot 和 `pair=confirmed`，再称升级完成。中断
或断线时先查询结果，不能盲目重发。历史 634 有首次真实 OTA，767 的自动重连通过，
770 的升级后自动重连失败，故 OTA 后重连仍须单独验收。

完整 8 MiB 恢复镜像必须来自同一设备的已接受 base 与证据，它含设备绑定数据，不能
复制给另一块板。独立构建得到的 factory software 明确标记
`same-device-hardware-data-required`，不是“任意板整片”。详见
[构建/刷写 SOP](nuttx-port/bk7258-build-flash-debug-sop.md) 的恢复和交付边界。

## F. 评委从官方仓库复现

1. 访问官方仓库的 `dev-ai-contest-2026`，记录目标 commit；不要把 fork 的 Actions
   结果当作官方结果。为该实际提交重新核对
   [Shaniu cold delivery](https://github.com/open-vela/contest2026_135_yongwangzhiqian/actions/workflows/shaniu-source-checks.yml)
   与独立交付校验的结果；不要用本文创建时的 PR、分支头或旧运行编号代替该核对。
2. 在全新空目录按项目 [README 的固定 manifest 命令](../../../README.md#获取固定版本的源码)
   `repo init` 和 bounded `repo sync`；保存 `declared-manifest.xml` 与团队项目 HEAD。
3. 安装 Python 3.10、JDK 17、Android SDK 35、`repo`、`openssl` 等前置条件，运行
   `toolchain install/verify`、两个 AIDK SDK 的 `rebuild/verify`，再以自己的开发身份
   `identity init --development` 运行 AIDK `build` 和 `release full --factory-init`。
4. 对 `.bkpack` 运行 `verify package` 与 `verify trust`，对审查档案运行 `sha256sum -c SHA256SUMS.txt`。
   只有按 README 的同机 accepted base 路线生成的 **product delivery ZIP** 才运行
   `verify delivery`；独立工厂软件与含 APK 的审查 ZIP 不是这类包。核对 build manifest、
   `release.json`、`declared-manifest.xml` 与 `SHA256SUMS.txt` 指向同一候选；官方 CI 归档还须
   核对其 `source-inputs.json`。
5. 在 `android/shaniu-companion` 运行 `:app:assembleDebug`、
   `:app:assembleDebugAndroidTest` 与 `:app:testDebugUnitTest`，保存 APK 和 Gradle 输出。
6. 运行 README 列出的主机合同与层次检查。它们只验证逻辑/协议；没有已授权的实体板、
   相同签名信任根、网络与云服务配置时，不声称语音、BLE、OTA 或声学通过。

CI artifact 仅保留 7 天且需登录下载。开发身份签出的 package 使用调用者自己的
BL1/MCUboot 信任根；它可以完成 package/trust/delivery 验证，却不能 OTA 或整片刷入
作者板，也不得用来替换其既有信任域。自建 debug APK 同样不能覆盖作者签名 APK。
四层“相同”的边界、完整命令和产物路径仍以
[根 README 的从源码复现](../../../README.md#从源码复现)为准。

## 演示录制最小清单

每段开头记录日期、固件版本、App 版本、板型和是否为历史包；保持原速，保留环境声。
至少分开拍：认领/配网、唤醒到追问与停止、识物、动作与振感、NFC、任务与相机、审批
批准与拒绝、OTA 与重连。对每段保存原视频、串口/终端 JSON、App 截图和哈希；敏感画面
停录或裁剪。拍到一次成功不等于整机验收，特别是审批、NFC、振感和 OTA 都需要各自的
失败/恢复分支记录。
