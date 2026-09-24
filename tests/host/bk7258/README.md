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
