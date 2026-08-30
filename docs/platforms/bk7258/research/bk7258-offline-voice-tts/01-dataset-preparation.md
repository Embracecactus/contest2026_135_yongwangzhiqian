# 文档 1:数据集制备 — Whisper 转写 + MFA 强制对齐(Step 0)

**目标**:把 `fixed/*.wav`(16kHz/16bit/单声道,单说话人语音)变成训练用的
`(audio, text/phoneme, duration)` 三元组,供方案 A 训练 / 方案 B 切单元共用。

## 1. 环境(全本地,护隐私)

```bash
python -m venv venv && source venv/bin/activate
pip install faster-whisper montreal-forced-aligner pydub
# ffmpeg / sox 需系统级已装(用于格式统一与静音检测)
```

- 用 **faster-whisper**(CTranslate2 后端,比官方 whisper 快数倍、纯本地)做转写。
- MFA 首次运行会按需下载中文声学模型 + 字典(一次性,可离线缓存)。

## 2. Whisper 转写(句级文本)

```bash
whisper fixed/ --model base --language zh \
       --output_format txt --verbose False
```

- 产出 `fixed/xxx.txt`,每文件一句文本。
- Whisper 仅给到"句"级文本;**音素级时间对齐**交给 MFA。

## 3. 文本清洗

- 去非语音标记(`[笑声]`/`[音乐]` 等)、归一标点;
- 中文数字 → 汉字(`123` → `一百二十三`),英文缩写展开,免得模型学到错音;
- 中文 TTS 最终喂音素:用 `pypinyin` 把汉字转拼音(含声调)作为模型输入。

## 4. Montreal Forced Aligner(音素级对齐)

MFA 要求语料布局(音频与文本同名同目录):

```
corpus/xxx.wav   # 16k mono,已在 fixed 阶段确认
corpus/xxx.txt   # 对应文本
```

```bash
mfa align corpus mandarin_acoustic_model mandarin_dictionary aligned/
```

- 产出 `aligned/xxx.TextGrid`:含 **phone 层 + 每音素起止时间**。
- 中文字典可用 MFA 内置 Mandarin 字典;或用 `pypinyin` 自生成 `lexicon.txt` 后传入。
- 若微信语音混入对方语音,先按说话人筛(Whisper 说话人区分或人工),只保留目标说话人。

## 5. 构建训练 manifest

遍历 `aligned/`,输出 `dataset.jsonl`(每条):

```json
{"wav":"fixed/xxx.wav","text":"你好","phonemes":["ni3","hao3"],"durs":[8,12]}
```

- 切 train/val = 95/5;单说话人无需说话人字段。
- 累加总时长,作为后续数据量判据。

## 6. 质检 & 兜底

- 抽看 TextGrid(用 praat 或文本)查对齐 gross error;
- 过滤 <0.4s、静音比过高、非目标说话人片段;
- 若有效语音 < 30 分钟:做**轻量数据增强**(±10% 语速、±2 半音变调)补充,但勿过度扭曲音色。

## 7. 交付

`dataset.jsonl` + 干净 wav 目录 → 进文档 2(模型) / 文档 3(量化) / 方案 B(切单元)。

## 注:数据来源合规

本步骤处理的语音来自私人聊天记录,**仅在已取得语音主体明确授权**时执行;全程本地、不外传。
