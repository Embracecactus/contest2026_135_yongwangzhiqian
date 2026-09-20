# 傻妞 AIDK 637：签名全镜像构建与实机功能验收摘要

- 日期：2026-09-20（Asia/Shanghai）。
- 记录与实现：CodeBuddy（构建、发布、文档）；烧录操作与功能确认：用户（owner）。
- 固件身份：`18.6.401+637`，board `aidk_ai_toy`，profile
  `app__openvela_ap`，artifact id `shaniu_aidk_v18_6_401_637_full`，
  security counter 与 rollback floor 均为 637。
- 产物类别：由**同板** readback 基线物化的完整烧录镜像（8 MiB operator）
  与 `.bkpack` 签名包。它不是通用首烧包，适用边界见末节。
- 后续：638 以同一基线在干净工作树上重建，CP/AP raw 与本文逐字节相同、仅
  计数器与签名变化，见
  [638 验收摘要](2026-09-20-shaniu-638-full-image.md)。

本摘要按“构建 / 包自检 / 传输 / 启动 / 功能 / App OTA / 公开发布”分层陈述；
没有采集到的字段写作“未记录”，不补造，也不因此要求重测全部功能。

## 包身份

| 项 | 值 |
| --- | --- |
| operator 镜像名 | `shaniu-bk7258-aidk_ai_toy-app__openvela_ap-v18.6.401+637-bshaniu_aidk_v18_6_401_637_full-full.bin` |
| operator 大小 / SHA256 | 8,388,608 B / `af2d74da4462d261abe350628d715044bd56a98eef3e9543b6b3529a5fed41f7` |
| `.bkpack` 大小 / SHA256 | 7,980,187 B / `1819d794a70166500840ca3add32fff624f8d59f4aa636364ab3b43b497cc5b9` |
| `release.json` SHA256 | `857324bd36e8d56e41753b5526920241e88044e80c92334d2bee4ca676246571` |
| build manifest SHA256 | `4d24fea616d903358636b574d44aebb811553fd9304ed0e2863fef82af0d07cb` |
| accepted-base 证据（435 B）SHA256 | `b713989ed04c664f486e5f88e00b699d179c42f3b9607718bf38ee47499da91f` |
| 分区布局 CSV | `boards/bk7258/common/partitions/bk7258/bk7258_ab_fixed_block_full_release.csv`，SHA256 `559f52beaec8a54eb3baa811dc08873e092abb51e96175573d268e325b6eaa09`（layout identity `bk7258-559f52beaec8a54e`） |
| 发布策略 SHA256 | `c72b031e99f3bafc1cb4f2c5df33203ee767dac94503f4fe73039582e8fbaf28`（`factory_mode: provision-required`） |
| accepted base SHA256 / 大小 | `a5046101af734eec6e39ea191120d04b3c60b7e2658aec4d1dd468125e698aa3` / 8,388,608 B（capture 方式 `same-device-partition-relocation`，设备标识脱敏为 `REDACTED-SAME-BOARD`） |
| 信任公开指纹 | BL1 `58e384ae78f88ef7f2b183dc254b7798538c663f285c160db1b743e873475dee`；MCUboot `4979ece7072284b2582bb9358198fa21baa415d9340d32779ecb0ba9a48dd984` |

物化范围：Flash `0x0..0x800000` 整体由一个 operator 镜像覆盖，写入集为
`boot(0x0)/cp(0x11000)/ap(0x143000)/pair(0x352000)/manifest_a/manifest_b/
bl2_a/bl2_b/persistent_data(0x6E8000)`；其中 `persistent_data` 来自同板
accepted base，`easyflash`/`sys_rf`/`sys_net` 按发布策略保持 `device-unique`。

## 构建输入

