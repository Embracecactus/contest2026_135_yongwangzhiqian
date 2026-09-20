# 傻妞 AIDK 638：签名全镜像构建与语音全链路实机验收

- 日期：2026-09-20（Asia/Shanghai）。
- 构建、签名、发布与本记录：Codex；烧录与实机验证：用户（owner）。
- 固件身份：`18.6.401+638`，board `aidk_ai_toy`，profile
  `app__openvela_ap`，artifact id `shaniu_aidk_v18_6_401_638_full`，
  security counter 与 rollback floor 均为 638。
- 产物类别：由**同板** readback 基线物化的完整烧录镜像（8 MiB operator）
  与 `.bkpack` 签名包；适用边界见末节。

本记录按“构建 / 包自检 / 传输 / 启动 / 功能 / App OTA / 公开发布”分层陈述；
没有采集到的字段写作“未记录”，不补造。

## 包身份

| 项 | 值 |
| --- | --- |
| operator 镜像名 | `shaniu-bk7258-aidk_ai_toy-app__openvela_ap-v18.6.401+638-bshaniu_aidk_v18_6_401_638_full-full.bin` |
| operator 大小 / SHA256 | 8,388,608 B / `33c387c1d57841fc4391c553f32ff29e55d3552ba48009b02456af52a9821cfc` |
| `.bkpack` 大小 / SHA256 | 7,980,186 B / `2a3b6c387f43e2a9c8a143bfcbeb6a876c73e68eab8d730e0bf88160a390ddfe` |
| `release.json` SHA256 | `582ae0aa6de8d093cd7a0add56aec5812318440ab8f73962a7fba96dcd7271d5` |
| build manifest SHA256 | `4b8d55e7d4416757d6d1a89414da77ab040e3581790296139422c92d23877a62` |
| accepted-base 证据（435 B）SHA256 | `b713989ed04c664f486e5f88e00b699d179c42f3b9607718bf38ee47499da91f` |
| 分区布局 CSV | `boards/bk7258/common/partitions/bk7258/bk7258_ab_fixed_block_full_release.csv`，SHA256 `559f52beaec8a54eb3baa811dc08873e092abb51e96175573d268e325b6eaa09`（layout identity `bk7258-559f52beaec8a54e`） |
| 发布策略 SHA256 | `c72b031e99f3bafc1cb4f2c5df33203ee767dac94503f4fe73039582e8fbaf28`（`factory_mode: provision-required`） |
| accepted base SHA256 / 大小 | `a5046101af734eec6e39ea191120d04b3c60b7e2658aec4d1dd468125e698aa3` / 8,388,608 B（与 637 同一份同板基线；设备标识脱敏为 `REDACTED-SAME-BOARD`） |
| 信任公开指纹 | BL1 `58e384ae78f88ef7f2b183dc254b7798538c663f285c160db1b743e873475dee`；MCUboot `4979ece7072284b2582bb9358198fa21baa415d9340d32779ecb0ba9a48dd984`（与 637 同一维护身份，未轮换） |

## 构建输入

本次为工作树干净的正式构建（`dirty=false`），与 637 记录的 dirty 输入不同。

