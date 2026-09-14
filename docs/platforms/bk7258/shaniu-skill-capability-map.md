# 当前 goal 的可复用能力映射

本次集中沉淀日期：2026-09-14。覆盖当前可访问对话、当前工作树、
`f49508dff4070cd5629608191b8361ccca255a46` 的相关源码，以及
工作区 `out/shaniu-p0-20260913/` 下已知训练、构建、烧录、手机及串口证据。
没有扫描其他会话或私人目录，不声称回放了全部历史。
这份文件是能力去向与项目适配索引，不替换 Master Plan。

通用正文只维护一份：新增能力在宿主用户目录 `~/.agents/skills/`；
已有硬件能力继续维护项目 `tools/` 权威来源，沿用现有用户目录符号链接。
不打包完整会话、私有设备备份、凭据或原始大日志进入 Skill。

## 原流程 → 最终去向

| 原流程与现有证据 | 去向 | 处理 |
| --- | --- | --- |
| Agent provider/turn 接入、调用者迁移、单会话/单音频 owner、chip/board 边界、按依赖删旧实现 | `dependency-framework-migration` | 新建独立 Skill；只处理实施，未决需求仍复用现有架构澄清能力 |
| 官方依赖复制/补丁检查/构建源替换/重链：`frameworks/cmake/{agent_provider,media_trigger}.cmake` | 同上 | 纳入构建接入步骤，项目补丁清单留在 `frameworks/` |
| 非阻塞短写、EAGAIN 后 poll、统一 deadline、EOF/drain、格式宽度、取消 quiescence、Trigger 分块 | 上述 Skill 的 `references/openvela-media-streams.md` | 同一框架迁移任务的音频故障分支，不再新建通用音频框架 |
| Android 跨页面会话、认证代次、配网绝对期限/控制活动期限、串行读写、快照新鲜度 | `android-device-session` | 新建独立实施 Skill；`DeviceControlSession`、`ProvisionGattSession` 为源码依据 |
| 音量 guard → STATUS → preferences → 播放、ACK 后回读、busy 原因 | 同上 | 属于设备控制协议与 UI 的完整设置链，不只修改启用条件 |
| 公开资产许可、混合语言 token/发音核验、真实 source/voice split、有限 CPU 训练、量化、运行时加载 | `edge-wakeword-training` | 新建独立 Skill；细节在 `references/data-and-runtime.md` |
| 分类/连续策略/物理回放/真人证据分离，标签与量化/算子/arena 对应 | 同上 | 模型交付的一部分；不将合成结果升级为真人验证 |
| APK 覆盖安装、真实 BLE、20 次切页、旧期限边界、文件选择器、音量回读、App OTA/取消 | `android-companion-hil` | 新建独立实机验收 Skill；与 Android 会话代码整改分开 |
| 指定手机扬声器、live UART 就绪门控、真实唤醒→ASR→TTS、当次摄像头、取消后下一轮 | `voice-device-hil` | 新建独立物理联调 Skill；不混入 Android 管理或模型训练 |
| AP/CP 网络 owner、临时热点、权限拒绝后正常 UI、断网相位、原配置恢复、命令副作用 | 上述 Skill 的 `references/network-interruption.md` | 条件流程/失败分支；语音请求中断恢复尚未验收通过，未封装自动化脚本 |
| 运行中 raw 缓冲、ready/live echo、PowerShell 命令数组与字面量换行、时钟关联、端口释放 | `windows-hardware-debug` | 更新权威 `tools/windows-hardware-debug/` 的参考文件和入口；保留显式调用策略 |
| 板型/端口唯一识别、BK Loader 原子接管、全量写入、签名/布局/保护范围、失败恢复 | `bk7258-hil-download` | 复用 `tools/bk7258-hil-download/`，已有能力不重复创建；AIDK 复位契约留板级适配 |
| 源码/配置产物/实板三种身份、source manifest、EOL 过滤、干净复现、签名 floor、同设备备份、产物与证据对应 | `embedded-release-verification` | 新建独立交付 Skill；调用既有 build/package/release/部署工具，不重新实现它们 |
| 官方/fork 归属、树/补丁等价、线性分支、脏树精确暂存、远端 SHA/compare、PR 内容 | `fork-change-publication` | 新建独立 Skill；提交、推送、PR、合并分别依当前授权处理 |
| 本项目中文 PR、官方/fork 身份与基线 | 既有 `.agents/skills/publish-pr-cn/` | 更新为项目适配；通用步骤引用用户级 `fork-change-publication`，删除重复正文及错误的固定 `origin` 假设 |
| 比赛开发记录：已授权单 session 公共消息/工具结果、时间与 call ID 保留、脱敏、schema/manifest 校验 | 既有 `contest-log-collector` | 复用工作区 `.claude/skills/contest-log-collector/`；不另建日志 Skill 或平台，也不重跑当前延期的日志导出 |
| 原生记录格式转换及额外私有路径过滤 | 项目 `contest-log-export/export-provenance.json` 和原 collector | 已有 5924 公共事件导出与分类/过滤记录；本 session 适配尚非稳定跨版本解析器，保留项目证据，不把临时格式映射包装成通用实现 |
| Skill 规范/权威来源/目录发现/静态检查 | 现有 `skill-creator`、`openai-docs` | 复用；不再创建 Skill 管理或安装平台 |

