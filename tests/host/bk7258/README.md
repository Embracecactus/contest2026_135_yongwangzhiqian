# BK7258 主机回归测试

本目录直接编译仓库中的现役 `chips/bk7258` 实现，用主机 mock 隔离 MMIO、SDK 和
NuttX 内核接口。它用于快速发现源码迁移、生成 ABI 和纯逻辑回归，不代替固件构建或
任一块 BK7258 物理板的实板 xTS。

## 运行

依赖 GCC/Clang、Python 3、`pkg-config` 和 cmocka。唯一完整入口是：

```bash
make -C tests/host/bk7258 check
```

Host 测试不映射进 OpenVela 应用树。`check` 从干净的
`tests/host/bk7258/build/` 开始，记录提交、编译器、Python、cmocka、sanitizer 和
权威分区 CSV 哈希，然后依次执行公共模块、BL1、BL2 和 AP/CP 外设测试。
成功结束必须出现 `BK7258_HOST_TEST_PASS`。构建产物只写入该 `build/` 目录。

也可在本目录执行分层入口：

```bash
make run-core
make run-bl1
make run-bl2
make run-ap
make run-voice-tls-concurrency
make run-provision
make run-voice-kws
make run-preferences
```

`run-voice-kws` 直接编译已同步的 TFLM microfrontend 与固定点 KissFFT，检查
上游输出、批处理/流式特征一致性及模型训练清单审计。该入口需 NumPy/pytest 和 C++ 编译器，不下载依赖、不使用
真人语料，也不构成唤醒准确率、能量门限标定或板端实时性验收。
`run-preferences` 检查 KVDB 适配的缺省/范围/错误传播，以及真实 CP 命令的 RPC 编码；
KVDB 后端和 RPC 传输在这些测试中为替身，不证明持久化或实板配置生效。

`run-voice-tls-concurrency` 编译实际 mbedTLS provider，以可控 SSL I/O 替身检查
WANT_WRITE 重试期间的独占、空闲读允许上行，以及故障/取消后的双向停止。
`run-provision` 依次运行供应 helper、产品 GATT 窗口、队列和连接代际测试，以及使用实际 mbedTLS 的
认领、本地按键 owner、Wi-Fi/Gateway 试连、存储与回执、设置、身份、时间和 TLS 测试。
它需 CMake、OpenSSL 和工作区 mbedTLS 源码；构建产物、测试证书和私有存储都位于运行后
自动清理的临时目录。不启动 LAN 服务，也不代表实板验收。

## 当前覆盖

- 公共层：RPTUN mailbox、CP/AP RPTUN core、PM activity、BL1 policy；
- BL1：libc、SHA-256、flash、clock、runtime 和现行 Beken manifest；
- BL2：security counter、flash-map/CRC trailer 写入和 CP/AP pair policy；
- AP/CP 外设：JPEG、YUV/H.264、scale/rotate、CAN 和 IrDA。
- App 纯逻辑：授权 voice-pack/WAV gate，以及 transport-neutral `companion-v1`
  network-byte-order codec、sequence/window/cancel/reconnect 状态契约；半双工 turn arbiter
  的 MIC/DAC 严格释放顺序、重放/旧 token、超时、取消和逐阶段故障回滚；下行测试还以
  一帧初始额度连续接收六帧，检查每次 DAC 接受后才返还同量窗口。

分区头不使用历史副本，而是由
`boards/bk7258/common/partitions/bk7258/bk7258_ab_agent_onchip_persistent.csv`
在 `build/layout/` 临时生成。BL1 公钥 fixture 是确定性的公钥字节，仅用于模拟验签
ABI；它不是私钥、下载密钥或可部署信任根。BL2 不固定断言任何签名公钥，因为正式
构建必须按每一代的新密钥生成对应源码。

## 边界

主机 PASS 只能证明被编译模块的逻辑和 ABI。它不证明串口、时钟、电源、真实 flash、
CAN 收发器、RTC、存储介质或 12 小时稳定性。涉及硬件的状态必须另附当前代构建身份、
下载边界、原始串口日志和恢复结果。

测试源码的许可证范围、SDK/NuttX 接口替身和公开密钥夹具来源见
[`PROVENANCE.md`](PROVENANCE.md)。

## v2 需求契约与测试入口

用户已取消旧版“不新增测试文件”限制。当前测试规格与审阅入口为
[acceptance/contracts.md](acceptance/contracts.md)，56项原编号在
[acceptance/cases.v1.json](acceptance/cases.v1.json)。执行：

```bash
make -C tests/host/bk7258 run-shaniu-contracts
```

该入口保持失败退出码，逐例输出到 `out/shaniu-contract-v2/`；不连设备、不安装、
不修业务。未绑定接口/设备不计通过。下方为首轮提交272b3b2f的历史测试记录，
其文件数量约束已由v2覆盖，结果不替代本轮基线。

## 2026-09-24 架构计划：测试先行审阅稿

本节是用户实施计划的测试规格，不是新增实板验收报告。当前检查点为：先写测试、
在主机检查测试能运行，再等待用户确认；不推进产品修复、刷板、安装、提交或推送。
沿用现有测试文件和平台。尚无稳定接口的场景列为“待接口”，不能用空断言、mock 自证
或 skip 数量宣称功能完成。以下 Q 编号沿用工程历史问题，子场景不改变历史销项状态。

本轮源码基线为主仓 `d06b265e76d4fd1ce6add90d9469926221cf5672`，分支
`dev-ai-contest-2026`；manifest 固定 Agent `62a304ea69c4076f0f3ef7955a9af69a5ed35277`。
这不证明工作区依赖、已部署固件、模型或 APK 与其一致；它们属于 M0 现场前置条件。

### 新增可执行回归与本轮结果

| 现有文件/入口 | 新增假设与断言；不覆盖时的风险 | 本轮结果 |
| --- | --- | --- |
| `test_bk7258_product_keys.c` / `run-product-keys` | 音量长按不累计关机资格；K2 满三秒只在松手请求一次；无中间 held 采样时也按压下/松手时间判定。避免采样节奏决定用户关机意图 | 失败：最后一项 `test_release_boundary_without_held_heartbeat` 未产生请求；前序断言通过 |
| `test_bk7258_usbmode_lease.c` / `run-usbmode-lease` | 查询/重复同模式不重新枚举；CDC 停止失败不启动 MSC；MSC 启动失败回滚，回滚失败明确 NONE；MSC 停止失败不让本地取得块设备租约。避免失败后双重所有权 | 通过；只验证模式管理与 mock 后端，不证明真实卷已卸载 |
| `test_bk7258_motion_core.c` / `run-motion-core` | 上次成功后本次读取失败，响应必须清除旧采样、绑定新会话/序号；非法请求不能打开设备。避免失败响应冒充新动作 | 通过；不证明动作识别、唯一采样者或时延 |
| `test_bk7258_nfc_core.c` / `run-nfc-core` | 上次检测成功后读取失败不能残留 present；非法请求不触达硬件。避免错误触发场景 | 通过；不证明卡片去重或授权 |
| Android `DeviceControlSessionTest.kt` | 配置取消等待在途 ACK 后优先发送；暂存期间拒绝其他写入；失败 ACK 不提前释放事务；释放身份后拒绝迟到结果且重连不重放。避免事务交错/旧会话覆盖 | 通过 |
| Android `ProvisionSettingsTest.kt` | 换 Wi-Fi 编码不携带云配置；保留/替换/清空标志互斥语义；绑定期望 revision；拒绝耗尽 revision、空操作 ID 和空补丁。避免换网清 Key 或无效事务 | 通过；仅编码约束，设备保存/应用仍待验 |
| Android `OtaControlUploadTest.kt` | 取消完成后迟到 BEGIN ACK 不改变 CANCELED 终态、不重新发送数据。避免结束结果被旧回调改写 | 失败：`lateAcknowledgementCannotOverwriteCanceledTerminalState` 得到 FAILED |

新增 12 个测试函数，覆盖上述 7 个既有文件。定向执行 4 个主机入口，3 通过、1 失败；
3 个 JVM 测试类共 29 个用例，28 通过、1 失败。失败用例保持真实断言，没有 xfail 或
放宽判据。K2 失败不是 HardFault 根因证明；OTA 失败是上传对象自身的终态契约，
控制会话会过滤部分迟到消息，不能直接外推为线上复现。

本轮完整输出在 `out/shaniu-test-first-20260924/{host,android-unit}.log`；该目录为
本地忽略产物，不属于交付源码。JVM 报告在
`android/shaniu-companion/app/build/reports/tests/testDebugUnitTest/index.html`。
以下只运行主机测试，不连接板卡或手机；`make -k` 用于保留独立目标的结果：

```bash
make -k -C tests/host/bk7258 run-product-keys run-usbmode-lease run-motion-core run-nfc-core
cd android/shaniu-companion
./gradlew :app:testDebugUnitTest --offline \
  --tests 'com.shaniu.companion.provision.DeviceControlSessionTest' \
  --tests 'com.shaniu.companion.provision.ProvisionSettingsTest' \
  --tests 'com.shaniu.companion.ota.OtaControlUploadTest'
```

### 统一记录与停止规则

每次场景记录：用例、源码/依赖、构建及 ELF 哈希、设备身份、固件/资源/原唤醒模型/
“我在”哈希、APK 版本和签名、配置 revision、会话/任务标识、刺激、时间线、期望、
实际、原始日志路径、恢复结果。秘密仅记录是否配置与必要哈希，不输出 Key 或私有内容。
证据等级分源码/主机、模拟器、真实板端；状态分通过、失败、未执行、待接口、待设备。
本节除上表外全部未执行。统计附样本数、失败数、分组和测量端点；样本不足不宣称 p95 达标。

第一次准确失败即保存现场并停止该实验，下一次必须增加信息；不以重复刷板、加缓冲、
加超时替代定位。故障后的正常恢复路径单独记录，不自动格式化、清身份或重刷来掩盖失败。
物理步骤在用户确认后明确提示并等待反馈，不能将未反馈当作已经完成。

### M0：身份与可重复基线（Q01、Q05、Q06、Q10）

1. 只读核对两仓实际 HEAD、脏改动、manifest、构建配置、ELF/固件对应关系；
   读取实际 APK 包版本/签名及资源哈希。任一不一致先解释，不回退覆盖后续正确修改。
2. 复核 COM13、Mi 10 历史 serial `59d707dc` 的实际设备和占用；不抢占资源。
   锁定原唤醒模型、前端/策略和“我在”的实际生产数据流；缺哈希则该基线待验。
3. 冷启动分别标记第一反馈、本地管理、原模型监听、云就绪、资源就绪；
   断网重复，确认本地管理不依赖云。目标本地就绪 p95≤5 秒且相对改善 30%，不是当前成绩。
4. 保存首故障有效上下文、复位原因、对应 ELF 和资源高水位；分别标记 658 HardFault、
   主动复位、早期睡眠失败、真实唤醒。只有诊断输出不能关闭 Q01。
5. 测量可分配内存/内部配置容量，而非使用分区或 PSRAM 总量；按能力记录 CPU 平均/p95、
   最长阻塞、ISR/临界区、栈高水位、常驻/峰值内存、DMA、无线、SD 写频与退出条件。
   未测量值填 UNKNOWN；不因驱动存在声称电机、双麦、充电关机或 USB 音频可用。

### M1：稳定性、配置和所有权

| 映射/前置 | 操作和故障注入 | 必须观察到的结果/证据 |
| --- | --- | --- |
| Q01 K2；先跑主机按键回归 | 短按、约三秒松手、重复松手；更长按进入恢复出厂确认后取消/超时；K1/K3 长按 | 关机只提交一次；恢复确认撤销关机候选，取消不补关机；K1/K3 只改音量。恢复阈值先经交互确认，不在测试里臆定数值 |
| Q01 生命周期；待退出握手接口 | 分别在收音、播报、扫描、安装、持续 App 查询时发关机意图；让一个参与者拒绝/超时退出 | 拒绝新普通任务；每个生产者、消费者、DMA/回调给出真实退出确认后才进入电源转换；超时不是完成；失败明确提示且不悄悄恢复采集；部分硬件关闭不能只改状态变量恢复 |
| Q01 电源；待设备 | 定向修复验证后至少 10 轮关机/按既有方式恢复，USB/电池分列；关闭期间插 USB、收到通知 | 原配置保持；插线/普通事件不撤销关机意图；CP 受理、复位原因和睡眠入口可解释。真实断电/关机充电单独测，不由 reboot-to-sleep 推断 |
| Q07 转交/重置；须确认数据范围与备份 | 先完整授权/物理确认/持久回执/新认领，再单独安排提交边界中断实验 | 旧凭据失效、新认领可用，无双重权限；保留硬件校准、TLS 身份、信任/防回滚状态；私有资源按确认范围处理；取消无破坏 |
| Q08 配置；现有 run-preferences/run-provision | 保存 A 尚未应用时保存 B；A 迟到成功/失败；换 Wi-Fi、断网、SD 暂不可用后读回；显式保留/替换/清空各一例 | desired_revision 为可靠保存选择，applied_revision 为实际应用；A 不覆盖 B；换网不清云 Key；失败不伪报应用成功；关键身份/小配置不因 SD 故障变空 |
| Q01/Q08 卷；现有 run-media-volume 与偏好存储测试 | 有打开文件/数据库时请求维护 MSC；关闭、排空、卸载各阶段失败；主机释放后恢复本地 | 单一所有者；任何交接失败不强行授予主机；正常路径关闭/排空/卸载后才交接；恢复后旧任务/缓存失效且不访问悬空对象；不自动格式化。统一文件系统契约仍需源码和设备核对 |
| Q04 音频；现有 run-media-audio-session/run-voice-media-recorder/run-agent-capture/run-voice-media-player/run-agent-tts-queue | 取消与最后 PCM/EOF/回调交错；断网、解析无效心跳、暂停恢复；分别本地 PCM、只接收、完整云播放 | 有效网络/解析/PCM/消费/输出进度分开；无效心跳不无限续期；消费者/DMA 退出可证；有限恢复或明确结束，无旧音频串入新对话 |

上述既有音频、存储、供应测试本轮未重跑；它们是下一轮入口，不是新增通过证据。

### M2：连接、对话和原生 App

| 映射 | 前置和刺激 | 判定条件 |
| --- | --- | --- |
| Q08 快照/事件；待统一服务接口 | 首次认证、正常变化、丢事件后重连、慢客户端；旧固件有限轮询 | 首连能力+快照，变化通知且能补快照/检测掉线；查询不扫描、挂盘、加载资源或开麦；遥测可合并，关键结果可查询，不被慢客户端阻塞 |
| Q08 多客户端/异步任务；待接口 | 已独立授权手机与电脑同时轻量写入；安装/录音/OTA/关机竞争；断线后查任务；旧会话回调 | revision/会话拒绝旧写入；独占任务仲裁明确；返回任务 ID，区分受理/进行/提交/成功/失败/取消/结果未知；大文件不饿死短控制；不复制手机 Keystore 到电脑 |
| Q02/Q03 Agent 与表情 | 普通完整无工具正文、混合工具、部分/重复/缺失 ID、工具失败/取消、视觉超时后下一轮 | 合法完整无工具正文复用；正式工具执行留账，结果不丢重；不播放中间推测，不全局禁 tools；终答和 rearm 明确；表情发意图后异步确认，不在语音回调解码/挂盘 |
| Q02/Q04 网络对照 | 相同条件下 BLE 未连接、认证但停轮询、正常轮询、设置/扫描负载；各跑本地与在线音频 | 比较有效 PCM 空档、欠载、吱响、EOF、取消和完整性；不能由一次关闭 App 改善认定 BLE 根因；没有声学采集只记录 Media 写入，不能叫真实出声 |
| Q05/Q06 原模型与“我在” | 固定实际哈希，真人按音量/语速/语调分组；可靠负例小时级；另列数字 gain/回放/同人变速 | 总体≥95%、关键组≥90%、误唤醒≤0.5 次/小时为目标；同时报告漏唤醒和失败组；听到指定应答，不静默换模型，不以合成结果替代真人验收 |
| Q09 原生 UI；现有 DeviceUiAcceptance | 按发布 Canva 从“云服务与模型”及后续页面逐页比较；浅/深色、大字体、键盘、后台/重建、20 轮导航；真机/模拟器分列 | 原生实现保留；布局/字段/操作完整，编辑草稿不丢、无重复连接，迟到结果不覆盖新页/设备，离线配置可用，过期状态不允许危险写入，无假成功 |

普通语音至少 30 轮，工具/视觉/冷热连接单列。记录完整请求成本、首个有效句与最终结束、
失败率和内容完整性；有效首句改善 30%、争取 p50≤3 秒/p95≤5 秒是目标，不靠截断回答
或假提示音达标。具体云服务、网络、音量与采集位置随结果固定。

### M3：新场景验收规格（待接口，均未执行）

| 场景 | Given / When | Then 与边界 |
| --- | --- | --- |
| N1 本地动作 | 同一加速度计采样所有者；拿起/放下/倾斜、持续重复、轻晃；分别播放、收音、桌面振动 | 有界短表情与去重，不改 owner/网络/电源；事件到可见反馈 p95 争取≤150ms；区分喇叭/电机自触发；不新增逐事件 LLM 调用 |
| N1 微表情/触觉 | listening/processing/completed/focus/charging/low battery 真实事件；低电量/录音/OTA 关键阶段 | 本地状态驱动且可恢复，动画不拖慢语音；电机仅硬件验证后有限脉冲，禁止阶段及时停止；未验证则该项待设备 |
| N2 预览/试用/默认 | 手机预览；设备限时试用；超时、取消、断连；最后显式设默认并重启 | 预览无设备改动；试用有明确期限和恢复目标，不反复写 SD；只有设默认持久化；恢复时不覆盖更新的用户选择 |
| N2 专注计时 | 断云下开始/暂停/继续/取消；重复命令；任务冲突；调整墙钟/断电 | 一个单调时基倒计时，暂停剩余时间稳定，结束提示一次；取消不再响；墙钟不影响时长；跨掉电策略另行确认，时间不可信不谎报准时恢复，不承诺关机唤醒 |
| N2 NFC 场景卡 | 绑定低风险场景，同一卡停留/移开重放、未知卡；App/已支持语音发相同动作 | 共用场景入口且去重，不不断重启计时；UID 不授权、不清 owner、不传 Key；语音离线能力单列，不把本地计时等同离线识别 |
| N3 USB 授权 | 原生 CDC 识别身份/能力；浏览器开串口、DTR 变化；无授权/错误身份；与调试日志并行 | 不复位、不自动授予 owner；稳定分包/流控协议不与 Shell 混流；CH340 不冒充产品 USB；开发诊断恢复仍可用 |
| N3 资源安装 | 电脑不同网段/无 Wi-Fi，上传合法/损坏/不兼容眼睛包；分包重复/断线、取消、提交中断；重连查询 | 文件级统一安装任务可查；校验/安装/激活分阶段；旧有效包在失败后保留；试用与默认区分；不切 MSC、不暴露任意文件写入；提交中不能取消时明确说明真实状态 |
| N3 任务提醒 | 用户配置工具发 start/progress/success/failure/cancel；重复 event、乱序、超限、过期、终态后的进度；收音/播报中到达 | task/event 去重、有期限/限流，终态不被旧进度覆盖；不抢音频；只传状态/允许摘要，不发完整敏感日志；拔 USB 后设备继续独立工作 |
| 共同场景层 | 场景执行中取消、资源忙、关机或高优先事件；重复/递归触发 | 有限允许动作、优先级、期限与恢复；复用音频/显示/存储所有者，禁止任意脚本与无限重试；确认前不在板端开启新增并发 |

### M4/M5：组合回归与交付出口

- M4：先复测原失败刺激，再组合语音+动作、专注+卡片、USB 导入+取消/语音、
  配置+掉线、OTA+新事件、K2+忙资源。确认安全边界后至少一轮 24 小时观察，记录
  未解释 Fault/复位、资源高水位、失败和恢复；有限零故障不能宣称彻底消除。
  每个 N 场景至少一轮真实纵向流程，Q 项依据实际证据销项，不能只凭主机 PASS。
- M5：正式验收从授权后的工厂全量部署、首次初始化、屏幕 QR、离线认领、认证配置开始；
  日常验证依改动增量构建/合法 OTA，同一轮持久化/K2/网络验证不反复全刷。
  外部 SD 恢复另定备份/清理范围；本规格不授权立即执行破坏性步骤。
- 冷构建固定两仓提交、manifest、身份/信任链、软件包/可物化同板 BIN、APK/OTA 和资源；
  用现有交付检查入口核对 SHA256/签名及 Actions 对应提交，下载后复验。
  第三方流程不得依赖作者旧密钥目录/历史整片 base；设备硬件数据经正式工具核验，
  不关闭验签、不降低计数、不写 OTP/eFuse。公开默认与私有定制分开。
