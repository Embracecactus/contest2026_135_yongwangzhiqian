# BK7258 第十一阶段：R3 工具安全流式回答

> 证据可得性（2026-11-04 核验）：本文引用的工作区 `out/` 原始产物不可用，仅保留本文记录的摘要/哈希；原链接已改为路径文本，结论边界不变。见[验收原始证据留存审计](../2026-11-04-acceptance-evidence-retention.md)。

日期：2026-10-27

## 结论

本轮找到了 R2 工具回合退化的直接原因，并实现了 R3 研究候选。

- R2 在工具回合开始时先增加一次约 220 ms 的 `final/tools` admission
  请求，随后仍完整执行原有两次 planner 和一次 final stream；额外请求
  没有替代任何原步骤，所以工具回合比 R1 慢约 28%。
- R3 将第一次严格流直接作为可执行工具计划。完整工具对象验证通过后，
  仍交给原 Agent guard、registry、Session 和执行器；工具结果回注后，
  下一次严格流可以继续请求工具，也可以直接进入最终回答。
- 在 330 个受控主机回合中，R3 保留了 R2 的普通问答收益；单工具、并行
  多工具、两轮串行工具、工具失败和工具超时均不再慢于 R1。
- **默认产品仍保留 R1。** R3 只在研究配置中开启。除 bktest 的刷写和启动外，
  本轮直接使用项目已有的无唤醒启动验证链，在同一块 BK7258 上实际跑过官方
  Agent、只读工具、TTS 解码/重采样、PCM 队列和 Media。固定内存 peer 下，
  普通 R3 回合的 ASR-final→Media 首写入诊断中位数为 522 ms，R1 单次基线为
  648 ms（-19.4%）；两个工具回合分别为 768/784 ms。该证据仍不包含真实 MiMo、
  DNS/TCP/TLS、板端 RAM/CPU 或 DAC/声学首音。

没有训练或修改 KWS，没有改 H3、默认唤醒阈值、Session/Media Owner、工具
权限、人物设定、历史或回答长度。实板补测临时刷入 R3 后已恢复原产品
`0.7.111+757` 并核验身份；没有提交或推送。

## 冻结状态

- 第十阶段 51 项证据的 manifest 逐项复核通过；manifest SHA-256 仍为
  `bd80f9b9fed20444b18288b76b0e8e070386ef9bbfbd2abde5fd30f7d61b2161`。
- H3 模型 SHA-256 仍为
  `8da783b962327e3cd5f1bd45b84c6e27c3dbfd8cb58b372acf6155fbbe66234d`。
- 正常 `openvela_ap` 保持 `CONFIG_AI_AGENT_VOICE_ROUTE` 和
  `CONFIG_AI_AGENT_VOICE_STREAM_TOOLS` 关闭；R1 的 early Media prepare 保持开启。
- R3 只在 `openvela_ap_bktest` 和本轮新增的独立
  `openvela_ap_audio_validation_r3` 研究配置中启用两个实验开关。

## P0：R2 工具回合退化根因

所有时间均从固定 ASR 文本进入 Agent 的 T4 等价边界开始，单位为 ms。
`first_media` 是主机 Media 写入完成代理，不是 DAC 或人耳首音。

### 单工具回合 P50 时序

| 阶段 | R1 | R2 | R3 |
|---|---:|---:|---:|
| LLM 首个事件 | 无流式事件；planner 整包返回 200 | 220（首事件即 phase） | 200 |
| 确认 tools phase | 不适用 | 220 | 200 |
| 第一个工具调用片段 | 不适用（planner 整包） | 不适用（planner 整包） | 200 |
| 工具参数完整 | 200 | 421 | 221 |
| 工具开始/结束 | 200/201 | 421/421 | 221/221 |
| 工具结果进入 Agent | 201 | 421 | 221 |
| 最终回答首内容 | 661 | 882 | 482 |
| 首个可播报片段 | 662 | 882 | 482 |
| TTS 首 PCM | 693 | 913 | 512 |
| 首次 Media 写入 | 776 | 997 | 597 |

