# BKVoice AIDK 授权音色伴侣：历史方案（已退出当前产品主线）

本文保留早期自有 Gateway、语音运行时与授权音色探索的设计来源和历史证据，不是
当前适配任务清单，也不要求恢复文中旧入口。当前设备端采用官方 Agent/Session/Voice/
Media，控制 App 不承担对话或音频中转；现状与优先级只见
[傻妞全项目 Master Plan](shaniu-master-plan.md)。历史源码按当前调用者及复现用途保留。

## 1. 产品目标与当前边界

BKVoice 面向 AIDK AI Toy，目标是做一台可随身携带、能看、能听、能说、能陪用户出行的
语音与视觉伴侣。AIDK 是唯一运行终端；手机只承担可选配网、位置授权和网络中继，用户自有
Gateway/GPU 主机承担动态 ASR、VLM、LLM、授权音色 TTS、唱歌生成和长期记忆。

产品伴侣名固定为“傻妞”，稳定 machine ID 为 `shaniu`。`fictional-ex-girlfriend` 只描述
虚构的互动风格；设备、Gateway prompt、日志和 UI 的真实身份始终是 `ai-companion`，并持续
披露 AI 与合成声音。人物设定不冒充真人，也不自动授予对任何真人聊天、声音或肖像的使用权。

移动端首版固定为原生 Android 完整控制台；微信小程序只作为后续经用户 Gateway 接入的
轻客户端。Android 负责 BLE 近场认领、Wi-Fi/Gateway 配置、设备状态、隐私、情绪偏好、
单帧图片和受控视频预览；小程序不直接承担底层配网、持续视频、OTA 或诊断。详细决策、
协议边界与验收阶段见 [傻妞 Android companion 计划](shaniu-android-companion-plan.md)。

产品不把“所有模型都塞进 MCU”当目标。板端必须独立完成隐私指示、唤醒、VAD、会话状态、
音频/相机资源仲裁、离线降级和安全更新；高质量生成任务按能力卸载到用户控制的主机。

以下是早期离线播放纵切的功能范围；当前执行和实板进度统一见主计划第 17 节：

1. 从已挂载文件系统读取 `bkvoice-pack-v1`；
2. 严格检查授权声明、合成声音披露和音频格式；
3. 按 clip ID 选择资产；
4. 通过公共 NuttX `media_player`/audio ABI 流式播放；
5. 通过同一公共播放 ABI 提供固定、低幅度的非语音喇叭诊断；
6. 在串口输出机器可读的 PASS/FAIL 结果。

早期播放 profile 没有启用 TFLite Micro、CMSIS-NN、Media、uORB、healthd、MQTT、Keystore 或
LVGL 产品 UI，也没有安装端侧唤醒模型。AIDK CP/AP 直接构建和 Gateway 侧 GPT-SoVITS
V2Pro 单轮微调、加载、合成 smoke 已通过，但两者尚未完成实机流式联调；不能把主机构建、
checkpoint 存在或一次 WAV 合成写成产品音色已经 `SELECTED`。文字聊天记录可用于后续的
表达风格检索或经脱敏后的主机侧训练，但不能单独产生音色；音色训练仍需说话者明确同意的
语音样本。仓库不保存私人聊天正文、原始语音、模型权重、密钥或可识别个人身份的信息。

2026-09-01 的隐私最小化源数据审计只使用 direct-chat 标记、send/receive 方向和 WAV
头信息：1,537 个 WAV 中，`received` 候选为 965 个、约 2,927.44 秒（48 分 47 秒），
`sent` 为 566 个、约 3,824.88 秒，另有 6 个方向冲突默认排除。由此可知“总计约两小时”
不是“目标说话人约两小时”；双方素材绝不能混训。该审计只证明方向关联和格式，不证明
音质、转写正确、单说话人纯度或训练就绪。

同日第一轮本地质量门已真实执行：965 个 `received` 候选中保留 949 个（约 2,825.74 秒），
拒绝 16 个。ASR 对其中 947 个生成私有草稿，2 个空结果继续排除；自动 speaker embedding
门保留 924 个主说话人候选并隔离 23 个异常，最后冻结 248 个训练片段（约 15.02 分钟）和
34 个评测片段（约 2.03 分钟）。私有语音使用脱敏文件名的符号链接，未复制进仓库。
当日 worklog 状态是 `VOICE_MODEL_SMOKE_COMPLETE`：特征、S2/S1 单轮微调和本地合成格式门通过，
但零样本对照、holdout 内容正确率/音色相似度、盲听、资产签发和 AIDK 实机流式播放仍未通过；
因此仍不存在可发布的 `SELECTED` 音色资产。

## 2. 分层与不可破坏的依赖方向

| 层 | 职责 | 禁止事项 |
|---|---|---|
| OpenVela Framework | TFLM、CMSIS-NN、Media、uORB、healthd、MQTT、Keystore、LVGL/UIKit、AI Agent 公共 API | 不复制到 BK7258 私有目录，不写 AIDK 引脚 |
| Chip (`chips/bk7258`) | SDK/SoC 初始化、NuttX lower-half、PSRAM/缓存/DMA/PM、硬件加速、SoC 能力查询和每个外设的安全生命周期 | 不包含人物、屏幕布局、语音包、网络账号、产品会话优先级或对话策略 |
| Board (`boards/bk7258/aidk_ai_toy`) | PA/MIC/双屏/相机/SD NAND/电池/NFC 等物理绑定、GPIO、电平和设备节点 | 不直接初始化通用 SDK 子系统，不实现模型或云协议 |
| App (`app/bk7258`) | 产品状态机、会话协议、模型/音色资产、阈值、UI 页面、Gateway 策略和授权流程 | 不包含 pinmux、寄存器地址或 SDK 私有调用 |
| User Gateway / training | ASR/VLM/LLM/TTS/唱歌、记忆、数据清理、授权留档、模型训练与资产签发 | 不成为出厂固件构建依赖，不把私人数据提交到 Git |

运行时依赖方向是 `App/Framework -> 公共 NuttX/OpenVela API 与设备节点`。Board 在启动
阶段向 Chip lower-half 传入引脚、总线、器件数量和设备节点等物理配置；Chip 不能反向引用
具体 Board，App 也不能通过 Board 私有 API 操作硬件。Framework 保持 OpenVela 原位；所谓
“做到 Chip 层”是补齐可跨 BK7258 板复用的 backend、allocator、PM、加速和 SoC 原语，而
不是 fork 一份框架到 `chips/bk7258`。

能力信息也必须分开：PSRAM 实测容量、DMA/cache/PM/TRNG 和控制器限制属于 Chip；AIDK
是否焊接 MIC、PA、相机、两块 GC9D01、SD NAND 和电池属于 Board。只有出现第二个真实
调用者并证明可跨板复用后才增加通用 capability API，首轮不为未来假设建立“大一统资源
管理器”。产品 turn/session 仲裁归 App；若标准 Media profile 启用，media focus/policy
归 Framework；Chip 只执行底层 reserve、互斥和异常恢复。

应用只接受文件路径，不负责挂载介质。这样可避免 App 抢占板级 SDIO 生命周期，也保留
`usbmode msc` 对 `/dev/mmcsd0` 的独占契约。

## 3. 语音包格式

