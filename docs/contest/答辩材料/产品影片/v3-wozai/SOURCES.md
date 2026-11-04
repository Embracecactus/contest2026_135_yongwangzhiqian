# 来源与许可（SOURCES）

## 画面

| 内容 | 来源 | 许可 / 说明 |
| --- | --- | --- |
| 三维场景、角色、镜头、界面绘制 | 本目录 `src/`、`blender/build_character.py`（原创） | 随项目 |
| 周屿模型与手 `assets/models/*.glb` | Blender 5.2 由 `blender/build_character.py` 生成（不来自扫描、照片或真人） | 随项目 |
| 傻妞的眼睛 `assets/eyes/shaniu-cyan-v2.png/.json` | 项目固件素材 `app/bk7258/assets/display/shaniu-cyan-v2.png`（SHA-256 10980433…，逐字节相同）；暖色“晨光”眼睛为片中对同一素材的色相变换演绎 | 项目素材 |
| 开发板正面 `assets/textures/aidk_top.jpg` | 由项目自有的 AIDK AI Toy 板参考照片校正得到；板上丝印为实物原样 | 项目自有参考照片 |
| Three.js 0.180.0 `lib/three/` | three.js | MIT |
| GSAP 3.14.2 `lib/gsap.min.js` | GreenSock | GSAP 标准许可（见 `lib/GSAP-README.md`） |
| HyperFrames 0.8.205 | 渲染器（`npx hyperframes@0.8.205`） | 按其发布许可使用，未打包进本目录 |
| 字体 Noto Sans SC | Google Fonts / Adobe | SIL OFL 1.1（`assets/fonts/OFL.txt`） |
| 字体 霞鹜文楷 LXGW WenKai v1.530（子集，用于手写卡片与便签） | LXGW | SIL OFL 1.1（`assets/fonts/LXGWWenKai-OFL.txt`） |

片中的手机 App 与电脑工作台界面按项目源码（`android/shaniu-companion`、`tools/bk7258/_lib/workbench_web`）重绘；
终端里的命令与 JSON 字段取自 `tools/bk7258/bk7258.py` 与 `_lib/workbench_*.py` 的真实接口。

## 声音

| 内容 | 来源 | 说明 |
| --- | --- | --- |
| 旧“我在”W.wav 的生成流程 | 项目公开内置素材 `app/bk7258/assets/wake_reply_public.pcm`（SHA-256 62091092…；Kokoro-82M v1.1-zh int8，sid 0，非克隆，来源见 `WAKE_REPLY_PUBLIC.md`） | 旧 WAV 不随源码入仓；其生成时只重采样，且不是板子扬声器录音。私有授权素材 `wake_reply.pcm` 不用于本片 |
| 傻妞其他台词 S01–S07 | 同一 Kokoro 固定文件（`csukuangfj/kokoro-int8-multi-lang-v1_1` @ 155831f1，sherpa-onnx 1.13.8），0.8·sid0 + 0.2·sid8 的风格向量混合 | Kokoro Apache-2.0；不模仿真人 |
| 四拍节律 `audio/sfx/local_rhythm.pcm` | 项目固件素材 `app/bk7258/assets/local_rhythm.pcm` | 项目素材 |
| 周屿（旁白与对白） | 剪映 SAMI 配音“温柔男主”（`zh_male_iclvop_chenxuanqinggan`，资源 7372406447696515584），经调用方显式指定的配音工具生成 | 资源元数据：is_vip 0、business_scope UNLIMITED、extra_is_vip true、entitlement_verified false。权益未核验；音频不随源码入仓，公开或商用前请在剪映中确认 |
| 上一版 GG侠 配音 | 剪映 SAMI `ICL_zh_male_zhuzhuxia` | 历史资料，不用于 v3.1，也不随本工程源码入仓 |
| 音效 `audio/sfx/*.wav` | `tools/build_sfx.py`（种子 1203）程序合成 | 原创，无采样 |
| 配乐 `audio/music/score.wav` | `tools/build_music.py`（种子 20261104）程序合成；母题“我在”F♯→D | 原创，无采样 |

只把本片的台词文本发给了剪映配音服务；没有上传音频、照片或其他资料。

## 仓库同步边界

本目录提交可编辑的画面源码、剧本、文本清单、字幕、生成工具和具有已列明来源的最小画面资产。生成的音频、试听、旧 GG侠配音、外部配音输入与后处理输出、分轨、母带、渲染缓存、剪映草稿和成片均不入仓。旧 `W.wav` 的生成流程仍对应上表所列公开音源版本；最新交付的实板“我在”替换及中文字幕 9 处替换，按 `verification/20261104-board-wake/` 中的校验元数据复核，不在此处重述为重新制作。

## 只用于分析（不进入成片）

faster-whisper medium（MIT）、SenseVoice-small int8（sherpa-onnx 转换）、3D-Speaker ERes2Net 声纹模型（Apache-2.0）、pypinyin（MIT）。
它们给出可懂度、情绪标签和音色相似度等“试听替代指标”，不是人工试听。

## 2026-11-04 公开发布确认

作者在本次材料同步任务中明确确认有权公开完整动画，并授权将最新 PPT、PDF、4K 实录合集与《我在》成片上传到官方仓库 Release。以上 SAMI 元数据描述制作时的核验状态；本次发布依据作者的明确授权，不声称独立审核了其服务合同。私人原始音源、完整配音分轨与账号材料仍不随 Git 或附件发布。