R1 的工具链是：

`planner(tool) -> execute -> planner(agent_finalize) -> final SSE`

R2 变成：

`admission(tools, 220 ms) -> 原 R1 全链`

因此 R2 的工具参数完整、最终内容和 Media 首写入均整体后移约 220 ms。增量
JSON 解析、TTS 和 Media 没有形成这一固定差值；退化来自额外串行模型请求。

R3 的工具链是：

`strict stream(tool plan) -> execute -> strict stream(final answer)`

两轮串行工具则是三次严格流、两次实际工具执行，不再需要 admission 后的
原生 planner，也不执行伪工具 `agent_finalize`。

## R3 实现

### 工具安全协议

R3 使用两种互斥、字段顺序固定的 envelope：

```json
{"voice_phase":"final","answer":"..."}
```

```json
{"voice_phase":"tools","calls":[{"id":"...","name":"...","arguments":{}}]}
```

安全边界如下：

1. `final` 的精确前缀确认后，只有解码后的 `answer` 字符串进入既有
   reply stream；协议头、reasoning、JSON 转义和工具字段不进入 TTS。
2. `tools` 阶段不产生 `AGENT_REPLY_BEGIN`，完整 envelope 在有界存储中结束后
   才解析；只接受 1–2 个调用、唯一且有界的 ID/名称、JSON object 参数、
   精确字段和无额外根字段。
3. 结构验证不等于授权。验证后的调用仍走原来的重复 ID 检查、tool guard、
   registry、checked cancellation、工具结果 history 和 Session 所有权。
4. 工具结果回注后，下一轮使用同一协议；可能继续调用工具，也可能进入
   `final`。工具参数、工具结果和错误正文不会直接播报。
5. 在正文开始前出现未知前缀、不完整工具 JSON 或断线，可安全回退既有
   structured planner；正文一旦开始，错误只中止本回合，不能回退并重复播报。
6. tool envelope 总尾部上限 16 KiB，单个 arguments 上限 8 KiB；SSE、UTF-8、
   Unicode 转义和回答正文继续使用原有有界解析器。

### 实际代码改动

- `packages/ai_agent/Kconfig`
  - 新增默认关闭且依赖 R2 route 的 `AI_AGENT_VOICE_STREAM_TOOLS`。
- `packages/ai_agent/src/llm/llm_proxy.[ch]`
  - 新增工具安全 strict stream API、阶段事件和完整工具 envelope 校验。
- `packages/ai_agent/src/core/agent_loop.c`
  - 首轮和工具结果后的后续轮均可使用 R3；复用原执行、权限、历史与取消链。
  - 增加不含正文/参数的数值阶段：首事件、phase、工具片段、参数完整、结果回注。
  - 修复工具阶段取消在尚未开始正文时没有向上回传负错误码的问题。
- `packages/ai_agent/src/voice/voice_channel.c`
  - 在既有分句器确认完整可播报片段时记录 `speakable_ready`。
- `boards/.../openvela_ap_bktest/defconfig`
  - 在 bktest 打开 R3；正常产品配置不变。
- `boards/.../openvela_ap_audio_validation_r3/defconfig`
  - 只为无需唤醒的启动 HIL 复制产品等价验证配置并打开 R3，不参与默认产品。
- 主机测试和 `tools/bk7258/voice_latency_round11_benchmark.py`（2026-11-04 移至 `tests/host/bk7258/`，测量逻辑不变；重跑写入新的时间戳输出目录）
  - 覆盖字节级分片、工具安全、失败/超时/取消/断线、并行与串行工具及性能。

## 受控性能对照

每个单元格为首次 Media 写入代理的 `P50/P95`，单位 ms。

