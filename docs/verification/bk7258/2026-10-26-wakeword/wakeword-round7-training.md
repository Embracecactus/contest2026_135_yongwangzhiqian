# 傻妞 KWS 第七轮：真实语料扩充与 H3 强化训练

> 证据可得性（2026-11-04 核验）：本文引用的工作区 `out/` 原始产物不可用，仅保留本文记录的摘要/哈希；原链接已改为路径文本，结论边界不变。见[验收原始证据留存审计](../2026-11-04-acceptance-evidence-retention.md)。

日期：2026-10-27。**保留 H3，A 继续作为原始回滚版本；本轮未形成全面优于 H3 的产品替换证据。**

完成累计 **224.441 小时、171,834 条原始录音**的去重真人训练池；本轮新增清洗并保留 **136,269 条 / 203.212 小时**（含保护的验证/测试）。其中新增可训练 **111,497 条 / 165.820 小时**。
实际完成7组有效训练、28个epoch，均导出INT8并通过原生TFLM/WKM2校验。另保留2组增强隔离失效的早期训练（共8个epoch），不混入有效对照。没有刷机、覆盖A/H3/默认模型、提交、推送、上传录音或新增付费资源。

验证集预选研究代表 **J4-staged**；最终冻结合成音色上，H3 115/128、研究代表 112/128，近音/缺词触发分别 48 与 44。这不代表真人完整目标词的识别率。

## 1. 继承数据、增量下载与去重

R6的52,261条分为：10,826条/12.351h保护验证测试；13,816条/17.570h已被旧新增采样器使用；27,619条/27.427h合格训练录音未被固定子集采样器选中。未使用的主要原因是旧10k负例/6292辅助子集的取样上限，并非本批音质或任务标签不合格。
本轮41,435条R6合法训练原始录音全部进入完整池实验，历史验证测试没有转入训练。R6清洗前已排除的661条ASCEND录音没有重新纳入。

| 本轮新增来源 | 去重后条数 | 全部分组小时 | 训练条数 / 小时 | 验证条数 | 测试条数 | 匿名ID说明 |
|---|---:|---:|---:|---:|---:|---|
| mobvoi | 45,853 | 33.235 | 45,853 / 33.235 | 0 | 0 | 448，均已在历史语料出现 |
| aishell3 | 50,751 | 48.133 | 38,249 / 36.001 | 6,552 | 5,950 | 124个新语料内ID；1组与历史AISHELL-1验证音频关联 |
| librispeech | 39,665 | 121.845 | 27,395 / 96.584 | 6,711 | 5,559 | 397个新语料内ID；其中241训练、83验证、73测试 |

Mobvoi/AISHELL-3复用本地官方大包，避免重复下载。LibriSpeech实际新增下载train-clean-100及dev/test clean/other五个分片，全部官方MD5匹配；逐条16k PCM与容器哈希另行校验。
136,595条新增抽取候选中先排除2条相同PCM，再对136,593条通过声学近复制筛查，排除324条，剩136,269条。324条包括5条Mobvoi旧录音近复制、319条与历史AISHELL-1验证音频近乎相同的AISHELL-3录音。后者只在验证来源内，未改为训练。
跨集合近复制检查覆盖231,896条已读录音；采用8×8时频指纹候选检索、时长差≤10ms、归一化波形相关≥0.995，并通过三来源半增益复制阳性控制。它不证明任意剪切/编码变换或跨语料真实身份完全无重叠。

时长按去重后的单声道PCM文件计算。同人同词不同录音条件仍作为有效声学样本保留，不能将224.44h解释成224.44h彼此独立的说话事件；无法从元数据确认的采集关联不作独立性保证。

匿名ID是语料内分组，不直接等于全球唯一真人。FLEURS未提供真实说话人ID；ASCEND沿用说话人连通分组且本批均为训练，不能宣称有新的独立中英对话测试。AISHELL-3新增124个语料内ID中，已有1组通过相同录音关联到旧AISHELL-1验证来源。