- 交付 App/电脑工作台步骤、能力/预算、问题矩阵和原始证据路径；CI、主机、模拟器、
  现场结论分列。已知 HardFault、数据破坏或 K2 高风险缺口未闭环时，不标首期推荐发布。

本轮出口：测试代码和待接口/实板测试规格可供审阅，已保留两个真实失败。
下一步需用户确认后再处理实现缺口与安排现场验证。

### S0 实施放行后的运行器门禁（2026-09-24）

历史 e3ecd6b8 和 baseline-20260924.json 保持不变。运行器默认使用新的时间戳目录；
可用 SHANIU_CONTRACT_OUT 指定独立目录。required-units.v1.json 固定本轮必须收集的
63 个执行 ID，不包含未来尚未接线的 56 项父规格。父规格另列 interface 和
各层 evidence_by_layer；PARTIAL 表示仅有部分绑定，不代表该层验收完成。

运行器自测：`python3 tests/host/bk7258/test_shaniu_runner_gate.py`。
修复前 9 个测试中 1 通过、7 断言失败、1 损坏 XML 异常；修复后包括总门禁的
12 个测试全部通过。原始证据：out/shaniu-s0/gate-before.log、gate-after.log。
S0 业务源码不变，完整复跑仍是 63 个：59 PASS、4 FAIL_ASSERTION，退出 1。
两个隔离变异均检出，恢复复验通过；新报告在 out/shaniu-s0/contracts/results.json。
门禁检查 XML 新鲜度、解析/身份、非零收集、必需 ID、重复、跳过、error 和非 PASS；
Gradle 退出 0 不能覆盖上述失败。此提交没有修复业务或执行设备操作。

### S1 K2 / OTA 终态（2026-09-24）

完整选择集为原 63 ID + 5 新 ID，共 68。先跑新增边界及纠正后的 close 期望，
生产未改时得到 58 PASS / 10 FAIL_ASSERTION / 0 SETUP_ERROR；修复后 68 PASS。
原 63 中四条原 Red 转绿，原 ID 全部重新收集；其中 closeCancelsWithoutSending
的错误期望纠正详见 contracts.md，历史报告不变。两个恢复复验属于原 63，
不是额外重复计算；两项变异独立记录为 DETECTED。门禁自测另计 12 PASS。

K2 在可信同会话消抖释放边沿核验实际 3000ms 时长，不要求 held 消息；
组合键、会话更换、时钟回退和重复释放有回归。OTA 已终结对象忽略迟到回执，
本地关闭用 CLOSED 区分；取消未确认仍 WAITING，远端拒绝保留其错误。
未更改 SDC1 协议：另跑 DeviceControlProtocolTest 的 14 个认证/序号/非法帧等
回归均通过。`:app:assembleDebug --offline` 成功，未安装 APK。
报告：acceptance/s1-before-20260924.json、s1-after-20260924.json；
原始日志分别在 out/shaniu-s1/before、after、protocol.log、assemble-debug.log。
这只证明主机生产模块/既有协议路径及 Android 构建，不证明板端关机、深睡、
HardFault 根因、远端安装完成或全部 56 项产品需求通过。

### S2 卷租约转换子切片（2026-09-24）

先加入真实 media_volume 模块、外部 USB 租约回调上的确定性交错测试：
MSC-01.acquiring 和 MSC-01.releasing 均在底层重复/过早释放观察器断言失败，
MSC-01.retry 原本通过。修复为原子保留转换占位，租约取得后才发布 owner，
释放期间拒绝再次释放，失败恢复可重试 owner。占位复用原 int，不新增线程、
缓冲或等待；真实 CPU/阻塞/栈及设备资源测量仍未执行。

完整集合 71 PASS = 原 63 + 累计新增 8；2 恢复已包含于原 63，2 个变异单独检出。
另外既有 run-media-volume / run-usbmode-lease 通过、运行器自测 12 PASS。
报告 acceptance/s2-20260924.json；原始日志 out/shaniu-s2/。
这是外部调用边界的确定性交错，非真实线程调度、文件系统句柄/DMA 或实板证明；
未宣称整个 MSC-01 或退出/卷架构完成，未改变硬件配置或依赖版本。

### S3 真实 Agent 采集退出证据（2026-09-24）

新增 LIFE-02.capture-close-failure / capture-route-failure，直接编译固定 Agent
的 voice/audio_capture.c，复用 socket Media 对端。注入 Media close 或路由释放
失败后，cleanup 必须返回错误、新 open 必须拒绝、不得提前释放路由；恢复后
cleanup 仅完成一次，新的采集可取得独立期望 PCM。这两项基线即绿，未修改
生产代码或 Agent manifest。旧 Dolphin 独立 recorder 不充当傻妞生产路径证据。
1ms 为测试调用传入的重试预算，不是冻结整机关机退出期限。

完整选择集 73 PASS = 原 63 + 累计新增 10；原 2 项变异/恢复继续通过。
另用 `python3 tests/host/bk7258/test_shaniu_capture_release.py` 在临时目录分别
忽略真实 close / route-release 错误，两个可编译变异均在指定 cleanup 断言被
检出，恢复后两次通过；这 4 次不加入 73 的分母。既有 capture 套件独立复跑通过。
新增脚本与运行器使用 black 24.10.0 格式检查，git diff --check 通过。

报告 acceptance/s3-20260924.json 记录真实生产源码/头文件及夹具哈希；原始日志
out/shaniu-s3/。这些证据不覆盖真实 Media dispatcher、DMA/IRQ、整机电源协调器
或实板 LIFE-02；不能声称该父需求已完成。下一切片须把产品电源协调器的
部分退出/恢复失败接入生产路径测试，不能用采集模块局部通过替代。

### S4 产品电源协调器停止边界（2026-09-24）

ace30aef 将原全局状态、power_request/restore 和 keys_step 原样移入同一生产
编译单元包含的 bk7258_agent_product_power.inc；机械重建与拆分前全文相等，
未引入替代协调器。随后测试直接包含这份生产实现，仅资源参与者与 CP 为对端。
新增五个 LIFE-02.power-*：normal、admission-failure、partial-failure、
cp-declined、cp-unknown。修复前 2 PASS / 3 FAIL_ASSERTION，修复后 5 PASS。

修复检查 admission quiesce 返回值；部分失败或 CP 明确未受理后保留停止边界，
不再自动恢复资源/普通业务；明确新 K2 关机意图可重试，复用已退出资源的确认。
CP 结果未知继续等待，不当成已关机。未增加线程、缓冲或循环；增加一个失败
状态布尔量，保留既有轮询及 30s 值，未把这些原实现参数称为新冻结性能合同。

总选择集 78 PASS（原 63 + 累计新增 15），原两项变异检出和恢复仍通过。
AIDK AP 真实现有编译配置下产品对象编译成功，依赖文件包含生产 .inc；没有
完整固件链接/签名/刷写。初次自定义对象编译命令缺少 -MMD，修正后成功，
该工具调用错误单列，未计业务 Red。保留既有非电源代码 initializer 警告。
报告 acceptance/s4-20260924.json；日志 out/shaniu-s4/。black 24.10.0 和
差异空白检查通过；当前 PATH 无 clang-format，未声称该风格检查通过。

未覆盖：真实资源退出整合、运行时故障提示界面、已运行 KWS 的失败清理、
DMA/IRQ、物理电流/睡眠、完整电源恢复与重置。该子切片不关闭 LIFE-01/02/PWR-01。
后续需让失败状态可由正式快照查询并完成实际参与者的退出确认，不能以
主机循环被阻止替代所有硬件资源停止。

### S5 关机失败不遗漏 KWS 退出（2026-09-24）

新增 LIFE-02.power-admission-stops-trigger / storage-stops-trigger /
trigger-failure / unpublished-trigger。前两项在真实协调器上未调用 trigger_stop，
第四项在启动成功标志为假时未清理便提交 CP，均先失败；停止失败阻止 CP 的用例
原本通过。修复后记录真实停止成功确认，不从 started 推测无资源；请求 KWS
停止及对话 recover 不被入口关闭错误短路，也不再排在存储退出之后。

完整集合 82 PASS（原 63 + 累计新增 19）；原两项变异继续检出及恢复通过，
运行器自测 12 PASS。实际 AIDK AP 产品对象编译通过，非完整固件/实板验证。
没有新增线程、动态分配或轮询，增加一个停止确认布尔量。真实 stop 的阻塞及
DMA/IRQ 确认尚未实测；本组替身只代表外部参与者，不声称 KWS/Media 联合验收。
初始定向反例未采逐例耗时，报告明确缺失，不反推数字；完整复跑逐例耗时齐备。
报告 acceptance/s5-20260924.json，原始日志 out/shaniu-s5/；未改原模型/应答资源。

### S6 退出握手继续驱动已受理任务（2026-09-24）

实际 bkprov_owner_quiesce 会在清理未完成时返回 -EAGAIN。S4/S5 的新错误分支
把它当永久失败，失败分支又跳过 config/network step，存在无法收敛的缺口。
新增 LIFE-02.power-admission-drains / failed-drains 在修复前均断言失败；
前者验证暂忙无需新按键即可继续完成，后者验证永久失败仍驱动已有任务清理，
但不重新开放业务或擅自提交 CP。修复区分 EAGAIN/EBUSY 与永久错误，保持既有
退出期限；存储已停止时不继续运行配置提交步骤。

完整集合 84 PASS = 原 63 + 累计新增 21，原变异及恢复仍通过，门禁自测 12 PASS。
AIDK AP 产品对象编译通过，未完整链接、签名或部署。没有新增线程、分配或等待；
复用既有循环，实际耗时预算及全链路仍待测。仅证明真实协调器调用进展与门禁，
资源参与者为外部 peer，不能称真实持久提交/网络释放已联合通过。
报告 acceptance/s6-20260924.json；原始日志及修复前输入哈希 out/shaniu-s6/。
S4/S5 历史报告不改写。额外查读确认音量查询仅调用 policy get，本轮没有为
未经证实的“查询启动采集”假设引入缓存或改变音量行为。

### S7 停止接收新写入的协议能力（2026-09-24）

先写独立 SDC1 字节 peer，再增加 session_quiesce；新接口原先不存在，编译
缺口记 BLOCKED_INTERFACE，不算业务 Red。5 个 NET-03.quiesce-* 覆盖查询、
OTA/config 暂存与提交门禁、认证前拒绝、非法值和旧序号；真实 parser/状态机
生产源码参与编译，外部业务执行为计数观察器。接口/允许命令见 contracts.md。

当前 89 PASS（原 63 + 累计新增 26），原两项变异/恢复仍通过，运行器 12 PASS。
另外原 C control_session 套件通过；隔离删除普通写入门禁的可编译变异被检出，
恢复后通过，两次不加入 89。AIDK AP control_session / provision_owner 对象
编译通过，无完整链接/实板结果。仅增加一个会话布尔量，无线程/分配/等待。
报告 acceptance/s7-20260924.json，日志 out/shaniu-s7/。默认会话行为保持；
**尚未接入生产 owner 的两阶段退出，设备关机查询仍不能据此称已实现。**

### S8 owner 与电源生产链路接入（2026-09-24）

新增 owner_prepare_stop 并实际接入产品电源循环，最后资源退出后仍调用原完整
quiesce。新增 API 的前置编译缺口为 BLOCKED_INTERFACE；已有最终关闭顺序的
反例在改业务前断言失败，修复后通过。主机联动编译真实 product power、owner、
control_session、scan；只替换 TLS/GATT 及外部资源。认证通过真实 AUTH 报文，
查询/写入通过真实 parser，不把 session.authenticated 直接设置成成功。

新增 6 单元：owner 查询/未认证/非法序号、既有 owner 整套回归、最终传输关闭
排空、协调器+owner+parser 联动。存储失败时查询仍可推进，写入 EBUSY，明确
重试后退出资源→关闭传输→请求 CP；持续查询不阻止最后关闭。旧身份/恢复出厂
关闭接口保持，未接入未定恢复手势。

完整集合 95 PASS（原 63 + 累计新增 32），原两变异/恢复通过，门禁自测 12 PASS。
另跑既有 test_provision_tls.py 的 1 个集成单元通过（真实 mbedTLS 分片/关闭），
不把它称新电源路径的真实 BLE 测试。AIDK AP product/owner 对象编译通过。
新增一个 owner draining 标志，无新线程/分配/轮询；原会话处理随既有循环执行。
相关延迟/内存高水位、真实设备退出和物理恢复未测。
报告 acceptance/s8-20260924.json，日志 out/shaniu-s8/；新联动的首次编译有
scan_busy 测试替身声明冲突，移除替身、链接真实 scan 后通过，未算业务 Red。

### S9 关机失败提示（2026-09-24）

先新增 LIFE-02.power-failure-display，真实协调器在存储退出失败后未发送失败
显示意图，断言失败；修复后发送 phase=3，普通轮询保留，明确新按键恢复 phase=2。
内置失败图标为红色 X，不依赖 SD，用形状区分进行中电源图标。DISP-01.power-pixels
编译实际生产像素函数，验证旧 0/1/2 图案逐像素保持及失败形状。新 helper 没有
旧生产接口，因此不把其首次通过称为原功能 Red。只验证像素与意图，未覆盖真实
framebuffer/显示线程完成确认；不把通知成功当屏幕已显示。

冻结源码完整复跑 97 PASS（原 63 + 累计新增 34）；原两变异检出/恢复保留，
运行器 12 PASS。AP product/display 对象编译通过，未完整链接或部署。
首次运行期间调整 include 位置，虽然 97 断言通过但源码变化门禁拒绝，保留
out/shaniu-s9/contracts/ 及失败说明；有效报告为 acceptance/s9-20260924.json，
附证据 s9-evidence-20260924.json。未改写旧报告。

只读 Windows 枚举为零串口，ADB 仅 emulator-5554，未列历史 Mi 10。未打开串口、
刷写或安装。无新增线程/分配/等待，复用原显示缓冲；CPU/实际显示延迟仍待测。

### S10 配置持久化故障跨层回归（2026-09-24）

新增 STORE-02.file-sync-failure / file-sync-retry / directory-sync-unknown，
编译真实配置合并、存储 worker/store、解码与 HTTP writer，仅包装外部 fsync。
文件同步失败时公开 SCS1 报 FAILED/-EIO，存储重开仍为旧版本/owner/Key；
明确新操作重试只提交到 revision 2，HTTP peer 核验保留测试 Key。目录同步
失败不报持久成功，同挂载读回/refresh 不解除未知，也不接受重复 APPLY。

首次两个测试误把 receipt 当操作错误接口，预期 -EIO；源码合同表明 receipt
只回答持久事务身份，因此应未知，具体 -EIO 从 SCS1 读取。修正测试观察器，
生产未改；原失败存档 s10-oracle-error-20260924.json，不称产品 Red。
校正后 100 PASS（原63 + 新增累计37）；原两变异/恢复保留。额外忽略 fsync
错误的隔离变异及恢复单列 s10-mutation-20260924.json，不加入100分母。
运行器12 PASS。没有修改生产、依赖或阈值，没有新构建/实板成绩。

目录同步失败后的可靠恢复仍未完成，本例只验证不误报成功；POSIX 与目标
LittleFS 的持久化契约分开，不由主机注入宣称掉电通过。当前设备只读基线见
s10-evidence-20260924.json：Mi10 已在线，App仍为42/0.7.12；COM9经端口API及PnP
可见，但尚未打开/确认板身份。未安装App或刷写。

### S11 未知发布后的新进程恢复（2026-09-24）

遵守 storage.h 的既有恢复边界：同一次启动的 refresh/stop 不抹掉未知，
仅新进程重新加载。新增 STORE-02.restart-before-publish / restart-after-publish：
真实配置/存储进程分别在 rename 前、目录 fsync 处注入外部失败，退出后另起
全新进程，读取同一临时目录。前者读旧 revision1/network-A，后者读新
revision2/network-B，均保持 owner/CA/测试 Key，并能通过 SCS1 查询为已存储。
没有通过写入或私有标志构造恢复态；接收端核验真实 HTTP writer 的测试凭据。

102 PASS（原63 + 累计新增39），原两变异及恢复通过，门禁12 PASS。生产、依赖
未改；报告 acceptance/s11-20260924.json，日志 out/shaniu-s11/。两个用例基线
即绿，不制造产品 Red。测试中的进程终止不清宿主机页缓存，不是掉电模拟，
不证明目标 LittleFS/DMA/真实重启后的持久性。S10提到的恢复缺口已补主机
新进程证据，目标板及用户可见恢复仍待验；不为消除未知擅自启用自动重启。

### S12 MSC 后端退出失败保留（2026-09-24）

真实 usbmsc_uninitialize 原样提取为同一 TU 包含的 stop.inc，主机也编译该
函数，只替换锁/临界区、USB驱动和块设备边界。先写 backend-stop-retry：
usbd_deinitialize=-EIO 时不得 close_blockdriver，旧生产逻辑实际触发失败。
修复后关闭回调 admission，保留 inode，解锁并返回错误；显式重试成功后只关
一次 inode。initialize 拒绝覆盖仍保留的实例。normal 用例保留正常退出及
重复退出不再次释放的行为；未新建生产状态机或替代后端。

104 PASS（原63 + 累计新增41），两项原变异/恢复通过，门禁12 PASS。MSC AP
对象编译通过；没有完整链接/硬件结果。报告 s12-20260924.json，原失败、源码
哈希及编译证据 s12-evidence-20260924.json；日志 out/shaniu-s12/。
新增无线程、缓冲、等待或阈值，失败只延长必要 inode 持有。启动失败清理、
close_blockdriver 错误、真实DMA/端点退出及全量文件句柄交接未由这两个用例
证明。外部卷 vfat/fatfs 消费者差异仍存在，不在本片擅自更换文件系统。

### S13 MSC 启动失败清理与模式隔离（2026-09-24）

真实 initialize 原样提取 start.inc。先测试控制器启动失败+反初始化失败，
原代码仍释放 inode；真实 mode_set 在该情况下立即启动 CDC 的第二反例也
失败。修复后启动失败保留资源供显式退出；mode 层清理失败目标前不回滚其他
后端，阻止本地块设备租约。失败的回滚也保留待清理后端。初始化入口对残留
CDC先清理再重试，对残留MSC返回忙（显式set可清理再转换）。

第一轮105 PASS/1 FAIL：旧 MSC-02.legacy-suite要求CDC回滚失败后 initialize
可重试，新门禁过宽破坏该行为。保留旧断言、修正生产后，106 PASS（原63+
累计新增43），原两变异/恢复通过，门禁12 PASS。AP MSC/mode 对象编译通过。
报告 s13-20260924.json；两条原Red和中间回归见 s13-evidence-20260924.json，
原始日志 out/shaniu-s13/。后端start/stop及模式管理均为生产源码，驱动/块设备
及锁为主机边界替身，不证明真实DMA/端点退出。无新增线程/缓冲或超时；只增加
一个待清理后端枚举。close_blockdriver异常及完整卷交接仍有缺口。未刷板。

### S14 Media EOF 的实际PCM账本（2026-09-24）

新增 AUD-03.media-tail / media-cancel-next。复用既有真实 Media 适配器主机
夹具，在已准备会话中调用公开 write_data/close_socket/close，IOCTL边界记录
实际 ENQUEUEBUFFER 的样本及 FINAL。两段2+4字节输入在EOF前仍缓冲，EOF恰好
提交6字节一次；重复EOF/结束后写入不再次提交。取消等待完成后不发完成回调，
新对象提交另一组样本，账本不混旧样本。使用真实播放器/EOF线程，不另写状态机。

夹具仍直接准备播放器对象，未通过真实设备 open/prepare；不宣称完整Media驱动
集成。108 PASS（原63+累计新增45），原两变异/恢复保留；额外隔离“丢弃尾部”
变异被新断言检出，恢复通过，单列 s14-mutation-20260924.json，不计入108。
既有 run-voice-media-player 通过，门禁12 PASS。生产及依赖未改；报告
s14-20260924.json，日志 out/shaniu-s14/。数字sink提交不是声学完成，真实
DMA、DAC/PA、网络断流以及全链路取消仍需后续验证。

### S15 专注计时首个生产入口（2026-09-24）

先写输入/输出合同及clock/replay/invalid用例；接口初始未实现的链接错误记
BLOCKED_INTERFACE，不算业务Red。新增无I/O计时服务，接入产品循环、认证config
kind10、关机/重置取消。暂停后剩余50秒，恢复只运行剩余时长；版本与最后操作
保证重试不重开，旧请求不覆盖新状态。真实SDC1+服务的wire用例使用外部路由
callback；AP产品编译确认接线，但不声称完整owner运行链在主机已执行。

112 PASS（原63+累计新增49），原两变异/恢复保留，门禁12 PASS；额外“恢复重置
完整时长”变异检出及恢复单列。AP product/control/focus对象通过，未完整链接。
主机静态状态88字节，无堆分配、新线程、每tick写盘；CPU/板端栈仍未测。
报告 s15-20260924.json、s15-evidence-20260924.json，日志 out/shaniu-s15/。

