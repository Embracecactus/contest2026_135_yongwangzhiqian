# 2026-10-18 稳定候选与手机实机推进

本文按时间顺序保留当天的修复、失败与实板证据，各节版本均指对应阶段。截至末节，设备已通过 App OTA 升至 `0.7.121+767`、A 槽 confirmed，手机为 APK54；765 拒绝取消后继续下载至 App 确认，766 曾出现升级后的蓝牙重连/扫描失败并在普通重启后恢复，767 自动重连与回读通过。来源上传期取消实机尚未验收，细节见末节。USB 空闲握手的定向复现已通过修后对照，NFC 状态读取及一次无卡加载已通过，实体卡场景触发尚未验收。**长期稳定性尚未验收通过**：历史 755/756/757/759 均有 CPU1 心跳停止，后续有界观察不能替代长期结论。完整恢复、相机与语音的各层验证见对应小节。

## 源码与构建

- 团队基线：`fd6c8d6fe9c8c2462f6eaf288f60e99eebd48135`，保留原有工作树修改；候选为本轮开始时的脏工作树快照，不能标为纯 HEAD 构建。
- Agent 固定 `35df4b347b17f07c972eb6e7e14a14fee1be78e4`，使用最小 PCM 消费线程优先级就绪修复；播放优先级 110。
- NuttX 从 `76354c637858ecb0aa4601629327acb6f44a26bb` 切换至 `5ecdd18b230acd2a86068cb4559b9f3f59363857`；单次采样同时拆分秒与纳秒，消除跨秒拼接倒退。主 `openvela.xml`、`kernel_compat.json` 与实际 checkout 已一致，原有未跟踪目录保留。
- 该依赖提交复用本地 bundle，未进行远程发布；不能据此声称远端 manifest 已可独立同步。
- CP/AP 构建通过；两核 `CONFIG_TIMER_ARCH=y`。工程测试、factory diagnostics 和 TTS isolation 均关闭。没有带入 754 的整组实验 Agent 修改。
- 时钟 5 项、kernel compatibility 6 项与 PCM/TTS/音频相关回归通过。子代理受限环境中的 `cancel-blocked-next` 超时保留；根环境复测通过，不能把该超时归为产品缺陷。

## 实板时间线

| 阶段 | 观察 | 限度 |
| --- | --- | --- |
| 755，`0.7.109+755`，B 槽 | 下载及签名校验通过；首次 `pair=confirmed`，初始 HEALTHY；KWS 采集及显示继续 | 仅证明初始启动 |
| 755 后续 | `SECONDARY_TIMEOUT` 锁存，CPU1 heartbeat 停在 155；CPU0 heartbeat 后续至 7428，故障/恢复 1/0；RPMsg manager 不可用 | 确认真实运行异常，未获得故障瞬间 CPU1 栈 |
| 同一 755 正常重启 | 观察至 healthy=133140ms，CPU1 heartbeat=1224，故障 0；期间手机 TLS pin 校验再次失败，心跳仍正常 | 该次认证失败未复现心跳停止；不能排除间歇竞态 |
| 756，`0.7.110+756`，A 槽 | 使用同一 build manifest 与程序，仅递增版本/签名元数据；立即接管 UART，记录 `pending → confirmed`；各采样点健康；后续正常扫码与文件选择器往返期间仍健康，已观察 healthy=1118830ms、CPU1 heartbeat=10184、故障/恢复 0/0 | 这一阶段未复现，不代表后续稳定 |
| 756 后续，11:33 UART 采样 | `SECONDARY_TIMEOUT` 再次锁存；CPU1 heartbeat=16620 不再前进，CPU0=17027/17028 继续；RPMsg FAULTED/error=110，manager=-107；faults/recoveries=1/0 | 相机测试前后存在时间关联，但缺少故障起点采样，不能归因相机；无有效 HardFault 记录，不使用 magic=0 的残留 PC/LR |

仅写对应备用槽 CP/AP，未写 BL1/BL2、用户数据、身份、校准或 SD 资源。使用既有开发签名身份，未生成或轮换密钥。这里是 BK Loader 有界分段下载，**不是 App OTA 验收**。

包 SHA256：

- 755：`dac2aced757782ec6d171273c2daaa283cec9fe00a0f243b09aab0d1bc35b3b9`
- 756：`cc5e34e5c8e41ac2885539704b9f59255bbcb0071c87cd142ff6fb9df2ae9716`

## 手机

- USB 调试经用户授权；原 App 42 / `0.7.12-shaniu-ui-v2`，以同签名 `install -r` 更新至 48 / `0.7.18-shaniu-desktop-camera`，未卸载或清除数据。
- 普通 48 APK：`ead027986d9a451191b59c8a0025c92c9dc97300bc1c8a1709324a057000c9ac`。
- 本轮诊断 APK 沿用 code 48，以哈希区分：`c1e3652633621f0db817a27e76aa46243268c9df3ddb8e05ad72b735b8d8c465`。已安装并拉取实际 APK 校验相同字节。
- 最终控制修复 APK SHA256：`355a2df09054e199b31f1ee5b260a50cd0ef4dc98a47845cc7c1092af03c70c6`，仍为内部调试 code 48；已同签名安装并拉取实际包验证完全一致。
- APK 签名 SHA256：`b2ea591116f967af230d0f06f678cc5fdd99d5884cc429b8d22c6489ba96050b`。
- 历史失败中 GATT 连接、服务发现、MTU 71 与通知订阅成功，TLS 阶段关闭。新增固定诊断码、至多 8 层异常 cause 分类，不记录 provider 原始消息、证书或凭据；原失败关闭行为不变。相关 6 项测试和 APK 构建通过。
- 历史失败已由真机日志确认为 `detail=certificate_pin_mismatch`。用户确认同一物理板且板面显示二维码；随后通过正常扫码入口认领，设备回执 `COMMITTED`，手机保存新认领结果。未绕过证书验证、清除 App 数据或轮换板端 TLS 身份。
- 已取得认证后 STATUS：管理已连接、设备未联网；更新页回读版本 `0.7.110 · 756`。9 次设备/定制/更新/设置切换保持同一 PID 13445；GATT 从 11:14:02 到 11:19:37 保持，关闭发生于文件选择器超过 30 秒的既有后台宽限，而非切页。
- 最终修复 APK 实机完成 20 轮往返、40 次切页，持续 163.2 秒；PID 22891 不变，期间无新 GATT connect/close，结束仍为认证后的管理已连接。仅证明手机管理会话，不代表 AP/相机健康。
- 文件选择器返回自动重连，但电脑授权表单没有在同 generation 再认证时重新发布状态，导致按钮停留禁用。最小修复只发布恢复后的本地状态，不重发读写或变更会话宽限；控制器 21 项单测通过，APK 构建通过，实机已验证超过 35 秒的外部文件选择器返回后自动重连，读取按钮恢复可用。
- 明确的 `CONFIG_READ` 原本会被进行中的 STATUS 轮询拒绝，导致授权页出现“请求未发出”。现仅允许该前台配置读取占用既有唯一等待槽，当前读取返回后再发送；不增加并发、队列容量或自动重发。相关 DeviceControlSession 28 项、PcAuthorizationController 21 项、诊断 6 项，共 55 项通过；最终 APK 另实测 3 次连续授权状态回读均成功，原授权保持。
- 电脑授权首先回读“当前没有有效电脑授权”；使用正常 App 配对流程，仅授予 tasks/camera。请求摘要核对一致，设备持久保存后回读确认；导出加密响应，使用当前已认证 App 的证书摘要核对后创建 Windows CurrentUser DPAPI profile。Native USB COM16 已实际鉴权并回读相机 admitted=true。

## PC 任务与相机

- 正常 PC profile 仅授予 tasks/camera，使用当前 Windows 用户 DPAPI 保护；请求临时 RSA 密钥用于配对封装，不是设备/固件签名身份。未提取手机私密凭据。
- 第一次 `task-run` 执行有界本地程序；WSL 调用包装因 Windows stderr 的非 UTF-8 文本解码失败，原输出未保留下来。没有盲目重发；随后只读 `task-status` 回读同一最近任务 `state=success, progress=100, event_sequence=2`。这证明设备任务终态，不补写缺失的本地主进程退出码，也不把 feedback_pending 当实际屏幕反馈。
- 11:32:44 保存的首次真实相机结果为 `expired/error=-110`，size/sequence/fps 均为 0，没有 JPEG。PC 协议释放 `release_confirmed=true`，随后回读 `canceled/valid=false`；这仅确认协议缓存状态，不能证明可能阻塞的 V4L2 worker 已退出。
- 为补充 UART 关联而进行一次相机尝试，设备返回 `-16`，没有新帧；同轮 UART 已显示 756 的 secondary timeout，停止继续采帧。没有把 CLI 返回 0、admitted=true 或 release_confirmed 当作相机成功。
- 首次 capture 文件保存时间为 11:32:44，UART 故障记录窗口自 11:33:11 开始；这不是故障起点。当前相机路径没有逐阶段日志，无法仅凭零元数据区分 worker 尚未调度、RPMsg/V4L2 阶段或等待首帧。

## 下一步与证据

1. 首要方向改为定位 755/756 均出现的 CPU1 心跳停止。分段下载阶段结束时保留了 756 故障现场；两次只读采样 CPU0 heartbeat 从 20154 到 20192，CPU1 均为 16620，faults/recoveries 为 1/0。用户随后确认只有串口/Type-C、没有 J-Link，并授权全量烧录；后续备份和恢复已重启，旧故障仅保留日志。需要增加有界心跳/调度/相机阶段快照；现有证据不足以调整优先级、超时或缓冲区。
2. 保留已完成的正常手机认领、认证 STATUS、电脑授权与生命周期修复。相机真实帧仍未验收，待 AP 稳定性明确后再测。
3. 设备尚未配 Wi-Fi/云服务；物理唤醒/云端对话、NFC 实卡与 App OTA 尚未执行。KWS 输入和显示日志不代表这些验收通过。