THCHS-30另已获取并校验完整gzip CRC、本地SHA，清洗13,216条/约33.75h。初次处理重复读取data与train/dev/test目录并错配转写键，已修复并保留失败清单。由于A/B/C/D是文本分组，文件前缀不构成已核实的真人身份映射，**全部隔离，不计入224.44h、不参与SGD或独立评测**。官方MD5端点不可用，不伪称通过官方校验。
Common Voice v27需要官方账户/条款访问；用户答复“暂无，先用其他合法公开语料”，本轮实际获取0条。

许可与归档证据：DATA-NOTICES.md（`out/wakeword-round7-20261027/DATA-NOTICES.md`），Libri归档校验（`out/wakeword-round7-20261027/data/librispeech/artifact-guard.json`），AISHELL-3校验（`out/wakeword-round7-20261027/data/aishell3-incremental/artifact-guard.json`）。
来源清单与去重证据：training-pool.json（`out/wakeword-round7-20261027/training-pool.json`）、unused-round6-audit.json（`out/wakeword-round7-20261027/unused-round6-audit.json`）、acoustic-overlap-audit.json（`out/wakeword-round7-20261027/acoustic-overlap-audit.json`）、source-overlap-provenance.json（`out/wakeword-round7-20261027/source-overlap-provenance.json`）。

## 2. 新目标正例与困难负例

固定Kokoro-82M v1模型版本，先冻结6训练、2验证、2最终测试标准音色，与既往已用音色清单比较后才生成。正负样本使用相同合成器和音色。
共440条完整原始合成：训练96条正例+168条困难负例；验证/测试各32条正例+56条负例。两种规范IPA形式分别覆盖中英切换停顿及连读。每个验证/测试原始录音派生正常、1.5倍速、0.75倍速、0.1增益条件，分别128条正例、224条困难负例。
R6 H3的62次近音/缺词触发为near_pila17、near_vita30、missing_open15；本轮只迁移失败类别，未把已暴露录音或其增强加入训练。补充missing_vela/open_only/vela_only/nihao_only对照。
完整合成波形限制0.2–3s，超限丢弃而非截断；本批超限0条。训练复用保音高速度增强、音量/背景增强、时间移动、随机结束位置及来源权重。最终会话起点按20ms相位散布，事件标签固定为开始至完整波形结束+600ms。
**波形完整与IPA意图不等于经过真人音素校验**；没有新增获得同意的真人完整“你好，open-vela”录音。普通中文、英语及辅助唤醒词均标unknown，不冒充目标正类；没有录音采集或拼接真人完整正例。
清单：freeze.json（`out/wakeword-round7-20261027/data/pronunciation/freeze.json`）、source-manifest.json（`out/wakeword-round7-20261027/data/pronunciation/source-manifest.json`）、hard-negative-root-cause.json（`out/wakeword-round7-20261027/hard-negative-root-cause.json`）。本地真人采集继续使用 wakeword-human-diagnostic-plan.md（本地未发布：`wakeword-human-diagnostic-plan.md`）。

## 3. 训练对照及实际曝光量

所有实验从同一个H3浮点检查点初始化。32通道、5层因果TCN，16k/30ms窗/20ms移/40维PCAN-v2；每20ms推理、每300ms判定、60%阈值、连续两次、20%释放及1000ms冷却均未改。
seed=2026102608，batch=64，学习率1e-5，4个epoch；沿用max正/负时序损失、负例双确认惩罚0.5、600ms正例保持、同一套旧训练校准特征，未改变模型容量或算子。

