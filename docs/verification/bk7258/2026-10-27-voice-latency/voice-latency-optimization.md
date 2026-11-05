# BK7258 语音响应延迟优化

> 证据可得性（2026-11-04 核验）：本文引用的工作区 `out/` 原始产物不可用，仅保留本文记录的摘要/哈希；原链接已改为路径文本，结论边界不变。见[验收原始证据留存审计](../2026-11-04-acceptance-evidence-retention.md)。

日期：2026-10-27

## 选型结论

**产品代码选择 R1，R2 只保留为 bktest 研究候选。**

- R1 不改 LLM 语义、工具、回答长度、TTS 文本或队列水位，只将
  Media open/prepare 与已经开始的 PCM 预填充重叠。它在普通、弱网和
  工具回合均缩短首次 Media 写入，且没有新的网络请求。
- R2 让普通无工具回答从第一次流式请求直接进入现有
  LLM→分句→TTS→播放流水线，在受控普通问答上改善约 58%。
  但工具问题需先路由再进入既有 planner，首写入反而慢 16–17%。
  在没有真实 MiMo 协议遵循率和工具流量占比数据前，不启用于正常产品配置。

## 改动 1：提前准备唯一播放器（R1）

### 问题

原消费线程需等队列达到 8192 B 预填充后，才在消费路径内调用
`audio_playback_open()`。Media open/prepare/start 和 PCM 预填充串行。

### 实现

`packages/ai_agent/src/voice/voice_channel.c` 新增单一
`tts_output_prepare()` 边界。第一个 PCM 回调先在同步 TTS 请求仍被锁定时
确认格式；唯一消费者随后在队列继续填充到原水位的同时执行
Media open/prepare。`CONFIG_AI_AGENT_TTS_EARLY_MEDIA_PREPARE` 默认开启，
配置中可显式关闭以回到原串行路径。

不变的契约：

- 仍只有一个 PCM 消费线程和一个 Media handle。
- 仍使用 64 KiB 有界队列、8192 B 预填充和 2048 B 消费片。
- 不提前播放未确认文本，不改重采样、TTS 格式或音频帧对齐。
- 取消仍经过原 `tts_abort`、Media stop/close 和回合 ID 检查。

### 结果

| 场景 | R0 P50/P95 | R1 P50/P95 | 改善 |
|---|---:|---:|---:|
| 正常普通问答 | 895/899 ms | 815/817 ms | 8.9% / 9.1% |
| 弱网普通问答 | 1447/1454 ms | 1328/1330 ms | 8.2% / 8.5% |
| 工具回合 | 857/860 ms | 777/783 ms | 9.3% / 9.0% |

R1 改善首音边界，不会缩短已给定 PCM 的总播放时长；普通回合
播放结束 P50 从 929 ms 变为 933 ms，4 ms 差异属于主机调度噪声，
未宣称全文完成改善。

## 改动 2：普通回答的一次流式进入（R2）

### 问题

官方 Agent 已经具备真正的最终答案 SSE→增量文本队列→自然分句→
TTS PCM→Media 流水线，但普通问题仍先走一次缓冲规划请求。
只有 planner 决定进入 final phase 后，后面的流水线才能开始。

### 实现

- `Kconfig` 新增默认关闭的 `CONFIG_AI_AGENT_VOICE_ROUTE`。
- `agent_loop.c` 的首次语音请求要求严格、有序的 `final`/`tools`
  JSON envelope。
- `llm_proxy.c` 独立实现有界流式 JSON 解码，只在 `answer` 字符串
  语法被确认后转发解码文本。协议头、reasoning、tool delta、转义符和
  半个 Unicode 代理项不会被播报。
- 在任何正文开始前，`tools` 或无效前缀可回退到原工具 planner；
  正文一旦开始，错误只中止当前回合，不会再请求并重复播报。
- 原工具权限、Session 历史、`agent_finalize`、取消和最终回答长度保留。
- 正常 `openvela_ap` 不启用该开关；只在 `openvela_ap_bktest`
  中开启以采集真实供应商遵循率与工具分流数据。

### 结果与止损

| 场景 | R0 P50/P95 | R2 P50/P95 | 变化 |
|---|---:|---:|---:|
| 正常普通问答 | 895/899 ms | 375/376 ms | -58.1% / -58.2% |
| 弱网普通问答 | 1447/1454 ms | 678/682 ms | -53.1% / -53.1% |
| 工具回合 | 857/860 ms | 997/1007 ms | +16.3% / +17.1% |

