# 650 后续诊断：Q01 入口与问题矩阵

本记录基于 `2460d51de17a72669e6497e5e8ff3af7b9f8c139` 加本次工作树修改，
Agent 仍为 `280cbbd592e60783e6ea205aafa3799a44296d9b`。这是诊断增量的
构建证据，不是新的 K2 修复验收，不替代 650 人工验证记录。

## Q01：已取得与尚缺的证据

原始 CP ELF 已按 650 清单核验并固定到本地
`out/shaniu-repair-20260923/q01-650-frozen/cp.elf`；SHA256：
`cdc6b7c99696f313252f33cc3f156fb5deb3ad5452ad4f3d395d8695f78fdedd`。
map 和原内存报告一并保留，后续地址不能反用于解释旧固件。

两次历史故障 `EXC_RETURN=ffffffed`、`CFSR=00020000`、
`HFSR=40000000`，原始 SP 为 `28044398`，输出的八个核心栈帧字全零。
原来的 `V=1` 仅检查地址范围、对齐和 stacking fault，不能证明这些字
就是有效现场。不能据此断言实际执行了 PC=0，或已经定位某条 SDK 指令。

对应 ELF 反汇编确认 SDK 的 super-deep 路径通过
`sys_drv_enter_deep_sleep` 调用 SRAM HAL，最后跳转 SRAM WFI 包装。
旧诊断入口本身位于 Flash。仅验证两个睡眠符号在 RAM 不能解释上述故障。

本次改变：

- CP 自有 `0x80` 字节故障槽中的记录从 80 扩为 116 字节，版本为 2；
  保留邻接 CPU2 记录边界断言，不改变分区、AP RAM 或堆边界。
- SRAM 裸入口无压栈、无动态分配、无锁，先记录原始 MSP/PSP、CONTROL、
  PRIMASK、BASEPRI、FAULTMASK、VTOR、FPCCR、EXC_RETURN、HFSR/CFSR。
  入口记录 `reserved=1`，C 路径读取基本帧后才记为 2；后者仍不等于帧语义有效。
- 初始 Flash 向量保留早期故障入口，只有 SRAM 复制及 RAM 向量初始化完成后
  才安装 SRAM 入口，避免在复制前跳入未初始化 RAM。
- 睡眠阶段 0/1/2/3/4/5/6 分别表示未进入、硬件准备、唤醒配置、SDK 入口、
  WFI 边界、WFI 返回、SDK 返回。实时阶段与故障快照分离，普通启动不擦掉
  上次异常保存的阶段。阶段只用于诊断，不授权或改变电源转换。
- 新增 `HX` 固定字段输出。后续 C 日志和原有复位路径仍依赖 Flash；
  **没有声称整个异常处理器已经 XIP-independent**。若 Flash 不可访问，
  SRAM 入口保存的记录与 UART 输出成功是两个不同证据层级。

本次最终 AIDK CP ELF SHA256：
`27dcad858838e0b693cc151445f6ddc964a78365120ea628ae9ab588fe50f705`。
Flash 启动入口 `0201148e`，SRAM 入口 `28010940`，复制范围
`28010940..28016d7c`。锁定 GCC 10.3-2021.10 反汇编核对入口及 literal pool。

| 字节 | 原 650 | 本次诊断增量 |
| --- | ---: | ---: |
| copied data（含选定可执行段） | 25404 | 25660 |
| BSS | 160544 | 160544 |
| 中断栈 / 启动栈 | 2048 / 2048 | 2048 / 2048 |
| 初始堆，未扣分配器开销 | 52380 | 52124 |

以上不是运行时最大连续块或高水位测量。没有恢复 649 的整 PM 对象搬移，
保留 PSRAM/BT 初始化顺序、链接依赖跟踪和内存报告。新增符号位置门槛及
原有测试内的两个负例，分别拒绝缺失 WFI reset 和缺失 SRAM fault entry。

## 执行记录

公共 CLI 实际增量构建，CP/AP 目标链接和相关启动组件均退出 0：

```sh
python3 tools/bk7258/bk7258.py build --board aidk_ai_toy --boot mcuboot --development-identity --rollback-floor 650 --jobs 4
python3 tools/bk7258/bk7258.py build --board t5_board --boot mcuboot --development-identity --rollback-floor 649 --jobs 4
python3 -m unittest discover -s tests/host/bk7258 -p test_bk7258_build_workspace.py
```

两个 floor 沿用各自现存构建输入，不是新的发行计数，不授权安装或降级。
日志在本地 `out/shaniu-repair-20260923/q01-fault-stage-{aidk,t5}-build.log`。
单元测试 9 项通过，`git diff --check` 通过。未新增测试文件。
`nxstyle` 全文件检查仍报告既有路径头、混合命名及历史格式问题，不能报告全绿。
本阶段未签名打包、未运行最终提交 Actions，也未操作串口、刷写、关机或恢复出厂。

## 不可遗漏的问题矩阵（650 后诊断阶段历史快照）

| 编号 | 当前状态 / 已有证据 | 剩余实现或确认 | 验收方法与通过条件 |
| --- | --- | --- | --- |
| Q01 | K2 实板失败；本次诊断构建通过 | 取得有效故障现场并修复根因；启动预算仍需运行证据 | 受控复测新增 HX/阶段；最终 10 轮关开机无 fault/立即重启，配置保持，USB/电池分开 |
| Q02 | 普通无工具首 Media 21.630750 秒；仍全正文后 TTS | 真实有效正文增量主链、请求时间轴、ASR 等待分析 | 保留工具能力与历史，验证首段在正文完成前播放；同条件延迟/失败率，声学与代理指标分开 |
| Q03 | 650 一轮视觉最终回答成功，648 最终请求超时未销项 | 最终回答 deadline、取消、资源释放审查 | 新图到最终 TTS/完成/再唤醒全链，失败后不忙锁、不重拍循环 |
| Q04 | 三轮 rebuffer=0；EOF 重复错误仍在 | EOF、录音尾边界和取消生命周期；不另建播放器 | 完整、欠载、取消、网络错误分别回归；自然语速，不以静音/整段下载伪装连续 |
| Q05 | 24 epoch 候选未优于默认，未替换 | 独立来源泛化与量化后的板端预算；更新/默认恢复回归 | 分组隔离评价召回/误唤醒/语速/噪声；达到门槛才发布合格候选 |
| Q06 | 公开“我在”资源已入构建，真人未单独确认 | 核对实际选择及失败兜底 | 实听人声与提示音分别留证，不计入答案首句提速 |
| Q07 | 认证撤销及持久回执代码、主机/模拟验证已有 | 多副本与中断边界审查；独立物理恢复缺口 | 模拟 ACK 丢失/重启/旧 worker；现场旧凭据失效、新 QR、TLS 保留，不用 K2 五秒替代 |
| Q08 | 配网/云配置人工成功，历史断网和灰显不能一并销项 | 重连、草稿/事务所有权、离线配置完整回归 | 热点失效恢复、重启回读、认证 BLE 离线管理；无并行重连和永久 busy |
| Q09 | v35 模拟器截图/导航已有 | 本轮实际复验并收敛发现、权限和错误状态 | 360/412dp、深浅色/大字体、键盘/重建、20 轮导航，实际截图/录屏；模拟明确标识 |
| Q10 | b5c831c9 冷构建与旧产物存在 | 本轮最终源码对应的构建/签名/包、APK、资源及准确 Actions | 哈希/协议/布局/信任一致；独立下载复验，OTA 传输与安装确认分层，不能用旧绿灯 |