- J0/H3：冻结原始模型；J0c是额外的旧数据继续训练对照，不能与H3混称。
- J1-100h / J1-expanded：目标正例不变，真人负例占固定unknown损失预算10%，各来源均分该预算；池分别100.007h和224.441h。
- J2：新目标正例占正例损失预算35%，旧正例65%；未加入新增真人池和显式近音池，保留现有目标词衍生负例机制。
- J3：J2正例+10%真人unknown预算+10%困难unknown预算，普通unknown80%；near_pila/near_vita/missing_open在困难池内权重2，其余1。
- J4：相同联合数据/权重，完整H3头不重置；第1轮仅头，第2–3轮上两层，第4轮全部解冻。CLI只允许新分类头迁移使用解冻参数，因此通过已有backend接口传递同一解冻日程；无生产代码修改。
- J3b：验证发现J3召回崩塌后、最终测试打开前，预登记一次单变量预算修正：困难unknown从10%降为0.5%，普通unknown补为89.5%，真人仍10%。困难损失总量由6754.66降为337.73；全部正例预算1523.00。模型、数据、种子、轮数、前端和策略相同。
- J5未执行：当前证据指向数据/损失权重与量化/时序取舍，未证实容量瓶颈。

| 实验 | 实际epoch / 选中epoch | 全程真人原始录音条数 | 导出检查点已见条数 | 全程非填充损失h / 导出检查点h |
|---|---:|---:|---:|---:|
| J0c-replay | 4 / 4 | 0 | 0 | 0.000 / 0.000 |
| J1-100h | 4 / 3 | 66,864 | 50,209 | 45.785 / 34.356 |
| J1-expanded | 4 / 3 | 152,932 | 114,751 | 108.548 / 81.411 |
| J2-positive | 4 / 1 | 0 | 0 | 0.000 / 0.000 |
| J3-joint | 4 / 4 | 152,932 | 152,932 | 108.548 / 108.548 |
| J4-staged | 4 / 1 | 152,932 | 38,124 | 108.548 / 27.057 |
| J3b-balanced-hard | 4 / 3 | 152,932 | 114,751 | 108.548 / 81.411 |

| 实验 | 训练入口墙钟 | PC训练峰值RSS GiB | PyTorch峰值allocated MiB |
|---|---:|---:|---:|
| J0c-replay | 未单独计时 | 未单独采样 | 70.804 |
| J1-100h | 4:31.10 | 9.956 | 70.804 |
| J1-expanded | 5:19.96 | 9.411 | 70.804 |
| J2-positive | 未单独计时 | 未单独采样 | 70.804 |
| J3-joint | 4:45.89 | 12.566 | 70.804 |
| J4-staged | 4:37.51 | 11.751 | 70.804 |
| J3b-balanced-hard | 4:44.16 | 12.889 | 70.804 |

使用已有RTX5060及本地工具环境；没有新增支出。墙钟含本次入口的数据准备、训练、验证和导出，存在同机评测负载；RSS包含mmap驻留页。PyTorch allocated不含CUDA上下文及其他框架预留，以上均不是板端资源数值。7组新训练的Torch→Keras分数最大误差约1.19e-07，后续INT8误差单独分析。

完整池有152,932条本轮真人窗口（含R6回放及新来源），每条原始录音在4轮中的一个确定性分区出现一次。每条选3s损失窗+最多2s邻接历史：实际非填充损失108.548h、邻接历史20.367h，**不能把224.44h原始池容量写成224.44h损失音频**。较长LibriSpeech录音仅取一个窗口；全转写词匹配是候选标记，不保证open等词落在该3s窗内。
完整池窗口中，来源转写词候选922条，冻结H3浮点双确认峰≥0.6的41条；绝大多数扩容是普通语音，不是已证实的高价值近音。152,932条均进入全程梯度训练，但较早导出的最佳检查点仅见到其前若干epoch子集。
所有101,615条旧特征逐值等同R6缓存；旧增强、背景和校准均与新增数据隔离。最初2次训练因共享衍生/背景池使旧特征改变，已判无效并保留，修复后重新完成对照。
coverage核验旧27,619条未使用训练样本全部进入完整池、Libri另保留的1,144条/4.007h在挖掘/SGD之前冻结、所有旧验证测试受保护；62个原始代码/模型/报告文件哈希未变。
配置、执行源码快照及每epoch原始录音列表见各`*-isolated`与`trained-source`目录；完整证据 training-coverage-audit.json（`out/wakeword-round7-20261027/training-coverage-audit.json`）、J3b-before-training.json（`out/wakeword-round7-20261027/J3b-before-training.json`）。

