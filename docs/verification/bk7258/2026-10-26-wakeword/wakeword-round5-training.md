# 第五轮：真人语音预训练与目标词迁移

> 证据可得性（2026-11-04 核验）：本文引用的工作区 `out/` 原始产物不可用，仅保留本文记录的摘要/哈希；原链接已改为路径文本，结论边界不变。见[验收原始证据留存审计](../2026-11-04-acceptance-evidence-retention.md)。

2026-10-26。本轮完成了公开真人语料读取、两个真人辅助训练基线、五个目标任务消融、INT8 导出及独立真实负例评估。研究候选选为 **H3：真人关键词编码器预训练 + 新目标分类头 + 分阶段解冻**。没有扩大网络、修改固件前端、降低默认阈值或减少确认次数。

同为 60% 阈值、300 ms 判定、连续两次确认：A 与 H3 在新来源合成快语速开发集上为 **0/40 → 31/40**；在新说话人真实负例保留集上为 **17 → 1 次 / 4.1880 小时**。这证明了本轮训练方法的研究价值，尚不构成真人目标词或 BK7258 实机验收。旧正常语速和近音负例仍有退化，因此 **A、默认模型和固件保留不变**。

全部实验根目录记为 `H=out/wakeword-round5-20261026`，相对于 open-vela 工作区。机器可读结果见 wakeword-round5-results.json（本地未发布：`wakeword-round5-results.json`）。模型、完整逐帧分数、训练日志和来源清单均保留在 H；不包含对外上传或发布操作。

（图：验证集工作点和新真实负例测试；`out/wakeword-round5-20261026/round5-comparison.svg`）

## 1. 冻结对象与实际部署契约

`H/baseline-freeze.json` 冻结 A、E、F4b、G0、G1、G2 的模型/元数据、已有报告、默认模型和前端/检测源码，共 47 项。结束时只有本轮修改的两个 Python 训练模块发生变化；原有模型、C 前端、触发逻辑及应用入口哈希保持一致。工作树原有未提交改动已保存为本轮前状态，未用 GitHub 版本覆盖。

| 对象 | 本轮核对值 |
|---|---|
| A TFLite SHA-256 | `18bfecacf346b23e10d8a324804b9caa30289c5a6ffe53eb6067b0e7259550aa` |
| A 原 WKM2 SHA-256 | `3975c5e7afdc9c4c55d76e264116f76a8c123508b47dae0b2af9400a653ae431` |
| 前端 | 当前 C `bkvoice-microfrontend-pcan-v2`；16 kHz、单声道 PCM16；30 ms 窗 / 20 ms 帧移、40 bins |
| 模型 | 原有 32 通道 causal DS-TCN；k9、dilation 1/2/4/8/16；没有新增算子或输入尺寸 |
| 目标三分类 | `silence / unknown / nihao_openvela` |
| 默认触发 | 最新一帧分数每 300 ms 判定；阈值 0.60、确认 2、release 0.20、cooldown 1000 ms |
| 实际文件大小 | TFLite **26,680 B**；含头/校验的 WKM2 **26,820 B**。二者不可混称 |

前端来源与张量/state/量化绑定记录在各模型 metadata；所有候选使用同一实际 C 前端。WeKWS 的 MFCC 权重没有直接导入这个前端。

## 2. 实际获取、读取和分组的真人数据

