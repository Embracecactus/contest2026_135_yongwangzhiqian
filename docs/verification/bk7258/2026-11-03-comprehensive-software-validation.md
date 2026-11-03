# 2026-11-03 综合软件验证记录

## 结论

当前工作区的 BK7258 主机门禁、补充协议检查、Android 构建/单元测试/lint、Android
模拟器 UI 与安全探针，以及三组 AIDK direct 诊断构建均通过。原生功能 ABI 3、NFC
动作 7、电脑审批和重构后的 Android UI 已进入对应固件或 APK。

本记录是**软件与模拟器证据**，不是当前源码的实板验收：本轮没有下载固件，没有
真实手机连接板子，也没有执行 BLE 安装、卡片启动、运动/电机反馈或 PC 审批闭环。
工作区基于提交 `541c6434a02aeb268721a751a16d10a3d49ca2fc`，并包含尚未拆分提交的改动；
最终提交身份应在提交后由 Git 记录补充，不用本文件的基线哈希代替。

## 验证结果

| 层次 | 入口 | 结果 | 能证明什么 |
| --- | --- | --- | --- |
| BK7258 主机门禁 | `make -C tests/host/bk7258 check` | PASS；结束标记 `BK7258_HOST_TEST_PASS`；104 个头文件在 default/CP/AP、C11/C++17 下审计通过 | 当前主机可执行逻辑及头文件组合通过 |
| 补充协议 | provisioning、Voice TLS concurrency、Agent profile/memory/local intent、runtime skills/store、response length、expression ownership/trial | PASS；provisioning 含 20 组随机样本，Voice TLS concurrency 9 例 | 受影响协议与拒绝路径在主机环境通过 |
| 外部依赖回归 | 实际 `nuttx-apps` webclient + 云 HTTP 适配器；实际 Agent TTS queue/capture/mixed-tools | PASS；同一 TLS 连接完成 length/chunked 两次请求且仅打开一次，peer-close 和歧义 framing 拒绝通过；Agent capture 5 例与流式路由异常分支通过 | 当前两个依赖工作树的受影响实现进入了可执行测试，而非仅编译 |
| 包工具 | runtime Skill、native app、CSI Python 测试 | 3/3、7/7、6/6 | 打包、清单校验和工具拒绝路径通过 |
| Android 构建 | `:app:assembleDebug :app:assembleDebugAndroidTest :app:testDebugUnitTest` | PASS；345 tests、9 skipped、0 failure、0 error | App、测试 APK 和 JVM 行为可构建/执行 |
| Android lint | `:app:lintDebug` | PASS；0 error、95 warning | 无 lint 阻断；剩余多为既有 i18n/KTX 和绘制分配建议 |
| Android 模拟器 | `ui_gallery=1` 浅/深色、`ui_probe=1`、BLE facade probe | 两种主题各 30 个模拟场景 + 20 轮导航通过；安全探针通过；BLE 扫描 API 可启停、0 个外设 | UI、导航、键盘/旋转等待、Keystore/TLS/OTA 断言及 Android BLE API 可运行 |
| AIDK 产品构建 | `bk7258.py build --board aidk_ai_toy --boot direct` | PASS，无警告 | 标准 CP/AP 源码在 direct 诊断链可编译链接 |
| 原生加载诊断构建 | `app + openvela_ap_native_test`, direct | PASS，无警告 | 原生加载诊断配置可编译链接 |
| CSI 诊断构建 | `csi_cp + csi_ap`, direct | PASS，无警告 | CSI 配对配置可编译链接 |

模拟器运行的是正常 App APK，并保留原有应用数据；测试 APK 的模拟状态明确标注，
不会写 owner 或把模拟值注入生产 App。电脑上的 Android 模拟器不能把主机蓝牙透传
为真实手机 BLE，因此 BLE facade 通过不等于发现、认证或控制了 BK7258。
本轮首次 gallery 运行时模拟器设置 `show_ime_with_hard_keyboard=0`，输入框有焦点且可
接收文本，但系统按配置隐藏软键盘，因而 IME 可见性断言失败。临时切换为 `1` 后完整
gallery 通过，测试后已恢复为 `0`；该首次失败不记作 App 通过，也不修改生产代码。

## 构建身份

以下均为未签名 `direct` 诊断产物，不可用于已供应安全设备的正式更新：

| 配置 | CP SHA-256 | AP SHA-256 | pair SHA-256 |
| --- | --- | --- | --- |
| `app + openvela_ap` | `f594fda28b6b3373c6d0a6e25ff77dfce2d6660cbe9588812080cc1a3cd9f043` | `31605b76d68ed757d05b9c3e4d12c2fc533a45ccc2ee9255974f4540407341c9` | `e6fb8293d176001d7df7111f48ffa802faeef487de8c8ab0e4cb61bdbe6a58b9` |
| `app + openvela_ap_native_test` | `f594fda28b6b3373c6d0a6e25ff77dfce2d6660cbe9588812080cc1a3cd9f043` | `00b230d584073cf022a4f13a8b584ed7b10120409aec8534150a2bcb34956de1` | `bb04e30131c839f11a1116ac2de73ee1eaefdfd41179ee993073feee09439b28` |
| `csi_cp + csi_ap` | `66469f8b49e33519fe90be6de00066f8057208147919e677fb9b6956b8e899` | `37bb454498e808683445e35d552577ac55d83f953c1586b57a36f2044b1e9da1` | `5939dcb12dd484b984933c8e317d640541df4dcf3ef841867967132aac4a9aa2` |