## 4. 选型与冻结测试纪律

验证时每来源真人FA/h≤1且近音触发≤H3才进入研究候选，再比较快/正常/慢等权召回；同时记录召回/近音/各来源FAR的Pareto取舍。该研究门槛不是产品验收阈值，且“数学上非支配”不意味着J3等严重失召回模型可用。
选型锁定时间 `2026-10-27T01:02:21.886071+00:00`，主模型 `H3`、预选研究代表 `J4-staged`。完整模型/验证结果/最终数据哈希见 selection-before-final.json（`out/wakeword-round7-20261027/selection-before-final.json`）。锁定后8个模型各做一次最终对照，没有依据最终结果继续训练或改选。

### 新合成音色验证：128正例、224困难负例

| 模型 | 快 | 正常 | 慢 | 低音量 | 全部 | 近音/缺词触发 | 正例窗口外触发 | 真人负例触发 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| H3 | 25/32 | 32/32 | 31/32 | 32/32 | 120/128 | 63 | 3 | 0 |
| J0c-replay | 17/32 | 32/32 | 28/32 | 31/32 | 108/128 | 39 | 1 | 0 |
| J1-100h | 20/32 | 32/32 | 31/32 | 32/32 | 115/128 | 46 | 2 | 4 |
| J1-expanded | 18/32 | 32/32 | 32/32 | 32/32 | 114/128 | 44 | 3 | 2 |
| J2-positive | 19/32 | 32/32 | 30/32 | 32/32 | 113/128 | 40 | 1 | 0 |
| J3-joint | 11/32 | 10/32 | 15/32 | 10/32 | 46/128 | 4 | 5 | 3 |
| J4-staged | 24/32 | 32/32 | 29/32 | 32/32 | 117/128 | 52 | 2 | 0 |
| J3b-balanced-hard | 22/32 | 29/32 | 25/32 | 31/32 | 107/128 | 34 | 1 | 4 |

真人负例列对应 20.99029h（原始音频20.95921h），不是上述合成负例时长。验证合成困难集为 0.46194h。

### 冻结最终合成音色：128正例、224困难负例

| 模型 | 快 | 正常 | 慢 | 低音量 | 全部 | 近音/缺词触发 | 正例窗口外触发 | 真人负例触发 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| H3 | 19/32 | 32/32 | 32/32 | 32/32 | 115/128 | 48 | 5 | 0 |
| J0c-replay | 11/32 | 25/32 | 23/32 | 29/32 | 88/128 | 32 | 2 | 0 |
| J1-100h | 14/32 | 29/32 | 26/32 | 31/32 | 100/128 | 40 | 5 | 3 |
| J1-expanded | 14/32 | 30/32 | 24/32 | 31/32 | 99/128 | 38 | 4 | 3 |
| J2-positive | 13/32 | 29/32 | 23/32 | 31/32 | 96/128 | 36 | 3 | 0 |
| J3-joint | 4/32 | 4/32 | 4/32 | 7/32 | 19/128 | 16 | 1 | 0 |
| J4-staged | 18/32 | 32/32 | 30/32 | 32/32 | 112/128 | 44 | 5 | 0 |
| J3b-balanced-hard | 19/32 | 30/32 | 25/32 | 31/32 | 105/128 | 38 | 4 | 6 |

真人负例列对应 16.46024h（原始音频16.43344h）。最终合成困难集为 0.44362h。每套仅2个同一合成器的留出音色；同源速度/音量变体有关联，不能把128条当成128位真人。
近音/缺词列只统计无目标短语会话；窗口外列统计正例会话固定有效窗之外的事件，不把两者混成普通人声FAR。
PC独立测试与真人现场/板端验收严格分开。本轮缺少真人完整目标词独立诊断集。