`voicepack.ini` 是 UTF-8/ASCII 兼容的逐行 `key=value` 文件。未知字段、重复字段、
绝对路径、`..` 路径穿越、超过 32 个 clip 或不符合固定音频 tuple 的输入都会失败关闭。

```ini
format=bkvoice-pack-v1
speaker_id=private_voice_01
authorization=explicit-consent
disclosure=synthetic-voice
sample_rate=16000
channels=1
encoding=pcm-s16le
clip.greeting=clips/greeting.wav
clip.goodnight=clips/goodnight.wav
```

约束：

- `speaker_id` 必须是匿名 ID，只允许字母、数字、点、下划线和连字符；
- `authorization=explicit-consent` 是语音包制作者的本地声明，不等同于密码学证明；
- 所有输出都必须按合成声音处理，产品 UI、日志或交互入口不得隐藏这一事实；
- WAV 必须是 RIFF/WAVE、PCM tag 1、16 kHz、单声道、16 bit little-endian；
- clip 必须是相对 manifest 的安全相对路径。

## 4. AIDK 存储准备

AIDK 启动后将焊接 SD NAND 注册成 `/dev/mmcsd0`，但不会自动挂载。`/data` 是条件式
片内 LittleFS，不是这颗 NAND。示例准备流程如下，具体挂载目录可以改变：

```text
nsh> usbmode cdc
nsh> mkdir /mnt/voice
nsh> mount -t vfat /dev/mmcsd0 /mnt/voice
nsh> bkvoice verify /mnt/voice/voicepack.ini
nsh> bkvoice play /mnt/voice/voicepack.ini greeting
```

当 `/dev/mmcsd0` 通过 USB MSC 导出给电脑时，AP 必须保持未挂载，`bkvoice` 也不能
同时读取该盘。安全顺序是：电脑弹出磁盘，切回 CDC，再由 AP 挂载和播放。

## 5. 命令与判据

```text
bkvoice status
bkvoice capture-test <duration-ms:100..5000>
bkvoice tone-test
bkvoice verify <manifest>
bkvoice play <manifest> <clip-id>
bkvoice stress <manifest> <clip-id> <count:1..100>
```

- `status` 报告音频 tuple、块设备是否存在、手工挂载策略以及当前模型状态；
- `verify` 仅确认 manifest 结构与授权/披露字段，不把声明当成法律或身份验证；
- `play` 再验证目标 WAV，播放前打印 `BKVOICE SYNTHETIC`，自然 drain 后才打印 PASS；
- `stress` 只验证一次 manifest，再有界重复播放并报告首个失败轮次，供 50/100 次资源
  回收实机验收使用；
- `capture-test` 只在操作者显式命令期间开启短生命周期本地统计 sink；原始 PCM 不落盘、
  不通过 RPMsg 返回，PASS 至少要求完整 640-byte 帧、非零样本和非零峰值；
- `tone-test` 固定生成 1 kHz、500 ms、峰值 4096 的 16 kHz/mono/S16LE 非语音信号，按
  640-byte/20 ms 帧完成 partial write，并在连接丢失或 stop/close 失败时返回失败；它不读取
  voice pack，PTT/MIC/DAC owner 忙时也不抢占；
- 任一解析、文件、reserve、queue、播放或清理错误都返回失败，不能用成功日志掩盖。

当前 `status` 分开报告服务、配置、连接、Gateway、TLS、PTT 链路、按键和 turn 状态；
启用 `BK7258_VOICE_TLS` 的构建启动时报告 `transport=tls-wss`。仓内 `companion-v1`、
半双工 turn arbiter、公共 `media_recorder/media_player` adapter、joinable capture/PTT owner、
CP GPIO PTT 事件和 TLS/WSS session 已进入 AIDK 产品配置与 AP 构建。host fake-media/
fake-sink test 覆盖 MIC/DAC 调用顺序、完整 640-byte/20 ms 帧、partial-frame 丢弃、滚动窗口、
主动取消、超时、断连、序号溢出、stop 重试和 close 失败后的故障回滚。`capture-test` 只为
取得 aggregate-only 实机 MIC 证据而临时打开本地 sink。真实 PTT、板端网络、MiMo 回复和
扬声器仍须在同一实板完成，因此不能把构建和主机互操作写成实机对话已完成。

## 6. 证据状态与 OpenVela 全树能力矩阵

任何组件必须分别记录四种状态，禁止把“源码树存在”写成“产品已适配”：

1. `AVAILABLE`：OpenVela 树中存在且依赖关系已审计；
2. `CONFIGURED`：AIDK resolved `.config` 已启用；
3. `BUILT`：AIDK AP/CP 直接构建、链接和 manifest 校验通过；
4. `BOARD_VERIFIED`：AIDK 实机功能、资源和长稳证据通过。

以下状态来自源码与目标 defconfig 审计，不代表已有一份同日 resolved `.config` 或实机组合
证据；每次 spike 必须补记录 build identity 后才能从 `AVAILABLE` 升级状态。

