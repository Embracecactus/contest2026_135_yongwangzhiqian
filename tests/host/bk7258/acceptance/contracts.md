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
| 在线TTS有效PCM进展 | 既有接收边界：首段10秒；首段后500ms接收超时、最多3次 | 首段PCM期限10秒，后续PCM期限1500ms；仅结构合法且非空的PCM刷新单调时钟。ping、ACK、空帧和畸形帧不得续命；超时返回失败且不发送成功终态。该值不是声学连续性结论 |
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
| volume / `bk7258_media_volume.c` + `bk7258_usbmode.c` | 本地占用/维护请求/卸载结果→独占主机导出；失败保留阻挡；主机释放后本地可再获租约且SD缓存失效 | 真实preferences/storage/media-volume/USB-mode链接，mount/umount、KVDB和USB硬件边界替身；独立mounted/host-writable及读次数观察器。成功MSC→CDC使SD播放音量缓存按generation重读，失败进入/退出不虚构交接；未证明数据库写句柄/DMA排空或实际FS缓存失效 |
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
4. MSC-01：生产preferences/storage/租约/模式不能被夹具复制；只有KVDB、OS与USB硬件边界可控。
   卸载失败后尝试导出，观察器断言mounted与host-writable不同时为真；成功MSC→CDC后下一次
   playback必须重读主机改写值，进入或退出失败不得发布新generation。尚不宣称完整FS/数据库/DMA一致性。
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

### S87 bounded pack storage input (2026-09-27)

RES-01/RES-02: a zero-initialized upload object belongs to one storage worker,
which retains the mounted-volume lease across begin/append/finish/cancel. These
are synchronous I/O primitives, never short protocol callback operations.
Begin validates the announced size (128 bytes through the existing 32 MiB
format limit), exclusively creates its private staging file, and does not
truncate another live upload or an unreviewed previous-boot remnant. A blocked
remnant requires explicit recovery policy, not automatic deletion.

Append accepts a positive, contiguous block of at most 4096 bytes. Invalid,
duplicate, reordered or excess input cannot change the file. Wire-level retry
idempotency belongs to the authenticated job layer before this primitive.
Finish rejects incomplete input without committing; complete input is synced,
closed, parsed and neutral-render validated through the production pack reader.
Successful installation publishes a new immutable filename without selecting it.
Setting the default is a separate explicit activation. The legacy memory-import
entry reuses these primitives and retains its existing import-and-activate
semantics; it still owns its preexisting full download buffer.

Cancellation before finish closes and removes only the owned temporary file,
is idempotent once confirmed, and cannot reopen a terminal upload. Finish is
not concurrently cancellable; the job owner must linearize the commit boundary.
A close error is retained without retrying the potentially reused fd. The owner
must quarantine uncertain release rather than treating it as exited. Directory
sync failure after rename can leave a published pack with an unchanged default:
return the real error, query the artifact before deciding recovery, and never
claim an atomic power-loss guarantee from a host filesystem fsync success.

Ten host cases execute the production store/pack implementations with real
files; only selected write/fsync/close failures are injected. Golden payloads
are produced from the existing public eye source, with a second public pack ID
for old-default preservation. They are not signature/trust validation evidence.
Two isolated mutants (implicit activation, truncating another upload) must fail.

Interface: storage primitive implemented and legacy importer bound. L1/L2:
file parsing, bounded input and selection preservation covered. Missing:
authenticated USB file/job schema, bounded inbox and background worker, lifetime
coordination with display/MSC/power/reset, result query/reconnect/cancel routing,
workbench import/try/default flow. L3: native USB/SD timing, faults and recovery
NOT_RUN. This slice does not enable PC EYE_PACK writes or complete RES-03.

### S88 asynchronous native installation owner (2026-09-27)

RES-01, LIFE-02, RST-02: the native job API accepts an already-authenticated
binding, a nonzero monotonically assigned job ID, and an absolute monotonic
caller deadline. It retains only the latest volatile result. External protocol
adapters must bind their verified authority to this identity, correlate results,
and define recovery/result-retention policy; this API does not authenticate USB.

Begin only reserves metadata. Append copies at most one 4096-byte block, with
exact job/binding/offset checks. Busy returns backpressure; it does not imply a
broken connection or a completed write. Read-only status does not mount, write,
renew deadlines or wait for file I/O. Metadata contention returns EAGAIN. The
sole consumer drops the metadata mutex before all filesystem/driver calls.
Written progress advances after real storage completion, not after enqueue.

Cancel before commit is an accepted request. A blocked write must return, owned
staging must be cleaned, and the volume must exit before CANCELED is published.
A queued commit remains cancellable; once the consumer enters COMMITTING,
cancel returns EBUSY and shutdown waits for completion/cleanup. Deadline expiry
or clock rollback cancels precommit work; queries do not keep it alive. A newer
job receives a new ID; old IDs/bindings never append or cancel it.

Failed unlink remains UNKNOWN/error even when the descriptor and volume can
safely exit. Failed close/unmount/lease release remains UNKNOWN and pinned;
there is no automatic retry that can reuse an uncertain descriptor or bypass a
lease. A finish error may follow rename and is conservatively UNKNOWN. A full
result-recovery protocol is still required before this is a public USB feature.

The native adapter creates one task on demand (existing configured display
priority 75, stack 6144), waits on a coalesced semaphore until a command or the
absolute deadline, and exits after a terminal outcome. It has a distinct INSTALL
owner in the same AP/MSC volume arbiter; it cannot release DISPLAY's lease.
It uses the same configured block device, vfat contract and /mnt/sdnand mount
point. No render mutex is held during installation. Product reset/power paths
close admission independently of other participant failures and wait for real
resource release. Normal admission requires bound identity/control and no OTA.

Host integration executes the real native adapter, worker, job, store, pack and
volume code. Only task/semaphore OS shims, physical mount syscalls and the host
root are external peers. It covers native success, task creation failure,
mount/unmount failure and existing display-volume ownership. Separate threaded
cases block real write/fsync boundaries to check control availability and the
commit/cancel boundary. Two isolated mutations must fail: metadata lock held
over I/O, and ignored cleanup error.

Current binding limits: product quiesce/admission is linked and called. There is
still no authenticated file-job command caller, so link-time garbage collection
removes native begin/worker entry paths from the AP image. Native service and
worker code compile and pass host integration; they are not yet a deployed or
reachable USB upload feature. No retention across reboot, Web workstation,
trial/default UI or physical USB/SD acceptance is claimed by this slice.

### S89: authenticated file-job adapter (RJI1/RJS1, kind 16)

Requirement: RES-01/RES-03/USB-01/USB-02; follows N3 file-level import and
install/default separation. The serialized PC guard requires RESOURCES on
its current owner binding, grant revision and client identity. Legacy kind 5
remains read-only over PC. This is not an arbitrary file/path or Shell API.