### 已暴露开发回归，不再称独立测试

| 模型 | 旧400快/正常/慢/低音量 | 旧400总命中 | 曝光来源160总命中 | R6规范发音160总命中 | R6近音/缺词触发 | 旧真人4.197h触发 |
|---|---|---:|---:|---:|---:|---:|
| H3 | 99/100, 96/100, 100/100, 98/100 | 393/400 | 151/160 | 147/160 | 62 | 1 |
| J0c-replay | 66/100, 53/100, 91/100, 78/100 | 288/400 | 142/160 | 141/160 | 42 | 0 |
| J1-100h | 82/100, 67/100, 100/100, 86/100 | 335/400 | 147/160 | 145/160 | 58 | 2 |
| J1-expanded | 83/100, 65/100, 99/100, 79/100 | 326/400 | 147/160 | 145/160 | 52 | 1 |
| J2-positive | 67/100, 53/100, 85/100, 68/100 | 273/400 | 144/160 | 144/160 | 46 | 0 |
| J3-joint | 41/100, 9/100, 7/100, 17/100 | 74/400 | 128/160 | 76/160 | 15 | 0 |
| J4-staged | 95/100, 75/100, 100/100, 95/100 | 365/400 | 148/160 | 146/160 | 60 | 0 |
| J3b-balanced-hard | 87/100, 50/100, 76/100, 61/100 | 274/400 | 141/160 | 147/160 | 41 | 1 |

R6表仅规范viila发音160正例；另64条vehla/veila诊断变体单列在results.json，不混入规范发音召回。旧160来源的各语速、窗口外触发及所有误例均保留在统一JSON/原始分数轨迹。

### 真人负例分来源与不确定性

| 模型 | 集合 | AISHELL-3 次数 / 小时；FA/h [95%区间] | LibriSpeech 次数 / 小时；FA/h [95%区间] |
|---|---|---|---|
| H3 | validation | 0 / 6.461; 0.000 [0.000, 0.571] | 0 / 14.530; 0.000 [0.000, 0.254] |
| H3 | test | 0 / 5.704; 0.000 [0.000, 0.647] | 0 / 10.756; 0.000 [0.000, 0.343] |
| J0c-replay | validation | 0 / 6.461; 0.000 [0.000, 0.571] | 0 / 14.530; 0.000 [0.000, 0.254] |
| J0c-replay | test | 0 / 5.704; 0.000 [0.000, 0.647] | 0 / 10.756; 0.000 [0.000, 0.343] |
| J1-100h | validation | 1 / 6.461; 0.155 [0.004, 0.862] | 3 / 14.530; 0.206 [0.043, 0.603] |
| J1-100h | test | 1 / 5.704; 0.175 [0.004, 0.977] | 2 / 10.756; 0.186 [0.023, 0.672] |
| J1-expanded | validation | 1 / 6.461; 0.155 [0.004, 0.862] | 1 / 14.530; 0.069 [0.002, 0.383] |
| J1-expanded | test | 1 / 5.704; 0.175 [0.004, 0.977] | 2 / 10.756; 0.186 [0.023, 0.672] |
| J2-positive | validation | 0 / 6.461; 0.000 [0.000, 0.571] | 0 / 14.530; 0.000 [0.000, 0.254] |
| J2-positive | test | 0 / 5.704; 0.000 [0.000, 0.647] | 0 / 10.756; 0.000 [0.000, 0.343] |
| J3-joint | validation | 0 / 6.461; 0.000 [0.000, 0.571] | 3 / 14.530; 0.206 [0.043, 0.603] |
| J3-joint | test | 0 / 5.704; 0.000 [0.000, 0.647] | 0 / 10.756; 0.000 [0.000, 0.343] |
| J4-staged | validation | 0 / 6.461; 0.000 [0.000, 0.571] | 0 / 14.530; 0.000 [0.000, 0.254] |
| J4-staged | test | 0 / 5.704; 0.000 [0.000, 0.647] | 0 / 10.756; 0.000 [0.000, 0.343] |
| J3b-balanced-hard | validation | 1 / 6.461; 0.155 [0.004, 0.862] | 3 / 14.530; 0.206 [0.043, 0.603] |
| J3b-balanced-hard | test | 3 / 5.704; 0.526 [0.108, 1.537] | 3 / 10.756; 0.279 [0.058, 0.815] |