| 能力 | OpenVela 现状 | AIDK 当前状态 | 需要的适配与归属 | 首个通过门槛 |
|---|---|---|---|---|
| TFLite Micro | runtime/tool/benchmark 接入存在；嵌套 upstream 源含 16 kHz/mono/S16 的 `micro_speech` yes/no 参考，但未接成产品 App/模型 | 未配置 | Framework 不改；App 按模型实测选择 caller-owned tensor arena、模型和阈值；Chip 只提供通用 allocator 属性、对齐、cycle counter 和 PM 原语 | 逐项启用后可链接；10 分钟实时流无 arena 越界，实时率和峰值堆有记录 |
| CMSIS-NN | `apps/mlearning/cmsis-nn` 存在，TFLM 可切换 CMSIS-NN kernels | 未配置 | Framework 保持原位；Chip 验证 BK7258 指令集、编译选项和 cycle counter；App 不直接调用私有 kernel | 与 reference kernel 输出误差在模型量化容差内，且实测加速不回退 |
| Media Framework | Player/Recorder/Server/Graph/Policy/Focus 均存在 | `CONFIG_MEDIA` 未启用；当前 standalone BKVoice 与它硬互斥，使用同名公共 Media API 的 raw bridge | **不新增 raw backend，也不只改 Kconfig 假装迁移完成**。`media-standard` 是独立替换 profile；先定义 buffer PCM、prepare/start/write/stop/drain/close、错误与单实例契约，再验证 FFmpeg NuttX `indev/outdev` 和 BK7258 lower-half | 独立 profile 完成同一 WAV 的录放、自然 drain、反复切换和资源回收；通过前不得与 standalone BKVoice 同启 |
| Media Trigger | load/start/stop/event 和抽象 model API 已存在；未发现可直接用于 BK7258 的具体 KWS runner | 未配置，无 KWS 插件 | 产品先实现调用经验证 TFLM 模型的 App-owned runner；只有选择独立 `media-standard` profile 时才增加 Media Trigger adapter；Chip 只提供连续 PCM、时间戳和低功耗 vote | App runner 独立 profile 先通过 build/resolved config；可选 Media Trigger adapter 单独验证；连续 1 小时 FAR/FRR 可复现，触发后带 1 秒 pre-roll |
| FFmpeg NuttX audio | 已实现 NuttX AUDIOIOC、mqueue、PCM input/output | 未随 Media 启用 | 优先复用；只有发现 BK7258 lower-half 违反公共 ABI 才修 Chip | 对 `pcm0c/pcm0p` 的 capability、reserve、queue、stop、release 全闭环 |
| uORB / topics | `apps/system/uorb` 与 battery/connectivity/miai/media topics 存在 | `SENSORS=y`，但 `USENSOR/uORB` 未启用 | Framework 保持原位；补齐配置依赖；Chip 发布 SoC 状态，Board 传感器走标准驱动，App 订阅低频状态 | 电池、网络、会话和故障事件有单一 schema；不通过 uORB 搬 PCM/JPEG |
| healthd | 标准电池状态采集并发布 `battery_state` | 未启用；默认扫描 `/dev/charge/`，AIDK 当前为 `/dev/bat0` | 优先让 Board 注册兼容的标准 charge 节点或给 healthd 增加通用可配置路径；不要在 App 轮询私有 ioctl | 拔插电源、低电和充电状态均正确发布，空设备不阻塞启动 |
| MQTT-C | MQTT-C 与 mbedTLS 支持存在 | 未启用 | MQTT 库保留 Framework；Chip 只负责网络 lower-half、TRNG 和 PM；App 定义 topic、QoS、重连和凭据引用。MVP 若一个 TLS/WSS 会话已承载控制和数据，则不强制 MQTT | 强制证书校验和 hostname；断网重连有上限；不通过 MQTT 发送连续 PCM/JPEG |
| Android Keystore/Keymaster | AOSP Keystore 存在，但依赖 Binder、HIDL、Keymaster | 未启用，BK7258 无已验证 TEE/HUK backend | 只做独立安全架构与尺寸 spike；先明确 software-backed 边界和可用硬件原语，不在 Chip 复制一套未兼容的“轻量 Keystore” | 选型后再定义验收；在此之前不得宣称 hardware-backed 或把它列为 MVP blocker |
| KVDB / Settings | KVDB 支持 direct/server 及 UnQLite/NVS/file；Settings 支持小型持久配置 | `BK7258_PREFERENCES` 适配及 CP 命令已有源码，默认关闭 | 复用 KVDB 保存非秘密音量/persona；AP 串行处理配置，持久分区和服务位置由 Board 配置。当前 file 短写处理和 UnQLite 提交/缺失语义需先验证，不能直接启用 | 1000 次更新和随机断电后仍能读取最后一个已提交版本；密钥不进 KVDB |
| Permission Manager | 基于 UnQLite，已有录音、网络、蓝牙等权限名与审计记录 | 未启用 | 单一可信内置 App 的 MVP 不强制启用；多 App/第三方扩展时再由 Framework 管权限、App 映射产品动作，Board 不作策略判断 | 独立 profile 中禁止态不能打开设备；授权/撤销和审计记录可追踪 |
| LVGL / UIKit | LVGL 与 UIKit 均存在；BK7258 的 DMA2D adapter 面向 RGB framebuffer 条件 | 未启用产品 UI；AIDK 是双 GC9D01 SPI framebuffer，不是 RGB scanout | AIDK 首轮只做低刷新、固定上限的 SPI UI；Board 声明两屏几何/背光/复位，App 定义页面。DMA2D/RGB 是另一显示 profile，不作为 AIDK 前置 | 两屏 30 分钟低刷新无越界或堆泄漏，语音实时任务无 deadline miss |
| Bluetooth Framework | GATT、scan、advertising、service 均存在，但完整 Framework 依赖 libuv | 当前只有 BK7258 HCI/NuttX host 基础已验证，完整服务未启用 | Chip 完成 controller/HCI/PM/GATT 能力；Board 不写协议；配网先走最小 GATT，完整 Bluetooth Framework 单独做依赖和尺寸验证 | 手机完成一次性配网、凭据加密落盘、撤销后不能重连 |
| AI Agent | `packages/ai_agent` 有 message bus、voice、LLM/tool loop、camera、MQTT、LVGL；公共采播和回复链路可复用，流式 ASR/TTS 仍绑定火山 | AIDK 当前产品 profile 未启用，App 已有 Agent 生命周期绑定 | 优先验证 standalone BKVoice 的替代 profile，补 MiMo/provider 接口；不直接复制到 Python Gateway，不可两边各建对话/音频 owner；范围见主计划第 17.5 节 | 二选一 owner 的最小 channel 组合可构建并通过清理和资源实测后才切换，远程命令全部 allowlist |
| Paired OTA | BK7258 CP/AP 配对更新与确认机制已存在 | 已有基础 | 保持 Chip/平台所有权；App 只发起更新和显示状态；模型/音色资产采用独立签名清单 | 固件、模型、音色三类版本可独立回滚，任何失败不破坏 `sys_rf` |

矩阵是实施清单，不要求一次把所有大型 Framework 同时常驻。每项先做独立 profile，记录
增量成本，再进入组合 profile；最终产品关闭未使用模块。

## 7. 16 MiB PSRAM 的正确结论

AIDK 实机启动日志已经识别 `capacity=16777216`，所以不能把 BK7258 简单称为低内存 MCU。
但当前 allocator ABI 仍按官方 v3.1.1.9 的低 8 MiB 布局运行：

| 地址 | 当前用途 | 是否普通 `malloc` 可用 |
|---|---|---|
| `0x60000000..0x606fffff` | 7 MiB SDK USER/AUDIO/ENCODE/YUV media slabs | 否，只能走分类 media allocator |
| `0x60700000..0x6071ffff` | CP private PSRAM heap，128 KiB | 仅 CP 私有 allocator |
| `0x60720000..0x607bffff` | AP private PSRAM heap，640 KiB | 部分可用；当前 512 KiB 已捐给 NuttX system heap |
| `0x607c0000..0x607fffff` | AP PSRAM section，256 KiB | 否，链接/运行时保留 |
| `0x60800000..0x60ffffff` | 已检测 16 MiB 器件的高 8 MiB，当前布局未分配 | 尚不可用，必须先完成 chip 级扩展与验证 |

因此本项目采用以下决策：

1. 低 8 MiB 的官方 SDK media slab 和 role-local ABI 保持不动，避免破坏闭源库假设；
2. 高 8 MiB 只是默认关闭的独立 Chip feature 候选，不进入首条产品纵切。只有现有布局的
   实测峰值证明需要它后才启动设计；禁止 Board 或 App 写死地址；
3. 标准 Media 先在现有布局上做 profile 构建和实机量测，再决定普通 heap、media slab 和
   高 8 MiB各承担什么；不能只看物理容量，也不能先验判定一定放不下；
4. 当前 raw recorder/player 保留为可比较基线和故障降级路径，不再安排“新写一个
   BK7258 low-memory raw backend”的重复工作；
5. Media player/recorder 数量设为 1，理由是当前 MIC 与 DAC 共用 AUD clock root 且 session
   硬互斥，也是产品半双工状态机的真实约束，不以“内存不足”作为理由。

若进入高 8 MiB feature，验收还必须覆盖 CP 向 AP 传递实测 capacity 的版本化 ABI、SDK 对
高半区不访问且不 alias、DMA/cache/SMP、低压休眠恢复和运行时 map telemetry。

标准 Media 独立 profile 首轮至少启用 `EVENT_FD + LIB_FFMPEG + MEDIA + MEDIA_SERVER +
MEDIA_GRAPH`，不同时启用 `BK7258_VOICE_SERVICE`；Policy/PFW、Focus、UIKit video adapter
分开测量，不默认一并打开。每次记录：