App页面、进度环/完成提示、NFC和TIMER-02跨重启策略仍未完成；未刷板。查询
到completed不等于提醒已呈现。新协议细节及层级缺口见contracts.md，历史56项
草案不改写。本片不宣称N2完整交付。

### S16 原生App专注计时会话接线（2026-09-24）

先新增5项JVM用例，再实现FocusTimerController，使用真实DeviceControlSession，
替身只在Transport结果边界。校验ACK后需回读、两段快照版本一致、断线与迟到
ACK不重发、旧固件拒绝、BEGIN未取得事务不能取消其他编辑器。未增加连接、
独立轮询线程或本地计时。定制页增加原生底部表单，保留Canva组件/颜色；输入
分钟数不随状态刷新重建。操作按钮仅在当前认证与有效回读下可用，显示上次
设备剩余时间，用户显式刷新；关闭表单不取消已运行计时。

117 PASS（原63 + 累计新增54），原两变异/恢复通过，运行器12 PASS。Debug APK
构建通过（仍44/0.7.14，必须按本轮hash区分，未安装）；hash及限制见
s16-evidence-20260924.json，逐例s16-20260924.json，日志out/shaniu-s16/。
新接口缺失、夹具未进入前台、Gradle参数及颜色引用错误均记录为接线/构建问题，
不称产品Red。测试不执行真实BLE，也未完成模拟器布局/真机导航验收。
设备进度环、完成提示、NFC以及待定跨重启策略仍未完成，N2不销项。

### S17 设备专注进度与完成视觉提示（2026-09-24）

新增生产32段进度环/眼睛、暂停标记、完成勾号。整数进度避免uint64乘法溢出，
只读focus_visual不改变计时。产品线程仅原子提交意图，已有显示worker分配画布
并写双屏；关机/认领优先，语音活动时抑制专注，取消后恢复既有帧或内置眼睛。
无SD写入、云调用、新线程。完成提示是视觉，不声称已播放声音。

像素用例覆盖0/半程/满环、暂停/完成形状和UINT64_MAX；真实显示提交helper
覆盖第二屏失败不确认、重试成功确认、重复状态不重绘、取消恢复。主机结构只
提供该helper必需字段，framebuffer是外部观察器；完整显示worker调度未执行。
119 PASS（原63+累计新增56），原两变异/恢复保留，门禁12 PASS；AP product/
display/focus对象通过，未全量链接。报告s17-20260924.json及s17-evidence-20260924.json。

out/shaniu-s17/focus.png从生产像素函数生成且已查看，不是实板截图。渲染临时
画布51200字节，复用线程且仅状态/进度变化时更新；目标耗时、峰值内存、实际
屏幕优先级和显示确认仍待验。不把119或累计新增56等同56项需求全部通过。

### S18 — focus draft navigation and Activity recreation (2026-09-24)

`UI-02.focus-draft` now exercises the actual MainActivity focus sheet on the
emulator. Synthetic authentication is only a UI admission fixture: no transport
exists, and start/pause/cancel must stay disabled without a device readback.
The original second opening lost a changed 47-minute input (reset to 25).
MainActivity now retains the non-secret minute draft across sheet navigation
and saved-instance restoration, without restoring or replaying a device command.
The obsolete notice is updated for S17's visual completion indication.

One emulator case passes 20 navigation rounds and one actual Activity recreation;
five existing FocusTimerController JVM cases pass. APK and instrumentation builds
pass. This is **not** physical BLE acceptance or the full UI-02 matrix. No phone
installation or board operation occurred. The prior 119-unit S17 report remains
historical and was not rerun for this UI-only edit.

Reproduce after building/installing both debug APKs **on an emulator only**:

```sh
adb -s emulator-5554 shell am instrument -w -e focus_draft_probe 1 \
  com.shaniu.companion.test/com.shaniu.companion.provision.ControlKeyInstrumentation
```

Require the returned `PASS:` report; ADB process exit alone is insufficient.
The fixture rejects non-emulator targets. Evidence, per-JVM-case times, APK/source
hashes and the original Red are in `acceptance/s18-evidence-20260924.json`.
An initial fixture cleanup assertion ran before Android delivered onDismiss;
its separate failure is preserved, and the same assertion now runs after UI idle.
Raw build/install logs remain under `out/shaniu-s18/`.

### S19 — linked firmware and fresh regression (2026-09-24)

At source `413a9c97786d2340887f85f1f9dafadcf6d6c759`, the maintained
`bk7258.py build` entry completed an incremental CP/AP/BL1/BL2 build using
the prior development public keys and unchanged rollback floor 661. The
build-manifest verifier rehashed the resulting artifacts successfully.
AP ELF contains `bkfocus_control`, `bkfocus_step`, `bkfocus_cancel`,
`bkfocus_visual` and `bk7258_display_focus`; this closes the earlier
object-only link evidence gap. The AP raw image is 1,684,232 bytes.

The frozen collection reran with **119 PASS, 0 assertion/setup failures and
0 NOT_RUN**; the original 63 IDs remain included. Two isolated mutations were
detected and restored. See `acceptance/s19-20260924.json` for individual cases
and `acceptance/s19-build-evidence-20260924.json` for artifact hashes and exact
scope. This does not convert 56 parent specifications into completed acceptance.

Raw logs/manifest are in `out/shaniu-s19/`. An initial direct CMake invocation
failed because it omitted the SDK environment supplied by the maintained entry;
a relative manifest path also resolved against the workspace, so verification
was repeated with an absolute path. Both setup mistakes are preserved separately.
Existing apps/nuttx dependency modifications were retained and identified by the
build provenance. No clean build, signed release package, current-board trust
verification, deployment or physical test occurred. Do not flash these raw
outputs as if they were an approved factory package.

### S20 — block close error consumes its inode (2026-09-24)

`MSC-01.backend-stop-close-error` compiles the real MSC initialize/stop helpers.
The external blockdriver fixture now models the current NuttX contract:
`fs/driver/fs_closeblockdriver.c` calls `inode_release()` even if a valid block
driver's close operation returns an error. The test checks error propagation,
no repeated close/deinitialization of that consumed reference, and a subsequent
explicit fresh open/close. **Production already passes; no product change.**
Retaining the inode on every error would introduce a dangling reference here,
unlike the earlier USB-deinitialize failure, which must retain live storage.

The expanded frozen set reports **120 PASS**, including all prior 119 IDs.
The runner's 12 gate tests also pass. The two existing mutations remain detected.
A separate temporary-include mutation retains the consumed pointer; it compiles,
reaches the repeated close and fails the reference-lifetime assertion. The original
case passes afterward. This extra mutation/restoration is reported separately,
not added to product pass counts. See `acceptance/s20-20260924.json` and
`acceptance/s20-inode-mutation-20260924.json`; raw logs are `out/shaniu-s20/`.

Boundary: the NuttX close contract is source-reviewed and hashed, not executed
inside a real kernel by this test. USB/blockdriver are external substitutes;
actual hardware, DMA, blockdriver durability after errors and filesystem mount
unification remain open. This is not full MSC-01 acceptance.

### S21 — display queries do not wait for renderer I/O (2026-09-24)

`NET-02.display-snapshot` compiles the production getter/publisher extracted
without behavior changes first. The initial getter waited for the render mutex
while the renderer was deliberately held, reproducing the query blockage.
The fixed getter copies the last completed service update through a separate
short spinlock. All display-service unlock paths publish the coherent status;
no mount/decode/framebuffer work occurs inside that snapshot lock. In-progress
pack/frame state is not exposed as a completed update. Public EYE1 layout and
render sequence semantics are unchanged.

The test observes the prior pack/sequence while rendering remains held, then the
new coherent status after publication. Its 1000ms harness watchdog detects the
blocked dependency, not a promised device latency threshold. The renderer does
not release its lock until the query observation finishes. External pthread/IRQ
shims replace NuttX primitives; the queried production function is not mocked.

**121 PASS** in the frozen host collection, with all prior IDs retained; two
original mutations still detected/restored and 12 runner-gate tests pass.
CP/AP/BL1/BL2 incremental build and manifest rehash pass. Target ELF reports
108 bytes for the snapshot plus a 4-byte lock, no added heap/thread. CPU, IRQ
hold duration, stack watermark and real SD/DMA contention remain unmeasured.
Explicit render/install operations still have their existing synchronous path;
this slice does not claim full DISP-01/NET-02 or asynchronous installation.

See `acceptance/s21-20260924.json`, `acceptance/s21-display-evidence-20260924.json`
and local `out/shaniu-s21/` for the Red, fresh Green, sources and artifact hashes.
New `.inc` source hash is explicitly included because it was untracked at build
time. No signed package, phone update, serial access or board operation occurred.

### S22 — Agent eye expressions use the display worker (2026-09-24)

`DISP-01.expression-intent` was written before the new interface existed;
its initial missing source is **BLOCKED_INTERFACE**, not FAIL_ASSERTION.
The production single-slot state machine is now linked into the existing display
worker. The Agent's `device_control(action=eyes)` submits an intent and returns
`state=accepted`, `request_id` and `rendered=false`; it no longer performs
mount/decode/render synchronously inside that tool call. `device_status` adds
`eye_request` with the latest ID, state and error. Acceptance is not completion.

Contract for this expression-specific interface:

- Only the existing nine allowed expressions are accepted; no persistent default
  is changed. Null/unknown inputs fail before admission.
- One pending/running request; another returns EBUSY without replacing it.
  Boot-scoped IDs monotonically increase and exhaustion returns EOVERFLOW.
- The existing worker marks RUNNING, releases the short intent lock, renders,
  then publishes DONE/FAILED. DONE means renderer/driver submission success,
  not independently observed photons. Missing framebuffer devices fail once.
- Power/claim overlays reject requests and cancel pending ones. They do not
  rewrite a completed result. New explicit synchronous expressions supersede
  an older pending intent; conditional vision restoration yields to a new intent.
- Only the latest result is retained; compare its ID. This is not a general
  install-job history. Request expiry and voice-turn cancellation binding remain
  open; do not claim full asynchronous lifecycle acceptance from this slice.

The host test compiles the actual intent code with external render/IRQ shims;
it checks no rendering on acceptance, RUNNING queryability during rendering,
rejection while occupied, one completion, error/absent-device paths, overlay
cancellation, supersession and ID exhaustion. Caller/worker wiring is target-linked,
not a full executed Agent/worker integration test. The legacy vision/RPC synchronous
APIs are retained and still need their own slow-path treatment.

**122 PASS**, original IDs retained; 12 runner-gate tests pass. Two original
mutations remain detected/restored; an additional isolated accepted-as-completed
mutation is detected and restored separately. Complete CP/AP incremental linking
and build-manifest verification pass. Named static intent storage is 39 bytes
before alignment; no new thread/heap allocation. Real CPU/IRQ/stack high-water and
end-to-end speech latency are unmeasured. No board/phone operation occurred.
Evidence: `acceptance/s22-20260924.json`, `acceptance/s22-expression-evidence-20260924.json`,
local raw logs `out/shaniu-s22/`. No historical report was replaced.

### S23 — cancel only the expression owned by the request (2026-09-24)

`DISP-01.expression-cancel` compiles the real intent state machine and the real
product request/cancel adapter. Only voice cancellation and rendering boundaries
are substituted. The new exact-ID API follows these semantics:

- Pending -> CANCELED; repeated cancellation of that canceled ID is idempotent.
- Running -> EBUSY; completed/failed -> EALREADY; an ID different from the latest
  -> ESTALE. No active rendering callback is aborted or declared safely stopped.
- Explicit authenticated BKCONTROL_CANCEL snapshots the ID before calling voice
  cancellation; it cannot cancel a newer intent created during that call.
- The eyes tool checks its original cancellation callback before and after
  enqueue. A crossing cancellation withdraws only its own still-pending ID.
  Callback/context are never retained by the asynchronous worker.
- When voice is already idle but a queued expression is canceled, the control
  operation succeeds. Voice cancellation success still means accepted, not
  proof that a running render exited. Its actual expression result stays visible.

The initial missing adapter/API was BLOCKED_INTERFACE, not a prior product Red.
A first harness build rejected an unused included helper under Werror; an actual
pending-state assertion was added, keeping the compiler gate intact.

**123 PASS**, all previous IDs retained; 12 runner-gate tests and full incremental
CP/AP build plus manifest verification pass. The two original mutants still
fail and restore. Two additional temporary-include mutants (cancel latest after
blocking, omit post-enqueue guard) also compile, reach their assertions and fail;
restoration passes. Additional mutations are counted separately from product cases.

No untagged TURN_COMPLETE event is used to cancel the latest intent: that event
has no request ID. Request expiry, other internal cancellation paths, complete
transport/Agent integration and real framebuffer/DMA behavior remain open.
No board/phone operation occurred. Evidence: `acceptance/s23-20260924.json`,
`acceptance/s23-cancel-evidence-20260924.json`, local raw logs `out/shaniu-s23/`.

### S24 — model readback cannot replace a failed durability barrier (2026-09-24)

While preparing the future scene-binding store, source review found an existing
MCP1 preference bug: after SCF1 commit returned EINPROGRESS, matching bytes on an
immediate read changed the setter result to success. The new
`CFG-02.models-unknown` test links actual preferences, model codec, SCF1 store,
mbedTLS hashing and POSIX files. It redirects only the fixed model directory to
its own temporary directory and injects the external directory fsync failure.
The original code returned **0 instead of -EINPROGRESS**; that Red is preserved.

The setter now retains uncertainty. Public reads clear their output and report
EINPROGRESS; further writes cannot overwrite it in that process. The lower store
can still expose valid new bytes, explicitly not treated as durable confirmation.
A fresh exec loads the valid record while the original process remains uncertain.
This is process recovery evidence, not physical power-loss proof.

After the existing reset worker durably removes user-record trees, the product
calls a new completion hook. It checks internal filesystem availability and that
the model directory is absent before clearing the uncertainty latch. Tests reject
both a still-present tree and an unavailable filesystem, then permit fresh use
after synthetic cleanup. Actual factory-reset sequencing is linked but not
executed by this host test; no real device files were removed.

**124 PASS**, all prior IDs retained, plus 12 runner-gate checks; both original
mutations remain detected/restored. CP/AP incremental build and manifest rehash
pass. One boolean is added; 409 bytes of confirmation buffers are removed from
the setter. No new thread or allocation. Target runtime/stack measurements,
LittleFS failure behavior, App unknown-state presentation and NFC remain open.
Evidence: `acceptance/s24-20260924.json`, `acceptance/s24-models-evidence-20260924.json`,
local logs `out/shaniu-s24/`. Historical results remain unchanged.

### S25 — configuration save admission (2026-09-25)

The native editor previously enabled save while the public SCS1 state was pending
or uncertain. The emulator regression observed that failure before the production
change. Saving now also checks unresolved public state and the existing local
receipt; reconciliation remains available. No protocol/authentication changes.

`settings_unknown_probe=1` on the existing `ControlKeyInstrumentation` exercises
real editor Views with explicitly synthetic snapshots. It is emulator-only,
uses a unique receipt namespace and cleans that namespace. This is UI evidence,
not authenticated transport or physical persistence acceptance. It covers all
five public states, a pending local receipt, read availability and disabled
admission. The original contract runner reports 124 PASS; this additional UI
case is counted separately. See `acceptance/s25-20260925.json` and
`acceptance/s25-app-evidence-20260925.json` for exact hashes and Red/Green output.
APK builds and Android unit tests passed; no phone install or board write occurred.

### S26 — local motion failure output (2026-09-25)

`make -C tests/host/bk7258 run-motion-rpc run-motion-core` now also exercises
`bk7258_motion_service_sample` with a previously successful response while its
owner is unavailable. The response must carry ENODEV and clear sample validity,
time and axes, without touching the driver. Restored ownership can sample again.
The existing harness compiles production service/core/client code, replacing
only NuttX/platform and sensor I/O. Its first new fixture invocation lacked the
worker-context marker and was corrected as SETUP_ERROR, before recording the
actual old-production assertion failure. Both logs are retained in the evidence.

The strict contract run remains 124 PASS. The additional RPC target reports 20
scenarios (one newly added); these are not folded into the original 63 IDs or
56 product designs. Official incremental build, layers and manifest verification
passed. No autonomous gesture recognition or physical sensor acceptance is claimed.
See `acceptance/s26-20260925.json` and `acceptance/s26-motion-evidence-20260925.json`.

### S27 — expression operation identity (2026-09-25)

`run-expression-ownership` reproduces the old same-name replacement bug using
production vision feedback, exact display command bodies extracted at build,
and the production async intent module. Only pixel rendering and OS locks are
substituted. A newer explicit `happy` during the old feedback hold was replaced
by `neutral`; the new identity check retains it and also prevents an old capture
result from overwriting a newer selection. Normal restoration still passes.

The new wrapper assigns non-reused identities to render attempts under the
existing mutex. Conditional updates atomically check/update that identity;
queued requests block old replacements. Failed renders may retry while still
owned. The vision service uses this API. Existing vision fixtures were updated
for the conditional result transition (one additional replace call) and token
comparison; expected user behavior and 800 ms hold were not loosened. The
byte-identical original Red test is archived inside the evidence JSON.

Strict collection now has 125 PASS, retaining all original 63 IDs. A separately
counted isolated mutation removing identity mismatch rejection fails, and the
unchanged production rerun passes. Target incremental build/layers/manifest and
runner checks pass. No new allocation/thread/wait is introduced. This does not
complete trial TTL, general scene arbitration, physical pixel/DMA or latency
acceptance. See `acceptance/s27-20260925.json` and
`acceptance/s27-display-evidence-20260925.json`.

### S28 — parameterized display trial service (2026-09-27)

`run-expression-trial` compiles production intent/trial state and render identity
with an external virtual monotonic clock and pixel sink. Its initial missing API
was BLOCKED_INTERFACE, not an assertion Red. The service accepts a caller-supplied
TTL, includes queue time, skips an already expired queued trial, and restores the
previous logical expression only while still owning it. Active cancel is pending
until the existing display worker restores; failed restore is terminal failure.
New explicit identity, pending expression or power/claim supersedes restoration.

Tests cover normal expiry, duplicate/stale cancel, rendering/restoring cancel
rejection, queue expiry, new same-name selection, regular pending request,
resource failure, clock reversal/unavailability and deadline overflow. Removing
expiry or new-identity checks in isolated builds is detected; original rerun
passes. Strict count is 126 PASS, with all original 63 IDs retained.

Incremental build, layers, manifest and runner checks pass. Named static trial
variables total 62 bytes (64-byte layout span), no new thread/heap/DMA buffer.
Deadlines are evaluated by the existing 100 ms worker poll after renderer I/O;
this is not a hard rendering deadline or I/O cancellation. CPU/p95 and stack high
water remain unmeasured. Protocol and App adapters do not exist yet, so linker GC
omits unused public admission/status/cancel entries. This is service preparation,
not a user-accessible trial or N2 acceptance. Default App TTL remains undecided.
See `acceptance/s28-20260927.json` and `acceptance/s28-trial-evidence-20260927.json`.

### S29 — authenticated expression trial protocol (2026-09-27)

`run-trial-wire` executes production SDC1 authentication/staging/sequence logic,
the ETC1/ETS1 controller, and actual display trial/identity state. Its literal
request/response vectors precede the controller implementation; the initially
missing source is BLOCKED_INTERFACE. Only clock/pixel/OS boundaries are replaced.
The host config callback is thin kind routing; the actual product route and
public service entry symbols are separately verified in the linked AP firmware.

It covers unauthorized/wrong-key/old-sequence rejection, fragmented submission,
readback, receipt collision, stale ID, close/reopen retry without deadline reset,
terminal retry without restart, cancel acceptance versus restored completion,
invalid records, local caller receipt isolation and read-only quiesce admission.
Expanded reconnect first omitted closing the old Session; that rejected fixture
is retained as `s29-fixture-error-20260927.json`. The fixture now uses real close
before reconnect. This correction does not loosen authentication or sequence rules.

Current strict collection: 127 PASS including the original 63 IDs. Two isolated
mutations (dedupe removal and atomic-ID guard removal) are detected and the
unchanged production rerun passes. Incremental build/layers/manifest/runner checks
pass. The new controller has 37 named static bytes, no new thread/heap/persistence.
App UI/controller and physical BLE/render checks remain incomplete. See
`acceptance/s29-20260927.json` and `acceptance/s29-wire-evidence-20260927.json`.

### S30 — native App expression trial (2026-09-27)

Eight JVM cases exercise the real foreground Session/controller with an external
transport peer and literal ETC1 golden bytes. They cover accepted versus rendered,
cancel-pending versus confirmed, inconsistent split snapshots, generation changes,
unsupported firmware, another editor's staging ownership, close without remote
cancel, invalid duration and mismatched receipts. Missing initial controller/UI
interfaces are BLOCKED_INTERFACE. The unsupported-message assertion then exposed
missing NuttX ENOTSUP/ENOSYS handling; its genuine Red and fixed run are retained.

