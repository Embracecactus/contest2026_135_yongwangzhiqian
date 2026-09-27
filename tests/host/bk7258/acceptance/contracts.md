<!-- SPDX-License-Identifier: Apache-2.0 -->
# 傻妞 v2：需求契约与测试审阅入口

日期：2026-09-24。范围：T0–T2，基于主仓 `272b3b2f366cf9ac9ae510757ac4288f0c68d3a0`，
Agent `62a304ea69c4076f0f3ef7955a9af69a5ed35277`。这是测试交付，不是产品实现或实板验收。
用户已取消“不新增测试文件”的限制；仅测试、夹具、说明及测试运行配置可变。
业务切片须待本次审阅放行；放行后按切片推进，不逐小提交重新请求实施许可。

## 需求来源与目录

- `USER-20260924-PLAN-V2`：用户《需求契约与测试先行实施计划 v2》，决定测试方法、
  禁止副作用、保留原模型/我在/原生 Canva/独立设备、M0–M5 与 N1–N3范围。
- `USER-20260924-CASES-V1:<ID>`：用户随后提供的 56 项附录；
  [cases.v1.json](cases.v1.json) 是其结构化压缩，保留全部原 ID、Given/When/Then、
  禁止行为、观察器、层级、优先级。它是规格，绝非执行结果。
- 兼容协议依据：基线公开头 `app/bk7258/bk7258_provision_config.h` 的 SCP1 和
  `bk7258_control_session.h` 的 SDC1。既有协议用于链接当前路径；三个独立云端点、
  USB 产品协议与场景命令仍是待审合同，不能将现有编码器的字节自动批准为新协议。
- 结果依据：每次运行的 `out/shaniu-contract-v2/results.json`、逐例日志与 JUnit XML。
  历史两项失败未删除、xfail 或放宽；完整设计数量、执行单元数量和通过数量分别报告。

下面的映射中，每项需求包含正常路径及异常/禁止副作用；每例正/反向设计在 JSON 的
`design_variants` 中展开。尚未执行的设计变体不加入运行分母。

| 需求 | 正向用例 | 关键反向/异常用例 |
| --- | --- | --- |
| Q01 电源/稳定/启动 | K2-01、PWR-01、BOOT-01 | K2-02/03、LIFE-01/02、FAULT-01、CROSS-01 |
| Q02 回答/采集/响应成本 | CAP-01、ASR-01、AGENT-01 | DISP-01、CAP-01取消、ASR-01乱序 |
| Q03 工具与视觉 | AGENT-02合法工具、AGENT-03下一轮 | AGENT-02无效批次/缺ID、AGENT-03超时 |
| Q04 音频供给与结束 | AUD-01、AUD-03正常尾部 | AUD-02、AUD-03取消迟到、AUD-04、NET-04 |
| Q05 原唤醒 | KWS-01、KWS-02正例 | KWS-02负例/隔离、KWS-03旧帧 |
| Q06 我在与交接 | CAP-01、KWS-01 | CAP-01应答残音、KWS-03迟到rearm |
| Q07 重置与认领 | RST-01确认新认领 | K2-02取消、RST-02中断、NFC-02拒高风险 |
| Q08 配置/离线/资源 | CFG-01、MSC-01正常、NET-01 | CFG-02/03、STORE-01/02、MSC-01/02失败、NET-02/03 |
| Q09 原生 App | UI-01/02/03/04正常交互 | UI-01过期、UI-02重建、UI-03错身份、UI-04错签名 |
| Q10 安装/交付 | OTA-02、RES-01/03、DEL-01/02 | OTA-01、RES-01非法包、USB-01越权、DEL-01错身份 |
| N1 本地动作 | MOT-01/02 | MOT-01错误采样、MOT-02自激振动、MOT-03禁用阶段 |
| N2 场景/卡片 | RES-02、TIMER-01、NFC-01 | RES-02旧恢复、TIMER-02未知时间、NFC-02权限 |
| N3 电脑协作 | RES-03、USB-02、PC-01 | USB-01恶意帧、USB-02慢客、PC-01乱序过期 |

## 独立期望、输入与参数冻结