| 场景 | 回合/变体 | R1 | R2 | R3 | R3 相对 R1 |
|---|---:|---:|---:|---:|---:|
| 普通问答 | 30 | 815/817 | 375/377 | 375/377 | -54.0% / -53.9% |
| 受控弱网 | 10 | 1327/1332 | 678/682 | 674/681 | -49.2% / -48.9% |
| 单工具 | 30 | 776/790 | 997/1014 | 597/609 | -23.1% / -22.9% |
| 同批并行双工具 | 10 | 776/779 | 997/1008 | 597/608 | -23.1% / -22.0% |
| 两轮串行工具 | 10 | 978/987 | 1199/1246 | 816/837 | -16.6% / -15.2% |
| 未注册工具失败 | 10 | 778/807 | 997/1007 | 597/604 | -23.3% / -25.2% |
| 工具超时结果 | 10 | 777/786 | 1001/1027 | 596/609 | -23.3% / -22.5% |

R2 相对 R1 的工具回合变化：单工具 +28.5%/+28.4%，并行双工具
+28.5%/+29.4%，串行工具 +22.6%/+26.2%，工具失败 +28.1%/+24.8%，
工具超时 +28.8%/+30.7%。R3 消除了该固定退化。

完整播放结束代理 P50/P95：

| 场景 | R1 | R2 | R3 |
|---|---:|---:|---:|
| 普通问答 | 932/936 | 818/820 | 818/820 |
| 弱网 | 1552/1564 | 1327/1336 | 1328/1333 |
| 单工具 | 895/906 | 1115/1133 | 715/726 |
| 并行双工具 | 893/899 | 1116/1128 | 715/727 |
| 串行双工具 | 1097/1106 | 1316/1365 | 933/955 |
| 工具失败 | 896/924 | 1115/1126 | 715/721 |
| 工具超时 | 896/904 | 1119/1146 | 714/728 |

这些数字来自受控 provider/tool/TTS/Media peer，不包含 ASR、DNS/TCP/TLS
真实服务时间，也不代表物理 T0→T10。

## 正确性与回归

- 330/330 个 R1/R2/R3 受控回合通过。
- 每回合最终正文长度保持一致：普通 103 B、工具 39 B；长回答另行验证
  162 B，不通过缩短回答制造收益。
- 每回合 PCM 为 16384 B，逐字节顺序验证；只 open 一个 Media handle；
  history/正文只提交一次。
- strict parser 对每一种分片大小运行；拒绝空调用、超过两调用、重复 ID、
  缺 ID、字符串参数、字段乱序、额外字段、截断对象和 final/tools 混合对象。
- R3 完整链路通过：正常工具、两工具并行、两轮串行工具、未注册工具、工具
  超时、工具流不完整、连接中断、工具片段阶段取消。
- 既有 mixed/finalize 变异测试仍能阻止 `agent_finalize` 被执行为真实工具。
- 视觉 checked 路径成功/取消通过；内建视觉和 provider 工具在取消边界均返回
  `-ECANCELED`，不记录 guard success、不留下输出。
- Agent SSE、TTS queue、Voice、Media、FunASR、capture/recorder、TLS 并发与
  取消、TTS isolation、cloud config/request/http 共 13 个相关 host target 通过。
- HIL 日志/时序解析 28 项通过；响应长度产品路由与持久化测试通过。

## 资源与构建

### 主机资源

普通问答 RSS P95：R1/R2/R3 = 1900/1912/1912 KiB。工具场景 R3 的多数
RSS P95 为 1904 KiB；工具失败一次达到 2096 KiB。`/usr/bin/time` 的 CPU
粒度过粗，P50 多为 0 ms。这些仅说明主机回归没有持续增长或队列失控，不能
作为 BK7258 RAM/CPU 数据。

### BK7258 构建

| 配置 | CP | AP | Manifest |
|---|---:|---:|---|
| 产品 R1 | 1,160,420 B | 1,973,904 B | PASS |
| bktest R3 | 1,161,746 B | 1,992,842 B | PASS |

- 产品 AP SHA-256：
  `8d25ab96d39ecf4068f010164ce98372146d5bfbf7351b58c7bf979246ef507f`。
