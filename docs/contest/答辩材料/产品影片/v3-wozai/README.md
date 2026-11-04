# 《我在》v3.1 · 傻妞叙事动画（可编辑工程）

一部约 9 分 38 秒的叙事短片：分手第三个月，周屿收到前女友订好的生日礼物——一块开发板。为了留住她熟悉的样子，
他亲手把它做成了会说“我在”的傻妞；一场停电里，他终于看懂了她留下的卡片。

**人物与情节纯属虚构｜产品交互为动画演绎。** 周屿不是项目作者，故事不是任何人的经历。片中等待经过剪辑，不代表实测时延；
每个功能的实现位置和证据等级见 [FEATURE_MAP.md](FEATURE_MAP.md)。

本目录是 v3.1 的唯一可编辑画面、剧本和制作工具源；成片和交付物在交付目录（见下）。不提交生成的
音频、缓存、剪映草稿或旧版配音。旧版本保留在各自的历史位置，未被本目录覆盖。

最新交付是在既有 v3.1 成片上完成的 **板载同源“我在”音频 9 处替换，并保留烧录中文字幕**；替换的校验元数据在
[`verification/20261104-board-wake/`](verification/20261104-board-wake/)。这是一项交付后处理记录，
不表示本目录在此次同步中重新制作过音频或视频。

## 读什么

| 文件 | 内容 |
| --- | --- |
| [SCREENPLAY.md](SCREENPLAY.md) | 定稿剧本（与成片一致） |
| [STORYBOARD.md](STORYBOARD.md) | 159 个镜头：成片时间、画面、声音（由影片自身导出） |
| [FEATURE_MAP.md](FEATURE_MAP.md) | 53 项功能 × 片中位置（成片时间）× 实现 × 证据，含不出现的 5 项及原因 |
| [REVIEW.md](REVIEW.md) | 成片检查记录与仍存在的问题 |
| [SOURCES.md](SOURCES.md) | 素材、模型、字体、声音的来源与许可 |

## 结构

```
index.html            整片（HyperFrames 0.8.205，1920×1080，30 fps，577.98 s）
index-range.html      只渲染一段（data-film-offset / data-duration，默认第 10 场）
src/                  影片源码：场景、镜头、世界状态、屏幕内容、角色、设备、插段、叠加层
  edit.js             总时间线：场景、台词/音效放置、放慢段（warp）、功能出现位置
  scenes_a.js / scenes.js / scenes_b.js   第 1–9 / 10 / 11–13 场的镜头
  world.js            灯光、天气、道具、傻妞的位置与双眼（按固件行为）
  screens.js          手机 App 与电脑屏幕（终端、AI 助手、资源管理器、路由器、工作台）的时间线
  phoneui.js pcui.js  手机与电脑界面的绘制；overlay.js 可读的手机特写、标签、片名、片尾
  device.js character.js face.js cutaway.js   傻妞、周屿（含表情贴花）、芯片插段
  timewarp.js         故事时间 ↔ 成片时间（与 tools/editlib.py 同一算法）
script/               features.json（功能清单与证据）、voice-timing.json、shots.json
audio/voice/zhouyu/   周屿台词清单；授权外部配音的输入与输出不入仓
audio/voice/shaniu/   傻妞台词清单；旧 W.wav 生成流程引用公开音源版本
audio/sfx/ audio/music/   程序音效与配乐的生成器和文本清单；WAV 输出不入仓
audio/stems/ audio/master/  分轨与母带的生成位置；输出不入仓
subtitles/            字幕：wozai.zh-Hans.srt / .vtt / .ass，subtitles.json
assets/               模型（zhouyu.glb、hands.glb）、眼睛素材、板子照片、字体
blender/              角色与手的建模脚本
tools/                全部制作工具（见“重建”）
```

## 修改入口

- 改剧情节奏：`src/edit.js` 的 `lines`（台词放置）、`sfx`、`warp`（放慢某段故事时间，让它“呼吸”）。镜头和屏幕内容写在故事时间里，
  声音按 `warp` 换算到成片时间，字幕和口型自动跟随。
- 改镜头：`src/scenes*.js`；常用模板在 `src/templates.js`（过肩、特写、显示器区域取景、手机特写、侧手持物）。
- 改屏幕文字：`src/screens.js`（终端命令与回执取自真实 CLI 输出字段）。
- 改台词：`audio/voice/zhouyu/narration.json`（周屿）或 `tools/build_shaniu_voice.py`（傻妞）。周屿成品配音需要另行提供已获授权的输入；本仓不含该音频。
- 预览一帧：`npx --yes hyperframes@0.8.205 snapshot --no-browser-gpu --no-end --at <成片秒数>`（时间为成片时间）。
- 剪映：由调用方以 `--name` 指定新草稿名（画面按 13 场切段；旁白 / 对白与设备声 / 配乐 / 音效 四条分轨；
  母带参考轨；字幕轨，另有“字幕（上）”放她在他说话时的台词，与 ASS 的顶部位置一致）。四条分轨是母带前电平（合计约 -21 LUFS），母带在此基础上 +5.8 dB 并做真峰值限幅；
  草稿里母带轨音量为 0。要听或导出与成片完全相同的声音：打开母带轨、关掉四条分轨。

## 依赖