| 参数 | 来源/状态 | 本轮测试选择及限制 |
| --- | --- | --- |
| T_off | 用户确定：3000ms | 2999/3000/3001，已消抖的可信单会话边沿；不是原始电平，更不证明物理未松手 |
| 会话/时钟 | 用户K2-03 | 新会话须新有效按压；时钟倒退不产生资格。信号丢失/来源不可信策略仍待完整输入接线，不从时间差推断物理连续 |
| T_reset、T_confirm、确认手势 | 待产品审阅 | 符号参数；只冻结互斥/取消/零副作用，不抄当前门槛 |
| T_quiesce、T_control、preview TTL、队列上限、抖动包络、动作去重、触觉脉冲/冷却、PC TTL/速率 | 待资源测量与审阅 | 本轮不更改运行阈值；主机测试45秒进程看门狗、构建180秒只是测试环境保护，不是产品SLA |
| TIMER-01 | 用户数学例 | 单调60秒，运行10秒/暂停5秒/续50秒；完成一次；墙钟跳变不影响。重复start同operation返回原任务，不新建计时器（待接口评审） |
| 跨掉电计时、不可取消提交、断连查询 | 待产品审阅 | 见下方输入/结果/取消/恢复合同。未知不得当成功；不承诺关机唤醒 |
| 性能/质量 | 用户 v2 第9节 | 本地就绪p95≤5秒目标；首句改善30%、p50≤3秒/p95≤5秒目标；唤醒≥95%、关键组≥90%、FAR≤0.5/h；本地反馈p95≤150ms；未实测 |
| 音频数据 | 确定性测试夹具 | 字节i取i%251，共200000字节，24kHz单声道PCM16；分块seed=1/20260924/4294967295，LCG系数1664525/1013904223、模2^32。只覆盖合法PCM分块，不泛化成任意网络抖动 |
| SCP1 golden | 公开布局独立构造 | operation=02+15个零；revision=1；flags=9；UTC=1800000000；长度9/10/0/0；零地址；network-B/password-B。71字节，共用 `android/.../src/test/resources/shaniu/scp1-wifi.hex` |
| 时钟字节 | 当前App真实墙钟 | JVM独立校验UTC处于调用前后区间，仅归一化该8字节再对比golden；不改生产时钟/代码 |
| CA/Key | 仅测试 | 公共CA取固定工作区mbedTLS `tests/data_files/test-ca.crt`并转DER；不读取对应私钥；测试Key仅内存比对，日志不输出请求/秘密。CA与输入文件SHA256见manifest |

性能测试前固定设备/固件/模型/前端/资源哈希、服务/模型/工具、提示和历史、音频输入、
声学摆位、音量/供电/网络、冷热组、样本数、超时定义。百分位拟用 nearest-rank
（排序后第ceil(p*n)项）；此分析约定待审阅，不修改已有评价口径。失败/超时单列并保留
总分母，不能删除；普通问答至少30轮是描述性回归。没有声学采集只能报告Media代理指标。
KWS按来源/说话人先划分再增强；评价后调参的数据不再盲测，16/18不得报≥90%。

## 执行边界与待接口绑定

下表为 `cases.v1.json` 中 `binding` 的解析表。输入/结果是语义合同，**不是已新增API**。
绑定只能调用真实产品入口；薄适配器只转数据、调度与观察，不实现被测状态机。
安全取消均遵循：请求不等确认；提交前确认后终态不再回跳；提交后不可取消则明确返回
不可取消或结果未知并允许查询。断连不能盲目重放副作用。关联ID必须含设备/会话/operation。