The native sheet requires an explicit duration, preserves only public drafts, and
does not replay a device operation on recreation. No chosen default TTL or local
countdown substitutes for device status. Emulator-only real View instrumentation
passes 20 navigation rounds and Activity recreation; synthetic admission cannot
prove BLE or rendering. Device actions remain disabled without readback.

Strict collection is 135 PASS (original 63 plus 72 added); original two mutations
are detected and their restores remain in the 63. One UI test and 12 runner-gate
tests are reported separately. APKs build; no physical install/flash occurred.
RES-02 interface now has App/wire/device bindings and host evidence, but physical
render/restoration, persistent-default interaction and broader UI accessibility
remain unverified. N1/NFC/N3 are not completed by this slice. See
`acceptance/s30-20260927.json` and `acceptance/s30-app-evidence-20260927.json`.

### S31 — NFC completion is not any nonnegative result (2026-09-27)

Before wiring NFC scenes, three production-core tests reproduce false presence
on zero-byte or oversized scan completion and a positive HCE transaction return.
Scan now requires exactly the requested one byte; zero is ENODATA, excess is
EPROTO. HCE success remains zero, positive status is EPROTO. EAGAIN still means
no card. Close errors win, stale presence is cleared, and the UID-free v1 wire
contract is unchanged. This is not card identity or authorization.

The old `test_result(0, 1)` assertion was incorrect: the selected driver's read
returns zero when selection has not yielded a complete sample. It is replaced
with a stricter negative assertion, not deleted to hide a regression. Evidence
archives the original assertion/source and three pre-fix assertion failures.
Two runner attempts reported SETUP_ERROR because the new executable marker was
not CONTRACT_PASS (the marker parameter is boolean, not custom text). Both raw
reports are preserved; the test now emits the existing required marker. No gate
was relaxed. Final strict collection: 138 PASS, original 63 retained; original
two mutations detected. A separate compiled mutation restoring broad nonnegative
acceptance is detected by all three cases. Existing RPC integration: 19 cases
pass. Incremental target build and manifest pass.

No new thread, buffer, timer or I/O is introduced. No physical NFC/RF validation
was performed; binding, persistence, dwell dedupe, scene dispatch and target card
compatibility remain incomplete. The underlying driver selection path still
needs separate review; these tests prove only the core's response to its input.
Full-file nxstyle reports existing header/section/style issues; it is not marked
PASS. See `acceptance/s31-20260927.json` and `acceptance/s31-nfc-evidence-20260927.json`.

### S32 — NFC selected-card validation at the real service boundary

Current target uses standard CL_MFRC522. Its legacy read path ignores the
selection return and may inspect an incomplete UID. The existing standard
GET_PICC_UID ioctl propagates selection errors. The service now uses it, checks
4/7/10-byte UID lengths and the incomplete-selection bit, returns only a presence
byte, and retains its single worker/close/error semantics. No UID is logged or
added to the version-1 RPC. This does not make UID an authorization credential.

Eight separately collected L2 cases compile actual client/service/core and replace
only OS/RPMsg/VFS boundaries: empty UID, selection timeout/error, invalid length,
incomplete bit and valid 4/7/10-byte identities. The public contactless ABI header
is read from the pinned NuttX checkout; the host shim isolates its filesystem
ioctl-number macro from Linux headers. An initial direct fs-header include failed
to compile (SETUP_ERROR), then the empty-UID case failed its product assertion
before the service change. Legacy RPC I/O-count assertions now count the selected
ioctl instead of read; replay/close counts and outcome assertions are unchanged.

146 strict PASS include the original 63; original mutations remain detected.
Full NFC RPC 27 and motion RPC 20 pass separately (8 NFC cases overlap strict
collection; do not add them again). Removing UID validation in a temporary
compiled mutation is detected by empty/invalid/incomplete cases. Incremental
firmware/layer gate and manifest pass. Whole-file nxstyle still reports existing
structural/style issues and is not a pass. No new thread, static state or heap;
a local 12-byte UID structure is added, target stack/latency are not measured.
Current target disables CL_ISODEP/CL_MFRC522_FRAME; HCE code existence is not
capability evidence. Scene-card binding, retention, dwell dedupe and common scene
dispatch are still missing; RF/physical compatibility remains NOT_RUN. See
`acceptance/s32-20260927.json` and `acceptance/s32-selection-evidence-20260927.json`.

### S33 — one focus owner behind typed and wire entry points

The existing authenticated FOC1 adapter now decodes into `bkfocus_execute`;
FOS1 reads the same owner through `bkfocus_snapshot`. No second timer, thread,
queue, authorization bypass or wire change is introduced. Typed callers must
first pass product admission and run on its serialized owner; NFC/voice workers
must post intents rather than call the service concurrently.

Four new real-production sequences alternate typed and wire start/pause/resume/
cancel, verify exact cross-entry retry despite struct padding, stale revision
rejection, lifecycle cancel, invalid fields and deadline completion once. The
initial missing functions are BLOCKED_INTERFACE, not a business Red. A compiled
isolated mutation disconnecting wire application from the common owner is
detected by all three mixed-entry sequences. Existing tests retain their oracle.

Strict 150 PASS retain original 63 IDs and both original mutation detections.
Incremental firmware/layers/manifest pass. AP ELF retains execute/snapshot/control
and the single g_focus state (88 bytes). Target stack/CPU not measured; no new
heap, thread, persistence or I/O. Whole-file nxstyle is NOT_PASS (format/section
conventions); no full style pass claimed. This is a production refactor serving
the existing App protocol, not a working card or voice binding. Those adapters,
authorization/queue admission, persistence policy and physical evidence remain
required. See `acceptance/s33-20260927.json` and
`acceptance/s33-focus-evidence-20260927.json`.

### S34 — voice focus tool posts to the product owner

`focus_timer` is registered in the actual product tool provider, parses the
existing cJSON input, and posts one bounded intent. Start/pause/resume/cancel
share the S33 timer. Product loop executes after reset/power gates; unknown
reset and power preparation close admission and cancel pending requests. OTA
busy blocks new intent application. No worker directly owns timer state.

The tool reports accepted plus request ID, not running/completed. Status copies
a cached observation with its monotonic timestamp and never advances the timer.
A changed revision fails when applied. Cancel checks before/after enqueue remove
pending work by exact ID; if application already won, cancellation reports
too_late instead of pretending to undo it. Timer execution then remains local,
independent of cloud connectivity; voice recognition itself is not claimed offline.

Five tests compile real tool parser/mailbox/focus with cJSON and the external
OS-lock shim. They cover start/pause/resume/completion, passive queries, queue
full, power gate, stale revision, pending/too-late cancellation, invalid input and
output reservation. Initial missing module is BLOCKED_INTERFACE. Removing
admission or pending cancellation in separate compiled mutants is detected.
Actual tool schema literal parses and its actions/limits match (static supplement,
not an Agent round). Existing authenticated App FOC1 path is unchanged.

Strict 155 PASS retain original 63 and original mutation detections. Target
build/layers/manifest pass; AP includes all mailbox/tool symbols. Named static
state: lock 4, request 32, status 56 = 92 bytes; no added thread, heap allocation
in mailbox, persistence or hardware I/O. Existing product cJSON parsing allocates
as before. Target CPU/stack/voice latency remain unmeasured. Whole-file nxstyle
is not PASS; style/section diagnostics are preserved. Timer completion is not
proof of visible/sounded reminder. No physical install/flash; actual Agent/ASR/
audio integration and NFC binding remain unverified or unimplemented. See
`acceptance/s34-20260927.json` and `acceptance/s34-voice-evidence-20260927.json`.

### S35 — close S34 production-module style findings

Only focus_intent source/header formatting is changed. The pinned nxstyle
source is built with TOPDIR set to this canonical team repository, so its
relative-path rule checks the correct root instead of treating all non-NuttX
files as apps/. No rules are removed or diagnostics suppressed. Both files pass
full-file nxstyle. Compiler invocation, checker source hash/commit and raw
results are recorded; this does not claim other legacy files are style-clean.

Target log confirms recompilation. The complete ARM relocatable object is
byte-identical before/after (also identical after stripping debug information),
including code, constants, symbols and relocations. Strict 155 PASS retain all
previous IDs and original mutation checks. Build/layers/manifest pass. No new
test, threshold, behavior, resource allocation or device operation is added.
See `acceptance/s35-20260927.json` and `acceptance/s35-style-evidence-20260927.json`.

### S36 controlled NFC reader (2026-09-27)

The maintained `CL_MFRC522_RF` driver derives from pinned NuttX
`76354c637858ecb0aa4601629327acb6f44a26bb`, retaining its license and standard
card protocol. It adds a 0/1 antenna ioctl, register readback and idle-off
registration. This is not the previously described but absent raw-frame patch
series. Frame exchange and ISO-DEP remain disabled. Board selection and the
NFC service's Kconfig dependency must both accept this driver.

`test_mfrc522_rf.py` executes actual driver antenna/ioctl/register functions
with only external register/allocation/registration boundaries replaced. Four
cases cover transitions/invalid values, stuck registers, idle-off publication
and failed release. Two lifecycle cases execute actual service functions;
partial RF-on failure must attempt RF-off before descriptor release while
retaining the original error. The new case first failed at that side-effect
assertion. A missing new API was BLOCKED_INTERFACE, not a business Red.

The function extractor was corrected to skip forward declarations after the
production cleanup introduced one. The first complete run's two compile
errors are retained as SETUP_ERROR; no behavior assertion was relaxed. The
strict set adds six IDs, preserving the original 63 including two restores.
Two extra isolated mutations (ignore register mismatch; omit failed-open
cleanup) must fail actual runtime assertions, and their restores pass. These
are counted separately from the strict set and its original two mutations.

Build success alone initially missed a disabled service: the new driver was
linked but an old Kconfig dependency rejected it. The explicit configuration
assertion failed. After fixing that dependency, cached incremental config was
still stale; the supported `build --clean` is used to regenerate it.
`check_nfc_rf_build.py --config <AP .config> --elf <AP ELF> --map <AP map>`
requires both actual production entries, exactly one driver and no claimed
ISO-DEP/frame support. This is a target-artifact check, not device acceptance.

No new worker, queue, persistent write, sampling loop or DMA allocation is
introduced. Existing worker stack and driver object are retained; each RF
transition adds register readback, with no new retry or wait budget. CPU p95,
UART latency, stack high-water, current/RF measurements and physical card
compatibility remain NOT_RUN. A failed RF-off reports an error, not physical
safety. Authorized card bindings, dwell/reentry and scene dispatch remain
unimplemented; L3 is BLOCKED_DEVICE, not evidence that those software gaps
are complete. No firmware was flashed or phone app installed.

### S37 NFC selection evidence before card bindings (2026-09-27)

`test_mfrc522_selection.py` adds six L1 units around real REQA/request/ioctl
functions: probe error, timeout, malformed ATQA, partial selection error,
invalid argument/result, and valid 4/7/10-byte selection (including collision).
The first run compiled and executed all six: four assertion aborts, one null
argument signal fault in the controlled peer, and one PASS. This is a host
ioctl-admission regression, not evidence of the historical board HardFault.

Production now checks the argument before I/O, clears failed output, preserves
probe/selection errors, permits collision to proceed to anticollision, rejects
incomplete/invalid results and copies only successful selection. Bad ATQA is
EPROTO rather than EAGAIN; neither malformed frames nor timeouts prove physical
removal. The standard ioctl number and RPMsg v1 presence-only schema are intact.
The old boolean detection helper remains for the unused legacy string-read
entry; product uses GET_PICC_UID. These tests do not validate that legacy entry.

The deterministic RF exchange and anticollision result are controlled peers;
the transaction admission and publication code under test is production code.
Thus these are L1 boundary tests, not full anticollision, transport or RF L2/L3.
An extra pair of isolated mutations (bypass probe gate; publish partial UID)
fail assertions; restores pass. S36 RF tests update only the unused peer
signature to match the real call boundary; their behavior assertions remain.

Strict result: 167 PASS, original 63 retained, six new IDs. Original two
mutations and their two restores remain separately visible in that result;
S37's two mutations/two restores are additional sensitivity evidence. Actual
target build, configuration/ELF coexistence and manifest rehash pass. No new
thread, heap allocation, persistent write or retry is added; temporary UID
and ATQA occupy 14 source-level bytes plus compiler alignment/frame overhead.
Actual stack high-water, CPU p95, transport time and physical card removal
are NOT_RUN. Bindings, dwell/reentry and scene dispatch are still unimplemented.
See `acceptance/s37-20260927.json` and `s37-selection-evidence-20260927.json`.

### S38 explicit internal card sample protocol (2026-09-27)

`acceptance/NFC_CARD_WIRE_V2.md` freezes the new device-internal CARD operation.
Existing V1 scan/HCE and CLI remain presence-only. V2 command 3 uses the same
24/40-byte transport envelopes and existing worker; its 12-byte card sample
never becomes authentication or a BLE/App/USB/log field. No automatic polling,
owner binding or scene dispatch is enabled. A future matching caller still
needs authenticated configuration, persistence, deduplication and admission.

Seven new cases in `test_bk7258_nfc_rpc.c` execute actual client, server queue,
core and worker with external NuttX/RPMsg/ioctl boundaries controlled: valid
UID lengths/tail, read errors, close failure, version mismatch, duplicate
request, malformed envelope/payload and invalid selected card. V2's EAGAIN
and timeout remain errors; no current scan result proves physical card removal.
Failed/close-failed replies have no card payload. Version now joins session,
sequence and connection epoch correlation. Exact duplicate response bytes
are cached without a second hardware read.

The initial new-card test reaches the old dispatcher and fails because V2
is absent: BLOCKED_INTERFACE, not a known-business Red. During implementation
`card-replay` incorrectly expected zero from a direct replay callback. That
callback forwards rpmsg_trysend's nonnegative byte count (also used in the
existing replay tests); the controlled peer returns exactly 40. Its assertion
was corrected to that exact count. No payload, no-repeat-I/O or error assertion
was removed. The original failure log is preserved.

Full NFC RPC: 34 cases; motion RPC: 20 unchanged; NFC core: PASS. Strict set:
174 PASS including original 63 and seven new IDs. Two additional isolated
mutations (omit reply version correlation; publish UID before failed close)
compile, hit their assertions and are detected; restores pass. Original two
mutations/restores remain visible separately. Actual AP/CP builds and manifest
rehash pass; ELF contains the real client, service and card validator.

No new thread, heap allocation, persistent write, polling interval, DMA buffer
or I/O retry is introduced. Request/response/cache sizes remain 24/40 bytes.
Card scratch is 12 bytes plus compiler frame overhead. Actual target symbols:
server object 240 bytes, client 216 bytes (whole objects, not incremental cost).
ISR duration, CPU p95, stack high-water, real transport and RF/card compatibility
remain NOT_RUN. Protocol host integration is not complete NFC-scene or L3 proof.
See `acceptance/s38-20260927.json` and `s38-card-wire-evidence-20260927.json`.

### S39 NFC binding persistence component (2026-09-27)

`NFC_BINDING_STORE_V1.md` defines the bounded table and unique filesystem-owner
contract. The production component reuses actual `bkprov_store` transactions,
checks persisted transaction/schema, supports revision/operation replay, and
blocks lookup/write after an unknown commit. UID does not grant authority.
The actual architecture places both NFC and focus on AP; S38's CP extension
is not a necessary scene path. The future adapter must use the existing AP
worker and power/reset drain, not a second sampler or an arbitrary path.

Six new storage integration cases: persistent save/reopen/remove, revision and
idempotency, invalid/golden records, prepublication failure, unknown directory
sync and fresh-process reopen, and corrupted transaction metadata. Only fsync
is fault-injected; real storage, checksum, parse and rename are executed. Initial
missing API was BLOCKED_INTERFACE. The initial golden duration byte was corrected
from index29 to27 before running the implementation: header8 + duration offset12
+ BE64 last byte7. Index29 is reserved; layout was not loosened. The corrupted
transaction case later produced an actual assertion Red, then passed after
loader validation. Two isolated mutants (drop uncertainty; publish failed cache)
compile and fail assertions, with restored cases passing.

Strict 180 PASS retain original63. Target compilation of the new ARM object and
full build/manifest pass. The object is not yet called from the product path;
linker retention or board functionality is NOT claimed. Authentication routing,
worker integration, exit/reset handshake, UI, enrollment, dwell/reentry and
focus dispatch remain software gaps. No new runtime thread, timer, filesystem
root or device operation is enabled by this commit.

Storage context contains eight 24-byte entries, existing store paths, revision,
transaction and flags; encoding adds 200-byte bounded buffers. Own code has no
heap, DMA, ISR, polling or network activity. Backend I/O latency, complete stack
high-water and hardware resource budgets remain unmeasured. Writes occur only
on explicit set/remove; duplicate retry and cached lookup do not write. See
`acceptance/s39-20260927.json` and `s39-bindings-evidence-20260927.json`.

### S40 NFC exit acknowledgement (2026-09-27)

K2 power coordination now closes NFC admission and waits for the existing worker's
actual I/O/RF-close result before storage/CP power transition. Busy is incomplete;
release failure remains failure. A new power intent can request cleanup retry in
the same worker. Canceled active requests retain a replay tombstone so explicit
resume cannot resample that old request. No I/O occurs in the short stop caller.

Six actual NFC worker cases cover queued/active/prestart stop, close/RF failure,
and late replay. Two power coordinator cases prove busy/failure cannot reach CP.
Strict188 PASS retain original63; original mutants/restores remain separately
reported. Two additional isolated mutants compile and fail assertions; restored
cases pass. Existing NFC34/motion20, runner gate12, target build and manifest pass.

Initial wrong include path was SETUP_ERROR; missing APIs were BLOCKED_INTERFACE.
Power-before-NFC-ack and canceled-request resampling produced real assertion Red.
The active fixture reenters worker initialization, causing an extra initialization
close: the corrected observer counts from the active read-hook boundary, still
requiring one sample release. The lifecycle fixture uses a designated initializer
for the added release_error field. Original failure logs are retained.

The target server object is248 bytes (+8). No new worker, timer, heap or DMA;
existing shutdown deadline remains unchanged. CPU/p95, stack and hardware timing
are unmeasured. Software RF-off acknowledgement is not physical RF measurement.
Reset coordination, binding jobs/authenticated enrollment, dwell/reentry, scene
dispatch and UI remain software gaps. L3 is NOT_RUN and requires actual artifact/
device preflight and user action; this does not assert hardware is absent.
See `acceptance/s40-20260927.json` and `s40-nfc-exit-evidence-20260927.json`.

### S41 reset coordinator NFC barrier (2026-09-27)

The actual product reset step closes NFC admission even when the owner fails,
and waits for NFC acknowledgement before submitting cleanup. Failed admission
restoration keeps FINISHING closed; completion cannot resume NFC over an existing
power intent. The reset gesture and persistent receipt contract are unchanged.

Five L1 cases compile the verbatim production function with external participant
peers: busy, failure, independent owner failure, resume failure and power/absent
reset. All produced runtime C assertion failures before implementation and pass
afterward. unittest wraps SIGABRT as ERROR; these are business assertion failures,
not compilation/setup failures. Two isolated compiled mutants ignore stop failure
or power intent; both are detected and both restorations pass. Strict193 PASS
retain original63 and separate existing mutant/restoration counts. Target build
and manifest verification pass. No new static state, thread, heap or DMA; no long
I/O in the coordinator. Stack/CPU/physical timing remain unmeasured.

This is L1 coordinator coverage, not full storage/voice/NFC integration. Permanent
release failure stays closed; automatic reset-specific cleanup retry is not
claimed. Future binding-root cleanup, authenticated jobs/enrollment, dwell/scene
dispatch and App UI remain software gaps. L3 NOT_RUN: no physical reset, install
or flash occurred. See `acceptance/s41-20260927.json` and
`s41-reset-nfc-evidence-20260927.json`.

### S42 binding namespace and authorized reset (2026-09-27)

The actual AP RPMsgFS whitelist now accepts exactly `/cpdata/shaniu/nfc-cards`,
with the existing mounted-geometry check. Product reset cleanup rechecks NFC exit
then clears only config.pending/config.bin in this fixed namespace. It does not
recursively delete, format, mount or create directories. Missing namespace is
empty only when the parent exists on an accepted filesystem. Directory-sync
uncertainty remains EINPROGRESS; errors propagate through existing reset handling.

Two production-function tests cover filesystem admission and cleanup dispatch;
both failed assertions before fixes. Initial cleanup fixture omitted preferences,
causing unused-ret SETUP_ERROR; corrected to actual configuration before Red.
Four real-store cases cover durable repeatable reset, sync failure, unexpected
directory preservation and absent-root/missing-parent/symlink behavior. Unknown
files are preserved. The missing reset API was BLOCKED_INTERFACE, not a Red.
Strict199 PASS retain original63; two extra compiled mutants (skip delete/sync)
are detected and restored. Target build/manifest pass; final AP ELF retains
bknfc_bindings_reset and product_reset_cleanup. No physical reset was executed.