因工具路径退化且没有真实 MiMo 验证，Gate 结论为“保留研究，不替换
默认”。这也防止把只对普通问答有利的指标描述成整体提升。
在同一受控回答上，R2 的完整播放结束代理 P50/P95 也从
929/933 ms 降到 817/819 ms（-12.1%/-12.2%）；弱网从
1556/1561 ms 降到 1330/1335 ms（均约 -14.5%）。工具回合则从
974/977 ms 增至 1114/1126 ms（+14.4%/+15.3%）。

## 文本分段与真正流水线

本轮复用并验证既有 `reply_text_worker`：

- 第一句在完整中英文标点边界就可提交 TTS。
- 不拆分 UTF-8 字符；英文 `.`/`;` 和中文句末有最小长度。
- 逗号只在累积足够文本后分段；无及时标点时有 300 ms 有界空闲
  flush；最后残余文本在 stream end 播报。
- 第一句播放时，LLM 可继续生成后文；每个 TTS 句段按顺序同步
  生成 PCM，共享同一播放队列和 Media handle。

因此 R2 不是只把 API 参数改成 `stream=true`；受控测试确认首个有效
正文 delta 在 260 ms 进入分句/TTS，而控制的后续 LLM 尾部在
700 ms 才释放。首次 Media 写入已在 375 ms 发生，证明三个阶段
实际重叠。

## 改动 3：不记录正文的端到端剖析

- Agent/Voice 新增 `agent_ready`、`llm_first_token`、Media prepare、队列峰值、
  PCM gap、rebuffer、Media write 等数值事件。
- BK7258 云适配器在 engineering build 记录 DNS、TCP connect、经验证 TLS
  handshake 和 session-offer 阶段；不记录域名、证书、密钥或 HTTP 正文。
- `hil_test.py` 能按匿名 request ID 聚合 T0–T12 和网络分段。没有外部
  同时钟 DAC/声学标注时，`dac_start_ms` 保持 `null`。

这些网络埋点只在 `CONFIG_BK7258_ENGINEERING_TEST` 中编译。由于本轮无
板端云请求，未根据假设开发 DNS 缓存或共享 TLS context。现有证书验证、
hostname 校验、可信时间、取消和各后端独立 TLS 所有权均保留。

## ASR 路径对照与本轮决策

| 契约 | 默认 Batch MiMo/OpenAI-audio | 显式 FunASR 流式候选 |
|---|---|---|
| 音频发送 | 录音结束后发送完整 PCM/WAV | 录音期间按块发送 |
| 连接时机 | 端点后 request prepare/open | 回合开始可 preconnect |
| 执行证据 | cloud request/http host 测试通过 | 真实 `funasr_asr.c` 受控 WebSocket 帧、final、error、cancel 契约通过 |
| 最终文本 | 一次最终结果 | 只在 `stream_finish` 接受最终稳定结果，online partial 不执行 |
| 产品当前证据 | 默认 dialect 会选中 | 必须有 `asr_backend=funasr` 和独立 host/path/port；本轮无设备配置证据 |

这已验证流式候选的实际代码和正确性边界，但没有同一设备、同一
语音和真实服务上的数值延迟。因此本轮不擅自改变 `asr_backend`；该
对照尚未达到产品选型证据层。

## 响应内容策略

本轮没有删除 persona、会话历史、工具能力或回答长度，也没有用
固定答案替代 LLM/TTS。工作区原有的 MiMo thinking 设置仍保留：用户
显式开启时必须开启，未开启时使用已有快速路径。R2 的分流实验
没有绕过工具，正是因为工具回合延迟退化而未进入默认产品配置。

## 代码范围

本轮实际改动：

- `packages/ai_agent/Kconfig`
- `packages/ai_agent/src/core/agent_loop.c`
- `packages/ai_agent/src/core/agent_trace.h`
- `packages/ai_agent/src/llm/llm_proxy.c`
- `packages/ai_agent/src/llm/llm_proxy.h`
- `packages/ai_agent/src/voice/voice_channel.c`
- `app/bk7258/bk7258_agent_cloud.c`
- `app/bk7258/bk7258_voice_tls.c`
- `app/bk7258/bk7258_voice_tls.h`
- `boards/bk7258/aidk_ai_toy/configs/openvela_ap/defconfig`
- `boards/bk7258/aidk_ai_toy/configs/openvela_ap_bktest/defconfig`
- `tools/bk7258/_lib/hil_test.py`
- `tools/bk7258/voice_latency_benchmark.py`（2026-11-04 移至 `tests/host/bk7258/`，测量逻辑不变；重跑写入新的时间戳输出目录）
- 相关 host fixture 和回归测试。