| 项 | 值 |
| --- | --- |
| 源码提交 | `dc06613d13a6369253baefdba9513ec4e32b90e1`（本仓 `dev-ai-contest-2026`，工作树干净） |
| 输入树摘要 | `input_count 565`，`input_tree_sha256 d97330e7c392f5bfc87dfa064530caa640f61115653d259825be457f21a1e01b`，`dirty=false` |
| 依赖提交 | NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`；apps `550cd3ba60a03f8ebf9ac7b72f6eed6aea3bedbe` |
| 角色构建标识 | AP `bk7258-role-d51a69e651aa5b94`（resolved config `275d309daf7e…`）；CP `bk7258-role-abc5b4b439bd2eff`（resolved config `58dbe343048e…`） |
| raw 镜像 | boot 65,376 B `84735872a81d5e496c959bc3624b70694c8fdfd7709c6b3f37e96308661c074b`；BL2 13,620 B `e4168fa1e3be8c8bc33d93c8a0536138dcf1fa7b864e2e2ffdaa2a6079acd255`；CP 1,107,276 B `b4e35e14f088ee939b0f60ed9be2499cd6b0e3ea6bf467302b47a713a1fa80b0`；AP 1,604,784 B `299212b2539de8333a91d9bb4ef42441ebe255b7f4844ed6be972058902a8165` |
| 工具链归档 SHA256 | `97dbb4f019ad1650b732faffcc881689cedc14e2b7ee863d390e0a41ef16c9a3` |
| 真实 configure 检查 | 分层门禁 PASS（500 源 / 252 Kconfig / 2 例外）；Agent 源集合检查在两个角色各打印一次（`Agent framework: filtered official sources, 28 kept`） |

### 与 637 的差异（证明只换了代数与签名）

CP/AP raw 哈希与 637 **逐字节相同**。两张 operator 全片逐字节对比差异 **659 B**，
分布如下（均为各签名镜像的 MCUboot 头/计数器与 ECDSA 签名、manifest 与 BL1/BL2
签名区）：

| 区域 | 差异字节 |
| --- | ---: |
| BL1 | 3 |
| CP 槽（头/计数器 + 签名 TLV） | 112 |
| AP 槽（头/计数器 + 签名 TLV） | 115 |
| pair 容器 | 227 |
| manifest A/B | 194 |
| BL2 A/B | 8 |
| `persistent_data` | 0 |
| 不可写尾部 `0x7E8000..0x800000` | 0 |

## 实机验收（用户回贴串口，13:02:57–13:03:54）

烧录与 App 认领/配置由用户完成（本机没有该镜像的传输日志）。回贴日志显示：

**唤醒与应答“我在”**

```text
BKVOICE wake reply result=0
[audio_pb] closing (31208 bytes written)
```

31,208 B 正是仓库内 `app/bk7258/assets/wake_reply.pcm` 的大小，即唤醒应答录音
完整播放；`result=0` 表示应答动作成功。

**一轮完整对话（不需要二次唤醒的追问）**

```text
BKVOICE HTTP request=chat/completions status=200 ret=0 bytes=466      ← ASR 请求成功
AGENT ASR backend=mimo mode=batch ret=0
[llm] OpenAI API with tools (model: mimo-v2.5, 4868 bytes)
AGENT LLM transport=verified-cloud status=200 ret=0 bytes=460
[voice] speak: "今天是 2026年9月20日，星期天。☀️" (47 bytes)
BKVOICE TTS stream ret=0 pcm_bytes=207360 stopped=1 done=1
AGENT TTS backend=mimo mode=audio-stream/full-text source_rate=24000 output_rate=16000 ret=0 bytes=138240
[voice] speak done: total 5732ms (play wait 463ms)
[voice] request=4 complete=0
BKVOICE turn complete=0 interaction=active next=capture result=0      ← 会话保持，直接续收音
```

**静音退出回待机**

```text
[voice] recording thread exit: 249 chunks, 160000 read, 0 sent
[voice] request=5 complete=-61
BKVOICE official Trigger rearm ret=0
BKVOICE turn complete=-61 interaction=exit next=wake result=0
```

**其它同窗口信号**：KWS 持续滚动（`KWS candidate hits=…`/`candidate released`/
`windows=… scores=…`），`BKVOICE Trigger model stream reset`；
音量键链路 `BKKEYS volume steps=… observed=60/67/73/87/100 result=0` 对应
`MusicVolumeDomain` 9→15 级设置成功。本窗口日志未包含启动段与显示/摄像头动作，
不作相应结论。

## 本窗口如实记录的异常

- **一次 ASR 请求失败并在同一会话内恢复**：`BKVOICE HTTP request=chat/completions
  status=0 ret=-5 bytes=0 body_bytes=165760`、`AGENT ASR backend=mimo mode=batch
  ret=-5`、`[voice] request=2 complete=-5`；设备保持 `interaction=active` 并重新
  收音（`request=3 complete=-61` 为静音超时），随后 `request=4 complete=0` 成功。
  本轮未定位该瞬时失败根因，也不把它写成已消除。
- **既有噪声仍在**：`input[0] write to mix failed, ret:-22.`（与 626 记录同类，
  见 Master Plan“不能把闭环成功写成这些现象已消除”）、录音关闭路径的
  `media_recorder_encode_frame failed: -541478725 / -32`、
  `[llm_router] No available backend` 警告、播放结束时的
  `audio play/resume failed: End of file`。本次不声称这些现象已修复。

## 边界与未闭合项

- **同板恢复包**：由同一台 AIDK 的 accepted base 物化，含设备绑定
  `persistent_data`；跨板烧录会复制设备绑定状态。它不是通用首烧包，
  通用 factory-init/身份初始化路径仍未验证。
- 未测：App OTA（仍引用 `18.6.398+634`）、NFC 断线注入、功耗与长期稳定性、
  多人唤醒泛化、整片下载的本机传输日志。
- 唤醒应答 PCM 为比赛期间经 owner 授权入库的私有资产，赛后删除文件并回退
  AIDK defconfig。
- 本记录不修改 634/635/637 的历史结论；637 记录见
  [637 验收摘要](2026-09-20-shaniu-637-full-image.md)。

## 证据位置

- 本机发布目录 `/tmp/bk7258-hil-20260920/aidk-release-full-638/`（`release.json`、
  `evidence/`、`flash/`、`package/`）。
- 构建日志 `/tmp/bk7258-build-638.log`（含门禁与 Agent 源集合检查输出）。
- 串口证据：用户回贴文本，未落盘为原始文件，因此不提供原始日志哈希。