原始证据位于工作区 `out/shaniu-stable-candidate-20261018/`（权限 0700）：`progress.json`、`main-integration.json`、`main-candidate-correspondence.json`、`host-evidence/`、`loader-755/`、`boot-755/`、`health-followup-755/`、`health-tls-correlation/`、`loader-756/`、`trial-756-timeline/`、`final-756-status/`、`phone-tls-diagnostic-logcat.txt`、APK 签名/安装/字节回读记录，以及 `phone-qr-*`、`phone-pc-*`、`pc-task-recovery-status.json`、`pc-camera-*.json`、`pc-camera-correlated/`、55 项 Android 单测 XML。失败的构建环境预检、依赖获取和 OpenSSL 路径错误日志同样保留。

主 NuttX checkout 遇到 2026-08-30 遗留 `index.lock`；核验无持有者和 Git 进程后移至证据目录保存，再切换依赖提交。Android 忽略的 `local.properties` 仅修正目录迁移后的 SDK 路径。没有提交或推送。

## 同日后续：757 同板全量恢复

用户明确授权这块板子重新完全烧录，连接仅使用 CH340 UART0 COM9 与原生 Type-C COM16，没有使用 J-Link 或 RTS/DTR 复位。

- 通过现有构建入口生成 rollback floor 757 的 BL1/BL2；CP/AP 的原始输入和 ELF 与 756 相同。这次验证的是完整启动链恢复，未加入 CPU1 或相机修复。沿用既有开发签名身份，没有生成或轮换密钥。
- BK Loader 的 `read` 不接受下载模式的 `--swrst/--reboot`。此前等待窗口超时，人工复位也未完成读取握手；这些阶段没有擦写。确认当前 NSH 支持 sleep 和分号命令后，发送 `sleep 3; reset reboot`，释放串口并启动只读 loader，成功读取完整 8 MiB。读取器将 UNC 输出路径解释为相对路径，并自动添加 dump 后缀；按实际成功日志找到输出，校验后迁入私有证据目录，删除已核对的 Windows 临时副本。
- 新鲜同板备份 SHA256：`7b495b1fbcc752e2a34f49f040493c451f488fe24ebc4261899b2dc1084176d4`。`accept-base` 通过；烧录前再次回读原设备为 `0.7.110+756`、A 槽 confirmed、双核健康。
- 正常 `release full` 生成 `0.7.111+757`，没有使用 factory-init。package/trust 验证通过，BL1/BL2/CP/AP 签名有效。镜像中 usr_config、persistent_data、factory_state、设备身份/校准尾区及未分配字节均与备份一致；SD NAND 未写入。
- 完整 BIN 大小 8388608，SHA256：`b9790093962836d1ef841e4a4f9dfefa4602da8a6f4d033b95600f4dee900be2`。签名包 SHA256：`84ba628e15f8f39ba54cdcd32707c81b360b56efa150a434b06eabaa38bf3d95`。
- 13:06:18–13:09:40，单次 BK Loader 写入地址 `0x0`、长度 `0x800000`。握手、擦除、写入及全部完成标记齐全，无失败标记；下载器退出码 1 按工具约定仅作记录。没有自动重刷。
- 立即采集 180 秒 UART：`pair=confirmed version=0.7.111+757 counter=757`，RPMsg CONNECTED、CPU2 SCHEDULER_ONLINE。84 次 health 输出含 83 个不同快照和 1 次重复缓存快照；不同快照间双核心跳均前进，primary/secondary 从 22/29 到 1550/1557，故障/恢复/连续故障均 0/0/0，无 SECONDARY_TIMEOUT。稍后只读采样仍健康。这是有界启动观察，不是长期稳定性验收。
- 手机沿用原绑定自动认证，更新页实际回读 `0.7.111 · 757`，结束时仍显示“管理已连接/设备未联网”。没有重新扫码、清除 App 数据或重做电脑授权。原 Windows DPAPI profile 首次通过 COM16 TLS 鉴权并读取 camera-status（admitted=true）。
- 随后仅发起一次 camera-capture。外层主机包装在 60 秒超时退出，未保留该次 stdout，未取得完整 JPEG 或释放回执。源码允许 READY 后最多 120 秒逐 16B 传帧，因此这个外层时限不足，**不能据此判定板端捕帧失败，也不能确定超时所在阶段**。确认 Windows 子进程已退出；两次后续只读 camera-status 均失败于 TLS handshake，没有重放 capture，当前相机释放状态未知。同期 UART 双核心跳继续前进，手机仍可管理，不能把 USB 异常归为已复现的 CPU1 停跳。

本阶段结束时保留了 757 的 USB 异常现场，没有自动再次重启。随后用户重新上电，新一轮使用仅主机侧的阶段观测完成了正常相机闭环，结果与当前方向见下节。CPU1 间歇停跳仍需更长时间的连续观测。

本阶段原始证据位于工作区 `out/shaniu-full-recovery-20261018/`（0700）：`readback-verified.json`、`current-device-8m.bin`、`accepted-base.json`、`build-comparison.json`、`release-757/`、`protected-ranges-verified.json`、`loader-757/`、`boot-757/`、`boot-health-summary.json`、手机 XML、`pc-camera-*.json`/stderr、`usb-timeout-status/`、`final-health-757/`。备份、恢复包和完整 BIN 含同板私有数据，仅用于原设备，未发布。

## 用户再次上电后的相机闭环

仍运行 `0.7.111+757`，未再烧录、修改固件、重做授权或改变设备身份。手机保持“管理已连接”。本轮首个由主机发起的 Native USB 控制连接直接执行一次 camera-capture，沿用原 DPAPI profile 与现有 CLI/协议。

为区分上次缺失结果所在阶段，私有 HIL 观察器仅在现有客户端调用边界记录时间、固定阶段、相机状态和传输偏移，不记录认证载荷、密钥、证书或帧内容；没有更换传输栈或协议。主机 stdout/stderr 直接保存，外层总超时设为 240 秒，覆盖协议允许的最长传输及清理阶段。

- TLS 鉴权完成约 1.39 秒；板端返回 READY，640×480 JPEG，15,211 字节，传感器配置 fps=30，capture_elapsed_ms=2065。fps 字段不是持续视频帧率测量。
- 从发起采样到完成帧读取及最终状态检查为 80.35 秒；其中逐 16B 读取帧的区间为 76.586 秒，实测有效载荷约 198.6 B/s。这次成功路径本身已超过此前 60 秒外层时限；不能反推出上次丢失输出时的具体阶段。
- 客户端收到声明的完整长度，验证 JPEG SOI/EOI 和最终帧身份/有效状态，并计算 SHA256：`fc4e7b9ccf9ecaeaa80d1c0269d83eccc75c01ae65c2d979b39e32ec5585a8f4`。这是完整帧读取与格式边界验证，未与设备侧独立哈希比对，也未保存图像、进行图像解码或人工画面验收。
- 取消/释放回执 `release_confirmed=true`；同一连接内回读 canceled、valid=false、size=0。客户端正常关闭后，新建第二个独立 TLS 连接，camera-status 再次成功并回读同一 id=1、canceled/valid=false/size=0，确认正常结束后的鉴权和缓存释放路径可用。
- 连续 360 秒 UART 采集覆盖上述操作，共 171 次健康输出、169 个不同快照；不同快照之间双核心跳均前进，primary/secondary 从 610/616 到 3773/3779，末次 healthy=414280ms，故障/恢复/连续故障均 0/0/0，没有 SECONDARY_TIMEOUT。仅构成该观察窗口的证据。

相机正常采样、整帧传输、协议缓存释放与正常重连已取得实板证据。当前首要瓶颈是逐 16B 请求的低吞吐，应基于已测时序评估现有认证协议内的批量传输；异常强制中断后的会话恢复仍需专项验证，不能把这次正常重连当作其已修复。CPU1 的长期稳定性结论保持未验收。

证据目录为工作区 `out/shaniu-powercycle-camera-20261018/`（0700）：`operation.json`、`camera-command.json`、`trace_camera.py`、`camera-stages.jsonl`、`camera-result.json`、`camera-run.json`、`pc-reconnect-status.json`、`pc-reconnect-run.json`、`uart-timeline/`、`verification-summary.json`、`health-summary.json`、手机状态 XML。本轮没有修改产品源码或重新发布产物。


## 同日后续：758 相机分块与强制中断回归

本轮按用户“开始吧”继续优化传输并验证异常中断，保持 hardware-fast iteration。
变化限于认证 SDC1 相机读取：kind25 先以旧 40 B 回复协商 CBR1，再返回固定
536 B 回复中的 512 B 图像块；kind22 的 16 B 路径及手机协议保留。相机仍唯一拥有
请求身份、帧缓存和期限，PC grant 每次重新核对，无新增采样线程或隐式重放。
协议详见 `docs/platforms/bk7258/shaniu-pc-camera.md`。

- 烧录前新鲜 UART：757 仍为 A 槽 confirmed，但已出现 `SECONDARY_TIMEOUT`，
  faults/recoveries/consecutive=1/0/1。这证明此前六分钟健康窗口不能外推为长期稳定；
  本轮没有修改调度/心跳策略。该现场保存后才开始本次下载。
- 原增量树构建通过，AP 原始程序和 ELF 改变，CP、BL1、BL2 的原始程序及 ELF
  哈希与 757 相同，rollback floor 仍为757。`release ota` 复用既有 MCUboot
  公钥指纹 `7163228f3221da6e81fcf5e9c6f13ae6c4afbd1e3b91b7669783a37d2f12fbc0`，
  生成 `0.7.112+758` CP/AP 配对包，没有重签或写入启动区。
- 包 SHA256 `387397bcfca885bc5c8f3e6692fc66300d29d0340f60bb212ecb0f34b7cde88f`；
  package/trust 和 loader preflight 通过。只写本次回读的非活动 B 槽 CP/AP，
  范围分别为 `0x352000+0x132000`、`0x484000+0x20f000`。BK Loader 一次完成，
  完成标记齐全且无失败标记；随后板端 `BOTA TRIAL CONFIRMED slot=1 counter=758`，
  `bkota status` 回读 B 槽 confirmed/version=0.7.112+758。
- 正常采集 id1：640×480 JPEG，14663 B，capture_elapsed_ms=1782；
  纯帧传输2.6804秒，有效载荷5470.45 B/s。相对757的198.61 B/s，约 **27.54倍**；
  两次图片内容/长度不同，比较按有效载荷吞吐归一。采样请求至最终状态检查5.9254秒，
  整个CLI另含鉴权/释放/关闭。验证完整长度、身份、JPEG SOI/EOI、主机SHA256，
  release_confirmed=true。未保存、解码或人工审阅真实图像。
