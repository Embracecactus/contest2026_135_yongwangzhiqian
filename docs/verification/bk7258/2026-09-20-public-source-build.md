# 2026-09-20 公开源码隔离构建

本记录只证明构建，不是新的硬件验证。最终实板候选仍为 635；没有烧录、
OTA、手机安装、重新认领、数据清除或密钥轮换。

## 输入与方法

- 团队源码：`c10a7668a0b990678c6a91dee9403fba773d45ea`；原开发工作树保留。
- Agent：`Embracecactus/packages_ai_agent@add0db19d00301769907a5ece03fb9bd88d2edb4`，
  与原工作树 21 文件差分一致；官方基线 `e65550f`，不是上游已合入版本。
- 从已发布 manifest 创建独立工作区，复用 Git 对象缓存，重新检出实际构建
  依赖及其父项目；52 个已检出项目 HEAD 与固定 manifest 一致。
  未为此下载或运行所有其他平台/测试项目。原工作区未同步覆盖。
- GCC：10.3.1，锁定归档 SHA256
  `97dbb4f019ad1650b732faffcc881689cedc14e2b7ee863d390e0a41ef16c9a3`。
- SDK：`cb080de1655d579c7593ecf504c440997c4c137b`。复用本机缓存 bundle，
  由既有 `sdk verify` 按公开 profile 哈希验证；**本次未重新编译 SDK**。
  CP/AP 源码和构建目录为隔离检出/新建，不复用旧固件目标文件。
- 执行现役入口 `tools/bk7258/bk7258.py build --board <板型> --boot direct --jobs 8`，
  按 AIDK、Core、T5-Board 顺序单一写入者构建；修正 T5 可选项后更新各板构建清单。

SDK bundle 树摘要：

| profile | SHA256 |
|---|---|
| cp | `b2282cc342fa2cfb269d77116ecedf3b1b85e73fdadb4bc3d8edd4195c8fb589` |
| ap | `b757356adad182af6cfe539ada915439fa1d8f565c9e89f8b8130b47cca74180` |
| cp-aidk | `d62004bf57db326149324184cf3c818914d77f46c66ad5ef8d053b67569c68d0` |
| ap-aidk | `f7de532a4433d5cef39e202fb63d08496a75805e4a77ba245da9d9a73fc6f633` |

## 结果

三板 CP/AP 均构建通过；build manifest 的团队源码 `dirty=false`，
NuttX/apps 的已跟踪构建源变更摘要为空。下表均为 **direct raw BIN**，
不是已签名 OTA、全片恢复或 635 原包，不能用于覆盖已有安全设备。

| 板型 | 核 | 字节 | SHA256 |
|---|---|---:|---|
| AIDK AI Toy | CP | 1085336 | `3d7e2db6b622cc77f0bf201e7ba2641760b518643eaef900234af890ee02498c` |
| AIDK AI Toy | AP | 1550960 | `8182c380c091e5b70756d73b25759c40b5494db17d965450b1c60193b7d3b77b` |
| T5AI-Core | CP | 257316 | `e581428243ecd2a57e7f50a2e7e4b0cdf35df4a10e3466cd16f83e06ddd30029` |
| T5AI-Core | AP | 139872 | `00e23d918fc588d992c40f107854db01bfda7de66f1df57f01dc622e72014827` |
| T5-Board | CP | 1069280 | `daac9672f79b768c4b9bfe8faec20ef196703231e6088a91897122256ce4acfd` |
| T5-Board | AP | 534564 | `06438be0d72ed0759d388f514422e6c0af0c882a663e9730d87c5a065c09e41c` |

AIDK AP ELF 实际包含 `agent_loop_init`、`llm_set_transport`、
`voice_channel_start_auto`、`media_recorder_open`、`tool_read_file_execute`，
说明所需官方核心/扩展已进入链接，不单凭配置名字推断。

同一隔离源码的 Android 工程执行 `./gradlew --offline --no-daemon
:app:assembleDebug` 成功（35 tasks，33 秒）；使用已缓存依赖、JDK 21.0.6、
Gradle 8.13 和 Android SDK 35，不需要设备授权文件或云凭据。
输出为 `0.5.23-shaniu-rebind` / versionCode 28 的 debug APK，8,038,482 B，
SHA256 `ce11225ce02417096206aa02b48a9ba04dd97b31358b13d4077aa6a9eac124f9`。
APK assets 只有三份公开唤醒模型；未安装到手机，未检验与已安装 APK 的签名兼容性，
不把本次 APK 编译算作新的 App 控制或 OTA 实测。