No new thread/static state/heap/DMA. Reset uses544 bytes of store path arrays; the
absent-root branch adds160 parent bytes plus stat/compiler frame. Actual stack,
CPU and RPMsgFS durability remain unmeasured. L1 peers and POSIX L2 are not L3.
Async registration/jobs, cache invalidation before enabling those jobs, auth/UI,
dwell/reentry and scene execution remain software gaps. See
`acceptance/s42-20260927.json` and `s42-binding-reset-evidence-20260927.json`.

### S43 single-worker binding jobs (2026-09-27)

The existing NFC worker now owns explicit LOAD/ENROLL/REMOVE jobs and the actual
binding store. Queries copy a UID-free cached status; they neither scan nor open
storage. A pending job excludes new RPC work, and existing RPC work excludes job
admission. Cancel before commit is acknowledged only after active I/O releases;
a commit cannot be canceled or reported canceled. Quiescence waits through actual
commit. An uncertain commit remains UNKNOWN and blocks store reuse. Authorized
reset reserves quiescent ownership, clears files and invalidates cached bindings.

Ten L2 tests use the actual server/core/worker and POSIX binding store, replacing
only RF, scheduler and fsync boundaries: persistence/replay, running cancellation,
stop, pending cancellation, RF read error, reset with sync failure, committing
stop/too-late cancel, unknown commit, removal and RPC conflict. Invalid requests
and stale IDs have no new job. Original missing APIs were BLOCKED_INTERFACE.
One wrong version constant was compile SETUP_ERROR, corrected to the existing
BKNFC_CARD_VERSION. The failure fixture initially set ioctl_error (sensor interval)
instead of read_error (UID ioctl): correcting the injection did not relax the
expected FAILED/no-new-binding assertion. Original logs remain retained.

Strict209 PASS retain original63. Two isolated compiled mutants bypass cancel or
retain reset cache; both fail assertions and restorations pass. Build/manifest
pass; target server is1144 bytes (+896), with the reset entry retained. Worker
job handling is compiled in its existing loop; external submission is not yet
wired through product authentication, and linker retention of unused public
submit APIs is not claimed. No new worker/timer/heap/DMA is introduced by job
code; existing store allocation and I/O remain. Actual CPU, stack and critical
section duration are unmeasured. There is no automatic scan or enrollment.

The previous reset-cleanup fixture now observes the service reset entry rather
than the file-only helper: the external requirement is unchanged, while the real
worker test verifies cache invalidation. Software gaps: authenticated control and
monotonic operation-ID adapter, native UI, disconnect policy, ambient dwell/reentry,
focus dispatch and full reset/storage concurrency. L3 NOT_RUN, no physical action.
See `NFC_BINDING_JOBS_V1.md`, `acceptance/s43-20260927.json` and
`s43-binding-jobs-evidence-20260927.json`.

### S44 authenticated binding control (2026-09-27)

Product config dispatch now routes kind12 to NCF1/NCS1, using the existing SDC1
AUTH/sequence/framing/staging implementation. APPLY only accepts a copied worker
job. READ is cache-only, including during quiescence and product OTA busy state.
A targeted NCF1 action cancels a worker job; CONFIG_CANCEL still discards only
staging. Accepted jobs survive transport close and are queried after reconnect.
Explicit LOAD publishes a floor covering the last durable operation ID; clients
must allocate above it, never wrap or resubmit blindly after a conflict.

Eight L2 cases execute actual session parser, protocol adapter, NFC worker and
POSIX store: auth, invalid records/offsets with independent golden read chunks,
explicit cancel, disconnect/result query, quiesced read with writes blocked,
durable operation floor, stale sequence and abandoned staging. The product-loop
config callback itself is source/target-build verified; the host fixture directly
routes the real parser callback to the real adapter. Physical BLE/TLS pairing is
not proven. Missing interface was BLOCKED_INTERFACE. Quiesced read produced a
real assertion Red before adding only this query to the allowlist. Original SDC1
validation remains unchanged and its existing suites remain in the strict set.

Strict217 PASS preserve original63. Target build/manifest pass; final AP retains
bknfc_control plus job submit/cancel/status (now called from product config).
Server object is1152 bytes (+8 floor). READ uses112-byte encoded snapshot plus
job status/compiler frame; no new thread, timer, heap, DMA or periodic scan.
Actual CPU/p95/stack high-water/lock and board I/O remain unmeasured. Shared OTA
staging constraints are unchanged; general multi-resource OTA arbitration is not
claimed solved here. Native UI, multi-page read consistency handling, ambient
card dwell/reentry and focus dispatch remain software work. L3 NOT_RUN.
See `acceptance/NFC_CONTROL_V1.md`, `acceptance/s44-20260927.json` and
`s44-nfc-control-evidence-20260927.json`.

### S45 native NFC controller (2026-09-27)

NfcBindingController reuses the existing foreground DeviceControlSession. It
collects112 bytes then rechecks all48 header bytes (phase/error, operation/revision,
floor/reserved), rejects unsupported signed counter ranges, and derives new IDs
from the device floor. Cancellation targets the existing operation. It distinguishes
pending/committing/failed/unknown from completion and refuses writes while pending
or unknown. Reconnect invalidates state without replay; close releases owned
staging only and never claims to cancel an accepted device job.

Nine JVM tests execute the actual Session and controller with a transport peer.
Two real Reds exposed40-byte APPEND against the device32-byte maximum and a newer
job's result being mistaken for this operation. The final sender waits for32+8
fragment ACKs before APPLY and correlates the subsequent readback. Initial peer
accepted arbitrary payload sizes; explicit32/8 assertions close that fixture gap
without weakening the wire contract. Missing controller was BLOCKED_INTERFACE.
Other cases cover mixed snapshot metadata, late ACK after disconnect, unsupported
firmware, unknown outcomes/counter range, targeted cancel and another writer's
transaction. Strict226 PASS retain original63. Debug APK builds; no release or
installation claim, and existing version/signing configuration is unchanged.

This controller is not yet called by MainActivity. Native sheet wiring, stateful
layout/navigation checks, real BLE, physical enrollment and scene dispatch remain
unfinished. No new connection, worker, background service or automatic polling.
Client buffers are bounded112/40 bytes plus reply copies; actual Android allocation
and device p95 remain unmeasured. See `acceptance/s45-20260927.json` and
`s45-native-nfc-evidence-20260927.json`.

Test-effectiveness correction: the first header mutant survived because the old
mixed-header test asserted null before consuming the final verification chunk.
The harness initially mislabeled this SETUP_ERROR; corrected classification is
SURVIVED, with the raw record preserved. Completing the input sequence makes the
same compiled mutant fail. The corrected test and restored production pass;
operation-correlation mutation also fails and restores. No business rule changed.
Final strict rerun remains226 PASS; runner gate12 PASS. Initial and final mutation
results/logs are separately retained in S45 evidence.


## S46 — native NFC binding sheet and preserved drafts

MainActivity now exposes the existing authenticated NCF1/NCS1 controller through
native Canva components. Explicit load, enrollment, confirmed single-slot removal,
status and targeted job cancellation keep the controller's actual result semantics.
Unknown device state disables mutations; closing releases the subscription/staging,
not an accepted device job. The page explicitly says automatic card-to-focus is
not available. No new connection, worker, polling, dependency or default resource.

`nfc_draft_probe=1` runs actual Views with synthetic admission and no BLE. It checks
20 navigation rounds, Activity recreation, minutes/slot retention, disabled unknown
mutations, input touch size, keyboard visibility and reachable bottom action. First
missing method is BLOCKED_INTERFACE, not business Red. Three subsequent actual
Reds found asynchronous Spinner selection lost at close, TextView minimum height
reset by numeric input type, and large-font keyboard clipping. Capture at close/save,
View.minimumHeight and parent-constrained sheet measurement fix these respectively;
no assertion/threshold was relaxed. Shared sheets use resize for the IME.

Final profiles: 360dp light/100% and 412dp dark/200%, with emulator size/density/font/
night restored afterwards. Related focus/expression sheet probes are separately
recorded. Strict226 PASS retain all original63 and two mutation/restoration pairs;
UI scenarios/rounds are not silently added to that denominator. Debug APK/test APK
builds are not release signing or physical acceptance. See `acceptance/s46-20260927.json`
and `s46-native-nfc-ui-evidence-20260927.json` for logs, hashes and statuses.

Interface: native page is wired; L1/L2 controller/session peer coverage remains
available. Automatic dwell/reentry/scene dispatch is NOT_IMPLEMENTED; actual card,
phone BLE, RF/audio coexistence and physical stop are NOT_RUN at L3. Actual UI memory,
CPU and response percentiles are unmeasured; finite eight slots and stable View
instances add no polling or persistent worker. This is not 56 requirements passed.


## S47 — NFC observation to shared focus intent contract

`bk7258_nfc_scene` consumes serial observations from one sensor owner. Generation
is fixed for that lifecycle; positive sequence strictly increases without wrapping.
UNKNOWN means no trustworthy presence conclusion and never rearms. Explicit ABSENT
rearms; the first valid PRESENT consumes one opportunity even if unbound, storage
uncertain, admission closed or mailbox busy. Further cards in that occupancy do not
retry. Initialization is for a new owner lifetime, not a way to bypass this latch.
The component stores no UID and reads only the existing binding cache. Only START
is submitted to the real focus mailbox; accepted ID is not applied/completed state.
The actual owner still resolves cancel/power and revision conflicts before applying.

Seven independent processes use the production scene, bindings and real POSIX store,
focus mailbox and timer. They cover dwell/reentry, unknown, stale generation/sequence,
gates, busy, unknown/uncertain bindings and malformed input. A running timer is not
restarted by a second card; the shared owner rejects START. No mock timer/scene is
used. Two isolated source mutations (UNKNOWN rearms; stale ABSENT accepted) compile,
fail the relevant assertions, and restore to PASS. Initial missing interface is
BLOCKED_INTERFACE. First strict run's seven SETUP_ERROR entries are a missing test
completion marker, not assertion failures; the marker correction changes no oracle.

The AP build compiles this component under NFC + provisioning + Agent. **It has no
worker caller yet**: current GET_PICC_UID errors do not establish absence. Reliable
absence sampling, bounded scheduling/admission, binding load at autonomous startup,
quiesce integration and actual card-to-focus flow remain NOT_IMPLEMENTED. L3 is
NOT_RUN. No polling rate, debounce time or physical absence threshold was invented.
A worker-owned state will cost sizeof(struct bknfc_scene_s) (24 bytes with the target
ABI); no heap, DMA, resident thread, radio or SD I/O is added by this component.
Actual hardware CPU/stack/p95 and RF coexistence remain unmeasured. This is an
intermediate service slice, not autonomous NFC functionality or product acceptance.


## S48 — actual MFRC522 RF software wait deadlines

The active AP configuration selects CL_MFRC522_RF, not the upstream frame driver.
Its real CRC and communication routines added200000ns then required both seconds
and nanoseconds to exceed the deadline. The existing CRC comment specifies200ms;
before repair, the tests freeze that software watchdog for both waits. This is
separate from RF hardware timeout, automatic scan period or a physical absence
criterion. No NFC scene threshold is inferred from this change.

Eight tests extract the unchanged production function bodies and real register
constants, replacing only the register bus and monotonic clock. Four deadline
cases fail before repair (finite clock script catches the overrun); the fixes use
200000000ns and normalized lexicographic >= comparison. Early completion, hardware
timeout and protocol-error paths retain their results. No real sleeping or assumed
physical removal. Initial setup failed to accept '#  define'; corrected extraction
is separately logged, followed by4 actual assertion failures and8 passes.

Strict241 PASS retain original63; target AP build/manifest verified. Isolated wrong
units mutation triggers6 assertions, wrong comparison4; restore passes8. The first
ad-hoc mutation classifier incorrectly demanded exactly4 failures and mislabeled the
6-failure mutant SURVIVED; raw records and correction are preserved, with no changed
oracle or production edits. Driver behavior remains synchronous; no heap/thread/DMA
or RF scheduling was added.200ms bounds software polling when the clock advances;
blocking SPI callbacks and actual scheduling/exit latency are not proved by host
clock substitution. Hardware timer programming is unchanged. The unrelated timer
comment's numeric mismatch was observed but not rewritten in this slice.

S47's scene still has no production worker caller. Distinguishing failed selection,
reader watchdog and a trustworthy no-response observation, admission/cancellation,
autonomous loading and card-to-focus L3 remain required. No physical action performed.
See `acceptance/s48-20260927.json` and `s48-nfc-deadline-evidence-20260927.json`.


## S49 — explicit RF observation without conflating failures with absence

MFRC522IOC_OBSERVE (_CLIOC(0x0010), local driver ABI) accepts a writable observation.
It requires TX1/TX2 control readback enabled and never enables RF itself. On success,
present=1 carries a complete selected UID. Success/present=0 means REQA reached the
hardware timer with a zero error register, not physical proof a card moved. Software
watchdog, malformed ATQA, selection timeout and RF errors remain negative/unknown;
output is zero on every failure. Collision can proceed through normal anticollision
selection. No new wire/schema for BLE, no UID export to the App.

The existing communication API is a wrapper passing no observation output; old
GET_PICC_UID and generic timer return semantics are retained. A shared real helper
collects the optional REQA timeout evidence. Eleven tests execute the production
ioctl, observation and communication/CRC function bodies, replacing only bus/clock
and the external selected-card result. The selection algorithm itself is not proved
by this fixture. Existing RF/selection suites retain their cases and an aborting
stub for their unexercised new ioctl; the new suite invokes the actual route.
The S48 suite now follows the real wrapper into the helper, retaining all8 oracles.

Missing API was BLOCKED_INTERFACE; enum/ioctl macro extraction setup errors are kept
separately. The added residual-error test initially failed: hardware timer plus CRC
error incorrectly appeared quiet. Requiring a zero residual error register fixes it.
All11 pass; isolated software-timeout-as-quiet and omitted-error-check mutations
fail then restore. Strict252 PASS include original63 unchanged. AP build and manifest
verified. Stack-only bounded observations add no heap/thread/polling; actual stack,
RF behavior, bus stalls and voice coexistence remain unmeasured. The original200ms
software watchdog and hardware timer programming remain unchanged.

NFC worker does not call this ioctl yet. It must hold RF/I/O ownership until release,
reject shutdown/racing jobs, load authoritative bindings and feed only fresh results
to the shared scene. Automatic scan timing and physical false absence need separate
validation before claiming autonomous card-to-focus. No board flash/reset/scan was
performed. See `acceptance/s49-20260927.json` and `s49-nfc-observation-evidence-20260927.json`.


## S50 — existing worker invokes observations and shared focus service

The product owner publishes NFC admission only when bound, outside OTA and with
an idle voice channel (or voice not initialized, preserving offline local use).
Reset and power paths revoke admission. The existing NFC worker uses its existing
500ms idle wait, not a new thread/timer. It loads the internal binding store once;
failed load does not keep retrying storage. Successful explicit LOAD remains a
recovery path. Cached revision/durable operation floor is published for clients.
No scan is hidden in a status query. Jobs/RPC and scanning share one I/O owner.

Every scan owns open/OBSERVE/RF-release/close. Only after successful release and
same admission epoch may its result reach the real scene/focus mailbox. Revocation
also cancels this component's pending focus intent; already applied timers remain
under the product owner. Jobs revoke the scene candidate, so registration never
starts a timer while the card remains present. Quiet observation can rearm; unknown
errors cannot. No key/UID leaves the existing device boundary.

Ten L2 tests use actual worker, real POSIX binding store, actual scene, mailbox and
timer. External seams are the OS scheduler, VFS RF ioctl/close and fsync/read fault
hooks. First enrollment persists a binding, requires quiet then reentry, and starts
the same timer. Revoke during storage load initially still opened RF; an admission
check after load fixes that actual Red. Other cases cover dwell, unknown, enrollment,
pending cancel, revoke during scan, power quiesce and release failure.

Initial enrollment fixture omitted the owner admission publication after a job;
adding the missing external event preserves its original timer assertions. Initial
release mutation SURVIVED because the fixture faulted the worker's startup idle
release, never scan close. Injecting at the read callback and asserting one completed
observation makes the same mutant fail. Raw and corrected mutation records remain;
load-admission and ignored-release-error mutants both detect and restore. The reused
scheduler fixture reenters worker on each drain, so its startup idle-release repeats;
it does not prove continuous real scheduling, wall-clock cadence or IRQ behavior.

AP build/manifest, actual product object reference to scene admission, and scene
symbol in final ELF are recorded. Server static size1200 bytes versus1152 before
this slice (+48). Scene observations are bounded stack values; no new persistent
worker, timer, heap or DMA.500ms is the preexisting worker idle interval frozen for
this software slice, completion-relative, not a measured feedback/power guarantee.
Each RF exchange retains the200ms software watchdog; total anticollision/bus-block
latency, CPU average/p95, stack high-water and radio coexistence need L3 measurement.
New RF operation has not been enabled on a physical board in this session.

Remaining: physical false-absence/dwell validation, voice/OTA/K2 combinations,
real card-to-visible-focus acceptance, explicit App capability/status presentation
(the older UI still conservatively says automatic use unavailable), and hardware
resource budgets. Source/host vertical flow is not physical acceptance. See
`acceptance/s50-20260927.json` and `s50-nfc-worker-scene-evidence-20260927.json`.

## S51: read-only NFC scene capability and native status

Authenticated config kind13 returns independent NCA1, 16 bytes big-endian:
magic, capability bits (bit0 card-to-focus), status flags (initialized, admitted,
published bindings ready, I/O active, release fault in bits0..4), signed last
error. Only READ at offset0 is accepted; writes are EPERM and other offsets
ERANGE. Unsupported builds return zero capabilities/flags/error. Query takes
a short existing lock, performs no storage/RF I/O, and remains available during
quiescing. NCF1/NCS1, authentication, sequence and existing job semantics remain.
The ready flag is published with revision/floor, not read from the worker's
in-flight mutable store. A real read-hook test caught that premature publication.

The existing native controller reads NCA1 after its verified NCS1 snapshot. Old
firmware or malformed/missing capability information is unknown, preserving the
confirmed binding snapshot. Connection changes and new writes invalidate the
capability display immediately; a late previous status cannot imply readiness.
A real JVM assertion caught stale capability retention at operation start. This
is a snapshot, not a live guarantee of RF availability or successful timer start.
No new polling, session, thread, persistent write, DMA or authentication bypass.

Ten added strict units: two actual Session/control cases, two worker/control
cases and six real Session JVM cases. Missing APIs initially BLOCKED_INTERFACE;
the stale-operation and in-flight-ready assertions were real Reds before fixes.
Two isolated query mutants detected and restored, separately from the two existing
strict-suite mutants. The selected collection remains272, including original63.
A new runner test found46 selected units omitted from the added-only report group;
current added_ids now contains209 with disjoint complete membership. Historical
reports retain their original grouping, which did not alter their collected list
or total gate result. Runner gate now13 tests, reported separately.

Emulator trace exposed a real floating-window defect: IME reduced visible height
to1083px but the dialog/ScrollView stayed1776px, so requestRectangleOnScreen did
not scroll the field. Shared companion sheets now constrain window height from
the visible display frame. The former NFC fixture could pass without an IME;
it now waits for actual window focus, requires visible IME and bounded geometry,
then retains the full48dp/input and footer visibility assertions. Initial failures
and misleading no-IME passes are retained, not used as keyboard acceptance.
Final emulator outcome and exact input/log hashes are in the S51 evidence record.

L1/L2 and native synthetic UI evidence remain separate. Actual phone/BLE/read-card
status, physical RF errors/coexistence, timer rendering, CPU/p95/stack and long-run
behavior are NOT_RUN. No physical installation, flash, reset or data clearing.

## S52: SC7A20 new-conversion admission for N1

Before autonomous action recognition, MOT-01 requires samples backed by valid
new observations. Actual SC7A20 fetch previously read output unconditionally and
assigned a new host timestamp even when no axis conversion was ready. The whole
production lower half now checks STATUS_REG ZYXDA before reading six output bytes.
No-ready/partial-ready returns EAGAIN without consuming output or publishing a new
timestamp. Bus errors propagate; failed fetch leaves caller output untouched.
Registration enables BDU without changing full scale, ODR or normal resolution.
BDU protects low/high pairs; it does not prove all axes sampled simultaneously.

`test_sc7a20_sampling.py` compiles the entire actual driver and public config
header, replacing only includes with external sensor-framework/clock/mutex/bus
fixtures. No driver function, register constants, state machine or conversion is
reimplemented. Literal bus/status expectations come from the manufacturer manual,
not the production encoder. Eight independent processes cover inactive, invalid,
no/partial readiness, status/data errors, registration BDU and successive valid
conversions (including equal values). Initial6 assertion Reds/2PASS become8PASS.
Two isolated mutants (ignore readiness and omit BDU) fail, then restore to PASS.
These are register-transport host checks, not physical data-ready behavior proof.