| binding / 绑定位置 | 输入、结果、取消与恢复语义 | 当前可执行范围与缺口 |
| --- | --- | --- |
| keys / `bk7258_product_keys.h` →产品意图入口 | 可信session、消抖mask、单调ms→意图计数和时机；会话改变取消旧资格；恢复需新按压 | 新独立进程测7条序列；旧失败原样保留。未观察真实USB/配网副作用；K2-03完整输入可信性仍未验 |
| power / `bk7258_agent_product.c`、`bk7258_pm_soft_off.c` | 关机/恢复候选/参与者退出事件→有依据的完成/失败；确认互斥；部分停止恢复须重获资源 | 统一参与者握手与观察接缝待接口，GPIO/CP/IRQ/电流待设备；不添加假生命周期实现 |
| reset / `bk7258_provision_storage.c`、owner/control服务 | 授权+物理确认+operation→持久撤销/回执；拒绝与未知区分；重启恢复回执而非旧权限 | 既有storage/owner测试可复用；完整鉴权+重置+新认领仍NOT_RUN，不能由存储测试关闭RST |
| config / `bk7258_provision_config.c`、storage、settings、cloud HTTP | SCP1→持久回执、SCS1；仅Wi-Fi不改云；旧revision拒绝；保存/应用分开；网络失败后重开仍持有原配置 | CFG-01用真实合并、fsync/存储线程、重开、解码、HTTP/webclient生成Authorization；只替换TLS I/O，没有真实HTTPS/TLS认证或Wi-Fi、AP启动。独立ASR/LLM/TTS端点和desired/applied双版本还待schema/接口 |
| store / `bk7258_provision_storage.c`、`bk7258_provision_store.c` | 原介质+写入/同步故障→旧/新有效或明确恢复态；durable前不得确认；刷新不将IOERR当空 | 不用内存成功mock代替POSIX存储。本轮未注入每个持久边界/真实掉电；STORE-01/02不能计完整PASS |
| volume / `bk7258_media_volume.c` + `bk7258_usbmode.c` | 本地占用/维护请求/卸载结果→独占主机导出；失败保留阻挡；主机释放后本地可再获租约 | 真实两个模块链接，mount/umount和USB硬件边界替身；独立mounted/host-writable观察器。当前本地OTA挂载只读，未证明数据库写句柄/DMA排空、实际FS或旧缓存epoch |
| audio / Agent `voice_channel.c`、`llm_stream.c`、Media adapter | PCM/文本事件、EOF、request-id、cancel→真实队列到sink字节与完成；旧回调拒绝；下一轮独立 | 现有include真实源码的队列夹具，初始化后置条件由fixture建立，不覆盖采集/完整Agent调用；真实队列/解析/历史与外部TTS供给、Media sink替身。PCM变分片、SSE已接入、取消旧回调；Base64/TCP变化及硬件drain尚未绑定 |
| agent / `packages/ai_agent/src/core/agent_loop.c`与guard/后端 | peer响应/工具批次→独立副作用账本、完整历史/终答；无效批次先拒；未知非幂等不重试 | 未编造替代Agent；正式工具账本与取消注入需要在现有Agent测试接线，AGENT-01/02/03仍NOT_RUN或待设备，不把SSE parser测试算整个Agent |
| session / 原生 `DeviceControlSession`、`DeviceControlProtocol`、设备control服务 | 认证会话+命令/结果序号→串行写、独立状态/结果；掉线查询补快照；旧代际丢弃 | 既有Session回归实跑；新增OTA接真实协议编解码。外部已认证Transport夹具不是TLS/BLE证明；手机+USB共用服务/短控预算待接口 |
| ui / 既有 `DeviceUiAcceptance`、真实Editor/Session | 用户操作/生命周期/字体键盘→可达控件/草稿/协议副作用；取消不更改设备；重建不重复事务 | 本轮不运行instrumentation、不安装APK；模拟布局与Mi10证据分列，BLOCKED_DEVICE |
| ota / `OtaControlUpload`→Session→Protocol→设备OTA | source record、ACK/取消→受理/待确认/确认取消/设备安装结果；终态不受旧ACK改写 | 对象原失败复跑；真实Session与协议的合法分片ACK和迟到拒绝可运行；协议拒旧帧可能关闭连接，不等设备故障；提交后不可取消及重启确认待设备 |
| resource / 既有display pack/store与control安装入口 | 包/签名/目标/分片/operation→校验/安装/激活job；失败保留旧包；取消/断连可查 | 统一异步安装job接口与源错误域待绑定；不得假造安装器；新接口须列不可取消commit边界 |
| scene / 现有display/motion/nfc服务 + 后续有限场景入口 | 预览只在手机；trial(expression,operation,TTL)、set-default(expected-revision)、timer(action,operation,duration)、event(task,event,expiry)→job/可见渲染确认/默认版本；旧trial恢复只作用于它仍拥有的临时状态 | 共用场景入口待实现，BLOCKED_INTERFACE；重复operation返回原结果，重复事件零副作用；取消结束本次临时拥有，不回滚更新默认B；限时参数、抢占/断连恢复策略待审。后续接现有显示/配置/采样者，不建新永久线程作测试替身 |
| motion / `bk7258_motion_core.c` →上述scene | sequence/timestamp/sample/error→只可信新样本；失败零旧值；无owner/电源副作用 | 旧core失败清理回归实跑；timestamp可信性、动作识别与去重待scene，不把whole legacy程序当所有动作case通过 |
| nfc / `bk7258_nfc_core.c` →上述scene | 卡型/出现/离开/读错→低风险场景；驻留去重、移开重进再触发；UID无授权能力 | 旧core错误清理实跑；场景事件/驻留窗口/兼容卡能力待接口，真实读卡待设备 |
| usb / `bk7258_usbcdc.c` + 后续设备公共服务/网页客户端 | 身份能力/auth独立于开端口/DTR；长度/版本/session/seq/operation/payload→有界帧结果；cancel/status有优先预算 | 稳定产品帧schema、授权、分包流控/查询入口待接口；接原生CDC，不能替换CH340/Shell或维护MSC；断连回查，不无条件重放 |
| kws / 现有KWS采集/资源选择与数据清单 | 原组合哈希+带来源标注音频→采样索引/唤醒事件；重排不改内容绑定；停用后旧帧/旧rearm无效 | 当前实际设备原模型/应答哈希未读，BLOCKED_DEVICE；不读取私有语料、不训练、不改阈值。来源隔离/顺序性质后续接现有评价器，不生成假成绩 |
| delivery / 现有构建签名与package检查 | 固定源码/合法本人签名/资源→独立校验产物、同板数据物化、首次认领；失败保留恢复入口 | 本轮仅证明测试差异与源码身份；不生成身份、不构建/刷写正式固件。授权范围和实际硬件身份另核；UNKNOWN不得伪造通过 |
| cross / 上述真实入口的事件序列 | 预先固定seed/时间线/已启用能力→资源账本和最终态；取消终态不重开；退出真实完成 | 只有单项安全合同满足再接组合；24h需设备，本轮无长测；所有N尚未绑定时不可空跑称稳定 |

## 六个重点用例的独立判据与缺口

1. K2-01：计数在返回意图处；没有读取 `power_request_latched` 作验收标准。
   原用例不改，新阈值正/负各独立进程。物理开关机另属PWR-01。
2. OTA-01：原对象失败与L2路由保护同时保留。先等取消ACK再检查终态；仅请求取消时必须WAITING。
   公共SDC1响应头由测试peer按契约独立编码，逐3字节喂给真实parser，不直接塞Snapshot冒充整条协议。
   另一个用例专测Transport迟到callback经过真实Session过滤，明确其较窄层级。