All integers are big-endian. An RJI1 record is a 64-byte header plus at most
4096 data bytes. Offsets: magic 0; operation u32 at 4 (1 BEGIN, 2 APPEND,
3 FINISH, 4 CANCEL); server epoch16 at 8; client job nonce16 at 24 (nonzero);
job ID u64 at 40; argument u32 at 48; TTL milliseconds u32 at 52; body size
u32 at 56; reserved zero u32 at 60. BEGIN argument is total 128..32 MiB,
TTL is positive and finite, job ID is the last observed service generation;
body is empty. APPEND argument is contiguous offset, body is 1..4096 bytes,
TTL zero. FINISH/CANCEL have zero argument, TTL and body. Records travel
through normal authenticated SDC1 staging; framing/authentication/sequencing
and capability negotiation remain unchanged.

Server epoch is generated from the already seeded authenticated TLS DRBG and
survives transport reconnect in RAM for the same authority. Boot/revocation
invalidates it. Client must query before BEGIN and must not replay a job
across a changed epoch. Known authority change cancels precommit work and
requires actual cleanup before accepting a new authority. An EAGAIN authority
snapshot suspends admission; it does not invent a revocation receipt.

Exact latest BEGIN retry (nonce, previous generation, total, TTL) returns the
original acceptance/error without renewing deadline or creating another job.
A conflicting retry returns EEXIST. Only latest APPEND retry is deduplicated
by offset, size and SHA256 of its bytes; same offset/different data is rejected.
Older offsets are stale. The hash is not an advertised whole-file digest.
ACK means enqueued, not written. `written` advances only after real I/O.
FINISH retries while pending/committing/done are idempotent; DONE means
installed immutable pack, never activation/default/rendered confirmation.
RJI1 CANCEL ACK means requested. Outer SDC1 CONFIG_CANCEL only discards
its staged request and does not cancel an accepted file job. COMMITTING is not cancelable. CANCELED is exposed
only after cleanup; cleanup/release errors retain FAILED/UNKNOWN semantics.
Native absolute deadline is not extended by queries, duplicates or reconnect.

READ kind16 requires an additional nonzero 16-byte query nonce (20-byte SDC1
payload). Offset0/new nonce captures one immutable 128-byte RJS1 snapshot;
subsequent 16-byte aligned offsets must use that nonce. Exact offset0 retry
returns the same snapshot, not a newer mixture. Another query invalidates the
old captured view; nonzero offset with an unknown nonce returns ESTALE.
Fresh status requires a new query nonce. Reads perform metadata access only.

RJS1 offsets: magic0; native job state u32 at4 (0 idle,1 queued,2 opening,
3 receiving,4 writing,5 commit pending,6 committing,7 canceling,8 done,
9 canceled,10 failed,11 unknown); epoch16 at8; job nonce16 at24; ID u64 at40;
snapshot revision u64 at48; total/written u32 at56/60; signed errno/release
errno at64/68; flags u32 at72 (bit1 resources held, bit2 volatile); remaining
TTL u32 at76; fixed NUL-terminated filename40 at80; reserved8 at120. No
receipt from a previous authority is disclosed; ID alone remains a service
precondition, with idle state/zero nonce. Snapshot revision is capture-local,
not a durable revision or proof that data is current after capture.

Latest result only, volatile across reboot. Loss of the server epoch or
superseding result means outcome unknown: no automatic resubmission or claim
of durable receipts. Persistent history, default/preview operations, desktop
file sender, real USB and physical rendering are separate pending gates.

### S90: desktop file sender and local receipt

RES-03/USB-02 client contract: validate finite absolute deadlines; authenticate
before every public operation; read a coherent RJS1 using one fresh query nonce;
never treat an enqueue ACK as installed. New upload first freezes the selected
bounded eye-pack input, then exclusively writes/syncs a public local receipt
before BEGIN. The receipt binds certificate SHA256, server epoch, client nonce,
previous device generation, length, input SHA256 and original TTL. It carries
no credential and is never silently replaced. It is not a durable device receipt.

Resume is a separate explicit operation: require the same file hash and current
device/job identity, query written progress, and issue no BEGIN. Unknown/lost or
superseded results produce a failure requiring review; no automatic re-upload,
mode switch, owner claim, default activation or TTL renewal occurs. Cancellation
is complete only at CANCELED; local close remains a transport action. Format/CRC
and installation checks are the existing production device path; the host's
initial magic/size filter is not a complete format verifier or signature claim.

### S92 explicit default-selection storage failures (2026-09-27)

RES-02 / STORE-02: selection runs under the existing exclusive mounted-volume
owner. `.active.json.tmp` is created exclusively; a preexisting entry is not
owned by this operation and yields EEXIST without deleting/truncating it or
changing the prior default. Recovery of a previous-boot remnant remains an
explicit operation, not an automatic cleanup on an ordinary select request.

After validating the installed pack, file write/sync/close and marker rename,
the parent directory synchronization must succeed before returning success or
populating the caller's selection result. Errors propagate. A failure after
rename can leave the new selection visible: this is uncertain durability, not
a claim of rollback. No automatic retry/undo is added. Even successful fsync on
NuttX FAT does not establish physical power-loss durability; L3 remains open.
These are synchronous store primitives, not a new USB callback or async default
job. Future desktop activation must preserve expected-revision, ownership and
render-completion semantics before its entry is enabled.

### S94 PC expression trial client (2026-09-27)

RES-02/N2/N3: consume the existing kind11 ETC1/ETS1 contract from the PC client,
without a firmware schema change. Caller provides explicit expected latest ID,
nonzero u64 operation identity, supported expression and positive u32 TTL. This
selects an expression within the current pack, not a temporary pack/default.
CLI rejects invalid mutation parameters before borrowing credentials/opening a
port. Apply ACK is accepted only; current readback separately reports rendered
or confirmed cancellation. Mixed two-chunk snapshots are rejected by rereading
the header; remaining UINT64_MAX maps to unknown, not an enormous valid time.

No automatic retries, new claim, TTL renewal, persistence or staging-CANCEL
substitution. An uncertain mutation closes the local session; the device TTL
continues and remote state is queried before explicit retry. Real C SDC1 and
trial transitions are tested through a host pipe with external renderer sink
and monotonic clock; this layer intentionally does not claim TLS/PC grant/USB or
actual pixels. Existing TLS/guard suites remain separate. Future pack-specific
trial/default, light UI and physical combination acceptance remain open.

### S95 volatile installed-pack render (2026-09-27)

RES-02: the existing display worker accepts a bounded installed filename plus
expression/TTL/expected trial ID, using the same trial slot, nonwrapping IDs,
monotonic acceptance deadline, cancellation and render identity. Admission
copies metadata only. The mounted-volume owner opens exactly that canonical
installed pack, validates it and renders using the existing parser/cache/dual
framebuffer path. Missing/invalid named packs fail; they do not fall back to a
different pack while claiming the requested trial succeeded.

