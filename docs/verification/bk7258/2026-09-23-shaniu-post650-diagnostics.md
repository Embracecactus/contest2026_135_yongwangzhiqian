# 650 后续诊断：Q01 入口与问题矩阵

## 2026-09-24：661 与 App v40 用户验证后提交

以下为本轮交付后的用户反馈及已执行检查，不替代历史记录，也不代表所有
网络、存储或异常复位问题均已解决。此次提交不重新烧录、格式化或清除 App 数据。

- 用户手动全量下载 661 后，报告认证后眼睛显示正常；日志中偏好恢复返回 0，
  唤醒阈值 85 保存为 `saved=1 result=0`。切换热点无需重新输入 API Key，
  相机采集返回 0，视觉工具及终答 SSE 均成功。未由此证明 SD 长期完整性。
- 用户报告热点播放仍有停顿，但停顿前后的语音内容连续。示例 TTS 请求耗时
  10,801 ms，生成 61,440 字节 16 kHz 单声道 PCM，仅约 1.92 秒音频，最长
  单次 read 为 5,184 ms。云端或传输停顿仍待区分；不宣称热点卡顿全部修复。
- App v40 将蓝牙设备候选放在各页共用的连接入口，修复设置页扫描后没有
  可选设备的显示缺口，不替换共享 BLE 会话。用户在收到 APK 后确认
  “安装验证成功”；没有附新的逐步 UI 或 GATT 回读记录，验收范围以该反馈为限。
- App 版本为 `0.7.10-shaniu-companion`（code 40），APK 18,013,232 字节，
  SHA256 `c561ca5bc65006cb62e88d4fbb544942c72289b7ff7882f22bc64f403e954905`。
  签名与 v39 相同；162 项单元测试通过、2 项跳过，debug APK 与 instrumentation
  APK 构建通过。页面候选回归已编译，本轮没有执行 instrumentation 真机测试。
- 661 的 CP/AP 增量构建、包校验及既有五项音频/云请求 host 目标通过。
  Agent 源码与保存的 `agent-661.patch` 可逆核对一致，现固定为提交
  `62a304ea69c4076f0f3ef7955a9af69a5ed35277`。构建本身仍按原清单标注 dirty
  输入；提交不会将历史产物改称干净提交构建，也不自动发布 Agent 远程版本。
- 提交前层级检查通过，`git diff --check` 通过。NuttX nxstyle 对既有模块
  布局及部分改动的注释/短语句格式仍报错，未记为通过；不在用户验证之后
  大面积重排代码。未执行跨板冷构建，本轮不是所有板型的最终源码验收。

## 653 首轮问答失败与 654 修复候选

`boot-653-capture-start` 的 COM13 被动采集于 21:11:45 至 21:21:45 完成，
1,536,071 bytes，SHA256 `424e333ab614e11a6de4f3626b672b0a3d6b2fbe6765c15c1b0db60ddf12652e`。
没有发送命令或复位。653 已完成云配置（revision=3）、16kHz/mono 采集、
一次唤醒/录音及 ASR 成功，不能继续把 652 启动格式错误当作当前唯一故障。
但 request=1 的终答 SSE 返回 `status=200 result=-71 text_bytes=0`，
之后恢复采集时 pcm0c 约每 140ms 打开/关闭，用户报告 App 再次断连。
此段没有新 boot/Fault，不能写成再次重启，也尚不能宣称 BLE 根因已经实板确认。

654 的两项窄修复：