- Node.js 20+、`npx hyperframes@0.8.205`（自带无头 Chrome）。WSL 下 WebGL 用软件渲染：`export PRODUCER_BROWSER_GPU_MODE=software` 并加 `--no-browser-gpu`。
- Python 3.10：numpy、scipy、soundfile、librosa；faster-whisper（medium，CPU）与 pypinyin（字幕切分、停顿对齐、可懂度检查）。
- FFmpeg 6+，需带 `rubberband` 滤镜。
- Kokoro 固定文件与 sherpa-onnx 1.13.8：调用时以 `KOKORO_DIR` 显式指定；其来源与公开“我在”素材见 `app/bk7258/assets/WAKE_REPLY_PUBLIC.md`。
- 周屿配音：如需重新生成，调用方必须显式设置 `JY_DIRECT_VOICE_SCRIPT`，并自行确认其服务权益；该步骤会把本片台词发给所选配音服务。
- 剪映草稿：调用方必须显式设置 `JY_SKILL_ROOT` 与 `JY_PROJECTS_ROOT`。不打开、不操作剪映界面。
- 角色重建（可选）：Blender 5.2，`blender/build_character.py`。

## 重建（按顺序）

```bash
cd docs/contest/答辩材料/产品影片/v3-wozai
REPO=$(git rev-parse --show-toplevel)
: "${KOKORO_DIR:?set KOKORO_DIR to the approved Kokoro/sherpa inputs}"
: "${JY_DIRECT_VOICE_SCRIPT:?set JY_DIRECT_VOICE_SCRIPT to the approved voice helper}"
: "${JY_SKILL_ROOT:?set JY_SKILL_ROOT to the JianYing editor Skill}"
: "${JY_PROJECTS_ROOT:?set JY_PROJECTS_ROOT to the local JianYing drafts root}"
R="${FILM_RENDER_DIR:-$REPO/out/film-lab/renders}"
F="${FILM_FINAL_DIR:-$REPO/out/film-lab/final}"
C="${FILM_COVERS_DIR:-$REPO/out/film-lab/covers}"
: "${FILM_DELIVERY_DIR:?set FILM_DELIVERY_DIR to an empty delivery directory}"

# 1 声音（周屿音频不是仓库输入；只有在已获授权时才执行）
python3 "$JY_DIRECT_VOICE_SCRIPT" audio/voice/zhouyu --voice-resource-id 7372406447696515584
python3 tools/direct_voice.py                      # 停顿与 0.95× 放慢；比原声更难懂的句子自动退回
python3 tools/build_shaniu_voice.py --kokoro "$KOKORO_DIR" --repo "$REPO"
python3 tools/voice_timing.py && python3 tools/build_speech_js.py
# 2 音效、配乐、分轨、母带（-16 LUFS，≤ -1 dBTP）
python3 tools/build_sfx.py && python3 tools/build_music.py && python3 tools/mix.py && python3 tools/master.py
# 3 字幕、功能对照、分镜
python3 tools/build_subtitles.py && python3 tools/build_feature_map.py && python3 tools/build_storyboard.py
# 4 画面（只有画面、没有声音；约 2–2.5 小时，32 核 CPU 软件渲染）
export PRODUCER_BROWSER_GPU_MODE=software
npx --yes hyperframes@0.8.205 render --no-browser-gpu -q standard --workers 5 -o $R/wozai-v3.1-picture.mp4
# 5 成片：画面 + 母带 → 无字幕版 wozai-v3.1-clean.mp4 与烧录字幕版 wozai-v3.1.mp4
bash tools/finalize.sh "$R/wozai-v3.1-picture.mp4" "$F"
# 6 缩略图与封面（取成片 387.5 s 与 415.37 s 两帧）
python3 tools/build_covers.py "$F/wozai-v3.1-clean.mp4" "$C"
# 7 剪映草稿（新名字；同名草稿会被拒绝）。画面用第 4 步的纯画面文件
python3 tools/build_jianying.py --picture "$R/wozai-v3.1-picture.mp4" --name 傻妞影片-我在-v3.1
# 8 交付（新文件名，已存在即停止）
bash tools/deliver.sh "$F" "$C" "$FILM_DELIVERY_DIR"
```

只改片尾或某一段时，不必重渲整片：用 `index-range.html` / `index-endcard.html`（`data-film-offset` 为起点、
`data-duration` 为长度）渲染那一段，再用 `tools/splice.sh <整片画面> <该段> <起点秒数> <输出>` 在帧边界上替换。
本次交付就是这样做的：整片渲染开始后修改了片尾的配音署名，于是单独重渲片尾（成片 566.0 s 起，360 帧），
在第 16980 帧处接上（接点落在片名“我在”的静止段，两侧画面相同）；从现在的源码一次渲染得到的内容相同。

## 交付位置

由 `FILM_DELIVERY_DIR` 指定的交付目录中，以 `傻妞影片-我在-v3.1` 开头的文件包含成片（烧录字幕）、无字幕画面、字幕三种格式、四条分轨与母带、缩略图与封面、交付说明和校验和。该目录是本机交付位置，不属于仓库输入。

## 事实边界（片中刻意遵守）

- 唤醒词只有“你好，openvela”；“我在”是项目公开内置素材，不是板子扬声器录音；私有授权素材不用于本片。
- 云端语音不是离线能力；断网时固件不播放“我在”、不改变双眼，片中的沉默如实表现这一点。
- 傻妞不能用语音发起电脑操作；命令来自电脑上的 AI 助手，傻妞只提醒，手机决定。
- 32.8 秒那次响应被作者归因于手机热点问题，本片不使用它，也不把片中时长当作时延数据。