这些是按同一匿名来源串接的真实原始语音，含显式接缝与每段不足20ms的对齐补零，不是自然连续录制20小时的房间。Poisson 95%区间只在平稳独立到达假设下解释；0次不等于永不误唤醒。不得合并中文/英语后掩盖来源退化。
逐事件原始录音哈希、接缝距离和前5s上下文来源数见 real-trigger-attribution.json（`out/wakeword-round7-20261027/analysis/real-trigger-attribution.json`）。

## 5. 失败原因、量化与时序证据

J3的10%困难预算约为全部正例预算4.435倍，验证召回46/128；J3b仅改变该预算分配后恢复至107/128，近音触发由4增为34。这个单变量消融支持“相似片段负惩罚过强会连带压低完整词”这一权重失衡机制，但不证明仅靠一个权重即可解决泛化。
J4选中第1轮仅头更新检查点；后续解冻未改善选型损失。不能称导出的J4编码器吸收了全部224小时新声学信息。
扩容100→224小时，验证快语速20/32→18/32，慢31/32→32/32，真实触发4→2；整体并非单调提升，小计数区间重叠。本轮无充分证据支持继续无差别扩到500小时。

| 模型 | 浮点/INT8 正例满足双确认分数 | 浮点/INT8 困难负例满足双确认分数 |
|---|---:|---:|
| H3 | 121 / 120 | 71 / 63 |
| J0c-replay | 112 / 108 | 42 / 39 |
| J1-100h | 119 / 115 | 51 / 46 |
| J1-expanded | 119 / 114 | 51 / 44 |
| J2-positive | 116 / 113 | 43 / 40 |
| J3-joint | 56 / 46 | 10 / 4 |
| J4-staged | 119 / 117 | 56 / 52 |
| J3b-balanced-hard | 120 / 107 | 37 / 34 |

上表是同一验证音频、同一C特征与300ms时钟上浮点/INT8的双高分检查，省略释放/冷却状态，**不是另一套实测唤醒率/FAR**。实际指标以C触发结果为准。量化造成部分边界样本分歧；主训练退化也存在于浮点结果，不能把所有失败归咎量化。

| 模型 | 最终集漏检机械分类 | 目标波形结束后600ms内平均高分时间 |
|---|---|---:|
| H3 | {"decision_ticks_missed_peak": 4, "all_frame_score_below_threshold": 1, "two_decision_ticks_not_sustained": 8} | 515.2 ms |
| J0c-replay | {"two_decision_ticks_not_sustained": 16, "all_frame_score_below_threshold": 11, "decision_ticks_missed_peak": 13} | 396.7 ms |
| J1-100h | {"two_decision_ticks_not_sustained": 17, "decision_ticks_missed_peak": 9, "all_frame_score_below_threshold": 2} | 477.3 ms |
| J1-expanded | {"decision_ticks_missed_peak": 10, "all_frame_score_below_threshold": 1, "two_decision_ticks_not_sustained": 18} | 469.7 ms |
| J2-positive | {"all_frame_score_below_threshold": 5, "decision_ticks_missed_peak": 9, "two_decision_ticks_not_sustained": 18} | 445.0 ms |
| J3-joint | {"all_frame_score_below_threshold": 63, "decision_ticks_missed_peak": 22, "two_decision_ticks_not_sustained": 24} | 132.3 ms |
| J4-staged | {"decision_ticks_missed_peak": 6, "all_frame_score_below_threshold": 1, "two_decision_ticks_not_sustained": 9} | 489.5 ms |
| J3b-balanced-hard | {"two_decision_ticks_not_sustained": 18, "decision_ticks_missed_peak": 5} | 422.2 ms |