- Agent 终答解析接受可选字段的 JSON null，仍拒绝非 null 工具内容、
  非 assistant 角色及不完整终止。依据 MiMo 官方
  [SSE 示例](https://platform.xiaomimimo.com/docs/en-US/usage-guide/passing-back-reasoning_content)：
  `tool_calls`、`role` 等允许 null；旧解析器会在此形状返回 EPROTO。
  该协议缺陷已由离线分片用例复现覆盖，但未获取本轮私人响应正文，
  因此 653 单次 -71 的归因仍需部署后确认。
- 已协商的固定 16kHz/mono 路由，在再次提交 Recorder link 前先恢复 start；
  首次启动仍等待 STARTED 并排队执行硬件 link，保留 653 的冷启动修复。
  这是针对官方 Media `audio_graph_run_all` 在 stopped 输入上反复 EOF、
  延迟处理后续控制队列的产品侧时序修复，不修改官方 Media/FFmpeg。
  warm prepare 仅允许产品固定格式，失败/超时仍由同一 owner 清理路由。

既有 `run-agent-capture` 15 次打开/关闭检查、`run-agent-final-stream` 通过；
前者覆盖 cold/warm 顺序及失败/超时释放，后者覆盖 null、逐字节 UTF-8、
工具/思考隔离、取消及结束边界。检查日志为 `voice-recovery-fix-host.log`。
654 CP/AP 增量构建、签名工厂包生成通过；不是冷构建或实板通过。
源码为主仓 `65410f5b`、Agent `938b66bd` 加工作树修复，未提交/推送；
源码差异和 ELF/map 随 `factory-654-voice-recovery/evidence/` 保存。
候选 `0.7.9+654` / counter 654，factory BIN 8,388,608 bytes，SHA256
`da9d98e41c6f5a763ea3c7173d28b6d031e7ecc1ca4d98593bf70f972fec700f`。
沿用相同开发签名、布局及经核验的同板硬件输入；factory BIN 会清用户配置，
不能作为保留当前认领的普通 OTA。本次修复轮未部署、未复位、未清数据，
板上仍是 653，App 仍为 v37。Q02/Q04/Q08 标记为现场失败后修复候选待测；
其余问题不据此销项。

## 652 现场失败与 653 定向修复（2026-09-23 21:08）

652 认领、Wi-Fi 和云配置完成后出现真实板端异常重启，并非仅 App 显示断连。
`claim-reconnect-652-manual-02/serial.raw` 的 600 秒被动采集有 25 条 `HF E=`，
本轮采集没有发送 reset 或配置命令。云配置先报告 `ready=1`、TLS 成功、revision=3；
随后单一采集启动，出现 `sample_rate=0`、实际 `sr:48000 ch:2` 与请求
16kHz/mono 不一致、格式/采样率错误洪泛，再次启动。
`E=2`、HFSR/CFSR=0，精确 CP ELF 将 PC `02012fd2` 定位为 `arm_lowputc`；
这不是证明 K2 HardFault 再现，也不能把最终打印位置当作首个故障源。

根因链已收敛到新采集 owner 在 Recorder 格式 link 之前应用硬件 start。
Media 的 start RPC 只确认入队；STARTED 也只保证格式 link 已入图队列。
653 因此等待有界 STARTED 结果，再由板级策略使用同队列的 `pcm0c,link`，
确保硬件激活排在格式协商之后，不以固定延时规避竞态。
超时/错误不激活路由，关闭并确认通知线程退出后才释放 callback 对象。
既有主机检查扩展了延迟回执、失败、超时、晚回调和下一次打开，12 次打开/关闭通过。

653 为当前主仓 `65410f5b`、Agent `938b66bd` 加窄修复工作树的硬件迭代候选，
不是已发布提交。CP/AP 构建及签名包校验通过；完整源码差异、ELF/map 和构建清单
保存在 `out/shaniu-voice-20260923/factory-653-capture-start/evidence/`。
版本 `0.7.8+653`，计数/编译 floor 653，同板工厂 BIN 8,388,608 bytes，
SHA256 `526b5bf7bd073b0ba0b3a36e6e607e34838f49801e18b7a293eee0a8b0ff250d`。
用户明确要求快速修复后全量下载；本次会重新初始化指定板的用户状态。
652 标为故障诊断产物；653 下载、启动和云配置后实测分别判定，不以编译通过销项。
653 本次全量下载于 21:11:44 完成，五个成功标记齐全、无失败标记，
Loader 原始退出码 1 按该版本既有规则单独保留。21:11:45 后 COM13 被动采集
已就绪，目录 `boot-653-capture-start`，限时 600 秒；已提示用户手动扫码配置。
本次未安装 v38，手机仍为 v37；不能把固件修复等同于 App 身份交接候选已验收。
Q02/Q03/Q04/Q06 的本轮语音验收先受此真实启动采集回归阻断；不是“缺现场”。

## 当前现场条件覆盖（2026-09-23 晚间）

用户已在电脑旁，当前为 Agent 执行、用户配合按键/屏幕扫码/真人语音验收。
下方“缺现场”仅保留为历史记录，不再作为当前阻塞；未恢复固定模型分工。
本轮开始源码为 `65410f5b07ba34d85b2c9ee7cd18f25707f4d0f2`，Agent 为
`938b66bd38bcf6111236ae1c3b9db520e1d3c3dc`，两工作树初始干净。

652 CP/AP 实际编译源码仍为 `ae8f36bdf3e5531b7723bce410b68454f1a2b763`。
两提交间仅主机训练/评估、既有测试及文档变化，目标运行时和 App 未变；
构建清单复验通过，复用目标构建，不能称重新构建或已经实板通过。
已用本地持久开发身份、经核验的同板硬件输入生成 0.7.7+652 工厂镜像；
本轮工厂初始化与后续产品撤销验收分别记录。

测试手机由 v36 保留数据覆盖安装至 v37 / 0.7.7-shaniu-companion，
新旧证书一致，安装返回 Success，实际版本回读一致。原认领保留，
点击原设备候选后重新认证、Wi-Fi 状态回读成功。下载前 App 读取板端仍为
0.7.6 / build 651 / counter 651；更新 APK 不能当作更新固件。
COM13 被动采集完成；随后只读 bkwifi/usbmode 查询确认 link=3、mode=cdc。

| 问题 | 本轮状态与下一证据 |
| --- | --- |
| Q01 | 872 中断保护候选包含在 652，尚未现场复测；基本闭环后只做一次定向 K2 测试 |
| Q02/Q03 | 生产语音流水候选已实现，652 的真人普通问答/视觉最终回答尚未测量 |
| Q04 | PCM 队列保留，实际 Media EOF 问题未销项；长回答及结束后恢复待测 |
| Q05 | 研究候选未达推广门槛，保持稳定默认；先做现场同一人六组回归 |
| Q06 | 本地人声应答候选包含在构建，实际听感与默认生效待用户反馈 |
| Q07 | 撤销/转交真实破坏性验收安排最后，不借 K2 或清 App 数据代替 |
| Q08/Q09 | v37 真机覆盖安装及旧绑定重认证已通过；工厂新认领、配置及后续完整交互待测 |
| Q10 | 652 同板工厂产物/匹配 APK 已核验；下载、启动、功能分别记录，不能以打包 PASS 代替启动 |

本地证据：`out/shaniu-voice-20260923/onsite-baseline-65410f5b/ARTIFACTS.md`、
`v37-authenticated.png`、`onsite-preflash-status/`、`factory-652-onsite/release.json`。
第一项路径内含本轮准确产物大小、SHA256、ELF/map 与签名对应，不包含密钥。

## 以下为此前各阶段记录

### 652 首用现场新增：认领后重连失败（20:40）

一次 652 工厂下载标记通过；用户确认圆屏二维码出现、手动认领成功。
手机认领日志 `protocol_state=COMMITTED`，随后首页长期重连，截图显示
673 秒前的缓存。用户执行一次重启并确认眼睛恢复，连接问题尚未验证解决。
COM13 已捕获新启动；有 HCI 0x2006/0x200a rejected，不据此独断根因。

源码确认 App 存在身份交接缺口：startProvisioning 仅 disconnect(user=false)，
保留旧 AndroidDeviceControlFactory 捕获的 deviceId；onStart 在重新读取绑定前
恢复该工厂。认领更新身份后可能持续在凭据构造阶段失败，尚未创建新的 GATT。
本轮过滤手机日志未见新的 GATT 记录，与此路径相容，但仍需人工重入对照，
不将源码缺口直接写成已证明的唯一现场原因。

修复候选 v38：新增 releaseIdentity 取消旧重连入口、递增回调代次并清除缓存；
进入认领及接收完成结果时调用，不清任何持久绑定。扩展现有 session 测试验证
前后台返回及迟到回调不能重开/污染旧身份，随后显式选择新设备可正常认证。
assembleDebug 与 testDebugUnitTest 完成，160 项中 158 通过、2 跳过、0 失败；
v38 APK SHA256 `e9e6f7b959b95bafd264659f8f54248bcd34f81788328b51c394fd5581f38bb1`，
18011472 字节，证书与已安装 v37 一致。尚未安装 v38、尚未真机验证该修复。
用户要求手机全程手动操作，本阶段仅采集串口及只读手机日志，未再次复位或清数据。

日志：`out/shaniu-voice-20260923/claim-reconnect-652-onsite/`；
候选：`out/shaniu-voice-20260923/shaniu-companion-v38-claim-handoff.apk`。

本记录基于 `2460d51de17a72669e6497e5e8ff3af7b9f8c139` 加本次工作树修改，
Agent 仍为 `280cbbd592e60783e6ea205aafa3799a44296d9b`。这是诊断增量的
构建证据，不是新的 K2 修复验收，不替代 650 人工验证记录。

## Q01：已取得与尚缺的证据

原始 CP ELF 已按 650 清单核验并固定到本地
`out/shaniu-repair-20260923/q01-650-frozen/cp.elf`；SHA256：
`cdc6b7c99696f313252f33cc3f156fb5deb3ad5452ad4f3d395d8695f78fdedd`。
map 和原内存报告一并保留，后续地址不能反用于解释旧固件。

两次历史故障 `EXC_RETURN=ffffffed`、`CFSR=00020000`、
`HFSR=40000000`，原始 SP 为 `28044398`，输出的八个核心栈帧字全零。
原来的 `V=1` 仅检查地址范围、对齐和 stacking fault，不能证明这些字
就是有效现场。不能据此断言实际执行了 PC=0，或已经定位某条 SDK 指令。

对应 ELF 反汇编确认 SDK 的 super-deep 路径通过
`sys_drv_enter_deep_sleep` 调用 SRAM HAL，最后跳转 SRAM WFI 包装。
旧诊断入口本身位于 Flash。仅验证两个睡眠符号在 RAM 不能解释上述故障。

本次改变：

- CP 自有 `0x80` 字节故障槽中的记录从 80 扩为 116 字节，版本为 2；
  保留邻接 CPU2 记录边界断言，不改变分区、AP RAM 或堆边界。
- SRAM 裸入口无压栈、无动态分配、无锁，先记录原始 MSP/PSP、CONTROL、
  PRIMASK、BASEPRI、FAULTMASK、VTOR、FPCCR、EXC_RETURN、HFSR/CFSR。
  入口记录 `reserved=1`，C 路径读取基本帧后才记为 2；后者仍不等于帧语义有效。
- 初始 Flash 向量保留早期故障入口，只有 SRAM 复制及 RAM 向量初始化完成后
  才安装 SRAM 入口，避免在复制前跳入未初始化 RAM。
- 睡眠阶段 0/1/2/3/4/5/6 分别表示未进入、硬件准备、唤醒配置、SDK 入口、
  WFI 边界、WFI 返回、SDK 返回。实时阶段与故障快照分离，普通启动不擦掉
  上次异常保存的阶段。阶段只用于诊断，不授权或改变电源转换。
- 新增 `HX` 固定字段输出。后续 C 日志和原有复位路径仍依赖 Flash；
  **没有声称整个异常处理器已经 XIP-independent**。若 Flash 不可访问，
  SRAM 入口保存的记录与 UART 输出成功是两个不同证据层级。

本次最终 AIDK CP ELF SHA256：
`27dcad858838e0b693cc151445f6ddc964a78365120ea628ae9ab588fe50f705`。
Flash 启动入口 `0201148e`，SRAM 入口 `28010940`，复制范围
`28010940..28016d7c`。锁定 GCC 10.3-2021.10 反汇编核对入口及 literal pool。

| 字节 | 原 650 | 本次诊断增量 |
| --- | ---: | ---: |
| copied data（含选定可执行段） | 25404 | 25660 |
| BSS | 160544 | 160544 |
| 中断栈 / 启动栈 | 2048 / 2048 | 2048 / 2048 |
| 初始堆，未扣分配器开销 | 52380 | 52124 |

以上不是运行时最大连续块或高水位测量。没有恢复 649 的整 PM 对象搬移，
保留 PSRAM/BT 初始化顺序、链接依赖跟踪和内存报告。新增符号位置门槛及
原有测试内的两个负例，分别拒绝缺失 WFI reset 和缺失 SRAM fault entry。

## 执行记录

公共 CLI 实际增量构建，CP/AP 目标链接和相关启动组件均退出 0：

```sh
python3 tools/bk7258/bk7258.py build --board aidk_ai_toy --boot mcuboot --development-identity --rollback-floor 650 --jobs 4
python3 tools/bk7258/bk7258.py build --board t5_board --boot mcuboot --development-identity --rollback-floor 649 --jobs 4
python3 -m unittest discover -s tests/host/bk7258 -p test_bk7258_build_workspace.py
```

两个 floor 沿用各自现存构建输入，不是新的发行计数，不授权安装或降级。
日志在本地 `out/shaniu-repair-20260923/q01-fault-stage-{aidk,t5}-build.log`。
单元测试 9 项通过，`git diff --check` 通过。未新增测试文件。
`nxstyle` 全文件检查仍报告既有路径头、混合命名及历史格式问题，不能报告全绿。
本阶段未签名打包、未运行最终提交 Actions，也未操作串口、刷写、关机或恢复出厂。

## 不可遗漏的问题矩阵（650 后诊断阶段历史快照）

| 编号 | 当前状态 / 已有证据 | 剩余实现或确认 | 验收方法与通过条件 |
| --- | --- | --- | --- |
| Q01 | K2 实板失败；本次诊断构建通过 | 取得有效故障现场并修复根因；启动预算仍需运行证据 | 受控复测新增 HX/阶段；最终 10 轮关开机无 fault/立即重启，配置保持，USB/电池分开 |
| Q02 | 普通无工具首 Media 21.630750 秒；仍全正文后 TTS | 真实有效正文增量主链、请求时间轴、ASR 等待分析 | 保留工具能力与历史，验证首段在正文完成前播放；同条件延迟/失败率，声学与代理指标分开 |
| Q03 | 650 一轮视觉最终回答成功，648 最终请求超时未销项 | 最终回答 deadline、取消、资源释放审查 | 新图到最终 TTS/完成/再唤醒全链，失败后不忙锁、不重拍循环 |
| Q04 | 三轮 rebuffer=0；EOF 重复错误仍在 | EOF、录音尾边界和取消生命周期；不另建播放器 | 完整、欠载、取消、网络错误分别回归；自然语速，不以静音/整段下载伪装连续 |
| Q05 | 24 epoch 候选未优于默认，未替换 | 独立来源泛化与量化后的板端预算；更新/默认恢复回归 | 分组隔离评价召回/误唤醒/语速/噪声；达到门槛才发布合格候选 |
| Q06 | 公开“我在”资源已入构建，真人未单独确认 | 核对实际选择及失败兜底 | 实听人声与提示音分别留证，不计入答案首句提速 |
| Q07 | 认证撤销及持久回执代码、主机/模拟验证已有 | 多副本与中断边界审查；独立物理恢复缺口 | 模拟 ACK 丢失/重启/旧 worker；现场旧凭据失效、新 QR、TLS 保留，不用 K2 五秒替代 |
| Q08 | 配网/云配置人工成功，历史断网和灰显不能一并销项 | 重连、草稿/事务所有权、离线配置完整回归 | 热点失效恢复、重启回读、认证 BLE 离线管理；无并行重连和永久 busy |
| Q09 | v35 模拟器截图/导航已有 | 本轮实际复验并收敛发现、权限和错误状态 | 360/412dp、深浅色/大字体、键盘/重建、20 轮导航，实际截图/录屏；模拟明确标识 |
| Q10 | b5c831c9 冷构建与旧产物存在 | 本轮最终源码对应的构建/签名/包、APK、资源及准确 Actions | 哈希/协议/布局/信任一致；独立下载复验，OTA 传输与安装确认分层，不能用旧绿灯 |

Q01 根因缺现场仅限制该项的下一次硬件观测；不阻断其他软件工作。
本记录不将任何尚未实现的代码写成“只差现场”。

## Q08/Q09：Mi 10 保留数据升级与音量回读（本轮新增）

源码基于 `5e27a0c060836880be77f86ab35f56f5bd508ca8`，加本节对应
Android 文案、既有测试断言及 versionCode 36 修改。先构建并安装当前
提交的 v35，再安装修复后的 v36；两次均为 `install -r`，返回 Success。
没有卸载、清数据、重新扫码或生成 owner。两版启动均保留认领资料，
选择原设备后完成认证。手机系统 Android 13，实际设备为 Mi 10。

当前已安装 `com.shaniu.companion`，`0.7.6-shaniu-companion` / 36；
APK 18008732 字节，SHA256
`5593aec1bf9f8eb4637104956ab788542c83a6c68808217fd0082a8a78acb3e4`。
手机实际安装包重新提取后的 SHA256 与构建产物相同。安装证书 SHA256
`b2ea591116f967af230d0f06f678cc5fdd99d5884cc429b8d22c6489ba96050b`，
与手机原 v34 相同，属于已有本地签名，不是 CI 临时签名。

- v34 待机音量入口本轮实际可用；原可用态文案仍显示“收音或播报时暂不可调整”，
  容易被理解为当前不可操作。v36 改为“可调节”，不改变认证、新鲜度、busy、
  写入待确认等安全门槛。既有测试保留忙碌禁用断言，新增可用态文案断言。
- 音量原值100%。仅做一次降低：App 提交95，设备档位回读93%，关闭重开仍93%；
  随后恢复原100%，再次设备回读确认。原值本来就是100%，没有超出原值增大音量。
  COM13在02:46:39记录MusicVolumeDomain index-14，02:47:17恢复full，与手机结果一致。
  未重启验证持久化，未验证忙碌过程及声学响度。
- 20轮设备→定制→更新→设置导航完成，进程保持，最终首页认证与状态刷新正常。
  对应App进程日志仅见起始一次GATT连接，无重复连接或FATAL异常。
  未直接观测订阅内部计数，不将此窗口扩展为所有生命周期通过。
- Wi-Fi与云配置页完成设备只读回读，读完后输入框及固定保存按钮解锁；
  设备报告Wi-Fi就绪。未提交网络、模型或密钥，没有默认值覆盖操作。
  扫描启动已操作，首次观察窗口仍为扫描中，之后取消；不能标扫描结果通过。
- 前后台返回使用同一App进程；完整Activity重建、草稿/键盘等回归仍需继续。
- 更新页实际回读板端0.7.5、build650、security counter650。
  手机升级不等于固件更新；布局、信任指纹、恢复条件仍未充分核实，未执行OTA。
- 电脑默认路由走Realtek有线网卡；尚未核实全部热点恢复参数，未切换热点。

构建命令为既有 `./gradlew :app:assembleDebug :app:testDebugUnitTest --console=plain`，
v36 构建及测试退出0。目标包位于本地
`out/shaniu-repair-20260923/mi10-current/shaniu-v36.apk`（工作区根相对路径）。
证据同目录含 v36-build.log、v36-connected.png、v36-update.png、
v36-logcat-navigation.txt、v36-logcat-final.txt、v36-control-serial.raw/json。
300秒被动串口采集7833字节并正常释放；没有串口命令、复位、关机或刷写。
真机导航录屏 `C:\Users\lijian\shaniu-ui-evidence\shaniu-v36-mi10-navigation.mp4`，
89.7925秒，8158513字节；已查看实际截图及录屏抽帧，不是静态原型。
原始日志/画面仅本地保存，不随源码公开。

## 本轮语音专项：研究基线与冻结目标（2026-09-23）

本节是后650诊断的语音专项增量，不改写上方 Q01–Q10 的历史证据或状态，
不表示已有固件、模型或实板通过。专项基线为 root `87254121`、Agent
`280cbbd592e60783e6ea205aafa3799a44296d9b`。Q01 的 651 复现 stage3 HF；
872 中断保护构建通过，仍待实测。Q02–Q10 继续按上方矩阵跟踪，不销项。

### 选型判断

| 路线 | 研究定位 | 当前决策 |
| --- | --- | --- |
| A：v1/default | 现有默认候选与冻结基准 | 保留为对照，不先替换 |
| B：stateful PCAN + 原轻量结构 | 持久化前端状态，沿用分类器结构但重新训练权重 | 主候选；与 v1 的同预算重训对照，禁止旧权重配新前端 |
| C：增量流式 | 每个音频小块更新特征/模型状态并尽早给出结果 | 并行方法候选；需同时核算状态内存、延迟和误触发 |

研究参考显示，micro-wake-word 的流式 MixConv 只消费最新特征切片，TFLite
Micro 前端使用 PCAN；PCEN 则用逐频带 EMA 状态
`M_t=(1-s)M_{t-1}+sE_t` 做动态归一化与压缩，和 PCAN 不是同一前端，不能
直接互换。micro-wake-word 当前核对固定提交
`4665173cd35f1cff9a61e06fc427f124766c488e`，仓库 LICENSE 为 Apache-2.0；其
16 kHz/40 维特征与量化模型可作实现参考，但没有 BK7258 直接兼容或实板验证
证据。TFLM PCAN 算子也不等同于板端已移植。78/xiaozhi-esp32 的单
AudioService、采集/编码/解码/播放队列和 JSON+Opus WebSocket 可作所有权与
协议参考，不视作 BK7258 代码移植结论。

服务器路径方面，FunASR 在线 WebSocket 是先发 JSON 配置，再发送裸 PCM 二进制
帧；文档列 8/16 kHz，服务端按单声道 16-bit 样本计时，句尾以
`is_speaking=false` 收尾，2-pass 提供在线和离线修正结果。未见应用层取消
消息；断开 WebSocket 不保证已执行推理被取消。MiMo-V2.5-ASR 接收一段完整
Base64 WAV/MP3，`stream=true` 只流式返回识别响应；MiMo TTS 的流式输出是
PCM16 音频，仍由完整文本请求触发。这些接口可作云端能力参考，不等于板端
流式音频上传或本地 ASR/TTS。

主要一手资料与许可证：

- [78/xiaozhi-esp32 AudioService](https://github.com/78/xiaozhi-esp32/blob/main/main/audio/audio_service.cc)、[WebSocket 协议](https://github.com/78/xiaozhi-esp32/blob/main/docs/websocket.md)、[MIT LICENSE](https://github.com/78/xiaozhi-esp32/blob/main/LICENSE)。
- [Espressif esp-sr AFE 指南](https://github.com/espressif/esp-sr/blob/master/docs/en/audio_front_end/README.rst)、[AFE 配置接口](https://github.com/espressif/esp-sr/blob/master/include/esp32/esp_afe_config.h)、[MIT LICENSE](https://github.com/espressif/esp-sr/blob/master/LICENSE)；上游定位 ESP SoC/ESP-IDF，未核实 BK7258 兼容。
- [micro-wake-word 固定提交 README](https://github.com/OHF-Voice/micro-wake-word/blob/4665173cd35f1cff9a61e06fc427f124766c488e/README.md)、[Apache-2.0 LICENSE](https://github.com/OHF-Voice/micro-wake-word/blob/4665173cd35f1cff9a61e06fc427f124766c488e/LICENSE)、[TFLM PCAN kernel](https://github.com/tensorflow/tflite-micro/blob/main/signal/micro/kernels/pcan.cc)。
- Wang et al., [Trainable Frontend for Robust and Far-Field Keyword Spotting](https://doi.org/10.1109/ICASSP.2017.7953242)；[Google kws_streaming README](https://github.com/google-research/google-research/blob/master/kws_streaming/README.md)。
- [FunASR WebSocket 协议](https://github.com/modelscope/FunASR/blob/main/runtime/docs/websocket_protocol_zh.md)、[服务端源码](https://github.com/modelscope/FunASR/blob/main/runtime/python/websocket/funasr_wss_server.py)。
- [MiMo-V2.5-ASR API](https://mimo.mi.com/docs/en-US/api/audio/Speech-Recognition)、[MiMo-V2.5-TTS 文档](https://mimo.mi.com/docs/en-US/quick-start/usage-guide/audio/speech-synthesis-v2.5)。

### 冻结评价目标（目标，非成绩）

- A/B/C 使用同一数据与评价协议；A→B 控制前端变量，B→C 控制模型结构变量。
  按来源分组后隔离 train/validation/test，
  同一录音或合成来源的切片、增强和派生版本必须留在同一 split。
- 唤醒召回目标：总体至少 95%，关键分组各至少 90%；词尾到唤醒决策延迟
  p95 不超过 500 ms。误唤醒目标不高于 0.5 次/小时，并争取至少 10 小时连续背景评估。
- 普通问答做 30 轮同条件基线与候选对照；目标是 p50 降低 30%，达到 3 秒，
  p95 达到 5 秒。记录首个有效正文、首个 PCM/Media 与最终完成时间；不得把
  流式中间推测文本计作有效答案。以上均待实施与测量，不是本轮成绩。

### 前端对照预注册（运行前）

- 原默认 SHA `922eba9175fcda60f7c8a4505ca4eb5a97c86ceb30fbe48c685fd612098ac910`
  不替换；A-r 为同结构重训控制，B 为 v2 重训，不将 A-r 冒充旧默认。
- A-r/B 同用已审计 1302 项语料，逻辑清单哈希
  `4a580d0e97babb91f24b495dd87c45a0f23734c1ec2a52df13eeb837ded79a77`；
  DS-CNN 32 通道、首层频率步幅 4、seed 20260923、batch 32、最多 24 epoch，
  每轮最佳 val_loss 权重保留 checkpoint，既有 patience 20，单运行上限 2 小时。
  CPU TensorFlow 2.15.1，最多 4 个 intra-op 线程、1 个 inter-op 线程，不占 GPU。
- 二者同用 400 ms unknown shifts、1 秒训练负例合成预热且交替冷启动；本轮
  不叠加 tempo/room/gain，避免把多个增强收益混入前端差异。验证/测试独立短片
  仍冷启动，连续会话按实际前置音频推进状态。合成预热不声称真实录音历史。
- 唯一 A-r/B 变量为 v1 与有状态 v2（PCAN + 温和频带噪声抑制）的前端契约。
  保留全部失败、浮点/INT8混淆矩阵和流式命中；已有验证/测试曾用于调参，
  仅称回归集合，不称新独立真人测试。离线门槛未达不得替换默认或发布可安装包。
- C 的模型结构与端侧状态预算尚待实现、冻结；不能将 B 写成增量模型已完成。

### 模型路由记录

| task_id | model_requested | model_verified | reasoning | usage | result | evidence |
| --- | --- | --- | --- | --- | --- | --- |
| `voice_source_research` | `gpt-6-luna` | `GPT-6`；具体 Luna 变体未核实 | medium（请求值；实际档位未核实） | 未知 | 已完成只读上游来源核验；未改固件、模型或硬件 | 本文上方列出一手资料 URL 与 micro-wake-word 固定 SHA |
| `pipeline_audit` | `gpt-6-sol` | 待核实 | high（请求值；实际档位未核实） | 未知 | 审计结果沿用其独立记录；本表不将其写作已通过 | task 记录待核实；root 当前模型不可切换，本地后续默认 Sol 配置已变化 |

### 本轮新增证据（主机，尚未部署）

- A-r/B 均完成 24 epoch 并保留最佳权重，INT8 SHA 分别为
  `1bd45137a3dbe8eac7a990d7f0c60a15c7dfbdde1410412efbc1e6c6d20f334a`、
  `f28f352f91c4de14788bc025976f85b98b6319a31c13967341a824ace87047f1`。
  固定 0.60/连续 2 次策略；连续验证会话 A-r 18/18、B 17/18；测试会话
  两者 20/20，但 B 额外误触发 1 次，A-r 为 0，均无重复触发。
  验证/测试分别仅 243/369 秒且是历史回归集，不作长期 FAR 或真人泛化承诺。
  **B 尚不优于控制，保留研究身份，不替换默认。**
- Q02/Q03：同一 Media recorder 的 KWS→会话 handoff、400 ms 前滚、2 秒
  有界队列及溢出/取消已通过现有 host peer；一次交接只 open/start 一次。
  单样本时间轴代表 Media 实际交付样本，底层硬件丢样仍不可观测。
- Q04/Q05：正文流式新增明确 `agent_finalize` 规划→终答边界，终答 SSE
  才进入有界文本队列及逐句 TTS；保留正式工具、安全与历史路径。会增加
  规划请求，必须计入延迟/费用，尚无云端加速成绩。UTF-8 分片、思考/工具
  内容拒绝、取消、尾句及首 PCM 早于 END 的既有 peer 检查通过。
- 实时 ASR 实现 FunASR PCM WebSocket 后端、固定长度发送队列及 finalize，
  产品通过显式 `asr_backend=funasr` 和 `asr_stream_host/port/path` 选择。
  复用已安装 CA 与可信时间，独立 TLS 且不发送 LLM Key，不默认切换服务。
  当前仅协议 peer 通过；官方服务源两次 TLS 下载失败，未得到真实识别结果。
- 端点候选为按样本推进的 20 ms 能量/噪声底判据，默认关闭实验开关；保留
  900 ms 尾静音。低能量开头不再直接丢弃，语音确认后一次性提交最多 400 ms
  前滚。合成信号检查覆盖分块不变性、轻声、句中 600 ms 停顿和 DC；不是 CER。
- 实际官方 Media EOF/resume 问题仍在其服务端，尚未修复；不能以本轮
  单播放器/一次 drain 的 peer 结果代替真实 Media 修复证据。
- 本轮 Mi 10 必要复核时 adb 列表为空；未反复扫描、未操作 COM13、未刷写、
  未触发 K2、未清 owner，尚无新板端/声学验收证据。

本地证据根：工作区 `out/shaniu-voice-20260923/`，含 `frontend-*-{validation,test}.json`、
训练日志/最佳权重/量化元数据、`final-stream-peer` 和 `endpoint-peer`；原始材料不公开。
额外参考 [sherpa-onnx KWS](https://k2-fsa.github.io/sherpa/onnx/kws/index.html)
仅作为电脑侧关键词/流式方法对照，未将 ONNX Runtime 或其模型当作 BK7258
可部署 TFLM 权重。新增背景候选来源为 [MUSAN / CC BY 4.0](https://www.openslr.org/17/)，
下载及来源去重完成前不计入评价时长。

### 增量结构及协议审查追加证据

- C 为 32 通道因果 TCN、249 帧上下文（4.98 秒特征步长，4.99 秒 PCM 覆盖），
  6 输入/6 输出全 INT8，状态 7936 字节。原生 TFLM 结构探针实际运行 333 帧，
  arena 21824 字节，桌面/原生最大误差 1 LSB，reset 逐值相同；不是板端耗时。
- C 正式训练固定 24 epoch、seed 20260923、批量 32、v2 前端、2 秒训练负例
  合成前史，模型 SHA256 `b6b13dfef42530834a9568c4d41716331e7f6b15024f8a508f2f6f3d658c77d1`。
  验证 18/18、0 误触发；测试 20/20、1 误触发，均无重复。
  C 同时改变结构、上下文和前史，不作纯结构单变量归因；原始正例仍只有
  3 秒，不能证明超长慢速目标完整覆盖，更不能称独立真人泛化达标。
- 真正默认 A（`922eba9175fcda60f7c8a4505ca4eb5a97c86ceb30fbe48c685fd612098ac910`）
  用隔离只读 `87254121` 工作树及原始前端哈希重新评价：验证 18/18、3 次误触发；
  测试 20/20、3 次误触发。不是前文 A-r 重训控制。
  输入音频、分组、会话完全相同，仅为默认元数据补齐显式显示短语“你好，openvela”
  （历史 manifest 缺字段时默认显示“你好，open-vela”），未改录音或标签。
- FunASR 审查已修正标准 `101 Switching Protocols` 握手、分段 `is_final` 与
  整轮 `is_end` 的区别、错误回执优先级以及连接入口前取消丢失。
  原有 peer 扩展覆盖多段终答累计、空终答、错误+结束、入口前取消及下一轮恢复；
  `make run-agent-funasr` 通过。只声明该 `is_end` 协议变体，不猜旧服务结束语义。
- 默认唤醒应答已改为显式半双工采集屏障：唯一 producer 保持打开，提示音期间
  清零丢弃输入，恢复时丢弃跨代在途块和 PCM16 半样本；300 ms 安静判据与
  250 ms 尾音隔离不是 AEC。连续语音跳过提示并保留有界前滚。现有 capture、
  TTS queue、FunASR、final-stream、endpoint 主机检查全部通过
  （`host-final-integration.log`）；短促连续命令及 Media 同时录放仍待板验。
- WKM2/WKA2/WKS2 携带显式前端版本，TFLite 内部 `bkvoice.frontend` 同时绑定；
  无内部字段只允许旧 v1，错配/未知/重复字段拒绝。C 迁移后 SHA256 为
  `94639a263093895365f57fa2e288016bf3bc473f861ab8e7c67269304b6b6277`，
  canonical 计算图/权重/量化未变。研究包 26820 字节，SHA256
  `69d616f20e65ff68a4e59f10d8848e9d47bd3f41c343af364cbea2b89cec3cab`，
  位于 `frontend-c-bound/nihao-openvela-streaming-research.wkm`，未替换默认。
- 模拟器 `emulator-5554` 从 v33 保留数据覆盖安装 v37
  `0.7.7-shaniu-companion`，安装前后证书 SHA256 均为
  `b2ea591116f967af230d0f06f678cc5fdd99d5884cc429b8d22c6489ba96050b`。
  APK SHA256 `dbca811d4f6dab86ad5198cf00d30cc9008fa8045d7c46a8ecd4c868481691d7`。
  实际启动、切换定制/更新/设置并查看截图；证据在 `emulator-v37/`，录屏
  `navigation-v37.mp4` 为 18.5512 秒。不宣称实板连接/模型安装成功。
- 用户确认 Windows 输出接耳机，模拟器声音不能到达板端；本轮不再执行该
  物理回放路径、不切换用户音频路由。扬声器到板端与真人声学指标均未测。
- 当前工作树以安全计数 652（已知 651 后的未部署候选）完成 CP/AP 全链接，
  见 `build-full-candidate-652.log`。CP 初始 gross heap 52124 字节，复制 data
  25660 字节，bss 160544 字节，中断/启动栈各 2048 字节；预算报告明确
  `boot_status=not-verified`，不是运行期高水位证明。
  独立审查随后发现规划截断回退及取消/历史提交竞态，已修复并通过
  `host-commit-boundary-final.log` 与 `build-post-review-652.log`。规划必须
  完整工具阶段，正文提交与取消互斥；提交后可停止播放，但不撤销已接受历史。
  本地 Agent 提交 `938b66bd38bcf6111236ae1c3b9db520e1d3c3dc`，两次推送均遇 TLS
  错误，最初未更新主 manifest。后续 Linux/Windows Git 传输仍失败，但
  GitHub API 可用：通过官方 Git objects API 创建树与提交，分别核对
  tree `bc98dc248f15fbb198edf430011c2e4a8f6fc405` 和 commit 与本地完全一致，
  再以 force=false 更新个人 fork 的 `feat/shaniu-streamed-reply`，独立回读
  确认 `938b66bd38bcf6111236ae1c3b9db520e1d3c3dc`。主 manifest 此后才固定该 SHA。
  工厂软件候选 `factory-652-worktree-candidate` 已签名生成（未物化同板 BIN），
  不是最终提交交付。build provenance 已补录实际 Agent checkout：现有
  workspace 检查 9 项通过，`build-agent-provenance-652.log` 构建退出 0；新
  build-manifest 的 `provenance.dependencies.ai_agent.source_commit` 为
  `938b66bd38bcf6111236ae1c3b9db520e1d3c3dc`，dirty=false。主仓仍为工作树
  候选；旧软件包没有自动获得这份新来源清单，须重新打包。
- MUSAN 归档大小 11086114085 字节，SHA256
  `86d1061c7e15b5c9e906777685c519701df51bfde3001e1070dcc9ffac955ee1`，
  完整 gzip/tar 检查通过。按包内逐来源许可排除未明确授权组，取 140 段
  去重音乐共 36003900 ms，仅裁去不足 20 ms 的末帧，不增益、不循环。
  署名与许可记录在 `public-background/selected-background-provenance.json`；
  原始音频不公开。现有 evaluator 重新绑定同一固定策略；C 已完成长背景
  评价，140 段音乐触发 **892 次 / 10.001083 小时，约 89.19 次/小时**，
  明确未达 0.5 次/小时目标，不准提升为默认。原有短测试还有 1 次误触发，
  因此合计报告 `background_false_positives=893`，不能混淆两个分母。
  同材料默认 A 为 24 次（约 2.40 次/小时），B 为 10 次（约 1.00 次/小时）；
  B 相对 A 有改善但仍未达门槛，C 长连续背景明显退化。三者原有 20 个
  短正例均命中。证据 `musan-{default-a,b,c}-test.json`；该音乐集合此后
  属于已观察回归集，不再宣称未见最终测试，也不外推到普通语音或真人泛化。
  后续应从独立来源补足训练背景并核查 C 的长上下文行为，而非降低阈值。
- 流式 ASR 新增真实本机服务证据：现有 `run-agent-funasr` 测试入口增加
  `--live localhost PORT PATH CA PCM`，调用生产 `funasr_asr.c`，校验 TLS
  CA/主机名后按 100 ms 上传 PCM，输出时间与最终文本摘要，不输出正文。
  FunASR 1.4.16 + 固定 Paraformer online/offline/FSMN 模型实际运行三轮
  公开 5.5467 秒样例。前两轮成功但有原始 PCM 被容器探测的警告；本机
  参考服务改为按已协商 PCM16 显式解码后，第三轮无该警告，首在线片段
  2078 ms，最终结果/结束 ACK 5908 ms，4 个非空在线片段，最终 57 字节
  SHA256 `3cb27df9ce1e524a1c9b54ae7d2548386971863601c95ba79b0690cfb71beefb`
  与公开样例参考文本一致。证据 `funasr-reference/c-live-pcm.log`、
  `service-pcm.log`、`STATUS.md`（含模型 SHA/服务适配说明）。这证明音频
  未发完前已有真实识别输出；不是板端、产品云服务、30 轮或声学指标达标。
  未把电脑设为产品永久网关，也未修改设备服务配置。
- C 连续背景退化的定向诊断已完成：固定原模型与策略，对同一 60 秒音乐，
  INT8 重现 8.1/23.1/28.8 秒的 3 次误触发；浮点权重经相同生产 C 判据
  仍触发 5 次。三处对应浮点分数为 0.994/0.996/0.980，完整序列与逐帧
  浮点结果一致。因此该样本不能用“量化或状态传递累积偏移”解释主要失败。
  200 个评分时刻中浮点/INT8 有 4 个阈值分歧，绝对误差中位 0.0024；
  后 30 秒 p95 0.049，小于 5～30 秒的 0.090，未见持续误差扩散。
  证据 `c-longstream-diagnostic/result.json` 及同目录复现脚本。
  首要待验证假设是训练仅监督序列末帧、异源前缀与短冷启动验证未覆盖连续
  背景各决策时刻；不是已证明的唯一根因。下一候选分开验证监督与数据覆盖，
  不通过降低门槛或重划已观察测试集提高成绩。当前训练工具源码哈希与旧
  检查点记录不同，诊断只证明权重可加载、输出/事件可重现，不证明源码逐字一致。

## 当前语音专项矩阵（工作树候选，非整机验收）

历史矩阵保留原始失败与基线；以下覆盖其“当前状态”，不删除未完成项。

| 编号 | 本轮已取得 | 剩余实现 / 验证 |
| --- | --- | --- |
| Q01 | 保留 87254121 的 K2 中断保护候选；652 CP/AP 链接及内存预算通过 | K2 根因闭环、真实启动和关开机仍未通过；本轮不触发 |
| Q02 | 单采集/前滚、样本时间轴、终答 SSE→句队列→TTS 已进生产调用链；FunASR 真实本机 WSS 上行识别通过 | 用户实际后端与板端部署、30 轮冷暖对照、额外规划成本及声学指标未测；App 未提供 FunASR 端点配置 |
| Q03 | 正式 Agent 工具阶段保留，终答边界、失败/取消和历史提交 peer 通过 | 新代码的完整视觉云回合、超时恢复和再唤醒实板回归未测 |
| Q04 | 既有有界 PCM 队列、一次播放器生命周期及取消 peer 通过 | 实际 Media 服务端重复 EOF/resume 尚未修复，不能用 peer 代替 |
| Q05 | A/B/C 长背景对照及同源背景实验完成；B 新候选回归失败。实际 C 权重揭示 XNNPACK 与原生推理差异，已固定参考内核并重新评价 | 历史桌面默认内核指标不得冒充原生/板端结论；候选均未合格，不替换默认，不称真人泛化通过 |
| Q06 | 合法人声资源与提示音兜底保留；半双工采集屏障和连续命令交接已实现 | 实际人声选择、应答尾音与紧接命令不吞字仍需声学验证 |
| Q07 | 既有认证撤销实现保留，本专项未扩展破坏性操作 | 多副本、独立物理恢复及现场重新认领缺口仍保留 |
| Q08 | 历史 Mi 10 凭据保持及音量回读证据保留，未更换设备云配置 | 当前手机不在线；本轮无新的热点、真实 BLE 或配置保持验收 |
| Q09 | v37 模拟器安装、四页操作、截图和录屏完成；模型前端兼容信息更新 | 不能扩大为真实设备连接或全部多尺寸/错误场景已验收 |
| Q10 | 主仓 ae8f36bd / Agent 938b66bd 已发布；匹配 CP/AP 和签名软件包完成；该提交 Actions 与本机独立下载校验通过 | 后续连续训练改动未提交、不在该 CI 范围；最终模型与新源码交付仍未完成；CI 临时身份不供当前板 OTA |

本次继续执行的现有主机入口为 capture、FunASR、final-stream、endpoint、
TTS queue、cloud-http、voice-kws，整体退出 0，日志
`continuation-host-regression.log`。系统 Python 的 KWS 部分为 11 passed、
1 skipped（缺 TensorFlow）；不将该跳过项计为模型数值验证通过。

随后使用已有 TensorFlow 虚拟环境补跑 KWS 全文件检查，11 passed、0 skipped，
包括新增的负例逐时刻监督、前缀遮罩、正例末帧语义、样本权重和检查点互载。
训练入口新增可选 `--streaming-negative-frame-loss-weight`，默认 0 保留旧配方；
启用时仅对已标记负例的源录音最后 149 帧添加非唤醒损失，不为正例猜词尾，
不监督异源前缀。单步导出图及运行判据不变。冻结 weight=1、同数据/seed/
24 epoch 的一次对照，CPU 2 线程、7200 秒截止，证据配置
`c-supervision-experiment.json`。

该对照已完成 24 epoch，模型 SHA256
`19fcbaab1916c995b33544307bb02ab8d3bcec4ac4b29698de2cc3226be538bf`。
短验证集 18/18、短回归目标 20/20，均无额外误触发；但同一已观察的
140 段、10.001083 小时音乐回归仍有 **734 次误触发（约 73.39 次/小时）**。
相较旧 C 的 892 次下降约 17.7%，仍远未达到 0.5 次/小时目标，不能推广为
独立测试或真人泛化改善，也不能替换板端默认。证据为
`train-c-negative-frame-run.log`、`musan-c-negative-frame-test.json`。

下一实验只扩展连续负例覆盖：现有 MUSAN 包内未使用的 Jamendo 录音中，
筛出逐项明确 CC BY/BY-SA 的完整录音，训练 12 首 / 5 个专辑来源组 /
3596060 ms，验证 3 首 / 2 组 / 1008460 ms。与上述 140 段的路径、原文件
哈希、对齐 PCM 哈希及许可来源 URL 无交集；仅证明来源组隔离，不能声称
艺术家身份或跨目录同作品声学等价已经排除。歧义许可及缺逐项映射的噪声
未纳入。证据 `c-background-training-preparation/candidates.json`；数据资格
准备不是已训练结果。长文件窗口必须带同源前序状态，不用异源拼接冒充连续。

当前模型目标仍为 BK7258 本地 TFLM 推理；Android 不执行唤醒模型。
Windows 输出接耳机，模拟器播放不能作为扬声器到板端麦克风验证。
本轮未部署上述候选，未触发 K2、刷写或清理设备身份。

连续负例候选完成 24 轮与 INT8 导出：模型
`1a2b3a66db9c173a2476cc4008a47227605f939511a9ff14158b4a6f001755df`，
26680 字节，状态张量共 7936 字节（不是 TFLM arena 总需求）。新增 1185 个
3 秒训练窗口各保留同源紧邻 2 秒历史；这不是整首前端状态从头滚动训练。
与前一候选在同一新验证集对照：目标均 18/18、无重复；1008460 ms 音乐
误触发从 12 降至 4，但候选约 14.28 次/小时，仍不合格。阈值、两次确认、
结构、seed 和轮数未改变。证据 `c-continuous-{control-,}validation.json`、
`c-continuous-experiment.json`；上述集合现已用于评价，不再称未观察盲测。
冻结训练清单 SHA256 为
`8620f879e510be94b0b3d1fa8a7e62fe859be85cb3e222d5a0d898815fe90de8`。
现有测试文件补验为 12 passed；不是目标板推理时限或真人召回证据。

同策略 10 小时音乐回归随后完成：该候选 220 次误触发，相较前一候选
734 次减少约 70.0%，短目标 20/20、无重复。但约 22 次/小时仍未达到
0.5 次/小时，且远差于原 B 的 10 次/10 小时；不能仅凭相对改善选择 C。
保留 C 为未合格研究候选，下一受控对照将同一连续背景覆盖用于 B，固定
其 DS-CNN32、频率步长 4、1 秒前端预热、seed 与 24 轮，不调判据。
证据 `musan-c-continuous-test.json`、`b-continuous-experiment.json`。

### 实际权重的原生推理核对与评价后端修正

B 同源背景候选（`572c65b5…e55b7`）完成后，短验证与原 B 同为目标
17/18、背景 1008460 ms 零误触发；但已有 36003900 ms 长背景回归从
原 B 的 10 次退化到 46 次，短目标 20/20 退化到 19/20，无重复。
淘汰该候选，不增加轮数或调整阈值补救。这里及前文长背景数字均来自
当时 TensorFlow Lite 自动选择的 XNNPACK；它们保留为历史实验，不能
直接归给固件原生 TFLM。

现有 `test_bk7258_voice_kws_model.cc` 增加实际权重核对入口
`--candidate <frontend> <model> <features.f32> <scores.f32>`，不替换原有
合成结构、包损坏、前端错配、重置断言。原 B 的 69 个验证窗口在原生
TFLM 与桌面分数完全相同，arena 使用 41856 字节。C 实际训练权重
`1a2b3a66…755df` 的 10281 帧则未通过原 2 LSB 容差；同一输入的
桌面 XNNPACK 与 `BUILTIN_REF` 最大分数差达 0.28125，共 520 个类别值
不同。改用参考内核后，原生与桌面逐值一致，两次重置后输出完全一致，
arena 使用 21824 字节，另有 7936 字节状态缓存。未放宽断言。

上述 C 输入是 69 个验证片段的特征顺序拼接，用于模型数值和重置检查，
不是连续声学准确率评价，也不是板端耗时。B 内部前端绑定迁移后哈希为
`1679970f931a6d6f5ecb104528fa9c896a34f6d086dc72b987d359d5614fa368`，
计算图/权重不变。完整输入哈希与结果在
`frontend-b-stateful-native/`、`frontend-c-continuous-native/`。

实际训练后 INT8 检查及 `kws evaluate` 已显式固定 `BUILTIN_REF`、单线程；
冻结策略和结果记录 TensorFlow 版本/内核，不接受缺少该绑定的旧策略
直接执行新 test。模型权重和唤醒阈值未变，C 长背景参考内核复验单独留档，
不能据一次数值一致便声称误唤醒或目标板泛化已达标。

C 参考内核长回归已完成：36003900 ms 背景 222 次误唤醒，目标
20/20、无重复；对照旧 XNNPACK 的 220 次，数值后端问题不是高误唤醒
的主要原因。`musan-c-continuous-reference-test.json` 记录 TensorFlow
2.15.1 / BUILTIN_REF / 单线程。原策略被实际 CLI 以
`frozen_policy_binding_mismatch` 拒绝，未写出新测试结果；证据
`rejected-old-inference-policy.log`。已有 Python 检查 12 passed，原生
合成/包兼容检查仍通过，没有删除或放宽原断言。

B 两组同内核复验也已完成，结果汇总如下；背景均为相同的 36003900 ms，
这些材料已经观察过，只是回归集，不是新的盲测或真人声学验收。

| 权重 / BUILTIN_REF | 背景误唤醒 | 目标命中 | 重复触发 |
| --- | ---: | ---: | ---: |
| 原 B `f28f352f…47f1` | 11 | 20/20 | 0 |
| 同源背景 B `572c65b5…55b7` | 45 | 19/20 | 0 |
| 同源背景 C `1a2b3a66…55df` | 222 | 20/20 | 0 |

同源背景 B 的退化在相同参考内核下仍成立。证据
`musan-b-{control,continuous}-reference-test.json`；三者均未达到背景
0.5 次/小时的目标，且正常短句命中不能替代轻声/快慢语速子组验收。

下一轮假设由训练元数据而非测试样本反向挖掘得出：1185 个连续音乐
窗口受来源归一约束，总权重实际仅 60，正例总权重约 3027。已冻结一次
每连续负例来源总权重 5→50 的 C 对照，总权重 60→600；数据、结构、
前史、seed、24 轮、判据均不改。新参数默认仍为 5，拒绝非有限值、
非正值和超过 1000 的值，并记录实际来源数/总权重。该实验不新增声音
来源，不将窗口数当独立说话人数；预算 CPU 两线程、7200 秒，不自动
加轮或换默认。该次训练随后完成 24 轮，实际每来源总权重 50、12 来源
总权重 600；最佳检查点第 24 轮。检查点仍按原 `val_loss` 选择，包含
终帧分类和负例逐帧辅助项，不是仅分类交叉熵。

同一参考内核验证集保持 18/18，1008460 ms 背景误触发从 4 降至 2；
36003900 ms 长背景回归从 222 降至 75，目标 20/20、无重复。约 66.2%
相对改善不等于合格：约 7.5 次/小时仍高于 0.5 次/小时，也差于原 B。
原生 TFLM 对该新权重再跑 10281 帧，分数误差 0、重置逐值一致，arena
21824 字节、单独状态缓存 7936 字节；不是板端推理 p95。

模型 26680 字节，SHA256
`86a72bc2e3bf5c738200a9fce86580e6e2e387586822bdab521a9b75d7dcb73a`。
仅生成显式未合格研究 WKM2 包
`frontend-c-continuous-weight/nihao-openvela-c-weight50-research-not-qualified.wkm`，
26820 字节，SHA256
`02788c7848f8447658b097608ceace1bd27d4284d54f7098a9c14fb85e1f419b`。
需支持前端 v2 的匹配固件，未向当前板安装，稳定默认未替换。证据
`c-continuous-weight-experiment.json`、`musan-c-continuous-weight-test.json`、
`frontend-c-continuous-weight-native/result.log`。本次有界试验结束，不再
自动加权/加轮；保留失败门槛和已实现的数值一致性修复。

已发布提交 `ae8f36bdf3e5531b7723bce410b68454f1a2b763` 的 Actions
run `35840720143` / attempt 1 成功，本机独立下载后六文件 SHA256 清单、
8 镜像包检查及 BL1/BL2/CP/AP 公钥验签均通过。CI 工厂软件包 SHA256 为
`cfb7f9de8898a6e490b8fa5f6b3cb82ef0bdda7a5aa1d4b0c07332185a801d89`。
证据 `ci-35840720143-verification.json`。它使用 CI 临时开发身份，不能更新
当前板；也不包含此后未提交的连续负例训练修改，不代替最终提交再验证。

### 验证来源的语速/数字增益分组（后续离线诊断）

已有 `kws evaluate` 补齐每会话漏报、重复、窗口外事件及分组统计；
分组参与冻结清单绑定。背景每小时误唤醒仅用无目标会话的时长作分母，
正例会话的窗口外触发仍保留，不挪进成功数。原有检查扩展后 13 项通过。

从相同 27 个验证会话、22 个来源组派生 6 个条件，共 162 个会话、
108 个目标区间、1409400 ms 音频。完整连续音频分别作保音高变速或
数字降幅，不重算轻声 VAD、不裁掉超长片段；仅补齐名义缩放时间轴和
20 ms 输入帧。原标注区间按语速映射，未人为延长。它们不是经过核对的
真实词尾，不能报告词尾 p95；同源变换也不是新说话人/真人泛化证据。
清单 SHA256 `617ae35048f2cba44d1b951762ee1dc9b5bcb87eb615ec0d9ffb1b24eeecdf5f`。

固定 0.60、连续两次、300 ms 判据与 BUILTIN_REF，下表是严格区间命中数，
每组分母均为 18，所有组重复触发为 0：

| 条件 | 稳定默认 A | v1 重训控制 A-r | 有状态 B | 流式 C weight50 |
| --- | ---: | ---: | ---: | ---: |
| 原速/原幅 | 18 | 18 | 17 | 18 |
| 数字增益 0.25 | 18 | 17 | 17 | 18 |
| 数字增益 0.10 | 17 | 6 | 17 | 18 |
| 语速 0.75 | 17 | 12 | 17 | 12 |
| 语速 1.25 | 17 | 16 | 15 | 13 |
| 语速 1.50 | 6 | 2 | 5 | 0 |

A-r 不是已部署默认 A；A-r/B 可用于这组相同训练配置的前端对照，
C 同时改变结构、上下文及背景权重，不能全部归因于流式结构。真实默认 A
在隔离的旧源码/旧前端路径运行，未修改其源码或权重；调用现有评估函数时
显式固定 Interpreter 为 BUILTIN_REF/单线程，之后仅汇总原始事件。
其清单仅显式写入旧默认要求的显示短语，音频、分组、区间完全相同；
旧评估器哈希和调用方式记录在 `a-default-grouped.json`。默认 A 的
1.5 倍速窗口外事件为 8，仍有明显弱项，但当前新候选不能替换它。
1.5 倍速窗口外事件分别为 11、5、12；B 的 13 个漏报中有 5 个仍出现
窗口外触发，其余 8 个无事件，不能只解释成标注问题。数字 gain 不是
声压、距离或真实轻声发音。此组缺口说明“正常短句 20/20”不能替代语速验收。
原始结果、输入变换/补零数量、哈希和汇总在输出根 `robustness-validation/`。

据此预注册一次 C weight50 的保音高 tempo 覆盖对照，而非继续背景权重
扫描。原有训练开关此前为隔离变量未启用；本次仅增加 0.75/1.25/1.5
训练变速，维持来源总损失权重，修正原先变速派生会额外增加正例及不完整
目标负例权重的问题。24 epoch、seed、结构、2 秒前史、阈值不变；
CPU 两线程、7200 秒上限，不自动追加轮数或替换默认。
实验注册 `c-continuous-tempo-experiment.json`。当前 3 秒训练入口仍拒绝
超过上下文的慢速完整目标，绝不裁断后保留正例；超长正例训练支持仍未完成。
最终效果必须在训练结束后按同组及长背景回归判定，不能用训练准确率代替。

该有界训练随后完成 24 轮、退出码 0，INT8 文件 26680 字节，SHA256
`9b583df85f8b1987f086e714bab5717bee802b9b1b5075c62a3e7c22ccc67366`。
训练代码哈希 `c417731f13ac2083d49e01367ca3c49fd4747e4ac3e0cd6d86ea9f386da8c9c8`。
新增变速副本 670 个，仍来自相同 59 个正例来源组；32 个超长慢速副本被拒绝。
实际正例总权重保持 3027、连续背景保持 600。元数据中的历史标量
`non_positive_weight` / `other_derived_unknown_weight` 是分组平衡前基值，
不是变速派生后的逐窗口权重，应结合新增 `tempo_augmentation.weight_policy`。

| 条件 | C weight50 对照 | 本次 tempo 候选 | 本次窗口外事件 |
| --- | ---: | ---: | ---: |
| 原速/原幅 | 18/18 | 17/18 | 0 |
| 数字增益 0.25 | 18/18 | 17/18 | 0 |
| 数字增益 0.10 | 18/18 | 18/18 | 0 |
| 语速 0.75 | 12/18 | 5/18 | 0 |
| 语速 1.25 | 13/18 | 10/18 | 8 |
| 语速 1.50 | 0/18 | 7/18 | 11 |

正常与慢速退化，按预注册首个门槛淘汰；未再消耗长背景回归，也未打包、
部署或替换默认。当前短片分类验证 18/18 与上述连续流表现明显不等价，
需要进一步核查时间监督与实际判据，而非追加变速或 epoch。证据
`c-continuous-tempo-result.json`、`robustness-validation/c-tempo-report.json`、
`robustness-validation/comparison-final.json`；未获得新的板端/真人证据。

### 流式正例时间监督：受控实现与首轮结果

随后固定上述两份 C 权重，记录每 20 ms 实际参考内核输出，再对照生产
300 ms/连续两次判据。`robustness-validation/timing-trace.json` 显示：
weight50 的 18 个 1.5 倍速目标中有 14 个在原目标区间内从未达到阈值，
不能将全部失败归因于评分间隔；tempo 候选的 18 个慢速目标中有 12 个
出现过高分却没有严格命中。源区间仍是原有粗区间，不补造词尾延迟。

核对正式训练入口发现正例仅有最后一帧分类损失。新增可选
`--streaming-positive-frame-loss-weight`，并实际接入全序列 fit：
从原始 PCM 的首/末非零样本取得保守完整波形范围，之后才进行增益等
变换；平移只在完整范围仍被保留时继承时间标签，变速按完整输出长度，
合成房间扰动追加最大反射/FIR 范围。不用 RMS 门限重新裁切轻声，
也不把这个范围称为人审词尾；未知或裁切后无法证明的范围保持末帧损失。
完整波形之后的帧均值与原末帧损失做凸组合，不增加正例来源/类别总权重。
合成前史、未完成的正例前缀与已有末帧不重复加辅助标签。

预注册 `c-temporal-supervision-experiment.json` 的 24 轮训练退出 0。
训练代码 SHA256 `fd09ffec0b83859a805168c4fd0adf7b30df4eff69b65cf90d0d6cf830fa3215`；
新模型 SHA256 `6d4a96ddf3c15a5624a770acf3ef7969660624b492692129da515442c7fe94b7`，
26680 字节。59 个来源组、5848 个训练窗口具有可用的保守时间标签，
不是增加了新说话人。继续使用相同验证派生流、0.60/两次/300 ms 和
BUILTIN_REF，没有更改任何接受区间：

| 条件 | 末帧监督 tempo 对照 | 新时间监督 | 新候选窗口外事件 |
| --- | ---: | ---: | ---: |
| 原速/原幅 | 17/18 | 18/18 | 0 |
| 数字增益 0.25 | 17/18 | 18/18 | 0 |
| 数字增益 0.10 | 18/18 | 18/18 | 0 |
| 语速 0.75 | 5/18 | 10/18 | 0 |
| 语速 1.25 | 10/18 | 18/18 | 0 |
| 语速 1.50 | 7/18 | 15/18 | 3 |

所有组重复触发为 0。慢速低于预注册推进门槛 12/18，且快、慢两组均未达
产品 90% 目标，所以没有继续长背景评估、生成更新包或替换默认。
新增梯度/边界/来源测试复用原文件，13 项 Python 检查通过；生产 C 核心
及前端入口通过，系统 Python 的两个 TensorFlow 跳过项已由独立 TF 环境
执行覆盖。实际模型在生产 TFLM 主机入口通过 10281 行、两次 reset 校验：
参考内核最大分数差 0，arena_used=21824 字节；不代表板上推理耗时或
物理回放通过。证据为 `c-temporal-supervision-result.json`、
`robustness-validation/c-temporal-report.json`、`frontend-c-temporal-native/`。

### 完整慢速源长度支持（实现验证，候选训练中记录）

原训练入口会拒绝超过三秒的完整慢速副本，前轮实际拒绝 32 个。
扩展现有流式入口 `--streaming-positive-max-ms`，上限五秒，默认仍三秒；
长样本必须启用流式结构、时间监督及变速，不能截成三秒后保留完整正例。
既有短源/不完整目标派生不改；长源单独保留完整波形及原 source/speaker。
每条记录显式携带真实源帧数和总有效帧数，末帧分类取有效末帧，辅助损失
排除前史和存储补齐后缀，量化校准同样裁到有效长度。补齐帧不冒充静音负例。
主机特征桥的有界输入从六秒扩为八秒，仅容纳五秒源加最多三秒前史；
没有扩大板端 arena、CP RAM 或模型状态，仍逐行使用同一生产 C 前端。

复用现有测试覆盖八秒特征、长目标未截断、补齐后缀损失/梯度为零、
按实际末帧计算准确率及校准不读取补齐帧，13 项 TF 环境测试通过。
`c-long-source-experiment.json` 记录本次训练条件和推进/产品门槛；
正常及语速结果需待该训练和独立评估完成，不能以支持长输入即宣称慢速通过。
Q05 仍为有真实离线改善但候选未合格；Q01–Q10 的其他未完成边界不变。

### 完整慢速源评估完成及对照有效性修正

上述训练现已退出 0，完成 24 轮；模型 26680 字节，SHA256
`cc9c98ca9ca4db8ae4b205057ae7d8dd9d29939dbedb4f07ce7bc06dc200a750`。
新增 32 个完整慢速副本后，变速副本合计 702，正例原始来源仍为 59 组。
正例总权重仍约 3027、每来源约 51.3051，不把变速副本计作新说话人。
相同连续流验证集及原判据下：

| 条件 | 三秒上限时间监督 | 支持完整长源 | 长源候选窗口外事件 |
| --- | ---: | ---: | ---: |
| 原速/原幅 | 18/18 | 18/18 | 1 |
| 数字增益 0.25 | 18/18 | 18/18 | 1 |
| 数字增益 0.10 | 18/18 | 18/18 | 2 |
| 语速 0.75 | 10/18 | 16/18 | 4 |
| 语速 1.25 | 18/18 | 17/18 | 3 |
| 语速 1.50 | 15/18 | 11/18 | 5 |

重复触发均为 0。纯负例 469800 ms 内 0 次事件，不等于长期误唤醒达标；
表中窗口外事件包含正例会话的区间外事件，不能混作纯负例统计。
快语速退化，未过预注册推进门槛，不继续十小时评估或生成部署包。
生产 TFLM 主机入口 10281 行、两次 reset 与参考分数差为 0，
arena_used=21824 字节；仍非板端执行时延或声学验收。
证据：`c-long-source-result.json`、`robustness-validation/c-long-source-report.json`、
`frontend-c-long-source-native/result.log`。

进一步审查发现此前合成前史使用 `seed:index:source_id` 分配；加入长源时
其他记录的序号改变，前史也随之改变。因此本次结果存在混杂，不能严格归因为
仅改变源长度，预注册的单变量意图未完全兑现。保留失败结果，不事后改接受区间。
生产训练入口现改为流式记录的 seed/source/label/PCM 内容哈希决定前史，
训练负例前史池按内容哈希排序；相邻真实负例前史保持原样，验证/测试不得入池。
元数据新增 assignment_version 和分配摘要；非流式旧流程保持原算法。
这个修正尚未用于上述权重，不可把新代码检查反算成新模型效果。

修正后已有测试验证：记录重排、前史池重排、插入长源时原样本前史不变；
测试来源不能进入前史池，相邻真实前史不变，非流式旧分配一致。
`robustness-validation/stable-history-tests.log` 为 13 passed，
当前训练代码 SHA256 为
`3efe4cfc75864cf4878529e831005c4ba6c57cd80a77bebc85079f4d8524a225`。

另复用旧 weight50/tempo 两个模型的既有逐帧分数，不重新推理或训练：
先精确重现 108 个会话的生产判据事件，再仅作离线判据诊断。每 20 ms
判定且要求连续 300 ms 高分，没有改善 weight50 的快语速 0/18；缩至
160 ms 支持也仍为 0/18。tempo 模型同样只能从快语速 7/18 到 8/18，
尽管慢速从 5/18 到 13/18。这否定“单改判定频率即可解决快慢泛化”的
假设，不据此放宽生产判据。该诊断只有正例会话，不能评估长期误唤醒，
也不能把原粗目标区间当精确声学词尾。结果为
`robustness-validation/fixed-score-cadence-diagnostic.json`；未修改板端判据。

### 655 热点反馈后的连续播放与眼睛修复候选

- 用户提供的 14:34 日志：录音结束至首 Media 写入 18.424875 秒，
  eyes 工具约 5.237 秒；分句间 pause/xrun 后约 0.96 秒才 resume。
  这些是软件时间点，不是声学测量，也不足以归因于手机热点。
- 已修改 Agent 生产链路：整轮最终正文复用一个 64 KiB PCM 队列及唯一
  消费者，下一句请求不再等待上一句队列排空；每句保留终止/格式/帧
  对齐检查，整轮结束才 join 消费者并 drain/close。未增加并发云请求。
- 已接入首 PCM 写入/输出清理事件到显示服务。回调仅更新原子状态，
  显示线程使用当前资源包预读的单个 speaking 帧，结束恢复原表情。
  新增缓存上限 51,200 字节，分配/资源失败记录 unavailable，不阻断语音；
  认领及电源覆盖优先。显示 CRC 改用等价半字节表，未取消包校验。
- 现有 AIDK mcuboot 目标增量编译链接成功，退出码 0，日志：
  `out/shaniu-voice-20260923/build-655-tts-continuity.log`（工作区根目录）。
  实际重新编译 voice_channel、display_service、display_pack。
- 此候选含未提交源码，尚未打包/烧录；当前板仍属此前 655 验证。
  不能声称热点卡顿、显示工具 5 秒耗时或整体延迟已经实板修复。
  用户要求之后不再扩写 test；COM13 由用户采集，本轮未打开串口。

### 656 现场全量部署（23:03）

- 用户随后明确授权全量烧录；补齐短句 terminal 水位放行及 Media 清理
  成功后才发 OUTPUT_FINISHED 的边界，重新构建 floor=656。
- 版本 `0.7.11+656`，factory BIN 为 8,388,608 字节，SHA256：
  `76dd70c7d7fcd69fbe0187492e0bb5bd4c0722a3b53d23e555cd75d0560cf2ed`。
  `out/shaniu-voice-20260923/factory-656-tts-eyes/release.json` 记录同板基线、
  原签名身份及布局；package/trust 校验通过，ELF/map 与 dirty patch 已冻结。
- COM13 CH340 空闲核对后执行一次 BK Loader 工厂下载，全部五个成功标记
  齐全、无失败标记；Loader 原始退出码 1 为 advisory，封装判定 passed。
  证据：`out/shaniu-voice-20260923/flash-656-tts-eyes/result.json`。
- 工厂用户状态初始化；硬件独有区域按清单保留。不写 OTP/eFuse。
  Loader 已释放串口；未启动日志采集，由用户负责。**仅 FLASH_PASS**，
  656 启动身份、重新认领及声音/眼睛效果等待本轮现场证据。

### 656 用户现场反馈：显示、偏好持久化及播放连续性失败

- 用户提供 15:06–15:07 对话日志及双屏圆环照片；15:13 再次设置
  唤醒门限时，两次 `percent=85 saved=0 result=-74`。这不是设置成功
  后的 UI 刷新问题；`configuration ready=1 revision=3` 仅说明云配置
  就绪，不能覆盖偏好恢复/保存失败。
- `Malformed database image` 对应现用 UnQLite 的短文件或数据库头部
  校验失败，偏好层将 `UNQLITE_CORRUPT` 映射为 `-EBADMSG`。尚未读取
  实板数据库，不将异常起因推定为断电、USB 切换或物理介质损坏。
- 显示持续读取 `/mnt/sdnand/SHANIU/DISPLAY/active.json` 返回 `-20`；
  eyes 工具也返回 `-20`，正常资源包没有成功加载。NuttX FAT 查找
  中间目录不存在时亦返回 `ENOTDIR`，不能据此直接断言目录损坏。
  此次 speaking 缓存分支未获得生效证据，显示修复现场失败。
- 录音结束 `15:06:27.168281` 至首 Media 写入 `15:06:47.520531`
  为 **20.352250 秒**（代理指标，非声学）；整轮队列
  `pcm=947200 peak=32768 rebuffer=50 max_receive_gap_ms=1887`。
  连续播放目标现场失败；不能将 HTTP 200 或 request complete=0
  写为听感通过。
- 只读核对官方分支当前 SHA 为 `6052156862784037a449b9776dfc5c66b36518f6`。
  偏好库路径、按操作挂载/关闭及显示资源解析路径在该版本已存在。
  当前 KVDB direct 后端逐次关闭数据库，但以 `OMIT_JOURNALING`
  打开，`property_commit()` 不另做持久提交；这说明存在恢复保障缺口，
  不能证明它就是本次损坏的触发点。未回退源码、未格式化、未再刷板，
  未接管用户持有的 COM13。下一步先只读保存目标 SD NAND 的数据库
  文件与显示目录证据，再决定有边界的修复/恢复。

### 656 MSC 只读取证（用户手动切换，23:29 起）

- 当前设备 USB 父节点 `VID_1209/PID_0002`，Windows Disk 1 / D:，
  126,877,696 字节。仅读取该目标；COM13 仍由用户操作。
- `shaniu.db` 为 12,288 字节，原文件与本地副本 SHA256 均为
  `2b7b9a93a34c2f7a6829fff7d4630b6eeee71f582ba41825a4125b06e20d401a`；
  前 8,192 字节全 FF，数据库头确实不存在，不只是应用层解码报错。
- FAT16 两份 FAT 表一致。根目录的 shaniu 链为 `16→90`，首簇 16
  全 FF；簇 90 指向 display 簇 91，其中 packs 指向簇 92。
  Windows System Volume Information 链同时为 `31→92`，确认簇 92
  存在目录交叉引用。正常眼睛包未找到；递归复制 staging 异常条目失败，
  因此眼睛资源备份只能标为部分完成，不能声称完整备份。
- 证据在工作区 `out/shaniu-voice-20260923/storage-656-msc-20260923T2329/`，
  含受限权限的数据库副本、266,240 字节 FAT/根目录元数据、定向目录簇
  和 `inspection.json`；未上传。所有原始磁盘句柄已关闭。
- Agent 未执行磁盘写入、删除、格式化、磁盘修复或刷板；Windows 自动
  元数据活动未被排除，不能承诺 MSC 枚举期间整个系统零写入。
  已确认盘上文件/目录状态异常，但造成异常的写入者和时刻尚未定位。
  SDIO 多块预擦假设不符合当前 `MMCSD_MULTIBLOCK_LIMIT=1` 配置，不作为
  修复依据。重建介质涉及额外清除范围，必须先明确授权，不能自行处理。

### 657 演示收束：授权重建 SD，恢复比赛仓库唤醒资产

- 用户明确允许备份后重建 SD NAND，并要求今晚最后一个候选，默认模型及
  唤醒后应答均取官方比赛仓库 `6052156862784037a449b9776dfc5c66b36518f6`。
  未新增或执行 test，未训练模型，未回滚工作树或改动云服务配置。
- 重建前已读取完整 126,877,696 字节 SD 镜像，源流摘要与落盘文件一致，
  独立复制到上述本地受限取证目录；SHA256 为
  `eaec1de5a7dd2617d104488e8c3129bc6285602678c5fc5dd4d563f5e72e0efd`。
  原损坏文件仍可从此镜像取证；不将镜像加入交付 ZIP 或上传。
- 即时复核 Disk 1 / D:、USB VID_1209/PID_0002、大小及分区 offset=0 后，
  Windows Format-Volume 重建 FAT（2,048 字节簇），仅此 SD 卷被清除。
  未触及主机系统盘、板内 Flash 或 OTP/eFuse。恢复 cyan-v3、default-v1
  眼睛包及 active.json，读回 SHA256 与原包一致；未恢复损坏的 shaniu.db，
  由设备重新创建偏好库。原偏好值不视为保留，损坏触发原因仍未证实。
- 默认模型原本即与官方一致，23,640 字节，SHA256
  `922eba9175fcda60f7c8a4505ca4eb5a97c86ceb30fbe48c685fd612098ac910`。
  唯一新增生产修改为选择已有获授权 wake_reply.pcm，31,208 字节，SHA256
  `772a8aa9841a96bf96acdce733e4e6a67c2b46968333edb51fedc7511e3cc42f`；
  该音频与官方 Git blob 一致，不再选择 wake_reply_public.pcm。
- 657 CP/AP 编译链接退出码 0；生成 `0.7.12+657` 同板工厂包，沿用
  `bk7258-dev-0b4a26ff09315603c93f` 身份和原布局，安全计数 657。
  包完整性及签名证据校验 PASS。实际 AP 二进制已逐字节找到上述模型
  和音频，不仅检查源文件；偏移分别 1,361,204 和 1,329,636。
- BIN 8,388,608 字节，SHA256
  `8fbfeed7c52c299aa6718311f54733796a6082d272790b68eb831ed5e7b56877`；
  路径为工作区 `out/shaniu-voice-20260923/factory-657-demo-final/flash/`
  下的 `*-full-factory.bin`。源码为主仓 65410f5b 与 Agent 938b66bd 的真实
  dirty 输入，详细输入哈希在 build-manifest.json，不能称纯提交构建。
- 当前状态：已构建、已恢复 SD 文件，尚未取得 657 烧录及启动证据。
  Windows 已请求弹出，随后枚举原生 CDC COM19（PID_0001）和 CH340 COM13，
  SD 盘已不在 Get-Disk 中；等待用户明确释放 COM13 后才下载。
  回复慢、热点欠载、设置持久化及正常眼睛效果均需本轮实板确认，
  不将 SD 恢复或编译通过写成这些功能已修复。

### 2026-09-24：657 现场失败、SD 恢复及语音改动审查

本节追加后续证据；上一节“尚未取得 657 烧录及启动证据”是当时状态，
不再代表当前状态。用户已报告手动烧录 657，并回贴运行日志。此次由用户
独占 COM13，Agent 未接管串口、未再次烧录。未新增或执行 test；以下是
源码差异、已有评估文件和用户实板日志的审查，不是新固件验收。

#### 存储及当前人工操作

- 用户回贴的 657 日志持续 `stage=mount ret=-22`；偏好恢复也返回 `-22`。
  云配置 revision=3 / TLS 验证成功与 SD 资源卷能否挂载是不同结果。
  “我在”写入 31,208 字节符合本包选用的比赛音频；不代替完整启动身份回读。
- 再导出 MSC 后，Windows 也把同一 Disk 1 / D: 卷识别为 RAW，而非仅板端
  拒绝 FAT。此前只以 Windows 文件读回成功宣布恢复，缺少安全交接后的
  板端挂载与重启保持证据。损坏发生时间和具体写入者仍未证实。
- 对照官方 `60521568`，板级 SDIO、BK7258 USBMSC/USBMODE 和偏好存储适配
  源码没有差异。新 `bkfactory mount` 处理片内 Flash `/dev/mtdblock0` 的
  `/data`，不是 SD `/dev/mmcsd0`；不能说整片 BIN 会修复或重建外部 SD。
  同样不能据此排除间接运行期损坏。641 的既有实板记录已经包含相同
  `-22`、格式化恢复和首次写入复位丢失的未闭合观察，不应描述为历史从未发生。
- 经授权，复用 631 成功记录对已核准的 126,877,696 字节 SD 做一次非快速
  `format.com D: /FS:FAT /A:2048 /V:SDNAND /X /Y`，exit=0，FAT16、2,048 字节簇、
  61,822 可用簇。日志：工作区 `out/shaniu-voice-20260923/`
  `storage-657-known-good-recovery/format-full.log`。此前完整备份未覆盖。
- 格式化后追加读取 PhysicalDrive1 被系统拒绝，未取得该次启动扇区快照，
  也未执行该命令后续的缓存刷新/离线步骤。随后用户明确反馈已手动退出并
  开始安装；不能把用户动作写成 Agent 自动安全弹出成功。
- Mi 10 本轮只读确认仍为 App v37；仅向 Download 推送已有 cyan-v3 包，
  108,634 字节，手机端 SHA256 与源文件一致：
  `cf9dff38d34ff220503021fdea68dcf4b5942a1467f8fbe9c395f0d97653ee56`。
  包名 `shaniu-cyan-v3-recovery-657.bkep`，未覆盖安装 APK、未清数据或操作 UI。
  正式 App 安装、渲染及持久性结果等待用户反馈，尚不能写恢复通过。

#### 评审基线与本次审查结论

评审主仓为 `6052156862784037a449b9776dfc5c66b36518f6`，其 manifest 实际固定
Agent `add0db19d00301769907a5ece03fb9bd88d2edb4`。当前源码为主仓
`65410f5b07ba34d85b2c9ee7cd18f25707f4d0f2`、Agent
`938b66bd38bcf6111236ae1c3b9db520e1d3c3dc` 加各自现有 dirty 内容。
657 并不是把整个 Agent 恢复成评审版，恢复比赛音频不能这样宣传。

1. **额外 LLM 请求是确定的延迟来源。** Agent `src/core/agent_loop.c`
   `run_react_loop()` 为 voice 添加 `agent_finalize` 规划请求，再调用
   `voice_final_phase()` 独立 SSE 请求。`src/llm/llm_proxy.c` 对完整的无工具
   `stop` 正文仍执行 `draft=discarded`。评审版完成无工具正文后直接交给
   TTS，没有这次强制重新生成。657 本轮规划 5,914 ms；录音结束
   `16:10:40.339938` 至首 Media `16:10:51.124281` 为 10.784343 秒。
   这是代理指标，不是声学首声，也不是与评审版同条件对照所得的改善率。
   建议优先撤除强制双阶段开销，保留真实工具执行与终答安全边界。
2. **逐短句同步请求会反复承担 TTS 网络等待。** `voice_channel.c` 的
   `reply_text_worker()` 经 `tts_queue_sentence()` 同步请求，返回后才处理
   下一段。中文逗号达到 24 UTF-8 字节即可分段，易产生很短的请求。
   656 实板 `pcm=947200 rebuffer=50 max_receive_gap_ms=1887` 证明本轮连续性
   不合格；不能全部归咎热点。dirty 版本已改为跨句共用 PCM 队列，但尚未
   消除逐段请求成本。建议停止把此策略作为默认提速收益，演示收束优先
   复用完整终答的一次音频流请求，不以全量音频下载替代流式播放。
3. **协议兼容回归确实存在。** 已提交 `938b66b` 的规划解析只接受
   `finish_reason=tool_calls`，合法无工具 `stop` 会被报 `-EPROTO`；dirty
   已接受完整无工具结束，但仍有上一项正文丢弃成本。早前 `complete=-71`
   与该错误类一致，缺少原响应不能把每次失败都定为这个原因。
4. **实际 ASR 没有享受新增上行流式路径。** 用户当前日志明确
   `backend=mimo mode=batch`。FunASR 的开发验证不等于本板用户后端已切换；
   不为演示自动替换服务商或 Key。新增能力不能计为当前产品提速完成。
5. **默认模型没有被研究模型替换，研究结果也不足以发布。** 官方和当前
   `nihao_openvela.tflite` SHA256 同为 `922eba91…ac910`，默认仍前端 v1。
   已有同组回归里，默认模型 1.25 倍语速严格命中 17/18，流式 C 为 13/18；
   1.5 倍为 6/18 对 0/18。C weight50 的约十小时背景仍有 75 次误触发，
   后续 tempo 候选也已 rejected。依据 `robustness-validation/comparison-final.json`
   和 `musan-c-continuous-weight-test.json`，继续保留比赛默认；研究不能宣称
   鲁棒性提升落板。上述同源离线材料不是独立真人泛化。
6. **采集与结束改动不能整体一键回退或宣称完成。** 保留单采集所有者、
   有界队列、取消检查及 consumer join 的设计收益；单独验收交接行为。
   `wake_ack_gate()` 在应答播放前读取 PCM，播放时 producer 持续丢弃自身
   回声；因此 handoff 到 `chunk#1` 的间隔不等于麦克风停采或队列无人消费。
   但 quiet 判定后再开口且与“我在”重叠，仍可能被半双工屏障丢弃，不能
   宣称无条件支持紧接命令。`audio_playback.c` 与评审 Agent 无差异，现有
   反复 EOF 日志不能包装成已经修复 Media 内部 EOF/resume。

本轮仅完成审查及记录，不执行整体回滚、不更换板端软件。收束建议是先保住
当前现场存储恢复，再对确定的双阶段 LLM 与逐短句请求做窄范围回撤；认证、
配置、OTA、K2 候选及已验证的资源保护不应夹带回退。未获得同条件实板证据前，
不再把这些架构改动称为已兑现的提速。

## 2026-09-24：保留 657，资源导入与热点播放窄修复候选 658

用户撤销回退评审版的意向，指定 `0.7.12+657 / shaniu-demo-657-final`
为基准；COM13 继续由用户采集。本轮不换模型、不换应答音，不新增或修改
test 文件，也不重新构建 Android。保留原有 dirty 生产修复；前述回撤建议
没有成为本轮实施方案。

### 新的实板证据（用户提供，非 658 验收）

- 用户已报告导入后眼睛正常。日志 `16:47:53.203563` 明确
  `BKDISPLAY voice speaking=1 rendered=1`。这证明本次资源已能渲染，
  不证明重启保持、SD 长期完整性或新固件通过。
- `16:47:43.425344` 采集关闭到 `16:47:53.165688` 首 Media 写入为
  **9.740344 秒**，是软件代理指标，不是用户词尾到真实出声。
- 首个 TTS HTTP 共 **11,532 ms**，其中接收等待 **10,978 ms**、
  消费 **330 ms**、最长单次 read **4,927 ms**。仅产出 51,200 字节
  16 kHz 单声道 PCM（1.6 秒），小于 64 KiB PCM 队列，本请求不可能因
  填满该队列产生接收背压。播放 pause 到 resume 为 **8.446594 秒**。
  LLM 正文此前已结束；不能把该停顿归给仍在生成正文，也不能仅凭这些
  日志区分云服务与热点/互联网传输。
- `input write to mix -22` 与 SD mount `-22` 是不同调用域；录音收尾的
  Media 错误不能作为 SD 再次损坏的证据。

### 实际实施（仅三个生产文件）

1. `app/bk7258/bk7258_provision_time.c`：认证配置提交成功后，owner UTC
   同时设置 `CLOCK_REALTIME`，不是只更新业务时间缓存。修复 Wi-Fi-only
   首用时没有语音配置加载来置时钟、业务认为有可信时间但 TLS 仍看旧系统
   日期的代码缺口。保留证书校验；未拿到用户 `-129` 对应验证标志，不能
   声称每次 `-129` 均由此造成。
2. `app/bk7258/bk7258_display_store.c`：仅在 canonical active/default
   均不存在时，逐级检查可选 legacy 目录，再尝试旧目录资源。空卷不再
   因不存在的中间目录被误报为 `ENOTDIR`；普通文件占目录、I/O 和损坏
   内容仍报错。不自动格式化、不删除数据库、不伪造安装成功。
3. Agent `src/voice/voice_channel.c`：首句早发规则不变；首句后不再按每个
   逗号/短句同步发独立 TTS 请求，按固定 300 ms 单调时钟窗口、320 字节
   上限或正文结束合并提交。到字/条件唤醒不会延长该窗口；UTF-8 不截断。
   保留单生产请求、64 KiB 有界 PCM 队列、原 8 KiB 预缓冲、取消与 Media
   所有权。该修改减少后续短请求开销，**不宣称解决首请求内部多秒断流**。

### 构建、产物与边界

- 增量构建及 `verify build-manifest`、`verify package` 通过；未执行
  实板下载、App 安装或新的物理验证。沿用原构建目录、SDK、工具链、
  CP/AP 配置、布局和签名身份。CP raw payload 与 657 相同；AP 已变化。
- 版本 `0.7.13+658`，计数 658；主仓 `65410f5b`、Agent `938b66bd`
  **均带 dirty 输入**，不是这些提交的干净构建。
  主仓输入树 `b12861372ec58d44a380c472e70c39214f86da3a4374acb96b23e975b050bd6f`；
  Agent 输入树 `7e97b754efffeb9b92f56266e3532fe1427cf72c7862d42206f05755bbe41222`。
- 工作区产物 `out/shaniu-voice-20260923/factory-658-resource-hotspot/`。
  工厂 BIN 为 8,388,608 字节，SHA256
  `f675d8c29cd8f7076dcb36bd0afae9556dbbd738937e7860bfcf6cf499fcdeda`；
  签名包 SHA256
  `19e84bbfa0e2e993b102d71a3e5408c802213e9cf579963db99032e1df20dbf5`。
  复用已认可的同板 base；末尾 24 KiB 硬件独有区逐字节一致。
- 默认 KWS `922eba91…ac910`、“我在” `772a8aa9…cc42f` 与 657 一致。
  不构建/替换 APK。保留 657 原产物，不将 658 标为最终验收版。
- 全量 BIN 只写内部 Flash，不包含外部 SD 卷。已有 SD 的恢复来自此前
  完整 FAT 格式化和用户资源导入，**不是本次代码自动修复了 FAT/数据库**。
  原始损坏写入者尚未定位；不再次格式化当前已经恢复的 SD。
- 下一实板观察：相同热点下的一轮回答、TTS 请求数/停顿/结束、眼睛同步；
  随后正常重启确认眼睛和设置保持。没有该证据前上述项目仍属已实现未测。
