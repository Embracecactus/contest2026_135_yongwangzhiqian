<!-- SPDX-License-Identifier: Apache-2.0 -->
# 源码许可证与来源记录

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

这些通用选项暂由团队 `app/bk7258/Kconfig` 声明，因为官方 Kconfig 在 CMake
派生源码之前已被读取；没有声称构建期改写 Kconfig 生效。配置同启官方 Agent 与
旧 voice service 会明确构建失败。旧请求级 `0001`/`agent_provider.cmake` 仅由
`VOICE_SERVICE` 路径消费（目前 `drivercheck_ap` 仍启用），不进入新正式目标。

`app/bk7258/bk7258_cloud_audio.[ch]` 从本仓 cloud client 提取现有音频服务协议；
`bk7258_agent_cloud.[ch]` 在官方 ops 下注册 MiMo/OpenAI 音频协议，持有配置快照、
TLS 及单次请求工作区，复用既有证书/主机名/可信时间验证、HTTP 和流式解码。
两者无录音、播放器、对话上下文、历史、线程或整轮恢复。新增 TTS 选择键复用官方
配置机制，区分 backend/model/voice/location；云适配拒绝未验证的音色和 device
执行位置。现有 CCF1/MCP1 编码、认领身份及 KWS 模型包格式不改变。

`bk7258_agent_trigger.c` 是 Media Trigger 下的 TFLM 模型适配，保留已维护的
模型张量/前处理和冻结分数策略；没有搬入旧 wake window、VAD、Recorder、Agent
或会话 owner。此保留不等于新链路或真人唤醒效果已经验证。

本地 TTS 接入边界已准备，具体引擎与模型未验证。通用接口不要求联网、地址或
API Key；init/deinit 负责准备/释放，prepare_request 只负责短请求状态重置。
切换先释放旧模型再加载新模型，失败不自动回退；资源预算为 0 时表示未知。
没有新增本地空实现、模型平台、训练任务或私有音色资产。

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

## Gateway 语音服务

Gateway 的自有协议、MiMo 适配、外部依赖及测试来源独立维护于
[Gateway 来源说明](gateway/shaniu/SOURCE_PROVENANCE.md)；模型协议适配不包含第三方源代码副本。

## KVDB 构建接入

`app/bk7258/bk7258_preferences_storage.*` 及配套主机测试为本项目 Apache-2.0
实现，复用既有 `bk7258_media_volume` 占用接口及 NuttX mount/umount 公共接口。

`frameworks/cmake/kvdb_patches.cmake` 为本项目 Apache-2.0 构建接入代码，
仅在输出目录消费 `frameworks/patches/README.md` 列明的 framework/UnQLite
维护补丁；生成副本保留原 Apache-2.0 / Symisc BSD-2-Clause 许可，不另复制上游实现。

## BLE GATT 通知维护补丁

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

`android/shaniu-companion/app/src/main/java/com/shaniu/companion/provision/`
及相应 host tests 为本项目 Apache-2.0 实现，调用 Android/JVM 公共 JSSE、
X509Certificate 和 MessageDigest API，没有复制密码库或上游 Bluetooth 实现。
测试身份由本机 JDK keytool 临时生成，测试结束删除，不包含真实设备凭据。

## 摄像头与 SDIO 录像适配

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
  保留为维护补丁，应用步骤见 [补丁说明](nuttx/patches/README.md)。
- `app/bk7258/bk7258_vision_*`、`bk7258_media_volume.*`、I2C 资源引用计数和
  配套宿主回归是本项目实现，使用 Apache-2.0。
- BK7258 HardFault 复位原因 `0x11` 复用清单固定的 Beken SDK v3.1.1.9
  `cp/include/components/system.h` 中 `RESET_SOURCE_HARD_FAULT`（Apache-2.0）；
  芯片层保留编译期 ABI 校验，自动复位策略复用 NuttX
  `BOARD_RESET_ON_ASSERT`，不修改 SDK 复位实现。