3. CFG-01：先持久保存测试owner/Key/CA；真实合并SCP1只改网；真实HTTP失败后停止/重启存储，
   再真实解码并请求peer核验Authorization。真实TLS建立、实际Wi-Fi联网与全产品重启仍未证明。
4. MSC-01：生产租约和模式不能都mock；只有OS/USB边界可控。卸载失败后尝试导出，
   观察器断言mounted与host-writable不同时为真；尚不宣称完整FS/数据库/DMA一致性。
5. AUD-03：每个PCM字节对独立公式核验，共200000字节，EOF后drain/close一次；取消旧回调不得
   增加新轮TTS内容。生产回调/队列真实，Media sink是观察器，不是目标板出声/DMA证明。
6. RES-02：必须观察持久默认版本、实际渲染确认与写计数；默认D→试A→默认B→旧A超时应仍B。
   语义已写入scene绑定；没有调用接口就BLOCKED_INTERFACE，不能让JSON播放脚本自己模拟成功。

## 测试有效性与结果分型

`make -C tests/host/bk7258 run-shaniu-contracts` 是唯一新增测试集合入口，底层仍使用既有
Make、Python unittest、Gradle/JUnit；不修改固件构建或生产平台。运行器逐C进程与逐JUnit
case收集时间/退出码/原日志；旧C main仅计一个 `legacy-suite` 执行单元，中途abort后不推测后续断言。
`FunctionTestCase` 为已有unittest适配，不是新的设备测试平台。

- PASS：该执行单元的实际断言通过；只覆盖标注层级与范围。
- FAIL_ASSERTION：真实业务断言失败，测试退出非零；已有Red不转换成PASS。
- SETUP_ERROR：编译/依赖/夹具前置条件失败或未获得有效测试结果，不是产品回归。
- BLOCKED_INTERFACE：合同已写，但生产入口/观察接缝仍缺。列出上表绑定位置，不永久留一句待接口。
- BLOCKED_DEVICE：真实设备/仪器/物理操作未完成；不进主机通过率。
- NOT_RUN：已有或计划中的场景未执行；JUnit skipped仍算NOT_RUN，另保留XML。

设计项的总体状态与collected子项分开。父项需要L3即使若干L1/L2通过仍未完成；同时列出其他
待接口/未执行缺口。不将实际收集单元数当56项覆盖率，也不把变异检出算产品通过。

两项单错误变异只写入临时目录、编译临时产物：

- 卸载失败仍提前释放块租约且保留错误返回：用例必须执行到主机导出边界，独立mounted观察器失败。
- Wi-Fi-only同时清云配置：真实持久化/重开后的云解码或请求凭据判据失败。

编译失败、未走到目标断言、等价变异不计检出。原源码前后摘要必须相同，临时源码删除；
原始程序恢复验证单列。工具结果丢失、DMA引用、真实断电等尚未做变异，不声称敏感性已覆盖。

首次运行发现测试接线错误时保留原日志：单独音频fixture没有初始化前置条件造成idle/首播断言，
配置集成在glibc动态PTHREAD_STACK_MIN下因signedness告警无法编译。它们是SETUP_ERROR；
修正仅在测试夹具/主机构建，告警保留可见。不是放宽业务期望，也不作为产品Red。

## 审阅、合同变更和停止点

本轮执行需求/断言/接线自审，**不是独立第二方验证**。用户提供的规则为已确定输入；
上表明确标为待审的参数/API提议尚未批准。修改合同必须单独记录原规则、新规则、理由、
影响ID、失效证据、批准者；仅修测试接线也保留前次日志，不移动时间/误差门槛来迎合实现。

审阅后首批切片：M0核对运行身份/原资源与预算；M1先OTA对象终态和K2边沿，再退出/配置/卷；
M2快照/事件/App与语音；M3有限场景、原生USB接共同服务；M4实板组合；M5首次交付。
P0未闭环不推荐正式发布。正式首用由获准工厂全量部署开始；日常持久化/K2/网络回归不反复全刷。

本轮不连COM13/手机、不安装、不刷板、不清身份或格式化；未来先复核占用与实际serial，
专用可恢复介质才做损坏/掉电。每次现场动作明确提示并等反馈。长测、音频听音、原模型哈希与
真实TLS/USB/关机均缺L3证据；本轮完成测试交付后等待一次业务实施放行。

## 本轮执行快照与自审结论

[baseline-20260924.json](baseline-20260924.json) 保存逐例结果、耗时、证据摘要、编译退出码、
源输入哈希、环境和56项父用例的未完成状态；[data-manifest.v1.json](data-manifest.v1.json)
保存数据/外部源码身份和未定参数。[fixtures.sha256](fixtures.sha256) 可从仓库根用
`sha256sum -c tests/host/bk7258/acceptance/fixtures.sha256` 核验。