分类仅依据实际20ms分数、300ms采样及C事件。`all_frame_score_below_threshold`为窗口内分数不足；`decision_ticks_missed_peak`为采样未覆盖峰；`two_decision_ticks_not_sustained`为两次采样未共同达标。每例还保存晚于固定窗口的实际触发时间。波形结束不等于人工音素尾界，未擅改标签或阈值来消除漏检。
H3最终13次漏检均为快语速：1次窗口内分数不足、4次高峰未被判定时钟覆盖、8次未持续到连续两次确认。J4对应1/6/9次。这直接描述现有分数与触发时序的矛盾，不能简单归因为模型容量或普通语音数据量不足。
当前每组检查点仍按三秒片段验证损失选取，最终模型选型才使用完整流式指标。片段平均损失与来源/语速均衡的双确认目标是否匹配，须在下一轮以逐epoch流式证据验证；本轮未用最终测试反选epoch。
完整逐帧轨迹在`evaluation-isolated/*/*-traces.npz`，列为time_ms/silence/unknown/wake；失败例与高分持续时间见`analysis/*-temporal.json`。

## 6. 部署兼容、回滚与未完成实测

所有8个模型：TFLite 26,680B，WKM2 26,820B；输入[1,1,40,1] INT8量化(scale=2.6078431606292725, zero=-128)，输出[1,3]量化(1/256,-128)，标签顺序silence/unknown/nihao_openvela。PCAN-v2及状态张量契约保持一致。
每个候选在同一1469帧原生TFLM测试中：包校验通过、分数与参考INT8逐值一致(max error=0)、reset exact、arena_used=21,824B。流式状态另7,936B；这些是原生host数据，不是完整板端峰值RAM。host单帧中位约20–21µs，排除前端/调度且存在并发负载，不据此宣称BK7258延迟或CPU达标。
已有KWS/mining/torch/frontend回归共75个不同测试通过（第一次67通过、5个CUDA条件skip；随后补跑CUDA5项及frontend3项均通过）。模型变化未触及生产音频、按键、BLE/NFC、配网或OTA代码；这些功能尚无本轮板端回归，代码哈希/host测试不能替代实机验收。

研究候选包：nihao_openvela_J4-staged_round7.wkm（`out/wakeword-round7-20261027/selected-research/nihao_openvela_J4-staged_round7.wkm`），SHA-256 `6261152f2d8122b718f654e21db44bb7eb388811671d68a808f8e09005f85f67`。
研究目录：README.md（`out/wakeword-round7-20261027/selected-research/README.md`）；原始H3仍位于`out/wakeword-round5-20261026/selected-research/nihao_openvela_H3_round5.wkm`，A仍位于`out/wakeword-training-20261026/selected/nihao_openvela_candidate.wkm`。
本轮没有设备部署，因此回滚无需写设备；停止引用Round7候选即可。恢复实验使用独立tag，禁止覆盖原模型。

下一阶段先补经同意的真人完整目标词，按人/房间/设备隔离诊断与盲测；重点对齐open/vela音节、连读和词尾，而不是把更多普通语音当目标正例。其次优先使用现有长录音中有时间对齐的近音片段，提高每个训练窗口的信息量，并研究量化状态与时序监督一致性；不先扩容量或拉长epoch。
未来获授权进行BK7258实机验证时：核对候选及固件哈希和前端契约；以同一扬声器/距离/环境测试H3与候选并记录20ms分数、实际唤醒事件、音频丢帧和处理耗时；测峰值RAM/CPU/延迟及长时误触发；回归K2电源、K1/K3音量、BLE/NFC认领、配网和App OTA。所有板端指标当前均待测。