A trial does not write the default marker. On expiry/cancel, restoration resolves
the current persistent default and prior expression. A later explicit render,
including a new default, supersedes the earlier trial; its late expiry does not
overwrite the newer visible choice. Queue expiry/cancel renders nothing. All
file objects close before the volume lease is released. This is the native
service primitive; authenticated phone/PC pack-specific message routing and
versioned default operations are still separate required work. The existing
expression-only kind11 contract is unchanged.

The test uses verbatim production render/cache/volume functions, real store/
parser/lease and intent/identity transitions. Mount syscalls, monotonic time and
framebuffer writes are external peers. An independently specified all-green
source requires RGB565 0x07e0 at every pixel on both sinks; ordinary default
background is checked separately. File writes are counted after setup. This
proves host behavior, not DMA/SD power-loss/physical pixels or target latency.

### S96 positive TLS fixture validity correction (2026-09-27, tests only)

Affected execution: USB-01.tls-transport, its PC interop, four RES-03.native-tls
cases and existing workbench client fixture. Former positive certificates used
OpenSSL's issuing instant for notBefore and one/two days for notAfter. S95's
retained failure reported not-yet-valid before AUTH; capture time was about
0.16 seconds before notBefore. This invalidates that run as evidence about the
PC key decision, not its original FAIL_ASSERTION record. It does not establish
the source of host-clock disagreement or retrospectively explain older failures.

The positive synthetic identity now explicitly uses 2024-01-01T00:00:00Z through
2030-01-01T00:00:00Z, independent of the issuing process's current second. Fresh
keys/serials still vary. This is a correction to test setup, not a product trust
or timing-contract relaxation. The generation helper runs ordinary OpenSSL
verification at the current host time before launching protocol stimuli; an
invalid fixture stops as SETUP_ERROR. There is no sleep/retry or clock adjustment.

Independent fixed-time oracles require rejection before validity and after
expiry, and acceptance at the captured S95 time with the corrected positive
fixture. Product certificate pinning/chain/time checks, defaults, protocol
sequences and request timeouts are unchanged. Outside the explicit fixture
interval setup fails rather than silently extending dates. A test-review
correction is recorded here under the user's permission to fix erroneous tests;
no new product behavior or external parameter approval is inferred.

### S97 installed-pack trial wire contract (2026-09-27)

Config kind 11 gains ETC2 (72 bytes) alongside unchanged ETC1 (32 bytes).
ETC2 is start-only: the first32 bytes use ETC1 fields with magic ETC2; bytes
32..71 are a nonempty ASCII installed filename, NUL terminated and zero padded.
The name begins a-z, contains only a-z/0-9/underscore/hyphen/dot and ends .bkep;
no slash, traversal or arbitrary path is accepted. It selects an already installed
pack, never imports or persists a default. Malformed input is rejected before
queue/render/I/O. A missing valid filename may be accepted then fail in the real
worker, with no fallback. Authorization remains the existing scene permission;
this read-only resource trial grants no resource-write or ownership permission.

ETC1 cancel targets the same current trial ID. Both versions share the single
last accepted operation record: exact repeated bytes do not renew TTL or render;
changed length/version/filename under the same operation is EEXIST; stale trial
ID is ESTALE. Readback remains ETS1, acceptance is not rendering, and cancel ACK
is not restoration. No new thread, filesystem work in the control callback,
persistent default, clock or default TTL is introduced. Old firmware rejects
72-byte BEGIN; clients must report incompatibility, not fallback to a different
pack or activate it. New wire inputs use explicit TTL and operation IDs.

### S98 PC installed-pack trial client (2026-09-27)

The existing trial-start adds optional --pack-filename, selecting ETC2 only
when explicitly supplied. No filename retains byte-exact ETC1. A supplied
filename must satisfy S97 wire syntax before credentials or port use. Cancel
and status reject this mutation argument.72-byte requests use32/32/8 APPEND
chunks within the existing conservative payload; acceptance remains unconfirmed
rendering. BEGIN rejection by old firmware stops without fallback/replay.
The actual client exchange/session/control/renderer path is exercised with
plaintext host pipes replacing TLS/USB and external clock/mount/framebuffer
only. Pixel/source/default-write observations remain in the real renderer
fixture. Cancel/expiry restore default and missing packs report failure, not
render success. Separate existing TLS/guard cases are not claimed as joint
new pack-trial TLS or device proof. No persistent set-default is added here.

### S99 durable default-selection version (2026-09-27)

RES-02/CFG-02: a saved default name and its revision must be one atomic marker
commit. The exclusive mounted-volume owner serializes all mutations. Existing
active/1 markers read as revision0; absent markers name the factory default at0
without claiming that its pack is installed. New commits write active/2:
`{"format":"shaniu-display-active/2","pack":"a.bkep","revision":"0000000000000001"}\n`.
Revision is16 lowercase hexadecimal digits, nonzero u64; each explicit default
write increments even if the filename matches. Checked writes require the exact
current revision; stale requests and exhaustion are rejected before writes.
Name+revision survive reopen in a new process. No second authoritative file.
Queries parse the marker only; render validation remains separate.

Malformed/unknown markers and media errors cannot silently become revision0.
Read accepts v1 but does not migrate until an explicit mutation. Existing local
activation also increments a valid revision, preventing a bypass around checked
writes. Reset removes selection only through the existing explicit privileged
reset path; that path must invalidate prior authorization/job scope. No new
automatic repair/format/recovery is introduced. Old firmware cannot parse v2;
a downgrade that needs old data format requires explicit scoped migration, never
automatic counter rollback. No device deployment is authorized by this format.

Test expectation adjustment: test_bk7258_display_pack previously required
unconditional activate to replace malformed active.json successfully. That
would silently reset an unknowable revision and contradict this stale-write
contract. Preserve its read-EPROTO assertion, require activate-EPROTO and unchanged
marker, then use explicit reset_selection before testing recovery activation.
This is a recorded stronger precondition, not deletion/relaxation of an assertion.
Directory-sync error after rename remains failure/uncertain durability even if
the new marker is readable; old expected revision is then stale. New remote
asynchronous worker/protocol/default UI are still subsequent integration work.

### S100 asynchronous default selection on the display owner (2026-09-27)

A single bounded selection job shares the existing display owner/short metadata
lock and mounted-volume lease. Explicit refresh queues a marker read; plain
status copies only the last result and never mounts or reads storage. Explicit
set-default carries an exact expected persistent revision and latest job ID.
No new permanent thread, default TTL, unbounded queue or arbitrary path exists.
Admission has no I/O. Pending cancels immediately; preparing/reading cancel is
accepted but confirmed only after resource release; committing/rendering reject
cancellation. Gate closure rejects new jobs and cancels precommit work.

The worker opens the volume, rechecks cancellation atomically before marking
COMMITTING, executes the real checked store operation, and closes the volume.
Store success records save_confirmed plus version independently of rendering.
Only then may the same owner render the new default. A successful save supersedes
an older volatile trial even if display later fails. DONE for set-default means
save+render confirmed; refresh DONE means a marker snapshot, not a save/render.
Store I/O/commit ambiguity is UNKNOWN, not canceled or rolled back. Release
failure pins the job busy/UNKNOWN and blocks new work until explicit recovery;
no timeout or terminal label authorizes exporting a still-owned volume.