标准产品配置解析为 `CONFIG_BK7258_NATIVE_APPS=y`，但
`CONFIG_BK7258_NATIVE_PUBLISHER_SHA256=""`。因此管理器和手机界面可以存在，设备仍会
拒绝模块安装；必须先由项目选择并配置独立的第一方发布公钥指纹。主机测试使用的
公开测试密钥不得成为产品身份。

## 验证中修正的问题

- 新的 SMP placement 公共头补入 `stddef.h`，并为主机架构头审计增加 CSI 转发头。
- provisioning 临时安装布局同步导出 `bk7258_wifi_csi.h`，避免测试夹具落后于真实依赖。
- Android UI 使用 minSdk 29 可用的 `LineBreaker.BREAK_STRATEGY_BALANCED`。
- 模拟器旋转后软键盘显示最慢约 1.8 s；测试改为等待现有 IME inset 条件，不放宽断言。
- Agent 新增 `llm_complete` 观测阶段后，严格 mixed-tools 时序夹具同步期望并重新通过；
  生产观测点未为迁就旧测试而删除。
- 云 HTTP 夹具补充真实连接复用、chunked 后继续复用、peer close、重复 Content-Length
  冲突及 Transfer-Encoding + Content-Length 歧义拒绝。
- motion/NFC 客户端的 epoch 局部变量和可选外设 bring-up 返回值声明消除配置相关编译告警。

## 提交前复现状态

`git status --short` 当前显示 252 行（未跟踪目录会折叠）；精确文件清单包含 158 个
已跟踪差异和 132 个未跟踪文件，其中两个删除已在索引中暂存。它们跨越固件、诊断、
语音/Agent、Android、工具和证据，不应描述成单一 UI 小改动。已对变更文件和未跟踪文件执行
私钥块及常见云/API key 形态的提交前扫描，未发现命中；这不替代发布者对内容和
许可证的最终复核。

当前构建依赖两个比赛仓外的固定候选：

- `nuttx-apps`：`include/netutils/webclient.h` 与 `netutils/webclient/webclient.c` 的
  keep-alive 接口已发布到 fork 提交
  [`d529ec814bc11a4e76954a8dfb903c55e1eca4f3`](https://github.com/Embracecactus/nuttx-apps/commit/d529ec814bc11a4e76954a8dfb903c55e1eca4f3)；
  既有 BK7258/Dolphin 等未跟踪目录未纳入该提交。
- `packages/ai_agent`：最终 16 文件树已发布到 fork 提交
  [`76c65bc6231a7eeb49c32f8938a3d005a4221776`](https://github.com/Embracecactus/packages_ai_agent/commit/76c65bc6231a7eeb49c32f8938a3d005a4221776)，
  当前依赖工作树已切到该提交且干净；切换前的等价树以命名 stash 保留。

团队 manifest 已固定上述两个依赖提交。当前结论仍是“同一工作区可构建”；完整的
干净 manifest 同步尚未在本轮重跑，不能用依赖已发布代替该复现证据。

## `shaniu-film` 动画检查

本地 HyperFrames 0.8.205 项目的静态检查为 0 error / 0 warning，15 张抽样画面经查看
未发现阻断问题。现有 175 s、1920×1080、30 fps 的 H.264/AAC 成片可完整解码，
SHA-256 为 `271bbb9d7d46e329da3eabd031d473cc321601d2272e3c92f6a63e37595a33c2`；
全新 delivery 渲染也完成。两次渲染的音频 PCM 相同，视频仅在约 150.33–165.97 s
出现轻微编码/渲染差异（平均 SSIM 0.999990744，最低 0.999645），所以不宣称逐字节
可复现。声音只完成解码和响度测量，没有正常速度主观试听。

该项目目前位于 Git 忽略的构建输出区，总目录约 448 MB，其中约 412 MB 是 Blender
帧缓存；它没有被纳入本次代码提交。成片、可编辑源、字体/SFX 许可与逐文件来源应在
独立媒体交付中整理，不能把缓存目录整体加入 Git，也不把未发布的本地成片描述成
仓库下载项或实机录像。

## 仍需实板完成

1. 在批准第一方发布密钥后，用真实手机安装、更新、启停和卸载 ABI 1/2/3 包，验证
   动作输入、触觉、声音、保存进度、定时和故障计数。
2. 用真实卡片验证 NFC 动作 7，从 App 登记、板端读卡到原生功能启动及状态回读。
3. 执行 PC agent → USB workbench → 板子 → 手机 owner → PC 的批准、拒绝和超时闭环。
4. 核对重构 UI 的扫描、认证、眼睛状态、设置回读和 OTA；模拟器交互不能替代射频。
5. 对正式签名候选执行下载、启动证据、资源泄漏循环、掉电事务和坏模块恢复测试。