`voice_channel.h` 中的 input-submission lease 以及项目中已有的 MiMo thinking/
response-length 改动在本轮前就存在，只被保留，不计作本轮成果。

## 验证

### 主机

- 150 个 R0/R1/R2 受控回合全部通过；每回合只提交一次文本、
  PCM 为 16384 B 且逐字节有序、Media 只 open 一次。
- 10 个 mixed/tool/final/cancel/protocol/vision 场景通过，包括中途取消、
  无效协议和 mutation 必须被拒绝。
- Agent TTS queue、final SSE、endpoint、FunASR、Media player、capture/
  recorder、TLS 并发/取消、TTS isolation、cloud config/request/http 相关目标通过。
- `hil_test.py` 28 项解析契约通过。

### 构建

- 正常 `app/openvela_ap` direct CP/AP 构建通过，且 build manifest 重哈希通过。
  AP image：1,973,598 B，SHA-256
  `73bdc2f8be8854dea02f7b6ba2d55a1c7accad80777abe117824474e306e33cb`。
- `app_bktest/openvela_ap_bktest` direct CP/AP 构建通过，且 build manifest
  重哈希通过。AP image：1,990,904 B，SHA-256
  `9e1c9a9af34ec0115fbd482f4e111cbb1c5fd9eb40f8f1814bd39efa76c8abee`。
- direct 产物是未签名的构建证据，不是可发布固件；本轮未刷机。

## 资源与限制

- R1 不新增队列、线程或音频缓冲；使用现有的 16 KiB 消费线程栈、
  64 KiB PCM 队列和 Media handle。
- R2 新增有界 JSON 解码状态和最多 192 B 协议预留；主机普通回合
  RSS P50 为 R0/R1/R2 = 1900/1900/1908 KiB。这不是板端 RAM。
- 尚无 BK7258 CPU、峰值 RAM、DAC 起播、外放断音/爆音/欠载和真实云
  P50/P95 数据，因此不声称达到 1.5 s 产品目标。

## 复现

重跑产生新的结果与身份，不替代本文已不可用的原始产物。

```sh
cd contest2026_135_yongwangzhiqian
python3 tests/host/bk7258/voice_latency_benchmark.py \
  --rounds 30 --weak-rounds 10 --tool-rounds 10

make -C tests/host/bk7258 -B \
  run-agent-tts-queue run-agent-final-stream run-agent-endpoint \
  run-agent-funasr run-voice-media-player run-agent-capture \
  run-voice-media-recorder run-voice-tls-concurrency \
  run-voice-tls-open-cancel run-tts-isolation run-cloud-config \
  run-cloud-request run-cloud-http

python3 -m unittest tests.host.bk7258.test_hil_test

python3 tools/bk7258/bk7258.py build \
  --board aidk_ai_toy --boot direct --jobs 8
```

R2 的 bktest 构建命令使用显式 `app_bktest`/
`openvela_ap_bktest` 配置和同一分区 CSV，完整命令保存在构建日志。

## 回滚

1. R2 不需修改产品配置；正常 `openvela_ap` 已是关闭状态。如果不再评估，
   从 bktest defconfig 删除 `CONFIG_AI_AGENT_VOICE_ROUTE=y`即可。
2. R1 的单点回滚是在对应 defconfig 显式关闭
   `CONFIG_AI_AGENT_TTS_EARLY_MEDIA_PREPARE`；不需改队列、Media API 或上层会话。
3. 网络埋点仅存在于 engineering build；去掉相关 observer 不改请求
   语义。
4. 任何回滚都不涉及 KWS/H3、默认模型或音频 owner 契约。

## 下一阶段实机步骤

1. 在明确的工程板和下载授权下使用 bktest 固件，不覆盖产品固件。
2. 每种简单问候、知识问答、长句、工具、多轮、冷/热连接和弱网至少
   30 回合，捕获 UART `voice-stage` 和数字 audio trace。
3. 用同时钟 DAC GPIO/codec 事件或对齐声学录音标注 T0/T10，不用
   `first_media` 替代。
4. 分别比较批量 MiMo ASR 和已正确配置的 FunASR，保持最终转写
   语义一致。
5. 先审核 R2 真实普通/工具分流正确率、工具占比和活跃服务延迟，
   再决定是否进入产品评审。

机器可读结果见 voice-latency-results.json（本地未发布：`voice-latency-results.json`）。
完整日志、原始数值样本、源码哈希、受控补丁和复现命令见
`out/voice-latency-round10-20261027/REPRODUCE.md`。
