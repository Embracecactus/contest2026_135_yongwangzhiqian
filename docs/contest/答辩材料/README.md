# 决赛答辩与影片工程

本目录保存可编辑源码与公开的交付记录。PPT/PDF 与成片放在官方仓库 Release，比赛表单仍按要求上传；官方模板、原始实拍与完整配音分轨留在本地，不加入 Git。个人联系方式不在仓库中。

## 当前材料

[PPT](https://github.com/open-vela/contest2026_135_yongwangzhiqian/releases/download/shaniu-finals-20261104/shaniu-finals-20261104.pptx) · [PDF](https://github.com/open-vela/contest2026_135_yongwangzhiqian/releases/download/shaniu-finals-20261104/shaniu-finals-20261104.pdf) · [4K 实录合集](https://github.com/open-vela/contest2026_135_yongwangzhiqian/releases/download/shaniu-finals-20261104/shaniu-finals-demo-4k-20261104.mp4) · [《我在》完整版](https://github.com/open-vela/contest2026_135_yongwangzhiqian/releases/download/shaniu-finals-20261104/我在-v3.1-板载应答音-中文字幕版.mp4)

| 材料 | 版本与用途 | 编辑入口 |
| --- | --- | --- |
| 答辩 PPT | 39 页，19 页主讲与 20 页隐藏备查；备查 Q（第 36 页）已链接《我在》板载应答音中文字幕版 | [build.py](build.py)、[讲稿](讲稿.md)、[问答准备](问答准备.md) |
| 《我在》v3.1 | 9 分 38 秒、1080p30；Claude 原动画，Codex 烧录中文字幕并替换 9 处“我在”；人物、情节与产品交互为动画演绎 | [完整源码与重建说明](产品影片/v3-wozai/README.md)、[剧本](产品影片/v3-wozai/SCREENPLAY.md)、[功能与镜头对照](产品影片/v3-wozai/FEATURE_MAP.md) |
| 决赛演示实录合集 | 暖白朱红 4K 版，7 分 06.9 秒；初赛实机/App 与决赛电脑审批、工作台实录，含 6 段标明为示意的动画 | [4K 工程](决赛演示视频/motion-4k/README.md)、[剪辑表](决赛演示视频/motion-4k/edit.json) |
| 原始实拍切片 | 保持原速，略去的等待由片内卡片说明；历史版本身份不变 | [cut_clips.py](cut_clips.py)、[素材清单](素材/实拍/clips.json) |

最新交付文件、SHA-256 与同步来源见 [delivery.json](delivery.json)。PPT 内的影片链接是相对文件名：下载或搬动 PPT 时，把 `我在-v3.1-板载应答音-中文字幕版.mp4` 放在同一目录；影片没有嵌入 PPT。PDF 仅供静态备用。

初次归集来自两个会话的实际成果：`01a1a5e4-3a50-74c0-acdc-aaa276d70222` 的字幕/应答音更新，以及 `01a1a6ab-7c70-7511-8cad-320b289fe71d` 的 PPT 备查 Q。Claude 的影片源码从独立 `film/shaniu-narrative-v3` 工作树归入此处；原工作树、原始成片和原始证据均保留。

## 验证边界

- 影片 9 处“我在”来自当前板载同源素材，保留原空间音效处理；不是扬声器实录。画面视频流、烧录字幕及编号 `1203` 保持不变。[音频与解码记录](产品影片/v3-wozai/verification/20261104-board-wake/README.md)。
- 终校版已校正产品 R1 与研究配置、官方 Agent 技术归属、公开/私人唤醒模型边界、当前 778/App 59 验收范围及 CI 查询入口；修订生成器后重新生成 PPT、讲稿和来源记录，并用 PowerPoint 导出 39 页 PDF。主讲顺序、时长安排和 14 段内嵌视频保持不变；历史截图与实录仍保留原版本身份。
- 4K 合集中实拍来源为 1080p/720p，新增图形为 4K；不把放大后的历史片段当作当前固件验收。
- 成片的完整解码、哈希和音量检查已经记录；这不代替正常速度人工试听，也不等于比赛表单已上传。
- 最新软件修复与实板范围见 [11-04 修复记录](../../verification/bk7258/2026-11-04-finals-review-repairs.md)。

## 重建 PPT

Python 3.10+、python-pptx 1.0.2、Pillow、lxml 和 FFmpeg 为生成依赖；Windows PowerPoint 用于导出 PDF/PNG。MiSans 字体与本地素材的准备说明见 [素材说明](素材/README.md)。

1. 将原始 PPT 与官方模板放到 `docs/`，文件名和校验值见 [答辩时间与来源.json](答辩时间与来源.json)。官方模板沿用组委会原文件。
2. 将原速实拍片段放到 `素材/实拍/`；可用 `cut_clips.py --source-dir <iPhone原片目录> --videos <电脑录屏根目录>` 从清单重建。生成器另需已入仓的 11-03 实板取景录像。
3. 执行以下命令，输出到 `docs/`。`--film` 指向最新外置影片；仅运行 `--help` 不读取影片或模板。

```bash
python3 docs/contest/答辩材料/build.py --film /path/to/我在-v3.1-板载应答音-中文字幕版.mp4
powershell -NoProfile -ExecutionPolicy Bypass -File docs/contest/答辩材料/render.ps1 -Pptx <deck.pptx> -Out <export-directory>
```

影片的画面源码、模型和字体在本目录；配音服务、私人音源和历史实拍需按各工程说明准备，不能仅凭公开仓库重建与本机完全相同的整部有声影片。归集工作不调用付费配音服务。

公开附件已获作者明确授权；2026-11-04 作者确认有权公开完整动画。该确认记录于[来源说明](产品影片/v3-wozai/SOURCES.md)，不声称独立核验了服务商合同。
