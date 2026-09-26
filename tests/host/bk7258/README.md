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