- ELF text/data/bss 和发布包增量；
- internal SRAM、NuttX PSRAM system heap、各 media slab 的 boot/free/min-free；
- 每个线程实测 stack high-water；
- 20 ms 音频 deadline、录放切换耗时、camera 并发时的峰值；
- 100 次 start/stop/release 后的 heap 差、fd/mqueue 泄漏和 AP supervisor 状态。

## 8. 最终系统拓扑

```text
                           用户自有 Gateway / GPU 主机
                  ASR | VLM | LLM | TTS | Singing | Memory
                                  ^
                    TLS 控制面 + 有背压的二进制数据面
                                  v
+------------------------------------------------------------------------+
| AIDK AI Toy / BK7258                                                   |
|                                                                        |
|  MIC -> AEC/NS/AGC/VAD -> TFLM+CMSIS-NN KWS -> turn state machine      |
|                                |                                       |
|  camera -> V4L2/DVP -> JPEG ---+----> session transport                |
|                                |                                       |
|  PCM downlink -> Media/NuttX audio -> DAC -> PA -> speaker             |
|                                                                        |
|  LVGL dual-screen | uORB/healthd | MQTT control | Keystore | OTA       |
+------------------------------------------------------------------------+
              ^
              | BLE GATT（可选，仅配网/授权）
              v
       原生 Android companion（可选，不是运行必需）
```

板端无网络时仍可唤醒、显示状态、播报固定授权语音包、记录待同步事件；动态对话、视觉理解、
新句 TTS 和唱歌在 Gateway 不可达时明确降级，不伪装为在线成功。

## 9. 产品状态机与资源所有权

主状态机只允许以下转换：

```text
BOOT -> PROVISIONING -> IDLE_LISTEN
IDLE_LISTEN -> CAPTURE -> UPLINK -> THINKING -> DOWNLINK -> PLAYBACK
IDLE_LISTEN -> CAMERA_CAPTURE -> UPLINK -> THINKING -> DOWNLINK -> PLAYBACK
任意在线态 -> OFFLINE -> IDLE_LISTEN
任意安全点 -> UPDATE
任意失败 -> ERROR_RECOVERY -> IDLE_LISTEN/OFFLINE
```

硬约束：

- `IDLE_LISTEN/CAPTURE` 独占 MIC；进入 `PLAYBACK` 前必须 stop、drain、release MIC；
- DAC release 后才能重新进入监听，首版不承诺全双工或边播边唤醒；
- camera 只按用户动作或明确授权抓取单帧，编码/发送完成立即释放；
- UI、MQTT、healthd 和日志不得直接 reserve MIC/DAC/camera，只向 App turn-state arbiter
  发产品请求；Chip 继续独立执行外设 lower-half 的硬互斥与安全释放；
- OTA、USB MSC、文件系统和 voice pack 继续遵守 `/dev/mmcsd0` 独占关系；
- 每个异步结果携带 `boot_generation/session_id/turn_id/sequence`，重启或取消后的旧包必须丢弃。

首版唤醒采用 AP 常醒模式：16 kHz、mono、S16、20 ms frame，维护 1 秒（32 KiB）pre-roll。
低功耗唤醒必须等 KWS 正确率、PM 恢复和首包不丢失分别通过后再加入，不能把 VAD 当唤醒词。

## 10. 传输与协议

MVP 优先只维持一个有长度、序号和窗口控制的 TLS/WSS binary session，同时承载小型控制
消息与 PCM/JPEG 数据，减少重复连接、重连状态和内存。若后续 profile 引入 MQTT-C，它只
传 presence、状态、命令、资产通知和小型 telemetry，不承载持续 PCM 或 JPEG；若复用 AI
Agent WebSocket，也必须发送 binary frame 并执行相同背压。

`companion-v1` 帧头固定网络字节序，至少包含：

```text
magic | version | type | flags | header_len | payload_len
boot_generation | session_id | turn_id | sequence | timestamp_ms
```

消息类型至少包括：

- `HELLO/WELCOME`：能力、固件、模型、音频 tuple 和最大窗口协商；
- `TURN_START/AUDIO_UP/TURN_END`：上行语音；
- `TTS_START/AUDIO_DOWN/TTS_END`：下行音频；
- `VISION_START/VISION_CHUNK/VISION_END`：显式单帧图片；
- `CANCEL/ACK/WINDOW_UPDATE`：取消、确认和背压；
- `HEARTBEAT/ERROR`：连接存活与机器可读故障。

任何长度越界、sequence 回退、未知 session、超时或证书失败都 fail closed。首版固定
`16 kHz / mono / PCM S16LE / 20 ms / 640 bytes`，压缩编码只有在 Gateway 与板端共同声明
能力后启用。

## 11. 端侧唤醒、视觉、对话和授权音色

### 11.1 本地唤醒