- bktest AP SHA-256：
  `3d7f7b0f77b71a8a1b228b1a6ae5b5eb4fa82fce6030ca6895323276c66550ff`。
- 相比第十阶段：产品 AP +306 B（共享阶段记录/错误传播），bktest AP
  +1,938 B；两种 CP 均无变化。固定分区 `pair.bin` 不能表示实际代码增量。
- 当前 bktest AP ELF：text 1,836,920 B、data 38,480 B、bss 197,424 B。
  该配置还包含其他 engineering 功能，不能用它与产品 ELF 的差直接归因 R3。
- 两套 direct 产物本身均为未签名构建证据。实板补测只将 bktest R3 通过
  同板已验收 8 MiB 基底物化为设备绑定诊断镜像，测试后恢复原产品镜像。

## 实板补充验证

连接设备确认为 AIDK AI Toy：COM9 为 CH340 控制台/下载口，COM16 为原生
USB 控制口。所有 UART 原文、profile 和回答内容均留在本机私有证据；此处只
记录哈希、计数、阶段和错误码。

### R3 刷写与启动

- 使用同一物理板的已验收 8 MiB readback 物化 R3；完整镜像为 8,388,608 B，
  SHA-256 `78563e9a6532331423c970145dd89605f6e534839f51382bb1cbc1b689c68ee0`。
- 下载器固定 COM9、460800、CH340、地址 0、软件复位、禁止 RTS/DTR；
  `Gotten Bus`、擦除、写入、`Writing Flash OK` 和最终完成标记全部通过。
- 新启动返回 `NuttX 0.0.0 892be7ba7dc arm aidk_ai_toy`，诊断命令存在；
  串口捕获 SHA-256 为
  `5a42646bd01eadf1a9905cb0c7555edbfc7e5c02ad7c20e79bfc0fe8009ce3cd`。
- AIDK 使用 `device-firstboot` 发布策略时，交付生成器会产生
  `requires-device-firstboot`，而验证器此前漏接纳该合法状态。本次一行修复
  验证集合并补了回归，相关 2 项定向测试通过。

### 声学与工程控制边界

- 板端真实麦克风/KWS 任务持续工作：两次有效 Windows 播放尝试期间共记录
  14 条输入进度，无任务错误；但没有 KWS candidate 或两次确认。
- 第一次声音送到默认耳机/第二输出，第二次有效声音送到 HDMI 显示器音频
  端点。两者都不能证明空气声实际到达板旁；另一次 WDM 主扬声器打开失败，
  在发声前即排除。因此 0 次唤醒只表示本次声学耦合未建立，不能作为 H3
  召回失败或通过证据。
- 当前设备已认领。一次性 factory diagnostics 注册被板端以 `-EACCES`
  拒绝；现有本机 PC profile 对 engineering status/text benchmark 返回
  `-ENOTSUP`。没有解绑、清身份、重配 owner 或绕过权限。
- COM16 固定文本 benchmark 没有执行有效回合，UART 中 `voice-stage` 为 0；
  现有 engineering profile 的拒绝结果仍然有效，没有通过改变 ownership 绕过。
  随后改用项目自身的启动自动验证入口完成了无需唤醒的板端回合，见下一节。

### 无需唤醒的启动直测

项目已有 `openvela_ap_audio_validation` 配置，会从
`product_pipeline_validation()` 自动进入 `bk7258_agent_audio_validation.inc`。
它在实板上实际走麦克风 start/stop 生命周期、官方 Agent worker、现有工具
registry、云适配层的 SSE/TTS 解码、24 kHz→16 kHz 连续重采样、PCM 队列和
Media playback；ASR 文本和服务响应由确定性的内存 HTTP peer 提供，所以无需
唤醒、网络和账号。它是板端软件链 HIL，不是在线 MiMo 或声学验收。

为保持产品 R1 不变，本轮新增独立研究配置
`openvela_ap_audio_validation_r3`，只在验证镜像中打开 R2 route 和 R3 tool
stream。R1/R3 均使用相同前端、Agent、TTS、Media 和启动测试数据。