- 下一条独立 TLS 连接认证成功且读到 id1 已取消，随后显式开始 id2；在收到
  1536/61663 B 时执行 `os._exit(86)`，故意不发 cancel/close_notify，原进程确实退出。
  退出25.01秒后首次只读重连在1.44秒内失败于 tls_handshake；没有重放 capture。
  退出60.32秒后第二次只读重连成功，回读 id2 canceled、valid=false、size=0。
  这些是两个离散采样，**不能将60.32秒说成自动恢复耗时或恢复下界**。
- 随后新连接显式采集 id3 成功：13683 B，纯传输2.5507秒，释放回执成功。
  从强制退出至恢复采集没有复位/断电/重新授权，手机仍显示“管理已连接”。
  结论是无需重启可恢复，但首次重连仍失败，不能宣称无感恢复已完成。
- 源码辅助分析：当前 ttyGS0 属于 BK7258 CherryUSB gadget；USB RESET/DISCONNECTED
  会向串口层报告断连，但 Windows 进程退出并不等价于 USB 断连，gadget 当前忽略
  DTR 控制线且无对应查询 ioctl。新 ClientHello 使旧 TLS 终止是与现象相容的解释，
  本轮没有设备端逐包 TLS 证据，尚不能作为唯一原因。下一步应围绕握手前会话回收
  做有界处理，不能重放已经提交的业务命令或放宽证书/授权检查。

定向检查：control-session、control-pair、pc-camera 三项 C 测试通过；camera Python
4项、client Python21项通过；production TLS fixture 通过，camera peer 明确断言512 B，
仍只使用合成图像。`git diff --check` 通过。nxstyle 对原有紧凑风格文件仍有诊断，
未声称全文件风格通过；Black24.10报告五个Python文件无需修改，但本次其进程未在
15秒内退出，timeout结果保留。此轮无APK修改、提交或推送。

本轮私有证据位于工作区 `out/shaniu-camera-block-20261018/`：源码输入及对应哈希、
构建/签名/校验日志、loader-758、boot-758、normal/abrupt/recovered 的阶段时间线与
完整stdout/stderr、手机XML、定向主机测试、verification-summary.json。失败的首次重连
及最初OpenSSL相对路径校验失败也保留；后者改用绝对可执行路径后通过。

758实测操作期间另有连续240秒UART：114次health输出、112个不同快照，
双核心跳从513/519到2585/2592，不同快照间均前进；
故障/恢复/连续故障均0/0/0，没有SECONDARY_TIMEOUT。此窗口不覆盖前述长期故障，
757再次停跳与首次USB重连失败仍是下一轮的明确未解决项。

最终 `verify layers` 通过（sources=562、kconfig=253、legacy-exceptions=2）；
生产C源码与实际构建候选对应哈希一致，`git diff --check` 通过。


## 同日后续：759 首次重连修订与 CPU1 停跳取证

本轮继续 hardware-fast iteration。758 新鲜现场已出现 `SECONDARY_TIMEOUT`：
CPU0 heartbeat 从23925继续增长至30814，CPU1固定5493；CPU0→CPU1调度IPI
发送27930、接收27929，随后计数保持不变。这证明心跳线程未继续运行，但尚不能
区分CPU1中断、休眠交接、调度投递或任务等待问题。故障现场保存后才重启。
CP NSH没有 `mw`，只读地址查询失败，未启用通用内存接口或执行内存写入。

主机 `authorized_client` 在单一鉴权期限内最多建立两次连接；仅业务开始前的
TLS握手传输/非证书SSL错误允许关闭、退避1.1秒后重建。证书验证、指纹检查、
PC鉴权失败不重试；进入业务后的失败不重放命令。单调时钟非法或回退即失败，
跨两次attempt共用期限，凭证退出时清零。定向client测试24项通过，camera测试
4项通过；Black报告无需变更，但进程退出超时，不能将该退出码记为通过。

AP只增加停跳观察：既有CPU0生命周期每轮检查CPU1 heartbeat；停跳1000/2000ms
各输出一次 `AP HEALTH STALL`，记录health TCB状态、等待期限、CPU1当前任务指针、
IPI pending和AON休眠位。此为无调度锁的最佳努力跨核快照，不保证字段同时性；
不唤醒CPU1、不改变线程优先级、心跳期限或Supervisor策略。

- 增量构建通过；同一签名身份生成 `0.7.113+759` CP/AP配对包，package/trust通过。
  包SHA256为 `607ba3fbddc92989729526206ce7642b82a9dff0da87af68b57cffc1cea019aa`。
  依据烧录前758 B槽confirmed回读，只写非活动A槽的
  `0x11000+0x132000`、`0x143000+0x20f000`。BK Loader一次通过全部完成标记；
  新UART显示759从A槽启动，未使用RTS/DTR。
- 强制退出：id1的16739 B帧收到1536 B后，主机 `os._exit(86)`，没有发
  close_notify或相机取消。退出13.89秒后开始第一次只读重连；第一个握手失败，
  同一CLI自动重建一次后成功鉴权，从第一次握手开始至成功约1.586秒。
  回读id1 canceled、valid=false、size=0，CLI返回0，观察器记录的相机请求数为0。
  该时间是人为发起的离散观察，不是设备自动恢复耗时。
- 随后新的显式采集id2成功：640×480 JPEG、20283 B，采样2115ms，帧传输
  3.7747秒，release_confirmed=true。完整长度、帧身份和JPEG边界校验通过；
  没有保存、解码或人工审阅真实图像。期间未复位、断电或重新授权。
  手机XML回读“管理已连接”；界面仍提示云端对话暂不可用，未将本次结果外推为
  云端语音验收。

证据目录为工作区 `out/shaniu-reconnect-stability-20261018/`（0700），包含758故障
复采、源文件哈希、759构建/签名/验证和烧录记录、`abrupt-759`、`reconnect-759`、
`recovered-759`完整阶段时间线、`reconnect-summary.json`及手机XML。新诊断C文件的
nxstyle检查报告原有文件头路径和原有 `for (;;)` 两处空白问题，未混入格式修改。

759长测结果：首段900秒有444次健康输出、443个不同心跳快照，双核心跳
25/32→8095/8102，快照间均推进，faults/recoveries/consecutive始终0/0/0，
无停跳诊断或SECONDARY_TIMEOUT，A槽759 confirmed。随后600秒完全不发串口
命令、只收日志；完整原始日志13897 B，同样无停跳诊断。结束后独立回读
primary/secondary=14000/14007、healthy=1539370ms，状态HEALTHY，故障计数0/0/0；
调度IPI两向发送/接收分别65813/65813、25955/25955，失败0/0。
这覆盖约25.7分钟的运行及一个不持续查询的观察窗口；本轮未复现CPU1偶发停跳，
因此不宣称其根因已确定或长期故障已修复。现有诊断可用于下一次真实停跳取证。
首次静默采样因工具CommandDelayMs上限在开口前拒绝，失败日志保留；改用零命令
采样并在结束后单独回读，没有修改通用采集工具。所有采集进程完成后释放端口。

## 同日后续：760 浅睡眠中断顺序及联网边界

继续 hardware-fast iteration。759 在烧录前仍为 A 槽 confirmed，双核健康持续
2687570ms（约44.8分钟），故障/恢复/连续故障0/0/0；本轮没有复现CPU1停跳。
相机批量测试前10帧成功，第11帧id13在11776/14855 B处耗尽帧传输期限，随后
取消请求也超时，立即停止批测。同期手机正在应用Wi-Fi配置；板端随后出现
`AGENT service TLS verified=1 result=0`，App确认配置revision3保存并应用。
这只建立时间上的重叠，不能把相机失败归因为Wi-Fi、CPU1或采集驱动。
后续只读TLS连接成功，id13为canceled、size=0、valid=false，帧已释放。
原计划360秒的串口进程以143退出，只保留raw前缀和实时回显，不能记为完整360秒；
后续独立12秒串口采样成功，未把采集进程退出误报为设备重启。

