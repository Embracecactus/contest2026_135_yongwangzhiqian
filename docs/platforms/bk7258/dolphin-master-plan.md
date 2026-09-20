# 小海豚固件（OpenVela / BK7258）

状态：0.1.0+18 Files文件系统容量、目录浏览及返回已获用户实板确认；本轮成果进入提交发布，后续主线切回傻妞。USB/ADC/相机未完成项仍保留，不宣称Dolphin全功能完成。

## 2026-09-20 最新源码上板补验

用户重新授权 T5/COM3 RTS 下载。首次写入 `8de0ae78` 后，CP/AP 启动正常，
但 Dolphin 报显示/触摸初始化失败。定位为团队接线仍宣称 GT9xx 支持
`TSIOC_GETMAXPOINTS`，实际编入的固定版官方驱动没有 ioctl，而 LVGL 必须查询该能力。

`c6976458` 在 Dolphin 使用 LVGL 输入回调读取该驱动的标准单点样本，保留
既有坐标映射和唯一输入 owner，并在输入对象删除时关闭设备；更正旧 Kconfig
说明。不复制驱动、不改 NuttX/LVGL/SDK/FFmpeg，不改录音实现。
增量构建、COM3 AP/配对段下载通过；复位日志于 AP 启动约 3.02 秒出现
`dolphin-ui: display and touch initialized`，本次观察窗口无崩溃。