## 7. 复现与文件

本轮根目录`out/wakeword-round7-20261027`；源码快照`trained-source/模型-isolated`是各次实际执行版本。当前入口保留相同训练算法并增加`--tag`防覆盖复现。

```bash
cd /path/to/openvela-workspace
export PYTHONNOUSERSITE=1 TF_CPP_MIN_LOG_LEVEL=3 OMP_NUM_THREADS=2
KWS_PYTHON=out/kws-optimize-20261024/venv/bin/python
KWS_R7=out/wakeword-round7-20261027
# 在已经核验的冻结输入上，产生全新候选目录
"$KWS_PYTHON" "$KWS_R7/run_training.py" J4-staged --tag reproduce001
"$KWS_PYTHON" "$KWS_R7/native.py" J4-staged-reproduce001
# 固定策略验证，结果也写新文件；不使用最终集选型
"$KWS_PYTHON" contest2026_135_yongwangzhiqian/tools/bk7258/bk7258.py voice kws evaluate \
  --manifest "$KWS_R7/target-master/manifest.json" \
  --model "$KWS_R7/J4-staged-reproduce001-isolated/model_int8.tflite" \
  --frontend bkvoice-microfrontend-pcan-v2 --split validation --threshold-percent 60 \
  --output "$KWS_R7/reproduce001-validation.json" \
  --frozen-policy "$KWS_R7/reproduce001-policy.json" \
  --trace-output "$KWS_R7/reproduce001-traces.npz"
```

数据准备按依赖顺序：`audit_and_freeze.py`→`prepare_mobvoi_incremental.py`、`data/aishell3-incremental/prepare.py`、`data/librispeech/acquire_librispeech.py`→`canonicalize.py aishell3/librispeech`→`audit_acoustic_overlap.py`→`prepare_pool.py`→`prepare_real_features.py inherited/mobvoi/aishell3/librispeech`→`mine_real_windows.py`同四来源→`combine_features.py`→`prepare_real_evaluation.py`。新发音使用`prepare_pronunciation.py`与`prepare_master.py`；必须保留原有pre-generation freeze及additional-validation reserve，不在同目录重新随机切分。
共享主特征缓存由J0c旧数据继续训练对照首次生成，须在`combine_features.py`之前完成；其101,615条旧特征还依赖保留的R6对照缓存。数据重建不是从零替代R5/R6输入，必须保留既有合法归档、历史来源/说话人划分与H3检查点。
已有冻结产物会拒绝覆盖；以上阶段日志、命令配置和下载校验均已保留，重新准备应在独立工作区快照中执行。A3/Mobvoi复用现有归档，Libri/THCHS下载程序支持分片与断点续传；THCHS隔离规则不能因为有下载完成标志而取消。补做了三项可执行边界检查：A3冻结后解码前拒绝、THCHS完整缓存只校验不重下/不改标志、复现tag拒绝路径穿越，全部通过。
逐项入口及参数见 reproduction-commands.json（`out/wakeword-round7-20261027/reproduction-commands.json`）。最终评估命令`run_final.py 0/1`只读取选型锁并按互不重叠模型分工；现在这些测试已经暴露，后续训练应另设新最终集。

统一结果：wakeword-round7-results.json（本地未发布：`wakeword-round7-results.json`）；数值图：round7-comparison.png（`out/wakeword-round7-20261027/round7-comparison.png`）；本轮代码差异包：round7-code.patch（`out/wakeword-round7-20261027/round7-code.patch`）；训练/兼容日志、完整数据清单和全部失败尝试均保留在Round7目录。
代码补丁只含本轮新增脚本和文档，基于本地已有的未提交KWS工作，不能当作对GitHub旧默认分支的完整迁移补丁。各次实际训练后端源码快照随本地实验保留。