用户提供的赛事规则第 4 条限定唤醒词为“你好，openvela”或“Hello，openvela”。首版选择
中文官方词，面向多人通用唤醒。“小冰”保留为后续非比赛版本扩展，单独训练并验收，
不进入首期适配；比赛 profile 在模型安装和 App 选择时均只允许官方词。
当前进度以[主计划](shaniu-master-plan.md#m7-kws本地唤醒)
为准。本地树里最接近 BK7258 实板的参考是 Beken
`asr_service_example`：它支持 AP/CP、板载/UAC MIC 与 8/16 kHz 输入，Wanson 实际要求
16 kHz，8 kHz 需重采样；AEC 只属于 `startnomic ... aec` 的 voice-service 路径，不能外推为
所有 MIC 模式自动具备。该示例选中预编译 Wanson group V1 图和 `libasrfst.a`，静态词中只有
“嗨阿米诺/拜拜阿米诺”等预置命令，没有“傻妞”。它只用于取得 MIC→识别的供应商性能基线，
不能直接并入产品，也不能通过修改回调字符串生成新唤醒词。

OpenVela 当前 checkout 的嵌套 upstream TFLM 源含 `micro_speech`：输入是 16 kHz/mono/S16，
现成模型只识别英文 yes/no；常规 OpenVela 入口只接入 runtime/tool/benchmark，没有把该示例
接成产品 App 或“傻妞”模型。Media Trigger 提供 load/start/stop/event 与
`media_trigger_model_*` 接口，没有已接入 BK7258 的具体模型 runner。当前 AIDK resolved config
同时关闭 Media、Media Trigger 和 CMSIS-NN，因此“树里有源码/接口”不等于已经配置或可部署。

产品采用 App-owned INT8 DS-CNN + TFLM runner；前端、触发门限和模型契约均在
`app/bk7258/bk7258_voice_kws*`，不调用 SDK ASR 私有入口。`BK7258_VOICE_KWS` 默认关闭，
依赖 TFLM/C++，目前构建推理库和一个纯 App wake-window core，不打开 MIC、不注册后台
监听任务。wake window 已用 caller-owned 32 KiB 环实现 1 秒 pre-roll，并对连续语音、静音、
空唤醒、最大时长和时间戳断层做宿主状态机验证；能量/噪声门限仍无实板标定。AP 的监听
录音、停止/join 后切入同一会话、pre-roll 上行和播放后恢复仍需接入与验收。
TFLM 是推理 runtime，Media Trigger 是生命周期/事件框架，两者不是
二选一；只有产品另行选择 `media-standard` profile 时，才给同一 runner 增加 Media Trigger
adapter：

```text
AP owner 的 PCM -> 上游 microfrontend -> TFLM INT8 DS-CNN -> 连续命中/冷却 -> AP owner
```

模型、特征参数、阈值、词表和 tensor arena 选址属于 App workload policy；Chip 提供可查询
的 SRAM/PSRAM 属性、通用 allocator、cycle counter 和 PM vote。可选 Media Trigger adapter
只负责生命周期和事件，不包含具体人物、产品词或第二套资源 owner。

验收不能只报 accuracy，至少覆盖：安静、音乐、车内、户外风噪、远场、扬声器回放、不同人
声；记录 FAR/FRR、P50/P95 延迟、CPU 占用、arena/heap 峰值、1 小时连续运行、PM 恢复和
触发后 1 秒 pre-roll。在目标模型、许可、resolved config 和实板门全部通过前保留 PTT。

#### 训练与推理契约

当前 `bkvoice-microfrontend-v1` 直接复用仓库的 TFLM microfrontend 和固定点 KissFFT，
替换此前自写的 `bkvoice-logmel-v1`。旧前端模型不能直接复用，须重新提取特征并训练。
输入为 16 kHz 单声道 PCM16、2 秒窗口；30 ms/480 点帧长、20 ms/320 点步长，
40 个滤波通道覆盖 125–7500 Hz。开启上游 log scale、`scale_shift=6`，关闭 PCAN；
降噪 even/odd smoothing 为 0、min_signal_remaining 为 1。每帧重置状态，
原始 `uint16` 特征转为 float，得到 `99×40`。窗口和 FFT 缩放由上游实现定义，
不再套用旧版自然 `log1p` 公式。训练与设备编译相同源；metadata 记录源及依赖头哈希。
前端初始化分配一次内存，释放时调用 uninitialize，逐帧不分配。流式端每 100 ms 推理，
暂停清空历史，时间戳回退拒绝；
门限、连续次数与冷却由调用者显式传入，尚无实测产品阈值。

类别顺序固定为 `silence / unknown / nihao_openvela`。模型输入 `[1,99,40,1]`、输出 `[1,3]`，
均为 INT8；只注册 Conv2D、DepthwiseConv2D、AveragePool2D、Reshape、FullyConnected 和
Softmax。模型字节和 arena 由调用者管理；runner 做边界、FlatBuffer、shape 和量化校验，
下载资产的签名/撤销校验由资产加载层承担，不能把 shape 相同当成可信模型。

唯一维护入口：

```sh
python3 tools/bk7258/bk7258.py voice kws audit --manifest /private/kws/dataset.json
python3 tools/bk7258/bk7258.py voice kws train --manifest /private/kws/dataset.json \
  --output /private/kws/candidate --epochs 12 --batch-size 16 --seed 1337
```

训练依赖独立 Python 环境中的 TensorFlow CPU；已用 `tensorflow-cpu==2.15.1` 验证转换链路。
`audit` 不需要 TensorFlow。manifest 使用 `schema: bkvoice-kws-dataset-v1`、上述 `frontend`、
`labels`，以及 `entries` 列表；每项有相对 WAV `path`、匿名 `speaker`、
`split`（train/validation/test）、`label`、文件 `sha256` 和 `consent: true`。
工具拒绝路径逃逸、软链接、错误音频格式、跨集合说话人/PCM 重复、静音正样本及缺失类别；
每段 WAV 必须含完整短语和前后留白，长度不足或超出 2 秒应在语料准备阶段人工复核，不能
直接裁掉尾音。普通语音、噪声、近音短语属于反例；持续负例录音另用于 FAR/小时测量。

校准只用 train 集；输出候选 `.tflite` 和模型/数据集/前端/训练源码哈希、版本、量化参数及
独立 validation/test 的分类误拒率、unknown 误识别率。它们是单段 argmax 指标，不等同于
加入阈值和连续命中后的流式 FAR/FRR。工具不会搜索、上传或复制私人语音。
生成的非语音信号仅用于流水线验证，不能作为官方词有效模型或比赛验收依据。

### 11.2 动态对话与视觉

板端发送 turn audio 和显式抓取的 JPEG；Gateway 完成 ASR/VLM/LLM，并以流式文本/PCM
返回。长期记忆默认仅在用户主机保存，板端只保留短期 session 摘要和可撤销的匿名引用。
位置来自手机时必须单独授权，不能从照片或网络信息静默推断并持久化。

以上为现有 BKVoice 路线。官方 Agent 替代 profile 将对话编排交给 AP 的 Agent，Gateway
作为模型代理与长期记忆服务；不能让两端同时编排同一个 turn。MiMo 对话/ASR/TTS 的适配、
代理路由、音频格式转换和 owner 切换门统一见
[主计划第 17.5 节](shaniu-master-plan.md#openvela-mimo-execution)。
通用对话入口已有源码；火山语音的二进制 WebSocket 与 MiMo HTTP/SSE 契约不同，
不能只换 URL。IndexTTS 的授权音色评测继续独立进行，不阻塞首条 MiMo 内置音色问答。

### 11.3 授权音色训练与播放

聊天导出的 send/receive 元数据是区分双方素材的主依据；声学性别、speaker embedding、
聚类和人工听感只用于异常检测与隔离，不能替代身份授权。训练流水线必须留下：

- consent 版本、speaker 匿名 ID、源文件 hash 和筛选理由；
- 自动切分、去重、静音/爆音/SNR、说话人异常和人工抽检结果；
- train/validation/test 划分、模型/代码/环境版本、指标和失败样本；
- 导出资产 hash、签名、有效期、撤销状态和合成声音披露。

训练与推理分层评测，不能因为完成训练就替换效果更好的零样本基线：

1. 所有候选使用同一固定句集、独立内容/CER 与 speaker-consistency hard gates；机器指标只
   淘汰，不决定冠军；
2. 至少两个候选通过客观门后，使用匿名标签完成全排序盲听；label mapping、reference、文本、
   embedding、checkpoint 和生成音频只留在私有 worklog/run；
3. 当前 IndexTTS-2.5 zero-shot BASE 与三个实验 LoRA checkpoint 均通过内容和一致性门，
   人工盲听由 BASE 胜出；因此 LoRA 不作为 Gateway 默认，不能为了“已训练”牺牲年龄感、
   语气、活泼度或自然度；
4. 当前零样本 prompt profile 固定 `emotion_alpha=0.25`、`duration_factor=0.94`。改变 reference、
   参数、模型文件或 runtime 必须建立新 profile 并重走客观门和盲听。

RTX 5060 8 GiB 的首轮 GPT-SoVITS 配置固定为 V2Pro、单卡、FP16、`batch_size=1`。V2ProPlus
通道更大，只能在 V2Pro 实测有显存余量后作为对照；V3 全量训练不进入 8 GiB 首轮。当前
系统 CUDA 13/PyTorch 2.12 只作为隔离环境的兼容性 smoke，若准备脚本或首个 batch 不通过，
回退到官方记录的 Python 3.11/PyTorch 2.7/CUDA 12.8 组合，不能把“CUDA 可见”当训练通过。

2026-09-01 的 GPT-SoVITS 历史 smoke 如下，精确文本、音频、checkpoint 名称和 hash 只保存在被
Git 忽略的私有 audit/worklog 中：

- 248 条训练片段的 text、HuBERT、32 kHz WAV、speaker embedding 和 semantic 特征全部
  生成并通过数量/hash 门；
- V2Pro S2 单轮产生 1 个约 128.7 MiB 的 checkpoint，S1 单轮产生 1 个约 148.1 MiB 的
  checkpoint；首次 S2 在 248/248 batch 后因官方原生 checkpoint 目录未预建而保存失败，
  流水线已补目录并保留失败 attempt，第二次通过；
- 私有本地 API 用 5.92 秒 reference 连续合成两次 4.46 秒音频：模型加载 8.56 秒，首请求
  6.48 秒（RTF 1.453），热请求 0.806 秒（RTF 0.181）；原生输出为 32 kHz/mono/S16，随后
  机械转换并验证为 AIDK 所需 16 kHz/mono/S16；
- 这些数字只证明 isolated smoke 与格式/速度，不证明音色相似度、文本正确率、长句稳定性、
  8 GiB 峰值余量或真人盲听效果，不能覆盖后续 IndexTTS-2.5 盲听结论。

当前模型结论是 `EVALUATED`，仍不是 `SELECTED`。进入 Gateway 产品服务前必须生成并签发
`gateway-model-profile-v1`，至少绑定 backend/source revision、模型文件 lock、prompt profile
hash、Python/Torch/Transformers runtime、16 kHz/mono/S16 delivery tuple、评测决定、授权/
披露复核、有效期和撤销状态；不得把 reference hash、音频、文本或 label mapping 放入公开
manifest。AIDK 只接收流式 PCM 和签名离线 voice pack，不加载 IndexTTS/GPT-SoVITS 权重。

上述候选来自各自官方实现，但公开 benchmark 不能外推到本机。AIDK 只接收合成 PCM；离线
时回退到本文件定义的签名 WAV voice pack。任何界面和播放入口都持续标明这是合成声音。

主机侧流程现已抽取为通用开源项目 **ConsentVox**（仓库名 `ConsentVox`、Python 包与命令
名 `consentvox`）。微信只是 `wechat-html` source adapter；GPT-SoVITS V2Pro 与 IndexTTS-2.5
zero-shot 是 backend，core 不含 BK7258/AIDK 分支，AIDK 所需的 16 kHz/mono/S16 是参数化
delivery profile。独立仓的 `docs/SOP.zh-CN.md` 是“来源 -> 授权 worklog -> 质量门 -> ASR
草稿 -> 显式转写接受 -> speaker consistency -> 候选生成 -> 客观门 -> 匿名盲听 -> 产品选择/
签发”的唯一主机 SOP。

历史外部仓记录为 `https://github.com/Embracecactus/ConsentVox.git`、`v0.2.0` /
`ced2bb0daa7dafe5fb6b7b0d92be317a0a9d8c42`，不代表当前参赛清单已经接入。
2026-09-18 核对本仓 `contest2026_135_yongwangzhiqian.xml`，没有对应 project 或
linkfile；因此干净同步不能据本文推断存在 `third_party/consent-vox` 或
`vendor/openvela/tools/consent-vox`。历史独立 checkout、import 和 22 项测试记录
也不继承为当前 manifest 验证。

若后续确需迁移，由实际消费者决定固定依赖及调用方式，使用现有工具验证后再
退役被替代入口；本轮不为语音音色扩展新项目或自动同步该依赖。该主机工具不进入
产品 CMake/Make、固件或 OTA；现有 `tools/bkvoice` 源码和历史复现入口保留。

当前 `tools/bkvoice` 只保留为本次可复现证据入口，不再新增通用功能。ConsentVox
公开 source manifest 只有 path hash，私有 manifest 才含相对路径；`operator-attested`
明确表示只有操作者声明，`recorded` 只记录授权证据 hash。ASR 草稿必须通过显式接受门，
speaker embedding 只作一致性异常筛选。`SOURCE_AUDITED`、`CORPUS_PREPARED`、
`FINETUNE_SMOKE_COMPLETE` 和 `VOICE_MODEL_SMOKE_COMPLETE` 均不得写成模型已经
`EVALUATED/SELECTED`。

### 11.4 唱歌

唱歌与对话 TTS 是两条独立 pipeline。Gateway 侧生成旋律/歌词/人声或做授权 voice
conversion，板端只流式播放和显示歌词/状态。首版不在 BK7258 上运行扩散模型、声码器或
实时变声，也不把“能播放生成歌曲”表述为“MCU 完成唱歌生成”。

## 12. 配置、密钥、权限与隐私

数据分四类处理：

| 数据 | 存储 | 保护 |
|---|---|---|
| Wi-Fi、Gateway URL、音量、UI 偏好 | KVDB/Settings | 原子提交、版本和掉电恢复；敏感字段只存 Keystore handle |
| 设备私钥、refresh token、资产验签密钥 | Keystore-compatible service | TRNG、AEAD sealed blob、设备绑定、回滚/删除保护 |
| voice pack、KWS 模型、UI 资产 | SD NAND/受控文件系统 | 签名 manifest、hash、版本、撤销列表 |
| 原始聊天、原始语音、训练 checkpoint、长期记忆 | 用户主机 | 不进入固件、发布包、Git 或默认 telemetry |

UID 只能用于设备标识或 KDF 上下文，不能当秘密。若 BK7258 无可验证硬件根密钥，文档和 UI
必须标记为 software-backed，不得宣称 TEE/hardware-backed Keystore。

MIC、camera、location、network 和 authorized-voice 分别授权；物理/屏幕指示必须覆盖
设备实际占用期。撤销后应终止活跃 session、删除 credential/asset key、拒绝旧 token，并
保留不含隐私正文的审计事件。

## 13. UI 与可用性

两块实装 GC9D01 都是 160×160/RGB565，分别注册为 `/dev/fb0`、`/dev/fb1`。每块完整帧为
51,200 bytes，双屏完整帧为 102,400 bytes；两块屏拥有独立 SPI transport 和 framebuffer，
但共用同一路背光，不能分别调亮灭。物理左右与 fb 编号必须先用测试图在实板确认，再由
Board 记录稳定映射，App 不根据编号猜左右。

首版把双圆屏优先当成一对协同的“眼睛”，而不是永久把一块屏浪费成仪表盘：

| 产品状态 | 双屏主画面 | 必须保留的提示 |
|---|---|---|
| `IDLE_LISTEN` | 对称眼睛、低频眨眼和轻微视线移动 | 网络/电量只作小角标 |
| `CAPTURE/UPLINK` | 瞳孔聚焦、蓝色收音环 | MIC 占用指示与真实 reserve 同步 |
| `THINKING` | 两眼协同扫视或三点节奏 | 弱网/离线状态不可隐藏 |
| `DOWNLINK/PLAYBACK` | 由 Gateway 情绪标签选择表情并叠加说话节奏 | 合成声音提示可随时查看 |
| `CAMERA_CAPTURE` | 双屏先显示明确确认态，再允许单帧抓取 | camera 指示覆盖整个占用期 |
| `OFFLINE/ERROR/UPDATE` | 降级表情；需要时一屏临时显示短错误码 | 不显示长字幕，不阻塞恢复/OTA |

板端不训练或运行生图模型。表情由 App 把有限标签（如 `neutral`、`happy`、`shy`、`sad`、
`angry`、`surprised`、`thinking`、`listening`、`speaking`、`offline`、`error`）映射到
参数化眼睛和少量关键帧；未知标签回退 `neutral`。刷新采用脏矩形、局部 RGB565 buffer 和
事件驱动的低帧率动画，避免持续搬运双屏全帧。完整字幕不适合 160×160；只显示短词、确认
和错误码，正文留给语音或手机。

如需统一美术风格，先在主机侧用现成生图模型制作资产，不做专门训练。资产 brief 固定为
160×160、圆形安全区、深色背景、无文字、左右视线一致、每个状态少量关键帧；生成的 PNG
经人工挑选后离线转换成 RGB565 的调色板/RLE 或小 sprite。先用程序化占位眼睛通过双屏方向、
颜色、局部刷新和 30 分钟稳定性，再开独立生图会话批量生产最终资产，避免在硬件门未过前
反复重做图片。

后续使用适合 SPI LCD 的标准 LVGL/NuttX framebuffer 路径驱动双屏，限制刷新区域、帧率和缓冲上限；
不把 BK7258 RGB framebuffer 的 DMA2D/PSRAM scanout adapter 当作这块板已经具备的能力。
UIKit 的完整 video/media 组合在独立 Media profile 通过后再启用。页面和动画属于 App，
双屏器件、几何、背光和 reset 属于 Board；只有实际使用的 DMA/cache 加速归 Chip。

## 14. 分阶段实施计划

本节保留原始 P0–P3 的功能拆分和适配记录；其中“下一步”和未接线描述属于当时阶段。
当前优先级、模型候选和剩余实板工作统一由主计划第 17 节维护。

### P0：当前 standalone BKVoice 闭环

1. 修通并 host-test `bkvoice-pack-v1` parser、WAV gate、RPMsg wire ABI 和微信方向清单；
2. 完成 AIDK CP `bkvoice`、AP voice service、2 KiB streaming raw player 的直接构建；
3. 实机先验 `status/tone-test/verify/play/stress`：先用不依赖存储的短 tone 验证 DAC/PA/喇叭，
   再验证手工挂载、USB MSC 互斥、断 RPMsg、重复 request
   replay、50/100 次播放后的 heap/fd/mqueue/audio reserve 回收和 AP supervisor；
4. 生成私有 `received` source manifest、source audit 和可复现质量门；Gateway worklog
   当前为 `VOICE_MODEL_SMOKE_COMPLETE`，但不冒充 `EVALUATED/SELECTED`；
5. 本阶段零 Chip/Board/大型 Framework 重构，不能把 build PASS 写成 audio/RPMsg 板证据。

### P1：Gateway 音色基线与训练

1. 已完成质量门、ASR 草稿、主说话人 embedding 过滤，并冻结 248/34 train/eval；`sent`、
   6 个方向冲突、16 个质量拒绝、2 个空转写和 23 个说话人异常均隔离；
2. 已记录每个私有 manifest、特征和 checkpoint 的 SHA-256；下一步按会话/时间复核
   train/validation/holdout 边界，并以自动 ASR 回读加少量盲听校验文本，不把草稿当真值；
3. 已在 RTX 5060 8 GiB 上以 V2Pro/FP16/batch=1 完成 GPT-SoVITS S2/S1 单轮微调和两次
   本地合成；下一步补 Fun-CosyVoice 3 零样本同集基线，F5-TTS 只在资源允许时加入对照；
4. 已记录模型加载、首/热请求 RTF、格式和 checkpoint；下一步补峰值显存、清晰度、
   speaker similarity、长句稳定性和真人盲听，达到门槛后才能把状态升为 `EVALUATED`；
5. 建立本机私有 worklog、撤销清单和资产签发，不向 Git、固件或默认 telemetry 写入路径、
   文本、原始音频、embedding 或权重。

### P2：按键说话端到端纵切

1. 已加入 transport-neutral `companion-v1` network-byte-order codec 和单会话状态机，并用
   deterministic fake server host-test request/cancel/window/reconnect、sequence replay、旧
   session/turn 与 synthetic TTS 标识；真实 socket/TLS/WSS transport 尚未安装；
2. 已加入硬件无关的单实例 turn arbiter，并以 fake audio ops 逐阶段验证 MIC
   `acquire/prepare/start -> stop/drain/release` 和 DAC
   `acquire/prepare/start -> drain/stop/release`，包括超时、取消、乱序、旧 token、partial
   write 与 release 失败后的 fail-closed recovery；session 必须显式打开且 ID 单调递增，
   control/downlink 两个 arbiter-local sequence 分别从 1 严格连续增长，overflow 必须销毁
   session；
3. 已加入 App 私有 `bk7258_voice_turn_audio`，只通过公共 media ABI 将上述 ops 映射到现有
   standalone recorder/player。voice-service 启动仅初始化空 backend，不提前打开设备；
   `media_player_stop()` 的 drain/stop/release 折叠语义由 adapter 显式幂等化，两个 raw
   bridge 的 `close()` 保证“负值即句柄仍可重试”，避免 fail-closed recovery 重试悬空指针。
   host fake-media 已验证正常录放、MIC close recovery、DAC stop retry 和 DAC close recovery；
   recorder reader 还必须显式 attach/detach，reader 未退出时 drain/release 一律 `-EBUSY`，
   防止 close 销毁仍被阻塞 read 使用的句柄；AIDK 构建不等于实机资源闭环；
4. 已加入 task-neutral `bk7258_voice_capture` 和 joinable `bk7258_voice_ptt` worker owner：从 source
   拼接固定 640-byte 帧，对 partial final frame 不补零，sink/start/audio/end/cancel 全部 fail closed；
   PTT release 的强制顺序是 `interrupt recorder -> bounded join -> detach -> arbiter drain/release ->
   sink TURN_END`，join 超时保留 worker/MIC 供安全重试。host fake source/sink 已覆盖正常、partial、
   零长度、上行失败、start/cancel 清理和 join-timeout retry；owner 级主动 cancel、deadline
   timeout、录音中 session close 和 sequence overflow 已统一按 stop/join/capture/turn 顺序清理。
   voice-service 已用公共 recorder adapter 初始化空 owner；只有操作者显式执行
   `bkvoice capture-test` 时才用 aggregate-only 本地 sink 开短会话，用于验证完整帧和非零 MIC
   数据，不保存原始 PCM。产品 PTT 事件、真实 companion sink 与 Gateway transport 尚未接线；
5. 下一刀将 CP 公共按键事件转换成有界 PTT control，AP worker 上传到用户 Gateway，由 ASR/LLM
   和 P1 胜出的 TTS 热模型处理；不能让 RPMsg/GPIO callback 直接阻塞读取或操作音频句柄；
6. Gateway 首个生成 chunk 立即重采样为 16 kHz/mono/S16/20 ms，禁止等整句 WAV；
7. AIDK 运行时接线必须继续满足 MIC 完全 release 后才允许 DAC reserve；
8. 当前基线只有 standalone BKVoice 会话/audio owner，断网回退固定 voice pack。优先验证
   AI Agent 的替代 profile，通过清理、资源和实板门后切换，不能并存第二套状态机。

BKVoice 的协议和清理门继续复用 [P2-A 单轮接话契约](shaniu-next-stage-single-turn-plan.md)，
执行顺序以 [主计划第 17.5 节](shaniu-master-plan.md#openvela-mimo-execution) 为准，
不重复已经完成的 host 纵切。产品 UART/串口功能明确不实现；只保留通用 provider seam，
串口不进入运行路径、回退路径或验收门。

本次 adapter 的 ownership 记录：

| 问题 | 决策 |
|---|---|
| SoC/Board 物理差异 | adapter 不感知；MIC/DAC 节点、通道数、PA GPIO 和极性继续由 Chip lower-half 与 Board 配置负责 |
| 产品差异 | 16 kHz/mono/S16、半双工顺序、超时与恢复归 App |
| 公共 ABI | 只调用 `media_recorder_*` / `media_player_*`，不 include Board 头、不访问寄存器或 SDK 私有 API |
| 生命周期 | voice-service 启动只初始化空 context；会话 owner 才 acquire，release 成功才允许换向 |
| 当前证据 | host fake-media、capture fake-source/sink、固定 tone PCM/partial-write/cancel 和 joinable PTT worker cancel/timeout/overflow 故障测试；AIDK CP/AP direct build、layer gate、ELF 存活检查和 build-manifest 生成通过；`capture-test` 与 `tone-test` 均仍缺实板 MIC、可听音/PA、物理按键、Gateway 或完整 MIC/DAC turn 证据 |

### P3：OpenVela 独立组件 profile

分别构建并量测 `media-standard`、TFLM+CMSIS-NN、uORB+healthd、LVGL SPI UI、
KVDB/Settings、Bluetooth GATT、MQTT/TLS 和 Keystore security spike。每个 profile 记录
resolved `.config`、build identity、ELF、heap/stack 和启动时延；只有产生组合需求时才改
Chip 公共 API。`media-standard` 不与 P0 voice service 同启。

### P4：本地唤醒

接入 App-owned TFLM+CMSIS-NN KWS runner、1 秒 pre-roll、FAR/FRR 数据集和连续运行验收。
Media Trigger adapter 只在独立 `media-standard` profile 中验证，不与当前 standalone BKVoice
形成第二 owner。首版保持 AP 常醒；低功耗唤醒另立 gate。

### P5：相机、双屏与旅行场景

实现显式单帧 JPEG、VLM 询问、GC9D01 低刷新 UI、字幕/语音回答、位置可选授权和离线待
同步；不做持续视频上传，不为 AIDK 假设 RGB/DMA2D scanout。

### P6：安全、升级与产品化

完成设置/凭据选型、TLS 证书与撤销、固件/模型/音色资产独立签名和回滚、低电、弱网、
断电、长稳、温升、隐私和故障注入；任何升级路径继续保护 `sys_rf`。

### P7：唱歌、长期记忆与可选优化

加入独立歌曲生成/转换 pipeline、分段下载/播放、歌词 UI、主机侧可查看/导出/删除的记忆；
只有硬件证据允许时才探索高 8 MiB PSRAM、低功耗 KWS、Opus、全双工或复杂 UIKit，不把
它们设为首版阻塞项。

## 15. 防止后续重新混层的机制

每个新外设或 Framework 适配必须提交一张 ownership 表：

| 问题 | 必须回答 |
|---|---|
| SoC 通用吗 | 是则实现于 Chip；否才进入 Board |
| 换成同 BK7258 的另一块板是否仍成立 | 成立的代码不得引用 `BOARD_AIDK_AI_TOY` |
| 物理差异是什么 | 只允许 pin、active level、bus、address、器件数量、设备路径和容量策略进入 Board |
| 产品差异是什么 | 模型、阈值、UI、服务器、人物和授权只进入 App |
| 使用哪个公共 ABI | 优先 NuttX/OpenVela API；私有 SDK 只能被 Chip wrapper 封装 |
| 如何证明 | resolved config + host/build test + 实机机器可读证据，三者不能互相替代 |

CI/交付门槛：

- layer lint 禁止 App include Board 私有头、Board 调用模型/网络协议、Framework 引用 BK7258；
- Classic Make 与 CMake source list 同步；
- 每个公共能力有 disabled-build、AIDK-build 和至少一个 BK7258 alternate-board compile case；
- capability 缺失必须返回 `-ENODEV/-ENOTSUP`，不能静默套用 AIDK 默认值；
- 资源 acquire/release、超时、取消和异常回滚都有 host test；
- `sys_rf` 永远不用于设置、密钥、模型或资产，完整升级默认不触碰其工厂数据；
- 文档明确区分 `AVAILABLE/CONFIGURED/BUILT/BOARD_VERIFIED`，避免以“树里有”冒充适配完成。

## 16. 验收清单

无硬件阶段：

- manifest/WAV/parser、RPMsg wire ABI、`companion-v1` codec/session、纯 turn-arbiter、
  public-media adapter、capture frame-pump 和 PTT worker owner host tests 通过；这些只证明
  ABI 映射、完整帧与故障回滚，尚未实现的产品 PTT event、真实 companion sink、KWS、
  Gateway transport 和实机 audio turn 不得列为已通过；
- source audit 明确列出 `received/sent/collision` 数量与总时长，训练 worklog 明确区分
  `NOT_STARTED/PREPARED/TRAINING/EVALUATED/SELECTED`，双方音频绝不混合；
- layer gate、Classic Make/CMake、AIDK AP/CP 及每个独立 component profile 构建通过；
- 输出组件增量报告，固件和发布包不含私人数据、voice pack、credential 或训练产物。

实机阶段：

1. 合法 voice pack `VERIFY PASS`，缺授权、路径穿越、错误采样率稳定失败；
2. `bkvoice tone-test` 连续 10 次均可听到约 500 ms 的纯音，无明显爆音/削波，日志固定返回
   25 帧、16000 字节和峰值 4096；示波或逻辑分析核对 PA P50 只覆盖播放生命周期，执行前后
   无 reserve、fd、mqueue 或 heap 泄漏；
3. `bkvoice capture-test 1000` 连续 10 次均返回完整帧、非零样本和非零峰值；静音/说话两组
   记录用于检查峰值响应，前后 `bkvoice status` 与 `apctl health` 正常，且没有原始 PCM 文件；
4. standalone raw profile 连续播放 100 次无 reserve、fd、mqueue 或 heap 泄漏；标准 Media
   是后续独立替换 profile，通过同等测试前不与之组合；
5. `apctl status` 全程 HEALTHY，音频 P95 frame deadline、stack high-water 和 min-free 有记录；
6. 高 8 MiB 若启用，跨核、DMA、camera、双屏、休眠恢复和 24 小时压力全部通过；
7. KWS 在目标噪声集达到约定 FAR/FRR，触发时 pre-roll 连续且旧 generation 数据被拒绝；
8. TLS 强校验，凭据撤销、错误证书、断网、Gateway 重启和重放攻击均 fail closed；
9. camera 只有显式授权才工作，MIC/camera 指示与真实资源占用同步；
10. 固件、模型、音色资产可独立升级/回滚，任一失败不改写 `sys_rf`；
11. Gateway 用同一 holdout 对零样本与微调模型记录冷/热首包、RTF、显存和盲测；只有
   `SELECTED` 资产才能服务 AIDK，撤销后 reference/cache/checkpoint/签发资产均不可再用；
12. 最终只把通过 `BOARD_VERIFIED` 的板端能力和通过 `EVALUATED/SELECTED` 的模型能力写进
    产品说明。