Read/refresh results are volatile observations, not power-loss durability proof.
The last operation result is volatile; old boot/authorization sessions must not
be replayed as new writes. Remote protocol/authorization and UI are separate
bindings still required after this native worker slice.

### S101: explicit selection release recovery

RES-02 / MSC-01 resource invariant: an unsuccessful unmount cannot grant a new
storage user access. A failed selection job may explicitly retry release on the
same display owner, without rewriting its saved default or replaying rendering.
`selection_recover(id)` requires the current nonzero job ID and an outstanding
release error; stale/zero IDs fail, and an already released job returns EALREADY.
Request and status perform no I/O. Pending duplicate recovery coalesces; cancel
is EBUSY while cleanup is pending. The business admission gate does not cancel
necessary cleanup. The worker attempts one real close before overlays/readiness
branches, clears the latch only on close success, and never retries on its own.
The original UNKNOWN state, error, saved revision and save/render confirmations
remain unchanged. A later explicit refresh is a new job and reads current state.

Three independently collected cases use the actual request/recovery functions,
volume owner, store and render implementation: successful release, closed-gate
cleanup, and repeated unmount failure followed by explicit retry. External
umount failure and framebuffer peers are controlled; no replacement production
state machine. The first missing API compilation is BLOCKED_INTERFACE, not an
assertion Red. A compiled isolated mutant that clears the latch on failed close
must be detected. These tests invoke the worker step directly; RTOS scheduling,
physical unmount, remote authorization and a product UI remain separate gaps.

### S102: ESC1 / ESS1 authenticated default selection

Config kind17 uses the existing authenticated SDC1 staging path. Resource
permission is required on PC; SCENES alone cannot persist a default. The native
service is shared with future phone callers; this slice binds PC, not phone UI.
The authority owner supplies a fresh nonzero 16-byte epoch after boot or grant
invalidation. Only the latest volatile operation receipt is retained. Reconnect
within unchanged authority retains the epoch and never automatically replays.

ESC1 is exactly96 bytes, big endian: magic0, action4 (1 select,2 refresh,
3 cancel,4 release recovery), epoch8..23, nonzero operation nonce24..39,
expected job ID40..43, zero44..47, expected durable revision48..55,
canonical zero-padded installed .bkep filename56..95. Only select has a filename
or nonzero revision; other operations require zero in these fields. Same nonce
and exact bytes return acceptance only while its resulting job is current;
changed bytes conflict. Native revision checking remains in the real worker.
Accepted does not mean persisted/rendered/canceled/released. CONFIG_CANCEL
only discards staging. New retry after failed release needs a new nonce.

READ requires a nonzero16-byte query nonce after the argument. Offset0 captures
an immutable128-byte ESS1 snapshot for that nonce, offsets0..112 in16-byte steps;
a new query must begin at0. No I/O occurs during capture. ESS1: magic0,state4,
epoch8..23,jobID24,error28,releaseError32,flags36 (1 version known,2 save confirmed,
4 render confirmed,8 refresh,16 recovery pending), revision40(u64),expected48(u64),
last operation nonce56..71 (zero unless job matches latest accepted operation),
filename72..111,snapshot sequence112(u64),reserved120..127 zero. All receipts
are volatile. Snapshot is global device public resource state, not credentials.
Invalidation cancels this authority's latest queued/preparing job where possible;
it cannot undo an already committing save, and does not claim to do so.

### S103: PC default selection commands

`default-status` reads latest-job metadata, not the disk. `default-refresh`
explicitly requests a storage read; its completed result supplies the current
revision for `default-set`. Every mutation requires caller-supplied epoch,
nonzero operation nonce and expected job ID; set additionally requires the
installed canonical filename and expected durable revision. No automatic write,
resume, credential fallback or request replay follows a timeout. Keeping these
public request arguments lets the caller query the latest volatile receipt.

Status may match expected epoch, operation nonce and job ID. Identity mismatch,
malformed fields or incoherent snapshot sequence leaves the result unconfirmed
and closes the client; it does not send any mutation. Pending acceptance cannot
report saved/rendered. Recovery clears release_error only when reported by the
device; UNKNOWN stays UNKNOWN and is not upgraded to rendered/done. `saved`
and `rendered` mean device-reported confirmations, not physical screen proof.

Host tests use independent ESC1 bytes and ESS1 fields, external transport faults,
and real Python _exchange -> native SDC1/controller/worker/store/renderer. The
native peer replaces TLS/USB transport with pipes and uses synthetic credentials;
separate existing TLS/guard regressions remain required. No single physical or
production-TLS-to-default-renderer path is claimed by the pipe integration.

### S116: ECC1/ECL1 installed catalog protocol (kind18)

Kind18 uses authenticated SDC1 staging and the existing resource authority epoch,
shared with kind17. PC requires RESOURCES; phone requires its actual owner scope.
No filesystem I/O in READ/BEGIN/APPLY: page action queues the existing catalog
job, status captures its metadata/result only. Old firmware returns unsupported;
clients must not interpret that as an empty catalog or replay a mutation.

ECC1 is96 bytes BE: magic0,action4(1 page,2 cancel,3 close recovery),epoch8..23,
nonzero operation nonce24..39,expected shared job ID40,zero44..55,canonical
zero-padded cursor56..95. Empty cursor starts a page; only action1 accepts it.
Actions2/3 require a current catalog job. Same nonce+exact bytes is idempotent
only while its resulting job is current; another operation with the nonce is a
conflict. CONFIG_CANCEL discards staging only, not an accepted job.

READ carries nonzero16-byte query nonce after kind/offset. Offset0 captures an
immutable608-byte ECL1; offsets0..592 are16-byte aligned. New query starts at0;
repeating a query sees the captured result, not a partially changed live page.
Header: magic0,state4,epoch8..23,shared job ID24,error28,release error32,flags36
(1 catalog job,2 recovery pending,4 page available,8 more),last operation nonce40,
sequence56(u64 nonzero),count64(u32<=4),zero68..95. No entries unless a catalog job
is DONE with no errors and its exact-ID page read succeeds. A raced job ID returns
ESTALE; never publish mixed fields. Each capture/page is an observation, not an
atomic multi-page directory snapshot. No full-file digest is claimed.

Four128-byte entry slots begin at96. Per slot: filename0..39,pack ID40..71,
revision72(u32),renderer API76(u16),width78(u16),height80(u16),entry count82(u16),
palette count84(u16),zero86..87,total file bytes88(u32),declared source SHA256
92..123,zero124..127. All unused slots and string padding are zero. Source SHA is
source metadata, not the file hash. Gate/validation/exit failure returns no page.
Close-only recovery preserves UNKNOWN; ACK is not catalog completion.

