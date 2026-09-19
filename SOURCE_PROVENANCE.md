<!-- SPDX-License-Identifier: Apache-2.0 -->
# 源码许可证与来源记录

## 2026-09-20 代码、视频与复现状态

- 发布代码快照 `82610138` 基于官方比赛分支 `7079493e`；后者与开发基线
  `295e57c4` 的源码树一致，因此采用线性提交，不回退或覆盖开发工作树。
  本轮不创建、恢复或应用历史 patch；Linux 公共依赖按已采用的提交固定，不升级。
- 官方 Agent 基线为 `e65550f18759f086d7f544edcf17d1e31223244f`；用户创建
  `Embracecactus/packages_ai_agent` fork 后，将下节 21 文件既有扩展原样提交为
  `add0db19d00301769907a5ece03fb9bd88d2edb4` 并推送
  `feat/shaniu-voice-integration-635`。远端 SHA 已核对；相对官方 ahead 1 / behind 0。
  该提交与原开发工作树差分 SHA256 均为
  `bc6ac0747bf857873608762c0f9f19abd16ce3fe4a61dcfaee9b9c583c02dcef`。
  主 manifest 固定此公开提交；未创建或合入官方 Agent PR。
- 本次发布前核对：SDK（`cb080de1`）、NuttX（`76354c63`）、Media
  （`fb7db0e9`）、external（`f2c1425e`）及其独立 FFmpeg 项目
  （`4b4723f2f66ccfdbadbd5d4c52dd5c41d6116418`）工作树，相对各自
  锁定提交均无已跟踪源码修改；NuttX 有两处未跟踪的 OpenAMP 手工目录。
  这不等于整个 openvela 工作区零改动：Agent 的 21 个修改文件单列如下。
  Android 工程和模型工具继续在团队仓管理，不另建 App/训练仓；Agent fork 是
  上述已实际存在并可获取的依赖仓，不是将官方依赖复制进团队仓。
- 635 运行时 `device-assistant.md` 和 `read_file` 接线由团队产品层提供，复用
  官方 Skill loader、tool_files 和工具注册机制；用户与 CodeBuddy 已完成
  安装/语音/显示验证，App 控制和 OTA 仍引用 634，635 未重测。
- 两段演示成片、配套字幕与封面由项目作者提供用于参赛发布；发布副本只转为
  1080p H.264/AAC，不修改原成片。文件大小、SHA256 见媒体 Release。
  与生成的 App 图标、眼睛图集的来源分别记录，不把真人视频封面说成 AI 生成。
- 私人原始语音、实验语料/候选、可选授权应答 PCM、密钥、认证文件和同板恢复
  镜像不公开。`logs/` 未手工删改；第三方模型/数据许可不由仓库 Apache-2.0 覆盖。
- 照片、海报、18 页答辩 PPT 复用作者提供的 `openvela-showcase-v2` 成品；
  三块开发板实拍共四张原图，实际为正面/俯拍，未伪造后视/侧视照片。
  仅公开交付成品，不复制另一会话的原始记录、个人路径或私有数据。
- 635 为最终板测包，不再烧录或重测。公开源码可重建实现，不承诺更换用户身份、
  签名或私有音频后仍产生同一全片哈希。旧 build manifest 未单列 Agent 输入摘要，
  因此本次差分对应证明不冒充 635 完整二进制逐字节干净复现。
- 复用现役 build 输入摘要核对：635 原工作树仍为 570 项、树摘要
  `5f9e4fd9cec7c1d7a1fa29e6a076a6de0112c84b49c40136191e9b7206a752f8`，
  与其封存 build manifest 相同。`ac73a94e` 公开快照为 564 项、树摘要
  `2d6916ca6cda9671cb66fb9166900b54d84b47da7ec356675663650b161fb2ac`；
  共同输入逐字节一致，差集仅为已退役的 AIDK `drivercheck_ap/drivercheck_cp/xts`
  三组 defconfig/profile（6 文件），不在傻妞现役 CP/AP 配置内。
- 隔离构建发现 T5-Board `DOLPHIN_RECORDER` 在 `!MEDIA` 下仍调用已退役兼容
  ABI，链接缺少 `media_recorder_*`。`c10a7668` 仅修正团队 Kconfig 的真实
  Media/graph 前置条件，并取消 T5-Board 的无效选择；保留录音实现，未补回旧后端，
  也未将其记为录音功能完成。AIDK 635 的运行代码、配置与产物未改变。
- `c10a7668` 的三板 CP/AP direct 构建已在独立检出的工作区通过；固定了
  实际依赖 revision、工具链/SDK bundle 与产物哈希。SDK 缓存经校验而未重编，
  没有签名、部署或板测。见
  [本次构建记录](docs/verification/bk7258/2026-09-20-public-source-build.md)。


## 2026-09-19 已认领设备只读 Wi-Fi 扫描

`bk7258_control_pair`、`bk7258_provision_pair` 与 `bk7258_provision_owner`
沿用团队 Apache-2.0 适配，按首帧复用已有 TLS、持有证明校验与扫描 worker。
只读分支拒绝配置写入，不替换普通控制鉴权；本次未修改任何上游或 SDK 源码。

## 2026-09-19 眼睛资源与 Wi-Fi 安装适配

- `android/shaniu-companion/app/src/main/res/drawable-nodpi/shaniu_launcher_ruby.png`
  为内置 imagegen 生成的红色水晶手机与原创少女启动图标，无真人素材，不使用演员肖像。
  SHA256 `8e789751500dfc7f8296021178257bd6637975bf64df608d73e73ba37eff4295`；
  原图保留，Android 自适应图标引用项目内副本并留出裁切边距。
  先前未安装的通用青色眼睛图标已退出 APK 资源，板端 LCD 图集不变。最终生成提示词：

  > Create one final Android launcher icon artwork for SHANIU / 傻妞, an affectionate, clever futuristic female AI companion. This is the icon itself, one square image, not a presentation or mockup. Create an original memorable emblem: a friendly young adult woman's head and small high-collared futuristic shoulder silhouette, turned slightly three-quarter, with flowing dark hair and a warm, confident expression, fused elegantly with a translucent ruby-red crystal mobile-phone silhouette. Interpret as a living digital companion inside a magical ruby device, not a generic robot or beauty salon logo. Strong simple sculptural shapes, clean readable face, minimal detail, premium softly dimensional illustration; luminous ruby and coral red crystal with warm ivory facial highlights, restrained tiny technological light accents. Deep wine-black full-bleed background, subtle warm glow, excellent silhouette and contrast at 48 pixels. All important artwork must remain inside the central 60 percent of the square with generous quiet background so Android circle and squircle crops never cut off her head or hair. Do NOT draw a realistic actor, celebrity, existing person's likeness or a photograph. No text whatsoever, no letters, no Chinese characters, no words, no numbers, no watermark, no frame, no generic pair of floating robot eyes. One coherent iconic composition, not many small decorative elements.