本轮改动仅处理一个源码可证的窗口：`pm_idle()`持有调度锁时，旧浅睡眠路径先
开放中断再执行WFI，调度IPI可能在WFI之前被处理，却因调度锁未释放而仍返回
idle任务。公共idle及AP coordinated shallow helper均改为先保存BASEPRI/PRIMASK，
以PRIMASK延后ISR、清BASEPRI后WFI，返回后恢复原掩码。依据为
[Arm Cortex-M33 DGUG 100235_0100_06_en](https://documentation-service.arm.com/static/66be2153882fec713ef49e49)
第2.6.2.1与3.13.17节，以及当前NuttX `pm_idle`/IPI/调度实现；没有改优先级、
心跳期限、缓冲区或深睡眠策略。此窗口尚未与历史停跳建立实板因果对应。

- CP/AP增量构建与最终ELF反汇编核对通过；签名包 `0.7.114+760` 的package/trust
  校验通过，包SHA256为 `843a960f0485e889131260a7534704ac711d7742390c3f6e464ef617e604cf40`。
  BK Loader只写当时非活动B槽CP/AP，一次通过；12.187875秒日志确认counter760。
- 重启自动恢复Wi-Fi与revision3云配置；ASR/LLM/TTS activation均返回0，服务TLS
  verified=1。实际加载 `nihao_openvela` 模型，SHA256为
  `922eba9175fcda60f7c8a4505ca4eb5a97c86ceb30fbe48c685fd612098ac910`，wake ready=1。
  手机自动恢复管理连接、显示Wi-Fi已连接及音量53%，未重新认领、配网或安装APK。
- 结束回读760 B槽confirmed，双核HEALTHY，healthy=382030ms（约6.4分钟），
  faults/recoveries/consecutive=0/0/0，双向调度IPI收发相等；这是短窗口验证。
- 同一PC会话两次camera STATUS相隔约0.94秒，均id0/canceled/admitted=false，
  未发起新的capture。随后两次独立只读重连均在TLS握手失败，未执行或重放业务。
  当前缺少相机准入分项原因及生产配置USB错误码，故不以合并状态猜测根因。
- 已向现场用户请求“你好，openvela”及一个固定问题；完整240秒UART仅观察到持续
  MIC采样/KWS推理，没有接受唤醒事件，也尚未收到用户尝试结果，语音回合未验收。

证据位于工作区 `out/shaniu-secondary-debug-20261018/`（0700）：759批测与失败时间线、
两版源码哈希、760构建/签名/校验/下载/启动日志、最终反汇编、只读PC失败、手机
脱敏XML、完整语音观察窗及 `iteration-760-summary.json`。`git diff --check`通过；
nxstyle原始误用多文件的失败及更正结果均保留，全文件仍有既有格式诊断。

## 同日后续：761 原因观测与 762 联网相机修复

761仅增加状态变化日志：相机八项准入条件的阻塞位图，以及USB owner返回码与
epoch变化，不改变准入策略、超时或重放规则，不记录凭据或业务载荷。增量构建、
签名与校验后一次写入非活动A槽，启动confirmed；完整180秒UART捕获到云服务
ready之后相机阻塞从00变为04。bit2仅代表 `voice_interaction_active`，同时PC通用
状态却是ready=true、busy=false、turn=0、runtime_error=0，KWS仍在监听。这将问题
收敛为产品交互状态错误，并非真实语音通道忙或相机授权缺失。

源码对应原因：`product_probe_worker()`完成云服务校验后调用了
`bk7258_agent_product_wake()`，错误置位本地唤醒事件4；触发处理器在没有
`turn_pending`时返回0，产品循环将其当作成功开启交互。762将云校验完成通知
改为仅 `sem_post(&g_product_wake)`，既有 `product_ready()`读取结果；真实KWS
事件入口保留，未改唤醒模型、阈值或云端接口。

定向回归直接执行生产回调：修前断言“probe不得新增wake位”失败；修后新增用例
及原12项local-ready共13项通过。扩展新用例覆盖成功0/失败-EHOSTUNREACH、
无wake/已有真实wake共四组合，验证发布结果、清running、恰好一次信号通知且
保留原事件位。证据在 `host-probe-wake-762/`。仅修正既有测试的源码截取边界，
未复制产品实现或新增测试框架。新增C诊断/修复范围nxstyle及diff空白检查通过；
未声称原有整个Python/C文件格式检查通过。

762包版本 `0.7.116+762`，SHA256为
`32a22c9ba1bd1d2f1b3dc222c7f28c0a2d95a623d679d36070d87fb0c7c747a7`。
签名、package/trust及非活动B槽边界校验通过，BK Loader一次完成，12.181657秒
日志确认counter762。云服务自动恢复revision3，准入未再出现bit2误置。
随后3次独立PC会话均首次TLS握手成功，每次显式新采集并确认释放：

| 帧ID | JPEG尺寸 | 字节数 | 采集耗时 | 帧传输耗时 |
|---|---|---|---|---|
| 1 | 640×480 | 9391 | 1687ms | 1.703s |
| 2 | 640×480 | 9527 | 1562ms | 1.679s |
| 3 | 640×480 | 9639 | 1858ms | 1.864s |

三帧均使用512 B块传输，完整长度、帧身份、JPEG边界及release确认通过；未保存、
解码或人工审阅实际图像。手机仍为管理已连接、Wi-Fi已连接、音量53%。这验证
联网后的相机误忙已修复，不覆盖配置变更打断进行中的传输。760两次TLS握手失败
的独立原因尚未确证；本轮762三次正常连接不能替代该故障的因果证明。
本次也未完成真实唤醒→ASR→回答→播音回合。现场步骤已更正：固件将唤醒应答
延至录音结束后，用户应在唤醒词后接着说问题，不应等待口头应答才开始说话。

新增证据仍位于 `out/shaniu-secondary-debug-20261018/`：761完整诊断、762红绿
回归、构建与下载记录、`online-camera-762-01..03`、汇总与脱敏手机XML。

随后用户现场明确反馈“唤醒了”“听到了完整回答”。接续UART观察到监听线程167
持续采样及KWS推理；完整唤醒/录音/云请求/播音过程没有同一段全程日志，因此
此处记录为用户确认的实际语音结果，不补造逐阶段时序或ASR/LLM/TTS独立验收。
语音后的新camera请求在业务开始前TLS握手超时（约10秒、未重放capture）；
随后独立只读重连成功，ready=true、busy=false、turn=0，camera admitted=true、
size=0、valid=false，资源已可用。状态保留runtime_error=-61（ENODATA），其
对应的具体回合未完整采到，不能仅凭此值判定刚才成功回答失败或假定后续静音。
音量回读为80%，与此前53%不同；本轮未执行音量写入，也未擅自恢复该值。
新证据在 `live-human-762/`、`post-voice-camera-762/` 和
`post-voice-readonly-762.log`。空闲USB握手失败仍待独立定位。

最终独立回读：B槽762 confirmed，双核HEALTHY，healthy=423320ms（约7.1分钟），
faults/recoveries/consecutive=0/0/0；IPI两个方向收发为19413/19413和9902/9902，
fail=0/0。762两段180秒UART均完整结束；本轮所有采集/下载/PC进程均已退出并
释放端口。此为短测及用户现场结果，不宣称历史长期停跳或空闲USB重连已闭环。

## 同日后续：763 USB 空闲边界、相机与 APK49 NFC 记录

763 为 `0.7.117+763` 的已签名 OTA 包，package/trust 校验通过；本次仍通过
BK Loader 单次写入非活动 CP/AP，实板启动后 BOTA 已确认 counter=763，不是 App OTA。
USB 修复将 CDC 保持为监听状态，收到首字节才建立有界
TLS 握手，非阻塞 POLLIN 检查不消费数据。原实现无主机输入也启动30秒握手计时，
临近到期的新连接可被旧计时中断；未放宽握手期限、认证检查或增加业务重放。
对照实测中，762 在 29.6 秒空闲后的近边界握手于 TLS 阶段失败；763 在
相同 29.6 秒空闲后于 0.503106 秒内完成认证，并在 35.243178 秒完成流程，两个
记录均标记业务未重放。此项仅覆盖一次约30秒边界复现，不证明任意长空闲或所有
断连情形。

763 随后完成一次 USB 相机采集：640×480 JPEG、7883 B、capture_elapsed=1849 ms；
使用512 B块传输，纯帧传输1.5080801秒，记录了SHA-256与 `release_confirmed=true`，
释放后的状态为 canceled/size=0/valid=false。相机 CLI 结束时间为
1792315932.7306707，下一只读USB认证开始于1792316108.4867382，同一Windows时钟
相隔175.7560675秒且认证完成；因此 `readonly-long-idle-763.log` 自身2.34秒的
内部跨度不应误读为空闲时长。上述只覆盖单帧、释放和一次空闲后只读认证；未做
图像人工观感、独立解码或传输中断恢复验收。

在 NFC 侧，763A 为 confirmed；现场 `bknfc scan` 报告 present=yes，AP supervisor
为 HEALTHY，healthy=1035340 ms，faults/recoveries/consecutive=0/0/0。APK49为
`0.7.19-shaniu-nfc-session`（code49），已验证安装字节与候选APK一致。其单测中
DeviceControlSession/Protocol/NfcBindingController分别28/19/19项通过；协议测试
覆盖旧NCS1/NCA1与扩展NCS2=176 B、NCA2=48 B的分块边界。手机实机初始及刷新均
成功读取NCS2/NCA2，未观察到新增 transport_close；场景状态提示卡片仍在读卡器上，
最近回执为-2。

两次显式点击“加载已保存卡片”(LOAD act1)均返回设备 `-16`；第一次拒绝后的手动刷新NCS仍为
state=0，故没有作业受理、卡片加载或自动专注执行的证据，不能记为通过。该 `-16`
按当前实现可来自NFC worker短暂I/O/作业/释放占用或OTA总闸，现有NCS/NCA快照不足
以事后唯一归因。手机连接后的音量显示100；本轮没有执行音量写入。

同段UART的 request=3 具有KWS、ASR、LLM、TTS各阶段 result=0、最终complete=0及
回到channel idle的记录；request=4 则因无有效语音返回-61，随后rearm=0并恢复KWS。
用户已确认唤醒并听到完整回答；该反馈未能唯一对应request=3，故分别记录人工听感
与日志链路证据。较早的 `voice-763/` 窗口仅捕获监听，不与后来完整回合混用。

USB PTY 定向检查及20组真实TLS主机互操作样本通过；后者的相机数据为合成数据。
原始失败及修正后的测试日志均保留在 `EV/host-763/`。四个USB/串口生产文件在主
工作树与构建候选中的SHA256一致；Android安装字节回读、66项定向单测以及
`git diff --check` 通过。未提交或推送。

最后只读回读仍为763A confirmed，OTA manager空闲、双核HEALTHY，
healthy=1450400ms（约24.2分钟），faults/recoveries/consecutive=0/0/0。
该计数是最后采样值，不表示已有24分钟无间断UART记录，也不宣称历史停跳已完全解决。
764普通CP/AP包已复用相同构建输入、以既有身份签名，并通过package/trust校验及
手机副本SHA256核对；包SHA256为
`977691910eccf88ba04447876aabeb79b267f8f0ae75bd399c8dd44d7cf0e700`。
尚未开始App升级，保留763的NFC忙现场，等待用户移开手机做有卡/无卡对照。

证据位于 `out/shaniu-usb-idle-20261018/`：`boundary-comparison.json`、
`camera-763/{camera-stages.jsonl,run.log}`、`readonly-long-idle-763.log`、
`nfc-scan-763/serial.raw`、`android-nfc-763/{apk-49-verification.json,installed-49-verification.json,
protocol-red.log,build-test-49.log}`、脱敏 `phone-49-nfc-*.xml`、`nfc-phone-763.log`、
`final-763-status/serial.raw` 及 `iteration-763-verification.json`。

## 同日后续：NFC 移开对照与 App OTA 764

用户将手机移开天线后，仍在763上进行对照，没有先重启或重新认领设备。
`bknfc scan` 从此前的 `present=yes` 变为 `rpc=0/operation=-110`。现有 scan
调用 GET_PICC_UID，未将无响应转换为 `present=no`；该结果只能证明本次未取得
卡片响应，不能单凭超时判定模块故障。随后 App 的 NCS2/NCA2 读取成功，一次
显式 LOAD 得到“设备确认作业完成”，8个卡位均回读为未登记。此前两次-16仍保留，
这次成功不能唯一证明手机贴近是忙状态原因，也不代表登记或刷卡场景已通过。

本轮没有修改产品源码或APK。764沿用763的同一构建清单
`43a4d3f1e0bf661b5f1526b374a16ca45dc2c907cb6280565ad883890d796a11`，
通过既有签名路径生成合法的新版本/安全计数，沿用前节记录的公钥身份及包校验。
手机副本哈希与本地包一致。系统文件选择器的搜索未定位到文件，后通过专用目录
`Download/000-Shaniu-OTA-764/shaniu-764.bkpack` 正常选择并完成App包检查；
没有绕过App入口或以直接烧录代替本次OTA。

- 手机日志18:30:29记录唯一一次start：authenticated/fresh/supported均为true，
  本机HTTPS来源在207ms后READY；18:30:42收到首个TCP连接，18:30:43完成首个TLS握手。
  板端也记录了认证后的App更新请求受理。
- UI进度从0%依次采样到13%、28%、41%、67%、84%，最终显示“已确认完成升级”及
  “新版本已回读确认”。期间串口独立读到AP staging为524288/2158592字节、error=0；
  App百分比是阶段映射，且与串口并非同一时刻采样，不能直接换算成同一字节进度。
- 更新期间的763健康采样为healthy=3773880ms（约62.9分钟），双核故障/恢复/连续
  故障均0/0/0。这是supervisor计数及离散采样，不是全程无间断UART证据。
- 分段串口捕获到 `BOTA TRIAL ARM slot=1 counter=764`。随后独立 `bkota status`
  确认为B槽、`pair=confirmed version=0.7.118+764 counter=764`，trial state=4、
  error=0，OTA manager回到空闲。最终AP为HEALTHY，healthy=74970ms，
  faults/recoveries/consecutive=0/0/0；该重启后的计数不与763相加。
- App恢复原设备管理连接，Wi-Fi已连接，音量回读仍为此前100%；没有重新认领、
  清除App数据或执行音量写入。本轮验证了真实App供包、设备安装、重启及版本确认；
  未执行OTA取消、断网或回滚分支，也未在764上重新要求用户做语音/相机验收。

新增证据仍位于 `out/shaniu-usb-idle-20261018/`：`nfc-removed-763/`、
`phone-49-removed-nfc-load.xml`、`app-ota-764-start.json`、
`phone-49-ota-764-final-metadata.log`、三段 `app-ota-764-uart*/`、
`app-ota-764-status-01/`、`final-764-status/`、更新进度/确认/回到首页的手机XML，
以及 `removal-and-app-ota-764-summary.json`。串口窗口之间有间隔，不宣称完整连续
OTA时序；所有观察任务已退出，端口已释放。没有提交或推送。


## 同日后续：OTA 首请求准入与拒绝取消时的来源寿命

本轮继续异常更新路径，保持 hardware-fast iteration；NFC 实体卡仍缺少，未用手机
驻留或无卡 LOAD 代替场景触发。固件程序未改，沿用 763/764 的同一 build manifest。

- 为取消验证准备普通 CP/AP OTA `0.7.119+765`，包 3417063 B、SHA256
  `31b215d9894192d30371a4369282202578ff40b1f6ea97414fa43c7eb5c6f6b3`。
  package/trust、手机复制哈希及同 build manifest 检查通过；未写启动区或更换身份。
- APK49 两次来源准备成功后在手机本地拒绝首个 `OTA_BEGIN`，UI提示未开始升级。
  后续板端回读仍为 764 confirmed、manager idle、双核健康，供包端口已关闭。
  旧日志缺少 readPending，不能唯一归因；源码明确在途读取会拒绝新 BEGIN。
- APK50 将尚未发送的首请求保留在现有来源 lease 中，读回后重新检查前台、来源、
  generation、认证、新鲜能力及在途写入，再仅发送一次。退出前台或连接变化使准备
  失效，不排队已发送的 BEGIN，不重放升级；Session 原来的拒绝约束保持。
  同一 owner 的发布重入前先摘除 callback。新增有意义的回归覆盖读取前零 BEGIN、
  成功后恰一 BEGIN、失败/不支持不发送及旧 lease 不能复活；相关 67 项测试通过。
- APK50 实测命中等待路径：手机开始时 readPending=true；97ms 后来源 READY，
  随后 readPending=false 才发送 BEGIN，板端接受来源并建立 TCP/TLS 下载。
  该证据验证新的等待路径，不反推 APK49 两次拒绝的唯一原因。
- APK50 两次下载分别在 AP offset 901120/2158592 返回 -129、98304/2158592
  返回 -111，均 staged=0，764仍 confirmed。两次操作中均点击过取消，但旧 App
  没有保存取消回执日志，不将它们标为取消成功。
- **时钟更正**：最初把串口行内时间与手机/电脑时间相减，得出“错误早于取消
  31秒”，该判断撤回。串口行内时间来自板端，采集器没有逐行主机接收时间，未做
  跨时钟校准；两域绝对时间不能用于确定点击与失败的先后。
- 源码发现确定的寿命错误：板端 `source_open()` 写入目标版本后设置
  commit_sensitive，后续取消返回 -EALREADY/-114；APK50 的对应分支却关闭仍被
  设备下载使用的 HTTPS source。APK51 保留来源、启动 lease 和前台保持，明确提示
  更新继续，并记录取消结果码。-129还可能由普通TLS错误映射而来，不能直接写为
  证书错误或轮换信任；旧两次失败与取消的唯一因果仍缺直接回执证据。
- 该阶段候选 APK51 `0.7.21-shaniu-ota-cancel`，18369273 B，SHA256
  `e9666364ec2785d9542c9669438a56e04c180a1ce6175ba396a3f0f678f679fb`；签名与
  APK49相同，同签名 install-r 后拉取实际 APK，字节哈希匹配。相关 67 项测试及
  assembleDebug 通过。没有重置手机数据、重新认领、改变音量或更换设备身份。

原始失败、时钟更正、测试/APK 和当前观察保存于
`out/shaniu-usb-idle-20261018/`：`android-ota-765/`、
`phone-49-ota-765-two-starts-metadata.log`、`ota-765-clock-correction.json`、
`ota-765-terminal-status/`、`ota-765-second-terminal-status/`、对应手机XML与
`app-ota-765-*.json`。本节后续实测结果在下方追加，不把构建或点击等同实板通过。


### 拒绝取消后的实板闭环与 START 回执竞态

- APK51 供包765期间，取消回执为 `-114/sourceAccepted=true`，供包端口41683仍监听。
  后续进度持续增长，独立UART读到 AP 1507328/2158592、error=0；随后捕获
  `TRIAL ARM/CONFIRMED slot=0 counter=765`。App显示“已确认完成升级”，独立回读
  A槽 `0.7.119+765`、manager idle、healthy=88710ms、faults/recoveries=0/0；
  确认后端口41683已关闭。这验证了拒绝取消后持续前台供包到终态。
- 766使用同一构建程序，普通包SHA256
  `5675267fbae65823345c57f3b6682c48dea55f0e10dab3c3d50239d75fd58f55`，手机副本一致。
  在App已接受来源但显示0%时取消，仍收到 `-114/sourceAccepted=true`。0%不能推导
  设备尚可取消；下载继续至84%及写入，UART捕获 B槽766的 ARM 与 CONFIRMED。
  独立回读为 `0.7.120+766`、manager idle、healthy=232060ms、faults/recoveries=0/0。
- 766重启后App未完成版本回读：18次观察至末次仍显示等待重连；手机日志存在
  GATT 133/19/255与连接超时，显式扫描无候选。APK54覆盖安装后扫描仍无候选。
  因此766记录为板端升级确认，不能补写为APK51自动重连通过。
  现场UART第二次确认manager idle、双核心跳3955/3961前进、healthy=434340ms、
  faults/recoveries=0/0；PC USB鉴权约0.567s完成，product ready=true、busy=false、
  volume=100，camera两次idle/admitted=true/valid=false。未发起采帧。
  保存现场后执行一次普通 `reset reboot` 排查蓝牙恢复，未清身份或执行工厂恢复。
- APK52修复来源上传期间的取消按钮：WAITING只设置本地取消标志，不应被自身在途
  写入或上次OTA终态禁用；仍等待当前ACK后发送单个CANCEL。68项测试通过。
  APK52仅构建，未安装。
- 收尾审查补到START回执竞态：取消请求可能早于START成功ACK，upload仍WAITING，
  原Activity因此漏记source accepted。现在由upload在成功START ACK后、发送CANCEL
  前通知来源拥有者，立即接受来源、清旧状态并进入结果核验。START拒绝不触发该事件。
  取消发送失败或被拒绝时，已接受的来源继续保留；仅未接受的上传失败走原清理路径。
  取消成功仍依设备回执释放来源，不自动重放命令。
- 最终APK54 `0.7.24-shaniu-ota-cancel-race`，18371325 B，SHA256
  `fd4b774cc56e2f5a1f91b5870ccd14e609d1678b061babc625c1f78ae47b1ec6`。
  同既有签名覆盖安装并拉取实际APK核对哈希；71项相关测试及assembleDebug通过。
  新增测试覆盖START ACK与取消竞争、取消发送失败、拒绝START不接受来源；原断线
  source lease测试改为通过实际upload接受事件驱动。APK53也是仅构建候选。
  这些回归不等同于实板精确复现START ACK竞态或前后台生命周期验收。

证据补充：`ota-765-refusal-verification.json`、`final-765-status/`、
`phone-51-ota-765-monitor-11.xml`、`app-ota-766-uart-final/`、`final-766-status/`、
`phone-51-ota-766-final-metadata.log`、`ota-766-reconnect-diagnostics/`、
`readonly-766.log`、`ota-766-reconnect-recovery.json`、`ota-766-reconnect-reboot/`，
以及 `android-ota-765/{tests-54-summary.json,apk-54-verification.json,build-test-54.log}`。


### 766 普通重启恢复与早期取消验证限度

普通重启后APK54扫描发现唯一候选，沿用现有凭据验证成功；随后App回读
`0.7.120+766`并显示“已确认完成升级”。Wi-Fi仍已连接，音量100%，没有重新认领。
重启前后的UART都有CP BLE初始化、校准就绪及Host Buffer提示，但缺少
advertising enable/disable与可发现状态；healthy及CP ble线程等待信号量，不能区分
设备未广播与手机扫描侧问题。普通重启恢复是已观察事实，根因尚未定位。
因此下一步应增加有界广播/HCI状态元数据，将其与手机扫描结果关联，而非直接改
蓝牙参数或将所有失败归因于CPU1。证据为 `phone-54-after-reboot-scan.xml`、
`phone-54-connected-sheet.xml`、`phone-54-766-confirmed.xml`及两次766启动UART。

767包用于来源上传期取消尝试，普通包3417062 B，SHA256
`c64569910d5b3c2377538944d0942cbe8f7fdf12d866b3d950b22573aa2ed8ad`，
package/trust及手机副本核验通过，程序仍与763—766相同。唯一start后，第一次界面
读取耗时12.449s，已错过来源上传窗口；脚本没有点击取消、没有取得取消回执，设备
继续普通更新。`phone-54-767-upload-control-0.xml`与开始前XML逐字节一致，采集器
此前未检查uiautomator的成功输出，原stdout未保存，故该画面按可能陈旧处理，
不用于证明按钮在上传时的状态。不能把本次计为取消成功或产品拒绝取消。
采集器随后先删除自己拥有的临时XML，要求dump成功标记并保存stdout；失败不再
读取旧文件。后续767进度观察使用此检查。本轮不追加768，也不为赶窗口中断已接受
的更新。来源上传期取消仍只有源码与回归测试证据，实机验收尚未完成。

对应证据：`ota-767-cancel-window-limit.json`、`app-ota-767-start.json`、
`phone-54-ota-767-cancel-metadata.log`、`hil_ota_finish.py`及带 `.capture.txt`
的后续界面文件。该采集限度不更改765/766独立UART的版本与健康回读事实。


### APK54 与 767 最终回读

767按普通更新完成：App新鲜进度采样从2%至88%，随后自动重连并显示
“设备已确认完成升级：0.7.121+767”。UART捕获 `TRIAL ARM/CONFIRMED slot=0
counter=767`；独立最终回读为A槽767 confirmed、manager idle、error=0，
healthy=175600ms，faults/recoveries=0/0，AP HEALTHY。供包端口38243不再监听，
ss返回0且取得3条TCP监听记录；netlink权限警告保留。手机管理已连接、Wi-Fi已连接、
音量仍100%。USB沿用现有profile鉴权约0.588s，product ready=true、busy=false、
runtime_error=0，两次camera状态idle/admitted=true/valid=false；没有请求新照片。
本次767未复现766的升级后重连异常，也未发送OTA_CANCEL，不能把它记为早期取消通过。

本轮最终交付为APK54及上述已确认的767；固件程序未改，仅普通OTA版本/计数和签名
元数据变化。71项相关测试与构建通过，实际安装APK哈希一致，限定文件diff检查通过。
先前失败、候选APK50—53和原始日志均保留。所有观察进程已结束，串口/USB已释放；
没有提交或推送。最终证据汇总为 `ota-54-767-final-verification.json`，另见
`app-ota-767-uart-final/`、`final-767-status/`、`phone-54-ota-767-monitor-12.xml`、
`phone-54-767-confirmed-lower.xml`、`phone-54-767-final-home.xml`、
`phone-54-ota-767-final-metadata.log`、`ota-767-source-terminal.json`、`readonly-767.log`。

接下来的优先工作是补齐升级后广播/重连的有界状态观测，并继续CPU1稳定性取证。
来源上传期取消需先建立可靠的实机操作窗口再验收，避免反复生成完整更新来赶按钮。
精确START ACK竞态与前后台生命周期仍限于源码/主机回归层；NFC实体卡场景继续等待
兼容实体卡，不用手机驻留或无卡LOAD代替。长期稳定性仍未通过。


## 升级后蓝牙异常排查：诊断版 APK55 / 768

本次从同一块板的 767 confirmed、manager idle、AP HEALTHY 与 APK54 管理已连接
开始排查。先核对当前 GATT/HCI/TLS 源码与 767 的冻结构建副本一致，避免用后来源码
解释旧现象。COM9 与 COM16 均由 PnP/串口枚举确认；没有 RTS/DTR、重新认领、恢复
出厂或改动信任配置。新证据根为 `../out/shaniu-ble-reconnect-20261018/`。

已发现但尚未证明命中的假设：`bkprov_gatt_poll()` 停止广播遇到非 `-EALREADY`
错误时会锁存 `g_radio_error`，关闭窗口并阻止之后重开，直到产品重启。固定 NuttX
版本先清 Host 广播标志再等待 HCI disable 回执，因此不能把后续 `-EALREADY` 当作
控制器已关闭的证据；本次保留这一错误策略，没有直接清锁存、改重试或调广播参数。
旧 766 的故障没有对应 HCI/广播日志，单凭重启恢复不能认定是这个分支。

为区分故障层，新增两处有界观测：产品 worker 在广播启停及状态变化时记录 window、
peer、Host 广播标志、CCC ready、连接代际、连接/断开次数、锁存错误和既有 HCI
opcode/status/counter；蓝牙回调仅累加持锁计数。手机 `ShaniuBleScan` 记录扫描实例、
首个匹配、结束原因、时长、数量、API 错误码及异常类名。没有地址、locator、TLS/ACL
内容或凭据；不新增 HCI 命令。`ready` 不是 TLS 鉴权成功，Host 状态也不是射频测量。
新增 syslog 有观测开销，本版不能用于代替未插桩版本的精确时延基准。

独立只读复核未发现阻塞性行为或隐私问题。既有 GATT 状态机测试通过，覆盖停止失败
锁存和原有队列/代际行为，并增加回调不输出日志、100 次稳定轮询不重复日志的检查。
全文件 nxstyle 的既有结构/格式问题仍存在（基线输出 63 行），不宣称全文件通过；
修正新增函数的行宽/空行后，抽取原文函数加标准 section 的局部 nxstyle 通过。
限定修改 diff 检查通过。Android assembleDebug 通过，本轮没有重跑先前的 71 项 OTA
测试，不把此前通过记为本轮新增验证。

APK55 为 `0.7.25-shaniu-ble-trace`，SHA256
`76fc202df0afaebd32ba640c52aa0df00bc89eb158c4f61d8b38b74fa348f87a`，沿用原签名，
`install -r` 后安装字节哈希一致。固件为 `0.7.122+768`，普通 OTA 包 SHA256
`24ea6f2ad35c6021349033174543673dea1d2c451843230473a5bde7bbebb988`；package/trust
核验及手机副本哈希通过。768 只有 AP ELF 改变，CP/BL1/BL2 ELF 与 767 相同，双核
配置/构建身份相同，冻结 GATT 源与工作区哈希一致，AP bin 含新增诊断字符串。
没有发布工厂包或更改 boot/config；以上只证明构建及安装对应关系。

APK55 首次扫描在约 220 ms 收到匹配，15 秒窗口记录 unique_matches=1，无扫描 API
失败。文件选择器导航与新鲜 XML 采集有失败，原记录保留；失败时未复用旧 XML，也未
补点更新。一次有效“开始固件更新”后，手机 metadata 为 authenticated/fresh=true，
来源 READY，UART 记录 `BKOTA App request accepted source=https catalog=pinned`。
设备随后进入普通 OTA，实板结果另在下文补记；不能仅凭接受来源称更新完成。


### 768 实板结果与定位到的启动冲突

767→768 只有一次 App start/source 接受，UART 为 B 槽 `TRIAL ARM/CONFIRMED
counter=768`；App 自动重连，准确回读 `0.7.122+768` 并显示升级确认。其后 App
退出，再次扫描发现唯一候选并沿用凭据验证成功，管理/Wi-Fi 已连接、音量100%。
独立串口回读为 pair confirmed、manager idle/error=0、AP HEALTHY、healthy=212870ms、
faults/recoveries=0/0。以上不等同于长期稳定性通过。

本次确实捕获启动异常：初始窗口未开时已出现一个 physical peer，断开后第一次
产品 start 返回 -5，HCI `complete=0a0c200a`（LE Set Advertising Enable，status
0x0c）；cleanup stop=0，约5秒重试 start=0，随后手机连接和 CCC ready。观察中又有
一次完整 boot；用户明确确认期间按过 RESET 或重新上电，记录为人工重启，不能记为
自发复位/看门狗/功耗故障。第二次启动在连接数为0时同样出现首个 start=-5/0x0c，
随后 stop=0、重试 start=0，因此“只因早期连接清理未完成”的推断不足。

源码定位到实际第二个广播开启者：固定 NuttX 的
`wireless/bluetooth/bt_netdev.c:1280` 在 `CONFIG_WIRELESS_BLUETOOTH_HOST` 下调用
`bt_add_services()`；`bt_services.c:230` 注册默认表并主动 `bt_start_advertising()`。
当前 resolved config 确认 Host=y。产品 `bkprov_gatt_register()` 原先只注册自己的表
和回调，却从 g_advertising=false 起步，未接管已开启的默认广播。该路径与两次启动
前置 opcode=0x200a、首次产品 enable 被拒绝及 stop 后成功的实板轨迹一致。它解释
本轮可恢复的启动冲突，仍不能直接证明历史766持续搜不到设备也是同一根因。

两次人工/OTA启动、两个无连接窗口超时以及后续重连的合并元数据中，产品 start
结果为 [-5,0,0,-5,0,0,0]，stop 结果全部为0，38条 radio event 均无非零 latch。
广播窗口超时后关闭和约5秒后重开正常。原假设“本次命中停止失败锁存”被本次观测
否定，但不能据此排除未重现的其他运行条件。`radio768-analysis.json` 保留完整事件。

USB旁证保留失败：首次只读查询内部两次TLS握手均在约5秒超时，未回放业务命令；
App关闭时再次查询成功（认证0.608353s），手机重新验证连接后再次查询也成功
（认证0.618299s），ready=true/busy=false/runtime_error=0。该序列不能证明手机连接
本身导致USB失败；期间有人为复位和语音活动，缺少充分控制变量，暂不另改USB实现。
原日志分别为 `readonly-768.log`、`readonly-768-appclosed.log`、
`readonly-768-connected.log`，均未请求新照片。

### 769 最小修正与验证口径

产品注册完回调后把已有默认广播纳入 g_advertising，仍保持窗口关闭；第一次
worker poll（包括 window(true) 内的前置poll）先按原有路径停止默认广播，再允许
开启产品广播。不直接操作 Host 私有全局量，不更改官方 NuttX、不改超时/广播参数、
不清错误锁存。stop错误仍阻止后续开窗；-EALREADY按原逻辑视为Host空闲。
回调注册后出现的早期peer继续由关闭窗口路径断开，TLS generation不会被置为ready。
边界：回调注册前已完成连接不会由Host回放，此补丁没有新增连接枚举，不能宣称完整
解决这个既有小窗口；默认广播也不是在Host创建的瞬间被禁止。

独立复核无阻塞问题；2个测试方法/4个编译执行fixture通过，覆盖原队列/窗口/代际、
初始默认广播先stop后start、初始-EALREADY、初始stop失败保持锁存、早期peer断开。
769与768相比仍只有AP ELF改变，CP/BL1/BL2与双核配置相同。普通包SHA256为
`10fe5d889ee7c55995bca12b99aaa85cec7bab4772fd642ce9f974e09a3804dc`，version
`0.7.123+769`；构建、package/trust及手机副本核验通过。APK继续使用55。
本次实板判据是首次poll停止默认广播成功，首次产品enable不再报-5/0x0c，App更新后
自动验证重连，并完成一次广播窗口关闭/重开检查。结果在下文补记。

### 769 复现持续失联：停止广播失败锁存

768→769 的 App 来源仅接受一次。设备完成 A 槽 trial confirmed，counter=769。
首次默认广播 stop=0，首次产品 start=0，证明本轮启动交接修正命中；但随后手机连接
时 stop=-5，产品关闭窗口并锁存 -5，断开 peer 后不再广播。紧随其后的 stop=-114
只反映 Host 已先清本地标志，不能证明控制器成功，也不能作为清锁存依据。
因此 **769 未通过更新后的自动重连验收，不能记录为持续失联已修复**。

当前事件的 HCI 最近完成值为 `0a00200a`（opcode=0x200a，status=0），而
post-769 boot 没有 HCI rejected 日志。该统计没有请求序号或 enable/disable 参数，
不能严格绑定本次 stop 与同一回执；只把它作为继续检查 Host 完成路径的线索。
`radio769-fault-analysis.json` 与 `ota769-uart/serial.raw` 保存实际时序。

手机随后完成一次 15026ms 扫描，`unique_matches=0`，没有扫描 API 错误。
故障未恢复时，COM16 仍可用既有授权完成 TLS 鉴权与只读 status；ready=true、
busy=false、runtime_error=0。COM9 回读 pair confirmed、manager idle/error=0、
AP/CPU2 心跳正常、supervisor faults/recoveries=0/0。因此该现场不支持“整机卡死”
或“CPU1 已死亡”的解释。证据为 `fault769-phone-metadata.log`、
`readonly-769-fault.log`、`fault769-health-uart/`。

### 同步 HCI 命令的生命周期缺陷与待应用补丁

固定 NuttX `5ecdd18b230acd2a86068cb4559b9f3f59363857` 中，
`bt_hci_cmd_send_sync()` 没有为等待者保留 command buffer 引用；TX 把自己的引用
交给 sent_cmd，`hci_cmd_done()` 写响应、post 后立即 release，等待者却稍后才从
同一 command buffer 读响应。缓冲池复用会清零该字段，成功 status 因而可能被读成
`-EIO`。另一个缺口是 timeout 后销毁栈 semaphore，但迟到 completion 仍可能使用
该地址。源码检查确认这些缺陷存在；它们与本次实板故障的因果关系仍需修复版上板
验证，不能由全局 last_complete 值单独确认。

`hci-sync-reproducer/` 保留原函数片段、确定性时序模型、编译参数及结果：
成功回执后先释放/复用再运行 waiter 得到 -5；超时模型记录迟到 post 会命中已销毁
semaphore。模型以替代队列/分配器控制时序，不是实板或 SMP 调度验收。
随后补充直接抽取、未经改写的两个函数原文并编译执行：原版成功回执/复用用例
确实返回-5；修复草稿的成功/无rsp释放、非零status、rsp转移、队列失败、超时后
迟到completion共5组源函数用例通过。平台API仍由stub控制，不能等同于并行SMP
测试。原版timeout不执行真实失效栈访问，只保留源码分析与安全模型；结果分别为
`actual-original-result.txt`、`actual-fix-result.txt`。早期手写模型未冒充源函数测试。
`wrap-probe/` 还验证了普通 GNU ld `--wrap` 不会拦截同一编译单元内该函数的调用，
因此没有加一个实际无法覆盖 bt_stop_advertising 的兼容 wrapper。

可审查草稿保存在证据根的 `hci-sync-fix-review/hci-sync-lifetime.patch`，只涉及
上游形态的 `wireless/bluetooth/bt_hcicore.c` 与 `bt_buf.c`：等待者保留独立引用，
响应放在独立栈 context，completion 与 timeout detach 共用锁，buffer 引用计数
改为原子操作以支持 TX/RX/waiter 并发释放。没有清广播错误锁存或放宽身份验证。
独立源码复核未发现阻塞问题；异常 previous sent_cmd 分支仍靠原 2.5s timeout
退出，没有宣称修复全部 Host 异常路径。

草稿的两个文件用当前 AP compile_commands 的原 ARM 参数分别编译到证据目录，
均通过，没有链接固件或替换构建树对象。使用同一 pinned nxstyle 源重新编译以去除
旧本机 TOPDIR 绑定，并按原相对目录放置逐字节副本后，两文件样式通过；旧工具路径
检查失败记录保留。草稿当前未应用、未生成770、未上板。仓库 AGENTS.md 的
“only writable submission source / official repositories free of team-owned
tracked edits”边界仍有效，官方 checkout 两个文件保持原哈希。

### 故障现场保留后的普通重启恢复

保存上述现场后，本轮由工具明确发送一次 `reset reboot`，不是自发重启，也不是
重新烧录；没有清配置、重新认领或更换身份。`recovery769-uart/` 记录首次默认 stop
和产品 start 均为0；随后 fresh scan 找到候选，手机用原凭据验证连接，连接后的
stop=0、CCC ready=1、latch=0。`recovery769-reconnect-uart/` 回读769 confirmed、
manager idle、AP HEALTHY、faults/recoveries=0/0。手机管理/Wi-Fi已连接、音量100%，
更新页最终显示“已确认完成升级”“已核对新版本 · 无错误”。已接受的手机固件来源
在确认过程中保持，不以 force-stop 中断供包。COM9 采集完成后释放。

此次恢复只证明当前769可重新连接，生命周期缺陷仍在已安装固件中，不能把一次重启
成功记为根因修复或长期稳定性通过。下一步是完成底层补丁的受控集成与同路径实板
复测，再继续此前的稳定性顺序；不扩展到缺少实体卡的 NFC 卡片验收。


### 本次授权后的 HCI 集成与 APK56 重试恢复

用户明确“此次允许”后，仅把已审查补丁应用到官方 NuttX 的 bt_hcicore.c、bt_buf.c，
同步到本轮冻结构建副本。官方基线仍为上述提交，补丁 SHA256 为
`f29efc2f072ea2f4cd136103f3a8f1e78569ee52dfc07f7be4b9aa2d0841e984`；
`hci-fix-770-applied.json` 保存授权与前后哈希。kernel_compat 同时更新实际来源
与两个文件的保护哈希。公开 manifest 仍指向未含该补丁的基线；没有提交、推送或
宣称全新同步即可重现此本地修改。

770（0.7.124+770）增量构建、package 与 trust 验证通过。与769相比只有 AP ELF
变化，CP/BL1/BL2 及双核配置不变；ELF包含同步锁和修复函数，引用计数反汇编可见
ldrexb/strexb。普通包 SHA256 为
`7dde8dac3df90fc4441fca27f716160e526684b7846d7b1be5ee3b5c9f4fdbae`，
手机副本一致。6项兼容保护测试、1项直接抽取源码的HCI测试（内部5条执行路径）
通过；后者由确定性API替身控制时序，不是SMP压力测试。新测试及兼容模块的
black、官方两文件 diff --check 通过；两文件 nxstyle 沿用输入一致的审查结果。

21:26:46首次点击770升级期间，设备正在语音交互，OTA_START返回-16；UART没有
接受来源记录，未开始写入。这不计为成功升级。APK55却持续保留目标770等待，
重新认证后查询到旧769的成功终态仍不能恢复按钮。`ota770-attempt1-refused.json`
和相关UI、数值化语音时序保存现场；曾尝试的第二次点击被disabled检查拦截，
没有发出另一次START。故障后USB原授权只读查询ready=true/busy=false，
runtime_error=-61，该值单独保留，不解释成整机故障。

APK56（0.7.26-shaniu-ota-retry）仅在当前认证代际的可信STATUS+INFO显示同设备
已成功结束旧更新、实际版本/计数与待核对目标不一致，且无准备或已受理来源时，
结束旧等待并允许重新发起。非零START回执本身不清pending：固件可能先入队再在
状态查询阶段返回错误。准备中来源的lease guard继续阻止旧缓存释放当前事务。
42项OTA定向测试与APK构建通过，候选APK SHA256为
`961c924108dc1b334ef04f53e377f1ea01b051d67fde04d59ebcfec7e1030e95`。
同签名install -r成功，安装后APK逐字节哈希一致，保留原认领及应用数据。
这些构建与安装结果不替代下述770实板结果。


### 770 实板：生命周期补丁不足以消除连接后失联

APK56重新认证后确实识别旧769成功终态与目标770不匹配，结束旧等待；选择770后
开始按钮恢复。21:45:57第二次实际START，仅一次点击，板端记录一次来源接受。
UI下载进度持续推进至86%，随后设备重启；首个post-click UI抓取失败只重试读取，
没有重复点击。来源被接受到新固件启动之间App一直在前台供包。

设备 B 槽（slot=1）counter770完成TRIAL ARM→CONFIRMED（新CP计时0.891657→
12.260125，约11.37s）。这证明镜像安装和启动确认，**不代表App重连成功**。
首次默认stop=0、产品start=0，但手机首次peer连接后，stop从AP启动计时
3.824407等待到6.330438，返回-110（约2.506s），窗口关闭并锁存-110。下一次
stop=-114不清锁存，随后peer断开；手机无法完成目标版本回读。

`radio770-fault-analysis.json`、`ota770-install-uart/`和对应 `.log` 保存现场。
last_complete在失败点为0a00200a，但没有同一请求序号，不能把它直接等同于
本次disable已获完成。此次没有HCI rejected、Uncleared pending sent_cmd等日志；
现有证据无法区分尚未进入Host完成路径、迟到完成、错误归属或其他通知问题。
源码复核未发现匹配完成进入新hci_cmd_done后无条件跳过post的路径；原有条件式
未匹配返回、命令credit处理等需以逐请求实测决定，不凭猜测继续修改。

故障状态下COM16原授权TLS鉴权约0.91s完成，产品ready=true/busy=false、
volume=100、runtime_error=0，见`readonly-770-fault.log`。设备整机可响应，
故此次不支持把BLE失联归因为整机死机。770因此**未通过OTA后自动重连验收**，
本轮没有进入所计划的五次正常重连回归，也不宣称长期稳定性已通过。
下一项有界诊断应把enqueue、TX、底层RX、Host完成、semaphore post/timeout
用同一请求标识串起来，保留opcode和时间，不记录私有控制payload。


### 770 恢复尝试、主核超时与最终状态

首次故障后15026ms新扫描unique_matches=0。故障健康快照仍为pair confirmed770、
manager idle、AP HEALTHY、faults/recoveries=0/0；随后进行普通reset reboot，
没有擦除、重新认领或更换签名。重启后可扫描到同一设备，广播stop=0，但手机在
MTU阶段30048ms超时，随后服务发现超时，尚未完成身份验证。

已知770 trial confirmed且manager idle后，才force-stop并重新启动App，保留数据；
新进程扫描仍为0。第二次普通重启后再次发现设备、stop=0，但新App进程仍在
discovering阶段30048ms超时。因此恢复尝试不能记为“手机已恢复连接”。两轮证据
为`recovery770-uart/`、`recovery770-phone-restart-uart/`、
`recovery770-clean-reconnect-uart/`及对应UI/GATT日志。

随后`final770-health-uart/`出现新事实：rptun state=6/error=110，supervisor
FAULTED/PRIMARY_TIMEOUT，faults=1/recoveries=0。此前的HEALTHY是更早时点，
不能覆盖此后失败。USB最后鉴权连续两次5s超时，关闭App再做一次只读对照仍失败；
没有把早先USB成功记作最终状态。这一观察扩大了后续排查范围，但尚未建立它与
本次HCI补丁、较早BLE超时之间的因果关系；此前主核心跳问题也未因此被解释。

在手机App保持关闭、停止自动重试的条件下，第三次普通重启用于恢复独立运行。
`restore770-standalone-health-uart/`回读仍为0.7.124+770 confirmed、manager idle、
rptun state=4/error=0、AP HEALTHY、faults/recoveries=0/0。USB原授权两次只读
鉴权恢复成功，约1.55s及1.63s完成，但两次产品状态均ready=false/busy=false、
runtime_error=0、volume=53。这里不把内核健康或USB成功等同于语音就绪；此前
volume100与当前53的差异保留，恢复操作没有写入音量或清理配置，未推断变化原因。

本轮最终保留：770固件及APK56已安装；App自动重试已停止，手机管理连接未恢复，
语音ready尚未恢复。串口采集和USB会话均结束。需要优先补充逐请求HCI/ATT与主核
心跳时间线，定位超时后再继续稳定性与功能验收；没有进行NFC实体卡验收。

## 22:48 后：按用户要求将 OTA 放最后，继续桌面功能

用户明确要求恢复原有功能顺序。前述“优先补充 HCI/ATT 时间线”不再是本次
执行优先级；已通过的真人语音和手机连接按其原版本保留。本窗口没有升级、
重启、重新认领、修改授权或改写固件；结尾经原生 USB 独立认证回读
`0.7.124+770 / security_counter=770`。

- 动作基线：`bkmotion status` 返回真实周期缓存，时间 2170823594 us、
  加速度 (-156, -745, -9767) mm/s²；双屏 READY、last_error=0、
  shaniu-cyan-v3/revision3，健康电压 3980 mV。用户明确回复
  “已操作，看到表情变化”。本次 180 秒串口窗口没有捕获 BKCOMPANION
  事件，因而保留真人视觉确认与日志证据的区别，不声称完成逐事件时间关联，
  也不把表情变化当作马达体感验收。
- 真实 Windows 子进程正常退出：`task-run` returncode=0、sequence=2、
  device_result=confirmed；板端 22:50:43.827 提交
  `BKFOCUS display visual=00000400 sequence=1380`。这补齐此前仅有任务
  终态回读、没有对应 LCD 提交的证据。
- 第二个真实子进程达到 1 秒期限后被终止：process_state=canceled、
  reason=timeout、device_result=confirmed、sequence=4；CLI exit130
  为预期取消结果。板端先恢复默认画面，再于 22:51:23.454 提交
  `visual=00000600 sequence=1384`。两条任务均由现有正式 runner 管理，
  没有直接注入终态，也没有把 `feedback_pending` 当成屏幕确认。
- 按需新拍单帧：13575 B JPEG，512 B 传输分块，帧传输约 2.240 s，
  采集至传完约 5.257 s。Pillow 10.4.0 在内存中执行完整 `load()`，
  结果为 JPEG/RGB/640×480；SHA256 为
  `86658ede02b922b03cd2e6d4698ccbd228600f73d4b2ed733b35979885977205`。
  release_confirmed=true；随后新 USB 会话回读 canceled、valid=false、
  admitted=true。没有保存、展示图像或上传至外部服务；解码成功不代表图像内容/画质
  已经人工验收，也不等同浏览器取景页已验收。

结束时任务终态按原 60 秒期限自然过期，pending=false；相机缓存无效，串口
采集与 USB 会话均已关闭。NFC 仍只有模块、没有实体卡，本窗口没有刷卡或
由卡片触发本地内容的实测结果。未发现需要据此修改生产代码的故障，未扩大
到额外固件构建或基础语音/手机连接复测。

证据保存在工作区 `out/shaniu-features-20261018/`：
`motion-baseline-r2/`、`motion-live/`、`task-run-{success,cancel}.stdout`、
`camera-decoded.stdout`、`final-readback.stdout`。`motion-live/serial.raw`
SHA256 为 `05cc3f3faaf261521f6c9efca037e9ffda6426ba1cbad0eeab76246e6da1a232`。
首次 PowerShell 执行策略拒绝与不适用的 `bkap status` 命令输出仍保留在原
目录；它们是主机调用/命令选择错误，没有解释为板端功能失败。

## 电脑取景页：修复浏览器拦截图片及缩放样式

继续原功能顺序时，NFC 场景/内容的软件合同复核未发现确定的新缺口；缺卡
仍仅限制实体刷卡。电脑任务已具有前节实板证据，本轮推进尚未验证的浏览器
取景入口，没有重新验证基础语音/手机连接，也没有进入 OTA。

正式 HTTP 服务的 CSP 只有 `default-src 'none'`，未声明 `img-src`，而页面
用 JPEG data URL 显示当前帧。Chrome for Testing 151.0.7922.34 加载正式页面
和控制器、仅将设备结果替换为 2×2 JPEG 夹具后，显示“有效单帧”但实际图片
宽高均为 0，并收到 `img-src / data` 违规事件。新增的现有 HTTP 头部测试断言
也先因缺少图片来源失败。此前 CLI 解码通过无法覆盖这一浏览器约束。

最小修复仅给图片允许 `data:`；默认拒绝、同源脚本/API、禁止嵌入等策略保持。
同一 Chromium 夹具随后实际解码为 2×2。另将相机图片原有的
`max-width:100%;height:auto` 从被 CSP 拒绝的内联属性移到既有样式表，
没有加入 `unsafe-inline`、开放远程图片或改变页面设计。

Windows Chrome 155.0.8059.80 无头浏览器，通过正式页面按钮、HTTP Service、
原生 COM16、现有 DPAPI profile 和真实板端服务完成：

- 首次取景 id2：15247 B，新 JPEG 在浏览器中解码并显示为 640×480，
  release_confirmed=true；页面读取状态进一步确认 canceled、valid=false，
  并清除页面旧图。
- 采集过程中点击取消，id3 返回 canceled、release_confirmed=true，页面未
  显示残留图像；随后再次点击取景，id4 的新 15055 B JPEG 正常显示，释放确认。
- 同一真实预览在 1280/375 px 视口分别显示宽 462/278 px，文档宽
  1265/360 px，没有横向溢出；最终 CSP 违规事件数为 0。

首次 Windows 自动化误选 Chrome 扩展后台 target，页面导航返回 ERR_ABORTED，
没有创建设备任务；保留失败记录。改为明确选择 about:blank 的 page target 后
完成上述实际流程，不把自动化选择错误归因于设备或产品。

12 项工作台 HTTP 测试通过；Black 24.10.0 检查与相关 diff 空白检查通过。
未更改固件、授权、App 或 OTA。关闭了本次浏览器、HTTP 服务和 USB 会话，
worker_alive=false；未保存相机帧或上传至外部服务，未宣称真人画质评价通过。

证据目录为工作区 `out/shaniu-workbench-preview-20261018/`：
`browser-red.json`、`browser-green.json`、`windows-browser-page-check.stdout`、
`browser-hil-r2-results.json` 及对应调用日志。实际浏览器/板端结果文件 SHA256：
`f69108e0e265be8fc8e682e4bf883fe8afd7b31cccc99fad8cd1922d46434d06`。