本轮实际收集63个执行单元（含旧套件的单个进程单元、33个JUnit用例及2个变异后恢复验证）：
59 PASS、4 FAIL_ASSERTION、0 SETUP_ERROR、0 skipped。四个失败为K2的3000/3001边沿、
原K2回归和原OTA对象回归；不是四种新根因。既有Agent队列完整入口、HTTP适配原入口也通过，
作为两个补充套件单列，不混入63的分母。两个隔离变异均编译成功、到达指定断言并被检出；
恢复原实现后相应用例通过。测试总入口保持非零（Python=1、Make=2）。

JSON父用例按用户复合规格计数，**没有任何一项被本轮冒充为全层通过**：2项含失败，45项首要
阻塞为设备，7项首要阻塞为接口，2项仍未完整执行。首要阻塞不是唯一缺口；已跑子项和未覆盖
层/绑定说明必须一起看。AGENT-02正式工具账本仍未接入，K2-03完整输入可信性仍未验证。
其余L3、N场景、实际TLS/SD/DMA、原模型/应答实际选择、掉电与性能目标也未获通过证据。

自审核对了需求来源、独立oracle、真实源码链接、替身边界、正/反例、逐例分型、变异敏感性、
实际输入哈希和未改生产的差异。Python格式由Black 24.10.0检查，`git diff --check`通过；
未运行不存在的ktlint或宣称全仓风格通过。外层apps仓库有既存脏改动，但实际用到的
webclient/netlib/header文件无差异且另列哈希，保留未动。此自审不等于用户/独立第二方批准。

原始日志与XML已复制到baseline中`evidence_root`指明的本地冻结目录，初次接线错误记录
另保存在 `out/shaniu-contract-v2/attempt-1/`，没有删除旧失败记录。提交SHA在交付消息及
提交后本地 `out/shaniu-contract-v2/submission.json` 中绑定本测试输入；不把尚未产生的SHA
写成已完成。源码/manifest/原资源未修改，未刷板、安装、恢复出厂或操作当前设备。


## 2026-09-24 S1 合同纠错记录

影响执行 ID：`ota.OtaControlUploadTest.closeCancelsWithoutSending`。
旧规则：本地 close 后断言 CANCELED。新规则：本地 close 不得声称远端已确认取消，
不发送请求，并释放本地记录；已确认终态保持。需求依据：用户放行第 3 条明确
“本地 close 不等于远端已取消”。源码 close 未发取消指令、旧测试 sent.size==1
共同证明旧期望失效。保留原 ID、e3ecd6b8 历史通过和 S1 修复前失败证据，
不是删断言或接受错误终态。增加独立迟到回执/无重发/不能重启测试。
审阅依据为本轮用户已明确的外部语义，未改动远端协议。

S1 新增执行 ID：K2-03.release-rollback、K2-03.combination，及 OTA 的
cancellationRequestWaitsForRemoteConfirmation、acceptedTerminalCannotBecomeCanceledFromLateAckOrClose、
localCloseIgnoresLateAckWithoutClaimingRemoteCancellation。原 63 ID 全部保留。
CLOSED 只表示本地对象释放，不是远端安装/取消结果；ACCEPTED 仍只表示 START 已受理。

## S7 停止接收新业务的 SDC1 内部入口

新增 `bkcontrol_session_quiesce(session)`，须与 packet 处理串行、已完成认证；
关闭会话返回 ENOTCONN，未认证 EACCES，不消耗线协议序号。它是单向门禁，
恢复写入需要新认证会话，不新增线协议 opcode、字段或身份来源。
允许 STATUS/INFO、CANCEL、OTA_STATUS/OTA_CANCEL、CONFIG_CANCEL，以及
CONFIG_READ 的 CAPABILITIES/SETTINGS/RESET_TRANSFER；其余新业务返回 EBUSY。
参数/帧/序号的既有合法性检查不能被门禁绕过。保留的取消确认仍不等于物理退出。
配置/OTA 已暂存数据可取消，门禁后不能继续 APPEND/APPLY/START。

生产绑定目标：owner 保留已有认证只读会话、禁止新认领/变更，排空阶段驱动
该会话；最终电源转换前再关闭传输，不以客户端持续查询延长退出。恢复出厂
继续使用完整权限撤销路径。S7 尚未完成这一 owner/电源调用绑定；不能以协议
单测证明设备关机时手机查询已可用，也不能以它关闭 LIFE-01/NET-03。

## S8 生产调用绑定

电源准备调用 `bkprov_owner_prepare_stop(now)`：只保留此前已认证的控制会话，
启用 S7 门禁并驱动真实 packet 处理；未认证/认领会话关闭，不开启新发现窗口，
读客户端不占用电源写入资格。身份替换仍使用原 busy 规则。资源全部退出后，
原 `bkprov_owner_quiesce(true)` 关闭传输，完成前不请求 CP；后续 prepare 不得
撤销已开始的最终关闭。恢复出厂原 full-quiesce 语义不变。

进入最终关闭后的 CP 拒绝/故障仍保留停止意图；本阶段不承诺重新打开 BLE。
新的物理关机意图可重试，关机后的物理恢复仍需实板验证。故障本地显示和 App
更明确的生命周期展示尚未完成，不能用查询协议接入替代这些体验验收。


## N2 focus timer initial binding (S15)