The preliminary close hypothesis was rejected before any product edits: the
actual NuttX descriptor is invalidated even when close fails; retaining that fd
would be wrong. sensor_accel.status is calibration status, not errno. Actual sensor
upper-half close currently ignores lower-half deactivate return, so hardware
shutdown confirmation remains a separate unresolved integration boundary.

Strict selected collection280: original63 (including2restores) plus217added.
Runner gate13 remains separate. Driver/header and new test input hashes are included
explicitly, because the historical production_digest does not cover nuttx overlays.
AP build and manifest verification are separate from software tests. An initial
nxstyle invocation used the official checkout's compiled TOPDIR and rejected the
team mirror header path; rebuilding the same pinned tool with the mirror TOPDIR
passes. Python Black24.10.0 and diff whitespace checks pass. No framework upgrade.

Resource change: one bounded one-byte status read per fetch, one stack status byte,
no added persistent state, thread, queue, timer, heap or DMA. Bus transaction and
lock duration need hardware measurement. Timestamp remains host fetch time, not
conversion time. Startup/ODR-change stale registers, missed/overrun conversions,
physical scaling, driver exit acknowledgment, autonomous sampling/recognition and
short expression restoration are not closed by this slice. No hardware action.
See `acceptance/s52-20260927.json` and `s52-motion-sampling-evidence-20260927.json`.

## S53: motion sampling admission and power preparation

The shared motion service now has a short-lock quiesce contract. Stop closes
local/RPC sampling admission before reporting busy; pending worker requests may
produce an error response but cannot open the sensor. Recheck after obtaining the
existing sample mutex prevents a waiter from starting after stop. A read already
running finishes its mandatory descriptor cleanup; if stop is observed before its
completion publication, the sample is sanitized as canceled. Cached replay is
invalidated and new stopped RPC requests return a sanitized error. A response
already submitted to transport is not retractable; this is not transport drain.

Normal and failed-open cleanup record close errors separately from the operation's
first error. A descriptor consumed by NuttX close is never retained/retried. A
cleanup fault refuses new samples/resume and remains diagnostic until service
restart; resume during active I/O is busy. No sleep or timeout claims completion.
The product power preparation includes motion alongside NFC/trigger, waiting on
busy and preserving permanent failure rather than requesting CP transition.
The service stop-before-start intent survives initialization. No new worker.

Seven service/RPC cases compile actual production service/core/client with only
existing OS/sensor transport substitutes. Two actual power-coordinator cases first
failed (motion busy/error ignored), then pass with the participant connected.
The missing service interface was BLOCKED_INTERFACE, never a claimed business Red.
Initial legacy fixture reused the same virtual source after a forced close fault;
its case-reset now explicitly starts fresh virtual device state, not a production
reconnect/reset capability. The late-RPC case initially asserted callback return0;
the existing synchronous trysend fixture returns frame length. Corrected to that
exact fixture contract; its no-new-I/O/no-old-valid-sample assertions remain intact.
Original failure evidence is retained. Final289PASS = original63 +226added;
runner13 separate, two new isolated mutants detected/restored, existing two retained.

AP build/manifest pass and real quiesce symbol linked. Current motion server288
bytes; no new queue, thread, heap, DMA or periodic writes. Actual lock duration,
stack usage and sensor power need hardware measurement. NuttX sensor_close currently
ignores lower activate(false) return: zero here proves no owned software I/O, not
physical standby. Factory-reset coordination, actual uORB deactivation feedback,
autonomous sampling/gesture/short-expression flow and L3 remain outstanding.
No physical installation, flashing, K2/reset or data clearing. See
`acceptance/s53-20260927.json` and `s53-motion-quiesce-evidence-20260927.json`.

## S54: reset coordination includes motion and preserves stop intent

The actual product reset coordinator closes motion sampling admission independently
of NFC and the control owner. A busy/error motion participant prevents product
clear and storage-finish submission. Cleanup completion alone does not reopen a
resource that failed to resume. Motion resumes before NFC; a later NFC failure
closes motion again. A failed control-owner resume closes the owner and both
sensor admissions again and retains FINISHING for a retry, without redoing cleanup.
An existing shutdown request/failure/pending CP request now also suppresses control
owner reopening, in addition to sensor resumption. No reset gesture or timeout
parameter, authentication, persistence layout or erase scope was changed.

Seven selected cases compile the actual product_reset_step function verbatim,
with external sensor, network, storage and owner participants substituted. Six
initial new cases failed before connection; then stronger power-intent and new
owner-resume-failure cases failed before the final admission fix. A partial-owner
resume fault additionally verifies explicit reclosure. These are coordinator
contract failures under controlled participants. Current bkprov_owner_quiesce(false)
returns0 in real production: its injected error is resilience coverage, not a
claim that such an error has occurred on a device. Service behavior remains covered
separately by S53; this is not a fully combined reset/service/storage integration.
The standalone unittest subclass inherits NFC cases, so its all-class discovery
count is not the selected denominator; strict collection selects seven new method
IDs exactly once and preserves previous NFC IDs.

Final296PASS = original63 +233added.13 runner checks separate. Ignoring motion
stop and omitting motion rollback are isolated detected mutants; both restore.
The two existing suite mutants also remain detected/restored. AP build and final
manifest verification pass. No new state/thread/queue/heap/DMA; added short service
handshakes reuse existing locks. Real latency, stack peak, cleanup interruption,
uORB hardware deactivation, actual reset/reclaim and N1 gesture feedback remain
unverified or unimplemented. No physical reset, erase, install or flashing.
See `acceptance/s54-20260927.json` and `s54-reset-motion-evidence-20260927.json`.

## S55: parameterized local action candidates (not physical gesture acceptance)

The existing production motion core now contains a single-owner finite candidate
detector: MOVED for a sufficient sample-to-sample delta, SETTLED after movement
remains inside one quiet anchor for the configured duration, and TILTED after a
stable gravity-range vector leaves the reference orientation. Separate enter/leave
cosines provide hysteresis. Moving/settling candidates are not proof of physical
pickup/putdown or position. No LLM, audio, motor, configuration or power action.

Caller supplies immutable reviewed thresholds, dwell/cooldown and maximum gap;
state is zero-initialized once per owner lifetime. Fixture values (including
400ms settle,200ms gap and example acceleration ranges) are synthetic test inputs,
not deployed sensitivity or performance budgets. No production default is selected.
Invalid/error/gated observations rebase candidates; duplicate time cannot advance
dwell; reversed time is rejected; a long gap starts a new baseline. Previously
emitted cooldown survives rebasing, and time comparison cannot underflow it. Events
suppressed during cooldown are consumed, not queued for later surprise replay.
A fixed anchor detects cumulative creep rather than treating every small step as
stable. Full int32 wire extremes use double arithmetic before products/subtraction.

Nine selected L1 cases compile the actual core with UBSan. Initial missing API is
BLOCKED_INTERFACE. First eight cases passed after implementation; a ninth exposed
admission toggle resetting cooldown, then passed after correction. Before any
implementation, the settle fixture was corrected from700000 to600000us: a stable
vector first observed at200000 reaches400000us duration at600000, independently of
code. No runtime threshold was relaxed to make an observed failure pass.
Forgetting cooldown and skipping settle time are isolated detected mutants; both
restore.305PASS = original63 +242added;13 runner gate checks remain separate.

Actual AP core object contains the candidate function; target build/manifest pass.
It is NOT yet called by the motion worker, so object compilation is not runtime
activation or a complete N1 path. ARM ABI sizes: state72bytes, policy48bytes; neither
has a production resident instance yet. No added thread, heap, DMA, file writes or
hardware polling. Double arithmetic CPU cost, stack high-water and physical
classification quality are unmeasured.

Next binding is explicitly the existing bkmotion_worker and sampling mutex, with
product-owned admission alongside reset/power/voice/OTA. It must feed only released
fresh samples to this core and send finite intents through the existing display
trial service. A later explicit display/default choice must win; cancel only the
owned trial ID; no independent renderer or sensor worker. The integration must
exercise these real owners, not a successful synthetic gesture/renderer mock.
Layer gaps: L1 candidate logic PASS; sampling/scene/display binding BLOCKED_INTERFACE;
production sensitivity and cadence NOT_FROZEN pending physical observations and
resource budget; actual gesture/voice coexistence L3 NOT_RUN. These gaps do not block
independent USB work or other contracted slices. No physical device operation.
See `acceptance/s55-20260927.json` and `s55-motion-candidates-evidence-20260927.json`.

### S56 — stop/resume cannot revive accepted motion work (2026-09-27)

Three independent deterministic schedules exercise the real service, collector,
RPC cache and production core: RPC queued before stop/resume; a local request
waiting for its sampling mutex across stop/resume; completed collection waiting
for its response-slot commit across stop/resume. Each initially failed its
behavior assertion. The external scheduler/descriptor peers remain substitutes;
no production state machine is replaced. The new mutex hook only schedules one
external boundary and is inert in existing cases.

Admission now has a generation captured at RPC acceptance or local sampling
entry, checked before/after acquiring the sample lock and before committing the
RPC response. Stop invalidates the generation; resume never restores it. Repeated
RPC delivery returns the canceled response without reopening the sensor. Fresh
requests after resume remain usable. Generation exhaustion fails closed rather
than wrapping; ordinary service_start does not clear this lifetime state. The
wire ABI and endpoint connection generation remain unchanged. Already submitted
transport messages cannot be recalled; this is not a complete transport drain.

Final selected collection: 308 PASS = original63 +245added, including this slice's
3 added cases. The original63 still includes two restore reruns. Thirteen runner
gate checks, the original two mutation detections and three new isolated
mutation detections/restores are reported separately. The preliminary307 run did
not include the later publication-boundary regression and is not final evidence.
Target build and manifest verified; actual ARM server state296bytes, +8bytes.
No new thread, queue, DMA, file write or hardware poll. Added short lock sections
and comparisons have no measured board CPU/p95 or stack high-water yet.

Source-level integration PASS is not sensor shutdown or physical action proof.
N1 candidate-to-worker/display binding remains BLOCKED_INTERFACE; production
sensitivity/cadence remain NOT_FROZEN; L3 NOT_RUN. Read-only host preflight found
only emulator-5554 in ADB and no Windows serial ports; no physical device was
opened, installed, flashed or reset. Full-file nxstyle/Black checks still report
pre-existing formatting issues and are not reported as passing gates.
See `acceptance/s56-20260927.json` and
`acceptance/s56-motion-admission-evidence-20260927.json`.

### S57 — native CDC RX byte ownership and bounded backpressure (2026-09-27)

The default serial CDC consumer previously armed every OUT transfer even when
its lower ring was full. Investigation through the pinned NuttX serial upper
half also found that receive returned1 instead of the character and placed the
character in the status output; both layers shared the same receive array.

The production path now reserves capacity for the entire256-byte USB transfer,
rearms after consumption, retries an unsuccessful arm on RX enable, and ignores
unarmed/wrong-endpoint callbacks. Reset/disconnect discards the lower RX ring.
The serial upper half has its own256-byte array. receive returns the byte and
zero status. rxavailable stops uart_recvchars before upper-buffer overflow;
uart_read's existing RX-enable path resumes draining. No dependency on optional
hardware flow-control Kconfig was added. The callback-based OTA consumer and
CDC descriptors/maintenance recovery remain unchanged.

Seven selected units exercise actual CDC functions/state and binding statements;
fast-reader and upper-backpressure additionally execute the pinned real
uart_recvchars function. USB endpoint/IRQ/notification and application reads are
external peers. The actual serial-read syscall, interrupt concurrency and DCD
hardware are not simulated successes. Sample bytes are independently generated
as sequence-index modulo251 and compared byte-for-byte at the application or
lower-half API boundary.

Test correction is material: the first serial peer copied the product's wrong
receive ABI. Its preliminary315PASS is INVALID as serial-chain evidence and
retained under contracts-invalid-serial-peer. Replacing that peer with the real
upper half produced seven assertion failures. A retry case then incorrectly
read the already-drained lower ring; it now verifies the same four expected
bytes at the application boundary. The initial misleading-indentation compile
error and a report-hash path outside the team repo are SETUP_ERROR, not business
Red; the external pinned source hash is recorded separately in slice evidence.
No historical committed report was rewritten.

Final315PASS = original63 +252added; this slice adds7, with two real-upper L2
cases. Thirteen runner checks, two baseline mutations/restores and four new
mutations/restores are separate. The four new mutants remove reservation,
restore the wrong receive ABI, alias the upper/lower buffer, or ignore upper
capacity; each compiles, fails an executed assertion and passes after restore.

AIDK AP incremental build/manifest passes; CDC resident object is1504bytes on ARM.
New storage is one256-byte array plus one pending flag (alignment applies), no
new thread, heap, DMA allocation or polling. Transfer and copy bounds remain256;
CPU/p95/critical-section timing and stack high-water are unmeasured. Existing
full-file nxstyle issues remain; Black24.10.0 passes the new Python test.

This is only the RX substrate of USB-02. TX ownership/start-error handling,
independent PC authorization, framed protocol, upload/install/query/cancel,
workbench and task events remain incomplete. Reset does not claim to flush the
NuttX upper/application buffers or revoke a product session; the product protocol
must own that lifetime. Real USB backpressure/re-enumeration and slow-host tests
are NOT_RUN; no device was flashed, opened or reset. Other board profiles were
not built; the callback OTA consumer uses a different data path.
See `acceptance/s57-20260927.json` and
`acceptance/s57-usbcdc-rx-evidence-20260927.json`.

### S58 — CDC TX ownership and queued progress (2026-09-27)

Five new cases execute the actual default CDC TX functions; upper-start and
upper-backpressure also execute the pinned real uart_xmitchars and the actual
buffer-binding statements. All five initially produced assertion failures.
The USB peer retains the submitted pointer until explicit completion, so the
byte ledger also observes in-flight buffer lifetime instead of eagerly copying
and hiding overwrites. Independent bytes are index modulo251.

A rejected start_write no longer consumes FIFO bytes. The bounded copy peeks
until submission succeeds; submission is serialized against local USB IRQs.
The upper serial queue has its own256-byte TX array. TX enable feeds the actual
upper half; completion continues lower queued bytes even after upper TX
interrupts are disabled, then admits remaining upper bytes. Wrong endpoint and
unarmed completions cannot mark the current transfer complete.

Final320PASS = original63 +257added; this slice adds5 and preserves S57 RX7.
Thirteen runner checks, original two mutations/restores and three new isolated
mutations/restores remain separate. New mutants discard rejected bytes, alias
the upper/lower TX array, or omit lower-queue progress; all are detected after
successful compilation and pass after restoration. Build and manifest pass for
AIDK AP only. CDC ARM state1760bytes, +256 from S57; no new heap/thread/DMA/poll.
The DCD submission does bounded FIFO/MMIO work under the critical section;
actual IRQ duration, CPU/p95, stack and wire throughput remain unmeasured.

This verifies software TX submission/progress, not USB/application delivery.
A rejected submission requires a later send/enable stimulus to retry; no timed
retry or failure-reporting product protocol has been added. Reconnect does not
yet invalidate all upper/lower queues or product authorization. Physical DCD
completion, reset/unplug during transfers, independent PC credentials and
framed status/install/cancel/event services remain incomplete. Callback-mode
OTA uses a separate data path and was not changed. No device operations; L3 is
NOT_RUN. Existing full-file nxstyle errors remain; Black24.10.0 passes new test.
See `acceptance/s58-20260927.json` and
`acceptance/s58-usbcdc-tx-evidence-20260927.json`.

### S59 — CDC disconnect notification and old-descriptor quarantine (2026-09-27)

The CDC Kconfig now selects the existing SERIAL_REMOVABLE facility. Default
serial registration starts disconnected. USB configuration calls the real
uart_connected(true); reset/disconnect calls uart_connected(false), notifying
poll hangup and waking both serial wait domains. The lower RX/TX queues are
invalidated. If a descriptor was open, fast USB reconfiguration leaves it
quarantined/disconnected, with both RX arming and TX admission closed until
last-close shutdown. Only then may a new open occur. First-open setup resets
upper cursor indices under the upper-half open/close serialization, instead of
mutating active readers' cursors in a USB interrupt. Setup rejects offline,
quarantined or still-transmitting state. This is serial lifetime, not PC auth.

Five production lifecycle cases initially failed; actual NuttX uart_connected
is linked into the host cases and external poll/wakeup boundaries are counted.
An added fast-reconnect TX-ready assertion separately failed before its gate
was fixed. Existing12 RX/TX cases remain green. Final325PASS = original63 +262
added, with5 added here.13 runner tests, original2 mutations/restores and new3
mutations/restores are separate. Reviving an old descriptor, omitting hangup,
or retaining upper receive cursors is independently detected after compilation.

The first incremental target build retained stale Kconfig and was explicitly
rejected as feature integration evidence. The existing AP CMake resetconfig
regenerated the profile; the public build then completed. Final.config selects
SERIAL_REMOVABLE, the CDC object references uart_connected and the final ELF
defines it. Manifest verification passes. CDC ARM state1764bytes (+4). No added
worker/heap/DMA/poll; generic serial state layout also changes with removable
support. Actual blocking-thread timing, IRQ cost, stack and other boards are
not measured. New test Black passes; historical full-file C style issues remain.

The host layer executes real lifecycle/notification functions, not full
open/read/write/close syscalls or concurrent task wake scheduling. Last-close
ownership is the existing NuttX upper-half contract. DCD reset completion and
late IRQ ordering need hardware validation; no product authentication is
created by opening a port or changing DTR. USB framing, independent PC
credentials, workbench/install/task events and physical reconnect tests remain
incomplete. No device operation or deployment was performed.
See `acceptance/s59-20260927.json` and
`acceptance/s59-usbcdc-lifecycle-evidence-20260927.json`.

## S60：同一 TLS 状态机的显式传输接口（2026-09-27）

`bkprov_tls_start_transport` 接收一份有界非阻塞字节传输描述：context、当前连接
代次、带期望代次的 read/send、最大分片和发送间隔。TLS 对象复制描述并借用 context
直到 close；同一产品 worker 仍独占 TLS。后端须在实际 I/O 边界拒绝旧代次，不得
重复提交正数返回的字节。新接口不授权设备、不选择业务服务。原 start 包装器仍用
GATT 20 字节/10ms，TLS1.2、密码套件、证书与应用认证、期限及终态清理契约不变。
本片删除 TLS BIO 对 GATT 的硬绑定，没有新增第二套 TLS/SDC1 状态机。

先扩展已有 `test_provision_tls.c/.py`：20 份临时公开测试证书各跑原 GATT 路径及
独立代次的内存字节流，真实 mbedTLS 客户端验签/握手，双向1024字节内容、背压、
篡改拒绝、代次撤销和清理。独立流打开后改变 GATT 代次并销毁调用者的描述，验证
会话没有借用描述或错误依赖 GATT。成功样本记录公开证书 SHA256；测试私钥仅在
原有临时夹具中创建/使用/移除，不涉及板端或发布身份。流的短写仍由20字节 peer
刻意触发，并不模拟 USB/DCD。后续原 claim/control/session 回归仍跑原 GATT。

新增 `test_provision_tls_transport.c` 包含真实 TLS 源码，定向检查 BIO 边界：
非法描述/陈旧代次入场拒绝、短读写、越界返回、零/背压、回调中代次改变、发送
间隔及倒退时钟。这里只直接测试回调适配，不能代替上面的真实密码协议集成。
两者作为一个明确标注的 `USB-01.tls-transport` bundle 收集，内部样本不虚增
逐例分母。全套326PASS = 原63（含2恢复复验）+ 新增263；13运行器门禁另计。
原2变异保留，新增3变异（越界返回放行、BIO忽略旧代次、错误使用GATT代次）均
在隔离目录中被断言检出，逐项恢复后通过。新接口不存在时的初次编译失败标
BLOCKED_INTERFACE，不是业务Red；没有把编译失败当变异检出。

首次加入 selected IDs 时遗漏 added_ids，分组自测确实失败；补齐同一必需用例的
报告分组后重跑，未减少收集或改断言。原日志留在 `out/shaniu-s60/`，旧S0—S59
报告不改写。ARM实测 TLS 对象2632字节，传输描述24字节（每TLS对象增加24）；
无新增线程/DMA/持久写入，原有 TLS 动态分配仍存在。CPU/栈/真实传输性能未测。
目标 AP 构建及 manifest 校验通过。完整旧C文件 nxstyle 仍有历史格式问题，不报
风格全绿；Python Black24.10.0 与 diff 检查通过。

分层：传输接口已实现，主机真实TLS集成通过；USB fd/端点接线、PC独立凭据授予与
撤销、同一SDC1服务、资源安装工作台与事件工具未实现；L3 NOT_RUN。没有安装、
刷写或操作真实设备。不能以326/326推导56项需求通过。下一片需继续共用设备服务
并建立电脑自己的授权，不复制手机控制密钥，不通过开串口自动认领。