- `app/bk7258/assets/display/shaniu-cyan-v2.png` 为内置图像生成工具生成的
  通用机器人眼睛图集，不使用真人照片、声音或肖像。最终编辑提示词及复现打包入口
  保存在同目录 README；PNG SHA256 为
  `10980433ca3c3063437dc113c22b5df2c3e865dee415593b5eb6ffe39ec5901a`。
- PNG 导入复用现有 BKep 构建器；动画复用现有 AP 显示 owner；资源传输复用
  App 临时 HTTPS 供包服务、既有受保护 TLS 与官方 webclient，不复制 Agent
  生命周期或增加下载平台。本次代码沿用团队 Apache-2.0；未新增上游、SDK、
  NuttX 或 FFmpeg 修改，已有依赖工作树的未合入修改仍按下节单独披露。

## 2026-09-18 评审修复的当前来源与边界

- 本轮以团队 `295e57c44961c1a3bd19dfb6d1246d851bb68f78` 加已有工作树改动为基线。
  记忆心情元数据兼容、MIC 完整帧容量及视觉子请求接线均在团队适配代码修改，
  继续适用 Apache-2.0；不改密文格式、密钥、Flash 布局或所选服务商。
- 实际 Agent 检出为 `open-vela/packages_ai_agent`
  `e65550f18759f086d7f544edcf17d1e31223244f`，开始前已经包含未提交的
  request/transport 扩展。本次另修改 `src/core/agent_loop.c`、`agent_mem.h`、
  `src/tools/tool_registry.[ch]`：移除不含上下文的回复缓存旁路，查询注册能力后才
  进入自然语言工具捷径，并把原请求检查传入串行/并行工具。保留原版权、Apache-2.0
  及已有 MimiClaw MIT 来源声明，不引入板号、厂商分支或另一套会话 owner。
- 三处责任代码没有可用的现成产品开关：Agent 的缓存与自然语言捷径为内部流程；
  原 provider 回调不带请求状态，而消息总线与 LLM checked 入口已有原请求检查。
  只在适配层清缓存、覆盖私有符号或另建取消状态不能正确覆盖这些边界。
- 上述官方检出修改保留在依赖工作树，**未**随本仓携带补丁文件或生成副本
  （`frameworks/patches/` 已随团队 `295e57c4` 退役并清空）。当前工作树相对
  `e65550f` 共 21 个文件、+1830/-549 行：`include/agent_config.h`、
  `include/voice/{audio_capture,audio_playback,voice_asr,voice_tts}.h`、
  `src/core/{agent_loop.c,agent_mem.h,message_bus.[ch]}`、
  `src/llm/llm_proxy.[ch]`、`src/tools/tool_registry.[ch]`、
  `src/voice/{audio_capture,audio_playback,voice_asr,voice_channel.[ch],voice_tts,volc_asr,volc_tts}.c`。
  历史构建直接编译该依赖工作树，故不是干净 manifest 复现；2026-09-20 已原样
  发布为上节 Agent fork 提交，并固定 manifest。官方 PR 仍未创建/合入；
  依赖发布、源码构建和实板验收是不同结论。
- 下方旧 `41723c61` pin、旧补丁链和构建派生机制的条目为历史来源记录；
  这些机制已随团队 `295e57c4` 退役，不代表当前构建仍采用。本轮未改 NuttX、SDK、
  FFmpeg 源码或任何依赖 revision，未将当前依赖工作树宣称为干净上游。

## 审计范围

本记录覆盖 Git 已跟踪的 `*.c`、`*.cpp`、`*.h`、`*.S`、`*.s`、`*.ld`、
`*.py`、`*.sh` 和 `*.ps1` 编译/可执行源码，AI 对话日志不作为源码统计。
2026-08-31 按当前拟提交工作树（包含拟提交的未跟踪新增文件，排除删除项、`logs/`、
`memory/` 及忽略的 SDK/工具链/构建产物）复核结果为：

- 非测试源码 370 个，其中 369 个声明 Apache-2.0 SPDX；唯一例外是保持初始化原样的
  `app/hello_app/hello_app_main.c`，由仓库根 `LICENSE` 管理；
- `tests/host/bk7258/`、`app/testing/bk7258/` 和 `tests/pytest/` 共 169 个测试源码，
  Apache-2.0 SPDX 覆盖 169/169；
- 合计 539 个，Apache-2.0 SPDX 覆盖 538/539，另有上述一个明确模板例外。

本轮新增源码在创建时声明 SPDX；既有源码若只缺机器可读标识，则在保留原版权与
完整许可正文的前提下补齐。任何从 SDK 或外部仓提取的协议/初始化序列均在下表固定
仓库、版本、路径和许可证，不因改写为 NuttX 组织形式而省略来源。

## 来源分类

`.agents/skills/` 的 7 份通用能力及必要参考文件来自本项目先前形成的操作指引，
现将权威正文从仅本机安装转为随仓库交付，适用仓库 Apache-2.0 许可；不包含
第三方手册副本、私人对话、设备证据或密钥。中文 PR 入口复用同仓通用发布流程。
`bk7258_os_adapt.c` 的互斥锁超时修复按当前 SDK `BEKEN_WAIT_FOREVER` 约定及
NuttX `nxmutex_timedlock` 的相对毫秒契约接线，未复制或修改其实现。

`app/bk7258/bk7258_agent_keys.c` 与 `bk7258_keys_main.c` 为本项目
Apache-2.0 产品适配，复用已有三键策略、KEY1 RPMsg 协议、官方 `/dev/buttons`
和 Media 音量入口；未复制 Agent/Voice 生命周期或 SDK GPIO 驱动。
`tools/bk7258/_lib/product.py` 的 `relocate_base` 为本项目同板分区迁移适配，
按原始字节与 SHA256 核对保护分区，不解析或重编码用户配置/加密记忆。