A single volatile timer is owned by the serialized product loop. SDC1 config
kind 10 carries FOC1, exactly 32 bytes: magic, BE32 action (1 start/2 pause/
3 resume/4 cancel), BE64 expected revision, nonzero BE64 operation identifier,
BE64 duration milliseconds (positive for start; zero for other actions).
A successful command increments revision. Exact retry of the most recent
successful request is accepted without repeating its effect; old revisions
return ESTALE. Active start returns EBUSY; invalid transitions/zero start are
EINVAL; arithmetic/revision exhaustion is EOVERFLOW. Backward clock input is
EAGAIN. Pause at/past deadline returns EAGAIN until the owner tick completes.

READ returns FOS1 (32 bytes): magic, BE32 state (0 idle/1 running/2 paused/
3 completed/4 canceled), BE64 revision, BE64 remaining ms, BE64 duration ms.
READ is side-effect free. Because two 16-byte reads may cross a state change,
clients must re-read revision and retry a snapshot if it changed. Remaining
may naturally decrease between reads while running. BEGIN stages only; it
never starts a timer. Session quiesce permits READ and rejects new writes.
No new wire opcode, authentication exception or sequence exception is added.
Unsupported kind on old firmware remains an error, not a fake capability.

The owner calls step using its existing monotonic clock; elapsed wall time
is not consulted. Completion changes state once. Power preparation and reset
cancel an active timer without reminding or restarting it. The module has no
heap, threads, cloud calls or persistence. This initial binding does not
freeze TIMER-02: cross-reboot policy is pending user input; no device deployment
or UI promise depends on the volatile prototype. A completion state is not
a rendered ring, sound or delivered notification. App/NFC/visual/audio binding
and resource measurements remain required before N2 acceptance.

### S75 PC offline pairing envelope v1 (USB-01 / NET-03)

Scope: host request/protected pending storage, Android codec and PC response import.
This is not phone consent, grant commit or a live USB owner activation. A future
phone caller must compare the request fingerprint with the PC, confirm explicit
capabilities, use the current authenticated peer certificate, submit the same
client/transaction/capabilities through PCW1, and require its durable receipt and
exact target readback before exporting the encrypted response. Never accept a
response's embedded pin as the user's trusted confirmation source.

All integers are big-endian. SPQ1 is magic(4), capabilities(u32, bits 1/2/4/8),
created epoch milliseconds(u64 within signed Long), expires(u64), random nonzero
client ID(16), random nonzero transaction ID(16), DER size(u32), canonical RSA
3072/e65537 SPKI DER. Total <=1024. This v1 exchange validity is exactly 600000ms;
both ends require created <= now < expires. This is an offline exchange window,
not expression preview TTL or a claim of monotonic expiry across OS clock reset.
Future changes need a versioned contract, not a relaxed failing assertion.

SPR1 is magic(4), SHA256 of complete SPQ1(32), device certificate DER size(u32),
DER(1..8192), ciphertext(384). OAEP uses SHA256, MGF1-SHA256 and label
ASCII `shaniu-pc-pair-v1` followed by request SHA256. The 104-byte plaintext is
SPK1(4), request SHA256(32), device DER SHA256(32), independent PC key(32 nonzero),
capabilities(u32). Every binding and exact length is checked. OAEP encrypts to
the PC; it does not authenticate the sender. The independently confirmed phone
pin and subsequent real device TLS/SDC1 authentication remain required.

SPP1 pending files use the existing CurrentUser DPAPI boundary: magic(4), sealed
size(u32), sealed SPX1 inner payload. SPX1 is magic(4), request size(u32), PKCS8
size(u32), complete request, ephemeral encryption private key DER. Only protected
bytes reach disk; private/public key correspondence is verified at import.
Protected readback must match before publishing the public request. Files are
created exclusively; old paths are never overwritten. Partial publication can
leave a protected pending file and is reported as unconfirmed, not success.
Import retains pending state for recovery within validity; single-use import or
secure deletion is not promised. Same destination cannot be replaced. Python/JCA
immutable internal copies cannot be claimed securely wiped; owned mutable buffers
are cleared. No private signing identity, raw PC key file, serial command or device
authorization is created by these offline CLI operations.

Evidence boundaries: Python protection fixture tests replace only OS encryption;
separate Windows DPAPI/JVM/TLS integration exercises actual OS protection and
production codecs. Emulator checks the Android OAEP provider with the explicit
nonempty label. Synthetic TLS peers are not connected-board acceptance. Required
IDs add seven Python and five JVM cases; the explicitly selected cross-language
subscenario, two mutants, seven restored tests and DPAPI three are separate and
must not be added again to the required-case denominator.

### S77 phone grant delivery (USB-01 / NET-03 / UI-01)

The native phone receives SPQ1 through the system file picker and confirms its
full request digest and requested capability names. It creates the independent
key in memory, saves only the encrypted SPR1 response plus public metadata, and
verifies saved bytes before starting PCW1. The request's transaction ID is used
unchanged; default transaction generation remains for other authorization calls.
Failure to save, expiry before submit, stale authorization state or connection
change does not send a grant or replay an earlier grant. Prepared key buffers
are wiped on submit/failure/close. A restart resumes receipt queries only.