## S61：独立传输进入同一 SDC1 控制会话（2026-09-27）

`bkcontrol_pair_start_transport` 复用现有 TLS、SDC1 解析和 session，由可信调用者
显式提供该客户端的控制凭据及业务回调，不读取/复制手机控制密钥，不自动授予
权限。新增入口只接受 SDC1；原手机 GATT 入口仍可转入既有 SPV1 持有证明/恢复
流程。会话保持单 worker 串行处理；撤销必须关闭会话或使其所属传输代次失效，
不能仅删持久凭据却留下已认证连接。配置/OTA handler 仍按原合同在认证前绑定。
这里只提供接线接口，产品 owner 尚未实例化 USB 控制会话和电脑凭据生命周期。

先扩展已有真实 TLS 用例，再实现接口。原来不存在新接口的编译失败明确记为
BLOCKED_INTERFACE；新代码没有改动 SDC1 序号、认证比较、配置缓存或取消状态机。
20组临时证书在独立流上执行：空传输/旧代次入场失败并清理、未认证命令拒绝、
手机测试凭据无法认证另一个 principal、SPV1 拒绝、分片AUTH后命令只执行一次、
背压不重复副作用、CANCEL受理仍回报busy/退出未知、重复旧序号拒绝、所属代次
撤销后在途命令不执行、超长APPEND头拒绝、10秒认证期限和整个会话清理。
另以真实旧 GATT/TLS/claim 路径验证 SPV1 持有证明可正常到 READY，防止通过
全局禁用恢复入口来实现隔离。业务动作终点是计数/状态观察器，不是完整设备服务，
取消受理不证明硬件已停止。原 `test-control-pair` 使用的 TLS 替身仍单独标注。

既有 `USB-01.tls-transport` bundle 增强，全部326个已选执行单元重新通过，原63
完整保留；本片没有新增收集ID，内部20组不追加分母。13运行器检查及原解析目标
通过；3新变异（给新入口开放SPV1、把手机SPV1也关闭、跳过控制认证）分别被
真实断言检出，恢复后通过；原2变异继续检出。报告及输入哈希另存，不改写S60。

AP构建/manifest通过，ARM pair对象仍69016字节（新增bool占用原padding），本片
没有新增实例/线程/DMA/持久写入。第二个并发控制对象的资源成本并未因此消失，
后续接线须预算该对象及TLS堆分配；CPU、栈和端点吞吐仍待实测。旧C文件完整
风格检查仍失败，未称风格全绿；Python Black24.10.0与diff检查通过。

接口与主机协议集成通过；实际CDC产品通道、独立PC凭据授予/撤销/持久化、共用
设备业务仲裁接线、资源工作台与任务发送器仍未实现，L3 NOT_RUN。没有刷写、
安装、清数据或操作按键。326PASS不表示56项验收通过，下一步继续产品通道与授权
生命周期，不把模拟字节流称为USB成功。

## S62：原始非阻塞串口承载 TLS/SDC1（2026-09-27）

`bk7258_control_serial` 仅打开固定原生CDC节点 `/dev/ttyGS0`，不启动USB、不切MSC、
不执行Shell，也不自动重连/授权。AP当前没有NSH或其他该节点消费者。对象由一个
产品worker独占，O_NONBLOCK/O_NOCTTY/O_CLOEXEC，保留虚拟端口硬件参数但关闭
字符翻译/回显/信号；没有DTR、flush或drain操作。每次成功打开采用新epoch，
饱和拒绝；poll的HUP/ERR/NVAL及致命I/O令该代次失效，旧回调拒绝。close先撤销
准入再关闭一次，保持epoch；NuttX原串口close在O_NONBLOCK下不等待TX排空。
这些退出语义不是远端取消确认，也不是DMA/IRQ已停止的证明。

先写五个生产模块测试，用链接器仅将固定节点重定向到真实Linux PTY，真实
termios/poll/read/write/close验证全部256种字节、无回显、短写、有界背压、
拔掉peer后旧代次拒绝、新打开不复活旧回调、非法参数、代次耗尽及打开失败。
再将已有真实TLS客户端/服务端＋SDC1回归接上同一生产串口适配，20组临时测试
证书全部通过。PTY不含CherryUSB/DCD，不能冒充真实USB或阻塞任务调度验收。
生产模块进入CMake/Make编译，但目前没有产品启动caller/常驻实例；线程、独立
PC凭据、共同业务服务接线和客户端连接准备/重同步流程仍需后续完成。

完整331PASS = 原63（含2恢复）+ 新增268；本片新增5个独立执行ID，20组协议
样本留在原bundle中不加分母。13运行器检查通过，原2变异保留；新3变异（输出
换行翻译、旧代次放行、阻塞打开）都被断言检出，恢复后通过。首次接口不存在的
编译失败为BLOCKED_INTERFACE；严格C编译缺POSIX特性宏的SETUP_ERROR已修。
首次全收集因为夹具误打印SHANIU_CASE_PASS而产生5个SETUP_ERROR；纠正为现有
CONTRACT_PASS后全量重跑，不修改运行器判据。首次报告保留在
`out/shaniu-s62/contracts-marker-error/`，未覆盖历史。

AP对象编译及manifest通过；ARM串口对象12字节，无新增线程/DMA/堆分配或持久
写入，尚无生产实例。字节收发本身不循环等待；真实锁等待、CPU/栈和吞吐未测。
Python Black24.10.0及diff检查通过；环境没有clang-format14，未宣称C格式验证。
源事实：固定NuttX serial.c的TCGETS/TCSETS上层处理、nonblocking close和
SERIAL_REMOVABLE，与S59的CDC隔离契约共同构成接线基础，未修改官方源码。

后续缺口：产品USB worker与电源/维护协调、连接准备/错误后重同步、电脑自身
凭据授予/持久化/撤销、工作台和任务事件，以及实物验证。没有现场操作。

## S63：与当前owner绑定的独立电脑凭据存储（2026-09-27）

新增 `bk7258_pc_grants`，复用真实 `bkprov_store` 的私有内部卷事务。首版一个
电脑principal槽；手机仍使用原控制凭据，两者独立。PCG1固定88字节：magic4、
capabilities BE32、owner binding32、client id16、PC key32。binding为
SHA256(`SHANIU-PC-OWNER-v1`的18字节ASCII拼接当前owner key32)，不存第二份
原owner key；公开snapshot仅revision/id/caps。cap位1/2/4/8对应资源、场景、
任务、诊断，尚未成为对外消息schema或完成权限分派，不含owner/reset授权。
电脑key不得为零或等于当前owner key；加载已存记录也执行相同独立性检查。

已认证owner的单一文件worker才能调用set，输入非零16字节transaction和expected
revision。最后一次相同transaction/内容可幂等回查，冲突内容EEXIST、旧revision
ESTALE。撤销写caps/id/key全零的持久墓碑，保留revision，不通过删除记录将版本
倒回零。owner变化时旧PC凭据不可读取，公开快照不暴露旧id；修改Wi-Fi但owner
不变不会因此失效。结果未知EINPROGRESS时该对象拒绝授权/写入/同启动重开，
新进程重读才能协调；明确的发布前失败仍保留旧有效文件，不能称撤销成功。

先写真实存储用例再实现，缺接口的初次编译单列BLOCKED_INTERFACE。9个新增ID
覆盖保存/撤销回读、owner变化、revision/重放、非法/共用owner key、文件同步
失败、目录同步未知与exec后读取、损坏、独立正向golden及磁盘中别名key拒绝。
独立hashlib正向向量通过后，别名key向量发现写入校验未覆盖加载的真实Red；补
加载校验后通过。不能把该Red外推为已在线上发生的凭据泄漏。

已有TLS/SDC1测试的独立principal现在从该真实存储读取key，20组证书同时覆盖
内存流及实际PTY串口路径，手机/PC凭据隔离继续成立。只替换文件系统故障边界
和设备动作终点；不是App授权/真实USB服务已跑。全集合340PASS = 原63 +277新增，
本片新增9；13运行器另计，原2变异保留，新3变异（跳过加载独立性、未知时继续
授权、把磁盘旧owner当当前owner）均断言检出并恢复。历史报告不改写。

AP对象编译与manifest通过；ARM对象696字节，PCG1本体88字节，无新增常驻实例、
线程或DMA。每次调用既有store_commit仍有32KiB临时堆分配，不能忽略；栈高水位、
CPU/文件系统时延待实测。环境缺clang-format14，不声称C格式检查通过。

未完成：固定产品root/文件worker接线、手机授权确认与PC端生成/交付、SDC1权限
分派、grant revision变化时关闭已认证USB会话、恢复出厂精确清理（同一手机key
再次出现也必须已撤销旧PC）、工作台与事件工具、实际设备验收。模块本身不是
授权入口或会话撤销器，不能仅删磁盘凭据后保留活会话。没有刷写、安装或实物操作。

### S64 电脑凭据的恢复出厂清理（2026-09-27）

- 在 S63 存储实现上，6 个独立用例调用真实 storage worker / PC grant / store，
  仅在 unlink/fsync 外部边界注入故障。初始 clear/unlink/sync/symlink 4 Red，
  absent/no-marker 2 Green。缺少清理是源码路径缺口，不表示尚未启用的 PC 功能
  已在线上发生凭据复活。
- 实际 JOB_RESET 在原产品退出回调成功后，先精确删除 `pc-grants/config.pending`
  和 `config.bin` 并同步目录，再走原完成回执。保留同目录其他文件、不递归、
  不创建缺失目录、不跟随根目录符号链接。错误保留 SRV1；同步未知仍保持
  EINPROGRESS，禁止同进程重开；新进程验证标记并续清理。
- RST-01.pc-sync 初稿错误要求未知同步后返回普通 pending=1 并允许同进程重开。
  依据既有 store sync / storage uncertainty 合同，纠正为未知状态关闭访问、
  fork+exec 新进程恢复；原失败日志保留在 `out/shaniu-s64/sync-after.log`。
  没有放宽同步要求或将未知结果改成成功。其余初始失败也单独保留。
- 固定产品目录 AP `/cpdata/shaniu/pc-grants`、CP `/data/shaniu/pc-grants`；
  RPMsgFS 仅放行精确 AP 路径，类似前缀/路径穿越拒绝，沿用原就绪/介质检查。
  路径断言先 Red 再修，纳入既有 RST-01.nfc-filesystem 执行单元，不虚增分母。
- 最终 346 PASS = 原 63 + 新增累计 283，本轮新增 6；13 运行器检查、原 2 变异、
  本轮 3 变异检出及 3 恢复另计。AIDK AP 构建和 manifest 校验通过。
  原始日志位于 `out/shaniu-s64/`，逐例/输入哈希在 acceptance/s64-20260927.json，
  证据和测试纠正在 s64-pc-reset-evidence-20260927.json。
- 沿用已有存储 worker，不新增线程、常驻对象、DMA 或正常运行轮询/写入。
  清理最多 2 次 unlink 和 1 次目录同步；局部 store/path 对象，不分配大块堆。
  CPU/p95、实际 RPMsgFS 阻塞时间、栈高水位仍待测。主机 fsync 故障不等于
  实板 LittleFS 掉电验证；NuttX 路径沿用现有元数据提交契约。
- 当前无生产 PC grant 缓存/USB worker 实例；未来 caller 必须先撤销准入并退出
  活会话，再进入清理，不能仅凭删盘文件撤销已复制的密钥。授权 UI/权限分派、
  USB 产品 worker、工作台和任务事件仍缺。未刷板、未触发实物重置。

### S65 电脑会话授权版本与权限分派（2026-09-27）

- `bkpc_control_start/step/close` 复用实际 PC grant、TLS、SDC1 pair/session，
  借用调用者持有的 pair/transport/身份，不复制手机凭据。单一串行 owner 同时
  持有 grant 和会话，grant 仅在 step 之间变更；跨线程同步并未由此自动获得。
- 启动时绑定 revision/client/capabilities，每次 step 在处理 TLS/排队输入或输出
  前检查实际 grant 缓存，业务回调也检查。版本变化（包括同 key/同权限再授权）、
  撤销或未知持久结果关闭并清零 TLS/暂存记录。close 不关闭 fd、不取消已经
  受理的服务任务、不宣称 DMA 退出；未来 worker 必须完成对应资源退出。
- 任何有效授权允许 STATUS/INFO 和非秘密 CAP1；SCENES 允许 focus/临时表情；
  RESOURCES 当前只允许眼睛状态读取。其他配置和普通写命令拒绝，不注册 OTA，
  不继承 SPV1。旧 HTTPS 眼睛导入不是 USB 文件入口，不能借它宣称 N3 导入完成。
  TASKS/DIAGNOSTICS 扩展仍须明确命令及接线，不是已经实现。
- 先写真实 TLS 测试，新头文件缺失记 BLOCKED_INTERFACE，不当业务 Red。20 组
  证书分别覆盖原 GATT、独立内存流、实际 PTY，PC 权限测试在后两条路径执行；
  服务端点只观察动作计数，未替换正在验证的授权/协议/持久化状态机。
  验证拒绝 owner/网络/重置/唤醒配置、允许专注请求、排队读取及暂存 APPLY 前
  撤销、已受理后丢 ACK 不重做、同权限重授权、写入失败与未知同步分别处理。
- 最终集合仍 346 PASS（原63、累计新增283）；本片扩展现有 USB-01.tls-transport
  bundle，新增执行ID为0，不把内部重复样本加到分母。13运行器检查、原2变异、
  新3变异/恢复另计。变异为忽略revision、跳过step检查、忽略SCENES权限，均为
  可编译的真实断言检出。日志 `out/shaniu-s65/`；acceptance/s65-20260927.json
  和 s65-pc-control-evidence-20260927.json 固定逐例/输入与边界。
- AIDK构建/manifest通过；新增ARM上下文56字节，借用原69016字节pair，不新增
  产品实例、线程或DMA；step/权限检查无文件I/O。新C/H完整nxstyle通过，Python
  black24.10.0通过。栈高水位、CPU/p95和双客户端峰值尚待实际 worker 测量，
  不能将物理PSRAM容量当可分配预算。
- USB产品worker、手机授权确认/密钥交付、共享真实服务终点、包安装/任务协议、
  工作台仍未接；没有启用新并发。生产接口实现和主机协议联动不等于实际USB
  服务可用或实板验收；无现场操作。后续必须先绑定授权生命周期再启用通道。

### S66 复用现有文件 worker 的电脑授权事务（2026-09-27）

- PC load/set 加入现有 provision storage 单 worker，不增加线程，不在调用方
  做文件I/O。load 明确提交加载/目录准备工作；snapshot 只复制缓存，不隐式读盘。
  主配置 revision 与 PC grant revision 分别验证；配置变更后旧绑定结果拒绝，
  产品调用者仍须提供来自当前已验证配置的 owner key，这不是新的认证入口。
- 请求字段由 worker 持有副本，精确重试返回同一 pending/最终结果；不同任务
  在忙时拒绝，不覆盖候选。已知写失败保留旧授权，重试同transaction不会偷偷
  再写；显式新transaction才重试。未知持久提交保持 EINPROGRESS，拒绝刷新/
  同进程 stop-start 清掉未知状态。原网络配置和身份结果不被 PC 写入结果覆盖。
- 内部 `bkprov_pc_snapshot_s` 为80字节，含设备内部PC凭据，绝不能直接作为手机
  或电脑的公开回包。错误时整个输出清零；新 owner 看不到旧授权密钥；重置
  完成会清理 worker 缓存及此前S64的磁盘记录。调用者必须在变更前关闭PC会话，
  完成后再发布新的只读视图，不能跨线程借用可变 grant 对象给 S65。
- 先写6个生产worker/store/grant测试；初次API缺失为BLOCKED_INTERFACE，首版
  实现6例通过，不制造虚假Red。阻塞fsync期间验证副本所有权与非阻塞返回，
  覆盖revision、重启/撤销、已知写失败、未知发布、恢复出厂后的同owner再绑定。
  仅替换fsync故障/阻塞边界，不替换生产状态机；数据由TemporaryDirectory清理。
- 本片新增6个NET-03.pc-storage-*执行ID，均通过。首次完整运行352 PASS；
  随后复跑351 PASS / 1 FAIL_ASSERTION，失败为USB-01.tls-transport客户端握手，
  当时没有错误码且临时输入已清理，根因尚未确定。两次均完整收集原63与新增289，
  不用首次绿灯覆盖后续失败，不将S66标为完整通过。13门禁、原2变异和本轮3变异
  检出/恢复分列。3变异：不检查主配置revision、刷新清掉未知、在写入中发布缓存，
  均可编译且触发业务断言，恢复通过。
- 补充20组、200组定向握手以及20组ASan/UBSan实验未复现，不证明问题消失。
  测试增加异常TLS返回码、校验状态及队列长度诊断，失败立即保持非零结果并将
  合成证书/测试密钥保留在ignored out/的700目录/600文件；不公开测试密钥，
  不改变认证/断言/期限/默认20组数量。证书不足以重放随机握手的全部状态。
  逐例失败结果存于acceptance/s66-20260927.json，首次结果另存
  s66-initial-20260927.json；诊断、输入与日志哈希见
  s66-pc-storage-evidence-20260927.json。原始日志out/shaniu-s66/保留。
- AP构建/manifest通过；ARM storage_s 50520→51344字节（现有对象增加824），
  沿用16KiB线程栈和既有store提交32KiB临时堆，未增加DMA或采样。CPU/p95、
  实际文件系统阻塞时间和栈高水位待测，不能由编译给出性能成绩。
  TLS/PC Python入口black24.10.0与diff检查通过；运行器保留原格式，避免无关
  重排（AST核对相同）。C全文件沿用历史格式，nxstyle未通过，
  新增长行已折行；不将局部编译当全文件风格通过。
- 仍缺手机授权协议/确认UI、产品主循环初始化与授权视图发布、USB worker及
  服务仲裁/工作台。当前API由主机测试调用，未启用产品电脑授权入口，也未
  把内部密钥结构接到公开协议；没有刷写、安装、按键或清理实物数据。

### S68 手机管理电脑授权的协议适配（2026-09-27）

本片定义并测试 kind=14，复用真实 SDC1 Session、storage worker、PC grant/store。
它不是新的认证入口：只有原 Session 认证成功才分派；PC 的 S65 权限分派仍拒绝
kind 14。当前生产主循环、手机UI和USB owner 尚未绑定此适配器，不报告为可用能力。
产品调用者必须提供当前已验证 owner 对应的配置 revision，提交前关闭 PC 准入与
旧会话；不能把本片测试中的薄路由当作真实设备已接线。原 TLS 偶发失败仍未闭环。

线格式 v1 均为大端，固定长度，保留字节为零：

| 格式 | 偏移与内容 |
| --- | --- |
| PCW1，88字节写请求 | 0 magic；4 主配置revision u64；12 预期PC revision u64；20 transaction[16]；36 client[16]；52 PC key[32]；84 capabilities u32 |
| PCS1，64字节只读快照 | 0 magic；4 active u32；8 主配置revision u64；16 PC revision u64；24 capabilities u32；28 reserved；32 client[16]；48 持久transaction[16] |
| PCR1，32字节只读回执 | 0 magic；4 result i32；8 phase u32；12 reserved；16 请求的transaction[16] |

PCW1 capabilities=0 为撤销，client/key 必须全零；非零授权沿用 S63 的四个已知位，
client/key/transaction 必须非零，独立PC key不能等于owner key。未知位/错误长度/
魔数/旧revision拒绝，禁止原样公开内部80字节带密钥结构。PCS1从不包含密钥。
BEGIN/APPEND只暂存，APPLY消耗暂存并返回存储受理结果；-EAGAIN不表示已持久化。
CANCEL只清Session暂存，APPLY后不能撤销正在提交的worker，也不能假报远端取消。

READ现有4字节kind/offset读PCS1；20字节kind/offset+transaction读PCR1。每次16字节，
offset必须对齐且在记录内。PCR1 phase=0为查询未能确定（result给出ENOENT/ENODATA/
ESTALE/EBUSY等实际原因），1处理中，2持久成功，3已知失败，4持久结果未知。
phase与result分开；底层已知失败即使返回EAGAIN也必须phase=3，不能无限显示处理中。
停止新写入期间允许结果查询。查询不加载卷/提交工作/取消写入。单槽只保证当前
持久事务及本进程最后尝试的回执；更早事务查询未知，不能将未知说成执行失败。
跨分片读取PCS1的客户端必须复核头部revision，若变化则重新读取，不能拼接旧新字段。

9个新增NET-03.pc-auth-*执行ID覆盖认证/序号、非法输入、取消、成功/撤销、已知失败、
未知提交、受阻写入及重开回执。初次缺接口为BLOCKED_INTERFACE。EAGAIN终态用例
先FAIL_ASSERTION后修复；原始Red留out/shaniu-s68/receipt-red.log。第一次集合因Make
依赖误删旧test_pc_storage入口产生6 SETUP_ERROR，355PASS，不当产品Red；已修接线。
首次变异还暴露pending phase断言缺失，补断言后3项变异（泄密/假成功/失败当等待）
均检出、恢复通过。既有完整报告不覆盖；最终逐例结果及限制在S68证据JSON中。