| 实板路径 | 有效回合 | ASR final→首次 Media 写入 | 说明 |
|---|---:|---:|---|
| R1 产品等价启动验证 | 1 次正常 + 1 次取消 + 1 次恢复 | 正常 648 ms | `decision=0`，取消返回 `-ECANCELED` 后可恢复 |
| R3 final-only 启动验证 | 三次启动各 1 个首正常回合 | 519/522/526 ms，中位数 522 ms | 相对 R1 单次基线 -19.4% |
| R3 只读工具回合 | 两次独立启动各 1 次 | 768/784 ms，中位数 776 ms | 每次只执行一次 `device_status`，再进入 final |

R3 工具回合的阶段分解如下；单位为 ms，两个样本仅作诊断，不能报告 P50/P95。

| 阶段 | 实板 1 | 实板 2 |
|---|---:|---:|
| 第一流请求→确认 tools phase | 200 | 185 |
| 工具执行 | 30 | 19 |
| 工具结果回注→第二流请求 | 1 | 2 |
| 第二流请求→首内容 token | 212 | 211 |
| TTS 提交→首 PCM | 152 | 155 |
| 首 PCM→首次 Media 写入 | 148 | 145 |
| ASR final→首次 Media 写入 | 784 | 768 |
| ASR final→playback end | 1642 | 1625 |

两次工具回合均满足：工具调用完整后才执行、工具结果只回注一次、final 在工具
结果后开始、工具参数/结果不进入 TTS、PCM 大小与哈希匹配、Media 尾部屏障在
首写入后才释放。两个工具启动还各自跑过正常、播放后取消和取消后恢复会话。
连同 R1 和 final-only R3，共 14/14 个启动回合通过，其中 4 个取消回合均干净
回到 idle；没有 HardFault、assert 或 panic。

第一次 R3 启动验证曾以 `-EPROTO`（`-71`）失败：旧 fixture 输出普通 SSE，
不满足 R3 严格 `voice_phase` envelope。运行时按设计失败关闭，没有误播；将
fixture 改为与 R3 相同的 `final/tools` 协议并补齐工具/最终两流计数后，连续
两次工具启动验证通过。该失败被保留为协议不兼容证据，不计入通过回合。

最终验证 AP 为 1,990,020 B，SHA-256
`697f61a1bad04028aafc427a5487a1a9947142636b14b2858978bc10e19aa51f`；
实板完整镜像 SHA-256 为
`21adddf99ea356a48ae0378db2965b4ffdfb93747d9566039b2b1d88e8d7b794`。
两次工具启动串口捕获 SHA-256 分别为
`8329061484040a58dd505497ae15fa14ce2cc9f4f724f672f06258dd1376778e` 和
`c2e64ee798502bb9ab71730cff7a66a5f62f43178556c8eb32b97086da8e27df`。

这里的“首次 Media 写入”只是播放器消费链代理，不等同于 DAC 开始输出或人耳
听到首音；内存 peer 也没有覆盖真实 DNS/TCP/TLS/MiMo 延迟与协议遵循率。
机器可读的脱敏阶段结果见
`out/voice-latency-round11-device-20261027/direct-pipeline-hil-results.json`。

### 恢复

- 恢复镜像版本 `0.7.111+757`，8,388,608 B，SHA-256
  `b9790093962836d1ef841e4a4f9dfefa4602da8a6f4d033b95600f4dee900be2`，与测试前
  内核基线匹配。
- 首次恢复启动下载器时发生 WSL/Windows interop timeout；证据确认没有获取
  loader 总线、没有擦除或写入，且板子仍运行 R3。改为 Windows Python 直接
  启动同一受控下载器后进行一次恢复重试，全部下载器成功标记通过。
- 恢复后独立 UART 返回 `NuttX 0.0.0 5ecdd18b230 arm aidk_ai_toy`；设备身份
  仍存在（539 B），bktest diagnostics 不再存在；`bkhealth` 返回 operation 0、
  充电中、4092 mV，无错误标记。