ESS1 keeps128 bytes and adds catalog flag32 for its shared-job view. A catalog
job has no default version/name/save/render/refresh flags. DONE catalog must not
be presented as a saved/rendered default. Current PC/Android decoders must accept
this distinction before this firmware is deployed with catalog enabled; older
strict ESS1 decoders reject the extension. Matching APK/firmware is a release
gate; no on-device compatibility is inferred from host decoding.


### S117: public catalog client and browser workflow

The sole workbench CLI adds catalog-status/page/cancel/recover. Authority epoch,
operation nonce and expected shared job ID are explicit; --catalog-after is
only a canonical filename cursor for page requests. Invalid/mixed input fails
before credentials or port access. Each request uses one BEGIN/3APPEND/APPLY,
returns accepted only and never reconnects/replays on an uncertain response.
Status reads one nonce-bound608B snapshot and rechecks its sequence-containing
chunk; receipt epoch/nonce/ID mismatch closes the connection and returns unknown,
not an empty list. Entries are strictly ordered/canonical with zero padding;
more requires a full4-item page. Source SHA remains declared source metadata.

Browser requests traverse the existing authenticated loopback server and sole
operation worker. GET local state never scans or contacts the device. Explicit
catalog-status is metadata-only; explicit page creates a device job, and the
user reads its result. UI lists only a validated available page; pending/error,
unknown or unconfirmed never become an empty successful catalog. Selection only
fills the filename field, with no device operation. Next page uses the returned
cursor and current shared job ID. Any new catalog/default attempt invalidates
cached shared-state controls before I/O, including a lost POST response followed
by a late old local snapshot. Shared default/catalog results require fresh reads
before another domain may mutate. Explicit close recovery leaves UNKNOWN and
no entries. Each page is an observation, not a frozen whole directory.

This step binds browser HTTP -> real TLS/SDC1 -> production controller/worker/
store with synthetic identity and controlled transport/hardware. PC grant and
USB product routing are separately covered; this is not physical native USB,
real BLE, actual SD or full paired PC acceptance. Native Android catalog picker
remains separate outstanding work. Browser layout/DOM states and native protocol
integration are separate evidence layers; no synthetic screenshot is hardware
proof. No automatic refresh loop, firmware downgrade, MSC or arbitrary file path.

## R1 CP outcome reconciliation (2026-09-28)

LIFE-02 extends the existing 30 s exit budget across CP acceptance/query; this
is a bounded failure policy, not a measured hardware completion guarantee.
An explicit new off intent during pending returns in-progress without sending
another CP request or renewing the deadline. After timeout, a new intent first
queries CP: unknown or still pending retains stopped resources and reports the
outcome; only an explicit not-pending response permits a new request. Late or
repeated positive responses cannot restore resources or declare sleep success.
An initial explicit refusal permits a fresh intent retry. Existing K2 edge,
session, clock and chord protection remains unchanged. Hardware deep sleep and
physical K2 are separate, unavailable without independent recovery/fixture proof.

Development prepare-only validation is default-off and creates no remote command.
An explicit diagnostic build supplies a one-shot software intent only after
local listening is running and the voice session is idle. It exercises the
production coordinator and actual participants, then blocks the sole final CP
request after acknowledgements/sync. Stopped resources remain stopped; operator
normal reset/download restores service. No sleep, physical key, current or
acoustic acceptance is inferred. Normal release configuration must disable it.
R2: contract, fixture and runner changes trigger the existing source workflow;
its required job runs selected-contract completeness and collector selftests,
fails normally, and retains result/log evidence even on failure. Future manual
L3 specifications are not added to the executable set.

### R4 local listening before cloud readiness (2026-09-28)

BOOT-01.local-* executes the actual local-listener routing block from the
production configuration worker; dependencies are explicit owner peers. Identity,
core initialization, model verification and persisted wake-threshold availability
remain prerequisites. Cloud unavailability or a network transaction alone must
not prevent starting that local listener or consuming a local wake event. A busy
threshold read must not arm the original model with a temporary default threshold.
A local model/start failure does not overwrite cloud readiness or retry each tick.
Cloud backend clearing while the official channel is idle does not own KWS capture.

Offline matches are consumed through the real trigger processing function with
cloud admission denied: release/rearm locally, no ASR request or wake acknowledgement.
This explicit admission input is required by the newly reachable offline route;
its initial guard failure is not evidence of an old reachable offline cloud turn.
These are L1 routing/guard checks. They do not substitute for live inference,
physical speech, radio coexistence, or retained hardware callbacks. Startup HIL
must separately establish actual capture readiness relative to cloud activation.

### R3 completed focus versus finite PC feedback (2026-09-28)

PC-01.task-focus-completion links the real focus timer and PC task receiver.
A completed focus remains completed in snapshots. Its fallback visual yields to
a valid terminal PC event until that event's existing receiver TTL expires.
Running/paused focus keeps priority. Voice unavailability suppresses both;
resuming after expiry does not replay the old notification. No new TTL is
invented, no timer is silently canceled, and acceptance is not a render receipt.
The first attempted run used a stale binary after a Make dependency error and
is SETUP_ERROR (see r3-observer-correction.json), not a product Red. The corrected
build fails the intended notification-selection assertion on unchanged production.

### PC-01 transient authorization snapshot (2026-09-28)

An accepted task event retains its existing finite receiver TTL when the PC
authorization snapshot is temporarily unavailable with `-EAGAIN`. This storage
publication interval is neither a revocation nor an admission failure. The
product must reject new authenticated traffic while the authority is unknown,
but it must not permanently expire the existing volatile task ledger. When the
same binding and grant revision become readable again before the task deadline,
the prior terminal result remains eligible for local feedback. A real revoke,
capability removal, changed binding/grant, quiesce, OTA admission closure, clock
rollback or TTL expiry still invalidates it.

`PC-01.task-transient-authorization` executes the extracted production routing
function with the real authorization owner and task ledger. Only one external
snapshot result is fault-injected; the recovery read uses the real persisted
grant. The oracle checks both the product visual selection and PTS1 flags. It
does not claim USB transport, panel pixels or a physical notification.

### AGENT-01 complete no-tool body reuse and test correction (2026-09-28)

The frozen user requirement preserves a legal, complete no-tool answer rather
than making an unnecessary second model request. The prior final-stream parser
test instead asserted that this body was deleted; that internal assertion was
incorrect for AGENT-01. Its earlier PASS is retained but does not establish
requirement compliance. The corrected assertion fails on Agent 62a304ea.
Incomplete/filtered responses and ambiguous tool payloads still fail closed.

AGENT-01.plan-parser runs the real LLM proxy/parser against a controlled peer.
AGENT-01.final-body-* executes Agent's actual final-phase, request check and
assistant-message construction functions; only the final cloud request and body
consumer are peers. A complete no-tool body uses zero extra cloud requests and
one begin/delta. Empty bodies and explicit finalize retain final streaming.
Cancellation before/during delivery remains an error. This slice does not claim
a complete mixed-tool ledger, full history commit, visual round, or measured
physical latency; those remain separate integration/device gates.

### AGENT-02 mixed tool and finalize ledger (2026-09-28)