Q01 根因缺现场仅限制该项的下一次硬件观测；不阻断其他软件工作。
本记录不将任何尚未实现的代码写成“只差现场”。

## Q08/Q09：Mi 10 保留数据升级与音量回读（本轮新增）

源码基于 `5e27a0c060836880be77f86ab35f56f5bd508ca8`，加本节对应
Android 文案、既有测试断言及 versionCode 36 修改。先构建并安装当前
提交的 v35，再安装修复后的 v36；两次均为 `install -r`，返回 Success。
没有卸载、清数据、重新扫码或生成 owner。两版启动均保留认领资料，
选择原设备后完成认证。手机系统 Android 13，实际设备为 Mi 10。

当前已安装 `com.shaniu.companion`，`0.7.6-shaniu-companion` / 36；
APK 18008732 字节，SHA256
`5593aec1bf9f8eb4637104956ab788542c83a6c68808217fd0082a8a78acb3e4`。
手机实际安装包重新提取后的 SHA256 与构建产物相同。安装证书 SHA256
`b2ea591116f967af230d0f06f678cc5fdd99d5884cc429b8d22c6489ba96050b`，
与手机原 v34 相同，属于已有本地签名，不是 CI 临时签名。

- v34 待机音量入口本轮实际可用；原可用态文案仍显示“收音或播报时暂不可调整”，
  容易被理解为当前不可操作。v36 改为“可调节”，不改变认证、新鲜度、busy、
  写入待确认等安全门槛。既有测试保留忙碌禁用断言，新增可用态文案断言。
- 音量原值100%。仅做一次降低：App 提交95，设备档位回读93%，关闭重开仍93%；
  随后恢复原100%，再次设备回读确认。原值本来就是100%，没有超出原值增大音量。
  COM13在02:46:39记录MusicVolumeDomain index-14，02:47:17恢复full，与手机结果一致。
  未重启验证持久化，未验证忙碌过程及声学响度。
- 20轮设备→定制→更新→设置导航完成，进程保持，最终首页认证与状态刷新正常。
  对应App进程日志仅见起始一次GATT连接，无重复连接或FATAL异常。
  未直接观测订阅内部计数，不将此窗口扩展为所有生命周期通过。
- Wi-Fi与云配置页完成设备只读回读，读完后输入框及固定保存按钮解锁；
  设备报告Wi-Fi就绪。未提交网络、模型或密钥，没有默认值覆盖操作。
  扫描启动已操作，首次观察窗口仍为扫描中，之后取消；不能标扫描结果通过。
- 前后台返回使用同一App进程；完整Activity重建、草稿/键盘等回归仍需继续。
- 更新页实际回读板端0.7.5、build650、security counter650。
  手机升级不等于固件更新；布局、信任指纹、恢复条件仍未充分核实，未执行OTA。
- 电脑默认路由走Realtek有线网卡；尚未核实全部热点恢复参数，未切换热点。

构建命令为既有 `./gradlew :app:assembleDebug :app:testDebugUnitTest --console=plain`，
v36 构建及测试退出0。目标包位于本地
`out/shaniu-repair-20260923/mi10-current/shaniu-v36.apk`（工作区根相对路径）。
证据同目录含 v36-build.log、v36-connected.png、v36-update.png、
v36-logcat-navigation.txt、v36-logcat-final.txt、v36-control-serial.raw/json。
300秒被动串口采集7833字节并正常释放；没有串口命令、复位、关机或刷写。
真机导航录屏 `C:\Users\lijian\shaniu-ui-evidence\shaniu-v36-mi10-navigation.mp4`，
89.7925秒，8158513字节；已查看实际截图及录屏抽帧，不是静态原型。
原始日志/画面仅本地保存，不随源码公开。

## 本轮语音专项：研究基线与冻结目标（2026-09-23）

本节是后650诊断的语音专项增量，不改写上方 Q01–Q10 的历史证据或状态，
不表示已有固件、模型或实板通过。专项基线为 root `87254121`、Agent
`280cbbd592e60783e6ea205aafa3799a44296d9b`。Q01 的 651 复现 stage3 HF；
872 中断保护构建通过，仍待实测。Q02–Q10 继续按上方矩阵跟踪，不销项。

### 选型判断

| 路线 | 研究定位 | 当前决策 |
| --- | --- | --- |
| A：v1/default | 现有默认候选与冻结基准 | 保留为对照，不先替换 |
| B：stateful PCAN + 原轻量结构 | 持久化前端状态，沿用分类器结构但重新训练权重 | 主候选；与 v1 的同预算重训对照，禁止旧权重配新前端 |
| C：增量流式 | 每个音频小块更新特征/模型状态并尽早给出结果 | 并行方法候选；需同时核算状态内存、延迟和误触发 |

研究参考显示，micro-wake-word 的流式 MixConv 只消费最新特征切片，TFLite
Micro 前端使用 PCAN；PCEN 则用逐频带 EMA 状态
`M_t=(1-s)M_{t-1}+sE_t` 做动态归一化与压缩，和 PCAN 不是同一前端，不能
直接互换。micro-wake-word 当前核对固定提交
`4665173cd35f1cff9a61e06fc427f124766c488e`，仓库 LICENSE 为 Apache-2.0；其
16 kHz/40 维特征与量化模型可作实现参考，但没有 BK7258 直接兼容或实板验证
证据。TFLM PCAN 算子也不等同于板端已移植。78/xiaozhi-esp32 的单
AudioService、采集/编码/解码/播放队列和 JSON+Opus WebSocket 可作所有权与
协议参考，不视作 BK7258 代码移植结论。

服务器路径方面，FunASR 在线 WebSocket 是先发 JSON 配置，再发送裸 PCM 二进制
帧；文档列 8/16 kHz，服务端按单声道 16-bit 样本计时，句尾以
`is_speaking=false` 收尾，2-pass 提供在线和离线修正结果。未见应用层取消
消息；断开 WebSocket 不保证已执行推理被取消。MiMo-V2.5-ASR 接收一段完整
Base64 WAV/MP3，`stream=true` 只流式返回识别响应；MiMo TTS 的流式输出是
PCM16 音频，仍由完整文本请求触发。这些接口可作云端能力参考，不等于板端
流式音频上传或本地 ASR/TTS。

主要一手资料与许可证：