## 项目适配输入（不能复制进通用正文）

| 输入 | 本项目权威来源或当前记录 |
| --- | --- |
| 官方与 fork、当前分支/HEAD、待合并范围 | 当前 Git remote/refs；本轮发布分支 `feat/shaniu-official-media-511`，每次发布重新核对 |
| 目标、端口、容量、布局、复位/签名/备份规则 | `boards/bk7258/aidk_ai_toy/AUTOMATION.md`、`tools/bk7258-hil-download/references/board-profiles.json` 和既有 build-flash-debug SOP；当前 AIDK AI Toy / COM8 不能当别的项目默认值 |
| 公共执行入口与产物参数 | `python3 tools/bk7258/bk7258.py --help` 及对应子命令；不假设存在 `log` 子命令 |
| 当前官方依赖与扩展 | 实际 manifest checkout、目标配置、`frameworks/cmake/` 与 `frameworks/patches/`；README 中名称不是接入证据 |
| Android | `android/shaniu-companion/`，手机 serial/package/安装版本从当前 ADB 查询；包内设备身份由现有认领保留 |
| 模型/训练 | `app/bk7258/models/nihao_openvela.{tflite,metadata.json}`、`tools/bk7258/_lib/voice_kws.py`；CLI `voice kws audit/train/evaluate`，从当前 `--help` 获取参数 |
| 产品词与实际发音 | 产品“你好，open-vela”；open /oʊpən/，Vela /ˈviːlə/“维拉”；候选合成文本 `你好，open vila` 是发音适配，不更换产品词 |
| 私有资产和服务 | 从已有授权配置读取，不抄入 Skill；备份/签名/热点秘密不进入这份映射或公开 PR |
| 现有进度与验收 | `docs/platforms/bk7258/shaniu-master-plan.md`、README 和以下已知证据；不另建路线图 |

## 证据与未成熟能力

- v46 模型训练与独立合成评估：`kws-tts-train/candidate-v46-viila-room-f4/`；
  模型约 23 KiB，实板 arena 已用 40692 字节。具体指标及输入量化以 metadata
  和对应报告为准。扩展评估包含原集合，不能相加成独立样本数。
- `voice511-diag-team-sources-manifest.json`、
  `shaniu-companion-0.5.5-sources-manifest.json`、
  `voice511-source-reproduce-clean-build.log`、`voice511-recovery-boot-floor-build.log`
  为版本对应/复现方法来源；旧 source_commit 标签不能代替实际输入哈希比对。
- `voice511-hil-full-quiet-r1/result.json`、`voice511-preservation-recheck.json`
  为原设备全量烧录与数据保全证据，不在本次 Skill 验证中重复刷机。
- `phone511-final-p0-regression/`、`voice511-final-physical-wake-image-r1/`、
  `voice511-after-image-physical-dialogue-r1/`、`voice511-full-playing-app-cancel-r1/`、
  `voice511-full-after-cancel-wake-image-r1/` 支持相应手机/物理链。
- `voice511-physical-wifi-outage-r1/` 已观察热点断开后自动关联/DHCP；
  `voice511-active-request-wifi-outage-r2/` 因未获得正式唤醒，未执行计划中的
  请求中途断网。不能把闲置断网恢复写成语音请求恢复通过。
- `voice511-original-network-reboot-restore/`、
  `voice511-after-wifi-restored-version/`、`phone511-wifi-recovery/restoration.json`
  支持重启回到原网络、仍为 511、热点恢复。运行时 disconnect 曾返回 busy；
  不将它固化为稳定恢复方法，不留存热点秘密。
- 真人正例泛化仍未验证；两次有效物理回放未形成正式唤醒，另一次迟到事件
  来源不明。已写成评估/故障分支，不能沉淀为“通用唤醒已完成”的方法。
- Media 格式失败和 MIC busy 曾在同一运行日志出现；后续成功不等于低频故障
  已消除。保留定位分支，后续沿原 goal 修复，不创建平行诊断平台。
- 固定坐标/IME 输入绕行不是稳定跨手机自动化，保留参数检查和失败经验。
  不新增万能 UI 执行器、探针或验收脚本。