AGENT-02.mixed executes the production `run_react_loop`, assistant/tool history
construction, parallel dispatcher and final-phase transition. A controlled LLM
peer first returns `get_weather(real-1)` together with
`agent_finalize(finish-1)`, then returns a sole finalize marker. The real tool
must execute exactly once; the pseudo-tool must never enter the registry and
must receive a matching deferred tool result. Final streaming may begin only
after the second planning turn. AGENT-02.missing-id and duplicate-id require
`-EPROTO` before any tool or reply side effect. The production parser separately
accepts the same two-call batch and preserves both IDs. These host contracts do
not identify the provider payload from the historical `-71` log or prove a live
cloud, voice, or board round.

### AGENT-03 vision cancellation handoff (2026-09-28)

AGENT-03.vision-cancel executes the production vision adapter followed by the
production ReAct coordinator. The vision peer cancels the same request during
the call. The adapter must pass the request checker to the checked vision API,
release the image once, and the coordinator must return `-ECANCELED` without a
second planning request, tool side effect, reply stream, or stale vision body.
The linked LLM regression additionally verifies that a successful transport
return cannot commit a response after its checker observes cancellation. This
host contract does not prove camera capture, live provider behavior, board
display, or cross-turn persistence of a successful non-idempotent tool receipt.

`AGENT-03.tool-vision-cancel` covers the separate registered
`analyze_image` route. It executes the production builtin dispatcher and
production image reader while a controlled vision HTTP peer changes the same
request to `-ECANCELED`. The request checker must reach the raw-image vision
call; a peer success racing with cancellation must return `-ECANCELED`, clear
the uncommitted output, and must not record a successful guard call. The fixture
extracts the production `analyze_image` registration and dispatcher so a legacy
unchecked registration cannot pass. It does not claim live-provider interrupt
latency, camera capture, persistent history, panel output, or that a canceled
non-idempotent tool can be treated as never executed.

`AGENT-03.tool-provider-cancel` covers the checked external-provider branch of
the same production dispatcher. The provider sees an initially live request,
then a controlled peer completes while cancellation wins. Before publishing
success, the dispatcher must check the original request again, return
`-ECANCELED`, clear the uncommitted provider output, and avoid recording a
successful guard call. This contract does not claim that a non-idempotent
provider side effect was undone, or measure provider interrupt latency.

### RES-01 phone eye install background boundary (2026-09-29)

Once the phone has sent the eye source and APPLY is in flight, leaving the
Activity cannot claim that the device canceled the operation: CONFIG_CANCEL is
serialized behind APPLY.  The phone closes the short-lived HTTPS source and
immediately closes this authenticated control generation instead of retaining
the ordinary 30-second Activity grace.  The device may cancel only before its
persistent activation commit; if that boundary already passed, the outcome is
unknown and the next authenticated connection must read the actual eye state.
No request is replayed and the previous selected pack remains active when the
generation changes before commit.

`RES-01.eye-install-disconnect-cleanup` extracts the production phone install
adapter.  A controlled peer changes the real GATT generation after a complete,
digest-valid HTTP body while cloud TLS cleanup is finishing.  The adapter must
return `-ECANCELED` and must not enter display import.  The stable control case
still imports exactly once.  `eye_background_probe=1` separately drives the
real MainActivity and DeviceControlSession against an in-memory transport; it
requires immediate transport close, no queued wire CANCEL, and an unknown
result message.  These tests do not prove BLE radio disconnect timing, SD
durability, panel rendering, or a physical phone.

### CFG-02 durable desired revision versus late network result (2026-09-28)

The durable storage revision is the selected configuration.  A network trial
uses the revision captured when it starts.  If revision B becomes durable while
trial A is still running, A's later success or failure must not publish ready,
an error, a link expectation, or a retry as though it belonged to B.  The
product reads only the stable durable revision at the storage publication
boundary; it does not copy or expose the configuration.  An active storage job
returns unavailable because its worker owns the backing revision.  A stale or
temporarily unreadable completion leaves configuration not ready and requests
activation of the latest durable record through the existing owner.

CFG-02.activation-* executes the exact production completion block with only
storage revision, Wi-Fi and network peers replaced.  On unchanged production,
stale success, stale failure and unknown desired revision were assertion Reds;
current-revision success and failure were baseline Green.  The same five cases
pass after the coordinator fix.  An initial missing virtual clock in the test
fixture was SETUP_ERROR and remains separate.  The real storage regression
also checks the revision view across load, in-flight commit, durable commit,
reset marker, empty reset result, blocked replacement commit and uncertain
publication.  The extracted coordinator case proves the pending request and
suppression of the old result; the downstream activation call remains covered
by its existing production-path tests rather than this extraction.

This closes stale-result publication, not the whole CFG-02 contract.  SCS1
still exposes the durable stored revision and save outcome; a public applied
revision schema, real Wi-Fi/TLS change, power-loss persistence and App display
remain separate gates.

The public applied-revision extension keeps frozen SCS1 byte-for-byte
compatible.  SETTINGS kind 7 READ at offsets `0x8000` and `0x8010` returns the
two chunks of a 32-byte SCA1 record: magic, BE32 application state, BE64
durable desired revision, BE64 revision belonging to the current/latest
application result, signed BE32 result and a zero BE32 reserved field.  States
are UNKNOWN=0, APPLYING=1, READY=2 and FAILED=3.  A revision mismatch means a
newer durable selection superseded the reported attempt; an old completion
must not relabel the desired revision.  Old firmware returns `-ERANGE`, which
clients treat as application state unavailable.  Reading SCA1 is side-effect
free and never returns Wi-Fi, cloud or owner secrets.

SCA1 describes local application of the durable selection, not Wi-Fi, TLS or
cloud reachability.  READY means that the local product services accepted the
selected revision (or that the revision deliberately contains no Wi-Fi
selection).  A later network-trial failure or link loss remains in the
existing connectivity status and must not rewrite READY as FAILED.  FAILED is
reserved for a local load/decode/bind/restore rejection before the selection
is accepted.  A cloud-bearing restore remains APPLYING until its local cloud
loader returns; loader success publishes READY before any remote service
probe, and loader failure publishes FAILED.  A remote probe result and later
link loss do not rewrite that local result.  SCS1 PENDING, FAILED and
UNCERTAIN must never be rendered as
"saved" merely because an SCA1 record was read.


### R1 read-only power outcome (2026-09-28)

LIFE-02.power-cp-query binds the production coordinator to the existing health
RPC core. A query after product transport exit returns preparing/pending/failed
and a signed error; unresolved CP ownership remains explicit. Repeated reads do
not resend CP requests, reopen resources, or sample battery/temperature. Query
success is not power-off success. New read-only command 2/reply 0x8001 uses the
same frame size, with phase 0..3 in low byte and unresolved bit 8 in reserved[0],
int32 error in reserved[1]. Existing STATUS/reply 0x8000 still requires zero
reserved fields. Session/sequence and reply kind must match. Before a coherent
snapshot is published the getter returns EAGAIN; unavailable builds ENOTSUP.
Missing pre-change getter/command binding is BLOCKED_INTERFACE, not a business
Red. Existing CP timeout/unknown Red remains in its original evidence. Prepare-
only board validation stops before CP submission and cannot certify deep sleep.