- [78/xiaozhi-esp32 AudioService](https://github.com/78/xiaozhi-esp32/blob/main/main/audio/audio_service.cc)、[WebSocket 协议](https://github.com/78/xiaozhi-esp32/blob/main/docs/websocket.md)、[MIT LICENSE](https://github.com/78/xiaozhi-esp32/blob/main/LICENSE)。
- [Espressif esp-sr AFE 指南](https://github.com/espressif/esp-sr/blob/master/docs/en/audio_front_end/README.rst)、[AFE 配置接口](https://github.com/espressif/esp-sr/blob/master/include/esp32/esp_afe_config.h)、[MIT LICENSE](https://github.com/espressif/esp-sr/blob/master/LICENSE)；上游定位 ESP SoC/ESP-IDF，未核实 BK7258 兼容。
- [micro-wake-word 固定提交 README](https://github.com/OHF-Voice/micro-wake-word/blob/4665173cd35f1cff9a61e06fc427f124766c488e/README.md)、[Apache-2.0 LICENSE](https://github.com/OHF-Voice/micro-wake-word/blob/4665173cd35f1cff9a61e06fc427f124766c488e/LICENSE)、[TFLM PCAN kernel](https://github.com/tensorflow/tflite-micro/blob/main/signal/micro/kernels/pcan.cc)。
- Wang et al., [Trainable Frontend for Robust and Far-Field Keyword Spotting](https://doi.org/10.1109/ICASSP.2017.7953242)；[Google kws_streaming README](https://github.com/google-research/google-research/blob/master/kws_streaming/README.md)。
- [FunASR WebSocket 协议](https://github.com/modelscope/FunASR/blob/main/runtime/docs/websocket_protocol_zh.md)、[服务端源码](https://github.com/modelscope/FunASR/blob/main/runtime/python/websocket/funasr_wss_server.py)。
- [MiMo-V2.5-ASR API](https://mimo.mi.com/docs/en-US/api/audio/Speech-Recognition)、[MiMo-V2.5-TTS 文档](https://mimo.mi.com/docs/en-US/quick-start/usage-guide/audio/speech-synthesis-v2.5)。

### 冻结评价目标（目标，非成绩）

- A/B/C 使用同一数据与评价协议；A→B 控制前端变量，B→C 控制模型结构变量。
  按来源分组后隔离 train/validation/test，
  同一录音或合成来源的切片、增强和派生版本必须留在同一 split。
- 唤醒召回目标：总体至少 95%，关键分组各至少 90%；词尾到唤醒决策延迟
  p95 不超过 500 ms。误唤醒目标不高于 0.5 次/小时，并争取至少 10 小时连续背景评估。
- 普通问答做 30 轮同条件基线与候选对照；目标是 p50 降低 30%，达到 3 秒，
  p95 达到 5 秒。记录首个有效正文、首个 PCM/Media 与最终完成时间；不得把
  流式中间推测文本计作有效答案。以上均待实施与测量，不是本轮成绩。

### 前端对照预注册（运行前）

- 原默认 SHA `922eba9175fcda60f7c8a4505ca4eb5a97c86ceb30fbe48c685fd612098ac910`
  不替换；A-r 为同结构重训控制，B 为 v2 重训，不将 A-r 冒充旧默认。
- A-r/B 同用已审计 1302 项语料，逻辑清单哈希
  `4a580d0e97babb91f24b495dd87c45a0f23734c1ec2a52df13eeb837ded79a77`；
  DS-CNN 32 通道、首层频率步幅 4、seed 20260923、batch 32、最多 24 epoch，
  每轮最佳 val_loss 权重保留 checkpoint，既有 patience 20，单运行上限 2 小时。
  CPU TensorFlow 2.15.1，最多 4 个 intra-op 线程、1 个 inter-op 线程，不占 GPU。
- 二者同用 400 ms unknown shifts、1 秒训练负例合成预热且交替冷启动；本轮
  不叠加 tempo/room/gain，避免把多个增强收益混入前端差异。验证/测试独立短片
  仍冷启动，连续会话按实际前置音频推进状态。合成预热不声称真实录音历史。
- 唯一 A-r/B 变量为 v1 与有状态 v2（PCAN + 温和频带噪声抑制）的前端契约。
  保留全部失败、浮点/INT8混淆矩阵和流式命中；已有验证/测试曾用于调参，
  仅称回归集合，不称新独立真人测试。离线门槛未达不得替换默认或发布可安装包。
- C 的模型结构与端侧状态预算尚待实现、冻结；不能将 B 写成增量模型已完成。

### 模型路由记录

| task_id | model_requested | model_verified | reasoning | usage | result | evidence |
| --- | --- | --- | --- | --- | --- | --- |
| `voice_source_research` | `gpt-6-luna` | `GPT-6`；具体 Luna 变体未核实 | medium（请求值；实际档位未核实） | 未知 | 已完成只读上游来源核验；未改固件、模型或硬件 | 本文上方列出一手资料 URL 与 micro-wake-word 固定 SHA |
| `pipeline_audit` | `gpt-6-sol` | 待核实 | high（请求值；实际档位未核实） | 未知 | 审计结果沿用其独立记录；本表不将其写作已通过 | task 记录待核实；root 当前模型不可切换，本地后续默认 Sol 配置已变化 |

### 本轮新增证据（主机，尚未部署）

- A-r/B 均完成 24 epoch 并保留最佳权重，INT8 SHA 分别为
  `1bd45137a3dbe8eac7a990d7f0c60a15c7dfbdde1410412efbc1e6c6d20f334a`、
  `f28f352f91c4de14788bc025976f85b98b6319a31c13967341a824ace87047f1`。
  固定 0.60/连续 2 次策略；连续验证会话 A-r 18/18、B 17/18；测试会话
  两者 20/20，但 B 额外误触发 1 次，A-r 为 0，均无重复触发。
  验证/测试分别仅 243/369 秒且是历史回归集，不作长期 FAR 或真人泛化承诺。
  **B 尚不优于控制，保留研究身份，不替换默认。**
- Q02/Q03：同一 Media recorder 的 KWS→会话 handoff、400 ms 前滚、2 秒
  有界队列及溢出/取消已通过现有 host peer；一次交接只 open/start 一次。
  单样本时间轴代表 Media 实际交付样本，底层硬件丢样仍不可观测。
- Q04/Q05：正文流式新增明确 `agent_finalize` 规划→终答边界，终答 SSE
  才进入有界文本队列及逐句 TTS；保留正式工具、安全与历史路径。会增加
  规划请求，必须计入延迟/费用，尚无云端加速成绩。UTF-8 分片、思考/工具
  内容拒绝、取消、尾句及首 PCM 早于 END 的既有 peer 检查通过。
- 实时 ASR 实现 FunASR PCM WebSocket 后端、固定长度发送队列及 finalize，
  产品通过显式 `asr_backend=funasr` 和 `asr_stream_host/port/path` 选择。
  复用已安装 CA 与可信时间，独立 TLS 且不发送 LLM Key，不默认切换服务。
  当前仅协议 peer 通过；官方服务源两次 TLS 下载失败，未得到真实识别结果。
- 端点候选为按样本推进的 20 ms 能量/噪声底判据，默认关闭实验开关；保留
  900 ms 尾静音。低能量开头不再直接丢弃，语音确认后一次性提交最多 400 ms
  前滚。合成信号检查覆盖分块不变性、轻声、句中 600 ms 停顿和 DC；不是 CER。
- 实际官方 Media EOF/resume 问题仍在其服务端，尚未修复；不能以本轮
  单播放器/一次 drain 的 peer 结果代替真实 Media 修复证据。
- 本轮 Mi 10 必要复核时 adb 列表为空；未反复扫描、未操作 COM13、未刷写、
  未触发 K2、未清 owner，尚无新板端/声学验收证据。

本地证据根：工作区 `out/shaniu-voice-20260923/`，含 `frontend-*-{validation,test}.json`、
训练日志/最佳权重/量化元数据、`final-stream-peer` 和 `endpoint-peer`；原始材料不公开。
额外参考 [sherpa-onnx KWS](https://k2-fsa.github.io/sherpa/onnx/kws/index.html)
仅作为电脑侧关键词/流式方法对照，未将 ONNX Runtime 或其模型当作 BK7258
可部署 TFLM 权重。新增背景候选来源为 [MUSAN / CC BY 4.0](https://www.openslr.org/17/)，
下载及来源去重完成前不计入评价时长。

### 增量结构及协议审查追加证据

- C 为 32 通道因果 TCN、249 帧上下文（4.98 秒特征步长，4.99 秒 PCM 覆盖），
  6 输入/6 输出全 INT8，状态 7936 字节。原生 TFLM 结构探针实际运行 333 帧，
  arena 21824 字节，桌面/原生最大误差 1 LSB，reset 逐值相同；不是板端耗时。
- C 正式训练固定 24 epoch、seed 20260923、批量 32、v2 前端、2 秒训练负例
  合成前史，模型 SHA256 `b6b13dfef42530834a9568c4d41716331e7f6b15024f8a508f2f6f3d658c77d1`。
  验证 18/18、0 误触发；测试 20/20、1 误触发，均无重复。
  C 同时改变结构、上下文和前史，不作纯结构单变量归因；原始正例仍只有
  3 秒，不能证明超长慢速目标完整覆盖，更不能称独立真人泛化达标。
- 真正默认 A（`922eba9175fcda60f7c8a4505ca4eb5a97c86ceb30fbe48c685fd612098ac910`）
  用隔离只读 `87254121` 工作树及原始前端哈希重新评价：验证 18/18、3 次误触发；
  测试 20/20、3 次误触发。不是前文 A-r 重训控制。
  输入音频、分组、会话完全相同，仅为默认元数据补齐显式显示短语“你好，openvela”
  （历史 manifest 缺字段时默认显示“你好，open-vela”），未改录音或标签。
- FunASR 审查已修正标准 `101 Switching Protocols` 握手、分段 `is_final` 与
  整轮 `is_end` 的区别、错误回执优先级以及连接入口前取消丢失。
  原有 peer 扩展覆盖多段终答累计、空终答、错误+结束、入口前取消及下一轮恢复；
  `make run-agent-funasr` 通过。只声明该 `is_end` 协议变体，不猜旧服务结束语义。
- 默认唤醒应答已改为显式半双工采集屏障：唯一 producer 保持打开，提示音期间
  清零丢弃输入，恢复时丢弃跨代在途块和 PCM16 半样本；300 ms 安静判据与
  250 ms 尾音隔离不是 AEC。连续语音跳过提示并保留有界前滚。现有 capture、
  TTS queue、FunASR、final-stream、endpoint 主机检查全部通过
  （`host-final-integration.log`）；短促连续命令及 Media 同时录放仍待板验。
- WKM2/WKA2/WKS2 携带显式前端版本，TFLite 内部 `bkvoice.frontend` 同时绑定；
  无内部字段只允许旧 v1，错配/未知/重复字段拒绝。C 迁移后 SHA256 为
  `94639a263093895365f57fa2e288016bf3bc473f861ab8e7c67269304b6b6277`，
  canonical 计算图/权重/量化未变。研究包 26820 字节，SHA256
  `69d616f20e65ff68a4e59f10d8848e9d47bd3f41c343af364cbea2b89cec3cab`，
  位于 `frontend-c-bound/nihao-openvela-streaming-research.wkm`，未替换默认。
- 模拟器 `emulator-5554` 从 v33 保留数据覆盖安装 v37
  `0.7.7-shaniu-companion`，安装前后证书 SHA256 均为
  `b2ea591116f967af230d0f06f678cc5fdd99d5884cc429b8d22c6489ba96050b`。
  APK SHA256 `dbca811d4f6dab86ad5198cf00d30cc9008fa8045d7c46a8ecd4c868481691d7`。
  实际启动、切换定制/更新/设置并查看截图；证据在 `emulator-v37/`，录屏
  `navigation-v37.mp4` 为 18.5512 秒。不宣称实板连接/模型安装成功。
- 用户确认 Windows 输出接耳机，模拟器声音不能到达板端；本轮不再执行该
  物理回放路径、不切换用户音频路由。扬声器到板端与真人声学指标均未测。
- 当前工作树以安全计数 652（已知 651 后的未部署候选）完成 CP/AP 全链接，
  见 `build-full-candidate-652.log`。CP 初始 gross heap 52124 字节，复制 data
  25660 字节，bss 160544 字节，中断/启动栈各 2048 字节；预算报告明确
  `boot_status=not-verified`，不是运行期高水位证明。
  独立审查随后发现规划截断回退及取消/历史提交竞态，已修复并通过
  `host-commit-boundary-final.log` 与 `build-post-review-652.log`。规划必须
  完整工具阶段，正文提交与取消互斥；提交后可停止播放，但不撤销已接受历史。
  本地 Agent 提交 `938b66bd38bcf6111236ae1c3b9db520e1d3c3dc`，两次推送均遇 TLS
  错误，最初未更新主 manifest。后续 Linux/Windows Git 传输仍失败，但
  GitHub API 可用：通过官方 Git objects API 创建树与提交，分别核对
  tree `bc98dc248f15fbb198edf430011c2e4a8f6fc405` 和 commit 与本地完全一致，
  再以 force=false 更新个人 fork 的 `feat/shaniu-streamed-reply`，独立回读
  确认 `938b66bd38bcf6111236ae1c3b9db520e1d3c3dc`。主 manifest 此后才固定该 SHA。
  工厂软件候选 `factory-652-worktree-candidate` 已签名生成（未物化同板 BIN），
  不是最终提交交付。build provenance 已补录实际 Agent checkout：现有
  workspace 检查 9 项通过，`build-agent-provenance-652.log` 构建退出 0；新
  build-manifest 的 `provenance.dependencies.ai_agent.source_commit` 为
  `938b66bd38bcf6111236ae1c3b9db520e1d3c3dc`，dirty=false。主仓仍为工作树
  候选；旧软件包没有自动获得这份新来源清单，须重新打包。
- MUSAN 归档大小 11086114085 字节，SHA256
  `86d1061c7e15b5c9e906777685c519701df51bfde3001e1070dcc9ffac955ee1`，
  完整 gzip/tar 检查通过。按包内逐来源许可排除未明确授权组，取 140 段
  去重音乐共 36003900 ms，仅裁去不足 20 ms 的末帧，不增益、不循环。
  署名与许可记录在 `public-background/selected-background-provenance.json`；
  原始音频不公开。现有 evaluator 重新绑定同一固定策略；C 已完成长背景
  评价，140 段音乐触发 **892 次 / 10.001083 小时，约 89.19 次/小时**，
  明确未达 0.5 次/小时目标，不准提升为默认。原有短测试还有 1 次误触发，
  因此合计报告 `background_false_positives=893`，不能混淆两个分母。
  同材料默认 A 为 24 次（约 2.40 次/小时），B 为 10 次（约 1.00 次/小时）；
  B 相对 A 有改善但仍未达门槛，C 长连续背景明显退化。三者原有 20 个
  短正例均命中。证据 `musan-{default-a,b,c}-test.json`；该音乐集合此后
  属于已观察回归集，不再宣称未见最终测试，也不外推到普通语音或真人泛化。
  后续应从独立来源补足训练背景并核查 C 的长上下文行为，而非降低阈值。
- 流式 ASR 新增真实本机服务证据：现有 `run-agent-funasr` 测试入口增加
  `--live localhost PORT PATH CA PCM`，调用生产 `funasr_asr.c`，校验 TLS
  CA/主机名后按 100 ms 上传 PCM，输出时间与最终文本摘要，不输出正文。
  FunASR 1.4.16 + 固定 Paraformer online/offline/FSMN 模型实际运行三轮
  公开 5.5467 秒样例。前两轮成功但有原始 PCM 被容器探测的警告；本机
  参考服务改为按已协商 PCM16 显式解码后，第三轮无该警告，首在线片段
  2078 ms，最终结果/结束 ACK 5908 ms，4 个非空在线片段，最终 57 字节
  SHA256 `3cb27df9ce1e524a1c9b54ae7d2548386971863601c95ba79b0690cfb71beefb`
  与公开样例参考文本一致。证据 `funasr-reference/c-live-pcm.log`、
  `service-pcm.log`、`STATUS.md`（含模型 SHA/服务适配说明）。这证明音频
  未发完前已有真实识别输出；不是板端、产品云服务、30 轮或声学指标达标。
  未把电脑设为产品永久网关，也未修改设备服务配置。
- C 连续背景退化的定向诊断已完成：固定原模型与策略，对同一 60 秒音乐，
  INT8 重现 8.1/23.1/28.8 秒的 3 次误触发；浮点权重经相同生产 C 判据
  仍触发 5 次。三处对应浮点分数为 0.994/0.996/0.980，完整序列与逐帧
  浮点结果一致。因此该样本不能用“量化或状态传递累积偏移”解释主要失败。
  200 个评分时刻中浮点/INT8 有 4 个阈值分歧，绝对误差中位 0.0024；
  后 30 秒 p95 0.049，小于 5～30 秒的 0.090，未见持续误差扩散。
  证据 `c-longstream-diagnostic/result.json` 及同目录复现脚本。
  首要待验证假设是训练仅监督序列末帧、异源前缀与短冷启动验证未覆盖连续
  背景各决策时刻；不是已证明的唯一根因。下一候选分开验证监督与数据覆盖，
  不通过降低门槛或重划已观察测试集提高成绩。当前训练工具源码哈希与旧
  检查点记录不同，诊断只证明权重可加载、输出/事件可重现，不证明源码逐字一致。

## 当前语音专项矩阵（工作树候选，非整机验收）

历史矩阵保留原始失败与基线；以下覆盖其“当前状态”，不删除未完成项。

| 编号 | 本轮已取得 | 剩余实现 / 验证 |
| --- | --- | --- |
| Q01 | 保留 87254121 的 K2 中断保护候选；652 CP/AP 链接及内存预算通过 | K2 根因闭环、真实启动和关开机仍未通过；本轮不触发 |
| Q02 | 单采集/前滚、样本时间轴、终答 SSE→句队列→TTS 已进生产调用链；FunASR 真实本机 WSS 上行识别通过 | 用户实际后端与板端部署、30 轮冷暖对照、额外规划成本及声学指标未测；App 未提供 FunASR 端点配置 |
| Q03 | 正式 Agent 工具阶段保留，终答边界、失败/取消和历史提交 peer 通过 | 新代码的完整视觉云回合、超时恢复和再唤醒实板回归未测 |
| Q04 | 既有有界 PCM 队列、一次播放器生命周期及取消 peer 通过 | 实际 Media 服务端重复 EOF/resume 尚未修复，不能用 peer 代替 |
| Q05 | A/B/C 长背景对照及同源背景实验完成；B 新候选回归失败。实际 C 权重揭示 XNNPACK 与原生推理差异，已固定参考内核并重新评价 | 历史桌面默认内核指标不得冒充原生/板端结论；候选均未合格，不替换默认，不称真人泛化通过 |
| Q06 | 合法人声资源与提示音兜底保留；半双工采集屏障和连续命令交接已实现 | 实际人声选择、应答尾音与紧接命令不吞字仍需声学验证 |
| Q07 | 既有认证撤销实现保留，本专项未扩展破坏性操作 | 多副本、独立物理恢复及现场重新认领缺口仍保留 |
| Q08 | 历史 Mi 10 凭据保持及音量回读证据保留，未更换设备云配置 | 当前手机不在线；本轮无新的热点、真实 BLE 或配置保持验收 |
| Q09 | v37 模拟器安装、四页操作、截图和录屏完成；模型前端兼容信息更新 | 不能扩大为真实设备连接或全部多尺寸/错误场景已验收 |
| Q10 | 主仓 ae8f36bd / Agent 938b66bd 已发布；匹配 CP/AP 和签名软件包完成；该提交 Actions 与本机独立下载校验通过 | 后续连续训练改动未提交、不在该 CI 范围；最终模型与新源码交付仍未完成；CI 临时身份不供当前板 OTA |

本次继续执行的现有主机入口为 capture、FunASR、final-stream、endpoint、
TTS queue、cloud-http、voice-kws，整体退出 0，日志
`continuation-host-regression.log`。系统 Python 的 KWS 部分为 11 passed、
1 skipped（缺 TensorFlow）；不将该跳过项计为模型数值验证通过。

随后使用已有 TensorFlow 虚拟环境补跑 KWS 全文件检查，11 passed、0 skipped，
包括新增的负例逐时刻监督、前缀遮罩、正例末帧语义、样本权重和检查点互载。
训练入口新增可选 `--streaming-negative-frame-loss-weight`，默认 0 保留旧配方；
启用时仅对已标记负例的源录音最后 149 帧添加非唤醒损失，不为正例猜词尾，
不监督异源前缀。单步导出图及运行判据不变。冻结 weight=1、同数据/seed/
24 epoch 的一次对照，CPU 2 线程、7200 秒截止，证据配置
`c-supervision-experiment.json`。

该对照已完成 24 epoch，模型 SHA256
`19fcbaab1916c995b33544307bb02ab8d3bcec4ac4b29698de2cc3226be538bf`。
短验证集 18/18、短回归目标 20/20，均无额外误触发；但同一已观察的
140 段、10.001083 小时音乐回归仍有 **734 次误触发（约 73.39 次/小时）**。
相较旧 C 的 892 次下降约 17.7%，仍远未达到 0.5 次/小时目标，不能推广为
独立测试或真人泛化改善，也不能替换板端默认。证据为
`train-c-negative-frame-run.log`、`musan-c-negative-frame-test.json`。

下一实验只扩展连续负例覆盖：现有 MUSAN 包内未使用的 Jamendo 录音中，
筛出逐项明确 CC BY/BY-SA 的完整录音，训练 12 首 / 5 个专辑来源组 /
3596060 ms，验证 3 首 / 2 组 / 1008460 ms。与上述 140 段的路径、原文件
哈希、对齐 PCM 哈希及许可来源 URL 无交集；仅证明来源组隔离，不能声称
艺术家身份或跨目录同作品声学等价已经排除。歧义许可及缺逐项映射的噪声
未纳入。证据 `c-background-training-preparation/candidates.json`；数据资格
准备不是已训练结果。长文件窗口必须带同源前序状态，不用异源拼接冒充连续。

当前模型目标仍为 BK7258 本地 TFLM 推理；Android 不执行唤醒模型。
Windows 输出接耳机，模拟器播放不能作为扬声器到板端麦克风验证。
本轮未部署上述候选，未触发 K2、刷写或清理设备身份。

连续负例候选完成 24 轮与 INT8 导出：模型
`1a2b3a66db9c173a2476cc4008a47227605f939511a9ff14158b4a6f001755df`，
26680 字节，状态张量共 7936 字节（不是 TFLM arena 总需求）。新增 1185 个
3 秒训练窗口各保留同源紧邻 2 秒历史；这不是整首前端状态从头滚动训练。
与前一候选在同一新验证集对照：目标均 18/18、无重复；1008460 ms 音乐
误触发从 12 降至 4，但候选约 14.28 次/小时，仍不合格。阈值、两次确认、
结构、seed 和轮数未改变。证据 `c-continuous-{control-,}validation.json`、
`c-continuous-experiment.json`；上述集合现已用于评价，不再称未观察盲测。
冻结训练清单 SHA256 为
`8620f879e510be94b0b3d1fa8a7e62fe859be85cb3e222d5a0d898815fe90de8`。
现有测试文件补验为 12 passed；不是目标板推理时限或真人召回证据。

同策略 10 小时音乐回归随后完成：该候选 220 次误触发，相较前一候选
734 次减少约 70.0%，短目标 20/20、无重复。但约 22 次/小时仍未达到
0.5 次/小时，且远差于原 B 的 10 次/10 小时；不能仅凭相对改善选择 C。
保留 C 为未合格研究候选，下一受控对照将同一连续背景覆盖用于 B，固定
其 DS-CNN32、频率步长 4、1 秒前端预热、seed 与 24 轮，不调判据。
证据 `musan-c-continuous-test.json`、`b-continuous-experiment.json`。

### 实际权重的原生推理核对与评价后端修正

B 同源背景候选（`572c65b5…e55b7`）完成后，短验证与原 B 同为目标
17/18、背景 1008460 ms 零误触发；但已有 36003900 ms 长背景回归从
原 B 的 10 次退化到 46 次，短目标 20/20 退化到 19/20，无重复。
淘汰该候选，不增加轮数或调整阈值补救。这里及前文长背景数字均来自
当时 TensorFlow Lite 自动选择的 XNNPACK；它们保留为历史实验，不能
直接归给固件原生 TFLM。

现有 `test_bk7258_voice_kws_model.cc` 增加实际权重核对入口
`--candidate <frontend> <model> <features.f32> <scores.f32>`，不替换原有
合成结构、包损坏、前端错配、重置断言。原 B 的 69 个验证窗口在原生
TFLM 与桌面分数完全相同，arena 使用 41856 字节。C 实际训练权重
`1a2b3a66…755df` 的 10281 帧则未通过原 2 LSB 容差；同一输入的
桌面 XNNPACK 与 `BUILTIN_REF` 最大分数差达 0.28125，共 520 个类别值
不同。改用参考内核后，原生与桌面逐值一致，两次重置后输出完全一致，
arena 使用 21824 字节，另有 7936 字节状态缓存。未放宽断言。

上述 C 输入是 69 个验证片段的特征顺序拼接，用于模型数值和重置检查，
不是连续声学准确率评价，也不是板端耗时。B 内部前端绑定迁移后哈希为
`1679970f931a6d6f5ecb104528fa9c896a34f6d086dc72b987d359d5614fa368`，
计算图/权重不变。完整输入哈希与结果在
`frontend-b-stateful-native/`、`frontend-c-continuous-native/`。

实际训练后 INT8 检查及 `kws evaluate` 已显式固定 `BUILTIN_REF`、单线程；
冻结策略和结果记录 TensorFlow 版本/内核，不接受缺少该绑定的旧策略
直接执行新 test。模型权重和唤醒阈值未变，C 长背景参考内核复验单独留档，
不能据一次数值一致便声称误唤醒或目标板泛化已达标。

C 参考内核长回归已完成：36003900 ms 背景 222 次误唤醒，目标
20/20、无重复；对照旧 XNNPACK 的 220 次，数值后端问题不是高误唤醒
的主要原因。`musan-c-continuous-reference-test.json` 记录 TensorFlow
2.15.1 / BUILTIN_REF / 单线程。原策略被实际 CLI 以
`frozen_policy_binding_mismatch` 拒绝，未写出新测试结果；证据
`rejected-old-inference-policy.log`。已有 Python 检查 12 passed，原生
合成/包兼容检查仍通过，没有删除或放宽原断言。

B 两组同内核复验也已完成，结果汇总如下；背景均为相同的 36003900 ms，
这些材料已经观察过，只是回归集，不是新的盲测或真人声学验收。

| 权重 / BUILTIN_REF | 背景误唤醒 | 目标命中 | 重复触发 |
| --- | ---: | ---: | ---: |
| 原 B `f28f352f…47f1` | 11 | 20/20 | 0 |
| 同源背景 B `572c65b5…55b7` | 45 | 19/20 | 0 |
| 同源背景 C `1a2b3a66…55df` | 222 | 20/20 | 0 |

同源背景 B 的退化在相同参考内核下仍成立。证据
`musan-b-{control,continuous}-reference-test.json`；三者均未达到背景
0.5 次/小时的目标，且正常短句命中不能替代轻声/快慢语速子组验收。

下一轮假设由训练元数据而非测试样本反向挖掘得出：1185 个连续音乐
窗口受来源归一约束，总权重实际仅 60，正例总权重约 3027。已冻结一次
每连续负例来源总权重 5→50 的 C 对照，总权重 60→600；数据、结构、
前史、seed、24 轮、判据均不改。新参数默认仍为 5，拒绝非有限值、
非正值和超过 1000 的值，并记录实际来源数/总权重。该实验不新增声音
来源，不将窗口数当独立说话人数；预算 CPU 两线程、7200 秒，不自动
加轮或换默认。该次训练随后完成 24 轮，实际每来源总权重 50、12 来源
总权重 600；最佳检查点第 24 轮。检查点仍按原 `val_loss` 选择，包含
终帧分类和负例逐帧辅助项，不是仅分类交叉熵。

同一参考内核验证集保持 18/18，1008460 ms 背景误触发从 4 降至 2；
36003900 ms 长背景回归从 222 降至 75，目标 20/20、无重复。约 66.2%
相对改善不等于合格：约 7.5 次/小时仍高于 0.5 次/小时，也差于原 B。
原生 TFLM 对该新权重再跑 10281 帧，分数误差 0、重置逐值一致，arena
21824 字节、单独状态缓存 7936 字节；不是板端推理 p95。

模型 26680 字节，SHA256
`86a72bc2e3bf5c738200a9fce86580e6e2e387586822bdab521a9b75d7dcb73a`。
仅生成显式未合格研究 WKM2 包
`frontend-c-continuous-weight/nihao-openvela-c-weight50-research-not-qualified.wkm`，
26820 字节，SHA256
`02788c7848f8447658b097608ceace1bd27d4284d54f7098a9c14fb85e1f419b`。
需支持前端 v2 的匹配固件，未向当前板安装，稳定默认未替换。证据
`c-continuous-weight-experiment.json`、`musan-c-continuous-weight-test.json`、
`frontend-c-continuous-weight-native/result.log`。本次有界试验结束，不再
自动加权/加轮；保留失败门槛和已实现的数值一致性修复。

已发布提交 `ae8f36bdf3e5531b7723bce410b68454f1a2b763` 的 Actions
run `35840720143` / attempt 1 成功，本机独立下载后六文件 SHA256 清单、
8 镜像包检查及 BL1/BL2/CP/AP 公钥验签均通过。CI 工厂软件包 SHA256 为
`cfb7f9de8898a6e490b8fa5f6b3cb82ef0bdda7a5aa1d4b0c07332185a801d89`。
证据 `ci-35840720143-verification.json`。它使用 CI 临时开发身份，不能更新
当前板；也不包含此后未提交的连续负例训练修改，不代替最终提交再验证。

### 验证来源的语速/数字增益分组（后续离线诊断）

已有 `kws evaluate` 补齐每会话漏报、重复、窗口外事件及分组统计；
分组参与冻结清单绑定。背景每小时误唤醒仅用无目标会话的时长作分母，
正例会话的窗口外触发仍保留，不挪进成功数。原有检查扩展后 13 项通过。

从相同 27 个验证会话、22 个来源组派生 6 个条件，共 162 个会话、
108 个目标区间、1409400 ms 音频。完整连续音频分别作保音高变速或
数字降幅，不重算轻声 VAD、不裁掉超长片段；仅补齐名义缩放时间轴和
20 ms 输入帧。原标注区间按语速映射，未人为延长。它们不是经过核对的
真实词尾，不能报告词尾 p95；同源变换也不是新说话人/真人泛化证据。
清单 SHA256 `617ae35048f2cba44d1b951762ee1dc9b5bcb87eb615ec0d9ffb1b24eeecdf5f`。

固定 0.60、连续两次、300 ms 判据与 BUILTIN_REF，下表是严格区间命中数，
每组分母均为 18，所有组重复触发为 0：

| 条件 | 稳定默认 A | v1 重训控制 A-r | 有状态 B | 流式 C weight50 |
| --- | ---: | ---: | ---: | ---: |
| 原速/原幅 | 18 | 18 | 17 | 18 |
| 数字增益 0.25 | 18 | 17 | 17 | 18 |
| 数字增益 0.10 | 17 | 6 | 17 | 18 |
| 语速 0.75 | 17 | 12 | 17 | 12 |
| 语速 1.25 | 17 | 16 | 15 | 13 |
| 语速 1.50 | 6 | 2 | 5 | 0 |

A-r 不是已部署默认 A；A-r/B 可用于这组相同训练配置的前端对照，
C 同时改变结构、上下文及背景权重，不能全部归因于流式结构。真实默认 A
在隔离的旧源码/旧前端路径运行，未修改其源码或权重；调用现有评估函数时
显式固定 Interpreter 为 BUILTIN_REF/单线程，之后仅汇总原始事件。
其清单仅显式写入旧默认要求的显示短语，音频、分组、区间完全相同；
旧评估器哈希和调用方式记录在 `a-default-grouped.json`。默认 A 的
1.5 倍速窗口外事件为 8，仍有明显弱项，但当前新候选不能替换它。
1.5 倍速窗口外事件分别为 11、5、12；B 的 13 个漏报中有 5 个仍出现
窗口外触发，其余 8 个无事件，不能只解释成标注问题。数字 gain 不是
声压、距离或真实轻声发音。此组缺口说明“正常短句 20/20”不能替代语速验收。
原始结果、输入变换/补零数量、哈希和汇总在输出根 `robustness-validation/`。

据此预注册一次 C weight50 的保音高 tempo 覆盖对照，而非继续背景权重
扫描。原有训练开关此前为隔离变量未启用；本次仅增加 0.75/1.25/1.5
训练变速，维持来源总损失权重，修正原先变速派生会额外增加正例及不完整
目标负例权重的问题。24 epoch、seed、结构、2 秒前史、阈值不变；
CPU 两线程、7200 秒上限，不自动追加轮数或替换默认。
实验注册 `c-continuous-tempo-experiment.json`。当前 3 秒训练入口仍拒绝
超过上下文的慢速完整目标，绝不裁断后保留正例；超长正例训练支持仍未完成。
最终效果必须在训练结束后按同组及长背景回归判定，不能用训练准确率代替。

该有界训练随后完成 24 轮、退出码 0，INT8 文件 26680 字节，SHA256
`9b583df85f8b1987f086e714bab5717bee802b9b1b5075c62a3e7c22ccc67366`。
训练代码哈希 `c417731f13ac2083d49e01367ca3c49fd4747e4ac3e0cd6d86ea9f386da8c9c8`。
新增变速副本 670 个，仍来自相同 59 个正例来源组；32 个超长慢速副本被拒绝。
实际正例总权重保持 3027、连续背景保持 600。元数据中的历史标量
`non_positive_weight` / `other_derived_unknown_weight` 是分组平衡前基值，
不是变速派生后的逐窗口权重，应结合新增 `tempo_augmentation.weight_policy`。

| 条件 | C weight50 对照 | 本次 tempo 候选 | 本次窗口外事件 |
| --- | ---: | ---: | ---: |
| 原速/原幅 | 18/18 | 17/18 | 0 |
| 数字增益 0.25 | 18/18 | 17/18 | 0 |
| 数字增益 0.10 | 18/18 | 18/18 | 0 |
| 语速 0.75 | 12/18 | 5/18 | 0 |
| 语速 1.25 | 13/18 | 10/18 | 8 |
| 语速 1.50 | 0/18 | 7/18 | 11 |

正常与慢速退化，按预注册首个门槛淘汰；未再消耗长背景回归，也未打包、
部署或替换默认。当前短片分类验证 18/18 与上述连续流表现明显不等价，
需要进一步核查时间监督与实际判据，而非追加变速或 epoch。证据
`c-continuous-tempo-result.json`、`robustness-validation/c-tempo-report.json`、
`robustness-validation/comparison-final.json`；未获得新的板端/真人证据。

### 流式正例时间监督：受控实现与首轮结果

随后固定上述两份 C 权重，记录每 20 ms 实际参考内核输出，再对照生产
300 ms/连续两次判据。`robustness-validation/timing-trace.json` 显示：
weight50 的 18 个 1.5 倍速目标中有 14 个在原目标区间内从未达到阈值，
不能将全部失败归因于评分间隔；tempo 候选的 18 个慢速目标中有 12 个
出现过高分却没有严格命中。源区间仍是原有粗区间，不补造词尾延迟。

核对正式训练入口发现正例仅有最后一帧分类损失。新增可选
`--streaming-positive-frame-loss-weight`，并实际接入全序列 fit：
从原始 PCM 的首/末非零样本取得保守完整波形范围，之后才进行增益等
变换；平移只在完整范围仍被保留时继承时间标签，变速按完整输出长度，
合成房间扰动追加最大反射/FIR 范围。不用 RMS 门限重新裁切轻声，
也不把这个范围称为人审词尾；未知或裁切后无法证明的范围保持末帧损失。
完整波形之后的帧均值与原末帧损失做凸组合，不增加正例来源/类别总权重。
合成前史、未完成的正例前缀与已有末帧不重复加辅助标签。

预注册 `c-temporal-supervision-experiment.json` 的 24 轮训练退出 0。
训练代码 SHA256 `fd09ffec0b83859a805168c4fd0adf7b30df4eff69b65cf90d0d6cf830fa3215`；
新模型 SHA256 `6d4a96ddf3c15a5624a770acf3ef7969660624b492692129da515442c7fe94b7`，
26680 字节。59 个来源组、5848 个训练窗口具有可用的保守时间标签，
不是增加了新说话人。继续使用相同验证派生流、0.60/两次/300 ms 和
BUILTIN_REF，没有更改任何接受区间：

| 条件 | 末帧监督 tempo 对照 | 新时间监督 | 新候选窗口外事件 |
| --- | ---: | ---: | ---: |
| 原速/原幅 | 17/18 | 18/18 | 0 |
| 数字增益 0.25 | 17/18 | 18/18 | 0 |
| 数字增益 0.10 | 18/18 | 18/18 | 0 |
| 语速 0.75 | 5/18 | 10/18 | 0 |
| 语速 1.25 | 10/18 | 18/18 | 0 |
| 语速 1.50 | 7/18 | 15/18 | 3 |

所有组重复触发为 0。慢速低于预注册推进门槛 12/18，且快、慢两组均未达
产品 90% 目标，所以没有继续长背景评估、生成更新包或替换默认。
新增梯度/边界/来源测试复用原文件，13 项 Python 检查通过；生产 C 核心
及前端入口通过，系统 Python 的两个 TensorFlow 跳过项已由独立 TF 环境
执行覆盖。实际模型在生产 TFLM 主机入口通过 10281 行、两次 reset 校验：
参考内核最大分数差 0，arena_used=21824 字节；不代表板上推理耗时或
物理回放通过。证据为 `c-temporal-supervision-result.json`、
`robustness-validation/c-temporal-report.json`、`frontend-c-temporal-native/`。

### 完整慢速源长度支持（实现验证，候选训练中记录）

原训练入口会拒绝超过三秒的完整慢速副本，前轮实际拒绝 32 个。
扩展现有流式入口 `--streaming-positive-max-ms`，上限五秒，默认仍三秒；
长样本必须启用流式结构、时间监督及变速，不能截成三秒后保留完整正例。
既有短源/不完整目标派生不改；长源单独保留完整波形及原 source/speaker。
每条记录显式携带真实源帧数和总有效帧数，末帧分类取有效末帧，辅助损失
排除前史和存储补齐后缀，量化校准同样裁到有效长度。补齐帧不冒充静音负例。
主机特征桥的有界输入从六秒扩为八秒，仅容纳五秒源加最多三秒前史；
没有扩大板端 arena、CP RAM 或模型状态，仍逐行使用同一生产 C 前端。

复用现有测试覆盖八秒特征、长目标未截断、补齐后缀损失/梯度为零、
按实际末帧计算准确率及校准不读取补齐帧，13 项 TF 环境测试通过。
`c-long-source-experiment.json` 记录本次训练条件和推进/产品门槛；
正常及语速结果需待该训练和独立评估完成，不能以支持长输入即宣称慢速通过。
Q05 仍为有真实离线改善但候选未合格；Q01–Q10 的其他未完成边界不变。

### 完整慢速源评估完成及对照有效性修正

上述训练现已退出 0，完成 24 轮；模型 26680 字节，SHA256
`cc9c98ca9ca4db8ae4b205057ae7d8dd9d29939dbedb4f07ce7bc06dc200a750`。
新增 32 个完整慢速副本后，变速副本合计 702，正例原始来源仍为 59 组。
正例总权重仍约 3027、每来源约 51.3051，不把变速副本计作新说话人。
相同连续流验证集及原判据下：

| 条件 | 三秒上限时间监督 | 支持完整长源 | 长源候选窗口外事件 |
| --- | ---: | ---: | ---: |
| 原速/原幅 | 18/18 | 18/18 | 1 |
| 数字增益 0.25 | 18/18 | 18/18 | 1 |
| 数字增益 0.10 | 18/18 | 18/18 | 2 |
| 语速 0.75 | 10/18 | 16/18 | 4 |
| 语速 1.25 | 18/18 | 17/18 | 3 |
| 语速 1.50 | 15/18 | 11/18 | 5 |

重复触发均为 0。纯负例 469800 ms 内 0 次事件，不等于长期误唤醒达标；
表中窗口外事件包含正例会话的区间外事件，不能混作纯负例统计。
快语速退化，未过预注册推进门槛，不继续十小时评估或生成部署包。
生产 TFLM 主机入口 10281 行、两次 reset 与参考分数差为 0，
arena_used=21824 字节；仍非板端执行时延或声学验收。
证据：`c-long-source-result.json`、`robustness-validation/c-long-source-report.json`、
`frontend-c-long-source-native/result.log`。

进一步审查发现此前合成前史使用 `seed:index:source_id` 分配；加入长源时
其他记录的序号改变，前史也随之改变。因此本次结果存在混杂，不能严格归因为
仅改变源长度，预注册的单变量意图未完全兑现。保留失败结果，不事后改接受区间。
生产训练入口现改为流式记录的 seed/source/label/PCM 内容哈希决定前史，
训练负例前史池按内容哈希排序；相邻真实负例前史保持原样，验证/测试不得入池。
元数据新增 assignment_version 和分配摘要；非流式旧流程保持原算法。
这个修正尚未用于上述权重，不可把新代码检查反算成新模型效果。

修正后已有测试验证：记录重排、前史池重排、插入长源时原样本前史不变；
测试来源不能进入前史池，相邻真实前史不变，非流式旧分配一致。
`robustness-validation/stable-history-tests.log` 为 13 passed，
当前训练代码 SHA256 为
`3efe4cfc75864cf4878529e831005c4ba6c57cd80a77bebc85079f4d8524a225`。

另复用旧 weight50/tempo 两个模型的既有逐帧分数，不重新推理或训练：
先精确重现 108 个会话的生产判据事件，再仅作离线判据诊断。每 20 ms
判定且要求连续 300 ms 高分，没有改善 weight50 的快语速 0/18；缩至
160 ms 支持也仍为 0/18。tempo 模型同样只能从快语速 7/18 到 8/18，
尽管慢速从 5/18 到 13/18。这否定“单改判定频率即可解决快慢泛化”的
假设，不据此放宽生产判据。该诊断只有正例会话，不能评估长期误唤醒，
也不能把原粗目标区间当精确声学词尾。结果为
`robustness-validation/fixed-score-cadence-diagnostic.json`；未修改板端判据。