| 数据 | 取得及实际读取量 | 用途与许可 |
|---|---|---|
| MLCommons MSWC zh-CN v1.0 | 官方音频/划分/对齐包约 83.4 MB；哈希及元数据检查 10,113 个 Opus、498 个词；**实际解码 1,911 条、1,910.894 秒** | 新增 train 1,500，保留 validation 211 / test 200。全部是中文短词 `unknown`；[官方 CC-BY-4.0](https://mlcommons.org/datasets/multilingual-spoken-words/) |
| MobvoiHotwords SLR87 | 新取 6,398,136 B 标注包；复用已有 17,869,366,577 B 音频包并重新计算完整 SHA-256；实际读/解码 **25,987 条、21.7840 小时** | 真人辅助唤醒、其他唤醒词负例、普通人声负例；[官方 Apache-2.0](https://openslr.org/87/) |
| AISHELL-1 | 官方镜像固定 revision `bbe295d530192a4cd41644b711c9aecd087df653`；固定名单中 44 个包成功，**15,144 条 WAV**实际解码/哈希 | 新真实负例验证/测试；[OpenSLR Apache-2.0](https://openslr.org/33/)，[固定镜像版本](https://huggingface.co/datasets/AISHELL/AISHELL-1/tree/bbe295d530192a4cd41644b711c9aecd087df653)。本次可取镜像只有男性，不能外推全人群 |

MSWC 此版 **没有“你好”（0 条）**，不能把其他词或部分词标为完整目标。其官方切分存在 564 个说话人、393 个原始录音来源跨集合，已剔除这些冲突及 415 条缺乏对应对齐边界的项。所有词段继承原始 Common Voice 录音组；匿名 ID 只做哈希映射，不寻求真实身份。同一原录音的不同词段保持同组，绝不按音频文件随机划分。Word clip 的真实时长由解码 PCM 测量，没有把 TextGrid 所属长录音时长误计成词段时长。

Mobvoi 正样本说话人 train/dev/test 为 474/78/236，已知负样本说话人为 38/6/11，各自跨集合无已知人重叠。原始负标注中的空 speaker 字段曾在初步统计中形成一个“共享 ID”，实为未知标记，已全部排除；没有人为给每条未知录音编造说话人。校正见 `data/mobvoi/known-speaker-audit.json`。

AISHELL 预先冻结 36 个验证候选和 10 个最终测试人，排除历史已使用的 8 人；两个验证包 S0021/S0046 长度校验失败后未替换。**实际为 34 个验证人、10 个测试人**。早期 `statistics.json` 的 speaker 字段仍是计划人数，实际修正记录在 `current-audit-correction.json` 与最终 audit；源文件及失败证据没有覆盖。首个下载进程退出原因没有日志可确认；后续已确认的 mkdir 重启错误单独修复，未将两者混为同一原因。

Mozilla Common Voice 27 zh-CN、RIRS_NOISES、MUSAN 本轮检查了官方入口/许可，但未新增下载或用于本轮模型。当前已有真人数据足够完成有界实验；没有购买资源、使用非商业受限新语料或上传声音。MSWC 已是 Common Voice 派生词级语料。具体版本、原始哈希、匿名来源及许可见 H 下 `data/*/audit.json`、`sources.json`、`decoded-import.json` 和下载清单。

### 训练数据实际变化

原链路并非完全没有真人数据：原目标训练集已含真人 unknown 2,388 条、真人环境 silence 72 条，以及历史已授权的 **1 个真人来源的 38 个目标训练条目**。这些不是本轮新增或独立真人正例。

所有目标实验保持同一 1,194 条原始目标正例：1,156 合成 + 上述 38 历史真人条目。H1/H2scratch/H2/H3 另加入 **10,297 个真人负例条目**：Mobvoi 其他关键词 5,970、普通人声 2,827、MSWC 短词 1,500。因此 unknown 训练条目从 3,136 增至 13,433，其中真人为 12,685。正例来源、说话人、原始哈希及全部原评测 session 完全相同，见 `paired-data-audit.json`。

全部增强继承 train 来源。PCAN 特征提取、PCM 增益、保持音高的 tempo 增强及随机种子复用已有实现；本轮新增的是数据与跨任务编码器迁移，不是新一轮合成损失搜索。

## 3. 原六模型 0/40 的声学与时序检查

对已暴露的 40 个来源 × 4 条件实际 PCM、事件窗口、C 特征、完整 20 ms 分数进行了复核，文件在 `acoustic-diagnostics*.json`、`feature-diagnostics.json` 和逐帧 NPZ。

| 检查 | 观察与边界 |
|---|---|
| 外部截断/标注 | 160/160 PCM 非零声音均落在既有事件窗内；40 个快样本可由对应正常原始渲染 + `atempo=1.5` 逐字节复现；没有发现外部裁尾或错位。非零范围不是人工音素边界 |
| 音量与音高 | 正常/快/慢有效支撑中位数 1308.5/899.1/1771.2 ms；正常/快 RMS 3197.8/3182.4，F0 估计 167.3/170.6 Hz；削波样本 0。未见音量崩溃或重采样导致 1.5 倍升调 |
| 前端与量化 | 实际 C PCAN-v2 provenance 与 A/G0 匹配；160 条的输入 INT8 饱和比例均 0。活动特征帧中位数正常 68、快 47、慢 91 |
| 全帧峰值 | 快样本低于 .60 的数量：A 40、E 40、F4b 39、G0 36、G1 37、G2 40。A 快样本峰值中位数仅 .046875；大多数不是恰好漏采一个高峰 |
| 浮点检查 | A 浮点快样本也为 0/40 峰值达标；G0 4/40 峰值达标但 0/40 存在实际 300 ms 连续确认对。量化不是该 0/40 的主要解释 |
| 混合发音 | 新来源为未用于训练的 Kokoro v1 音色及固定英文式 IPA。用合法本地 Whisper small、无目标文本提示检查 18 条合成录音，发现新旧 TTS 的英文部分转写均不稳定。未知专名 ASR 本身也会错，不能据此判定某个音素错误或修改正负标签 |

证据支持的解释是**短时音节模式 × 混合语言发音分布的模型泛化不足，并伴有剩余时间确认问题**；不能认定容量不足。相同容量的 H2scratch 已达到该快集 39/40，也排除了“必须扩宽才能识别”的推断。固定音色跨快/正常/慢的差异很大，不能只归因于音色。

H3 剩余 9 个快样本漏检中，**2 个窗口内峰值不足，7 个没有实际两次 300 ms 确认对**。前端/标签问题与音素层面的确切原因仍需人工听辨和新真人完整目标词样本；本轮没有用降低阈值或放宽事件窗来消掉这些失败。

## 4. 两个真实辅助任务及迁移消融

[WeKWS hi_xiaowen 工程](https://github.com/wenet-e2e/wekws/tree/6a45aeb994dd81c0969ff877a5a7c46d60ed0c86/examples/hi_xiaowen/s0) 固定在 `6a45aeb994dd81c0969ff877a5a7c46d60ed0c86`。实际调用上游 `local/prepare_data.py`、`processor.compute_mfcc/spec_aug`、`model/mdtc.py`、`loss.py`、`Executor.train/cv` 和 `compute_det.py`，不是仅阅读 README。源码路径/hash 与适配差异保存在 `WeKWS-MDTC-small/config.yaml`。

| 实验 | 实际配置/完成轮数 | 目的 |
|---|---|---|
| WeKWS-MDTC-small | 6,500 train / 900 val / 1,400 test；80 MFCC、25/10 ms、32 hidden、3×4 MDTC、两关键词 sigmoid/max-pooling；8 epochs，batch64，seed2026102608 | 验证真人关键词训练可执行。当前 Wenet 数据入口与旧 recipe 不兼容，使用薄数据适配器调用原模型/训练器；不是官方 100 epochs、batch100、30-checkpoint 平均的完整基准复现 |
| P-mobvoi-encoder | 实际 C PCAN-v2 + 原32通道 TCN；真人“你好问问”正例 4000、真人 unknown 5000、silence88；12 epochs，lr .001，最佳第7轮 | 可迁移的同前端编码器。val/test 正例400/600；训练、验证、测试正例说话人474/78/236隔离 |
| H0c-continuation | A 初始化，旧数据；6 epochs，lr .00003，最佳第6轮 | 与 H1 配对，控制继续训练收益 |
| H1-real-negatives | 同 H0c 初始化、轮数、学习率、正例，加入10,297真人负例；最佳第2轮 | 隔离新增真人负例的效果 |
| H2scratch | 新目标数据，随机初始化；12 epochs，lr .0003，最佳第12轮 | H2/H3 的同数据、同训练量、同种子随机对照 |
| H2-transfer | P 的编码层 + **全新目标分类头**；其他同 H2scratch；最佳第8轮 | 隔离真人辅助预训练效果 |
| H3-staged-transfer | 同 H2；先1轮只训练头，第2–3轮解冻最后两个时序块，第4轮起全部解冻；最佳第8轮 | 检查逐步解冻是否平衡召回和误触发 |

各实验独立配置/日志/模型均在同名目录及 `<name>-config.json`、`<name>.log`。使用已有 RTX5060 8GB，Torch 2.12.1+cu130 / TF CPU 2.15.1 / numpy1.26.4，batch64，固定 seed2026102608；实际版本见 `environment-current.json`。保存了每轮损失、实际 trainable 参数、恢复 checkpoint 与选中权重，没有把增加轮数当作收益证明。相同运行环境可复现训练过程；本轮没有重复训练声称跨机器逐位相同。

WeKWS 未训练时验证准确率 .2286，8轮后 .9600（上游 max-pooling 口径）。验证集选择的“你好问问”阈值 .94，在 test 命中 **494/600**，1 次误触发 / .7800h 非该关键词音频；“Hi Xiaowen”在扫描范围内**没有满足1 FA/h的验证工作点**，.99只是已披露的失败回退点。上游零计数显示为微小非零 floor，报告按真实次数解释。它的 MFCC、单峰 DET/50帧跳过规则不同，不能与傻妞两次确认结果直接排名。

可迁移的 P 用实际 C 连续判定，验证 **390/400**、测试 **575/600**；测试负例3次。测试原始负音频 .7800h，送测含每条前后各1s补零共1.2259h，二者分别保留；没有用补零扩充真人录音时长。此处正类始终是“你好问问”，**不是“你好，open-vela”的真人成功率**。静态最后一帧分类与连续检测分别记录，没有互相替代。

## 5. 同条件目标结果与长时真实负例

下列两个目标集此前均已暴露，只作为开发回归。正常/快/慢/低音量来自同原始合成语音，不能当作四倍独立说话人。旧开发负例1.216794h，来源开发负例 .886483h；Quiet 是数字增益，不是实测距离。

### old-development — unchanged60%/two-confirm policy

| Model | Fast | Normal | Slow | Quiet | All | Negative triggers | Outside-positive |
|---|---:|---:|---:|---:|---:|---:|---:|
| A | 21/100 | 100/100 | 97/100 | 100/100 | 318/400 | 0 | 0 |
| E | 41/100 | 100/100 | 95/100 | 100/100 | 336/400 | 0 | 0 |
| F4b | 72/100 | 100/100 | 99/100 | 100/100 | 371/400 | 0 | 1 |
| G0 | 77/100 | 100/100 | 99/100 | 100/100 | 376/400 | 0 | 0 |
| H0c-continuation | 51/100 | 100/100 | 100/100 | 100/100 | 351/400 | 0 | 4 |
| H1-real-negatives | 66/100 | 100/100 | 100/100 | 100/100 | 366/400 | 0 | 0 |
| H2scratch | 100/100 | 100/100 | 100/100 | 100/100 | 400/400 | 1 | 0 |
| H2-transfer | 100/100 | 91/100 | 100/100 | 100/100 | 391/400 | 0 | 0 |
| H3-staged-transfer | 99/100 | 96/100 | 100/100 | 98/100 | 393/400 | 0 | 0 |

### source-development — unchanged60%/two-confirm policy

| Model | Fast | Normal | Slow | Quiet | All | Negative triggers | Outside-positive |
|---|---:|---:|---:|---:|---:|---:|---:|
| A | 0/40 | 15/40 | 21/40 | 14/40 | 50/160 | 3 | 0 |
| E | 0/40 | 30/40 | 39/40 | 24/40 | 93/160 | 5 | 0 |
| F4b | 0/40 | 35/40 | 40/40 | 34/40 | 109/160 | 5 | 0 |
| G0 | 0/40 | 38/40 | 40/40 | 37/40 | 115/160 | 6 | 0 |
| H0c-continuation | 0/40 | 21/40 | 33/40 | 20/40 | 74/160 | 3 | 0 |
| H1-real-negatives | 0/40 | 28/40 | 36/40 | 23/40 | 87/160 | 4 | 0 |
| H2scratch | 39/40 | 40/40 | 39/40 | 40/40 | 158/160 | 13 | 0 |
| H2-transfer | 19/40 | 40/40 | 40/40 | 40/40 | 139/160 | 8 | 1 |
| H3-staged-transfer | 31/40 | 40/40 | 40/40 | 40/40 | 151/160 | 9 | 2 |

### New real negatives at unchanged60%

| Model | Validation count / 18.9644h | Final count / 4.1880h | Final FA/h [95% Poisson] |
|---|---:|---:|---:|
| A | 54 | 17 | 4.059 [2.365, 6.499] |
| E | 62 | 19 | 4.537 [2.731, 7.085] |
| F4b | 20 | 11 | 2.627 [1.311, 4.700] |
| G0 | 17 | 8 | 1.910 [0.825, 3.764] |
| H0c-continuation | 21 | 9 | 2.149 [0.983, 4.079] |
| H1-real-negatives | 17 | 8 | 1.910 [0.825, 3.764] |
| H2scratch | 150 | 45 | 10.745 [7.837, 14.378] |
| H2-transfer | 4 | 2 | 0.478 [0.058, 1.725] |
| H3-staged-transfer | 1 | 1 | 0.239 [0.006, 1.330] |

### Validation-selected <=1 observed FA/h workpoints

| Model | Threshold | Validation target hit | Balanced speed recall | Validation FA/h | Final FA/h |
|---|---:|---:|---:|---:|---:|
| A | 88% | 123/348 | 21.795% | 0.949 | 2.149 |
| E | 88% | 141/348 | 23.077% | 0.896 | 1.671 |
| F4b | 62% | 248/348 | 57.265% | 0.738 | 1.433 |
| G0 | 60% | 270/348 | 66.667% | 0.896 | 1.910 |
| H0c-continuation | 61% | 254/348 | 59.829% | 0.896 | 1.671 |
| H1-real-negatives | 60% | 257/348 | 61.111% | 0.896 | 1.910 |
| H2scratch | No feasible50..90% point | — | — | — | — |
| H2-transfer | 60% | 330/348 | 93.590% | 0.211 | 0.478 |
| H3-staged-transfer | 60% | 338/348 | 96.581% | 0.053 | 0.239 |


真实负例验证为 AISHELL 14.8584h、Mobvoi 普通背景3.4094h、其他唤醒词 .6380h、MSWC短词 .0586h，合计 **18.9644h原始声音 / 19.0025h送测**。最终测试为新 AISHELL 10人3379条4.1325h + 保留MSWC 162匿名说话人200条 .05555h，合计 **4.18804h原始声音 / 4.19721h送测**。最终测试没有新完整目标正例。

同一匿名说话人的原始录音按固定顺序拼成持续输入，模型和前端在相邻录音之间不重置；只为20ms帧对齐每条尾部补0..319样本。所有接缝、原样本数、原始哈希可追溯。这是**真人录音构造的连续输入，不是19小时自然连续对话或真实设备环境**。完整 manifest、source/speaker/hash 跨集合检查、时长与逐帧轨迹在 `long-evaluation-dataset/`、`long-real/`。

默认工作点验证的分来源误触发（AISHELL / 其他唤醒词 / 普通背景 / MSWC）：A **49/1/4/0**；H2scratch **150/0/0/0**；H2 **4/0/0/0**；H3 **1/0/0/0**。因此没有用大量容易的语音时长掩盖难负例结果。

**困难负例仍明显退化**：来源开发组的近音合成负例 A2、H3 8 次，部分词 A0、H3 1 次；该组真实 AISHELL 子集 A1、H3 0 次。不能把该挑战组的高频近音播放率当作日常环境的自然每小时频率，也不能因长时真人结果变好而忽略它。H3 在旧正常集4次漏检、低音量2次漏检；旧慢速100/100，但并非全面支配 A。

选型严格先完成验证，再写 `selection-before-final-test.json`，然后才打开新最终集。扫描50–90%只用于所有模型相同的研究工作点比较，固件默认仍60%。规则为先满足验证观察误触发≤1/h，再比较快/正常/慢均衡召回，平局考虑总召回及困难负例。H3在60%即入选；H2scratch整个扫描范围无可行点。全部默认阈值 replay 与真实 C 事件逐项相同，A88%与H3 60%的选中工作点又通过709个冻结PCM session、每模型264,587帧的 C 判定对照。

置信区间为计数的95%双侧精确 Poisson 区间，不代表复杂家庭环境满足泊松独立/平稳假设。H3 最终1次 /4.188h = **.239/h，区间约 [.006,1.330]/h**；仍不能统计性保证长期≤1/h。A为4.059/h，[2.365,6.499]/h。验证区间还受到选型偏差影响。最终数据一旦在下一轮参与调参，就只能作为回归集。

接缝分析保留在 `real-negative-failures.json`：A最终17次中5次距接缝≤300ms，H3的1次不在该范围；但其约5秒感受野仍跨原录音接缝。接缝邻近只是诊断，尚不能判定误触发由拼接导致。`target-failures.json` 保留全部候选逐样本峰值/确认失败分类。

## 6. 本轮可归因的训练改进与部署结果

H1 对 H0c：同正例/初始化/训练量，旧快51→66/100，来源总召回74→87/160；新长时验证误触发21→17，最终9→8。仅加入真人负例没有修复来源快语速0/40，且旧验证召回有下降，说明不能把“更多负例”当作完整解法。

H2 对 H2scratch：同数据、模型、种子、12轮和优化参数，唯一关键差别是 P 的真人辅助编码器。新长时验证误触发 **150→4**、最终 **45→2**，但来源快召回39→19/40。H3对H2再改变解冻节奏，来源快19→31/40，验证误触发4→1，最终2→1；测试1与2的差别样本不足，不能单凭此断言稳定差距。H3选型依据是先前验证，不是这个最终结果。

实现只修改 `tools/bk7258/_lib/voice_kws.py`、`voice_kws_torch.py` 和对应 Torch host 测试：增加跨任务 `--encoder-from`，严格核对源模型包/前端/INT8/state/权重哈希，复制非 classifier 层并保留目标新头；`--encoder-thaw-epochs 1 3` 控制已有优化器中的参数解冻。默认行为不变。新的两个测试实际验证头没有被复制、损坏源拒绝且不污染目标、分阶段 Adam 更新的参数集合。

训练时 P 已单独通过严格 WKM2 package 检查；最终代码又把该准入检查直接复用进 encoder 初始化函数。`transfer-admission.json` 验证了真实 P 包与新头不变，并核对最终评测函数 AST 与训练时归档版本一致。`trained-source/<name>/` 保留实际执行源码，最终加固没有伪装成当时执行的版本。

| 部署检查 | 结果 |
|---|---|
| 九组目标包 | 全部 INT8/TFLM/WKM2 加载、包校验通过；每组749帧分数误差0、reset exact |
| H3 包 | `H/selected-research/nihao_openvela_H3_round5.wkm`，26,820 B |
| H3 包 SHA-256 | `8da783b962327e3cd5f1bd45b84c6e27c3dbfd8cb58b372acf6155fbbe66234d` |
| H3 TFLite SHA-256 | `09e8401fc6dbf379133175b189ca9970a66088b75db7cbb5330984a4d733f759` |
| H3 输入/输出 | frame `[1,1,40,1]` INT8 scale2.6078431606292725/zero−128；输出`[1,3]` scale1/256/zero−128；5个现有INT8状态，7936 B |
| 原生 host arena | 21,824 B，与A一致；不是BK7258系统峰值RAM |
| 原生 host 推理 | H3 median19.918µs、p95 34.315µs，1498次；不含前端/调度，不是板端延迟 |
| 回归 | 74项KWS/frontend/mining/Torch测试通过；C core二进制退出0；Black24.10变更行检查、`git diff --check`通过 |
| 实机 | 未刷机；板端延迟、CPU、峰值RAM、丢帧、功耗及交互回归待测；本轮只改host训练，不新构建固件 |

## 7. 复现、产物与回滚

在工作区根目录执行；保持原始 H 不动，给重跑选择全新输出目录。适配器额外记录既有来源权重，不能只摘取基础CLI参数而省略它。

```bash
P=out/kws-optimize-20261024/venv/bin/python
H=out/wakeword-round5-20261026
RUN=out/wakeword-round5-reproduce
export TF_CPP_MIN_LOG_LEVEL=3 OMP_NUM_THREADS=2 PYTHONNOUSERSITE=1

# 复用已哈希冻结的数据，使用独立输出/特征缓存；已有目录不覆盖。
"$P" "$H/run_training.py" P-mobvoi-encoder "$RUN"
"$P" "$H/run_training.py" H0c-continuation "$RUN"
"$P" "$H/run_training.py" H1-real-negatives "$RUN"
"$P" "$H/run_training.py" H2scratch "$RUN"
"$P" "$H/run_training.py" H2-transfer "$RUN"
"$P" "$H/run_training.py" H3-staged-transfer "$RUN"

# 示例导出；输出路径必须尚不存在。原件和A不写入。
"$P" contest2026_135_yongwangzhiqian/tools/bk7258/bk7258.py voice kws package \
  --model "$RUN/H3-staged-transfer/model_int8.tflite" \
  --metadata "$RUN/H3-staged-transfer/metadata.json" \
  --output "$RUN/H3-round5.wkm"
```

数据重建脚本：`prepare_mobvoi.py`（安全成员提取、说话人隔离）、`data/mswc/prepare_mswc_zh.py`（官方获取/许可/词级来源）、`prepare_training.py`（辅助任务及目标配对）、`import_mswc.py`（实际解码/导入）、`research/aishell/prepare_aishell_long.py`、`prepare_long.py`。在新实验根目录保留相同相对布局和冻结注册表运行，不能在已完成目录重建清单。原始下载 URL、大小、hash 见对应 audit，完整 Mobvoi 源包复用而非重复下载。

WeKWS 复现使用 `run_wekws.py prepare` / `run_wekws.py train`。复制该脚本到新的 `out/<实验名>/`，该目录以只读链接引用现有 `data/`、`auxiliary-dataset/` 后执行，生成自己的 `WeKWS-MDTC-small/`；实际执行版本另存为 `run_wekws.executed.py`。不在原模型目录重跑覆盖。

评估通过现有 `bk7258.py voice kws evaluate`，先 validation 写 frozen policy，再 test；完整参数在 `evaluate.py`。`select_workpoint.py`只读验证，`verify_workpoint.py`验证C判定，`summarize.py`只汇总冻结结果；`native.py`执行包/原生TFLM对照。原实验输出存在即跳过/拒绝，重评应使用新输出路径且不继续称已暴露数据为新测试。图表用系统 Python 的 `PYTHONNOUSERSITE=1 /usr/bin/python3 "$H/plot_results.py"`，没有改变训练环境。

交付入口为 `H/selected-research/`、`H/results.json`、`H/round5-only.patch`、`H/verification.json`；每个失败候选、epoch日志、配置、模型hash及分数轨迹均保留。补丁仅含本轮三个源码文件和本报告/结果，不包含之前的脏工作树改动。

回滚时继续使用原 A 的路径及上述哈希即可，默认模型本轮从未替换。若要撤销本轮工具代码，先对 `round5-only.patch` 做反向检查，再仅在无后续冲突时撤销这三个文件中的对应增量；不要 `git reset/checkout` 整个文件，避免损失前几轮工作。数据和候选可留作证据，无需删除。

下一步优先按已有 真人录音/本地导入清单（本地未发布：`wakeword-human-diagnostic-plan.md`） 补完整混合发音目标及近音、部分词；诊断人与盲测人隔离。板端获得授权后先核对载入hash、前端版本与arena，再测实际声音到事件延迟/CPU/丢帧及K1/K2/K3、认领、配网、OTA原交互。当前不能将公开其他唤醒词训练成绩、合成目标成绩或PC原生TFLM通过称为这些验收已完成。
