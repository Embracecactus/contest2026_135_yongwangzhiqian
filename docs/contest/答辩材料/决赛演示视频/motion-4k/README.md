# 傻妞 · 4K 暖白朱红动画版

本版根据作者对首版“UI 不喜欢、没有动画”的反馈重做视觉。采用作者选定的暖白与朱红，复用已选定的短发角色，在同一套剪辑内容上加入实际运动的角色/设备示意、审批流程、架构说明与操作导读。首版 1080p MP4 和原始视频保留。

## 交付

成片位置：

```text
<本地交付目录>\傻妞-决赛演示视频-4K动画版-20261104.mp4
```

- 3840 × 2160，30 fps，H.264 / AAC，48 kHz 双声道。
- 最终文件 1.317 GB；另有 [7 秒审批动画预览](assets/approval-preview-with-sound-4k.mp4)，可先看动作与声音风格。
- 剪辑时间线 7 分 06.9 秒；6 段动画合计 47 秒，其中 17.5 秒架构动画与原片讲解重叠，不增加讲解时长。
- 最终大小、时长、SHA-256 与整片解码结果见 [delivery.json](delivery.json)。
- 图形、文字和动画按 4K 画布绘制；初赛原片为 1080p，电脑录屏为 720p。缩放与局部放大不等于增加原片真实细节；角色图片复用原项目素材。

| 成片起点 | 段落 | 新增设计 |
| --- | --- | --- |
| 00:00 | 相遇 · 实机 | 短发角色、眨眼设备、回应气泡与手机/电脑连接动画；实拍阶段用暖白画框、朱红导读 |
| 03:56.5 | 架构说明 | 在原片 229–246.5 秒的讲解上覆盖架构动画；CP、AP SMP、RPMsg 与两份固件随原声依次强调 |
| 04:38.9 | 相识 · 手机 | 手机认领、网络连接、个性化设置依次完成的流程示意，再接旧版 App 实拍 |
| 05:44.3 | 决定 · 审批 | 电脑请求传给傻妞，再到手机；批准后反馈到电脑，计算器出现；实录增加平滑局部放大 |
| 06:29.4 | 延伸 · 工作台 | 表情、动作、单帧取景连续切换；实录保留采图结果 |
| 07:01.4 | 片尾 | 回到同一个角色、设备和“我在”的回应，收束主题 |

动画章节只加入本项目程序生成的轻提示音，没有新增配音或外部音乐。实录原声保留，架构段使用原作者同一段讲解；没有把新的提示音加到真实设备操作中冒充设备反馈。

## 素材与事实边界

- 实机、App、电脑审批、工作台仍使用上一版四份原素材。完整输入文件和字幕/独立 WAV 的哈希见 [sources.json](sources.json)。
- 初赛实拍与旧版 App 身份持续保留在画框顶部。其他唤醒词切换段、重复致谢和末尾未来规划口述继续避开。
- OTA 只展示入口，导读明确写“本段未执行升级”。
- 新增角色、设备造型、手机面板、摄像头画面与审批动画是图形示意，不能替代实物照片或最新固件验收。审批实录仍只证明本段手机批准后计算器打开；不能用它宣称拒绝路径、AI Hook 或固件 777 完整验收。
- 架构动画明确标为示意，按原声强调 CPU0 的 CP、CPU1+2 的 AP SMP、核间通信和 CP/AP 两份固件；图中没有虚构性能测量值。
- 实录原有烧录字幕保留。新增架构画面内重排对应讲解字幕；独立 [4K 时间轴 SRT](中文字幕-4K时间轴.srt) 供继续编辑，不重复覆盖其他原片字幕。

## 可编辑源与剪映

| 文件 | 作用 |
| --- | --- |
| [edit.json](edit.json) | 片段切点、章节、导读、取景放大配置 |
| [scenes.js](scenes.js) + [index.html](index.html) | 6 段确定性 Canvas 动画，可按任意时间重绘 |
| [render.mjs](render.mjs) | 无界面 Chromium → 4K 图形帧 → FFmpeg NVENC；使用已安装的 Playwright |
| [build.py](build.py) | 4K 画框、导读、原创轻音效、原声提取、视频合成、剪映草稿、核验 |
| `assets/` | 剪辑依赖的动画 MP4、透明画框、导读 PNG 和音频 |
| `jianying-draft/` | 保存后的剪映 JSON 快照 |
| `verification/` | 动画状态抽帧、实录合成抽帧、渲染日志与检查结果 |

本机剪映工程：**傻妞-决赛演示-4K暖白朱红-20261104**。

草稿保留原片/动画主轨、独立透明画框、带位移/透明度关键帧的导读、操作放大关键帧、原讲解和轻音效音轨。Skill 会把引用素材暂存到草稿的 `materials/`，项目中同时保留代码源与素材身份记录。画面内的角色和流程动画以已渲染片段进入剪映，内部对象须修改 `scenes.js` 后重新生成。

本轮不打开剪映界面。FFmpeg 成片与剪映使用同一剪辑表，但两者的局部放大缓动插值、图层合成与最终限幅并非逐像素/逐采样相同；提交用 MP4 为当前核验对象。

角色素材来自主仓 `android/shaniu-companion/app/src/main/res/drawable-nodpi/shaniu_launcher_response.webp`；字体复用答辩材料内的 `素材/fonts/MiSans-Medium.ttf`。其余新增图形和轻音效在本项目中编写，没有下载新的第三方图片、音乐或配音。

## 重新生成

```bash
python3 ../build.py prepare --videos /path/to/videos
python3 build.py prepare --videos /path/to/videos
node render.mjs --batch
python3 build.py render --videos /path/to/videos
python3 build.py assemble --videos /path/to/videos
python3 build.py draft --videos /path/to/videos
python3 build.py verify --videos /path/to/videos
```

`render.mjs` 使用当前 Node 环境已安装的 Playwright 与对应 Chromium；可设置 `PLAYWRIGHT_PACKAGE` 和 `CHROME_BIN` 指向相应安装。FFmpeg 须能运行 `h264_nvenc`。`--batch` 和 `build.py render` 复用已有生成结果，避免重复编码。修改某个动画后，可以显式更新它，再重做相应时间线片段，例如：

```bash
node render.mjs --scene approval --duration 7 --output assets/approval-4k.mp4
python3 build.py render --videos /path/to/videos --only 08-approval-card
python3 build.py assemble --videos /path/to/videos
```

`draft` 会重建本轮同名剪映草稿；若已在剪映继续手动编辑，请先另存或在 `edit.json` 修改工程名。

机器检查、抽帧检查、正常速度观看和实际听音是不同层级。最终核验结果写在 `verification/`；没有打开剪映进行正常速度全片播放，也没有把音量测量当成人工试听。

本次最终核验已通过：整片解码零错误；检查 16 个动画状态帧和 15 个成片时间点；手机操作、计算器与采图结果保留在放大画面内；综合响度 −16.08 LUFS、真峰值 −1.99 dBTP；剪映草稿 3840 × 2160、总长 426.9 秒、36 个媒体引用均可找到。已删除本轮可重建的分段编码缓存，保留最终成片、预览、动画源、草稿依赖与核验记录。

## 源码归集边界

MP4/WAV 原片、剪映草稿、字体和渲染缓存保留在本地；仓库保存代码、剪辑表、字幕、图片和公开验证摘要。重新生成需准备 `sources.json` 中的输入及上级 `素材/README.md` 指定的 MiSans 字体；草稿路径通过 `JY_SKILL_ROOT` / `JY_PROJECTS_ROOT` 选择。历史验证记录描述当次产物，本次未重新渲染全片。