Internal SPD1 is magic(4), created(u64), expires(u64), expected grant revision(u64),
capabilities(u32), client(16), transaction(16), certificate DER SHA256(32), response
size(u32), SPR1 response. Total is exactly 100 + response size and bounded <=8716.
The private app file is selected by device ID hash. Atomic publication, explicit
file sync and readback precede submission. This is not proof of physical storage
power-loss behavior. Export requires CONFIRMED, exact transaction/target/current
view, matching authenticated certificate and validity window; a locally saved
response or matching snapshot without the durable receipt is insufficient.

Export writes ciphertext to a user-selected document. It does not install a PC
profile or authenticate a USB session. Deleting the local delivery record needs
explicit confirmation and never claims to revoke the remote grant. Existing
unknown/pending jobs remain queryable; chooser return cannot implicitly confirm,
repeat authorization or restore a closed session. UI file I/O uses the existing
worker; no new BLE owner or service is introduced. No raw PC key is written to
Bundle/preferences/files/clipboard. Certificate pin and request digest are public.

### PC-01 / S80：PTE1/PTS1 事件接收合同

来源为用户PC-01的任务状态、ID、有效期、去重和终态规则；以下是本片新增公开
格式及资源选择，不是既有实板成绩。SDC1配置kind15沿既有认证/序号/分片服务。
PC需要独立TASKS权限；手机owner沿同一dispatcher，设备当前也必须存在TASKS授权。
每个设备保留一个当前任务，授权绑定为主配置revision＋PC grant revision，改变
绑定清空旧账本；暂不可读时关闭准入，不能复活原任务。无文件写入/新线程/云调用。

PTE1固定40字节，均BE：magic4，state4（1开始、2进度、3成功、4失败、5取消），
非零task ID16，非零event sequence8，剩余TTL毫秒4，progress4（0..100或全1未知；
开始必须为0）。有效期为调用方参数1..UINT32_MAX，接收端只用单调时钟，不信任PC
墙钟；溢出拒绝。发送端扣除本地排队时间，不离线重放，不在同一授权期间重用task ID。
序号在同一授权期间跨任务递增；设备只记最后已受理事件，不承诺无限历史去重。

完全相同的最后事件重复提交幂等、不续期；同序号不同数据拒绝；旧序号拒绝；
不同任务的非开始事件拒绝。新任务仅能显式开始且旧任务已终态/过期，运行中不能
被替换。终态不接受之后的进度/结果。第一条进度后至少间隔1000ms才接收下一条
进度，受拒绝事件不占用序号，终态不受该限频限制。此为协议限频策略，不是硬件
响应时间或性能成绩。停止准入/时钟倒退使旧有效期失效，恢复准入不复活旧任务。

PTS1固定48字节：magic4，state4，task ID16，event sequence8，剩余毫秒8，flags4，
progress4。flags：1准入、2过期、4终态等待提示。过期不改写电脑报告的最终结果。
READ无副作用；客户端分片读后须回读含ID/sequence的头确认一致。BEGIN/APPLY
受理不等于提示完成；CONFIG_CANCEL仍仅取消传输暂存，任务取消用PTE1状态5。
停止准入期间允许查询结果，禁止新的BEGIN/APPLY；不放松旧协议的鉴权/序号/长度。

本片仅接收、状态、准入/取消边界，**尚无显示消费者和电脑发送工具**，flag4
不是已渲染回执。不调用喇叭、电机或LLM。后续显示必须在语音忙时延后/合并，
只消费未过期事件并核对实际渲染结果；USB运行入口和实板门槛仍单独开放。

### S81 PC task sender binding (2026-09-27)

The maintained Python workbench now binds PTE1/PTS1 to authenticated SDC1 config
kind 15. Independent golden byte strings cover encoding/decoding; client tests
replace only external responses and exercise production staging and readback.
The external C TLS peer uses real grant storage, PC authorization, SDC1 staging
and `bkpc_tasks`; its synthetic principal now explicitly receives TASKS alongside
its prior capabilities. The separate missing-TASKS rejection test is retained.
READ offset 16/32 is delegated to the real task module, not an offset-zero mock.

The sender has no automatic replay, process monitoring, implicit pairing, mode
switch or arbitrary shell execution. Its input result is caller supplied; it
cannot establish that the caller's build actually succeeded. A single operation
has an absolute deadline; task reads recheck the first 32 bytes after collecting
48 bytes and reject a changed ID/sequence/state. ACK means accepted only. Invalid
local event arguments fail before credentials or hardware are opened.

PC-01 software binding remains partial: live USB ownership, device reminder
consumption/arbitration and physical output are still missing. TTL is receiver
remaining lifetime supplied explicitly; callers subtract pre-existing queue age.
This slice does not add durable event replay or a background process supervisor.


### S82 local task-result visual binding (2026-09-27)

Terminal PC results now use the existing product/display owners and transient
local visual plane: success check, failure cross, canceled horizontal bar.
No new thread, SD write, cloud request, motor or audio output is introduced.
The existing voice-idle observation gates selection; focus timer visuals retain
priority. Pending results can become visible after those activities end only
while receiver TTL remains. Expiration, quiesce and clock rollback produce no
task overlay; original display content is restored through the existing renderer.
Exact event duplicates do not renew TTL or cause redundant framebuffer writes.

