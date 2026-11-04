# 2026-11-04 应答音更新记录

来源会话：`01a1a5e4-3a50-74c0-acdc-aaa276d70222`。本目录保存该次实际处理的公开元数据；原始完整日志、音频中间文件和授权音源保留在本地。本次同步仅将 `delivery-verification.json` 的本机交付绝对路径转为文件名，不修改测量值。

- [replacement-plan.json](replacement-plan.json)：9 个替换时点、原/新语音边界、字幕区间与音源哈希。
- [audio-verification.json](audio-verification.json)：替换区间以外的预混/对白采样相同；沿用原空间音效与母带设置。
- [delivery-verification.json](delivery-verification.json)：全片解码通过，17340 帧，1920×1080 / 30 fps，视频流与原字幕版相同；综合响度 −16.1 LUFS、真峰值 −1.5 dBTP；交付文件哈希已核对。

画面编号 `1203` 未修改。板载同源应答音不等于扬声器实录；上述检查没有声称完成正常速度人工试听。

编辑入口是 `tools/replace_wake.py`。调用时显式提供授权的 16 kHz 单声道 PCM16LE 应答音及原始工程；工具不会读取设备、不会下载/生成配音，也不修改原影片。需在本地准备原 `W.wav`、原混音/对白分轨和母带设置，输出目录必须没有同名产物。先用 `--prepare-only` 只生成替换计划，再去掉该选项重建音轨；最后 FFmpeg 复制原视频流、替换音轨即可：

```bash
python3 tools/replace_wake.py . /path/to/new-audio --wake-pcm /path/to/authorized-wake.pcm --prepare-only
python3 tools/replace_wake.py . /path/to/new-audio --wake-pcm /path/to/authorized-wake.pcm
ffmpeg -i /path/to/original-subtitled.mp4 -i /path/to/new-audio/master.wav -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac -b:a 256k -movflags +faststart /path/to/new-film.mp4
```

音源、完整分轨与配音服务产物不随公开源码发布。重建后的成片须重新核对哈希/音量；AAC 编码器不同不承诺输出字节一致。
