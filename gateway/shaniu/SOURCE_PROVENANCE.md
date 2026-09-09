<!-- SPDX-License-Identifier: Apache-2.0 -->
# 来源与许可证

| 范围 | 来源与许可处理 |
|---|---|
| `shaniu_gateway/protocol.py` | 根据本仓 `app/bk7258/bk7258_voice_companion.[ch]` 的 `companion-v1` 常量、40-byte network-order header 和会话约束独立实现；未复制外部源码，两端均适用本仓 Apache-2.0。 |
| `shaniu_gateway/server.py` | 本仓创建的 WSS 固定回复/provider 接入实现，适用 Apache-2.0；调用 Python 标准库及 `websockets` 的公开 API。 |
| `shaniu_gateway/devices.py` | 本项目独立实现的操作员证书绑定配置读取，Apache-2.0；使用 Python 标准库，不包含真实设备登记信息或凭据。 |
| `shaniu_gateway/console.py` | 本项目按现有 Android console-v1 契约独立实现，Apache-2.0；使用 aiohttp HTTPS/WSS 和 Python sqlite3 保存控制代次，未复制外部服务源码。 |
| `tests/fixtures/console-v1/*.json` | 本项目合成的跨语言协议样例，由 Python 真实本地端点测试及 Android 测试共同消费；不包含真实设备、令牌或用户数据。 |
| `shaniu_gateway/mimo.py` | 本仓依据小米 MiMo 官方 ASR、OpenAI-compatible chat、TTS 文档（2026-09-07 核对，网页标注更新 2026-07-17）独立实现，Apache-2.0；未复制官方 Agent 或第三方客户端源码。输入 WAV 使用 Python 标准库，HTTPS 使用 aiohttp 公开 API，重采样通过外部 FFmpeg 进程的标准输入/输出调用。 |
| `websockets==12.0` | Python 第三方依赖，发行包元数据声明 BSD-3-Clause；本仓不复制其源码。 |
| `aiohttp==3.14.3` | Python 第三方依赖，本机发行包元数据声明 `Apache-2.0 AND MIT`；本仓不复制其源码。 |
| FFmpeg | 使用系统安装的可执行文件与流式重采样器，不随本仓分发源码或二进制；实际许可证和启用组件以部署版本的 `ffmpeg -L`/构建配置为准。 |
| `tests/*.py` | 本仓为上述协议与服务创建的 host 测试，适用 Apache-2.0；测试证书在临时目录即时生成，不作为源码或交付凭据。 |

协议来源：[ASR](https://mimo.mi.com/docs/en-US/api/audio/Speech-Recognition)、
[Chat](https://mimo.mi.com/docs/en-US/api/chat/openai-api)、
[TTS](https://mimo.mi.com/docs/en-US/api/audio/tts)。只依据字段与交互契约实现，不复制示例文本。

复用审计的官方 Agent 基线为 `packages/ai_agent` 提交
`41723c61725c4e845bfee724f3ad2fafc416b6e1`（Apache-2.0），重点路径为
`src/voice/{voice_channel,voice_asr,voice_tts,audio_capture,audio_playback}.c`、
`src/channels/cmd_llm.c` 和 `src/core/agent_loop.c`。本轮未复制或改动这些源码，
也不将 Python Gateway 的 MiMo adapter 宣称为已部署官方 Agent。
