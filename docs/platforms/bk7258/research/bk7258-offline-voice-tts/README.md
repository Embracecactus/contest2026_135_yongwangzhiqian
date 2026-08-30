# bk7258 完全离线单说话人语音 TTS — 研究文档索引

> **状态：研究提案，尚未实施。** 当前 `chips/bk7258/` 与 `boards/bk7258/`
> 不包含 TTS 模型、推理运行时或板级集成代码；本文档中的算力、内存和实时性数据除非
> 明确标为既有 BK7258 平台证据，均为目标、估算或待执行的验证方案，不是 TTS 实测结果。
> 该目录暂不作为当前产品能力或验收交付声明。

> 目标:在 BK7258(三核 Cortex-M33,最高 480MHz,带 DSP/SIMD,无 NPU;640KB SRAM + 16MB PSRAM + 1GB 存储)上,**完全离线、模型在芯片**地实现单说话人语音合成(TTS)。
> 声音素材来源:已获本人明确授权的私人语音(微信语音消息),经导出/解密/转码为 16kHz/16bit/单声道 wav,用作单说话人训练/拼接语料。

## 文档地图

| 文档 | 内容 | 阶段 |
|---|---|---|
| [01-dataset-preparation.md](01-dataset-preparation.md) | Whisper 转写 + MFA 强制对齐,把原始 wav 变成 `(audio, text/phoneme, duration)` 训练集 | Step 0 数据制备 |
| [02-model-architecture.md](02-model-architecture.md) | ~1M 参数非自回归声学模型逐层参数/算力表,INT8 友好 | Step 1 模型定义 |
| [03-int8-quantization-tflite.md](03-int8-quantization-tflite.md) | PTQ/QAT 量化、TFLite 导出、算子覆盖与坑 | Step 2 模型导出 |
| [04-board-benchmark.md](04-board-benchmark.md) | bk7258 板端 GOPS / RTF / SRAM 实测基准方案(放行闸门) | Step 3 板测验证 |

## 两条可行路线(本仓库调研结论)

- **方案 A(神经 TTS,推荐深入)**:~1M 参数 INT8 声学模型 + Griffin-Lim 声码器,全链路离线,模型在芯片。sanoTTS 在更弱 ESP32-C3(160MHz 无 DSP/FPU)已 5.7× 实时,佐证 bk7258 实时余量充足。
- **方案 B(拼接式,保底)**:用对齐切出的音素/双音子单元直接拼接,零训练、保真度最高、零算力依赖;本索引文档集聚焦方案 A,方案 B 可复用 01 的对齐产物。

## 关键约束与合规

- **算力**:推算 1–2 GOPS(480MHz × ~1 MAC/周期 × 2),经板测(见 04)确认。
- **内存**:权重 ~1.3MB 放 PSRAM/Flash;激活需 TFLM Arena **逐层流式**压在 640KB SRAM 内。
- **音频**:BK7258_AUD 固定 16kHz/16bit/单声道,设备名 `pcm0p`;输入素材统一为该规格以免板上重采样。
- **合规**:声音与私人聊天内容均属个人敏感信息,本方案**仅在已取得数据主体明确授权**前提下成立;处理全程本地、模型仅用于授权者自有设备、不分发。请保留授权记录。

## 构建入口(唯一)

```
tools/bk7258/bk7258.py build | package | verify
```