LIFE-02.nfc-deferred-registration: after service start but before deferred device
registration, RF-off open returning ENOENT remains unfinished cleanup. Shutdown
keeps admission closed and retries only that initial RF-off at the existing
500ms worker cadence; it never scans or enables RF. After registration, actual
RF-off and descriptor close must finish before quiesce returns success. Other
RF/close failures remain failures; the coordinator's 30s deadline is unchanged.
The 668 board error -2 and matching production-worker host Red precede the fix.

LIFE-02.power-cp-lost-reply-replay links the actual AP PM client and CP PM
server through a deterministic RPMsg peer. If the first CP response is lost
after the operation is committed, AP retries the identical generation and
sequence within its existing bounded request. CP must execute soft-off once and
replay the cached result. Reusing that sequence with changed content returns
EPROTO; an older sequence returns ESTALE; neither may repeat the side effect.
This is L2 protocol reconciliation evidence, not product-coordinator timeout,
deep-sleep, physical K2, or board recovery acceptance.


### AUD-03 deployed-path binding correction (2026-09-28)

The existing AUD-03.media-tail/media-cancel-next execute the BK7258 PCM bridge
compiled only when CONFIG_MEDIA is off. They remain valid for that profile;
they do not prove the full-Media path enabled in AIDK firmware 671. Preserve
all prior results and do not infer an AIDK playback pass from those two IDs.
AUD-03.agent-{tail,cancel-next,close-failure,eof-failure} links the actual pinned
Agent audio_playback.c with controlled external Media/socket/clock boundaries.
The oracle requires exact ordered bytes across partial writes, waiting for a
real completion callback after EOF, canceled writes/late events not reviving a
session, a clean next player, retained ownership after failed close, and errors
not overwritten by later completion. All four are baseline PASS; no production
change or artificial Red. This is L1 adapter evidence, not actual FFmpeg queue,
DAC, acoustic or continuous-dialogue acceptance. The initial build setup errors
are retained separately and are not business failures.

AUD-03.agent-truncated-close binds the actual Volcengine WebSocket receiver.
The provider's negative sequence or the documented frontend terminal is a
successful terminal audio marker.
If the peer closes after one or more positive-sequence PCM frames, the receiver
must return the connection error and must not emit the terminal callback; bytes
already delivered remain observable but cannot relabel the truncated stream as
complete. A negative-sequence final frame retains the existing single terminal
callback behavior. This is source/parser evidence, not Media, DAC, or acoustic
completion.

AUD-03.agent-ws-close-before-terminal executes the same registry, Volc backend
and WebSocket parser. A WebSocket Close frame after positive-sequence PCM but
before the provider's negative sequence or frontend terminal is a truncated
stream: it returns a connection error, emits no terminal callback and releases
the request so a subsequent valid request completes once. Transport closure is
not a successful protocol EOF and must not start downstream final drain.

### UI-01 stale OTA admission (2026-09-28)

A retained OTA capability may remain visible after a STATUS read fails, but it
cannot authorize a new update. The native update button, its action boundary,
and the asynchronous source-open callback all require the current authenticated
snapshot to be fresh and to advertise OTA support. A stale result closes any
newly opened local source before saving an expected receipt or sending
`OTA_BEGIN`. A new `OTA_BEGIN` also cannot queue behind an in-flight STATUS read,
because its Boolean admission result cannot later report that the queued request
was dropped when STATUS makes the capability stale. Leaving the Activity while
the local source is opening releases the start gate and keep-awake ownership;
it does not leave a blocked future update. A callback from an older transport
epoch may release only the lease acquired by that source attempt, so it cannot
unlock a replacement source after disconnect/reconnect. An already admitted
upload and its cancel/result reconciliation keep their existing terminal-state
rules.

The emulator case uses synthetic snapshots and a test-owned package/preferences
namespace, so it is App behavior evidence rather than BLE, board installation,
or App OTA acceptance. The pure admission matrix and the serialized-session
race are also part of the selected JVM contract set. Existing OTA session
fixtures now state their previously implicit `otaSupported` precondition; their
first run after the new admission guard failed before reaching the terminal-
state assertions and is retained as a fixture correction, not a product Red.

### NET-03 queued configuration cancellation (2026-09-29)

When an authenticated native session accepts `CONFIG_BEGIN` locally but it is
still queued behind an in-flight read, canceling that local request must remove
the queued payload and release the local writer reservation without sending
either `CONFIG_BEGIN` or `CONFIG_CANCEL`. A cancel for a transaction that never
reached the device must not alter device-side staging owned by another request
or client. Once `CONFIG_BEGIN` has been sent, the existing remote cancellation
handshake and terminal-state rules still apply. After the queued-only cancel,
the current read may finish normally and a later configuration transaction may
be admitted.

The JVM case exercises the production `DeviceControlSession` scheduler with a
controlled transport peer. It is L1 session/serialization evidence; it does
not establish BLE coexistence, Android lifecycle behavior, or board-side
configuration persistence.

### LIFE-01 claim-window close during display rendering (2026-09-29)

Closing an existing native claim window is part of product exit. A NULL
`bk7258_display_onboarding` request must publish a bounded clear intent without
waiting for the display render mutex, mounting storage, or touching either
framebuffer. Ordinary display work remains gated until the existing display
worker consumes the clear intent. The worker clears the in-memory QR and marks
the overlay dirty; rendering the replacement frame remains a separate result.

Opening a claim window keeps its existing success contract: success is not
returned until the QR has been rendered, so the owner must not expose a claim
secret or open GATT merely because metadata was queued. This host case covers
the NULL close path and production worker handoff only. It does not prove QR
pixels, physical screens, BLE claiming, or K2/deep-sleep behavior.

A power request that arrives after claim rendering starts has higher priority
than opening the claim transport. The display open must return failure, clear
the in-memory QR, and keep ordinary display admission closed. The existing
owner/bootstrap caller copies its fresh secret and opens GATT only after a
successful display return. The deterministic host case injects the production
power request at the real framebuffer boundary; it proves this software
ordering, not a physical K2 edge or BLE radio shutdown.

### LIFE-02 voice cleanup outcome during power exit (2026-09-29)

The product power coordinator must preserve the actual voice-owner teardown
outcome. `-EAGAIN` and `-EBUSY` mean cleanup is still in progress: admission
stays closed, no CP request is sent, and the existing shutdown deadline is not
renewed. Any other negative cleanup result is a permanent participant failure
and must be published immediately with that error instead of being discarded
until it becomes a generic timeout. Completed participants remain stopped.

