# KWS 第三轮：开源训练技术迁移与模型消融（2026-10-26）

> 证据可得性（2026-11-04 核验）：本文引用的工作区 `out/` 原始产物不可用，仅保留本文记录的摘要/哈希；原链接已改为路径文本，结论边界不变。见[验收原始证据留存审计](../2026-11-04-acceptance-evidence-retention.md)。

本轮实际完成 **7 组训练 × 12 epoch = 84 epoch**，另从其中 5 组已保存的最佳验证 checkpoint 重导出；重导出不是新增训练。所有实验均产生全 INT8/WKM2 产物。**没有新模型全面优于 A，冻结基线 A 保留。** 没有刷机、覆盖内置模型或 A、提交或推送。新正例隔离了标准音色，但新负例复用了旧回归说话人群体；下文明确区分来源隔离与完全说话人隔离。

B 在旧回归集同时改善快语速（21→30/100）和慢速（97→100/100），但上一轮留出负例触发 2→3 次。E 改善旧快语速至 41/100、保持旧/上一轮负例 0/2 次，却使旧慢速降至 95/100。第三轮新来源测试进一步否决替换：E 总命中 129→143/160、慢速 29→40/40，但 1.214328 小时非目标音频的触发 1→4 次。不能把这些召回收益称为低误唤醒目标已经达成。

完整结果：机器对照表（本地未发布：`wakeword-round3-results.json`）、原始冻结与最终汇总（`out/wakeword-round3-20261026/comparison-final.json`）。候选名中的 `-best` 仅表示该次训练的最佳 clip-validation checkpoint，不代表通过产品选型。

## 1. 比较对象与部署契约

本轮冻结 A、上一轮 B/C 的模型、WKM2、权重、metadata、数据身份和前端输入哈希，见 baseline-freeze.json（`out/wakeword-round3-20261026/baseline-freeze.json`）。A 实际模型载荷 26680 B，完整 WKM2 **26820 B**；不能把两种大小混用。

| 对象 | SHA256 |
|---|---|
| A TFLite | `18bfecacf346b23e10d8a324804b9caa30289c5a6ffe53eb6067b0e7259550aa` |
| A WKM2 | `3975c5e7afdc9c4c55d76e264116f76a8c123508b47dae0b2af9400a653ae431` |
| 原训练 manifest 文件 | `e2f0e325c5c58d1acff5c745c71a6100e90ff6c185270c13472bf5c0b7e2c930` |
| 第三轮留出 manifest 文件 | `c65511f3287e9d3f016b3bd4bf70058be328915a59064f7c06b1f3ff684f2db8` |

归一化数据身份 `94aa01f5…` 与 manifest 文件 SHA 是不同定义；未将这种差异误判为数据改变。A 重新评测复现旧集 318/400、快 21/100、慢 97/100、纯负例 0；上一轮留出集 150/160、快 30/40、纯负例 2。B0 重训的第 12 轮全部 13 个参数张量与上一轮 B 的 last checkpoint **最大差为 0**；其第 7 轮重导出的 TFLite SHA 与上一轮 B **完全相同**。证据：checkpoint 对照（`out/wakeword-round3-20261026/checkpoint-control-equivalence.json`）。

实际链仍为 `bk7258_voice_kws.c` → `bk7258_voice_kws_frontend.c` → `bk7258_voice_kws_model.cc`：16 kHz 单声道 PCM16，480 sample/30 ms 窗、320 sample/20 ms hop，40 维 C PCAN-v2 特征；TCN32、kernel 9、dilation 1/2/4/8/16、5 个显式状态。所有候选使用同一产品 C 判断：阈值 0.60、300 ms 决策间隔、连续 2 次、释放阈值 0.20、冷却 1000 ms。没有改前端、窗口标注、阈值、冷却或应用接口。

本地训练代码已包含 `torch-cuda`、速度因子及 fresh-positive 监督，不能用远程旧文件替换。源码比较时 fork `dev-ai-contest-2026` 为 `51115602dcfea419cd9996357bb47efdb862e2bf`，上游同分支为 `2e6caad11cda8b3fa12861138e8b0b6ad712fc4f`；两者的 `voice_kws.py` 均缺少这些本地能力。仅抓取只读源码，未同步或覆盖工作区。