| 项 | 值 |
| --- | --- |
| 构建时工作树 HEAD | `26540056`（合入后的同一内容提交为 `daacdc75`） |
| 构建时额外未提交内容 | 唤醒应答资产 `app/bk7258/assets/wake_reply.pcm`（31,208 B，SHA256 `772a8aa9841a96bf96acdce733e4e6a67c2b46968333edb51fedc7511e3cc42f`）与 AIDK AP defconfig 中的 `CONFIG_BK7258_AIDK_WAKE_REPLY_PCM=y`（合入后的提交为 `019a449e`） |
| 输入树摘要 | `input_count 565`，`input_tree_sha256 169152ae81df2129128c1c2d68b57da4a484b8c1dfa9a868a2fed843cd70c36a`，`dirty=true` |
| 依赖提交 | NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`（dirty，447 输入）；apps `550cd3ba60a03f8ebf9ac7b72f6eed6aea3bedbe`（dirty，33 输入） |
| SDK profile 树 | `ap-aidk` `f7de532a4433d5cef39e202fb63d08496a75805e4a77ba245da9d9a73fc6f633`；`cp-aidk` `d62004bf57db326149324184cf3c818914d77f46c66ad5ef8d053b67569c68d0` |
| 角色构建标识 | AP `bk7258-role-d51a69e651aa5b94`（resolved config `275d309d…`，seed defconfig `3f251950…`）；CP `bk7258-role-abc5b4b439bd2eff`（resolved config `58dbe343…`，seed defconfig `137d7a33…`） |
| raw 镜像 | CP 1,107,276 B `b4e35e14f088ee939b0f60ed9be2499cd6b0e3ea6bf467302b47a713a1fa80b0`；AP 1,604,784 B `299212b2539de8333a91d9bb4ef42441ebe255b7f4844ed6be972058902a8165`；boot 65,376 B `d5fd05779b413eb0b6b3a09efdfafba9b2b10b6ccc4fbe0efa6deffa01285c79`；BL2 13,620 B `df06344357628b2d3abe01be0d6a5dd8d8efdb45dd0ef007985dc13af5546965` |
| 工具链归档 SHA256 | `97dbb4f019ad1650b732faffcc881689cedc14e2b7ee863d390e0a41ef16c9a3` |

## 分层结论

| 层 | 状态 | 依据 |
| --- | --- | --- |
| 源码构建 | 通过 | build manifest 与 CP/AP resolved config；本次 AP 角色配置发生变化（启用唤醒应答资产） |
| 签名与包自检 | 通过 | `release full` 一步完成签名、布局、回滚下限与 overlay 校验并写出 `release.json`；哈希如上表 |
| 传输 | 由 owner 手动烧录，本机未记录 | 命令行形态见 `tools/bk7258/README.md`；本机工具留存的 transport 记录属于 `18.6.400+636` 且以 `Writing Flash Fail` 结束，不能计入 637 |
| 启动 | owner 回贴串口日志 | `BK7258 FINALINIT PASS`、`AIDK DEFERRED DONE failures=0`、`runtime skill installed`、`BKDISPLAY RENDER PASS`、MFRC522 probe `ret=0`；该日志没有版本行，绑定依据是 637 路径交接后（19:39）的同一轮回贴（19:43） |
| 功能 | owner 确认 | 认领 → 建立连接 → 调整设置 → 本地唤醒 → 应答“我在” → 完整对话全链路通过（19:46 用户报告） |
| App OTA | 未测（不属本轮） | 637 没有 App OTA 记录；OTA 证据仍引用 `18.6.398+634` |
| 公开发布 | 未发布 | 没有 Release 上传、没有官网提交；比赛材料 PDF/DOCX/ZIP 附件未按 637 重新生成 |

## 证据位置

- 本机发布目录 `/tmp/bk7258-hil-20260920/aidk-release-full-637/`：`release.json`、
  `evidence/build-manifest.json`、`evidence/accepted-base.json`、`flash/`、`package/`。
- 构建与发布命令、串口回贴上下文：本机 CodeBuddy 会话记录，不随仓库公开。
- 原始串口文件：未落盘（owner 回贴文本），因此本摘要不提供原始日志哈希。

## 边界与未闭合项

- **同板恢复包**：该 operator 镜像由同板 readback 基线物化，含设备绑定
  `persistent_data`。跨板烧录会把设备绑定数据复制到另一台设备，因此它不得被
  当作通用首烧包或跨板交付物；原设备恢复以外的用途需要另外验证的身份初始化
  （`factory-init`/认领）路径。
- `startup_migration.status` 为 `unknown`，`unconditional_start_allowed=false`；
  整片覆盖不授权布局迁移。
- 本轮未做：App OTA、NFC 断线注入与恢复、功耗与长期稳定性、多人唤醒泛化。
- 唤醒应答 PCM 为比赛期间经 owner 授权入库的私有资产，赛后删除文件并回退
  defconfig，见 `docs/platforms/bk7258/shaniu-master-plan.md`。
- 本摘要不修改、不覆盖 634/635 的历史记录。