This internal visual choice does not extend PTS1 or mark rendered receipts.
PTS1 pending still means feedback confirmation is unavailable; dual framebuffer
writes and actual panel appearance are separate evidence. Physical voice-start
races, real rendering latency and display quality remain L3 checks. Host tests
exercise the exact product visual statement with real task state, the actual
render function with external framebuffer sinks, and input boundary sequences.

Storage remains the existing 80-byte task ledger. Selection is constant-time
arithmetic with no allocation; renderer reuses the existing single transient
160x160 RGB565 buffer (50 KiB), freed after writes. No additional permanent
buffer/DMA or worker is introduced. CPU p95, stack high-water and real output
latency are NOT_MEASURED, not inferred from host success.


### S83 native serial connection lifetime (2026-09-27)

The PC connection lifecycle now owns its serial descriptor alongside the actual
PC authorization lease, while the caller still owns the control pair and device
identity. Open/step/close remain serialized; this module does not start a thread,
select USB mode, invoke a shell, toggle DTR or automatically reopen. Terminal
TLS/auth/authorization/source/transport errors destroy the pair before closing
the descriptor. Repeated close is idempotent on success. Close failure is latched
and blocks future opens rather than assuming descriptor ownership is known.
An open with an active pair/descriptor is rejected; failed authorization after
serial open releases it without processing product commands.

Real host PTY plus production TLS, PC guard, parser and config/task modules
exercise this lifecycle. The initial terminal test found TLS cleared but fd still
open. Hardware USB driver IRQ/DMA exit, all lower-level open rollback failures,
exclusive native port use and product reset/power wiring remain separate gates.
This module is compiled for AP but is not yet called by the startup loop; no
physical USB capability is claimed from the host adapter tests.

ARM sizeof: connection wrapper 88 bytes, external full control pair 69016 bytes,
plus TLS dynamic allocations. The latter is not hidden within the 88-byte figure.
No new resident thread/heap allocation comes from the wrapper itself. Actual heap
headroom/peak crypto cost remain unmeasured; production admission must account
for both phone and PC lifetimes, and close both before freeing shared identity.


### S84 product-owned USB admission and exit (2026-09-27)

The existing product thread now owns the native serial PC lifecycle. It admits
only when identity/control are bound, OTA is inactive and a coherent independent
PC grant is available. New opens require voice idle; existing connections keep
bounded SDC1 service during voice activity. The same serialized thread steps
phone and PC TLS, and must stop PC before resetting/freeing the shared identity.
No USB mode selection, Shell, DTR toggle, auto replay or new resident thread.

Before implementation, the retry budget was fixed at one failed open/allocation
per 1000 monotonic milliseconds (at most one per second, not every 20-ms product
tick). Existing TLS deadlines remain unchanged. Clock rollback closes the active
pair. The full pair is allocated lazily only after a valid grant and is freed on
open failure, terminal transport/auth/source error, OTA admission closure or
explicit stop. Allocation failure leaves local product operation available;
actual heap high-water and crypto latency remain unmeasured, not claimed safe
by total PSRAM size. ARM static owner is 120B; active external pair is 69016B plus
TLS allocations. This cost is additional to a simultaneous phone connection.

Shutdown attempts stop USB along with other participants; failed USB exit cannot
reach the final CP request. Reset stops USB before cleanup/identity release;
errors leave reset pending and admission closed. The product loop closes PC on
terminal wait errors and on reset read uncertainty too. Controller DMA shutdown
is separate from descriptor/TLS exit and still needs real-board verification.

Host validation runs the production owner, PC guard, TLS and serial adapter
against a real PTY, and the exact production power/reset coordinators against
external participant outcomes. No product-state machine is replaced by a success
mock. The PTY adapter now represents a missing external device as ENODEV instead
of asserting that a test master must always exist. Code is linked into AP but
not deployed in this slice; initial claim, USB enumeration/authorization and
actual paired phone/PC competition remain L3.

### S85 serial open rollback and uncertain close (2026-09-27)

The native adapter owns cleanup immediately after open, before termios setup.
Failed get/set attributes or initial link check use the same close path. On
successful cleanup the original setup error remains; if cleanup close fails,
its error takes precedence and stays latched. No descriptor/epoch callback is
published on a failed setup. Live callbacks become stale before close.

A close error is never retried, even if the underlying OS already released the
fd. That integer might now belong to another file. Repeated close/open returns
the latched error without touching the kernel; the product owner therefore
cannot mistake uncertain release for successful shutdown/reconnect. A normal
successful setup failure cleanup still allows a fresh epoch on later open.

Six cases use a real PTY and controlled external syscall errors. The fd reuse
case actually releases it, opens an unrelated /dev/null handle with that number,
and verifies the unrelated handle remains live. This is deterministic host
fault injection, not a claim that the target kernel physically reproduced EINTR.
No new thread or heap buffer; linked product owner remains 120 bytes.