## 2. GitHub 源码级研究与实际迁移

检查了固定提交下的训练、数据和转换实现；没有执行下载的远程代码。以下项目均为 Apache-2.0，迁移的是算法思路，在现有三分类/TCN/Torch 流程内实现，没有导入英语模型或不兼容的 embedding、Piper 或前端算子。

| 项目与固定源码 | 核实的实现 | 本轮适配与限制 |
|---|---|---|
| microWakeWord [`data.py`](https://github.com/kahrendt/microWakeWord/blob/4665173cd35f1cff9a61e06fc427f124766c488e/microwakeword/data.py#L32) | 32–71 行时间/频率零遮罩，长度从 `[0,max)` 取样；540–569 行先按 provider 的 sampling weight 采样，再返回 penalty weight | C 用 50% 概率、2 个时间/2 个频率遮罩，长度 0–4 帧/频带；只作用有效帧，不移位、不改变标签，不遮存储填充。B 将派生速度数量与来源损失总量分开处理。 |
| microWakeWord [`train.py`](https://github.com/kahrendt/microWakeWord/blob/4665173cd35f1cff9a61e06fc427f124766c488e/microwakeword/train.py#L290) | penalty × class weight；401–451 行先满足被最小化指标，再最大化目标指标；验证/环境验证分开 | 使用纯负例时长归一化 FA/h 和速度召回共同筛选；不照搬其二分类 BCE 到 silence/unknown/wake 三分类。 |
| microWakeWord [`test.py`](https://github.com/kahrendt/microWakeWord/blob/4665173cd35f1cff9a61e06fc427f124766c488e/microwakeword/test.py#L293)、[`utils.py`](https://github.com/kahrendt/microWakeWord/blob/4665173cd35f1cff9a61e06fc427f124766c488e/microwakeword/utils.py#L148) | 导出后使用 TFLite 分数、移动平均和冷却计算 ROC；转换时克隆流式模式并复制权重；INT8 取 500 条训练代表样本 | 其冷却/平滑与本产品不同，不能拿原 FA/h 直接比较。继续使用本项目 full-sequence/step 一致性与原生 TFLM 测试，不把“能转换”当“行为一致”；不拷贝首个校准样本强设 0/26 的假设。 |
| openWakeWord [`train.py`](https://github.com/dscripka/openWakeWord/blob/368c03716d1e92591906a84949bc477f3a834455/openwakeword/train.py#L669)、[`data.py`](https://github.com/dscripka/openWakeWord/blob/368c03716d1e92591906a84949bc477f3a834455/openwakeword/data.py#L741) | Piper 合成与时长变化（音色多样性取决于生成器/配置），近音及部分词负例；`n_per_class` 明确批次构成；463–499 行筛选困难正负例再施加负类权重 | 复用合法中文训练源、按来源/音色划分，再变速和增益；执行 train-only 挖掘；D2 独立比较已标记困难负例的 ×2 损失，不把一般 unknown 全部当近音。 |
| openWakeWord [`train.py:225–324`](https://github.com/dscripka/openWakeWord/blob/368c03716d1e92591906a84949bc477f3a834455/openwakeword/train.py#L225)、[`metrics.py`](https://github.com/dscripka/openWakeWord/blob/368c03716d1e92591906a84949bc477f3a834455/openwakeword/metrics.py) | 用独立负例小时数约束 checkpoint 召回；阈值连续越界需成组计数 | 分类混淆矩阵保留在 metadata；正式选择使用 C 状态机事件与负例小时数，不能用片段分类正确率替代持续监听误唤醒。 |
| TFLM [`micro_speech` notebook](https://github.com/tensorflow/tflite-micro/blob/8f6596d99898deaac36ebd8ff8bf1b9754fd2cd1/tensorflow/lite/micro/examples/micro_speech/train/train_micro_speech_model.ipynb)、[`audio_preprocessor.py`](https://github.com/tensorflow/tflite-micro/blob/8f6596d99898deaac36ebd8ff8bf1b9754fd2cd1/tensorflow/lite/micro/examples/micro_speech/audio_preprocessor.py) | 实际 notebook 配置 `tiny_conv`，不是 DS-CNN；16 kHz、30/20 ms、49×40；Python 依次执行窗口、FFT、filterbank、noise reduction、PCAN、log、int8 | 不误称它提供当前 TCN 架构。复用本项目实际 C 前端，保持张量和量化契约；其 notebook 使用测试样本校准，本轮明确不迁移这种数据泄漏做法。 |
| TFLM [`micro_speech_test.cc`](https://github.com/tensorflow/tflite-micro/blob/8f6596d99898deaac36ebd8ff8bf1b9754fd2cd1/tensorflow/lite/micro/examples/micro_speech/micro_speech_test.cc#L51) | 顺序前端帧、整数输入/输出与量化换算；示例 resolver 仅 reshape/FC/depthwise/softmax | 当前 TCN 还需自身 Conv/Concat/StridedSlice 等实际算子；以本项目原生 resolver 真正加载每个导出包为准，不能从示例 resolver 推断支持。 |

microWakeWord 的环境数据目录本身不阻止同源切片跨集合；其 ambient 分片表达式与附近注释也不一致。本轮继续按源录音/说话人先切分，再切窗；不照抄目录组织即认为隔离成立。详细取证：microWakeWord（`out/wakeword-round3-20261026/research/microwakeword/microwakeword-4665173-source-notes.md`）、openWakeWord/TFLM（`out/wakeword-round3-20261026/research/oww-tflm/SOURCE_RESEARCH.md`）、抓取源码哈希（`out/wakeword-round3-20261026/research-source-hashes.json`）。

## 3. 快慢矛盾的新增证据

没有重复完整链路审计。对已有暴露回归集的 100 正常/100 慢速/100 快速会话，用同一实际 C 特征逐帧配对 A 与上一轮 B；保留每条样本哈希、20 ms 分数、300 ms 判定点和失效分类。

| 原因类别 | 证据与不能推出的结论 |
|---|---|
| 音素/音节压缩 | 100 个快样本均来自完整音频 atempo=1.5；既有证据显示波形支撑区间 885–1668 ms。没有音素对齐，不能把 69 个低分样本具体分摊为某音素缺失。 |
| 词首/词尾截断 | 原始生成器超长排除而不裁切，变速后完整加前后静音；未发现主动截断。保留 PCM 完成边界，不能把能量 VAD 当作人工词尾。 |
| 分数与时间判定 | A 快速仍为 69 个全窗低于 0.60、10 个未满足连续确认；上一轮 B 为 30/15。A 慢速 97 命中、3 次连续确认不足；B 慢速 92 命中、8 次确认不足，没有慢速全窗低分失败。 |
| 慢速新增 5 次失败 | B 峰值仍为 0.796875–0.941406；≥0.60 的 20 ms 帧数分别从 62/52/46/43/43 降到 46/29/24/25/27。固定分数序列，仅诊断性移动采样相位 200 ms 时 5 条均能确认；没有修改实际产品策略。直接问题是高分区间变窄/波动与决策点相交不足，不是完全认不出慢语音。 |
| 声学分布变化 | 实际 C PCAN 的时间平均归一化频带轮廓，正常对慢/快的 cosine 中位分别为 0.997961/0.997790。这只排查粗频谱整体偏移，不能证明 F0、音素或短时频谱无失真。仅给旧快速音频乘 0.1，A 从 21→15/100，不能把漏检归因于音量过大。 |
| 训练分布/权重 | 原六速度方案正例总损失质量 1546，其中快 915.52（59.2%）、正常 223.14（14.4%）、慢 407.33（26.3%）。更多速度派生样本稀释了正常/慢速份额。B 固定每个来源总质量后将三类各设 515.33；其慢速提升支持权重偏移确有影响，但不能解释全部失败。 |
| 泛化与 checkpoint | 最后 epoch 的训练损失下降没有带来流式收益；最佳 clip loss 也不能替代 348 个流式验证条件。SpecAugment 与关闭增强均退化；本轮设置不支持移除旧频率增强。困难负例×2 损伤快速召回，说明正负损失取舍是实际问题。 |

既有浮点追踪中 A 只有 19/100 快速满足连续判定，INT8 为 21/100，量化并非原 79 次漏检的主要解释。静音前缀 0/1200/3000 ms 的旧对照逐样本一致；本轮没有把未变化的前端重写成另一套实现。

证据：逐样本配对 JSON（`out/wakeword-round3-20261026/failure-evidence.json`）、轨迹 NPZ（`out/wakeword-round3-20261026/paired-traces.npz`）、所有新增慢速失败及快分数分布图（`out/wakeword-round3-20261026/failure-evidence.svg`）、数字增益诊断（`out/wakeword-round3-20261026/fast-gain-evidence.json`）。图中的相位分析是定位手段，不是改变确认时序后的产品成绩。

## 4. 实际训练、数据与配置

七次训练均从 A 已校验浮点权重初始化，seed=20261026、batch=64、Adam lr=1e-4、12 epoch，Torch CUDA；共同使用 0.65/0.75/1.15/1.35/1.5/1.7 速度，C PCAN 特征、2 s 历史、完整目标最多 5 s、正常/低音量/冷热历史混合。相同配对种子用于控制变量，不冒充跨种子稳定性证据。

| 实验 | 相对控制变化 | 最佳 clip epoch | 导出方式 |
|---|---|---:|---|
| B0 | 保留原六速度权重/legacy 增强/正例逐帧监督 | 7 | 第 12 轮及保存的第 7 轮分别导出 |
| B | 来源总量固定，快/中/慢权重均衡 | 7 | 第 12 轮及第 7 轮 |
| C | B + 训练有效帧 SpecAugment，替代 legacy 频率变形 | 2 | 第 12 轮及第 2 轮 |
| C0 | B + 关闭特征增强，区分遮罩与去除频率变形 | 1 | 第 1 轮 |
| D1 | B + 仅关闭正例逐帧损失 | 7 | 第 12 轮及第 7 轮 |
| D2 | B + 已有部分词/近音难负例权重×2 | 3 | 第 12 轮及第 3 轮 |
| E | 原速度权重与 B 均衡权重各 0.5，保留 legacy 和逐帧监督 | 7 | 第 7 轮 |

D1 的最终正例位置仍是完整 PCM 支撑边界 + 600 ms（受实际有效长度限制），不落入填充区；负例最大帧损失保留。正例逐帧只监督可靠完整波形结束后的区间，不虚构音素级标签。D2 涉及 4868 个已有难负窗口，损失质量 896.96→1793.92，未增加样本或改变标签。train-only 挖掘实际扫描 973 个来源、3600 s，A 触发 0，新增挖掘窗口 0；因此没有声称产生了新的误唤醒负例。结果见 挖掘产物（`out/wakeword-round3-20261026/mined-dataset/`）。

冻结后修正了一处报告字段：D1 的旧 `positive_frame_supervision.enabled/loss_weight` 反映配置开关 1，而 checkpoint 中实际 `positive_weight=0`、`packed_labels=true`。梯度测试与训练执行正确；当前源码改为同时记录配置值、实际值及 reduction。原产物和评测 metadata SHA 不改写，更正见 metadata-corrections.json（`out/wakeword-round3-20261026/metadata-corrections.json`）。实际训练源码在 `trained-source/`，仅含元数据修正的最终源码在 `final-source/`；没有重新训练或改变冻结模型。

E 只组合已有对照支持的权重折中和保留项，没有叠加已经退化的 SpecAugment 或×2困难负损失。正例总质量仍为 1546，快/正常/慢分别为 715.43/369.24/461.33。它是显式记录哈希的一次性权重实验适配器，未新增产品公共入口。

训练清单沿用授权来源：train 正例真实 38 / 合成 1156，unknown 真实 2388 / 合成 748，silence 真实 72 / 合成 16。38 条真实正例不等于 38 名说话人；独立验证/测试没有真人目标词。训练来源、说话人、PCM 与原文件哈希隔离继续由现有校验器执行；派生速度、增益、切窗继承原来源。详见 既有数据许可与来源（本地未发布：`wakeword-training.md#数据与防泄漏`） 及 许可证绑定（`out/wakeword-round3-20261026/../wakeword-training-20261026/data-provenance-bindings.json`）。Kokoro/MeloTTS 标准音色、OpenSLR87/Mobvoi、FLEURS、THCHS-30、MS-SNSD 分别沿用各自许可，不能用代码许可代替录音许可；没有上传或分发音频。

新测试在模型选型前冻结：40 个新合成原录音、2 个许可标准音色 `af_sarah/am_echo`，每源正常/0.1增益/0.75速度/1.5速度共 160 条件；800 条真实公开非目标、80 条合成难负例，均排除前两轮已用来源。Kokoro-82M 资产固定到 `f3ff3571791e39611d31c381e3a41a3af07b4987`。这是新标准英语音色执行既有中英混合 token 路径的合成压力测试，未做真人声学发音验收，不等于中文口音泛化。

新负例真实 3741320 ms、合成 630260 ms，共 4371580 ms / 1.214328 h；每段独立监听，不能称连续长稳运行。生成及隔离证据：计划（`out/wakeword-round3-20261026/holdout-plan.json`）、来源/资产哈希（`out/wakeword-round3-20261026/holdout-provenance.json`）、审计（`out/wakeword-round3-20261026/holdout-audit.json`）。

跨历史清单的追加核对发现：与 train/validation 的来源、说话人、片段哈希重叠均为 0；与前两轮 test 的来源/完整会话文件哈希重叠为 0，但负例沿用 **11 个真实说话人 + 2 个合成音色**。因此负例只具备新来源隔离，不能称完全新说话人测试。80 条合成负例的片段入口取了共同的 3 s 静音前缀，与上一轮有一个片段文件哈希重叠；完整会话音频不重叠，本文 FA/h 使用完整会话，不用这些片段分类成绩证明改善。原冻结数据与结果均未改写。证据：隔离限定（`out/wakeword-round3-20261026/isolation-qualification.json`）、复核脚本（`out/wakeword-round3-20261026/audit_isolation.py`）。

## 5. 统一评测

以下全部为相同 C 前端、相同触发策略下的 **INT8 实际事件**。快为 1.5×、慢为 0.75×；小音量为数字增益 0.1，不冒充距离/SPL。全窗口外包含纯负例事件和目标会话过早/过晚事件，不能将两列相加。各组重复唤醒均为 0。`validation` 另含 tempo1.25/gain0.25 各 18 条，已计入总数。

### 流式验证集（用于选型）

| 模型 | 快 | 正常 | 慢 | 小音量 | 全部正例 | 纯负例触发 | 小时 | 次/h | 全部窗口外 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A 冻结基线 | 17/78 | 52/78 | 20/78 | 72/78 | 197/348 | 3 | 0.641844 | 4.6740 | 4 |
| B0 原权重控制 | 17/78 | 48/78 | 18/78 | 76/78 | 195/348 | 2 | 0.641844 | 3.1160 | 3 |
| B 速度均衡 | 17/78 | 49/78 | 31/78 | 73/78 | 206/348 | 2 | 0.641844 | 3.1160 | 3 |
| C SpecAugment | 16/78 | 18/78 | 18/78 | 21/78 | 109/348 | 2 | 0.641844 | 3.1160 | 4 |
| C0 无特征增强 | 15/78 | 18/78 | 18/78 | 18/78 | 105/348 | 1 | 0.641844 | 1.5580 | 4 |
| D1 仅末帧正例 | 13/78 | 47/78 | 41/78 | 73/78 | 210/348 | 2 | 0.641844 | 3.1160 | 8 |
| D2 难负例×2 | 16/78 | 33/78 | 21/78 | 64/78 | 170/348 | 2 | 0.641844 | 3.1160 | 4 |
| E 半均衡组合 | 17/78 | 51/78 | 22/78 | 74/78 | 200/348 | 2 | 0.641844 | 3.1160 | 3 |

### 旧集回归（此前已暴露）

| 模型 | 快 | 正常 | 慢 | 小音量 | 全部正例 | 纯负例触发 | 小时 | 次/h | 全部窗口外 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A 冻结基线 | 21/100 | 100/100 | 97/100 | 100/100 | 318/400 | 0 | 1.216794 | 0.0000 | 0 |
| B0 原权重控制 | 55/100 | 100/100 | 92/100 | 100/100 | 347/400 | 0 | 1.216794 | 0.0000 | 0 |
| B 速度均衡 | 30/100 | 100/100 | 100/100 | 100/100 | 330/400 | 0 | 1.216794 | 0.0000 | 0 |
| C SpecAugment | 17/100 | 93/100 | 88/100 | 100/100 | 298/400 | 0 | 1.216794 | 0.0000 | 0 |
| C0 无特征增强 | 4/100 | 94/100 | 85/100 | 100/100 | 283/400 | 0 | 1.216794 | 0.0000 | 0 |
| D1 仅末帧正例 | 33/100 | 100/100 | 100/100 | 100/100 | 333/400 | 0 | 1.216794 | 0.0000 | 3 |
| D2 难负例×2 | 3/100 | 99/100 | 93/100 | 100/100 | 295/400 | 0 | 1.216794 | 0.0000 | 0 |
| E 半均衡组合 | 41/100 | 100/100 | 95/100 | 100/100 | 336/400 | 0 | 1.216794 | 0.0000 | 0 |

### 上一轮留出集（本轮作为已暴露回归）

| 模型 | 快 | 正常 | 慢 | 小音量 | 全部正例 | 纯负例触发 | 小时 | 次/h | 全部窗口外 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A 冻结基线 | 30/40 | 40/40 | 40/40 | 40/40 | 150/160 | 2 | 1.177500 | 1.6985 | 11 |
| B0 原权重控制 | 40/40 | 40/40 | 40/40 | 40/40 | 160/160 | 2 | 1.177500 | 1.6985 | 2 |
| B 速度均衡 | 35/40 | 40/40 | 40/40 | 40/40 | 155/160 | 3 | 1.177500 | 2.5478 | 8 |
| C SpecAugment | 24/40 | 40/40 | 40/40 | 40/40 | 144/160 | 1 | 1.177500 | 0.8493 | 3 |
| C0 无特征增强 | 23/40 | 40/40 | 33/40 | 40/40 | 136/160 | 1 | 1.177500 | 0.8493 | 4 |
| D1 仅末帧正例 | 30/40 | 40/40 | 40/40 | 40/40 | 150/160 | 1 | 1.177500 | 0.8493 | 11 |
| D2 难负例×2 | 24/40 | 40/40 | 39/40 | 40/40 | 143/160 | 1 | 1.177500 | 0.8493 | 13 |
| E 半均衡组合 | 37/40 | 40/40 | 40/40 | 40/40 | 157/160 | 2 | 1.177500 | 1.6985 | 5 |

### 第三轮新来源留出集（正例音色独立；负例说话人已暴露）

| 模型 | 快 | 正常 | 慢 | 小音量 | 全部正例 | 纯负例触发 | 小时 | 次/h | 全部窗口外 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| A 冻结基线 | 20/40 | 40/40 | 29/40 | 40/40 | 129/160 | 1 | 1.214328 | 0.8235 | 1 |
| B 速度均衡 | 21/40 | 40/40 | 40/40 | 40/40 | 141/160 | 5 | 1.214328 | 4.1175 | 6 |
| E 半均衡组合 | 23/40 | 40/40 | 40/40 | 40/40 | 143/160 | 4 | 1.214328 | 3.2940 | 6 |

新测试 1/5/4 次纯负例触发全部来自真实公开非目标语音；合成负例三者均为 0。B/E 另有 1/2 次目标会话窗口外事件，不能混入纯负例 FA/h，也不能隐藏。没有独立真人目标词识别率。

### 末轮 checkpoint 失败控制（保留，不挑选删除）

| 第 12 轮导出 | 验证总命中 /348 | 旧快 /100 | 旧正常 /100 | 旧慢 /100 | 旧总 /400 | 上轮总 /160 | 上轮纯负例 / 全部窗口外 |
|---|---:|---:|---:|---:|---:|---:|---:|
| B0-control-last | 142 | 42 | 84 | 75 | 301 | 150 | 1 / 5 |
| B-speed-balanced | 131 | 18 | 87 | 75 | 280 | 148 | 1 / 9 |
| C-specaugment | 104 | 18 | 68 | 71 | 242 | 116 | 0 / 5 |
| D-final-only | 122 | 22 | 95 | 88 | 305 | 147 | 0 / 10 |
| D-hard-negatives | 124 | 10 | 81 | 75 | 266 | 142 | 1 / 7 |

自动规则同时检查验证与已暴露回归的各语速、纯负例、全窗口外、重复事件、模型大小和原生兼容性，要求快语速有增益。没有模型全部满足，测试前已冻结为 A；E 仅作为满足误触发约束后验证总命中较高的研究比较对象，B 作为速度均衡对照。第三轮测试没有触发训练或改选。证据：选型脚本（`out/wakeword-round3-20261026/compare.py`）、测试前冻结（`out/wakeword-round3-20261026/selection-before-holdout.json`）、独立测试执行（`out/wakeword-round3-20261026/evaluate_holdout.py`）。

## 6. 包、推理与资源验证

所有 12 个实验导出及 A 均通过 WKM2 校验、原生 TFLM 加载、749 行既有公开训练前端特征的逐行分数对比及 reset 检查；最大原生 TFLM/参考 TFLite 分数差 0，reset exact。各模型为 INT8 `[1,1,40,1]` → `[1,3]`，含 5 个显式状态；输入、输出和状态量化参数按各包 metadata 使用。模型 26680 B，WKM2 26820 B，原生 host arena 使用 21824 B。证据：原生检查脚本（`out/wakeword-round3-20261026/native_round.py`）、全部包及结果（`out/wakeword-round3-20261026/native/`）。

| 对象 | PC TFLM median / P95 μs | WKM2 SHA256 |
|---|---:|---|
| A 冻结基线 | 21.105 / 21.903 | `3975c5e7afdc9c4c55d76e264116f76a8c123508b47dae0b2af9400a653ae431` |
| B 速度均衡 | 21.573 / 22.234 | `51dcd9974cd9ad54032ba8e7a6e5bab4c30c40e99ddcd9879fa0f8ab5525b6d2` |
| E 半均衡组合 | 21.091 / 23.149 | `4d36b9bd052f400f404c945a25ad551150cfbf505a6d714e6b1ed5f7f5300f58` |

上述计时各 1498 次调用，排除前端和调度，仅是 PC 兼容性/耗时证据。**BK7258 的推理延迟、端到端唤醒延迟、CPU、峰值 RAM、掉帧与长稳仍待测。** 当前会话没有可靠人工 word-end 标注，不能用宽事件窗口末尾伪造唤醒延迟；未把 host arena 当板端峰值 RAM。

研究包：B（`out/wakeword-round3-20261026/native/B-speed-balanced-best/nihao_openvela_candidate.wkm`）、E（`out/wakeword-round3-20261026/native/E-half-speed-balance/nihao_openvela_candidate.wkm`），配套模型/metadata/训练配置在同名实验目录；二者均未获替换资格。当前可回滚基线仍是原 `out/wakeword-training-20261026/selected/nihao_openvela_candidate.wkm`，没有另造“已选最佳”目录。

最终源码的 KWS/frontend/CUDA 测试 **55 项通过**，覆盖来源权重、有效帧遮罩、填充边界、末帧/逐帧梯度、TF/Torch 损失、checkpoint 恢复/末轮导出及原 C 流程；原生测试目标已确认最新。记录：pytest（`out/wakeword-round3-20261026/regression-tests-final.log`）、原模型与运行时保留检查（`out/wakeword-round3-20261026/preservation-check.json`）、限定范围样式检查（`out/wakeword-round3-20261026/style-check.json`）。没有改音频采集、应用事件接口、按键、认领/配网或 OTA；未声称这些功能已完成新实机回归，也未为未改的固件重复构建。后续板端验收沿用既有 [部署与回归步骤](wakeword-speed-training.md#部署兼容性与资源)。

## 7. 复现与回滚

训练环境为 Python 3.10.12、Torch 2.12.1+cu130、TensorFlow 2.15.1、NumPy 1.26.4、RTX 5060；固定随机种子及确定性算子设置。精确命令/数据/训练源码哈希在 training-recipe.json（`out/wakeword-round3-20261026/training-recipe.json`） 与每组 `experiment-config.json`，实际 epoch 日志为同名 `.log`。不保证其他依赖/GPU 的模型文件逐字节相同。数据准备复用原流水线及 新留出生成脚本（`out/wakeword-round3-20261026/prepare_holdout.py`）；没有重新训练合成器。

本轮新训练入口仍为 `tools/bk7258/bk7258.py voice kws train`。实现修改限于 `voice_kws.py`、`voice_kws_torch.py` 及相关现有测试，增加速度质量均衡、有效帧 SpecAugment、正例末帧消融、已标记困难负例权重与 checkpoint 选择。编排脚本放在 out，非另一套产品 CLI。

工作区根下可在新目录复现整组训练；原模型目录不覆盖：

```bash
mkdir out/wakeword-round3-repeat
cp out/wakeword-round3-20261026/run_initial.py out/wakeword-round3-repeat/
cp out/wakeword-round3-20261026/reexport_best.py out/wakeword-round3-repeat/
cp out/wakeword-round3-20261026/run_followup.py out/wakeword-round3-repeat/
cp out/wakeword-round3-20261026/run_combination.py out/wakeword-round3-repeat/
export TF_CPP_MIN_LOG_LEVEL=3 OMP_NUM_THREADS=2 PYTHONNOUSERSITE=1
out/kws-optimize-20261024/venv/bin/python out/wakeword-round3-repeat/run_initial.py
out/kws-optimize-20261024/venv/bin/python out/wakeword-round3-repeat/reexport_best.py \
  B0-control-last B-speed-balanced C-specaugment D-final-only D-hard-negatives
out/kws-optimize-20261024/venv/bin/python out/wakeword-round3-repeat/run_followup.py
out/kws-optimize-20261024/venv/bin/python out/wakeword-round3-repeat/run_combination.py
```

重导出适配器逐项核对训练 features/targets/weights/split 索引、源码及配置 contract，加载已保存 `best_model`，不执行 optimizer；使用同一 train-only 校准生成器。标准 metadata 明确标注继承 epoch/history、源 checkpoint SHA 及 `no_optimizer_steps=true`。评测用 evaluate_round.py（`out/wakeword-round3-20261026/evaluate_round.py`），汇总用 `compare.py freeze` 后执行 `evaluate_holdout.py`、`compare.py final`；复现已看过的同一测试只叫复现，不再叫新的独立测试。

本次专属差异与原文件保存在 round3-only.patch（`out/wakeword-round3-20261026/round3-only.patch`） 和 before/（`out/wakeword-round3-20261026/before/`），没有把进任务前已有改动当作本轮提交。代码回滚先在仓库根执行：

```bash
git apply --reverse --check ../out/wakeword-round3-20261026/round3-only.patch
git apply --reverse ../out/wakeword-round3-20261026/round3-only.patch
```

检查失败时停止，不强制覆盖后续工作。模型从未切换，因此当前无需设备回滚；未来获得部署授权后若试装研究包，应通过现有 App 模型入口恢复上述 A 并回读 SHA，不能覆盖内置模型或改变阈值以补偿实验。

## 8. 剩余问题与下一轮边界

本轮证实速度损失份额会影响快慢取舍，逐帧监督能约束窗口外触发；也证实这组 SpecAugment、去除原频率增强或加倍已有难负惩罚没有综合收益。E 在新留出集多命中 14 次，但额外真实语音误触发使其不合格。现有 train-only 挖掘对 A 为零，不能推导它覆盖了候选的新失败模式。

后续若开展新一轮，应先在训练来源上挖掘 B/E 的真实语音困难负例，独立控制其损失，并补充带可靠完成边界的自然快慢语音；当前第三轮测试从此降为回归集，必须另留最终测试。音素压缩/音高伪影缺少对齐证据，声学房间、距离、自然口音、电视/风扇和板端并发仍未验收。以上是明确剩余工作，不是已证明的下一轮收益。

严格未见真实负例说话人仍不足：盘点中 THCHS-30 看似额外的 57 条/6 人其实已在旧清单的 `sessions` 中用于验证/测试，不能只核对 `entries` 就重新宣称隔离；FLEURS 的现有记录缺少可靠 speaker 字段，Mobvoi 的已下载具名 speaker 均已在既有集合中出现。没有改 speaker ID 伪造独立，也没有因这个限制停止本轮实际模型实验。当前的来源隔离结果保留，但完整独立说话人与实机验收尚未达成。