`app/bk7258/bk7258_agent_memory_codec.[ch]` 从本团队仓
`129d4f9ad23d9120c0c72500d8d336e0e187fad9` 的
`app/bk7258/bk7258_cloud_memory.[ch]` 保留 Apache-2.0 的 SMP1 策略、
AES-256-GCM SMM1 封装及旧 SD 密文读取部分；不恢复旧 runtime 或 SD 写入实现。
`bk7258_agent_memory.[ch]` 为本项目 Apache-2.0 存储适配，兼容同版本
`bk7258_cloud_history.c` 的 SMH1 线格式，仅通过官方 Session 公共 API 投影和取快照；
策略关闭时不读取或新增保存，迁移本身不产生云请求，新快照仍加密。
`bk7258_agent_vision.[ch]` 为本项目 Apache-2.0 适配，通过当前官方 Agent 工具
provider 与 `llm_chat_tools` 接口连接现有唯一摄像头 owner 和所选受保护 LLM。

### 历史 Media / FFmpeg / Agent 补丁来源（已退役）

本小节保留许可谱系，文件名与“应用到构建副本”等措辞仅描述当时机制，
不表示这些路径仍存在或可用于当前构建；当前接线与未发布依赖以上文为准。

本轮 Media Trigger 集成使用官方 Media 提交
`fb7db0e9f826fb6d71937c948e7da1eb10ffc896` 的 `server/media_trigger.c`
及公开 `media_trigger*.h`；团队 Apache-2.0 补丁
`frameworks/patches/media/0001-trigger-resource-lifetime.patch` 修正 Trigger 资源生命周期，
`0002-stream-io-lifetime.patch` 修正同版本 `client/media_graph.c` 的准备失败清理、
短传输和可重试错误处理，均通过现有 CMake 构建副本应用。
同版本的 `0003-wait-for-audio-route-format.patch` 与
`0004-graph-error-recovery-progress.patch` 修改 `server/audio_graph.c`，
处理格式协商和错误后的拆链；`0005-player-eof-drain.patch` 修改
`server/media_player.c`，保留 EOF 后尚未播放的数据直到排空。均保留 Apache-2.0。
`0006-player-output-release.patch` 基于同一 Media 提交的 `server/media_player.c`，
保留 Apache-2.0；设备释放和 graph unlink 确认后才发布播放终态，避免软件队列
排空早于硬件完成。它是构建时应用的本地未合入补丁。
`bk7258_voice_trigger_model.c` 为本项目适配，
推理及前处理仍取下表 TFLM 版本。`frameworks/cmake/tflm.cmake` 选择 Ruy
实际检出 `cf455c059506d2f64103d7cbb640b99e816b23c7` 的 Apache-2.0
`ruy/profiler/instrumentation.cc` 组件，未复制矩阵引擎或另建线程池。
板级 Media 音量命令依据实际 FFmpeg 检出
`4b4723f2f66ccfdbadbd5d4c52dd5c41d6116418` 的 `libavfilter/asrc_abufsrc.c` 的
`set_parameter` 与 `volume.c` 表达式解析接口；两者保留上游 LGPL-2.1-or-later。
同一 FFmpeg 版本的 `libavfilter/asink_adevsink.c` 由团队
`external/patches/ffmpeg/0001-adevsink-acknowledge-end-of-stream.patch`
修正 EOF 确认和设备输出收尾，保留 LGPL-2.1-or-later；现有 BK7258 构建入口
通过生成的 apps 源码视图交给官方 FFmpeg Make 规则编译，官方检出不修改。
`0002-nuttx-stream-reservation.patch` 同样基于该版本、保留 LGPL-2.1-or-later，
修复 `libavdevice/nuttx.[ch]`、`nuttx_enc.c` 的跨轮设备预留与输出时间戳生命周期。
`0003-build-source-dependencies.patch` 基于 `open-vela/external`
`f2c1425ef199e1dc7393a6b1d9ce14e52a430068` 的 `ffmpeg/CMakeLists.txt` 与
`ffmpeg/Makefile`，保留 Apache-2.0；修正导入归档的同轮链接依赖，并使用编译器
依赖文件跟踪实际输出对象的头文件变化，仍由原 CMake/Make 入口执行。
`0004-asubgraph-preserve-drain-errors.patch` 基于上述 FFmpeg 版本的
`libavfilter/af_asubgraph.c`，保留 LGPL-2.1-or-later；修复排空时赋值代替比较、
调用者吞掉混音和内部输入错误的问题，由同一生成源码视图集成。
`0005-aresample-report-invalid-configuration.patch` 基于同版本
`libavfilter/af_aresample.c`，保留 LGPL-2.1-or-later；检查选项读取结果并保留
采样率、格式及声道一致性条件，失败时返回真实错误，避免终止共享Media进程。
`0006-opt-respect-format-enum-width.patch` 基于同版本 `libavutil/opt.c`，
保留 LGPL-2.1-or-later；格式选项读取使用 API 声明的枚举类型，消除 ARM
短枚举下的四字节越界读写，与已有数值读写处理一致，沿用同一源码视图。
`0007-output-drain-release.patch` 基于同一 FFmpeg 提交的
`libavdevice/nuttx.[ch]`、`nuttx_enc.c`、`libavfilter/asink_adevsink.c` 和
`asrc_abufsrc.c`，保留 LGPL-2.1-or-later；将独占输出的设备 COMPLETE/RELEASE
传递到 graph unlink，并保留设备错误。补丁只在生成源码视图应用。

官方 Agent 扩展基于 `open-vela/packages_ai_agent` 提交
`41723c61725c4e845bfee724f3ad2fafc416b6e1`，许可 Apache-2.0。
`frameworks/patches/ai_agent/0001-request-scoped-provider.patch` 修改其
`Kconfig`、`CMakeLists.txt`、`src/llm/llm_proxy.[ch]`、
`src/core/agent_loop.c`，并提取 `src/core/agent_turn.[ch]`。
对话消息、provider 封包与工具循环来自该官方实现；产品仅注入现有传输、期限、
取消和当次摄像头接口。补丁由 `frameworks/cmake/agent_provider.cmake`
应用到构建副本，不依赖官方检出中的本地修改。

2026-09-15 完整框架迁移继续使用上述 Agent 提交与原许可证。远端比赛分支
`31faed70f683a6f5e690437c5507891360f0814a` 已只读核对，与本地 `41723c61`
具有相同源码树 `39c1309387084fdf079ed7d9b10c3b83cca119ec`，无需升级。

以下均为**本地未合入补丁**，不是上游原有能力。官方检出不修改；构建通过
`frameworks/cmake/agent_framework.cmake` 对受影响文件逐个检查、应用补丁并替换
完整官方应用目标的输入。补丁不适用或源目标不唯一时明确失败；未复制一套 Agent。