实现没有新增线程、常驻状态、DMA或FS同步调用；局部公共快照64字节与内部view80字节，
真实栈高水位/CPU/p95仍待测。仅主机与AP构建，不是手机授权或USB实板验收。

### S69 真实产品中的当前 owner 授权绑定（2026-09-27）

- 在真实配置激活函数中，从持久快照解码SCB，确认control_key与已绑定手机owner
  一致，再向S66文件worker提交PC加载；所有权比较为只读，无网络/密钥输出。
  准备位于SSID/Agent core/云恢复门禁之前；授权使用已保存revision，不复用表示
  网络应用进度的g_config_revision。配置变化后旧版本由真实存储层拒绝，坏包或
  owner不匹配关闭此入口。PC准备失败不冒充云失败；查询显示其真实不可用结果。
- S68 kind14已接入生产product_config：OTA忙时可读、不可新写；Session继续负责
  认证/序号/停止期间的准入。重置完成时清授权绑定。仍没有USB产品owner/工作台，
  手机原生授权UI尚未接入，不能把“手机协议可分派”称为完整电脑使用体验。未来
  USB owner必须加入变更前关闭旧会话、完成后发布只读授权视图的规则。
- 10个NET-03.pc-owner/pc-product-*执行标识。前5链接真实owner、SCB编码/解码、
  worker/grant/store；后5还编译从生产文件提取的原样配置激活函数与product_config。
  已有TLS身份预绑定，GATT空闲与无关云/OTA/显示端点为外部替身；未替换被验证的
  授权/存储/配置状态机。真实激活函数读取实际持久SCB owner-only记录，在无SSID、
  无云配置、Agent core未就绪时仍能准备本地授权。不是实际BLE认证/首次认领证明。
- 初次接口缺失为BLOCKED_INTERFACE；最初5例因夹具2023日期被既有SCB格式拒绝，
  修正为固定2025日期1750000000后执行，未改生产时间限制。真实product_config
  漏接kind14的断言先Red，接入后Green。四项隔离变异：跳过owner比较、绑定旧revision、
  解绑保留权限、删除实际启动准备调用；均可编译并被断言检出，恢复通过。
- 新增全局缓存仅revision 8字节、error 4字节（ARM对象符号核对），不保存第二份
  owner key，不加线程/DMA/同步文件I/O。局部解码结构与既有激活工作区仍有栈/堆
  成本，CPU/p95和真实栈高水位未测。新模块C/H风格检查与AP构建/manifest通过。
  S66偶发TLS失败仍未闭环；后续TLS通过不注销该记录，也不构成发布或实板通过。

### S70 原生 App 电脑授权管理与真实 SDC1 接线（2026-09-27）

- 发现并修复既有接线缺口：Android底层编码器未接受专注kind10、表情试用11、
  NFC绑定12、能力13及电脑授权14。旧Controller测试的传输替身绕过了该校验。
  新增7项真实SDC1测试，合同合法请求在修改前6 FAIL_ASSERTION/1 PASS，修复后全过；
  仍拒绝未知kind、越界/不对齐offset、错误长度、只读能力写入、错序及未认证请求。
  kind14的PCS1与PCR1分别严格为64/32字节，不能互换；不放宽原kind及SDC1校验。
- 新PcAuthorizationController复用前台Session及真实编码器。公开PCS1六次读取
  （0/16/32/48/0/16）核对两个revision；只提供查看与明确确认后的撤销，不导出或
  新建PC密钥。88字节撤销记录分32/32/24发送，APPEND全部确认后才APPLY；请求
  缓冲用完清零。APPLY ACK含EAGAIN仍只触发回执查询，phase3即使result=EAGAIN
  也是已知失败。只有phase2、匹配事务ID、回读inactive且持久事务ID相符才确认撤销。
- 9项新JVM用例经过真实Controller→Session→SDC1，只有远端协议peer为替身；
  覆盖混合revision、回执错ID、未知持久结果、迟到ACK、其他编辑器占用、提交前
  关闭/断线及提交后恢复查询。只有进入APPLY才向Activity暴露回查ID，
  避免重建恢复未提交的暂存任务；该边界新增断言也先Red后Green。首次没有Controller为BLOCKED_INTERFACE，不称业务Red。
  后续本地BEGIN被拒、提交前关闭/断线残留事务或unknown状态分别先Red后修。
  两个隔离变异（失败当等待、跳过事务echo）断言检出；恢复9项通过，另计不混分母。
- Canva原生页面增加“电脑授权”：未知状态禁用撤销、单独确认且绑定当时的快照，
  关闭底部页同时关闭确认框。只保存公开设备标识/事务ID到Activity saved state，
  支持同设备页面重开/系统保存状态后的恢复；不声称覆盖未保存状态的进程丢失或
  持久事务历史。新授权入口和电脑端完整配对仍未实现，没有把撤销页面当工作台完成。
- 模拟器UI用例只注入公开快照，检查未知门禁、负向确认零请求及关闭后的弹窗清理。
  第一版断言在Android异步OnDismiss回调前检查，改为waitForIdleSync后再检查；
  原失败保留，不改变关闭契约或生产代码来适应测试。不是BLE/实板授权证据。
- 最终集合387项（原63+新增324）；本片新增16个ID。14项既有SDC1 JVM、13项
  运行器门禁及模拟器UI单列。首轮新增ID未加入added_ids被分组门禁检出；修正清单
  后重新完整执行，保留原报告。APK和instrumentation构建，模拟器覆盖安装均有记录。
  Mi10本次枚举在线但未安装/操作；板子未刷写，固件和Agent依赖未改。
- 本片没有新固件线程/缓冲。App仅当前页面持有有界64字节拼装、最多88字节请求及
  一条Session任务，无新增定时轮询；对象/框架额外开销和真实CPU/p95未测。
  USB运行owner、授权密钥交换、完整工作台、任务提醒及实板路径仍待接线/验收。
  S66偶发TLS失败保留，当前通过不关闭该问题。详见同目录acceptance下S70证据。

### S71 电脑客户端与真实 PC TLS 互通（2026-09-27）

- 新主机客户端位于tools/bk7258/_lib/workbench.py，由既有唯一CLI的workbench
  status/info命令消费。固定证书SHA256先于串口打开检查，TLS继续使用CERT_REQUIRED，
  握手后再核对实际leaf指纹，最后才发送独立PC凭据。合法证书链也不能替代精确pin。
  没有另造加密实现、无设备身份/签名密钥生成、不复用手机Keystore。
- 复用既有native串口打开器，新增可选日志label/output，旧OTA调用默认行为及文案
  保持；新入口用stderr且不重定向整个进程stdout。只接受选定native VID/PID端口，
  USB身份分类不是授权。只发送AUTH/STATUS/INFO，不发Shell、配网、OTA或模式转换。
  原串口驱动同步read/write时限保留，主机操作期限不冒充实板响应SLA。
- 13个USB-01.pc-client-* ID，每项独立执行生产客户端；外部TLS peer由Python
  OpenSSL实现，覆盖分片/短写、停流/反压、错误序号/字段、设备拒绝、单调时钟倒退、
  生命周期、CLI输出与串口分类/适配。临时测试CA显式设置CA属性，合法其他leaf
  使用它签名；两种证书的公开DER SHA256写入各例日志，测试密钥不输出。
- 既有USB-01.tls-transport集合内再执行两个跨语言子场景：真实mbedTLS、PC grant
  持久化/权限和SDC1模块，外部字节流用管道替代USB，STATUS/INFO业务值由明确
  的测试服务提供。独立PC凭据成功读取，手机owner凭据失败关闭。两例不额外冒充
  顶层执行ID；日志独立标记。不是物理USB、真实版本回读或手机配对验收。
- 缺Python接口和CLI路由分别为BLOCKED_INTERFACE/接线错误。第一次跨语言正例
  已完成TLS/认证/状态读取，但测试服务未填INFO却断言0；现显式填0.7.14/build123/
  counter661后核验，未将未初始化的UINT32_MAX改成需求答案。原失败和合成证书
  保留在out/shaniu-s71/tls-failures，不作为产品TLS失败根因。一次无效的样本环境
  变量未被运行器使用；实际继续固定20组，没有缩小TLS覆盖。
- 隔离变异“跳过实际leaf指纹”和“忽略SDC1序号”均被可执行断言检出，恢复13项
  通过；原两项存储变异/恢复继续保留。最终集合400=原63+新增337，本片新增13；
  13运行器门禁、两项互通子场景、变异/恢复分别记数。冻结前后源文件哈希另存，
  主机工具不借旧production_digest的固件/App范围假称无变化。
- 本片不改固件/App/Agent，不编造上板结果。客户端一次只持有一个请求，固定40字节
  回复，TLS调用/积压设64KiB上界和4096字节块；Python/OpenSSL额外内存及真实CPU、
  OS串口阻塞、USB时延仍未实测。S66偶发TLS问题仍开放；手机授权交换、受保护的PC
  凭据保存、USB产品owner、资源安装/网页/事件入口仍未完成，也未启用真实端口。

### S72 · 手机提交独立 PC 授权与目标回读（2026-09-27）

在 S71 `5222e5a3` 上，先补 `PcAuthorizationControllerTest` 的授权行为，再实现
真实 Controller → Session → SDC1 的 `grant` 路径；设备端仍使用既有 PCW1/PCS1/PCR1。
独立 client/key 与权限由后续可信电脑交换入口提供，本片没有配对 UI、密钥生成或传输入口。
不能把可调用的提交 API 称为用户已能首次授权电脑。

- 88 字节 PCW1 固定编码两种 revision、transaction、client、32 字节 PC key 和权限；
  输入长度、非零及权限范围先校验。借入数组只复制，不由控制器清除；自有暂存继续按
  现有提交、失败、关闭路径清除。状态与 Activity 保存数据不含密钥，不声称 JVM 完全擦除。
- APPLY 回复不是授权完成。只有 PCR1 持久成功且 PCS1 的 transaction、递增后的授权
  revision、client、capabilities 全部与原操作相符才确认；grant/revoke 共用此规则。
  断开不重放。需要明确用户确认的调用责任保持，现有 UI 仍仅查看/撤销。
- 只在 APPLY 已提交时向页面发布公开 Target 元数据，与事务 ID 按设备保存至 Activity
  状态；重建仅查询，不重传密钥。旧状态只含 transaction、预期缺失或非法时可以回查，
  但不会把当前状态冒充原操作的目标确认。这是对恢复证据的加强，未改变设备协议。
- 新增 6 个 JVM 执行 ID；同类原 9 项全部保留，共 15 项。最早缺少 grant 接口的
  编译失败归 `BLOCKED_INTERFACE`，不是业务 Red。首次组合 Gradle 命令把 `--tests`
  放在 assembleDebug 后导致命令设置失败，保留日志；调整参数归属后测试和构建通过。
- 完整当前集合 **406 PASS** = 原 63（内含 2 项恢复复验）+ 新增累计 343；本片新增 6。
  运行器门禁另计 13 PASS。隔离 Android 副本中漏发 key、忽略目标匹配两项变异被检出，
  原源码恢复后 15 PASS；不把变异或恢复重复加进 406。测试替换远端 peer，不替换
  Controller/Session/SDC1；手机到实际 C 设备的新增 grant 纵向互通仍需后续验证。
- Debug APK 构建通过但未安装。Activity Bundle 接线经编译/源码核对，本片没有新的
  Activity instrumentation 或真实 BLE 证据。固件、原模型/“我在”、Agent pin 不变。
  S66 TLS 偶发失败仍开放；PC 配对交换、安全保存、USB owner、工作台资源/事件及实板
  门槛均未由本片关闭。没有开串口、刷板、按键或清设备数据。

逐例结果：`acceptance/s72-20260927.json`；补充证据：
`acceptance/s72-pc-grant-evidence-20260927.json`；原始日志：`out/shaniu-s72/`。

### S73 · PC 加密凭据配置与实际 DPAPI 接线（2026-09-27）

基于 S72 `ea93d693`，先写配置/客户端行为用例，再实现 `workbench_profile.py` 与
唯一 CLI 的 `save-profile` / `status|info --profile`。不是新认领流程：保存成功明确
返回 `device_authorization_verified=false`，可信配对交换仍缺。

私有文件格式 SPC1 包含精确密文长度；DPAPI 保护的 PCI1 内同时保存证书指纹、独立
PC key、证书长度及 PEM。证书≤16KiB、保护输入/输出≤32KiB；版本、长度、证书/指纹、
32字节非零 key 都校验。密文临时文件 fsync 后独占发布，旧文件不替换，再解密回读。
文件提交后检查失败保留未知结果，不谎报成功。该文件格式不是设备协议；无需改固件。

- 本片新增 11 个独立主机 ID，验证真实配置格式/文件处理/CLI/TLS客户端，只有系统
  保护边界与串口 peer 被替换。替身 AES-GCM 来自已固定的 `cryptography==44.0.0`
  （`tools/bk7258/sdk-python-requirements.txt`），未升级依赖；不是生产加密实现。
- 最早缺模块为 `BLOCKED_INTERFACE`；接模块但未接 CLI 时 3 个设置错误保留。
  当时直接导入已有 TestCase 导致 unittest 额外收集原13项，已改为模块引用和显式
  ProfileTest 选择；这些历史重复执行不计作新增覆盖。
- 实际 WSL→Windows DPAPI 先出现 3 个 setup 错误，定位为错误调用不存在的
  `Console.ReadToEnd`。固定合成输入证实系统 DPAPI 本身可用，改为
  `Console.In.ReadToEnd` 后 3 项真实 OS 检查通过：回读、上下文/篡改拒绝、解密后
  CLI→真实 TLS peer。原日志 `dpapi-live.log` 留存，修正结果另存。
- 实际 OS 检查只处理临时合成数据，通过当前 Windows 用户执行。文件操作在 WSL
  文件系统；不是另一 Windows 用户隔离实验，也不是原生 Windows Python 文件接口、
  真实 USB、BLE 或设备授权验收。操作系统语义依据官方文档，实验范围不扩大。
- 最终 **417 PASS** = 原63（含2恢复）+ 累计新增354，本片11；门禁13另计。
  实际 DPAPI 的3项单列，不混入跨平台主机必选集合。覆盖旧文件、忽略解密后信任
  校验两项隔离变异均被断言检出，恢复后11PASS。原2项卷/配置变异及恢复仍保留。
- 子进程期限10秒，运行完退出；数据不放argv/日志，拥有的明文数组使用后清除。
  未承诺整个Python/PowerShell进程内存有界于32KiB，也未声称内部副本完全擦除。
  没有 Linux/macOS vault 后端或 OS 服务时拒绝；不降级存明文。未改变固件、APK、
  模型/“我在”或 Agent；没有真实串口/刷板/安装动作，S66 TLS缺口继续保留。

结果：`acceptance/s73-20260927.json` 与
`acceptance/s73-profile-evidence-20260927.json`；原始记录：`out/shaniu-s73/`。

### S74 · 配对交接所需的可信设备证书路径（2026-09-27）

基于 S73 `b7fd2d01`，先补行为断言，再把生产 TLS 提供者中已经校验过的公开叶证书
接到共享控制会话：`ProvisionTlsChannel → ProvisionGattSession → AndroidProvisionGatt
→ DeviceControlConnection → AndroidDeviceControlFactory → DeviceControlSession`。
这是首次 PC 配对的身份数据前置路径，不是已经实现加密配对交换或新增授权 UI。

- 叶证书仅在客户端 TLS 已建立且未关闭时导出为不可变 PEM/SHA256；DER 接受范围
  1..8192 字节，拒绝尾随数据/无效结构。导出失败是缺少交接能力，不使已有手机控制
  失效。该上限不是 SSLEngine 内部内存上限；解析/编码在 GATT worker 上运行并缓存，
  没有新增线程或 I/O。缓存与共享会话引用在退出时清除。
- 生产连接只在 SDC1 AUTH 成功后发送身份事件。共享会话收到身份事件本身不会认证，
  仍待既有控制就绪结果才发布。事件受 generation 限制，重复事件不发新命令；断开、
  身份释放和旧回调不能恢复旧证书。同一代出现矛盾身份时连接失效。
  `fromDer` 是有界格式解析，不独立证明信任；信任来自现有 TLS pin/日期/握手路径。
- 新增 4 项 Session 测试；把原有 6 项 TLS 测试纳入本轮必选集合，并在真实 TLS/GATT
  测试里增加导出 DER/指纹、握手前空值与关闭清理断言。计数新增10个收集标识，不是
  新增10个测试函数。最早缺接口为 BLOCKED_INTERFACE，未冒充业务断言 Red。
- 完整集合 **427 PASS** = 原63（含2恢复）+ 新增累计364；运行器门禁另计13PASS。
  `SHANIU_ANDROID_INTEROP=1` 的原有 C/JVM 入口另取得232项JVM通过，无跳过；其中
  真实C mbedTLS→JVM生产TLS/GATT的证书与对端原始DER一致，错误pin仍拒绝。
  232与427有重叠，不相加作为独立覆盖率。原20个TLS样本和PC互通子场景也保留。
- 两项隔离变异分别移除旧代过滤、保留退出缓存，均被业务断言检出；恢复后定向
  24项通过。变异与恢复不重复加入427。当前源码哈希另核对，主工作树未接受变异。
- 模拟器安装本轮debug APK/测试APK，用Android实际TLS提供者检查导出身份及关闭
  清理、原pin拒绝与分片。首次instrumentation自身输出PASS，但外层采集脚本误要求
  普通输出含原始结束码；保留输出，使用`am instrument -r -w`核验原始结果-1后通过，
  未改业务断言。两次运行同一复合检查，不当作两组独立用例。仅使用临时合成证书/
  独有测试Keystore与偏好；Mi10在线但未安装或操作，没有真实BLE/USB/板操作。
- Android定向构建通过；固件、模型/“我在”、Agent pin不变。配对请求/加密响应、
  手机确认与PC导入的完整串联仍缺，S66偶发TLS失败仍未定位，不能关闭实板/发布门槛。

逐例清单：`acceptance/s74-20260927.json`；跨层与模拟器证据：
`acceptance/s74-peer-identity-evidence-20260927.json`；原始日志：`out/shaniu-s74/`。

### S75 · 离线 PC 配对加密交换（2026-09-27）

先建立缺接口测试，再实现 SPQ1/SPR1 编解码和唯一 CLI 的 `pair-start` / `pair-finish`。
实际请求生成、保护文件读回、响应解密和 SPC1 保存均走生产模块；只在一般单测中替换
外部 OS 保护。独立向量检查请求/权限/设备pin绑定、非法长度、有效期、空key、输出
不覆盖，以及保护回读失败不得发布请求。具体 wire/时间合同见 acceptance/contracts.md。

本轮新增 7 Python + 5 JVM 必选ID。完整439收集：438 PASS、1 FAIL_ASSERTION，
0 SETUP_ERROR/NOT_RUN；原63/63通过。失败是既有 USB-01.tls-transport 在恢复握手
`test_provision_tls.c:981` 的断言，保留非零退出及合成证书/日志。不是新配对协议已
导致或修复了该问题的因果证据。固定失败证书150次有界复放通过，不取代原失败，
S66继续开放。准备复放时截获过宽导致另一identity夹具缺失，记录SETUP_ERROR后
改为运行已构建测试程序，不把该设置错误算产品断言失败。

单列证据（不累加到439）：13运行器门禁；真实Windows CurrentUser DPAPI回归3；
真实DPAPI→Python请求→生产Kotlin响应→Python导入→真实TLS客户端/外部对端鉴权1；
2隔离变异分别移除请求绑定/权限绑定被检出，原生产代码恢复7测试通过。Android
Debug/androidTest构建及仅emulator-5554安装通过，原生Android提供方完成SHA256+
MGF1-SHA256+非空label加密/解密。未安装Mi10、未访问板子/原生USB、未修改Agent。

可复跑入口（互通环境变量选择的是额外场景，不是假定默认执行了它）：

```sh
python3 tests/host/bk7258/test_workbench_pairing.py
SHANIU_PAIR_INTEROP_OUT=out/pairing-interop \
  python3 tests/host/bk7258/test_workbench_pairing_interop.py
python3 tests/host/bk7258/test_workbench_profile.py --dpapi
```

互通脚本缺真实Windows DPAPI或Gradle依赖时必须非零，不能视为业务失败或静默skip。
普通Kotlin用例仅验证公开证书解析；互通也使用合成外部TLS对端。手机侧确认、grant
持久回执后导出尚未接通。父需求接口/L1L2/L3缺口继续分层，不以codec通过标完成。

逐例：`acceptance/s75-20260927.json`；补充：
`acceptance/s75-pairing-evidence-20260927.json`；原始日志：`out/shaniu-s75/`。