- 用户此前要求开发日志统一收口；本次复用已有 collector 能力与 provenance，
  不重新导出、不补造历史记录，不新增日志工具。原生格式映射只包含允许公开的
  user/assistant 文本和工具 use/result；排除内部推理、system/developer、内部
  注入上下文及代理通信。归档单一目标会话，不使用全会话列举作为默认入口。
  默认 token 正则不足以覆盖私钥路径和无关手机活动，项目追加过滤仍需保留。
  上述收口时暂缓的换词/换模型 App UI，已被后续用户的三词、自定义模型和云模型授权纳入当前范围；以 Master Plan 当前段为准。

## 验证口径

7 个新 Skill 已实际安装到 `~/.agents/skills/`；现有 `quick_validate.py`
全部通过，内部相对引用存在，安装文件与草稿逐字节一致，无当前项目名、
个人绝对路径、串口或临时版本硬编码，无空目录。
更新的 `windows-hardware-debug` 与复用的 `bk7258-hil-download` 元数据通过；
项目 PR 适配在去重后另做同一检查。

宿主为 Codex CLI 0.154.0；使用现有 `codex debug prompt-input` 本地渲染，
7 个新名称和 `r1` 用户技能目录映射均已出现。该命令未调用模型或执行项目任务；
最初沙箱只读失败后通过执行环境审批运行，没有修改全局配置或启动新服务。
用户级发现路径也与 [官方规范](https://learn.chatgpt.com/docs/build-skills) 一致。

| Skill | 应触发的实际调用示例 | 不应触发的近邻请求 | 检查结果 |
| --- | --- | --- | --- |
| `dependency-framework-migration` | `$dependency-framework-migration 把已有产品音频链迁入当前官方框架并切换调用者` | 只讨论架构，不实施 | 独立静态演练通过；先取真实启用后端，不假装迁移完成 |
| `android-device-session` | `$android-device-session 修复切页导致设备连接状态丢失` | 仅验收一次已有真机连接 | 独立静态演练通过；协议/生命周期先于 UI 字段搬移 |
| `edge-wakeword-training` | `$edge-wakeword-training 用已许可语音训练匹配现有 TFLM 的唤醒模型` | 云端文本匹配、声纹识别 | 独立静态演练通过；补明最终留出集先冻结 |
| `android-companion-hil` | `$android-companion-hil 覆盖安装候选 APK 并验 BLE、音量回读和 App OTA` | 重构 GATT session 代码 | 独立静态演练通过；补明签名/版本兼容，禁止卸载清数据绕过 |
| `voice-device-hil` | `$voice-device-hil 用指定扬声器验证物理唤醒、看图和下一轮恢复` | 软件注入冒充唤醒验收 | 独立静态演练通过；没有 idle/正式唤醒门就不执行依赖动作 |
| `embedded-release-verification` | `$embedded-release-verification 核对源码清单、固件和设备的交付对应关系` | 泛用 UART 刷机或新建发布平台 | 独立静态演练通过；无匹配下载 Skill 时使用原项目 SOP |
| `fork-change-publication` | `$fork-change-publication 为已审查的 fork 分支准备 PR 内容` | 只要求代码评审 | 独立静态演练通过；授权动作和远端角色分别核对 |
| `windows-hardware-debug` | 显式指定该 Skill，核对目标后收集 capture-only 串口证据 | 泛泛要求嵌入式调试 | 独立静态演练通过，保留显式调用策略 |
| `bk7258-hil-download` | 明确授权且已核验 BK7258 profile/同设备产物的下载 | 未知板型、OTP/eFuse 操作 | 原有描述/规则人工静态检查；本次没有重新刷机 |
| `publish-pr-cn` | 本项目已推送分支需要中文 PR 交付 | 其他仓库的 PR | 项目适配保留，通用执行委托独立 Skill |
| `contest-log-collector` | 已授权的指定比赛 session 日志归集 | 无关私人会话或全机历史扫描 | 复用原工具/既有 provenance；当前 Codex 元数据校验拒绝原 description 中的尖括号，未当作新装 Codex Skill；本次没有导出或上传 |

以上替代项目名称、路径及 Linux/Android 输入的演练只验证静态可执行性，
不是跨项目实测。没有为 Skill 新增脚本、测试工程或重复构建/烧录。
既有 collector 的元数据问题保留为上游适配限制；本次不修改官方依赖仓、
全局自动归集配置或生成副本，其既有脚本复用和本 session 导出证据分别成立。
主线接续已核对当前模型哈希与全部 41 项前处理源码哈希一致，并通过现有
`voice kws audit` 读取当前 414 条留出评估资产，仍为 candidate/accepted=false。
这是本项目的实际只读工具调用，不代表整个新 Skill 已端到端实测成功。