| 补丁 | 必须修改的官方文件 | 现有接口不能完成的原因 |
| --- | --- | --- |
| `0002-voice-backend-stream-dispatch.patch` | `include/agent_config.h`、`include/voice/voice_{asr,tts}.h`、`src/voice/voice_{asr,tts}.c`、`volc_asr.c`、`volc_tts.c`、`volc_tts.h`、`volc_tts_ws.c` | 原流式操作绕过注册后端；补可选流式、取消、请求准备、忙时拒绝切换和实际格式/能力快照。已有 Volc 适配同一契约并停止请求中重读配置，不增加通用层厂商分支。 |
| `0003-media-playback-completion.patch` | `include/voice/audio_playback.h`、`src/voice/audio_playback.c`、`voice_channel.c` | 原 close 立即 stop；补 Media 完成/失败排空及关闭失败时的回调资源保留、下次 open 前清理。 |
| `0004-voice-channel-capabilities.patch` | `src/voice/voice_channel.c` | 原通道预连接不支持的 ASR，流式失败只重试尾部 PCM；按能力选同后端批处理、传播错误、先 join 后释放、按请求绑定格式播放，取消同时到后端。 |
| `0005-external-network-configuration.patch` | `src/infra/network_manager.c` | 原硬件网络路径固定通过 shell/wapi 重新配置 Wi-Fi；补观察既有网络服务的通用选择。 |
| `0006-optional-service-startup.patch` | `src/agent_main.c`、`src/tools/tool_registry.c` | CLI、WebSocket、cron、heartbeat 默认无条件启动；补通用构建选择，正式产品关闭无需求的模块。 |
| `0007-voice-auto-endpoint.patch` | `include/voice/{audio_capture,voice_asr,voice_tts}.h`、`src/{agent_main.c,core/{agent_loop,message_bus.[ch]},llm/llm_proxy.[ch],tools/{skill_loader,tool_registry}.c,voice/{audio_capture,voice_asr,voice_channel.[ch],voice_tts}.c}` | 上游没有产品所需的单轮终态关联、自动端点和端到端取消边界：补 voice request ID/终态及自动端点；message bus/Agent/outbound 透传结果；受控外部 LLM transport 与开始前取消检查；可选 capture route 回调；工具和 builtin skills 按既有或新增 Kconfig 选择。它们是通用接口扩展，不含板号、厂商传输或产品状态机。 |
| `0008-playback-wait-output-release.patch` | `src/voice/audio_playback.c` | 等待 Media 的输出释放终态后才销毁播放资源；关闭失败保留回调所有者，有界等待不以固定延时替代完成事件。 |
| `0009-camera-device-format.patch` | `src/tools/tool_camera.c`、`src/llm/{llm_proxy.c,llm_internal.h,llm_vision.c}` | 通过 V4L2 枚举固定 JPEG 尺寸并校验返回缓冲边界；视觉请求接受已选择的外部鉴权传输，不要求通用客户端另存一份 API key。 |

这些通用选项暂由团队 `app/bk7258/Kconfig` 声明，因为官方 Kconfig 在 CMake
派生源码之前已被读取；没有声称构建期改写 Kconfig 生效。配置同启官方 Agent 与
旧 voice service 会明确构建失败。旧请求级 `0001`/`agent_provider.cmake` 仅由
`VOICE_SERVICE` 路径消费（目前 `drivercheck_ap` 仍启用），不进入新正式目标。

`0002` 至 `0006` 本轮未改；`0007` 同样只应用于构建派生副本。官方 pin 仍为
`41723c61725c4e845bfee724f3ad2fafc416b6e1`，官方 checkout 未编辑；构建派生确实
应用补丁。以上是来源和构建输入说明，不表示已部署或语音链路已验证。
`0008`、`0009` 也基于同一官方 pin，保留 Apache-2.0，并由同一构建入口应用；
播放与相机的实际部署和验收范围以现有 Master Plan 当前状态为准。

`app/bk7258/bk7258_cloud_audio.[ch]` 从本仓 cloud client 提取现有音频服务协议；
`bk7258_agent_cloud.[ch]` 在官方 ops 下注册 MiMo/OpenAI 音频协议，持有配置快照、
TLS 及单次请求工作区，复用既有证书/主机名/可信时间验证、HTTP 和流式解码。
两者无录音、播放器、对话上下文、历史、线程或整轮恢复。新增 TTS 选择键复用官方
配置机制，区分 backend/model/voice/location；云适配拒绝未验证的音色和 device
执行位置。现有 CCF1/MCP1 编码、认领身份及 KWS 模型包格式不改变。

### 当前产品协议与性能适配来源