- 最终一次恢复下载重新验证全部五个成功标记；独立启动捕获 720 B，SHA-256
  `48337e0edb55eda9095c98b897b7aaa7d49acb2bdf20395b221e5f655b18e36d`，
  未出现 `BKPIPE`、HardFault、assert 或 panic。随后状态捕获 SHA-256 为
  `69b172f7623c0fec8970a56e82a7a13429aee1a9510d2fc71cd137a222707279`。

原声学/控制口机器摘要为
`out/voice-latency-round11-device-20261027/device-hil-results.json`；无需唤醒的
直测摘要为
`out/voice-latency-round11-device-20261027/direct-pipeline-hil-results.json`。

## 选型判断

R3 达到了本轮主机研究 Gate：

- 普通问答和弱网保持 R2 的大部分（本次为全部）首写入收益。
- 所测工具回合均不慢于 R1，且没有跳过工具、权限、结果回注或最终回答。
- 未发现正文遗漏、乱序、重复、串轮、PCM 截断或额外 Media Owner。
- INT/JSON/SSE 异常、取消和工具错误均有失败关闭或安全回退证据。

但 R3 **尚不进入默认产品配置**，原因是：

1. 项目无唤醒入口已证明 R3 严格 envelope、只读工具执行、结果回注、TTS 和
   Media 能在 BK7258 上按序工作，但内存 peer 不能证明真实 MiMo 的协议遵循率、
   错误率和线上 P50/P95。
2. 已有实板队列时序和取消/恢复证据，但没有板端峰值 RAM、CPU、真实网络回合、
   DAC 或声学首音数据。
3. 实板已验证一次只读 `device_status` 工具每回合只执行一次；真实服务下的工具
   选择质量、多工具、视觉权限、失败重试和长会话仍未验证。

因此当前建议仍是：保留产品 R1，将 R3 留作获得合法 diagnostics grant 后的
真实 MiMo 灰度对象；通过后再单独进行默认配置评审。

## 回滚

最小回滚不修改源代码：从 bktest defconfig 删除或关闭
`CONFIG_AI_AGENT_VOICE_STREAM_TOOLS`，即可回到第十阶段 R2；再关闭
`CONFIG_AI_AGENT_VOICE_ROUTE` 则回到产品 R1。正常产品本来就是 R1。

源码级回滚使用交付的 Round11 scoped patch：

```sh
git -C packages/ai_agent apply -R \
  ../../out/voice-latency-round11-20261027/round11-ai-agent.patch

git -C contest2026_135_yongwangzhiqian apply -R \
  ../out/voice-latency-round11-20261027/round11-bk7258.patch
```

工作区原本有大量未提交成果，执行源码级回滚前必须先检查 patch；不得用
`git reset --hard` 或 `git clean`。

## 下一步实机验证

1. 使用未认领工程板，或由当前设备 owner 显式颁发 diagnostics grant；不为
   测试解绑、清除身份或放宽 factory gate。R3 的同板物化、刷写和恢复路径已验证。
2. 用真实 MiMo 对普通、弱网、单工具、并行工具、串行工具、视觉权限、工具
   失败/超时/取消各至少 30 回合，统计协议遵循率和回退率。
3. UART 记录匿名 request ID 和数值阶段；正文、参数、结果、密钥和音频不落盘。
4. 用同时钟 GPIO/codec 或对齐声学录音标注 DAC/物理 T10；不能用
   `first_media` 冒充可听首音。
5. 测量板端峰值 RAM、线程栈、CPU、队列欠载、音频完整性和连续多轮恢复，
   再决定是否进入默认产品。

机器结果见 voice-latency-round11-results.json（本地未发布：`voice-latency-round11-results.json`）。
原始数值、日志、构建 manifest、patch 与完整命令见
`out/voice-latency-round11-20261027/REPRODUCE.md`。