共执行两次有不同源码输入的下载，均成功；不作完整擦除或 TF 格式化。
当时的观察是 TF 的 CMD1/CMD8/CMD55 无响应；该候选随后复测为卡接触物理问题，
重插后恢复正常（根 README 的 T5 段已按“接触问题已恢复”记录），新包的触摸与新
录音仍未复验，不把恢复当成触摸或录音通过。
**只通过启动与界面初始化，触摸操作及新包录音保存 SD 尚未验收**，不继承旧包结论。
源码、产物和原始日志摘要见[本次补验](../../verification/bk7258/2026-09-20-public-source-build.md#同日实板补验与触摸适配)。

## 2026-09-20 录音构建接线恢复

`8de0ae78` 修复 Agent 清理误删 Dolphin 消费者接线的问题：恢复 CMake
录音适配源与 T5 默认录音开关，Make/源码条件统一限于 Dolphin + MIC + !MEDIA。
不接回傻妞旧语音路径，不修改 SDK、NuttX、官方 Media 或 FFmpeg。
`dolphin_recording.c/.h` 未改；采集适配函数体与 `7c25a95b` 一致，只收窄编译条件。

复用既有录音主机检查通过；现役 CLI 的 T5 CP/AP direct 构建通过。
最终 AP 配置 `DOLPHIN_RECORDER=y`；ELF/map 确认 UI → 录音 worker → 既有
NuttX audio 适配的单一链接链路，AP raw 547,020 B。哈希见
[构建记录补充](../../verification/bk7258/2026-09-20-public-source-build.md)。
本次未烧录、未新增实板验收；以下 `0.1.0+13` 用户录音保存 SD 结果继续保留，
不宣称新二进制已上板。635 不变。

## 2026-09-20 录音验收表述更正

用户再次确认 T5 小海豚能够录音并保存到 SD 卡。下文已有 `0.1.0+13` 的
用户实板确认：录音停止后生成 WAV，Files 中可见；镜像 SHA256 为
`af12ab9a55744ac370b7c8be5a6b0beccd6582f89ddece8e2aaa5a38634152f8`。
不新增或扩大音质、异常恢复等验收结论。

比赛交付隔离构建 `c10a7668` 为处理缺失的 `media_recorder_*` 链接而关闭了
录音选项。该配置与上述实测能力未对齐，不能把构建通过当作完整功能复现，
也不能反向写成“小海豚录音从未完成或未实测”。本次仅更正证据归属，
未改录音源码/配置、未刷机，傻妞 635 不变。

## 当前目标：T5 多功能便携工具（2026-09-10扩展范围）

基线为`d633799`，板上direct包0.1.0+10。现有未跟踪日志及三份撤回SD探针保留，不进入本轮实现。以下以用户最新范围覆盖旧M0及无线第一版限定；旧记录仅作版本证据。

| 能力 | 已确认链路 | 当前缺口与本轮验收 |
|---|---|---|
| 显示/触摸 | T35P128CQ-02 ILI9488 RGB、GT1151 → framebuffer/input → 双缓冲+DMA2D → Dolphin；用户已确认卡死/拖动/闪烁修复 | 保留基线，工具详情/取消/重入回归；FPS未量化 |
| Wi-Fi/BLE | AP worker与radio_mode、既有scan/monitor及NuttX BLE host → 有界列表 | 详情/报告/保存错误；实板13含AP本地32条带截断，CP wire仍4条；32条射频、BLE及连续切换待验收 |
| SD | TF P2–P5，一位SDIO避让P10/P11下载UART；FAT `/mnt/tf` → 只读浏览已接入 | 报告、录音、图片、受控复制；卡检测无有效实板证据，仍仅开机前插卡，不声称热插拔 |
| 按键 | SW5=P12/ADC14单键，数字别名与SARADC不能并用；LED P1与控制台共享 | 只按真实单键设计导航/确认/返回；不套用AIDK三键，不驱动共享LED |
| 麦克风 | 双模拟麦→板级mic配置→AP `/dev/audio` NuttX lower-half，配置已启用 | 复用已有采集接口，显式WAV录音+相对电平+有界保存；不启动云语音 |
| USB Host | 原生接口存在，已有BK7258 HCD→NuttX waiter/class；当前产品未启用 | 核实VBUS/连接器策略后启用MSC及设备信息接口；实物待用户说明，不能跳过软件接入 |
| 相机 | DVP连接器/P13、P15 SCCB及P27–P39数据，已有V4L2绑定 | RGB LCD与DVP真实引脚冲突；装配未知，先核对安全可用模式，不在屏幕主配置硬开摄像头 |
| 缺失模块 | 无已确认Sub-GHz、NFC、125k RFID、IR、iButton模块 | 排除，不建立占位菜单 |

执行批次：先完成无线详情→显式保存SD报告→文件浏览；其后按接口准备程度接录音/USB交换，单独处理相机冲突及单键导航。每批记录真实输入、退出/取消、资源回收和保存结果，未实测不称完成。

调度事实：现有collaboration支持指定模型及工具执行；`dolphin_publish_check`已以`gpt-5.6-terra` medium完成上一轮实际源码审查，现复用该代理实施app/dolphin及对应host测试。根代理处理硬件/资源所有权及独占COM3构建刷写，Shell/编译器处理机械工作。不宣称后端计费或路由独立验证。新增`gpt-5.6-luna`请求因宿主agent thread limit被拒绝，未执行，不再加代理；无付费入口/额度变更。

执行续接：原terra代理报告当前上下文token余额耗尽（非API额度错误），随后新建指定 `gpt-5.6-luna` 的 `dolphin_connect` 成功，并实际完成临时Wi-Fi连接UI与host用例。再申请并行terra被宿主thread limit拒绝，未执行；继续复用luna串行实现，不把普通UI交回根模型。

目标管理工具仍保存旧无线目标为blocked，并拒绝创建新目标（unfinished goal）；不把旧目标冒充完成。本页作为本轮实际恢复入口，该工具状态不阻塞已授权开发。

## 2026-09-11 本轮提交检查点

用户在18容量显示、目录浏览与返回复测请求后回复“正常”，据此记录该版本对应正常路径实板通过；镜像与证据沿用容量小节。随后用户授权提交推送，再回到傻妞开发。

本轮提交范围：Dolphin无线连接/显式断网采样/网关检查及报告、WAV录音、Files容量/扫描栈修复；包含默认关闭的ADC单键、受控复制、USB快照/UI候选与对应测试，后者未实板验收。复用共享media_recorder接入，不改SDK。排除原始日志及三份引用已撤回实现的SD探针。

发布验证：团队仓相对路径 `out/dolphin-t5-wireless-20260910/stall/publication-host.log`（与工作区根 out 中的构建/下载证据目录不同）记录既有Dolphin check、真实LVGL ui-check通过；wifi_async 4项、USB snapshot 4项、ADC 1项通过。当前主配置构建及18下载/启动/用户确认沿用已有有效证据。未重复全量clean或签名，未操作密钥。Git结果以实际提交及远端SHA为准；不将推送算作未验收外设通过。

## 网关检查接入（0.1.0+17已安装）

`gpt-5.6-luna`复用既有CPU0工作线程和NuttX psock ICMP，实现ping_async/poll/cancel以及NETWORK的CHECK GATEWAY。AP请求固定PING，local_ping区分其它票据；前后检查取消，等socket关闭后才发布完成并释放busy。CP wire与SDK不变。页面显示真实网关响应/错误，可保存报告；离页仍消费旧任务但不重绘，结果不代表互联网连通。既有wifi_async 4项和真实LVGL host测试通过。

固定流水线已完成增量构建、打包、校验与目标预检，日志`stall/{build-gateway17,package-gateway17,verify-gateway17,preflight-gateway17}.log`；8MiB direct镜像SHA256 `72275eeb061b0d3623a8db41122c944ade5af34cc204d3fb04b1c9d20e75de38`，同设备data保持，T5 COM3 PNP身份匹配。`stall/hil-gateway17/result.json`下载成功marker全部通过；`stall/boot-gateway17/serial.txt`包含FINALINIT PASS、SRAM-DMA2D及显示触摸初始化。已请求从UI连接Wi-Fi→CHECK GATEWAY→保存报告→再次检查并取消；真实结果待回报，不能用启动通过代替。

后续独立推进Files容量显示，所有statfs在现有后台扫描worker中，不能因显示容量再次阻塞LVGL。USB供电/实物、相机RGB冲突和ADC校准仍未解决，不用主机测试替代实板验收。

### 0.1.0+17网关实板确认

用户明确反馈“Wi-Fi 验证检查、报告保存及取消返回验证成功”，据此记录17的网关检查、报告保存、取消返回正常路径通过。对应镜像SHA256 `72275eeb061b0d3623a8db41122c944ade5af34cc204d3fb04b1c9d20e75de38`，下载/启动证据沿用`stall/hil-gateway17/result.json`和`stall/boot-gateway17/serial.txt`。功能依据为用户物理反馈，未新增抓包或报告读回；不外推互联网连通、所有异常路径或USB通过。

### Files容量显示（0.1.0+18已安装）

`gpt-5.6-luna`在既有扫描worker加入标准statfs，锁内发布文件系统总量/可用量及错误；64位乘法溢出检查。容量失败仍继续目录扫描，LVGL不执行额外文件系统I/O，不新增线程、挂载或格式化。现有UI host测试覆盖正常容量、EIO、EOVERFLOW且文件仍可浏览；`stall/build-files-capacity.log`目标增量构建通过。复用该有效构建生成18，`stall/package-capacity18.log`与`verify-capacity18.log`通过；8MiB镜像SHA256 `2aca538a6fbbccfcc6b362dba242058ab4b1b005b0ee8a59b9f3a2f863db383a`，同设备data保持。`stall/hil-capacity18/result.json`下载通过；`stall/boot-capacity18/serial.txt`包含FINALINIT PASS、SRAM-DMA2D和显示触摸初始化。后续用户已确认Files总量/可用量、目录与返回正常，见本轮提交检查点；不据此宣称物理卡容量检测通过。

17网关验收首次55秒被动采集`stall/gateway17-acceptance.raw`为0字节，采集已结束，用户真实结果待回报；不据此判连接失败。保留17供用户操作，不用下一候选打断验收。未提交、推送或清理用户数据。

## 0.1.0+16切换验收确认

用户在“连接Wi-Fi→CHANNEL STATS→DISCONNECT & SAMPLE→统计→START AGAIN”明确复测步骤后回复“好了，继续推进”，据此记录该路径用户实板通过。固件身份及下载/启动证据沿用下节16；未新增串口采样日志，不外推异常恢复、BLE或其它外设验收。下一批复用已有NuttX ICMP与工作线程实现显式网关检查、取消/结果和报告，不把网关响应等同互联网可用。

## 当前修复：已连接网络与信道采样互斥

用户确认15连接成功，随后CHANNEL STATS及START AGAIN显示Sampling stopped(-16)。源码 `bk7258_wifi_channels_run` 明确在LINK_CONNECTED时返回-EBUSY，避免逐信道monitor静默断开STA；重复重试不能改变状态。这不是已证实的无线故障，原互斥保护保留。

最小处置：普通进页检查链路并说明互斥；仅显式DISCONNECT & SAMPLE走新增AP-local请求，在既有worker中停止STA、等断开并同步native link后开始采样，失败或取消不继续，不自动重连。CP wire结构、版本、SDK及板配置不变。`gpt-5.6-luna`实现新AP-local `channels_switch_async`及显式UI按钮，公开link-state枚举保持原数值。旧票据未消费时不重入；worker检查预取消、stop、断开等待与native同步，成功stop后清除saved连接。现有wifi_async 4项测试（含stop失败、链路超时、sync失败、stop→sync→monitor顺序及预取消）和真实LVGL UI测试通过。`stall/build-channels16.log`增量构建及`verify-channels16.log`校验通过，8MiB direct镜像SHA256 `e2350f4bd2eec4b5c07cc6201ace28d52692148cfa441ef1462f1e04cc269464`，同设备data保持。16已完成COM3下载（`stall/hil-channels16/result.json`全部成功marker）和RTS启动（`stall/boot-channels16/serial.txt`含FINALINIT PASS、SRAM-DMA2D、显示触摸初始化）。复测：连接Wi-Fi→CHANNEL STATS说明页→DISCONNECT & SAMPLE→真实统计→START AGAIN；返回NETWORK后需手动重连。15连接证据为用户实测反馈，不外推16切换通过。无提交、推送或SDK修改。

## 当前修复：CONNECT点击后表单空白（0.1.0+15已安装）

用户进一步澄清14的现象是“没有失败，点击connect没反应然后输入密码栏空白”，不是已经得到Connect failed错误。当前源码在async受理/拒绝后均清空输入，受理后的提示追加在较长表单底部，缺乏可见状态。chip提交函数先复制SSID和密码到独占请求，再返回；不能仅因UI清空推断发送空密码或SDK故障。失败采集`stall/connect14-failure.raw`55秒0字节，没有射频错误证据。

`gpt-5.6-luna`最小修复：同步拒绝保留masked输入及重试能力；安全网络空密码在UI拒绝；受理后清旧表单，进入独立WI-FI CONNECTING页，显示SSID/30秒期限/CANCEL，再绑定ticket与generation，避免换页误取消。另修正清屏后访问旧输入对象的UAF，改清屏前清理。现有LVGL host验证提交拒绝、再次提交、进度页、取消及离页通过。

增量构建`stall/build-connect15.log`、包校验`stall/verify-connect15.log`通过；8MiB direct镜像SHA256 `2df4e4b4ff1a83728382bea9748072ac317f035f68121a295eb8d1eba979b154`，data与同设备基底逐字节一致。候选包含已完成报告增强，USB/ADC/相机保持关闭；COM3下载成功marker齐全（`stall/hil-connect15/result.json`）；RTS启动包含FINALINIT PASS、SRAM-DMA2D和显示触摸初始化（`stall/boot-connect15/serial.txt`）。当前待用户从UI重新提交并观察进度/结果；启动通过不代替连接通过。未提交或推送。

## 当前检查点：Wi-Fi连接（0.1.0+14，2026-09-11）

- 复用 `gpt-5.6-luna` 实现连接候选，根代理负责COM3独占构建、下载和启动；现有host已验证输入、取消、离页旧结果隔离。连接仅临时生效，不持久化密码。
- `stall/build-connect14.log`增量构建通过；`verify-connect14.log`校验通过；direct 8MiB镜像SHA256 `5010b120ad6e4deeed16716ceb7f0f95c66ed5a4c414dd0044660a3e2abdfdbc`。同设备data逐字节保持，TF未擦写，非签名OTA。
- 当前枚举COM3 CH342 A/MI00与原设备一致。`stall/hil-connect14/result.json`全部成功marker通过；RTS后`stall/boot-connect14/serial.txt`包含FINALINIT PASS、SRAM-DMA2D与显示触摸初始化。已安装不等于实际Wi-Fi连接通过。
- 最小物理步骤：NETWORK → SCAN WI-FI → 自有热点 → CONNECT，板端输入密码，检查IP/网关并返回首页再进入NETWORK。已请求操作，真实结果待回报；`stall/connect14-acceptance.raw`首次55秒被动窗口为0字节，采集已结束，不将零日志判作连接失败或假称仍在监听。不在会话或日志输入密码。
- 后续源码已增加连接状态报告并通过现有LVGL host测试，尚不在14内。USB-A原生信号与CH342 Type-C分离已由板README确认，VBUS供电策略仍缺证据，Host/ADC/相机保持关闭。未提交或推送。

### 后续工具源码（未安装，2026-09-11）

- `gpt-5.6-luna` 继续修改现有UI及host harness：连接成功页可显式保存真实链路状态；Wi-Fi/BLE列表可保存本次有界结果，注明发现/显示/截断数量，报告缓冲不足保留完整行并注明遗漏条数。复用原后台保存线程，不保存密码。根代理发现正文/换行/NUL容量边界后最小修正，现有harness增加exact-fit与保护字节验证通过。
- USB Host信息页仅在 `CONFIG_BK7258_USBHOST` 生效时出现，使用公开只读快照显示枚举状态、VID/PID、设备类、速度及描述符有效性/截断；手动刷新，无周期清屏、无初始化和供电副作用。现有真实LVGL host覆盖已连接、未初始化、未连接、枚举失败、忙及离页不重绘。USB MSC挂载、受控复制的产品入口尚未接入，不能以此页宣称USB文件交换完成。
- 本批host命令为 `make -C tests/host/dolphin ui-check`，代理实际执行并返回 `DOLPHIN_UI_HOST_PASS`。完整当前配置增量构建通过：`stall/build-tools-next-final.log`。USB宏启用的UI翻译单元检查通过：`stall/usb-ui-target-compile.log`（存在原录音状态文本的有界snprintf截断警告，非零诊断通过）；Host驱动最终翻译单元零诊断：`stall/usbhost-final-target-compile.log`。这些不是启用USB的完整ELF或射频/USB物理验收。
- 板上保持14等待实际连接反馈；后续源码未打包安装，无提交或推送。USB-A VBUS原理图/现有U盘条件已向用户询问；相机RGB引脚冲突、ADC P12所有权与阈值验证继续独立待处理。无需重做已通过的13文件保存基线，修改后的功能仍需对应版本验收。

## 当前检查点：FILES修复（0.1.0+13）

**已修复故障记录：** 用户报告0.1.0+12点击FILES卡死。COM3 `free`仍正常返回，证据`stall/files-stall.raw`，不能判定CP整体死机。AP实际默认pthread栈2048B；FILES扫描线程未设attr，scan及嵌套path校验各有3×192B路径数组再叠加stat/FAT调用，栈余量明显不足，作为首个可区分假设。先仅将扫描线程显式设16KiB，与已有preview/report存储线程一致；不同时改目录渲染。录音和报告实板闭环暂停于此，不能计通过。 实际目标对象反汇编确认scan栈帧712B、嵌套verify栈帧704B，合计1416B（尚未计libc/FAT），见`stall/files-stack-before.txt`。0.1.0+13显式16KiB扫描栈候选已构建及校验，SHA256 `af12ab9a55744ac370b7c8be5a6b0beccd6582f89ddece8e2aaa5a38634152f8`；同设备data保留，COM3下载及RTS启动通过（`stall/hil-files-stack/result.json`、`stall/boot-files-stack/serial.txt`），已请求真实FILES→HOME→FILES复测并开启55秒采集；结果未确认。该候选同时包含已完成主机及目标构建的AP32扫描改动；文件故障修复不改64项渲染策略。


- 无线详情及报告由明确指定的 `gpt-5.6-terra` 代理实现并运行现有 `tests/host/dolphin` 的 `check ui-check`：有界快照、详情页、显式 `SAVE REPORT`、独占创建、写入/同步/关闭错误及旧页面完成隔离。报告位置 `/mnt/tf/dolphin/reports`，经现有 Files 浏览；未自动覆盖或删除。
- 0.1.0+11 增量构建、delivery 校验及同设备 data 逐字节保留通过。8 MiB SHA256 `3fb93aab20187b827ed56340bd638a410396462fe993aa94a27db8ca0db306ac`；COM3 下载与 RTS 启动通过，日志确认 `FINALINIT PASS`、`render=sram-dma2d` 和显示/触摸初始化。证据工作区 `out/dolphin-t5-wireless-20260910/stall/{build-reports.log,verify-reports.log,image-reports.json,hil-reports/result.json,boot-reports/serial.txt}`。已请求用户执行真实扫描→详情→保存→Files 打开，尚无该流程实板通过结论。
- 录音源码复用现有 `bk7258_agent_media_recorder.c`/NuttX audio，以 `DOLPHIN_RECORDER` 单独接入，不开启语音助手。实际候选构建通过（`stall/build-recorder.log`），公共 media_recorder 头与链接有效；尚未安装。现有主机 harness 覆盖 WAV 头、STOP、ENOSPC、线程失败 FD 所有权与关闭失败防重入。上板前补充读长度边界和 UI 错误保留；不得以 mock PCM 代替真实录音。
- 0.1.0+12 录音候选最终增量构建、有效 `DOLPHIN_RECORDER`/MIC 配置及 delivery 校验通过。8 MiB SHA256 `da0c89cc4a1062ed77795a1ad4f1bf5bc9bae502d851a4e81f89eb5a505bf1fa`；同设备data保留，COM3 HIL与RTS启动通过，显示/触摸仍走DMA2D。证据 `stall/{build-recorder-final.log,verify-recorder.log,image-recorder.json,hil-recorder/result.json,boot-recorder/serial.txt}`。已请求用户从 RECORDER 开始真实说话并停止保存；未获录音实测结果，不能写成麦克风闭环通过。
- 后续AP本地Wi-Fi32条源码和定向主机测试通过，CP wire保持4条；有界选择算法复用，新增约1552字节chip缓存及1344字节UI快照。最终目标构建进行中，尚未下载；当前实板仍为12，不用主机结果冒充32条射频实测。
- USB Host 的标准 HCD、waiter 和 MSC notifier 可复用，但当前板资料未确定原生口 VBUS 控制/供电安全条件；只阻塞相关硬件启用，不阻塞其他功能。摄像头装配及可用于验收的现有 USB 外设待用户说明。无 NFC 等缺失模块入口。

### 后续源码候选（未安装）

- 安全类型可读名称及未知值保留数值：既有Wi-Fi/UI主机测试与目标增量构建通过，`stall/build-security-label.log`；实板13尚未包含此后续名称改动。
- USB Host只读枚举快照：复用现有GET_DESCRIPTOR真实返回，不主动新增USB传输；主机短包/截断/generation用例通过，高风险生命周期复核仍在修正，未启用Host及VBUS、未目标构建、未实测。
- USB快照后续已补真实C运行用例（共4项，代数跨priv重置/饱和拒绝/snapshot忙返回/真实描述符边界），完整driver目标编译器检查通过：`stall/usbhost-final-target-compile.log`；属于显式USB宏下的翻译单元检查，不是启用Host的ELF或物理验收。
- 单键ADC消费：通过标准ADC设备后台采样及有界事件队列驱动LVGL手势，默认关闭。host手势逻辑通过，不代表ADC实测；配置阈值未确定时拒绝输入。现有CP `GPIO_LOWERHALF` 已占P12，启用SARADC前须解除该互斥，并配套CP SARADC_SERVER；本批尚未改板配置。RPMsgFS现有ioctl表不支持GPIOC_READ，不能简单把CP `/dev/gpio1`路径当作AP可用按键接口。

- 临时Wi-Fi连接UI退出密码页的已释放对象访问已修正：先清除密码引用再删除LVGL对象；提交后禁用输入，离页取消仍消费旧事务但不重绘。修复后目标构建 `stall/build-connect-fixed.log` 通过，现有真实LVGL主机回归 `stall/ui-connect-recheck.log` 通过。尚未安装或实测连接；USB与ADC也保持未启用，当前板仍是13。

- 相机切换核对：现有 `bk7258_lcd_setpower()`只调用显示使能及背光回调，不释放RGB引脚；因此不能将FB关屏等同于安全DVP切换。触摸与SCCB还共享P13/P15，需同一I2C生命周期协调。未改板互斥配置，未在RGB主版启用相机。

### 0.1.0+13实板反馈

用户在下载及启动后回复“可以了”，确认本次FILES卡死修复。55秒只读窗口没有新字节（`stall/files-stack-acceptance.raw`），因此证据是用户物理确认，不虚构扫描完成日志。栈余量不足是本次最小修复依据；未获得溢出异常栈，不声称有完整崩溃回溯。下一步保持13验证报告保存及真实录音；未变化的文件访问已恢复不需再次扩大排障。

### 2026-09-11 报告与录音实板确认

用户在明确询问“扫描报告保存成功、录音停止后生成WAV，且两者都能在Files中看到”后回复“确认”。据此将这三项正常路径记录为T5-Board 0.1.0+13用户实板验收通过；连同此前Files不再卡死的确认，完成本批保存及浏览正常路径。对应已安装镜像SHA256：`af12ab9a55744ac370b7c8be5a6b0beccd6582f89ddece8e2aaa5a38634152f8`，下载/启动证据沿用`stall/hil-files-stack/result.json`与`stall/boot-files-stack/serial.txt`。

本次功能证据为用户明确反馈，未新增串口日志、文件读回或音频解码证据；不外推音质、WAV内容完整性、异常恢复、长稳或USB复制通过。后续候选连接UI、USB后端与ADC未安装；后续先推进Wi-Fi连接实板闭环，USB仍须核实供电与实际外设条件。本次仅更新验收记录，未刷写、提交或推送。

### 2026-09-11 文件交换后端检查点

`gpt-5.6-luna` 实现受控SD/USB单文件复制后端及默认关闭的构建接入；未增加假可用菜单，未启用USB供电。根代理针对线程生命周期确认4KiB局部缓冲不能使用AP默认2KiB栈，已改显式16KiB及创建前detached属性，属性/创建失败无文件副作用。路径文件系统检查在worker执行，不阻塞UI入口；独占创建禁止覆盖，取消/失败保留并标记不完整文件，不自动删除。

现有host测试通过（`stall/copy-final-host.log`），实际AP编译器翻译单元检查零诊断（`stall/copy-target-compile.log`）；不是已启用的ELF或USB实测。CMake/Make共用默认关闭`DOLPHIN_FILE_COPY`，存储根配置不依赖NSH命令目录。当前板仍为13，报告保存和真实录音待物理验收；USB供电及现有外设条件未确认，相机与RGB引脚冲突仍未解决。未提交、推送或重新刷机。

## 前一批无线第一版记录


最新实板反馈：用户确认进入 CHANNEL STATS 后黑屏卡住；0.1.0+6该功能验收失败。COM3被动15秒、只读状态命令12秒及COM4被动10秒均0字节，两CH342接口仍枚举OK。用户说重启后的50秒被动窗口也没有字节，因此未捕获崩溃栈或复位事件，不能仅凭零日志判定CPU死机。证据`out/dolphin-t5-wireless-20260910/stall/`。

当前最小假设：chip监听适配先set_channel再monitor_start，与固定SDK自带CLI的register_cb→monitor_start→set_channel相反。SDK CP的monitor_start初始化RW driver，而set_channel直接进入rwnxl_reset_evt/rw_msg_set_channel。已仅在chip调整顺序，并在设信道失败时停止监听，停止失败保留占用供重试；未改SDK。该差异有源码依据，尚不能称为黑屏唯一根因。

0.1.0+7候选增量构建及现有Wi-Fi两项主机生命周期测试通过；delivery校验通过，8MiB镜像SHA256 `327fbf15d8ac6bd9ab82a4edeff4115acada60cafa0ff56955367a51347f9340`，data与同设备有效读回逐字节一致。证据`stall/{build-order.log,verify-order.log,image-order.json}`；COM3 HIL下载通过（`stall/hil-order/result.json`），RTS后SYSINIT/FINALINIT及显示/触摸初始化通过（`stall/boot-order/serial.txt`）。已开启一次55秒真实CHANNEL STATS操作采集（`stall/channel-order.raw`），功能结果待判定。目标恢复执行，非等待旧验收的阻塞状态。

实板结果补记：0.1.0+7串口`stall/channel-order.raw`记录`wifi-diagnostic: channels=1 status=0 count=13`，用户明确确认正常出结果、不再卡死。该单次成功不代替多轮切换或BLE验收。

下一问题：CHANNEL STATS列表无法滑动。现有GT9xx overlay每次DOWN后无条件合成UP，不能表达持续拖动；修正为ready报告中有触点时首次DOWN、持续MOVE，触点数零才UP；无ready/无事件返回EAGAIN，且在I2C前消费pending防覆盖新IRQ。不改SDK或LCD。0.1.0+8增量日志确认实际重编gt9xx.c；生产触摸报告函数经I2C边界mock验证DOWN/no-ready/MOVE/UP/同位置重按/I2C失败，证据`stall/{build-touch.log,touch-state-test.log,touch-state-harness.c}`，不代替实板手指拖动。

0.1.0+8候选8MiB SHA256 `a24f264e8c09d85b9d66a04f78fb17c7483027332fdf0512ef519ec8095001da`，delivery校验及data逐字节保留检查通过（`stall/{verify-touch.log,image-touch.json}`）。COM3下载通过（`stall/hil-touch/result.json`），RTS后FINALINIT及显示/触摸初始化通过（`stall/boot-touch/serial.txt`）。后续只复测真实点击/持续滑动/抬手和信道统计回归。未提交、未推送。

0.1.0+8物理结果：用户确认能够正常滑动，连续触摸修复通过该单次验收；新增反馈是滑动时持续闪烁。核实此前最终AP config.h未启用FB_SYNC，RGB驱动因此只有单帧缓冲，LVGL采用direct模式直接写扫描内存。0.1.0+9仅在T5 AP配置启用已有CONFIG_FB_SYNC，使现有chip EOF中断翻页/双缓冲路径生效，增加307200字节PSRAM；不改SDK、LCD时序或UI。最终AP角色`bk7258-role-0d0371f13e3927f8`已启用FB_SYNC。该闪烁假设待物理复测，不提前宣称消除。

0.1.0+9构建/完整性校验/data保留检查通过：`stall/{build-double.log,verify-double.log,image-double.json}`；8MiB SHA256 `3aa12ecee59b77630dfa711104a5636b52cf8155d7a841a31007fe655ce133b1`。COM3下载通过（`stall/hil-double/result.json`），RTS后FINALINIT、RGB LCD及显示/触摸初始化通过（`stall/boot-double/serial.txt`）。尚待用户确认同一列表滑动是否仍闪烁；不把初始化日志当作物理画面通过。

0.1.0+9物理结果：用户确认滑动闪烁已修复。随后反馈帧率观感偏低；核实LV_DEF_REFR_PERIOD=33，仅表示理论刷新调度上限约30fps，未测实际FPS。现有Dolphin走generic NuttX fbdev direct，在PSRAM中软件绘制/同步；仓库chip已具备SRAM partial+DMA2D搬运与EOF等待的公开适配。

0.1.0+10仅将Dolphin显示初始化接到已有bk7258_lvgl_fb_create/bind_touch（编译配置门控，创建失败保留generic fallback并记录路径），T5启用既有FB_ACCEL，最大宽度320；保持33ms以隔离绘制路径变量。未改SDK、LCD时序或触摸。最终AP ELF SHA256 `a0923d9841e032d45fd38d9de7d6301eb45dc73ca864ce0b1f314c8537c30c8b`，实际config.h确认DMA2D/FB_ACCEL/FB_SYNC及33ms。普通显示路径现有LVGL UI回归通过，不代替新加速分支硬件验收。

证据`stall/{build-dma.log,ui-dma.log,verify-dma.log,image-dma.json}`；0.1.0+10完整镜像SHA256 `011336918e73059663ca48bde563f3cb6fadda93e108b291565ae09f7725c47a`，data保持一致，COM3下载通过（`stall/hil-dma/result.json`）。新启动日志`stall/boot-dma/serial.txt`确认FINALINIT PASS、render=sram-dma2d、显示/触摸初始化通过，未走fallback。实际同页滑动观感及FPS尚待实测；不宣称达到30fps或虚报提升比例。未提交、未推送。

范围固定为Wi-Fi热点列表、被动信道统计、BLE广播列表；不增加NFC/Sub-GHz或恢复专项。

| 项目 | 源码/主机 | 当前版本实板 |
| --- | --- | --- |
| Wi-Fi热点列表 | AP worker异步请求、4项有界结果、停止/结果消费通过 | 0.1.0+4旧版CLI曾发现17项；本版待验收 |
| 被动信道统计 | AP本地结果13项，每信道300ms，仅帧/字节统计；停止/重启和失败清理主机通过 | 待验收 |
| BLE广播列表 | 16项×31字节；按地址/类型/事件去重；10秒窗口；独立CPU0异步HCI操作 | 待验收 |
| UI停止/离页/再启动 | 实际LVGL主机回归通过 | 触摸及连续切换待用户操作 |
| 资源互斥/失败 | 复用radio_mode；停止失败保留占用，重试停止确认后再释放；生产适配源码编译mock通过 | 正常停止/跨功能切换待实测，硬件故障路径未实测 |

- 不调用命令main、不解析终端输出；信道数组、BLE数组仅AP本地，不扩大RPMsg消息。
- T5 CP开启现有BT Controller IPC，AP开启现有NuttX Host；未为扫描引入N13 GATT产品服务。BLE最初候选使用LPWORK存在HCI接收依赖风险，已在安装前改为CPU0线程，未刷入问题候选。
- 新代码：`chips/bk7258/ap/bk7258_ble_scan.c`及公开头文件；Wi-Fi接口/worker、Dolphin UI、T5两核配置和chip CMake/Make做必要集成。原GATT文件未改，分层例外未放宽。
- 主机证据：`out/dolphin-t5-wifi-20260910/{channels-async-test.log,ble-adapter-test.log,wireless-ui-test.log,build-wireless-final.log}`。BLE测试实际编译生产适配源，mock HCI/线程/射频边界，覆盖启动失败、foreign scanner、启动中停止、停止失败重试、重复/超限广播，不是字符串检查，也不是射频验收。
- `out/dolphin-t5-wireless-20260910/dolphin-wireless-full.bin`，8MiB，SHA256 `073c6df5c8830a02d494ab8a5eb1e305ad9d7e5fbf4ee1b6894b3c1186f8ca0d`。direct开发包0.1.0+5，非签名OTA；已验证data与前次实际读回一致。打包/校验及烧录通过；其后发现CP堆不足，已由下述0.1.0+6替代。
- 后续采集步骤：NETWORK中依次扫描Wi-Fi→停止→再扫，CHANNEL STATS→停止→再采样，BLE BROADCASTS→等待广播→停止→再开始；每页返回HOME，连续切换至少三轮。记录真实结果和串口摘要；操作尚未发生时不得填写通过。
- 0.1.0+5实板启动出现CP 4096字节分配失败（free1512/maxfree1120），旧版本不得标记健康通过，见`out/dolphin-t5-wireless-20260910/boot/serial.txt`。T5 CP原只启用PSRAM驱动；本轮加入既有role-local系统堆96KiB及SDK SRAM专用堆16KiB（CP预留范围0x60700000+128KiB，未与AP重叠）。未改SDK实现。
- 0.1.0+6堆配置修复构建和delivery校验通过，镜像`dolphin-wireless-heap-full.bin` SHA256 `75945b157188ac8d1e99b6a0a119b4954ddd7c8886df36e840959813da63d00e`；保留data，COM3安装通过。新启动日志未再出现分配失败，SYSINIT/FINALINIT及UI初始化通过；CP Umem free83232/maxfree81872，SDK SRAM free15800，data为LittleFS。证据`build-cp-heap.log`、`verify-heap.log`、`image-heap.json`、`hil-heap/result.json`、`boot-heap/serial.txt`、`acceptance.raw`及`result.json`。45秒采集没有诊断完成marker，未发生的真实操作不计通过。
- Wi-Fi扫描停止/注销失败保留射频占用并可重试的生产cleanup函数另经编译mock验证；`wifi-lifecycle-test.log`两项通过。没有设备故障注入，不外推硬件故障恢复。
- 已向用户发出三页真实操作请求。当前串口采集已结束，不假装仍在监听；用户准备操作时重新开启采集。未提交、未推送。当前目标未完成。

## 2026-09-10 UI更新与授权清空data（当前恢复点）

### 历史Wi-Fi扫描增量（0.1.0+4）

- `chips/bk7258/common/bk7258_wifi_control.c`及公开头文件：AP异步scan/poll复用现有CPU0工作线程及CP/连接事务的busy互斥；完成结果一次性消费，连接poll不能取走扫描结果。保留SDK后端与CP扫描协议。
- `app/dolphin/dolphin_ui.c`：NETWORK → SCAN WI-FI；LVGL定时消费结果，显示发现总数、最强最多4项、信道及RSSI；扫描期间可回HOME，离页后仍消费完成但不重绘。失败可重试；SSID按当前字体将非ASCII字节替换为问号。未实现连接/密码输入。
- 复用现有 `tests/host/bk7258/test_wifi_async.py`和`tests/host/dolphin`，补充扫描忙碌、错误票据/错误结果类型、完成消费、投递失败、界面退出、显示和失败重试检查。主机通过不代表真实射频或触摸通过。T5增量构建、layers、delivery校验通过。
- 工作区证据：`out/dolphin-t5-ui-tools-20260910/{build-wifi.log,wifi-ui-test.log,wifi-async-test.log}`、`out/dolphin-t5-wifi-20260910/{layers.log,verify.log}`。
- 为防旧data恢复，COM3仅读回0x600000起1MiB；以实际已安装0.1.0+3完整镜像加本次data读回组成同设备基底。原包不覆盖；新包data逐字节一致。`data-read.log`、`base-composition.json`和`accepted-base.json`记录来源。
- 新direct镜像 `out/dolphin-t5-wifi-20260910/dolphin-wifi-full.bin`，8MiB，SHA256 `d7d8525444af545a336026946e5c5323f7419d90d99422a0ae86407802700976`。COM3烧录通过；RTS后FINALINIT PASS、TF及显示/触摸初始化通过，data仍是LittleFS。真实CP命令扫描status=0、发现17项、返回4项、截断13项；不能替代AP触摸入口验收。证据`hil/result.json`、`boot/serial.txt`、`runtime.raw`、`scan-runtime.raw`及`result.json`。初次多命令采集只记录到扫描命令部分回显，后续单命令采集取得完整成功结果；不把首次缺尾日志当成扫描失败。物理UI等待用户确认；无新私钥、OTP、提交或推送。

- 已安装 `out/dolphin-t5-ui-tools-20260910/dolphin-ui-tools-full.bin`，8388608字节，SHA256 `29fa7ac2b65f9622db84ba58a4107f583e8fb974b162242c398403ac5685e78a`。direct开发包标识0.1.0+3，不代表签名计数或OTA能力；COM3 HIL下载通过。
- UI加入卡片布局、网络刷新及异步只读ASCII文本预览（最多2048字节）。主机实际LVGL回归、最终增量构建及delivery校验通过；本版实板仅确认启动及显示/触摸初始化，不能沿用上一版用户确认充当新交互验收。
- 用户明确授权删除data现有1 MiB旧内容。核实 `/dev/mtdblock0` 为1048576字节且未挂载后，一次执行 `mount -t littlefs -o forceformat /dev/mtdblock0 /data`。只格式化data；未修改启动脚本为自动格式化，未清理TF及其他分区。
- 格式化后以及COM3 RTS重启后，均显示 `/data type littlefs`；4096字节×256块，共1 MiB，已用2块、可用254块。`ls -l /data`仅有点目录。重启日志出现 `BK7258 FINALINIT PASS`，TF正常挂载，Dolphin显示/触摸初始化通过。最初 `ls -la`不受NSH支持，已改为 `ls -l`重验。
- 证据均在工作区 `out/dolphin-t5-ui-tools-20260910/`：`ui-host.log`、`build-final.log`、`hil/result.json`、`data-before.raw`、`data-format.raw`、`data-after.raw`、`data-reboot/serial.txt`、`data-reboot-status.raw`。
- **后续全量下载约束**：历史 `out/dolphin-t5-flash-20260910/t5-current.bin` 和本次下载镜像均包含格式化前的data；保留原文件不覆盖，但不得再将其data作为当前有效数据回写。后续需保留当前设备data或使用经核实不覆盖该区的路径；不能把现有全量传输冒充局部更新。原包尚未进行恢复演练。
- 本轮未提交、推送、操作密钥或OTP。下一步验收新版界面/文件预览，再推进实际网络工具；不将本轮清空及启动通过称为小海豚完整功能完成。


## 完整工具体验的推进边界（2026-09-10）

### 与Flipper Zero及板载外设的实际差距

当前不是Flipper Zero功能等价实现，也未完成全部T5外设的产品接入。官方功能参考：https://flipper.net/ （2026-09-10核对）。

| 能力 | 当前证据与缺口 |
| --- | --- |
| 屏幕、触摸 | ILI9488/GT1151已接入；上一版触摸用户确认，新版界面交互仍待验收 |
| TF、内部data | TF挂载及只读浏览已接入；文本预览主机通过；data格式化后重启挂载通过，产品设置持久化未接入 |
| Wi-Fi | 0.1.0+6已接入触摸热点扫描及被动信道统计；当前版本真实触摸验收待完成，连接未实现 |
| BLE | 0.1.0+6已有有界广播列表、启停及10秒窗口；真实广播结果和触摸切换尚待验收，连接未实现 |
| 麦克风、扬声器 | T5已有板级音频绑定；Dolphin录音/播放工具尚未接入和验收 |
| 按键、背光、LED | 板级已有相关定义；产品控制未闭环，需遵守ADC键/数字键和控制台共享引脚约束 |
| USB、GPIO/UART/SPI/I2C工具 | 串口下载成功不代表USB设备功能完成；扩展工具、引脚占用保护及用户入口未完成 |
| 摄像头 | 板级DVP绑定存在，但RGB屏与DVP共享多组引脚且配置互斥；不能在当前屏幕配置直接同时启用，也未确认实接相机 |
| NFC、125kHz RFID、Sub-GHz、红外、iButton | Flipper有对应专用硬件；本T5无NFC，其余没有已确认前端/接线，不能只靠软件宣称等价 |

依据：`boards/bk7258/t5_board/include/bk7258_board_config.h`、板级bringup及CP/AP defconfig、`app/dolphin/dolphin_ui.c`、`app/bk7258/bk7258_wifi_main.c`（扫描命令）与本页实板证据。当前先验收三项已安装无线工具，不扩展外设或接入互斥摄像头。

用户要求继续复刻完整功能并提升UI。以T5实际硬件为边界，不把Flipper固件直接移植或伪造射频能力：

1. 触摸桌面/文件工具：深色卡片、清晰导航、TF目录与文本预览；先安装可用增量。
2. 网络工具：复用AP标准网络接口及现有Wi-Fi/BLE栈，状态、刷新后再接扫描/连接；不以已有地址页冒充扫描完成。
3. 本地工具：按已声明硬件逐项接音频、受控GPIO及USB；每项先核对共享引脚和标准接口，再做实际交互。
4. 设置和设备维护：先闭合数据存储、设置持久化，再接签名升级及恢复；当前direct包不是OTA能力证明。
5. NFC、红外、Sub-GHz、125kHz等扩展：T5当前没有已确认的对应前端，需实际模块/接线才能验收，不显示虚假成功菜单。

/data原缺口已按本轮明确授权闭合：0x600000–0x700000旧内容已格式化为LittleFS并通过重启验证，详情见当前恢复点。历史读回仍原样保留。

## 2026-09-10 触摸无响应修复（历史0.1.0+2证据）

用户确认上一版桌面可见但触摸无响应。实际AP编入团队维护的`nuttx/drivers/input/gt9xx.c`，不是官方checkout；该版本缺少O_NONBLOCK空闲EAGAIN保护，空样本持续返回成功，LVGL的continue_reading会不断读下去。恢复非阻塞保护并允许待发送的合成TOUCH_UP通过；不修改IRQ接线、显示、SDK或/data。

本轮增量仅重编GT9xx对象和相关链接，复用同板原始备份及校准/配置保持策略；完整包校验、COM3 RTS全量下载及启动marker通过。当前镜像SHA256 `c60b80fffa5a94e02ee58dba6e1374c52c52a339b6334d9b570eb36b899b35b4`，direct包标识0.1.0+2。证据`out/dolphin-t5-touch-20260910/result.json`、`driver-change.patch`、`build.log`、`hil/result.json`、`boot/serial.txt`。

**实际触摸已通过用户确认**：本轮用户回复“可以了”，确认已安装的0.1.0+2修复版点击恢复。该确认仅覆盖触摸恢复，不外推长稳、滑动、多点或其他外设。/data失败仍在日志中，保持独立待处理；所有端口已释放，无新密钥、OTP、提交或推送操作。

## 2026-09-10 M1全量安装（最新实板恢复点）

- 用户明确授权COM3全量下载及RTS。先保存本板8 MiB读回，通过现有accept-base、unsigned delivery及verify delivery合成完整镜像；校准/设备区、usr_config和persistent_data与基底逐字节一致。只清理策略定义的reset_marker，不格式化文件系统。
- `out/dolphin-t5-flash-20260910/dolphin-t5-full.bin`，8388608字节，SHA256 `061f930da0166a1d249718256ca159cc8950be0931d412ab461c5ba044e7e2d7`。包标识0.1.0+1只是direct开发包标识，板端uname仍0.0.0，不冒充签名计数升级。
- HIL single/direct-full，COM3/6 Mbaud/RTS，仅一次写入。Loader最终WriteFlash、Writing Flash OK、All Finished Successfully及HIL结果通过；内部寄存器重试/分段重连保留原始日志，不当成整轮重刷。
- RTS后启动串口显示LCD ili9488 320x480、`dolphin-ui: display and touch initialized`、TF `/mnt/tf`挂载；随后uname/ps/mount正常响应。
- **未通过项**：CP有`mount failed: 14`及`FINALINIT FAIL: persistent data at /data has type 00009fa0, expected 0a732923`，后续CP挂载表只有/etc、/proc。不能称整机健康通过；未确认真实屏幕和触摸效果。下一步定位/data挂载，不自动格式化或再刷。
- 证据：同目录`result.json`、`hil-run/result.json`、`hil-run/bkloader.txt`、`boot/serial.txt`、`post-flash-status.raw`。原板读回`t5-current.bin`留作设备专属恢复材料，不公开提交；恢复写回尚未演练。
- Loader read仅支持工具目录纯文件名，并生成`*_dump_TIMESTAMP_0x0_0x800000.bin`；初始UNC/绝对路径失败及参数拒绝日志保留，不再按预期文件名假定读取成功。未生成/使用私钥、未操作OTP、未提交推送；串口已释放。

## 2026-09-10 T5连接核实（M1尚未安装）

- Windows实际枚举COM3=CH342 A、COM4=CH342 B。COM3在115200回应help、uname、bkota status和ps；确认是运行控制台，历史下载角色本轮未执行验证。COM4同波特率空闲及help采集均0字节，不能确认用途，也不能据此判死机。
- 实板uname为t5_board，旧版`18.6.98+158`，counter=158，A槽confirmed；AP READY/error=0，CPU2 ready=1/error=0，supervisor faults=0/recoveries=0。这是旧版启动健康证据，不是Dolphin M1验收。
- 当前M1候选为unsigned direct构建，尚未建立与此已安装签名系统兼容的安装产物；不能把它当签名OTA直接更新。下一步仅准备匹配本板和明确写入范围的安装路径，不重新调查AIDK418或生成密钥。
- 证据`out/dolphin-t5-connect-20260910/result.json`及`com3-status.raw`。全部串口采集已结束并释放。未复位、刷写、读取OTP或使用密钥。空闲采集工具在WSL UNC路径写summary失败，但raw文件存在且为0字节；不把该工具提示当成板端错误。

## 2026-09-10 M1 触摸桌面（当前恢复点）

- `app/dolphin/dolphin_ui.c`：AP上独立LVGL任务，复用 `/dev/fb0`、
  `/dev/input0`；首页、只读文件浏览、目录上下级/详情/返回、网络与设备信息。
  固定HOME导航，文件枚举放在工作线程，主线程消费结果；缺卡可重试。
  仅浏览板级已挂载的 `/mnt/tf`，不挂载、格式化或写卡；单次最多显示64项、
  检查256项，跳过符号链接。网络读取标准getifaddrs，接口启用不等于已联网。
- `app/dolphin/{Kconfig,CMakeLists.txt,Make.defs,Makefile}`及
  `app/bk7258/{bk7258_product_lifecycle.c,Make.defs}`接入已有唯一AP产品启动回调；
  T5 AP配置开启Dolphin UI、LVGL/NuttX触摸及16 KiB任务栈。
  未启用UIKit板级循环或旧显示服务，避免多个LVGL所有者。
- CP继续复用已有Dolphin命令目录；未更改驱动、芯片实现、产品信任关系。
- 验证：`make -C tests/host/dolphin check`、同目录`ui-check`、
  `python3 tools/bk7258/bk7258.py verify layers`通过。
  UI检查直接运行生产UI源码和真实LVGL软件渲染，使用主机临时文件目录，
  覆盖缺卡重试、目录上下级、详情返回、链接过滤、条目限额、横竖屏及固定HOME。
  NuttX设备和任务适配未运行；这是主机证据，不能替代实际触摸事件验收。
- 最终构建命令：`python3 tools/bk7258/bk7258.py build --board t5_board
  --boot direct --workspace /home/lijian/project/open-vela/out/bk7258-plan-validation
  --product dolphin --jobs 8`。无clean、私钥、签名或设备操作。
  AP ELF确有`dolphin_ui_start`、`dolphin_ui_task`、`lv_nuttx_init`，
  实际配置启用DOLPHIN_UI及应用生命周期。AP ELF SHA256：
  `a932c71e3ca9e7aac9c71d1e37b9717a0427ab3b86b78e158fedd2fb41593fba`。
- 工作区证据：`out/dolphin-t5-m1-20260910/result.json`、`build-ui-final.log`、
  `ui-host.log`、`cli-host.log`、`layers.log`、`ap-symbols.txt`及四张PNG主机截图。
  manifest所列最终文件大小/哈希已逐个核对。先前`build.log`/`build-ui.log`
  未包含UI，不能作本次UI构建通过的依据；最终使用新的AP配置身份。
- 下一步：用户连接T5后核对实际板型、串口、固件和安装路径，再验证启动、
  显示方向、触摸命中、反复返回/滑动、缺卡与有卡浏览、网络状态刷新、资源稳定性。
  当前不访问AIDK COM8，不借用AIDK验收；真实板级均标记“未验证”。
- 本轮新代码未提交、未推送；已有用户修改保留。M1不代表Flipper全功能完成。

## 2026-09-10 配置入口收束

用户明确要求每块板各一份应用配置，并非三板合并一份。
三板正常入口均为 `configs/app/defconfig`，CP配套保留各板现有
`configs/openvela_ap/defconfig`，由 `openvela.conf` 选择。
T5-Board app选择Dolphin、AIDK app保留Shaniu、T5AI-Core保留原功能。
移除重复的dolphin_cp/ap目录和旧openvela_cp入口，不新增公共片段、解析器或CLI。
下文旧路径仅为过程证据；当前命令为 `bk7258.py build --board t5_board --boot direct`。
源码组织参考本地OpenVela Vendor文档、ESP32-S3板配置、BES独立核配置。
验证：三板通过 `--board <board> --boot direct` 的CP/AP开发构建，构建workspace
回归3项及打包回归13项通过。证据 `out/per-board-app-20260910/result.json`、
各板同名log、`package-tests.log`（工作区路径）。未签名发布、未刷写、未新增实板证据。
AIDK与T5AI-Core新app/defconfig对原CP配置逐字节一致；三板CP/AP兼容标识匹配。
本轮改动仍在本地，未提交或推送。

## 2026-09-10 目标板校正（优先于下文历史记录）

- 小海豚主目标：T5-Board；傻妞仍为AIDK AI Toy。两板数据、信任材料和验收证据独立。
- 用户确认T5-Board无NFC。不要求为首版增加NFC硬件；NFC仅为可选外接扩展。
- 仓库T5-Board配置已有RGB LCD和GT1151；触摸复用NuttX gt9xx及i2c_bitbang，
  标准设备路径 `/dev/input0`。这是源码配置证据，尚不是本次显示/触摸实板证据。
- 新建 `boards/bk7258/t5_board/configs/dolphin_cp`、`dolphin_ap`，从本板
  openvela配置派生，保留本板SDK、Flash布局、LCD和触摸。不得挪用AIDK配置刷T5。
- 工具目录按编译能力裁剪NFC、运动、健康入口；T5首版只注册已有Wi-Fi查询，
  不把AIDK传感器或电池能力迁移过来。主机验证无NFC时即使registry有同名命令也不可启动。
- 新目标交互以触摸菜单为主；不沿用AIDK三键/双眼160×160布局。
- 下一步：在AP侧核对现有LVGL framebuffer与触摸输入接入，落实本地菜单、
  文件浏览及现有网络工具；不重写显示或Goodix协议驱动。
- 开发构建使用T5自身 `bk7258_ab_onchip_persistent.csv` 和 `--boot direct`，
  仅验证编译集成，非签名发布包，不刷写，也不继承AIDK公钥。

- 当前验证：主机测试和T5 CP/AP构建通过；实际配置确认AP启用LCD/GT1151、CP启用Dolphin，两核均未启用NFC。证据 `out/dolphin-t5-20260910/result.json`、`build.log`（工作区路径）。

## 以下为改板前的AIDK M0历史，不能作为T5验收


## 范围与起点

用户要求先提交推送傻妞进度，再启动 Flipper Zero 风格“小海豚”固件，优先复用
OpenVela 软件生态。傻妞进度已发布到 `fork/feat/shaniu-product-417`，
提交 `32d36292bc4c6c5ce9f55d2edc14c4d2ef18667e`；Gateway 冻结历史保留。
小海豚本地开发分支为 `feat/bk7258-dolphin`，不修改已推送分支。

此前曾暂按AIDK AI Toy实现；已由上文用户确认的T5-Board选择取代。
这不是把 Flipper Zero 原机固件直接烧进 BK7258，也不假定两块板的射频能力相同。
新产品独立应用/CP/AP配置；复用芯片、板级、标准外设和已有本地工具。
M0 禁用傻妞云语音、认领、人物记忆和眼睛/拍照业务自动启动，不删除设备持久数据。

Flipper Zero 官方源码包含其 Furi、平台和应用层，使用 GPL-3.0；当前不复制它的
系统层或图像资产。体验与能力分解参考 [官方源码](https://github.com/flipperdevices/flipperzero-firmware)
和 [官方硬件规格](https://docs.flipper.net/zero/development/hardware/tech-specs)。
后续确需引入协议实现时，逐项核对现有实现、接口、来源与许可证。

## 复用清单与真实缺口

路径以本工作区为基准，`contest/` 代表当前提交仓库。

| 能力 | 现有可复用代码 | 本板/产品缺口 |
| --- | --- | --- |
| 应用入口 | NuttX builtin registry + `task_spawn` | M0 仅统一本地工具目录/启动，不重写进程管理或shell |
| 图形菜单 | `apps/graphics/lvgl`；LVGL `src/drivers/nuttx/lv_nuttx_lcd.c`、`lv_nuttx_fbdev.c` | 本板两块160×160 LCD；需做布局与三键输入，不能直接沿用Flipper的128×64布局 |
| 按键/电源 | 标准 `/dev/buttons`、AIDK lower half；417 soft-off候选 | 菜单导航/返回/电源手势待定义；417休眠电流与唤醒未验收，M0不自动启用 |
| NFC | NuttX MFRC522 + 团队frame/ISO-DEP overlay、现有 `bknfc scan` | M0只是存在性查询；不等同读写/模拟所有卡型。MFRC522不能因软件菜单变成Flipper的ST25R3916 |
| 存储/文件 | VFS、FAT、SDIO/MMCSD、MSC租约、既有一致性修复 | 复用128MB级已验收基线；文件浏览/保存界面尚未实现，不格式化介质 |
| 红外遥控 | `nuttx/drivers/rc/lirc_dev.c`、`include/nuttx/rc/lirc_dev.h` | LIRC upper half存在；BK7258遥控收发lower half及发射/接收电路待核对，UART IrDA不等于消费级红外遥控 |
| Sub-GHz | `nuttx/drivers/wireless/cc1101.c`、`CONFIG_WL_CC1101` | 未确认AIDK装有CC1101或外接模块；协议/原始采集能力需另验，先不增加假驱动 |
| 125kHz RFID / iButton | 待按选定前端核对NuttX现有接口 | 当前板级无已确认的相应前端/接线，不宣称具备 |
| Wi-Fi/BLE | 现有BK7258 CP controller/AP NuttX host、bkwifi工具 | M0只提供连接状态查询；小海豚手机协议另定，不继承人物/云凭据要求 |
| USB | 现有CDC/MSC和签名OTA Manager | 复用传输与校验；菜单入口/新产品兼容性/升级验收待做 |
| 电池/温度/运动 | 现有 `bkhealth status`、`bkmotion sample` 和标准驱动 | M0复用查询，后续接状态栏；构建存在不表示传感器物理验收通过 |

## 实施顺序与完成门

1. **M0 独立固件启动基础**：标准应用 `app/dolphin`、独立 `dolphin_cp/ap`
   配置，启动显示工具目录；固定参数调用既有NFC/健康/运动/Wi-Fi查询，正确传播
   缺命令和子进程错误。主机回归与双核构建必须通过；实板启动另列。
2. **M1 屏幕与按键**：用LVGL现有LCD端口做首页/列表/详情/返回，接标准按钮；
   同时证明不与旧眼睛renderer争用屏幕。操作全流程须在屏幕和实物按键上验证。
3. **M2 本地工具闭环**：NFC检测与明确支持的读卡、文件保存/浏览、传感器和网络
   状态；有限操作超时可取消，资源可再次打开，不把缓存值称为实时结果。
4. **M3 硬件扩展**：先确认红外/CC1101/低频前端的实际装配与接线，再适配现有
   NuttX接口缺失部分。没有硬件时保持“未接入”，不做成功占位。
5. **M4 电源/手机/升级**：根据小海豚交互复用已验收的绑定与签名升级组件，做
   休眠唤醒、重连、失败恢复及完整体验验收。

用户当前仅要求开启新固件开发。M0完成不代表Flipper全功能复刻完成；后续边界随
确认的硬件和功能验收收敛，不以无限扩展作为完成标准。

## 开发入口

- 保持 `tools/bk7258/bk7258.py` 为唯一构建工具。
- 使用显式 `--cp-config boards/bk7258/aidk_ai_toy/configs/dolphin_cp`
  和 `--ap-config boards/bk7258/aidk_ai_toy/configs/dolphin_ap`，
  `--partition boards/bk7258/aidk_ai_toy/bk7258_ab_fixed_block_full_release.csv`。
  这是现有CLI支持的开发profile输入，不新增第二个打包器。
- M0镜像仅构建，不覆盖实板；签名发布/安装继续遵守同一设备的信任与恢复条件。
- 标准入口：`dolphin list`；`dolphin run nfc|motion|health|wifi`。
  列表只证明命令编入，结果由原工具执行后的退出状态决定。

## 当前证据与下一步

- 已创建独立板配置和manifest目录映射；保留现有CP/AP归属、SDK和Flash分区。
- `make -C tests/host/dolphin check`：通过（mock回归，非实板）。
- 独立CP/AP、BL1/BL2构建及layer gate：通过；开发构建使用现有公开信任材料，
  未制作新产品签名发布包。构建layout为 `bk7258-e66ee7b7206dc724`。
- 检查实际生成配置：CP/AP均未启用旧语音/认领/眼睛/视觉业务；CP builtin表
  包含 `dolphin`、`bknfc`、`bkmotion`、`bkhealth`、`bkwifi`。
- 第一轮发现旧 `exec_builtin` 头文件不在当前构建中，已改用现有 builtin
  registry + NuttX `task_spawn`，保留优先级、栈和子进程退出状态；复测构建通过。
- 证据：工作区 `out/dolphin-m0-20260910/{result.json,build.log,host.log}`。
  `result.json`保存两核bin路径、大小及SHA256。构建命令遵循上文开发入口，
  使用 `--boot mcuboot --jobs 8 --rollback-floor 418`；公钥文件由本地信任材料提供。
- 下一动作：M1基于LVGL的屏幕菜单与标准按钮导航；先核对LCD控制归属和输入，
  再进行首个小海豚版本的受控实板部署。
- 实板安装、屏幕菜单、实物按键、RF功能：未执行。
- 无原生手机联调；不触碰COM8，不刷新或清空用户板卡。


## 2026-09-10 chip-board 整改恢复点

构建归属已迁回 chip，三个内核 wrapper 按 C 保留并增加版本/源码/配置门禁；
三板六个最终 clean ELF 调用链及对象重定位检查通过，固件 bin 与整改前基线一致。
源码与构建分层检查覆盖 Dolphin，正反例通过。实板仍为旧 411；新固件未部署，
三板运行、触摸、SDIO 回归未完成，不能作为产品验收。
详细命令、哈希索引、实际阻塞和恢复顺序见
[chip-board 整改报告](chip-board-wrapper-review-20260910.md#整改实施检查点2026-09-10)。
本轮未提交或推送，保留原有工作区成果。


## 418 全量下载及 AIDK 实板回归（2026-09-10）

用户明确要求“直接全量下载不要管那个规范”，覆盖当前整机读回前置。采用哈希匹配的
历史同板 accepted base，不冒充当前备份；保留新的独立 BL1/MCUboot 签名、布局、
回滚计数及8MiB校验。生成并通过 COM8 HIL 自动软件复位全量写入 `18.6.354+418`，
无需人工按键；Flash PASS、实板 pair=confirmed/counter418。

证据根目录 `out/chip-board-remediation-20260910/download418/`：
`release/release.json` 标识产物；全量 BIN SHA256
`56f38e601807580c2cc011906025d91c01597133f7f69fd5d6659e48edd306e1`。
`hil-run/result.json` 保留下载结果，`boot-status.log` 证明本轮启动；
`signed-elf-audit/result.json` 核验新签名 CP/AP 的 wrapper 调用边；
`kernel-live.json/log` 保留十轮实板计数：AP tick 8683→11134、CPU2 heartbeat
790→1013、IPI 813→1036，全程递增，CP sleep/ps 响应；故障/恢复0/0。
双屏服务 READY、SD NAND 默认资源读取成功，电池/运动/NFC命令响应。
物理屏幕映射、标定、已知卡片、SDIO写读压力与另两板仍未验收，不能称三板目标完成。
本次使用 Windows COM8 真实命令及相同计数断言，未宣称 Linux pytest fixture 已执行。
签名临时私钥已删除，仅保留公开验证证据；未提交或推送。

此前“新固件未部署/需当前备份”的 AIDK 阻塞已由本次明确授权及实际部署解除；
恢复入口以本节为准。下一步为确认另外两板物理路由并执行对应回归，AIDK保持418。

## 发布检查点（2026-09-10）

用户确认0.1.0+10可用并授权提交推送全部有效工作区改动。提交前只修正生命周期到Dolphin的头文件引用方式，CMake/Make显式提供映射目录，不改变产品行为；该整理另做增量构建，不将其新构建字节冒充已安装包。临时SD探针因对应候选源码已撤回不入库，原始日志/设备材料/构建输出不入库。此前三板wrapper及工具验收仍以原报告对应证据为准，不用T5单板替代。