After a permanent voice cleanup failure, ordinary polling may continue cleanup
but cannot submit CP or reopen resources. A new explicit power intent may retry
after the voice owner reports both successful cleanup and idle. These host cases
execute the production coordinator with a controlled voice-owner boundary; they
do not prove Media/DMA teardown, physical K2, deep sleep, or board recovery.

The deadline case keeps cleanup busy through 1000 ms and 29999 ms, then checks
the exact 30000 ms boundary. It records that repeated progress does not change
the original deadline and that expiry publishes `-ETIMEDOUT` without a CP
request. This preserved behavior also passes against the pre-fix production
coordinator; it is a Green baseline, not a manufactured Red.

### BKTEST authenticated engineering input (2026-09-29)

`USER-20260929-BKTEST` requires an unattended development-build path from an
authorized HIL client into the existing K2 and power state machines. The public
behavior and negative boundary are frozen in
`docs/platforms/bk7258/bktest-hil-interface.md`. The transport is the existing
pinned-TLS SDC1 connection. A live, independently granted PC principal must
have `BKPC_CAP_DIAGNOSTICS`; the new kind remains unbound when
`CONFIG_BK7258_ENGINEERING_TEST` is absent. Port access, DTR, a browser grant,
or a production firmware image cannot enable it.

The PC permission boundary is executed through the existing pinned-TLS SDC1
guard. A non-diagnostics principal is denied kind 19 in every build. A
diagnostics-only principal is denied when the engineering symbol is absent and
may reach the product config adapter only when that symbol is present. The
adapter remains responsible for the BKT1/BKS1 contract; the PC guard cannot
perform a power action itself.

K2-01 binds exact 2999/3000/3001 ms sequences to the production key policy.
Every session begins with an explicit released baseline. `advance` changes the
engineering input clock and emits no KEY1 record, so `down -> advance -> up`
proves that no threshold heartbeat is required. A qualifying release produces
one power intent; a duplicate or stale session/sequence produces no event or
side effect. Existing K2 session, rollback, combination and volume contracts
remain required and are not replaced by BKTEST.

The transport fixture also compiles the actual AP key receiver with the
engineering build symbol. It checks that a normalized engineering session uses
the same product intent accumulator, that a down/up edge separated by 3000 ms
needs no synthetic held heartbeat, and that duplicate, wrong-session, or
post-end events cannot create an intent.

LIFE-02 binds declined, unknown, pending and late-ack outcomes at the existing
PM peer dependency. The engineering command handler cannot call the PM request,
sleep, reset, shutdown, or factory-reset terminal functions. The real product
coordinator remains responsible for admission close, resource handshakes,
deadline, retry/reconciliation and its public power snapshot. An engineering
build fails closed at that PM boundary, including physical input while test
mode is compiled, so unattended HIL cannot enter real deep sleep.

The old source has no authenticated engineering kind and is therefore
`BLOCKED_INTERFACE`, not a business assertion Red. The pre-existing K2 and
power cases are saved as Green baseline evidence before implementation. After
the interface exists, the same new cases must run through the production key
handler and the SDC1 capability gate. Board HIL may use `bkhealth power` as an
independent read-only observer after product USB is quiesced. Host/HIL results
do not close physical GPIO debounce, real K2, deep sleep/wakeup, current draw,
phone BLE, App OTA, or subjective sound acceptance.

An idle engineering input session expires after 60000 ms. Expiry releases the
virtual source and reports `-ETIMEDOUT`; it must not leave physical input
blocked or silently report an orderly explicit end.

An active engineering session cannot be replaced by a second session. After a
session ends, a non-idle or unresolved product power snapshot also rejects a
new session. The CP peer mode therefore stays bound to the virtual release that
created the power intent; a later client cannot change an in-flight request's
declined, unknown, pending, or late-ack result. An accepted but not yet consumed
power intent likewise prevents a new engineering key source from starting.

Closing or revoking the authenticated PC lease ends an incomplete or
non-qualifying virtual key session immediately, so it cannot block physical
input until the idle deadline. A release already accepted by the production
K2 policy remains an accepted product intent; its engineering PM peer stays
bound long enough for the real coordinator to reconcile or reach its own
deadline. The status flag identifies this retained intent. Repeated close is
idempotent and cannot replay or cancel the accepted product action.

The engineering PM peer is unavailable until the common production key policy
has accepted a qualifying release. Tests cannot invoke the peer helper alone
and count that as a K2 or coordinator result.

The read-only BKS1 snapshot reports bounded product observations, not internal
object names: voice is unavailable/idle/busy, storage is unavailable/ready,
and network is offline/link/ready. Together with existing authenticated INFO,
the HIL JSON carries firmware version/build/security counter, the manifest
source SHA supplied by the runner, test-mode identity, and power state.

### Factory diagnostics enrollment and audio BKTEST (2026-09-29)

`USER-20260929-FACTORY-BKTEST` permits a destructive factory-init image but
does not permit a lasting test backdoor.  A normal product build has no
factory-diagnostics command and no engineering configuration kinds.  The
factory engineering build may accept exactly one nonzero `BKD1` client/key
through the existing echo-disabled CH340 `bkprov-v1` operator channel only
after the SFJ1/SFB1 factory transaction is READY, a generated BPI2 identity is
active, and no owner control key is bound.  Missing, malformed, already-used,
expired, owner-bound, or non-factory states reject without opening native USB
or changing persistent authorization.

The accepted principal lives only in RAM, carries diagnostics capability only,
and has a fixed monotonic deadline.  Expiry, explicit revoke, claim/control
binding, or reboot clears it and causes the native USB lease to fail current
snapshot validation.  Revocation is idempotent and never writes PCG1, owner,
Wi-Fi/cloud settings, identity, trust, calibration, or external SD.  A fresh
reboot is credential-free and requires another physical factory enrollment.

The CP channel returns only public state and the device leaf-certificate
SHA-256.  The host must match that value to the negotiated native-USB TLS leaf
before sealing the random principal in a CurrentUser-DPAPI profile.  The random
principal must not appear in argv, stdout/stderr, JSON evidence, or a retained
plaintext file.  A pin mismatch or unavailable OS protection leaves no profile
and revokes the transient source.

`AUD-03.factory-bktest-audio` uses engineering config kind 20 and one fixed
`BKA1 run` record.  It drives the deployed Agent `audio_playback` and Media
owner through normal EOF/drain, cancel plus rejected post-cancel write/drain,
and a fresh following EOF/drain.  It accepts no external PCM/path/URL, never
changes volume or persistent data, and returns a bounded `BAS1` receipt with
byte counts and stage results.  Host tests replace only Media/clock boundaries;
board PASS proves digital lifecycle and owner release, not audible output,
DMA/I2S, acoustic quality, online TTS parsing, or lack of xrun/noise.

Before implementation the absent transient source, factory enrollment command,
and audio kind are recorded as `BLOCKED_INTERFACE`, while the existing K2 and
startup audio validation remain Green baselines.  Missing symbols or command
routes are not counted as product assertion failures.  After implementation,
the same selected cases must exercise the production PC guard, key/coordinator,
Agent playback adapter, and explicit revoke path.