2026-09-18 的回答模式适配依据 [MiMo 官方 Chat Completions 协议](https://mimo.mi.com/docs/en-US/api/chat/openai-api)
中的 `thinking.type=enabled|disabled`；官方文档声明 MiMo v2.5 默认开启思考。
团队实现仅在选定 MiMo 协议、且官方调用者未显式设置该字段时加入设备选择，保留
model/messages/tools、受保护传输及取消契约。App 复用既有 SDC1 配置事务 kind 4，
传送 12 字节 `RSP1 + BE32 thinking + BE32 reserved=0`，ACK 后回读确认；设备复用
既有 KVDB/存储租约保存 `persist.shaniu.thinking`，缺少该新键时默认快速模式，
存储错误不伪装成首次使用。没有复制厂商 SDK 或修改官方 Agent/FFmpeg/NuttX/SDK；
ASR 仍为整段请求，LLM 仍等待完整文本，TTS 为完整文本输入、PCM 流式返回。

2026-09-18 的 Wi-Fi MTU 适配只在 AIDK AP defconfig 显式设置
`CONFIG_NET_ETH_PKTSIZE=1514`。依据锁定 SDK
`cb080de1655d579c7593ecf504c440997c4c137b` 的
`ap/components/bk_wifi_driver/wdrv_main.h`：`MAX_MSDU_LENGTH=1500+14`，以及当前
NuttX `net/Kconfig` 的包大小契约（含 14 字节以太头）。原产品继承 590 字节默认值，
即 IPv4 MTU 576；现有 `bk7258_wifi.c` 收发缓冲已经由 `MAX_NETDEV_PKTSIZE` 派生，
无需复制 SDK 实现或改变公共网络栈。未同时调整 IOB 数量、端点等待或服务参数；
延迟收益以 Master Plan 中对应候选的实板数据为准。

2026-09-18 的 TLS 会话缓存仅修改团队 `bk7258_voice_tls.[ch]` 和
`bk7258_agent_cloud.c`，调用现有 mbedTLS 3.4.0 的公开
`mbedtls_ssl_set_session/get_session`，没有复制密码实现或改依赖。ASR、LLM、TTS
在同一不可变服务/信任配置快照中共享受锁保护的恢复会话，各自保有活动 TLS/socket。
配置替换创建空缓存；最后一个快照引用释放时清除会话。恢复前后检查原证书链时间
边界，保留证书验证、SNI/主机名、可信时间、取消和原请求 deadline；不复用 HTTP
请求，不缓存答案，不重发 POST。当前产品 mbedTLS 已启用客户端会话票据及对端证书
保留，无需改配置或版本。日志 `offered=1` 只表示提交恢复会话，不表示服务器已接受。

2026-09-18 的后续上传优化保留同一 WAV/JSON 请求：团队
`bk7258_cloud_request.c` 用现有 `mbedtls_base64_encode` 连续编码 PCM 主体，
原边界路径处理 WAV 交界、非对齐回调和最终填充，不额外缓存完整 Base64。
`bk7258_cloud_http.[ch]` 仅在既有每请求摘要中补充请求体长度、总耗时、连接与
发送耗时，不记录凭据或正文。626 实板两次 ASR 的发送等待为 1511–1622 ms，
据此在 AIDK AP 配置验证 `IOB_NBUFFERS=128`、`IOB_NCHAINS=36` 的有限调整。
依据当前 NuttX `76354c637858ecb0aa4601629327acb6f44a26bb` 的
`mm/iob/Kconfig` 与 `net/tcp/tcp_send_buffered.c`：TCP 发送/接收共用 IOB 池，
发送路径在池不足时允许部分写入。没有修改公共网络栈或 SDK；配置收益与 RAM
占用由对应候选记录给出，不把调优假设写成上游缺陷。

本轮参考小智固定提交
[`78/xiaozhi-esp32@5d54beb7…`](https://github.com/78/xiaozhi-esp32/tree/5d54beb743ff49c4e8db81bbef9413bdd6e2ba17)：
本地唤醒应答参考 `main/application.cc` 将提示与监听状态衔接的思路，未复制其播放器或会话实现。私人授权音色仅离线生成提示资源，通过既有 Agent/Media 播放；不将主机推理声称为板端 TTS。
`main/protocols/websocket_protocol.cc` 按需建立语音通道，
`main/audio/audio_service.cc` 使用 Opus 分帧上传及独立播放队列。
另只读参考独立社区服务端（不是 78 官方云实现）
[`xinnan-tech/xiaozhi-esp32-server@f4ba65f2…`](https://github.com/xinnan-tech/xiaozhi-esp32-server/tree/f4ba65f2248906e2bb85ba458a80d410cac707c3)：
`main/xiaozhi-server/core/connection.py` 将 LLM 增量放入 TTS 队列，
`core/providers/tts/base.py` 按标点形成首段。这里只借鉴减少串行等待的思路，
没有复制实现、协议或会话运行时，亦没有同条件实板延迟可供排名。当前官方 Agent
接线仍等待完整 LLM 文本；不能在 HTTP 适配回调中私自启动另一条 TTS/Media 链。

本轮只读比较了竞品固定提交
[`d28df626…`](https://github.com/open-vela/contest2026_106_VelaGoGoGo/tree/d28df626a07ed6cbc2c041b933a875b74a0b7dc3)
和官方 `packages_demos` Gitee `dev` 当时提交
[`b5bb9407…`](https://gitee.com/open-vela/packages_demos/tree/b5bb9407146f6a2f2afe576c7b0aef0ef80bfd20)。
竞品使用豆包实时协议，但本地 `voice_player.c` 收齐整轮 TTS 后才播放；官方
`ai_chat` 的 Volc 插件接收音频增量并交给 Media，而 `mimo` 是整包响应的文字示例。
这些实现均不是本产品选定 MiMo ASR/LLM 的直接替代。未复制其会话引擎、启用示例、
切换服务商，亦未采用示例中的放宽证书/主机名校验设置。MiMo ASR 的
[`stream=true`](https://mimo.mi.com/docs/en-US/api/audio/Speech-Recognition)
是整段音频输入后的文字 SSE 输出，不据此宣称支持实时 PCM 上行。

`bk7258_agent_trigger.c` 是 Media Trigger 下的 TFLM 模型适配，保留已维护的
模型张量/前处理和冻结分数策略；没有搬入旧 wake window、VAD、Recorder、Agent
或会话 owner。此保留不等于新链路或真人唤醒效果已经验证。

`bk7258_agent_ota.[ch]` 是本地产品升级来源适配，复用本仓已授权的 App SDC1
协议、HTTPS source、AP OTA manager 与受保护 BVO2 store/flow。它只拥有一次请求
的来源/CA/worker 和 intent 发布；Flash 事务及启动确认仍归原系统服务。没有复制
旧 voice runtime 的语音调度、gateway、会话或 OTA 引擎，也没有修改官方源码。
断电后不自动重建临时手机 URL；同一 catalog 的重试须由 App 明确再次发起。

本地 TTS 接入边界已准备，具体引擎与模型未验证。通用接口不要求联网、地址或
API Key；init/deinit 负责准备/释放，prepare_request 只负责短请求状态重置。
切换先释放旧模型再加载新模型，失败不自动回退；资源预算为 0 时表示未知。
没有新增本地空实现、模型平台、训练任务或私有音色资产。

以下 FAT patch 路径为已退役的历史许可记录，不是当前同步或构建步骤：

`nuttx/patches/fs/0002-drain-block-writes-before-sync-unmount.patch` 与
`0003-support-fat-open-file-path.patch` 基于 `open-vela/nuttx`
`76354c637858ecb0aa4601629327acb6f44a26bb` 的 `fs/fat/fs_fat32.[ch]`，
保留 Apache-2.0，依次叠加既有 `0001` 补丁。它们分别补齐块设备同步收尾和
`FIOC_FILEPATH`，由现有隔离构建补丁机制消费，不复制 FAT 实现。

CP 蓝牙 `rand()` 中断适配依据本项目 440 ELF 的
`lld_adv_frm_isr → rand → nrand` 调用及 manifest NuttX
`76354c637858ecb0aa4601629327acb6f44a26bb` 的
`libs/libc/stdlib/lib_srand.c`/`include/nuttx/spinlock.h`，调用既有
`rand_r()`，未复制随机数算法或改变硬件熵源；适配代码 Apache-2.0。

| 范围 | 来源与许可处理 |
|---|---|
| `app/bk7258/models/nihao_openvela.tflite` 与对应 metadata | 本项目通过既有 `voice kws train` 训练的通用合成语音候选，产品唤醒词为“你好，open-vela”；当前 v46 正例实际合成文本为“你好，open vila”，用于得到产品词 open-vela 的 /ˈoʊpən vˈiːlə/（维拉）发音；依据为同一 Kokoro/sherpa 运行时 ConvertTextToTokenIds 输出及模型 tokens.txt 反解，不以静态词典代替实际发音，不包含私人录音或声纹训练。标准 TTS 为 [Kokoro-82M-v1.1-zh](https://huggingface.co/hexgrad/Kokoro-82M-v1.1-zh) 的 sherpa-onnx 多语言 INT8 分发（模型 SHA256 `bda15858163726a492d02a9a727bc263551b86ac77f90812c4b30ff41d380e26`，Apache-2.0）；普通语音训练反例为 [Google FLEURS](https://huggingface.co/datasets/google/fleurs) `cmn_hans_cn` dev 的 30 条录音（CC-BY-4.0，保留 Google FLEURS 署名与许可）。训练和独立合成评估按原始音色、录音来源分组；实际生成提示、音素依据、速度覆盖、音频哈希与冻结 manifest 保存在交付模型资产中。当前 v46 沿用的背景资产包含 [Microsoft MS-SNSD](https://github.com/microsoft/MS-SNSD) 的 6 段 noise_train 录音切出的 72 个训练背景窗口；按其 Freesound CC0 / DEMAND CC-BY-SA-3.0 混合来源说明保留署名与许可，原项目缺少逐文件原始谱系映射，不能把这些素材统一宣称为 CC0。环境回归使用不同 noise_test 文件，但不能仅凭文件哈希不同宣称原始来源完全独立。它们均不作为真人目标词证据。真人泛化、实际端侧资源与声学效果仍须分别核验。 |
| `nuttx/drivers/contactless/isodep.c`、`nuttx/include/nuttx/contactless/isodep.h` | 团队 Apache-2.0 实现，激活参数依据 ISO/IEC 14443-4:2018 第 5 节（公开预览）与 NXP AN12057 Rev. 1.2（2026-07-03）；不复制外部协议栈代码。 |
| `nuttx/drivers/contactless/mfrc522.{c,h}`、`nuttx/include/nuttx/contactless/mfrc522_frame.h` 与 `nuttx/patches/contactless/0001-*` 至 `0005-*` | 基于 `https://github.com/open-vela/nuttx` 提交 `76354c637858ecb0aa4601629327acb6f44a26bb` 的 `drivers/contactless/mfrc522.{c,h}` 和 `include/nuttx/contactless/ioctl.h`（Apache-2.0），保留上游许可。团队差分提供错误传播、CRC_A 帧交换与超时控制；定时器行为参照 NXP MFRC522 Rev. 3.9（2016-04-27）手册 8.5、9.3.3.10 节，不复制手册正文。 |
| `app/bk7258/bk7258_voice_kws*`、`tools/bk7258/_lib/voice_kws.py` 和对应 host tests | 本仓 Apache-2.0 适配；直接编译工作区 `apps/mlearning/tflite-micro/tflite-micro` 的 `tensorflow/lite/experimental/microfrontend/lib`，调用同树 `tensorflow/lite/micro/{micro_interpreter.h,micro_mutable_op_resolver.h}`（Apache-2.0），不复制前端或推理实现。本轮验证的实际 TFLM 提交为 `94f7cee178aeceb492a074b4e092db2706d7c9c2`；manifest 跟随 `openvela/dev-ai-contest-2026`，并非外层 Make 下载回退值 `cfa4c91…`。固定点 FFT 由上游 `kiss_fft_int16.cc` 编入现有 `apps/math/kissfft/kissfft` v130 的 `kiss_fft.c`、`tools/kiss_fftr.c` 及头文件，许可 BSD-3-Clause（Mark Borgerding，见该目录 `COPYING`）。候选 metadata 保存实际前端源/头内容哈希；发布时保留这些上游许可。训练采用 TensorFlow/Keras 2.15.1；真实语料与权重的许可和验收随资产独立提供。 |
| `frameworks/patches/kvdb/0002-unqlite-explicit-journaled-commit.patch` | `open-vela/frameworks_system_utils` 提交 `5e582301ffa1401d1e48d49cea46edeba97b822e` 的 `kvdb/unqlite.c`、`kvdb/direct.c`，Xiaomi Apache-2.0；团队差分恢复日志和显式提交。 |
| `external/patches/unqlite/0001-propagate-commit-sync-errors.patch` | `https://github.com/open-vela/external_unqlite` 提交 `25731ab0e2a4aa119df1329f799cd571a794720c` 的 `unqlite.c`，Symisc BSD-2-Clause；保留原版权/许可和 CRLF，仅维护同步错误传播差分，主机测试应用于临时副本。 |
| `frameworks/patches/kvdb/0001-file-handle-partial-interrupted-io.patch` | 源自 `https://github.com/open-vela/frameworks_system_utils` 提交 `5e582301ffa1401d1e48d49cea46edeba97b822e` 的 `kvdb/file.c`（Xiaomi Apache-2.0）。团队维护 I/O 语义差分，host 测试只在临时副本应用；未修改官方 checkout 或启用板级持久化。 |
| `app/bk7258/bk7258_preferences.[ch]` | 本仓 Apache-2.0 产品配置适配，调用 OpenVela `frameworks/system/utils/include/kvdb.h` 的公开 API；KVDB 实现保留在原仓，未复制存储后端。配置后端与掉电验收尚未闭合，默认不启用；不存储凭据。 |
| `app/hello_app/hello_app_main.c` | 来自本仓初始脚手架提交 `8987bbc`，并在 `7d9c26c` 统一为 team 135；本轮按该基线逐字恢复，不为许可证格式单独改写模板。仓库根 `LICENSE` 为 Apache-2.0。 |
| `app/bk7258/*.c` | 13 个 BK7258 命令与 3 个 Agent 生命周期/轻量音频桥接源均由本仓创建；命令从 hello 模板目录分离到独立产品应用，Agent 源则从板层迁入应用层。全部适用 Apache-2.0。 |
| `boards/bk7258/*/include/board.h` | 本仓提交 `eaef241` 创建的三个最小板级转发头，不复制其他 NuttX 板实现；适用仓库默认 Apache-2.0。 |
| `nuttx/drivers/lcd/gc9d01.c` | 初始化序列源自 Beken BK-AVDK v3.1.1.9 的 `components/bk_peripheral/src/lcd/spi/lcd_spi_gc9d01.c`（Apache-2.0）；本仓重写为传输无关、可上游化的 NuttX LCD 驱动，未暴露 SDK 私有面板对象。 |
| `nuttx/drivers/lcd/ili9488_rgb.c` | 初始化序列源自 `tuya/TuyaOpen-T5AI` 固定提交 `13379b63e07e78770fb4d0bffe36db2754658132` 的 `tuyaos/tuyaos_adapter/src/test/test_dvp/lcd_ill9488.c`（仓库根许可证 Apache-2.0）；本仓实现仅保留通用寄存器序列和传输回调，并复用官方 NuttX `ili9488.h` 命令定义。 |
| `nuttx/drivers/input/gt9xx.c` | 基线逐文件来自 manifest 工作区 `open-vela/nuttx` 固定提交 `76354c637858ecb0aa4601629327acb6f44a26bb` 的 `drivers/input/gt9xx.c`（Apache-2.0）；本仓补齐标准 `TSIOC_GETMAXPOINTS` ioctl、无报告时的非阻塞EAGAIN，以及按控制器ready/触点数上报DOWN/MOVE/UP（移除合成抬手），并以独立 Kconfig/build gate 替代而非同时链接官方实现。GPIO、复位、电源和 bitbang-I2C 实例策略仍由物理板绑定提供。 |
| `nuttx/drivers/sensors/sc7a20.c` | 设备 ID、寄存器、量程和 ODR 语义源自 manifest 固定的 Beken BK-AVDK v3.1.1.9 提交 `cb080de1655d579c7593ecf504c440997c4c137b` 的 `ap/components/bk_gsensor/gsensor_sc7a20.c`（仓库根许可证 Apache-2.0）；本仓重写为硬件无关寄存器 transport 和标准 NuttX uORB sensor lower-half，未复制 SDK 线程、私有回调或 I2C/GPIO 实例策略。 |
| `chips/bk7258/bootloader/` | BL1、BL2、链接脚本及板级 MCUboot 配置/ABI 由本仓提交创建。BL2 在构建时链接工作区 `apps/boot/mcuboot/mcuboot` 的固定上游源码；仓内文件只是 BK7258 启动、Flash map、安全计数和最小配置适配，不包含上游 bootutil/TinyCrypt 实现副本。两侧均为 Apache-2.0。 |
| `chips/bk7258/ap/bk7258_ble_scan.c` / `include/bk7258_ble_scan.h` | 本仓原创Apache-2.0异步有界列表适配；Host启停/回调语义核对本地NuttX提交`76354c637858ecb0aa4601629327acb6f44a26bb`的`wireless/bluetooth/bt_hcicore.c/.h`（Apache-2.0）。未复制Host实现；内部函数声明仅保留在chip边界，HCI同步调用与Host接收LPWORK分离。 |
| `chips/bk7258/common/bk7258_os_adapt.c` | 本仓面向 NuttX 编写的 SDK OS 适配层，文件原有完整 ASF Apache-2.0 许可正文；本轮仅增加 SPDX。 |
| `chips/bk7258/ap/bk7258_sdio.c` | SDIO TX DMA 启用及通道归属检查依据 manifest 固定的 Beken BK-AVDK v3.1.1.9 提交 `cb080de1655d579c7593ecf504c440997c4c137b` 的 `ap/middleware/driver/sdio_host/sdio_host_driver.c` 与 `ap/include/driver/dma.h`（Apache-2.0）；复用 SDK 公开 API，未复制私有 DMA 状态机。SDK 开关来自本仓 ap-aidk profile 并由规范 SDK rebuild 入口生成匹配静态库。 |
| `chips/bk7258/common/bk7258_sdk_partition.{c,h}` | 本仓实现的 SDK 语义分区 ID 与生成布局行之间的显式转换；枚举顺序对齐 manifest 固定的 Beken BK-AVDK v3.1.1.9 提交 `cb080de1655d579c7593ecf504c440997c4c137b` 所产出 SDK profile `chips/bk7258/bk_idk/armino_as_lib/versions/v3.1.1.9/cp-aidk/include/partitions.h`（生产源的 `cp/include/driver/flash_partition.h` 通过 `<partitions.h>` 消费该枚举），未复制 SDK Flash 实现。上述 SDK 头及本仓实现均为 Apache-2.0。 |
| `chips/bk7258/include/eth_mac*.h`、`lan8742.h` | 来自 manifest 固定的 Beken SDK v3.1.1.9 Ethernet 公开头。原文件保留 Beken 版权和完整 Apache-2.0 正文；其中 `lan8742.h` 与 SDK 相同，其余仅有换行或已注明的 NuttX 符号兼容调整。 |
| `docs/platforms/bk7258/hardware/t5ai-core/probe/*.{c,ld}` | 本仓提交 `56b303e` 创建的历史实板探针源码，适用仓库默认 Apache-2.0。 |
| `boards/bk7258/build/vendorsetup.sh` | 本仓提交 `eaef241` 创建的构建环境适配脚本；随完整板目录映射进入 OpenVela 的 vendor 树，适用仓库默认 Apache-2.0。 |
| `tools/windows-hardware-debug/**/*.{cpp,ps1}` | 本仓硬件调试工具，由 2026-07-31 至 2026-08-03 的调试与 BLE 验证提交创建；10 个文件在本轮前已声明 Apache-2.0 SPDX。 |

Beken SDK 由 [`contest2026_135_yongwangzhiqian.xml`](contest2026_135_yongwangzhiqian.xml)
固定在提交 `cb080de1655d579c7593ecf504c440997c4c137b`，其根 `LICENSE` 和上述
Ethernet 公开头均声明 Apache-2.0。MCUboot 由 openvela 工作区的 `apps` 项目提供，
其上游目录保留独立 `LICENSE` 和 `NOTICE`。

BK7258 主机测试的更细分类见
[`tests/host/bk7258/PROVENANCE.md`](tests/host/bk7258/PROVENANCE.md)。第三方项目、预构建工具、
生成输出及历史材料继续适用各自声明；SPDX 补齐不改变其版权归属。

## 历史 Gateway 语音服务（非现役产品路径）

Gateway 的自有协议、MiMo 适配、外部依赖及测试来源独立维护于
[Gateway 来源说明](gateway/shaniu/SOURCE_PROVENANCE.md)；模型协议适配不包含第三方源代码副本。

## KVDB 适配与历史构建接入

`app/bk7258/bk7258_preferences_storage.*` 及配套主机测试为本项目 Apache-2.0
实现，复用既有 `bk7258_media_volume` 占用接口及 NuttX mount/umount 公共接口。

下面构建 patch 路径已退役，只保留许可来源：

`frameworks/cmake/kvdb_patches.cmake` 为本项目 Apache-2.0 构建接入代码，
仅在输出目录消费 `frameworks/patches/README.md` 列明的 framework/UnQLite
维护补丁；生成副本保留原 Apache-2.0 / Symisc BSD-2-Clause 许可，不另复制上游实现。

## 历史 BLE GATT 通知补丁（已退役，不作为当前入口）

`nuttx/patches/bluetooth/0001-gatt-report-notification-enqueue-result.patch`
派生自 OpenVela NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`
的 GATT 源码/头文件，保留其 BSD-3-Clause 许可；相应 host harness 为本项目
Apache-2.0 实现。官方 NuttX 工作树不作修改，补丁仅应用到隔离构建副本。
`nuttx/patches/bluetooth/0003-gatt-ccc-do-not-allocate-unbonded-key-slot.patch`
派生自同一 GATT 源码，使用既有非分配式 key 查询区分长期密钥与未配对连接，
同样仅应用到隔离构建副本。
`nuttx/patches/bluetooth/0004-att-cap-mtu-to-receive-buffer.patch` 派生自同一
NuttX 的 ATT 源码，保持较小 peer MTU，仅限制不能完整进入接收缓冲的协商上限。

## 认领 TLS 与 GATT

首段 patch 路径为历史来源；后续产品 TLS/认领适配仍按实际配置使用。

`nuttx/patches/bluetooth/0002-expose-gatt-connection-lifecycle.patch` 将同一
OpenVela NuttX 基线的 `wireless/bluetooth/bt_hcicore.h` 既有连接回调结构和
Host 函数声明公开到 `include/nuttx/wireless/bluetooth/bt_gatt.h`，保留上游
BSD-3-Clause 许可，不复制 Host 实现或改变 SDK。

`app/bk7258/bk7258_provision_tls.[ch]` 和
`tests/host/bk7258/test_provision_tls.{c,py}` 为本项目 Apache-2.0 实现，调用
工作区 `apps/crypto/mbedtls/mbedtls` 的公开 mbedTLS 3.4.0 API（Apache-2.0，
官方 apps `crypto/mbedtls` 从 `ARMmbed/mbedtls` 的 v3.4.0 发布包引入）；
不复制密码算法，不改上游源码。主机 gate 使用该目录默认主机构建配置，
只对上游 PSA helper 的 missing-prototypes 保留 warning 而不升级 error；
产品源仍使用 `-Werror`。身份由 OpenSSL 生成临时 P-256 测试证书，结束删除。
板端使用 AIDK 的实际 mbedTLS 配置另做镜像构建，主机配置不作为目标验收。

`app/bk7258/bk7258_provision_owner.[ch]` 和
`tests/host/bk7258/test_provision_owner.c` 为本项目 Apache-2.0 实现。
复用现有 AP 语音任务、CP 按键租约及 provision pair/storage API，
不复制 SDK 的按键或 BLE 示例状态机。

`app/bk7258/bk7258_provision_gatt.[ch]` 及
`tests/host/bk7258/test_provision_gatt.py` 为本项目 Apache-2.0 实现，使用
NuttX 公共 GATT/UUID/锁 API 与团队维护的定向通知接口；不包含 SDK 私有设备对象。

2026-09-18 的 CCC 生命周期适配仅在该产品断开回调清理自有易失订阅表。
契约核对基于上述固定 NuttX 提交的 `bt_gatt.c`、`bt_keys.c` 与 `bt_att.c`；
没有复制 Host 的连接运行时或改动其 checkout，也未改变配对密钥和设备认领数据。

`android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/`
及相应 host tests 为本项目 Apache-2.0 实现，调用 Android/JVM 公共 JSSE、
X509Certificate 和 MessageDigest API，没有复制密码库或上游 Bluetooth 实现。
测试身份由本机 JDK keytool 临时生成，测试结束删除，不包含真实设备凭据。

## 摄像头与 SDIO 录像适配

本节 `.patch` 条目仅保留历史来源与用途，相关应用机制已退役；
当前 SDK checkout 与构建不得据此重新启用补丁。

- `chips/bk7258/bk_idk/sdk-profiles/v3.1.1.9/cp-flash-notification-errors.patch`
  基于 `https://github.com/Embracecactus/bk_avdk_smp` 固定提交
  `cb080de1655d579c7593ecf504c440997c4c137b`（v3.1.1.9）的
  `cp/middleware/driver/flash/{flash_notify,flash_driver}.c`，保留 Beken Apache-2.0。
  它传播跨核 ACK 耗尽和 prepare/finish 错误，平衡调度与锁清理；仅在 `cp-aidk`
  的临时构建克隆应用。该修复不等于 App OTA 写后校验故障已解决。

- `nuttx/drivers/video/gc2145.c`、公开头文件及板级回调为本项目 Apache-2.0
  实现。控制寄存器依据 GalaxyCore GC2145 CSP DataSheet V1.0
  （2013-12-01）第 22、23、32、35 页；默认值参照固定 SDK 的
  `ap/components/bk_peripheral/src/dvp/dvp_gc2145.c` 初始化表。
- `chips/bk7258/bk_idk/sdk-profiles/v3.1.1.9/ap-sdio-tx-start.patch` 和
  `ap-dvp-register-errors.patch` 派生自固定 Beken 提交
  `cb080de1655d579c7593ecf504c440997c4c137b`（Apache-2.0），分别修复 SDIO
  FIFO 中断掩码顺序/判定和 DVP/GC2145 寄存器错误传播。规范 rebuild 仅为
  `ap-aidk` 在临时克隆应用，未修改固定 SDK checkout。
- `nuttx/patches/{video,mmcsd,fs}` 是针对 OpenVela NuttX
  `76354c637858ecb0aa4601629327acb6f44a26bb` 的 Apache-2.0 修复，涵盖
  V4L2 scalar 控制初始化/编号、MMCSD 传输限制/完成与 FAT 错误传播。
  此项仅记录历史来源；补丁目录及应用机制已退役，不属于当前构建步骤。
- `app/bk7258/bk7258_vision_*`、`bk7258_media_volume.*`、I2C 资源引用计数和
  配套宿主回归是本项目实现，使用 Apache-2.0。
- BK7258 HardFault 复位原因 `0x11` 复用清单固定的 Beken SDK v3.1.1.9
  `cp/include/components/system.h` 中 `RESET_SOURCE_HARD_FAULT`（Apache-2.0）；
  芯片层保留编译期 ABI 校验，自动复位策略复用 NuttX
  `BOARD_RESET_ON_ASSERT`，不修改 SDK 复位实现。