## 失败尝试与修正范围

1. 首次全项目获取遇到 GitHub fetch 中断；后续使用已有对象缓存定向获取构建依赖。
2. 首次 AIDK 隔离构建未检出 `frameworks/multimedia` 等父项目，Media Kconfig 未加载，
   因而缺 `media_recorder.h`。补齐四个父项目后从新目录重新配置/编译通过，未改框架。
3. T5-Board 首次链接缺少 `media_recorder_*`：Dolphin 在 `!MEDIA` 下选择了已退役
   兼容 ABI。`c10a7668` 只修正团队 Kconfig 前置条件与 T5 的无效选择。
   录音源码保留，本次配置退出不构成录音功能复现；本条功能结论的同日更正见下文。
4. 当前 AIDK/Core/T5 构建结果均来自同一 `c10a7668`；AIDK 运行代码及配置未改，
   与冻结 635 的关系和私有资产边界见 `SOURCE_PROVENANCE.md`。

## 留存

现役构建输出仍使用 `out/bk7258/<board>/.../releases/direct/build-manifest.json`；
三份清单 SHA256 分别为：

- AIDK：`5138d74b8b26dcde51f7fbcceec238e8fad422e58381b89fa8d462a3e30d0114`。
- Core：`2b336b30b294efd2f496b8b7d7ec8115fa54aa60e9b5383733ca349a116cb7e7`。
- T5-Board：`bcfe3c108198c3f05b1dc8e87753805283e94742ccbf868c7ca3546256498732`。

固定声明 manifest SHA256：
`8b9909e1f14edf988c4b85258a2f8083c7d401cc3959d1dafa05343247c49091`。
这是固定 revision 的 Repo 导出及已检出项目核对，不冒充未检出全部项目的 `repo manifest -r`。
三板日志、ELF/map/config 与清单在本机隔离构建输出留存；不公开含个人路径的完整主机日志。

## 同日更正：T5 录音实测与本次构建不是同一结论

用户确认 T5 小海豚录音保存 SD 已实测成功；既有 `0.1.0+13` 的用户确认与
镜像哈希见 `docs/platforms/bk7258/dolphin-master-plan.md`。先前第 3 项把
当前构建缺失的接线扩大表述为录音功能未完成，现予更正。上述构建命令、
产物哈希和失败事实不变；`c10a7668` 关闭录音的配置仍未与该实测版本对齐。
本次未变更固件或重新实板验证。

## 同日修复补充：恢复 Dolphin 专用录音接线

源码 `8de0ae7880581ceb831cba6ef68808eedd333fb3` 确认首个偏差是 CMake 清理
误删了仍有 Dolphin 消费者的 NuttX audio 适配，而不是官方 Media 缺陷。
恢复 T5 `DOLPHIN_RECORDER=y`，CMake/Make/源码统一限于 Dolphin + MIC + !MEDIA；
录音应用及采集函数体未改。现有 `test_dolphin_recording` 主机检查通过，
复用现役 CLI、原构建目录执行 T5 CP/AP direct 增量构建通过，source dirty=false。
未新增测试、未重编 SDK、未烧录或重测实板；AIDK 635 不变。

AP ELF/map 确认 `dolphin_ui.c.o → dolphin_recording.c.o →
bk7258_agent_media_recorder.c.o`，open/prepare/read/start/stop/close 各只有一个定义，
无 Agent 或官方 Media 服务。恢复的是既有独立 NuttX 录音适配，不是旧语音 runtime。

| 产物 | 字节 | SHA256 |
|---|---:|---|
| T5 CP raw（未变） | 1069280 | `daac9672f79b768c4b9bfe8faec20ef196703231e6088a91897122256ce4acfd` |
| T5 AP raw（录音已编入） | 547020 | `bed679bd9f16b986e1335886b7ff429cda4f60ee8845e9fbe7640fe1220e12f0` |

本次 build manifest SHA256：
`63f9c6f60ead1e8d3a436ec987469d41cda4a96abe5f2944a75ebbf23af9c4ec`。
这些 direct 产物不是已签名 OTA 或可随意全片烧录的恢复包。
