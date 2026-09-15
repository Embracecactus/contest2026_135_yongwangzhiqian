# 傻妞 AIDK AI Toy 全项目 Master Plan

状态：`IN_PROGRESS`

## 2026-09-15 比赛冲刺当前状态（基于 4da7f80e）

状态仍为 `IN_PROGRESS`。`4da7f80e` 与已提交的 `073fb7ec` 内容树相同；本节之后
的 531/532 记录是历史失败证据，不继承为新版本通过。源码、增量构建、下载和实板验收分别记录。

- 当前聚焦 CP HardFault、播放完成/MIC 恢复和 App OTA 写入校验三项阻塞。
  没有训练、依赖升级、额外测试平台或旧语音 runtime 回退。
- 已核对 531 CP 签名段解码后的完整载荷与故障分析时的 CP BIN 一致。
  `PC=0/CFSR=00020000/LR=02016747` 仍未定位根因。新的只读 procfs 显示 OTA
  线程栈在片内 SRAM，BLE 栈在 CP PSRAM；不能将两类故障直接归为 PSRAM 栈问题。
  现有 HardFault UART 记录补发已经保存的 R0–R3/R12，不改变异常或复位策略。
  历史 `voice521-522-retained-build/cp.elf` 的 SHA256 为
  `8a6568bc33eea71d96cee845ce165de54dc4294beadc09325f59ad7a08ce4b77`，与 531
  故障产物一致；522、526、531 的 CP raw SHA256 均为
  `eced44137d444dbb51a4584a52b01f30938832d4b9c2da1c483e4f6f23c448ac`。
- 本地 FFmpeg 0007、Media 0006、Agent 0008 补丁接通独占 Music 路径的设备
  FINAL/COMPLETE、STOP/RELEASE、graph unlink、Media 终态和 playback close。
  普通结束等待设备释放；取消/失败保留清理回调，等待有界。没有 sleep 或假 latency。
  官方 checkout 无受跟踪修改；这些是构建时应用的本地未合入补丁。
  533 单次独立合成物理回放中，设备日志顺序为 pcm0p complete（09:52:09.765531）
  → voice request complete（09:52:09.804281）→ Trigger rearm（09:52:10.094344）。
  125 秒观察内无 CP HardFault、MIC -EBUSY 或 pcm0p IOERR；这不证明 CP 根因已修复。
- CP SDK `cp-flash-notification-errors.patch` 修复 ACK 循环耗尽仍成功和擦写忽略
  prepare/finish 错误。只重建 `cp-aidk`，SDK pin 不变，bundle tree 从
  `d62004bf57db326149324184cf3c818914d77f46c66ad5ef8d053b67569c68d0` 更新为
  `86ca1ff783e9ea357eee646903e9f34d2b02ff1012de238e3e23c570e4b20fb1`。
  AP SDK 不变。它不补充 AP 停核协议，也不证明 532 读回不一致根因已解决。
  校验仍失败关闭，补充记录失败后重读是否匹配，不重试写入或放宽接受条件。
- `sprint-p1-build.log`、`sprint-cp-sdk-build.log`、`sprint-p2-build.log` 均成功。
  533 调试包通过既有信任校验，包 SHA256 为
  `3fcea09f248c96004e6a1d50d5501d8db909ab3e940b40a4aab19484a1e3b405`。
  下载前回读是 531/A confirmed；此次仅授权范围内 B 槽 CP `0x2ca000+0x132000`、
  AP `0x3fc000+0x187000`，保留 A/数据/启动信任。下载完成并确认 533/B、counter533；
  受控下载不计作 App OTA 通过。证据：`hil533-bootstrap/`、`voice533-boot.raw`。
- `sprint-camera-final-build.log` 的 camera/vision 增量构建成功（此前
  `sprint-camera-build.log` 是格式日志修正前的构建）。AP 角色
  `bk7258-role-3cfae67149d8362b` 的 raw 为 1478500B / SHA256
  `e076b62b5809e3e36906b61512d0664c21b3c2f2ff5aced0d467a31c1973f5a3`；CP 复用
  `d7443a041021ef71d86beead193d6e36a5f48eb3ca10335944ea0031083e255f`。
  解析配置启用 `CONFIG_AI_AGENT_CAMERA`、`CONFIG_VIDEO` 和
  `CONFIG_AI_AGENT_NETWORK_EXTERNAL`，链接图含 `tool_camera`、`tool_vision`、
  `llm_vision`。本地 0009 补丁只让原 camera_capture 在固定尺寸 V4L2 JPEG
  设备上协商格式并校验缓冲边界；已编入不等于 `/dev/video0`、外置 LLM 传输或当次
  图片工具已经在实板验证。
- 533 独立合成物理回放 1 次尝试、1 次接受，Mi10 扬声器→MIC→官方 Trigger/自动收音
  → MiMo 批处理 ASR→官方 Agent/LLM→MiMo 全文本、逐块音频 TTS→Media 排空→重新监听。
  证据 `voice533-acoustic-r1*`。App 21/0.5.16-shaniu-ota 重新读取当前词为“你好冰冰”，
  设备实际模型 nihao_bingbing / SHA256
  `2ced56715079b8dcbed076ceef95d6340098949c7e52aea9001810baced15040`。
- 533 的无有效语音观察 1 次：录音 4.97 秒后 request2 返回 `-ENODATA`（-61），
  Media 关闭 MIC 后 Trigger 重新监听，之后持续收到 MIC 帧。紧接的普通语音观察
  1 次尝试、1 次唤醒、request3 complete=0，10:50:49.685562 pcm0p complete
  → 49.706281 output_released → 49.723531 request complete → 50.015000 Trigger rearm。
  证据 `voice533-no-speech-r1*`（65 秒）、`voice533-after-no-speech-r1*`（85 秒）；
  均为已有独立合成素材经手机扬声器回放，均未观察到 HardFault。没有改变门限、
  注入事件或发送无语音请求到云端；尚不构成最终候选三轮连续验收或真人效果验证。
- 首次 533→534 真实 App OTA 启动一次，534 包 SHA256 为
  `fc654c2a44acb2f8371c01eded41fda012bfed5e22843cf162a005f70080fd1d`。
  10:09:20 在 A 槽 AP 偏移 356352、地址 `0x0019afa4` 校验失败：missing_zero_bits=5、
  extra_zero_bits=0、stable=1、reread_matches_expected=0，manager-apply=-5/staged=0。
  App 显示设备错误 -5，仍回读 533；Trigger 随后恢复。A 槽已被失败升级部分覆盖，
  不能继续视为完整 531 恢复槽；当前 B/533 confirmed 保持有效。证据 `app534-ota.raw`、
  `app534-final-ui.xml`。SDK ACK 修复未解决此故障，当时停止重复发送；后续有新观察条件的
  同包恢复尝试另列于下方。
  对照历史 `app-ota-522-confirmed.json` 与 525→526 日志，两次确有 App OTA 成功；
  522 至迁移基线的 CP 固件完全相同，AP Flash 通知接收也仍在。优先比较 AP 迁移后的
  实际运行/跨核条件；历史已有间歇 Flash 校验失败，尚不能将本次根因归为迁移或硬件。
- 已用既有受信身份生成 535 的 CP/AP OTA 开发包（未安装），版本 `18.6.363+535`，
  2859879B / SHA256 `b7f2c8a77d8a3769e86ee6118713f4e3ecf3f431c2fa4e34a52e75eeff8564ac`。
  `verify535-package.log`、`verify535-trust.log` 均通过；对应 ELF/map/config/BIN 冻结于
  `voice535-ota/debug/`，没有重签 BL1/BL2。它包含当次图片接线，不包含已证明有效的
  OTA 校验根因修复；生成时尚未再次 App OTA，不作为稳定比赛候选。
  当时 App `app533-after-recovery-ui.xml` 回读 533/counter533 和 534 升级失败 -5。
  随后独立读取失败扇区 `0x0019a000+0x1000` 与 534 目标载荷比较；只读工具无
  软件复位选项，需要现场硬件 RESET 配合。第一轮只读工具打开 COM8，但 GetBus
  超时，未生成扇区文件（工具退出码 0 不代表读取成功），证据
  `flash534-failed-sector-read-r1.log`。宿主曾因审批模型容量拒绝执行，随后已恢复。
  第二轮实际成功接管、报告读取完成，但工具错误拼接 UNC 输出路径，未生成文件；
  不能据 Read Flash OK 宣称已拿到数据（`flash534-failed-sector-read-r2.log`）。
  改用短文件名后的第3/4轮接管超时。第5轮通过立即最终消息提示松开而接管成功，
  但文件名遗漏 `.bin` 导致工具实际只读地址0，不能用于本故障判断；未发生写入。
  第6轮使用短文件名加 `.bin@0x0019a000-0x1000`，成功保存目标4096B：SHA256
  `652e922d15e12ab12fa800bc738f90375bf57020f4d59b6f2fcbac76fdf6a1bb`。独立Loader回读
  仍只有 `0x0019afa4` 从应有 `0x98` 变成 `0xff`，5个missing_zero_bits，其余4095B
  与534目标一致，证明错误持久存在；不能仅归为运行时校验读错。证据
  `flash534-failed-sector-read-r6.{bin,log,comparison.json}`。
  为区分介质与运行时编程路径，已沿同板有界下载授权，仅向失败闲置A槽
  `0x0019a000+0x1000` 下发534已验签AP载荷的对应4096B；原失败数据已保存，
  B/533 confirmed、manager idle、USB CDC 已在操作前回读。来源切片SHA256
  `95480bce9f01e6bc74031b08d2697f11633f62d3320af20618ed3f21980ce395`，证据
  `hil534-sector-program/`，下载报告成功。工具日志使用64K Erase文案，实际擦除边界
  当时尚须独立回读A槽 `0x190000+0x10000` 核对邻区。它不是新固件安装或App OTA通过。
  后续64KiB只读在21:08:41（本地时间）握手成功，确认起址和长度后进入ReadFlash，
  随即LinkCheck Timeout，60秒有界停止且未生成文件（`flash534-after-rom-read.log`）。
  不能据此判定目标写入或邻区完整性。后续使用现有CLI支持的逗号分段读取，
  将相同64KiB范围拆为16个4KiB；4KiB大小已有第6轮成功依据。分段首轮于21:12:37
  在握手阶段GetBus fail，未进入ReadFlash、没有文件（`flash534-after-rom-read-sectors.log`），
  不能用于判断分段参数或Flash数据。分段第2轮成功保存0x190000至0x196fff共7个扇区，
  28672B逐字节匹配534 AP载荷，已读取邻区未丢失；随后第8段失联，目标扇区仍未取得。
  证据`flash534-after-rom-read-sectors-r2.{log,result.json}`及逐扇区BIN。60秒有界停止，
  未写Flash。随后目标优先的3段只读全部完成（`flash534-after-rom-read-target.{log,result.json}`）：
  0x19a000扇区4096B完全匹配534载荷，SHA256为前述95480bce…ce395；相对失败原件，
  仅偏移4004由0xff变为0x98。前邻区0x199000也匹配，后邻区0x19b000全为0xff，
  符合失败后尚未写到的记录。只能证明该位置本次Loader编程成功，不能宣称App OTA修复。
  回读后仍为B/533 confirmed、manager idle（`flash534-after-rom-board-status.raw`）。
  利用533已编译的NSH `xd`及同版CP ELF只读取得SDK状态：ID=c86517、容量8MiB、
  线模式4、时钟选择字段1/分频字段1（`flash533-runtime-config-read.raw`）。这些是
  控制器/SDK事实，不能单凭JEDEC ID确定封装内Flash的外部器件时序条件或故障根因。
  Git提交`6b79a1bfa`（2026-08-03）已在[N5硬件结论](nuttx-port/n5-flash-filesystem.md)
  与[N15接口边界](nuttx-port/n15-ota-source-verification.md)明确集成Flash及外部手册仅作旁证；
  本轮漏查该既有结论，新增时钟方案已撤回，未生成或部署新固件。SDK已恢复为
  原86ca1ff7…0fb1，恢复构建PASS（`sprint-cp-sdk-restore-approved.log`），既有ACK修复保留。
- 535 包已通过现有签名校验并在 App 选择；14:06:42 UTC 的真实 App START 被设备
  接受，但随后 `stage=intent result=-114 staged=0`，未进入镜像擦写。当前仍为
  533/B confirmed。上次 534 的受保护 DOWNLOADING 事务已记录目标，现有恢复契约
  只接受相同 catalog 的 RESTAGE；不能清除事务或换包绕过。证据 `app535-ota.raw`
  与 `app535-start-actions.json`，不记为 535 部署或 Flash 写入诊断通过。
  现有 NSH `xd 44030024 8` 读到 CRC 开启、计数为0；该计数属于 Flash→CPU
  读取路径，不等于编程成功或 OTA 写后校验，零值不能排除实际写入错误。
- 按同一受保护 catalog 重供原534包，14:13:30 UTC接受，14:17:00再次真实校验失败：
  地址`0x0027b4a4`、AP偏移1277952，missing_zero_bits=3、extra_zero_bits=0，
  两次读回一致且均不匹配；没有再次重试写入。`app534-crc-ota.raw` SHA256
  `e0a6d7460a12d8eabd1daa9b8a796c9f11df6a238db1c0e576f501a3be25b9bc`，
  `app534-crc-final-ui.xml`回读仍533/计数533、失败-5。600秒记录中10次CRC读数
  都为0且检查开启，没有HardFault，失败后Trigger恢复MIC帧；不构成OTA或稳定候选通过。
  失败后用533 CP ELF定位、只读SRAM `g_bk7258_ota_write_sector` 4096B，与签名包
  对应扇区完全一致（SHA256 `4b45a463c95763066a20da798a59725bc77d310e7b521ed61b171fed21d75d38`）；
  证据`app534-crc-retained-buffer-compare.json`，不代表已排除SDK内部瞬态错误。
  实际ELF的SDK临界区使用BASEPRI=0x80，纠正先前按配置推断PRIMASK的错误；
  `flash533-nvic-read.raw`回读64个外部IRQ优先级均0x80，因此没有外部IRQ穿过该阈值的证据。
  实际AP链接的Flash client经IPC到CP，未发现AP直接操作Flash FIFO的实链。
  写入故障仍未定位，未调整时钟、屏蔽异常、增加写入重试或放宽校验。
- 三轮连续普通交互、当次拍照、App 控制/模型切换及成功 App OTA 尚未在本候选集中验收。
  camera_capture 与外置 LLM 传输现已编入，App persona/云模型写入和按键仍待必要迁移。
  现场真人效果未验证；本地 TTS 引擎/模型未验证，旧加密长期记忆保留未迁移。

## 2026-09-15 历史迁移证据（4c22a945 至 531/532）

本轮增量基于已合并的 `4c22a945`，其父提交为 `447b91fa`；没有依赖升级，官方 Agent、Media、
NuttX 与 apps 均保持原 pin。本轮仍是产品适配迁移，不将旧 voice runtime 重新作为
正式消费者。

候选源树是 `out/shaniu-official-candidate-source/contest2026_135_yongwangzhiqian`；
现有唯一构建入口仍映射原工作树，本轮固件构建时其 HEAD 为 `1cd4a8c0`、包含未提交增量。候选文件
逐项校验后同步，未覆盖无关修改；构建 manifest 如实记录该 HEAD、dirty 和输入树哈希，
不能把单独的 `4c22a945` 当作可复现此次二进制的完整源码身份。
本次 PR 提交已有迁移进度；531/532 和 App21 的下述证据保留各自构建身份，
不将源码发布记为新的部署或稳定候选验收。

- 敏感配置缓冲在 heap 上统一 zeroize；`g_identity` 是唯一借用 provisioning 生命周期
  的身份对象。受保护 config 已接现有 network restore/time/owner 泵，ASR、LLM、TTS
  由各自配置独立激活。
- 官方 message metadata 已关联 terminal；`480000B` PCM 对应 15 秒，5 秒无话等待。
  Media Trigger 以 WKM1/SHA 管理活动资产，WKS1 READ 回读实际加载者，并复用既有 CAS
  导入/恢复事务。TTS 本地接口只预留，尚未实现。
- 官方 session manager 目前仍使用临时 tmpfs；旧加密长期记忆保留、尚未迁移。
- 本次仅裁除无本轮消费者的 Web/files/music tools、依赖它们的 builtin skills 和 fb
  example；没有裁除 Media、Agent、voice、camera 或 OTA 机制。

当前 `.su` 单函数证据为：product-config 120B、identity_load 1016B、cloud_configure
416B；它们不是完整调用链峰值。当前增量构建 `build-p0-12.log` 通过；模型适配器
使用显式分配、placement construction 和成对析构，避免两处 `new(nothrow)` 引入
未使用的 C++ 异常运行库。AP 链接预算现在包含签名 TLV 和现有尾部保护空间。
之前 p0-4～10 的体积或签名失败仅为迭代历史，不是当前产物结果。

当前实板为 `18.6.363+531 / counter531 / slot0`：`BOTA TRIAL CONFIRMED` 与启动日志
已确认，App 也刚刚实际回读 531。531 启动直接恢复原持久选择“你好冰冰”并进入
Trigger 推理；这证明当前版本/槽位/模型恢复，不代表真人唤醒、完整语音轮或现场通过。
同 raw payload 的 App 验证包 532 已准备，App OTA 尚未完成；不得把 531 的受控槽位
安装或版本回读写成 532 OTA 通过。

531→532 的第一次真实 App OTA 于 07:50:06Z 点击，07:50:16.632 已接受，App 曾显示
4% 与 33%；07:54:23.936 `manager-apply=-5`、`staged=0` 终止。CP 在 image1、offset
1511424 的 `0x56de65` 报 verify mismatch：`missing_zero_bits=3`、`extra_zero_bits=0`、
`reread=0`、`stable=0`。这只说明两次观测不同，不能断言永久坏块或第二次已匹配 expected。
失败后 07:54:24.754 Trigger 已重新加载“你好冰冰” SHA
`2ced56715079b8dcbed076ceef95d6340098949c7e52aea9001810baced15040` 且 `wake ready=1`；
新鲜 `bkota status` 为 pair confirmed、531/slot0，manager state=7、phase=3、image=1、
progress=1507328/1601536、error=-5，supervisor faults/recoveries 均为 0。

**当前 531 仍不可按稳定候选验收。** OTA 失败后的一次独立合成物理回放（Mi10 扬声器、
媒体音量 130/150，既有冰冰词＋普通命令）触发了真实自动收音：113280B，MiMo ASR batch
ret=0，官方 message_bus/Agent 的 LLM ret=0，MiMo TTS 完整文本/逐块音频 ret=0，
798720B、24kHz/PCM16/mono。AP 时钟中 request1 在 08:09:45.430969 报 complete=0，
pcm0p 至 45.686094 才报告 complete，期间 MIC 多次 -EBUSY，且播放器出现 I/O error；
随后恢复 MIC 输入。两种完成时间不一致的问题在当前固件再次复现，不能算完整闭环通过。
LLM 7289ms、TTS 首块 2004ms 是设备软件计时；主机与设备时钟未对齐，不跨时钟计算首响。

同一采集随后出现 CP HardFault 与整板重启：
`E=3 X=ffffffed S=60703288 H=40000000 C=00020000 V=1 P=0 L=02016747 Q=60000000`。
当前 CP ELF 将 LR 邻近地址定位到 `systick_getstatus` 的 64 位除法调用返回处；这不是
根因证明，也不能据时间先后断言由语音或 OTA 引起。当前 `CONFIG_TIMER_ARCH` 下，
`arch_timer.current_usec → TIMER_GETSTATUS` 也服务普通线程与内核时钟读取，不能归因于
idle 路径。重启后 `/proc/19/stack` 的 BLE 栈范围包含该 SP，只能证明当前地址重用，
不能确定故障前任务；沿用既有 `ps`/procfs 取证，未新建探针。未发重启命令，
已停止后续回放/升级。
重启后只读状态仍为 pair confirmed 531、counter531、slot0，模型完整 SHA 保持不变。
此时 supervisor 的 0 计数属于新启动周期，不能抹掉刚捕获的硬故障。
证据 `voice531-acoustic-r1.raw`、`voice531-acoustic-r1-actions.json`、
`voice531-phone-audio-route.log`、`voice531-post-fault-status.raw`；本轮声学尝试1次、
接受唤醒1次，完整稳定通过0次。未做无话第二轮、10轮、真人现场或泛化验收。

527 是历史构建与声学证据：其候选包曾完成 CP/AP 公钥验签和包结构检查，启动重试曾
恢复受保护配置、Wi-Fi/时间、ASR/LLM/TTS 与 Media Trigger。527 的恢复下载不记为
App OTA 通过；历史 525→526 的 App OTA 后，526 曾因过早读取 CP 文件系统并缓存
`-ENODEV` 而未激活身份/配置。

两次既有独立合成素材通过 Mi10 扬声器→实板 MIC 进行尝试，目标均为“你好冰冰”，
实际 Trigger 模型 SHA 为 `2ced56715079b8dcbed076ceef95d6340098949c7e52aea9001810baced15040`。
第一次唤醒后不播放命令：成功进入自动收音，之后恢复 Trigger 输入；终态处采集有间隙，
不记为完整无话恢复通过。第二次唤醒并播放既有普通命令：ASR MiMo batch ret=0，官方
message_bus/Agent 调用所选 LLM ret=0，MiMo TTS 完整文本/逐块 PCM 输出 ret=0，645120B、
24kHz/PCM16/mono，经官方 Media 单次转换到设备 16kHz，request=2 complete=0 后恢复收音。
见 `voice527-acoustic-r1*.raw` 和 `voice527-acoustic-r2-actions.json`；未做真人现场或泛化。

这还不是完整恢复验收：Media 播放器队列完成后，request=2 在 06:27:40.488 结束，
MIC 重开短暂 `-EBUSY`，pcm0p 停止完成日志在 40.741。官方队列完成不能替代硬件排空；
当前 `get_latency` 在 NuttX adevsink 路径未实现，不能据此伪造完成确认。还需闭合输出
释放与重新监听的时序。首段日志为 LLM 5533ms、TTS 首块 1845ms；自动收音结束至设备
首次 write/start 约 12.384s，仅为这一轮软件时间证据，不是声学首响测量。

实板还发现短促开麦高幅度数据可使不足 200ms 的声音取消“无有效语音等待”，进而
等到 15 秒上限。0007 已将 5 秒条件按最低有效语音时长判断，并修正 auto/ptt 日志；
另补 SDC1 整轮 idle/未知错误字段映射。该增量通过 `build-p0-13.log`，已包含在当前
531 的 p0-19 构建中；不能把包含修复等同于端点/恢复实板验收通过。

App 已保留原数据安装 `0.5.16-shaniu-ota / code21`，APK SHA256
`d2e444bad32a6dafaa5bd12e1d2ef098ca06ac8581e687b4870b73ed0fdef1a6`。531 已由 App
实际回读版本；历史 527 的 WKS1 READ 显示“你好冰冰 · 模型 2ced5671”，与当时设备
加载 SHA 一致。527 升级页曾返回 `-138`、普通 STATUS 未声明 INFO/OTA 能力位，
不能以旧缓存替代当前 531 回读，也不能把该历史失败混作 532 OTA 结果。
App 21 已修正 OTA active/queued 的 `-115` 显示，并修正 MCP1 不支持时无限优先重读而
饿死 WKS1；增量 `assembleDebug` 通过，APK 5877438B、SHA256
`d2e444bad32a6dafaa5bd12e1d2ef098ca06ac8581e687b4870b73ed0fdef1a6`。原身份重新认证后，
App21 已自动回读“你好冰冰 · 模型 2ced5671”，与当前 531 Trigger 完整 SHA 一致；
见 `app21-install.log`、`app21-model-readback.json/png`。这验证了 WKS1 不再被失败的
MCP1 读取饿死；MCP1 配置迁移及新文案下成功 OTA 尚未验证。
现有 KWS host harness 的加载/推理/释放、目标标签和无效输入检查通过，但不代表
真人效果。本轮尚未开始 10 轮集中稳定性验收，旧 525 成绩不继承。

App OTA 产品接线新增于 `bk7258_agent_ota.c`：只持有一次 App 来源请求、HTTPS CA、
源生命周期和受保护 BVO2 intent；既有 AP OTA manager 仍拥有 Flash 写入、验签与取消，
板级 trial 服务仍拥有启动确认。产品回调先停 Trigger，拒绝活动语音/模型/配置事务；
普通 STATUS 声明既有 INFO/OTA 位，并注册 START/STATUS/CANCEL。目标身份在 image write
前经原 CAS 存储提交；不确定提交保留，取消需等待源关闭和 worker join，最多 5 秒。
中断后的手机供包不能自动重建；仅明确再次 START 同一 catalog 可恢复未决下载。
既有 OTA store/flow/manager host 检查通过（`ota-existing-host.log`）；最终接线增量
`build-p0-17.log` 通过，AP 原始 1474332B，SHA256
`813f0125cb97fd97aae0b36b0f3bdbe76e2bc786fbd7d40654819a4b4fa6aa79`。OTA worker 单函数
栈 424B，沿用 16384B 的 AP OTA 栈；不是完整调用链峰值。528/529 复用同一 raw payload
并按各自版本/计数重新签名，未重签启动组件。528 包 SHA256
`d007d72ea01a821bed3536d40c56ae4d9b4221ad13bafd8d866f7e2fe35edb64`，529 包 SHA256
`6f8779afaa3b481773f72771583a0c6c24763b8b7710e8e44348fe654d7951c9`，公钥验签均通过。
528 已通过同板非活动 B 槽分段安装并 `BOTA TRIAL CONFIRMED slot=1 counter=528`；
ASR/LLM/TTS 与受保护配置恢复。App 原凭据重连后已实际回读 build/counter528，OTA
status idle，见 `app528-ota-info.xml`；能力位和状态回调已实板通过，升级传输尚未通过。
528 Trigger 启动失败 `-ENOTBLK(-15)`：WKA1/WKM1 实际位于受保护 `/cpdata`，旧
`preferences_with_storage` 却要求先挂载 SD/FAT。已改为模型存储自身互斥，保留原有
文件完整性检查和 CAS，不移动/覆盖用户选择。增量 `build-p0-18.log` 通过，AP raw
1474068B，SHA256 `b4528aa0347224acb7c654d2ca22d41b07401212a59fe0eb71c72364e9af279e`。
实际 App OTA 改用包含该修复的 530；已签发的 529 未安装、不复用版本标识覆盖包。
530 包 SHA256 `359b05d61753541cdc1fda5c0acd48c5849737e53f2578f077575ae86e5581f6`，
验签及手机文件回读通过。App 单次开始 528→530 在 START 返回 `-EINVAL(-22)`，
未进入 manager/镜像写入，见 `app530-start-actions.json`、`app530-after-start.png`、
`app530-ota*.log/raw`。新增 adapter 调用 `bkcontrol_ota_request_parse` 的实参顺序错误；
旧构建的 pointer/int conversion 警告曾被遗漏。该函数还在输入校验前清零输出，因此
该错误不仅是包格式拒绝，也存在向错误地址写入的风险；不在该旧适配上重复请求。
已修正调用，为此源文件把不兼容指针、整数/指针转换和隐式声明提升为编译错误。
`build-p0-19.log` 通过，修正源无此类警告；531 已通过原受控非活动 A 槽安装并 `BOTA TRIAL CONFIRMED slot=0 counter=531`，
启动直接加载原持久选择“你好冰冰”，完整 SHA `2ced56715079b8dcbed076ceef95d6340098949c7e52aea9001810baced15040`，
`wake ready=1` 后连续推理；这验证了移除错误 SD 前置依赖后的模型启动，但不代表
真人唤醒或语音全轮通过。包 531 SHA256 `93bd52925b4c0b7a66169231d7c3b058e444dfde107489cf2b9ff6bef0f6e48d`；
同 raw payload 的 App 验证包 532 SHA256 `c2564e0a3f504c7ac217b50f6bd7c14a88b0e84dfb7741cc90a6334712c9cbbe`。
531/532 的 raw AP SHA256 `82b1955ab8cadd35aedc263ffa3a6201f30e253734794952b5ea93e5d6860e2a`，1474052B。
531 的 App START 实际调用尚待 532 升级尝试验证。不把复用组件的 host 通过当成新入口验收。527 没有该接收入口，需先
受限安装修复，再由真实 App 完成后续候选包升级验证。未增加官方补丁或测试程序。

待迁移产品能力：App persona/云模型写入、加密记忆、当前拍照工具、App OTA 实板验收、按键、
表情和马达。项目保持 `IN_PROGRESS`。

### 447b91fa 后纵向闭环接线（历史构建证据，2026-09-15）

以下记录的是父提交 `447b91fa` 的构建与接线证据，不代表当前 `4c22a945` 已通过构建、
部署或实板验收。

| 现有职责 | 官方接管者 | 必要适配 | 旧实现退出方式 |
| --- | --- | --- | --- |
| 唤醒、收音、重新监听 | Media Trigger、Agent voice channel | 三词模型资产、设备与产品事件映射 | 退出 wake owner/session 及 PTT/turn 编排 |
| ASR/TTS 服务 | `voice_asr_ops_t`、`voice_tts_ops_t` | 认证、MiMo 协议、流式解码和原生采样率 | 退出整轮 cloud runtime，仅提取协议职责 |
| 对话、历史、工具循环 | `message_bus`、`agent_loop`、`context_builder`、`session_mgr` | 服务传输、当次拍照工具、受保护存储 | 退出请求级 Agent 静态库和自研通用历史 |
| 播放、排空、音量 | Media Player/Policy 与完成、失败事件 | 表情、马达及 App 状态映射 | 退出重复播放及音频仲裁 |
| App、认领、配置、OTA | 现有系统及产品服务接口 | 保留认证、数据格式、模型恢复及迁移 | 从旧 voice runtime 拆出设备管理职责 |

已核对的官方缺口：通用 ASR/TTS 流式函数绕过已选择的后端而直接调用 Volc；
voice channel 仅有 PTT 录音控制，录音停止后提前回到 IDLE；播放器关闭直接 stop，
没有将排空完成/失败返回语音生命周期；启动目录硬编码与配置目录不一致。
这些不是官方已提供的产品能力。必要修改作为团队维护、无板号及旧框架依赖的
独立补丁集成，保留官方原始仓库；不能将整套旧状态机迁入补丁或适配器。

当前实施与证据边界（仍为 `IN_PROGRESS`）：

- 远端比赛分支已核对为 `31faed70f683a6f5e690437c5507891360f0814a`，与本地
  `41723c61` 的源码树同为 `39c1309387084fdf079ed7d9b10c3b83cca119ec`。
  只读取得单分支作比较，未升级工作区、未 sync；本地 dev/trunk 也未提供所缺接口。
- 完整官方应用目标已增量构建、链接；不再 include 请求级 Agent provider，正式配置
  关闭旧 voice service/wake runtime 和内置 Volc。五份本地补丁及必要性见
  [SOURCE_PROVENANCE](../../../SOURCE_PROVENANCE.md)。官方 Agent/Media 检出无受跟踪改动；
  构建确实应用补丁，这两项分别成立。
- 启动代码已接入 `mimo` / `openai-audio` 外置后端注册，仅持有服务配置、TLS 和请求资源。
  ASR 是完整 16kHz/PCM16/mono 输入、完整文本输出的批处理；TTS 接收完整文本，
  输出 24kHz/PCM16/mono 的音频块，不接受增量文本。普通与流式均走同一选择，
  不支持的模式明确失败；旧 16kHz 批处理入口在不兼容时不发请求，新格式入口随
  结果返回格式快照。PCM 的字节分片在协议适配内拼成完整采样，不重采样、不误标。
- 后端在请求期间拒绝切换；准备与单次请求分离，取消向实际 TLS 传递且由请求方
  释放资源。通道不再预连接已知不支持的 ASR，也不重试残余 PCM；Media 排空
  使用真实完成/失败事件。上述是实现和构建证据，尚无新主链取消/超时/切换实板证据。
- **本地接入边界已准备，具体引擎与模型未验证。** TTS backend/model/voice/location
  复用现有配置；通用接口允许设备端、离线执行，描述联网、流式输出、非增量文本、
  取消、PCM 和未知资源预算。切换不要求双模型驻留，加载失败不自动转云端；
  无空后端、固定 PCM、新训练或模型管理平台，KWS 资产不当作 TTS 包。
- `official-framework-build-8.log` 已通过（最终显式关闭旧 wake/示例可选服务的配置）。
  AP 诊断 BIN 为 1484716B，SHA256
  `5a1afe3f1788e5715f578cdb537bb798acfcd49ff2deaa32066f9e75c1aa00b2`，
  `.config` SHA256 `9e28f3e3cc4c8a690ba2a55752117c53af6600af70a5bca0575c7e433a3d4b3a`；
  AP role `bk7258-role-b5ceab089e6f41f3`，见既有
  [构建 manifest](../../../../out/bk7258-plan-validation/out/bk7258/aidk_ai_toy/app__openvela_ap/bk7258-34dfff5891ff2b05/releases/mcuboot/build-manifest.json)。没有签发新 OTA
  或将该诊断构建作为可交付运行固件。符号确认 `ai_agent_main`、
  `agent_loop_start`、`session_append`、官方 TTS/Media 排空进入 ELF；旧
  `bkcloud_runtime_create`、`bkvoice_runtime_command`、`agent_turn_run` 和 Volc
  流式实现未进入 ELF。**关闭示例 CLI 后 `voice_channel_start`/ASR 调用因尚无真实
  Trigger 消费入口而被回收，不能算架构 A 或语音闭环通过。** 旧源码清理也未完成；
  `drivercheck_ap` 仍是旧 voice/TLS 的实际独立消费者。
- 既有 cloud-request 回归 1 项通过，既有 TLS 回归 6 项通过（沙箱外仅本机回环
  套接字）。cloud-http 旧测试仍使用已退出的 JPEG/旧 chat 签名，未通过；提取时
  遗漏的旧 client 请求容量常量已修正，未修改旧断言冒充通过。未新增测试程序。三个已派生的 Volc C 文件亦用本次 ARM 编译参数完成语法编译；
  不代表 Volc 服务、取消或现场模式已测试。
- App 本轮回读仍为 `0.5.15-shaniu-model` / code 20；没有新 APK 安装。关闭 MobaXterm
  后只读重试 COM8 成功（115200/8N1、8 秒、无 DTR/RTS）：`bkvoice status` 返回
  `provider=ap ready=1 configured=1 connected=0 gateway_ready=0 tls_available=1`、
  `PTT ready=1 link=1`、`CLOUD ready=1 busy=0`，采集 1443 字节，SHA256
  `24c4e588df7c409a6d8dde2075430830c2acf9548e715815beb6bd83853e41f7`；同时看到
  `BKVOICE trigger input`、KWS 窗口/分数及 Media 音量策略日志。证据
  `out/shaniu-p0-20260913/official-framework-baseline/serial-retry-20260915.{raw,json}`。
  这是当前板运行状态和触发输入的只读证据，不是官方 Agent 端到端语音闭环或真人现场通过；
  未复位、烧录或 OTA。当前活动模型 SHA 与 App 选择仍未完成新的设备一致性回读，不能用界面
  默认词代替实际状态。
- 仍须完成：Trigger→官方通道的真实入口及自动端点、Agent 在途关联/取消与重监听、
  SCB3 配置到官方后端激活、官方 LLM 传输及受保护会话存储、相机/模型切换/按键/
  App/OTA 交互迁移，随后清理无消费者的旧框架并集中实板验收。现有 SpeexDSP
  1.2.1 的 VAD 源码自称临时实现，官方构建仅启用 resampler；不能据源码存在
  就认定有可直接部署的成熟端点方案。真人现场唤醒、泛化和新路径首响均未验证。


集中 Skill 沉淀已完成，能力分别归档，见[能力映射](shaniu-skill-capability-map.md)。原有 Android 连接、切页及音量 P0 保留。本轮按更新授权继续三词切换、云端模型配置、私人录音适配和板载按键/马达，不新增测试代码。

当前已核验实板：`18.6.363+525 / counter525 / pair=confirmed`；已覆盖安装 APK `0.5.15-shaniu-model`（code 20），SHA256 `25d260b9ccf0bf66a688327ff2ab5a2c840880b3f372a8907b608773503133c8`。525 完整镜像 `voice525-camera-owner-full/` SHA256 `e8990588a07ad680709be923d8902180244eb7774615609554e7337b555f8465`，同设备身份、信任、校准、配置与原模型保留。构建输入快照 `voice525-camera-owner-source/` 含 651 个输入及实际 AP/CP ELF，源码树 SHA256 `661a39109f9b66501442b327296f009525891a17652033d5f26c4c586a53bd5d` 与构建 manifest 一致。证据均在工作区 `out/shaniu-p0-20260913/`；设备绑定完整镜像和私人素材不公开。

523→525 延迟切片已进入真实产品：官方 Media 队列从 66 帧改为 30 帧，仍由 Media 处理缓冲、欠载恢复和排空；语音取图接口退出独立表情序列，语音会话独占 thinking/speaking/error，独立拍照命令保留自身反馈。523/524/525 留出合成手机扬声器回放均完成真实唤醒、当次 JPEG、理解、TTS 和重新监听；取图 4429/4475/1305ms，播放器打开到 DAC 启动 8907/4175/4153ms，唤醒到 DAC 27222/23931/19858ms。每轮云回答与网络时序不同，这些是实际单轮观测，不是受控性能基准。525 JPEG 7935B、TTS 445440B/ret0/done1，排空后 MIC 约 454ms 重开，随后约 84 秒持续 KWS；本轮无 XRUN/underflow，重开时的短暂 -16 日志仍保留，不称为全部音频问题已消除。证据 `voice523-boot-image-stream/`、`voice524-boot-image-stream/`、`voice525-camera-owner-image/`。

522/0.5.10 的真实升级后回归已完成：20 轮/60 次底部切页，已确认可观测的原生 GATT/扫描日志窗口无新增连接或扫描事件；音量回读 40%、60%，最终恢复 73%。0.5.11 修复实测新增导入问题：系统文件选择器超过前台宽限后，所选模型曾在认证未完成时被误判为设备忙而丢弃；现仅为尚未发送的本次选择保留 30 秒有界等待，认证、状态和能力回读后单次提交，主动断开/离开前台/取消会丢弃等待项，已发送的事务不重放。0.5.11 真机在文件选择器停留约 403 秒后，仅选择一次新 WKM，完成自动认证、下发、激活和期望 SHA 回读。0.5.12 随 APK 提供该候选，并显示当前/上一模型短哈希；实际往返恢复 `2ced5671→d3256841→2ced5671` 均回读确认。正常软件重启后 525 confirmed、Wi-Fi/云服务/KWS 自动恢复，App 自动重连并读回新模型。20 轮/60 次切页持续约 150 秒，本 App PID 的 GATT connect/register、扫描及关闭事件均为 0；其他进程的周期扫描分列，未混作 App 回归。证据 `app0511-long-picker-import.json`、`app0511-525-external-model-activation/readback.json`、`app0512-model-restore-readbacks.json`、`voice525-new-model-reboot/`、`app0512-tabs20.json`。 0.5.12 音量实际回读 40%、60%，最终恢复原请求值 70%（官方 Media 档位回读 73%）；证据 `app0512-volume-readbacks.json`，不将 UI 自动化误落到其他页面的早期观察记作设备设置失败。

当前冰冰模型为 fresh-end1200 候选：TFLite SHA256 `2ced56715079b8dcbed076ceef95d6340098949c7e52aea9001810baced15040`，23640B；WKM SHA256 `20345f85b58ffccdf0e3d09cd3f0e3a3549c49c92bc2bcd665e7ef8c2e4345ba`，23776B。仅调整完整正例在训练窗口的位置，验证/测试数据及 0.85/0.2、连续 2 次、1000ms 政策不变。验证分类 9/10、普通语音 0/36 误判，冻结测试 12/12、0/42；严格连续窗口漏检/窗外事件从旧模型验证 7/7、测试 9/9 降至 1/1、3/3，不能将窗外事件统称普通背景 FAR。原录音 33/33 切片命中，但密集连续诊断仍受释放锁存影响，属于训练来源诊断；真人泛化未验证。525 上新模型实板完成一次留出合成声学唤醒、当次 JPEG 7267B/1244ms、TTS 775680B、排空后约 357ms 恢复 MIC，随后约 54 秒 KWS。证据 `voice525-fresh1200-bingbing-image-r1/result.json`；本轮无 XRUN/underflow 或重开 -16。旧冰冰 WKM 保留在本地；本轮结束板内上一模型为新傻妞，见下方恢复回读。

0.5.15 更新傻妞词为已训练的 fresh-end1200 候选，未改标签、阈值或严格验收窗。验证分类 10/10、普通语音 0/36，严格窗漏检/窗外事件 8/8；10 段各触发一次，词尾到事件中位数 122ms、最大 248ms，上一模型为 175.5/772ms。依据验证冻结后首次执行该候选的独立合成测试：分类 12/12、普通语音 0/42，12 段各一次事件，严格窗漏检/窗外 7/7；词尾延迟中位数 69ms、最大 216ms，最小 -128ms 表示在原词尾标注之前，不延展验收窗或改写原指标。TFLite `2ade86dd6e203deb8bdc34ce8aaa4c2417e153fe8fe978c84360fbeca6469368`；WKM `d363c8253f18f3303e24f34e20accb415df04869b7f6c7cec4249ac7e4228045`。真实 App 单次下发至激活回读约 152 秒，525 加载 23640B、arena 40692B、首次监听推理约 81ms。首次留出合成声学唤醒取得新 JPEG 20419B/1215ms，但后续图片理解 HTTP200 仅收 15 字节并超时 -110，约 92ms 恢复 MIC；串口采集间有间隙，保留原记录，不称完整看图通过。证据 `app0515-shaniu-model-activation/`、`shaniu-existing-trigger-delay-analysis.json`。超时后下一轮普通对话完成：ASR/对话/TTS HTTP200，483840 PCM 字节/ret0/done1；唤醒到 DAC 约 16.31 秒，排空后约 229ms 重开 MIC，随后约 136 秒持续 KWS，本轮无 XRUN/underflow。证据 `voice525-fresh-shaniu-ordinary/result.json`；正常重启后 525 confirmed、DHCP/云/KWS 恢复，App 自动认证并回读新傻妞模型 `2ade86dd`。随后单次恢复进入本轮时的冰冰 `2ced5671`，新傻妞模型留作上一模型，App 两项回读一致；证据 `voice525-fresh-shaniu-reboot/`，真人泛化仍未验证。

交付对应关系：525 固件源码快照与 ELF 保持封存，0.5.15 源码/三模型包快照为 `app0515-source/`，APK 内模型字节已逐一核对；外置新模型由 App 部署，不能声称原 525 完整镜像已包含该次配置。再次全量烧录前必须保全最新模型选择与配置；pre521 基础恢复材料仍是旧选择，禁止直接覆盖当前选择。

0.5.14 模型 ID 字段使用可见、无组合的 ASCII 单行输入。实测当前讯飞输入法仍会在 0.5.13 的 URI 字段中改写/重排连字符，已核对新安装版本与实际 EditorInfo，未把该候选写成修复通过；0.5.14 精确输入和设备配置回读通过。自动化保存必须在键盘收起、对话框位置稳定后取得新鲜控件范围，不能用旧坐标误判保存失败。525 实际声学唤醒后已观察 ASR HTTP200、对话 HTTP400/ret=-121、turn_fail=-121 与 KWS 重新运行；App 已恢复原 ASR/对话/TTS ID 并回读确认；服务错误后约 211ms 重开 MIC、3.262 秒取得首个 KWS 窗口。下一轮再次唤醒、ASR 与工具路由 HTTP200、新 JPEG 7975B/1226ms，但未在采集内完成播报；之后只读状态确认 `last_error=-110 / turn=0 / busy=0` 与持续 KWS。超时转换发生在两段采集之间，未补造具体时刻或 HTTP 阶段。证据 `voice525-service-error-recovery/`、`voice525-after-service-error/`、`voice525-after-error-state/`，超时后的下一轮普通语音已完成：ASR/对话/TTS HTTP200，322560 PCM 字节/ret0/done1，播放排空后约 691ms 恢复 MIC，随后约 115 秒持续 KWS，未记录 XRUN/underflow；证据 `voice525-after-timeout-ordinary/result.json`。该次唤醒到 DAC 约 15.95 秒，为单轮观测。

525/0.5.14 已完成活动请求 Wi-Fi 中断恢复：原手机热点仅临时开启，原 SSID/密码/安全模式/频段未改；板卡通过既有 RAM 会话连接，未覆盖持久配网。真实唤醒及 ASR HTTP200 后关闭热点，记录实际断链、后续请求 ret=-129、turn_fail=-129，约 116ms 重开 MIC、3.294 秒出现 KWS 窗口。热点恢复后无需板端重连或有副作用的 STATUS 命令，即自行关联并完成 DHCP；下一轮 ASR/对话/TTS 三次 HTTP200，491520 PCM 字节/ret0/done1，排空后约 85ms 重开 MIC。该轮有一次 Media 欠载暂停/恢复，对应三条 XRUN/underflow 日志，不能写成无欠载通过。最初自动化错误匹配唤醒日志的轮次未执行断网，已分列且未计为中断通过。证据 `voice525-active-wifi-recovery/result.json` 与两段完整 UART 原始采集；手机与原网络恢复另见 `voice525-original-network-restored/`。

WKM1 已通过真实 App 完成一条下发/存储/激活/回读链：517 上选择“你好，open-vela”，App 按期望 SHA 确认 `0d05f073df667d7b595a0a0478f4a527b04bbe710c22016205ae24611c860b5b` 生效，随后恢复材料保留同一选择。514/515 的 `-36` 原因已整改：当前 VFS 连 32 字符中间目录也会在斜线前拒绝，改为 24/24/16 保留全部哈希位；模型与配置共用既有存储持久化规则，删除不适用的 NuttX 目录 fsync。519 的分片 ACK/取消不再为每 32B 读取完整产品 STATUS；单请求串行、认证、序号、APPLY 校验和最终 READ 确认保持。0.5.9 增加发送百分比。519 已完成“你好冰冰”真实 App 激活与期望 SHA 回读，arena 40692B；520 全量烧录后 App 回读仍为冰冰，云配置 ready=1/busy=0，原身份和设置保留。520/0.5.10 使用认证后的 CAP1 只读能力协商，将 CONFIG_APPEND 上限提高到 512B，旧固件回退 32B；AUTH、OTA 和底层 GATT 写入期限不变。520 上第三词约 132 秒完成实际传输和激活，App 按期望 SHA 确认；同样 23776B 旧分片操作约 7 分钟，不是仅按理论分片数估算。恢复上一模型已回读确认为冰冰，普通软件重启后 520 pair confirmed、cloud ready 和 KWS 运行通过；App 重启后已回读“当前：你好冰冰”。既有 C 控制会话/TLS 测试通过；Android 单元测试因既有 Peer 未实现之前新增的 requestPayload 接口而编译失败，未新增或修改测试。MCP1 经原配置所有者保存公开 ASR/对话/TTS ID；520/0.5.10 上对话模型已从 mimo-v2.5 改为 mimo-v2.5-pro，并由设备回读确认，真实冰冰词合成物理回放已完成 ASR/新对话模型/TTS 三次 HTTP200、391680 PCM 字节、排空及约 54 秒持续监听；随后 App 恢复原对话模型，串口配置摘要与原记录一致。ASR/TTS 字段本轮保持原值，不能称三个服务分别切换验收通过。

519 的留出合成手机扬声器回放已通过真实 MIC 唤醒、用户收音、云请求、TTS 537600 PCM 字节/ret0/done1、playback_complete1 和持续重新监听。新模型 arena_used 为 40692B，恢复后的首次推理 81562µs。该轮 GC2145 有新打开/关闭记录，但没有独立的帧返回成功记录，不将图片理解完整验收判为通过；仍有 Media 停启阶段 `-32/-22` 与格式日志，完整原始证据保留于 `voice519-adapt-acoustic-openvela/`。这是合成语音物理回放，不是真人泛化或主观扬声器/马达体感验收。

520 傻妞词的有界留出合成手机回放：一次 hotword → waiting-user → user-speech → 新 GC2145 打开/关闭 → TTS 606720 PCM 字节/ret0/done1 → playback_complete1，MIC 于排空后约 358ms 重开；其后约 44 秒连续监听无额外触发。实际 DAC START 距本次 hotword 约 28.9 秒，首响延迟仍需优化。前一轮音乐播放器仍处于播放状态并伴随额外触发，排除其作为自唤醒判定依据；本轮在唤醒时停止手机目标词播放，检测到命令语音后 2.2 秒停止手机播放器。没有真人泛化、板上物理按键或主观振感证明。证据 `voice520-shaniu-acoustic-bounded/`。

板载交互保持功能键 KEY1/P13 音量减、KEY3/P8 音量加、KEY2/P12 独占长按 3 秒后松开提交软关机，P12 为板级超深睡唤醒输入。原理图另有位号 K1 的 CEN 复位键，不能与功能键 KEY1 混用。515 起按实际 Media 音量档位计算，避免一次按键仍落在原档位。唤醒与关机通过标准 force-feedback 设备，在 MIC 释放后等待有界脉冲完成；保留录音抑制马达。517 首次物理唤醒后 AP/CP 通信停滞，源码确认 GPIO FF 私有自旋锁与 NuttX watchdog 全局临界区顺序反转；519 改用同一 NuttX SMP 临界区，实际同音频复验已越过该停点并完成语音恢复。旧 GPIO FF 主机替身未提供此临界区 API，无法编译，未改测试或计为通过；目标构建/签名及实板证据分列。真实按键、电源唤醒与震动体感仍未验收。

0.5.8 已在真机完成 20 轮、60 次底部切页，GATT connect/register 无新增；已认证的滚动位置跨轮询保持。0.5.9 仅在其上增加模型发送百分比，已覆盖安装；回归证据不冒充 0.5.9 的新实测。三个候选均 23640B、INT8 `[1,149,40,1]`、三标签六算子，WKM 包 23776B。留出合成分类目标命中 10/10、12/12、12/12，普通语音各 0/42 目标误判；原严格连续测试漏检/窗口外事件为 0/0、9/9、11/11，重复 0，后两词迟触发仍需改进，未扩大窗口或降低阈值。原录音训练来源诊断候选匹配 1/5、2/33、5/10，未归属事件 12、3、10 不直接称误报；原文件已经用于训练，该结果不是独立验收。当前工具核对未发现批量/流式前处理窗口错配，不据此臆造修复。

真实 App OTA：511→513 在 AP 偏移 258048/地址 `0x0043b9e4` 校验失败；514→515 在 36%、AP 偏移 1290240/地址 `0x00537bb2` 再次失败（均 `-5`）。位置不同，不能推断固定坏扇区。521 已接入原 AP 构建未链接的 AIDK SDK Flash 通知接收入口；SDK 通知 ACK 不代表远端 XIP 暂停，也不启动另一套 AP Flash 客户端。目标链接、既有存储/OTA 合同及实板启动通过。原校验失败分支增加一次有界读回与位差数量日志，仍保留失败，不重写或掩盖错误。522 OTA 包 SHA256 `c24b890f626365f1b5c5096db1a3a54b2c2b0904ad2c84ea04ed577350898de0`，编译 floor 为 521、签名版本/计数为 522，符合现有 OTA 契约。该包已由真实 App 单次发起，完成 AP/CP 写入、自动重启、`BOTA TRIAL CONFIRMED slot=1 counter=522`；手机重新认证后实际回读构建 522/计数 522、100%、已确认完成升级、无错误，KWS 已恢复。证据 `app-ota-522-apply-uart/`、`app-ota-522-apply-uart-continuation/` 与 `app-ota-522-confirmed.json`。这是一轮本版本真实 OTA 成功，不据此断言历史间歇写入错误的所有原因均已消除。全量烧录与 App OTA 验收分列。

剩余必做：三词迟触发和效果继续改进，独立真人泛化仍未验证；ASR/TTS 各自更换另一可用服务模型的实测尚未完成（对话模型切换已回读并真实运行）。当前授权服务的真实 `/v1/models` HTTP200 目录仅提供一个 ASR；其他 TTS 项为音色设计/克隆，需要不同输入，不能直接当作预置音色模型互换，见[官方说明](https://github.com/XiaomiMiMo/MiMo-Skills/blob/main/skills/mimo-v2-5-tts/SKILL.md)。新增词训练、WKM 打包、App 导入与设备激活/回读已以本轮冰冰及傻妞完成对应切片；任意词效果不作普遍保证，不另增第四词作为交付前置。KEY1/KEY2/KEY3 实体按压、关机后实体唤醒与马达触感没有自动物理执行条件，仍未验收。低延迟仍受云端首包和网络供数影响；保留本轮热点链路的实际欠载事件，不宣称连续播放质量已完全收口。已完成的切页/音量、WKM 导入激活/回读/恢复、服务错误和断网恢复不重新列为待做。历史间歇 Flash 校验问题仅在真实复现时继续定位，不无变化地重复升级。项目仍为 `IN_PROGRESS`。

以下为按版本保留的历史记录，不代表当前运行版本或当前范围。

506 历史实板为 `18.6.355+506 / counter506`，原AIDK COM8全量部署与pair confirmed通过；模型及构建配置均为纠正发音后的v46，SHA256 `922eba9175fcda60f7c8a4505ca4eb5a97c86ceb30fbe48c685fd612098ac910`。505因遗漏模型配置SHA pin返回wake unavailable=-129，不能作为可用唤醒版本；506已修正pin，并在既有ROMFS构建中加入模型与配置哈希一致性检查。原签名、六个同设备保留分区及日志恢复回读通过，未清手机数据。源码包 `voice506-diag-team-sources.tar.gz` SHA256 `dc93e86b0f3e1af81098e86f9859b47b1539349deb906ef78ee88877f3db0f2e`，完整BIN SHA256 `4702e4460a408ea7739f4e90ce90273a68dfe5181a4cac745e3ac55ba783d59c`；APK仍0.5.4。

506已用原Mi10扬声器播放实际音素为 /ˈoʊpən vˈiːlə/ 的留出合成音频，经真实MIC产生本地wake→waiting-user→user-speech→ASR/Agent→当次GC2145取图→图片理解HTTP200→TTS 1098240 PCM字节/ret0/done1→playback_complete1→持续KWS。证据 `voice506-physical-wake-image-synced-r1/`；模型23640B、实际arena_used40692B、首次推理78125µs。这是合成语音的物理声学链路，非真人泛化验证。播报仍有3次underflow；显示状态更新EYES=-11，随后现有status确认display ERROR/-110/sequence0，为启动等待设备超时，具体缺失节点尚未确认。当前整体仍未完成。

v46由全部62条正例纠音后重训，保留40/10/12划分与原负例；24轮CPU训练6分11秒，最佳epoch5/val_loss0.099193。现有连续评估验证10/10、原测试12/12、扩展纠音回归14/14，窗口外/重复触发均0；扩展14包含原12，非独立26条。未降低0.85连续两次/300ms策略，评估复用已观察的合成音色，不能称新盲测或真人效果。模型与完整纠音资产归档 `kws-v46-viila-assets.tar.gz`（25451864B，SHA256 `17de5340831d4182f3bc92323daabce4f82dc2e63af304b9875eb3f82e584c43`），训练和实际token解码记录随包保留。

506 阶段曾将手机 App 换词列为后期扩展；此范围限制已被后续三词及自定义模型授权取代。503→504实际App OTA及其后60次底部切页/原凭据连接超过120秒的证据继续有效；当前506→507正通过同一真实App单次执行，未提前记为通过。以下保留按版本区分的历史发现，不替代上述当前事实。

501→502真实App OTA仅启动一次，在0%返回原始connect -101，供包端无TCP、未写Flash；504标准OTA包于06:25:17 UTC从真实App只启动一次；真实TCP/TLS供包后，实板TRIAL ARM slot1/counter504并重启，手机原凭据自动恢复，回读构建504/counter504及“已确认完成升级、100%、无错误”。证据 `phone504-app-ota-run-r1/`，包SHA256 `e6e400d5ed9145c368f8070c13e82bcdccdc8fe04f0d90a1a9b2d485982a7843`。这是本次OTA成功证据，不足以宣布所有历史-101或Flash回读间歇故障根因已消除。503两次物理合成回放均没有正式唤醒事件，未播放后续命令，不能声称新Media播放路径已验证。**本轮核对发现实际回放sid043/sid045的文件哈希与final-v37-fresh生成记录一致，而记录为vela /ˈvɛlə/，不符合用户指定 /ˈviːlə/“维拉”。此前将这批合成回放当作标准发音验收输入的判断撤回；历史触发和下游链路日志保留，但不作为正确唤醒词发音的验收通过。** 已停止沿用这两份音频作正式验收。进一步核对v31冻结manifest的runtime_warning确认：62条训练/验证/测试正例也沿用实际 /vɛlə/ 路径，条目中的 /viːlə/ 只是词典参考，与实际不符。v45不能作为用户指定“维拉”唤醒的交付模型，已保持原40/10/12说话人划分重生成全部62条正例，406条总资产及22条连续流由现有audit校验通过，v46沿v45的24轮/32通道/频率步长4/幅度与房间增强配方训练完成，结果见当前记录，未改触发阈值。新的 `pronunciation-viila-r2` 两条合成通过实际token IDs反解确认 /ˈoʊpən vˈiːlə/，已以新文件名复制原Mi10并验hash；sid043单次手机扬声器物理回放已执行，旧v45峰值0.988但只命中一次、没有正式事件，不能写为正确发音唤醒通过（`voice504-corrected-pronunciation-playback-r1/`）；实际合成辅助拼写为 `你好，open vila`，产品拼写仍 `你好，open-vela`。第一轮r1候选仍走错音素，已废弃，不作为正确音频。证据 `kws-tts-train/final-v37-fresh/manifest.json`、`voice503-physical-wake-command-synced-r{1,2}/`。

500同次启动真实结果：第一轮Mi10唤醒后未检测到用户语音，按5秒无语音路径退出并恢复KWS；第二轮最高分0.839未过现行0.85门限、没有正式事件。第三轮复用已有串口实时输出，以板端waiting-user为同步点播放“看看眼前”，真实MIC触发本地wake→user-speech→ASR/Agent→当次GC2145打开/取图关闭→图片理解HTTP200→TTS 506880 PCM字节/ret0/done1→playback_complete1→连续KWS。证据 `voice500-physical-wake-{command-r1,image-r1,image-synced-r2}/`。未用PTT、软件注入或缓存图；这是合成留出音频的物理回放，非真人泛化/听感通过。跨设备时钟未校准，撤回仅凭手机/板端时间差判断早收口的结论；wake预录无条件进ASR的问题由源码独立确认。当前仍有播放underflow、Media边界错误和EYES=-11，整体项目未完成。

498只针对当前缺口修改产品代码：OTA连接等待保留原始errno并记录失败阶段，不再将所有等待错误伪装为-101；KWS在已有日志中记录候选命中/释放转换，不改模型、0.85门限或连续两次规则。增量目标实际编译链接通过；已有HTTP/TLS主机回归退出0（仅主机同步套接字路径，不覆盖板端异步poll）。已有KWS主机测试在旧2秒/100ms假设`state.calls == 1`处失败，与现行3秒/300ms合同不符，未改断言、未写成通过、未新增测试。证据 `voice498-connect-candidate-build.log`、`voice498-existing-http-host-authorized.log`、`voice498-existing-kws-host.log`。

496→497真实手机App OTA本轮失败：手机listener READY，但无TCP/TLS接入；板端manager-apply返回-101、进度0，仍confirmed496，失败后自动重新加载Trigger并持续推理，supervisor faults0/recoveries0。不能把此轮写成升级成功，也未执行`bkwifi status`改变租约后掩盖原状态。证据 `phone497-app-ota-run/`、`voice496-after-ota497-status/`。499标准App OTA包由498同份源码/构建签发；手机重新选中原Shaniu BLE 7F:81并以原凭据认证后，实时回读498。05:17 UTC从真实App启动499，TCP/TLS接入成功；05:21在38%失败，AP image=1 offset=1331200的写后回读校验返回-5，仍confirmed498。后续status确认manager state7/phase3/error-5，supervisor faults0/recoveries0，Trigger自动恢复并连续推理超过5分钟；当前日志不能区分Flash读失败和内容不符，未重试同包。证据 `phone499-app-ota-run/{uart,uart-progress}/`、`during-3-ui.xml`。

498同次启动的两轮Mi10回放均触发正式本地唤醒，ASR/Agent/TTS HTTP200且回到持续KWS；但第二轮在“看看眼前”播放前已收口、没有相机调用，不能写成语音看图通过或指令正确收录。证据 `voice498-physical-wake-{command,image}-r1/`。代码确认冻结的一秒wake PCM会无条件送入ASR，live observer又在sink之后才判定端点；500已将过滤移到同一worker的sink之前，丢弃wake尾音，连续160ms静音后等待用户语音，保留原5秒无语音/30秒总期限与原取消join路径。已删除无人再用的32KB wake pre-roll及其读取接口；泛用历史协议prefill仍由原合法调用者使用。500已增量编译、签名校验并完成全量部署：`voice500-diag-full/`、`voice500-wake-segment-build.log`、`voice500-diag-team-sources.tar.gz`，源码包SHA256 `178703ce8cb22203d28486566968c78ec2a7572d69c2d6016954086aab0995a2`、完整BIN `774ce1fea425ac80444a4954a039c2f8ab6e6abe6ebb459da93815423693dbbc`。未改相机consent、模型或KWS阈值，未新增测试。

v45在496的两条Mi10留出物理回放峰值分别0.878、0.980，均未产生正式唤醒事件；原始留出源在同一C前处理/运行策略中有连续高分，尚不能归因于发音时长。498重启实测模型23640B、arena_used40692B（实际arena分配262144B）、Trigger栈10240B、首次推理79938µs；真人泛化未验证。498源码包 `voice498-diag-team-sources.tar.gz` SHA256 `f93587afb767acb1ed9c188adead4fb4da4b0be2cfa2dbeef2775d176d2880e8`，647项树 `59d1141462fbff3455653d9025594b66781841e3d5ce13074e85ef72c15d124c`；AP ELF `79f0584cbfaa9a7e7d04fc45e88217e7bcd549f7ae5cd6e7b2eb157652599835`，完整BIN `eb523fe02c7511f824923cba0affbbb9961322f72e197cc62bf165c06f5a9496`。APK仍0.5.4、Android P0成果保留；正式唤醒、当前同版本完整产品链和真实OTA仍未验收。

494失联及496交接记录（2026-09-14）：原板已由用户物理复位，COM8单条状态/ps/目录查询均恢复；实板确认 `18.6.355+494 / counter494`、manager idle/error0、supervisor faults0/recoveries0。用户确认复位前眼睛画面静止，只能说明LCD保留帧，不能证明AP活跃。494导出配置误用了AP本地 `/data/kws-in494`，实际CP持久存储在AP挂载于 `/cpdata`；原日志全程ENOENT，CP目录为空且短文件不存在，本轮没有PCM资产。此前“rename后停止保存”的推断撤回，不再尝试读回不存在的文件。该回放max_wake=0.933但没有正式事件。命令共201字节、sleep后155字节，小于当前256字节RX环的255字节容量，不能把NSH失联归为已确认的软件环溢出。495真实App OTA只到手机供包READY，没有TCP接入，板端Trigger/recorder/MIC停止和close均成功后AP日志停止，CP Wi-Fi日志继续；不能归为整板死机或Media stop死锁，失联根因仍未闭环。原生BK Loader read未建立握手、无备份，出现未核实do_reset_signal后该路径已停止。496关闭导出并包含v45，已沿原板既有可信备份全量部署；版本与身份保护沿现有契约。证据 `voice494-after-user-reset-{status,threads,pcm-list,pcm-stat}/`、`voice494-official-trigger-input/`、`phone495-app-ota-run/`。 496首次下载为safe_prewrite_failure，所有擦写成功标记缺失；CP uptime持续至约500秒，未发生软件重启。随后首条ps被握手残留字节污染并返回command-not-found，下一条ps正常，说明本次可不复位恢复NSH。当前实际编译的NuttX upper已有uart_is_termios_hw_change过滤，撤回“只改变回显就会重配硬件”的候选解释；尚不改UART驱动。证据 `voice496-hil-full/`、`voice496-loader-failure-{observe,console,console-recovered}/`。

模型v45-room-f4已通过现有训练和连续评估：24轮CPU训练，最佳第21轮/val_loss0.107240，23640B，SHA256 `7c9765f943ee76d62a9e48736ae1fe13dafdc390cb94520bc0d80aba429c93e7`；频率步长4使理论卷积MAC从4.85056M降为2.42528M，尚非实测延迟。输入/标签/官方前处理及300ms、0.85连续两次的产品政策保持。原验证10/10、原测试12/12、另一批来源留出6/6均命中，窗外/重复事件均0；后一组132秒含24秒MS-SNSD背景，不能外推环境FAR。切片背景被分为unknown，非三分类全对；真实说话人泛化仍未验证。模型已进入团队源码，496已构建、签名及包/信任校验通过，496/v45已全量部署且实板版本确认，效果需以当前物理回放证据判断。报告在 `kws-tts-train/candidate-v45-room-f4/`，原训练及评估元数据保持不可变。 496源码包 `voice496-diag-team-sources.tar.gz` SHA256 `4c22c53747bca27ef19acae8847dd5fc9fff63b5e52558552de189f8fe058e86`，647项树 `39609ce0c8b25c068d339eb281846315b9d1429ca82f2cbef385bc71e6ba8fa0`；AP ELF `096b4be0e8398d9e94b2a856ee6b727377e0a93d3d9c502b148966fa5871f30e`，完整BIN `d8b23e6dc965e1769e00f8e4bfbbae5411e9b04bc15714d4fbf1b9a07821e3c2`。产物在 `voice496-diag-full/` 及同前缀manifest/ELF；APK仍0.5.4、未改Android源码或新增测试代码。

前一个正常运行模型检查点为 `18.6.355+493 / counter493`，状态确认configured=1/cloud ready=1/busy=0。493使用v44-room候选，首次正式入口Mi10音色43留出回放的max_wake=0.476、未触发；同样参数下487/v38回放记录为0。这证明产生目标响应，不等于正式唤醒通过，未降低阈值。证据 `voice493-diag-status/serial.raw`、`voice493-physical-wake-command-r1/`。第二次音色45留出回放max_wake=0.871，但未满足连续两次0.85条件，仍未触发，证据 `voice493-physical-wake-command-r2/`。已构建/签名494，临时启用官方MEDIA_TRIGGER_DUMP进行限时、本地声学输入定位；不新增采集程序或测试代码，诊断后恢复关闭。

493源码包 `voice493-diag-team-sources.tar.gz` SHA256 `e9abd6fcf595888a52c6a2990eeed72e9abcd22a8a17d6b89d53e82f2733bc07`，647项树 `d60338e8acf186a82144ba45a9019db5d209cb5e469da083107c9dc735722408`；AP ELF `e493d29ac16268b2e1d7c339cdb0d8b27369673726e26ee117c06d76bb917331`，完整BIN `c195e0b2ee077b8713fb27b24e0f733d0761edf0f30caca8948af0b5e1999e61`，对应产物见 `voice493-diag-full/`及manifest/ELF目录。包与信任校验、保护尾区一致及BK Loader全量写入通过；未重建身份或清App数据。

已完成的音频迁移实板检查点为 `18.6.355+492 / counter492`，全量烧录、原身份/配置保留及启动确认通过。492通过现有FFmpeg补丁机制在已释放音频流重新reserve前关闭并重开标准设备fd，使NuttX upper-half与lower-half重新执行同一轮configure；未绕过busy/权限检查。491的官方Media调度修复同时保留：本轮暂缓失败节点，让其他节点与STOP/unlink继续排空，错误仍上报。官方Media/FFmpeg工作树没有仅本机的tracked修改。

492同次启动已完成两轮普通语音诊断：实板MIC→ASR→官方Agent→TTS→DAC停止→MIC/KWS恢复，第二轮未复现491的EACCES。两轮PCM分别238080B、222720B，均ret=0/stopped=1/done=1/parser=0；第二轮DAC start=0，02:27:36.916完成后MIC 02:27:36.986启动、02:27:40.137恢复推理。仍有播放欠载与Media收尾错误日志，不能把完整接收/恢复写成听感通过；诊断采集不等于正式唤醒。证据 `voice492-diag-ordinary-r{1,2}/uart/serial.raw`。同次启动第三轮语音看图也完成：当次GC2145 open/close 02:29:32.51–32.60，Agent后续理解HTTP200，TTS正文1440411B/PCM1059840B、ret=0/stopped=1/done=1/parser=0，DAC再次START=0，MIC 02:30:07.116启动、KWS 02:30:10.211恢复。证据 `voice492-diag-image-r1/uart/serial.raw`；不将诊断输入或网络成功冒充正式唤醒及听感验收。

490启用官方TCP SACK与2144B乱序缓存后，首轮TTS由489的接收超时转为完整接收，但随后因Media失败节点饿死其他节点而未恢复。491修复调度后首轮恢复通过，第二轮语音看图已当次GC2145取图、Agent理解HTTP200，但DAC enqueue=-EACCES造成局部背压超时；错误后KWS恢复。492针对该实际第二轮设备生命周期整改，未新增第二个音频所有者或测试代码。历史证据 `voice490-diag-ordinary-r1/`、`voice491-diag-{ordinary,image}-r1/`。

492交付输入为 `voice492-diag-team-sources.tar.gz`（647项，SHA256 `4572ca27f185f6a7cd7262bbda67c0f59156542cd210af8e7045bfa3dc2101e2`）、对应build-manifest/ELF及 `voice492-diag-full/`。源码树 `d1d14b8234b6475359c371a6541e36e9e17d383b54f221b1b47eef03cd2e0c32`，AP ELF `68b3ff65f857c2b5bf2dcb4ce35ecd292f3f51760aabab199146e3543a023179`，完整BIN `062c752ce8b809d8398576a2c31aa627ff6bcb79e5b58b5f264f6428cd08a0f4`。492仍使用v38模型；493已接入v44并完成部署，具体身份与正式唤醒结果见顶部，不将两者混为同一版本。

自主训练v44房间声学增强候选已完成24轮CPU训练（TensorFlow2.15.1，当前无可用GPU），模型 `kws-tts-train/candidate-v44-room/model_int8.tflite` 23640B，SHA256 `2f87fbc82725b2058621e5e44e90ad19253632fef27e4ef44ad212aa40e6a72a`。训练仅增加原训练集的合成反射、带宽、噪声和幅度变化，未改标签、说话人或验证/测试来源。输入149×40 INT8、六算子与现有运行时兼容；固定产品触发参数下，原验证连续9/10命中且1次近音误触发，原测试10/12命中且1次窗外事件，独立来源留出5/6命中且1次窗外事件，均无重复触发。分别使用对应manifest生成验证绑定后执行测试，未跨manifest伪造绑定。切片结果不替代连续触发；当前尚未替换板上v38，真人泛化与新候选实板效果未验证。

已完成的App OTA回归（2026-09-14，历史版本）：原 AIDK/COM8 已由真实手机App OTA升级至 `18.6.355+489 / counter489`；串口确认TRIAL CONFIRMED，App重新认证并回读新版本。487采用板级初始MIC数字增益+12dB，chip只提供有界增益机制，模拟增益/校准不变；ELF实例字节与寄存器转换已核对。手机留出回放期间帧均幅峰值从484约211升至487约864，但max_wake仍为0，正式唤醒未通过。连续480/320 PCM窗与149×40特征顺序同主机，尚无实际声学PCM对照；不能把音量提高写成模型问题修复。证据 `voice487-diag-status/serial.raw`、`voice487-physical-wake-command-r1/`。

486即时TCP确认没有消除TTS停顿：117次读取、38次超过200ms、最长5229ms、接收累计76169ms，最终PCM345600B完成；487主机curl对同一既有赛事端点请求公开TTS文本，HTTP200、157206B、首字节0.602秒、总计1.191秒。主机回复与板端长度不同，不能据此量化改善；仍需定位板端停顿。证据 `voice486-diag-ordinary-r1/uart/serial.raw`、`voice487-host-tts-timing.json`。用户已补充授权全部项目请求，先前出站审批问题已解除。

487诊断语音“看看眼前”ASR200，但Agent400，未进入取图；错误后MIC/KWS恢复。根因已由当前官方源码确认：产品传入OpenAI封装工具，而Agent要求name/description/input_schema，二次封装后function.name缺失。488已将产品定义改为官方工具输入合同，保留唯一Agent循环、当次取图和产品期限；增量构建、包/信任校验及全量部署通过，实板确认18.6.355+488/counter488。现有provider回归1项通过；构建清单项仍期待旧两文件列表、因新增已生效agent_turn.c而失败，未修改测试或记为通过。488源码646项树SHA256 `2c2943e8395760bda8f6c9335a883413330f36441ab617f1e572a497970efb31`，源码包 `voice488-diag-team-sources.tar.gz` SHA256 `fd01a1ee0b15f01bf1b2169dd74dec30b2d91c77ce6b32f71ea88b70fbfde7a7`，AP ELF `8f7becf159b60b62f69a4f07c98c64195e3342a8e4770988aa4f8dc0347d1afe`，8MiB BIN `783496b1898993b8d56b9bb7a1f53a9fbf205bbb92ff040834a6265be9e426b5`；产物见 `voice488-diag-full/`、manifest及ELF目录。APK仍0.5.4，未重做身份供应或用户配置。正式唤醒、流畅TTS、真实看图和新版本App OTA仍未验收。

488修复后的真实图像诊断已执行：Mi10播放合法“看看眼前”→实板MIC/ASR200→Agent工具请求200→GC2145在当次请求后open/close→携带新JPEG的Agent后续请求200（1025B）。TTS仍HTTP200/ret=-110，正文219548B、PCM161280B，receive70346ms/consume1406ms、57次读/18次慢读/最长20118ms，未收齐结束事件；随后MIC/KWS恢复。证据 `voice488-diag-image-r1/uart/serial.raw`，摄像头当次open/close为01:02:55–56，后续理解返回01:03:04。未记录或导出当次图像；未将诊断采集当正式唤醒，也未把不完整播报写成闭环通过。精确摄像头图像外发授权已取得。

489候选在原有HTTP摘要中增加系统TCP计数差值（所有TCP流量范围、非单连接独占），用于定位板端停收；使用NuttX既有统计，不新增探针程序或测试代码。构建/签名/包/信任校验通过：源码646项树SHA256 `3c7061a43cb6bef7e6369cccf1184c3b0917802b78de6903c5624f5dd5cf5dc3`，源码包SHA256 `bed5357e8ca6577df5d5e17e3fdf62f52b94c19d53e564b7feabf53b08d3ad42`，AP ELF `64008a9163f5b6fcdc0af306a840e9b31add8cf2b9fe37f6fe8fa86b834f7de8`，完整BIN `fe7c240b11ba0db6e56978b61a7436e3f50c3f2f1fc67d45e850aa113e4e1a9a`，bkpack `dd24bbec2097ff85f601911fa04688b5f75b35983a914ad7167db8504c67fd56`。上述为完整烧录包；App正确拒绝其作为OTA输入，未改验签逻辑。随后经现有release ota入口生成标准五成员CP/AP包 `voice489-app-ota/package/`，SHA256 `451fbcbdb2651aa20649cc2bf334a811110ac70cde6f7b60c78ff9ffcaa9a768`，大小2859880B。手机系统文件选择器选包并校验通过，01:24:44单击一次开始，手机日志确认TCP与TLS接入，设备下载后重启，串口BOTA TRIAL CONFIRMED slot=1 counter=489，App回读489/100%/已确认完成/无错误。证据 `phone489-app-ota-run/`、`phone489-app-ota-follow/serial.raw`；初次读回和23%进度保留，当前成功结果见progress3-ui.xml。文件选择器长时间后台后，控制会话自动恢复并重新读取OTA状态，未手动扫描或重新认领。488手机设置页仍回读音量53%且入口enabled/clickable，固件更新页真实读取488，未清空数据或重认领。

485历史运行结果：原 AIDK/COM8 已确认运行 `18.6.355+485 / counter485`，本轮继续全量烧录，保留既有VOICE_HIL_TEST诊断配置。485采用官方播放器六帧预缓冲；首轮手机普通指令回放加诊断采集的ASR/Agent/TTS均HTTP200、TTS ret=0，PCM345600B、stopped=1/done=1/parser=0，receive8346ms/consume2199ms。官方SpeakerRate执行24k→16k，DAC configure/start均0、间隔约167ms，DAC complete后重新配置MIC，KWS恢复。仍有4次DAC欠载及图收尾错误日志，不能宣称听感通过；484较短回复有5次欠载，输入回复长度不同，不能仅以次数宣称量化改善。首轮后手机仍已连接，音量53%入口enabled/clickable=true。同次启动第二轮再次TTS接收超时：HTTP200/ret=-110，156962B正文、PCM115200B，receive81729ms/consume1047ms，stopped=0/done=0/parser=0；重采样与DAC启动正常，错误后MIC及KWS重新运行。第二轮超时后手机仍“已连接傻妞”，单独提示对话未完成，音量53%入口继续可用（`voice485-phone-after-error-{home,settings}-ui.xml`）。485不是稳定闭环，下一项定位接收停顿，不继续仅调缓冲。证据 `voice485-diag-status/serial.raw`、`voice485-diag-ordinary-r{1,2}/uart/serial.raw`、`voice485-phone-after-voice-{home,settings}-ui.xml`。

本次迁移的实际合同：云端24k PCM原样经既有turn资源所有者进入官方Media；App内Speex转换、延迟补偿与flush循环已删除，TLS不再选择SpeexDSP，481新配置及ELF均无Speex重采样。`dac_prepare`接收源采样率，历史协议调用者明确16k；MIC录音与音量仍走原入口。AIDK主图使用官方`asubgraph`承载`aresample`，因为当前官方主图显式禁用自动格式转换。团队`0004-asubgraph-preserve-drain-errors.patch`修正排空错误被赋值及调用者吞掉的问题，经现有apps视图集成；相关官方源文件无本地tracked改动。没有新增测试代码。既有旧固定采样率回调测试不再对应本次接口，未将其写成通过。

478实板已经证明标准缓冲合同的改进：lower-half接受官方FFmpeg的4块缓冲请求，保留8块物理队列上限与固定DMA帧；DAC从configure到真正START约224ms，477约12秒。478 ASR/Agent均HTTP200；TTS仍在receive82308ms、consume1180ms后超时，PCM99840B、stopped=0/done=0/parser=0，IOB36/36空闲。错误后MIC恢复，手机仍“已连接傻妞”、单独显示对话失败，音量53%启用。证据 `voice478-diag-ordinary-r1/uart/serial.raw`、`voice478-phone-after-voice-ui.xml`、`voice478-phone-after-voice-settings-ui.xml`。479将24k直接送到DAC返回-ERANGE，480缺少启用asubgraph的精简配置、未烧录；这些均不算迁移验收通过。

485交付输入对应 `voice485-diag-full/`、`voice485-diag-build-manifest.json`、`voice485-diag-elfs/`、`voice485-diag-team-sources.tar.gz`。源码646项，树SHA256 `bfe59a53715ae89b3d820e144828fce1fa6c886ceed3fd6735769ac8258ee9f1`，源码包SHA256 `40975cd570dfcfb8fc9868baaa4e40838d68d64adc027cb3d9f78992773d6829`；AP ELF `d4ed9b57bc1d1127560745ccbb26945870cec55d9d3671652d8bcd098bcc9113`，8MiB BIN `49f45b75f48566201a5a967c5ff65d5f6ced760c4e822043fc538ac1a2588f83`。manifest、包及信任验证通过，保护尾区与原板有效base一致。APK仍为0.5.4，未更改认领或用户设置；未新增测试代码，未提交/推送。484首次完成下游的原始证据及同版本产物保留在 `voice484-diag-*`，不作为485本次运行结果。

482因重采样一致性断言终止Media，483通过0005返回错误并恢复MIC/KWS，日志显示16000/256。实际ELF确认AVSampleFormat为1字节，格式变量在sp+47、采样率在sp+48，官方get_format的int指针四字节读写覆盖相邻变量；484通过团队0006按实际枚举类型访问修复，反汇编已确认byte/halfword访问。官方源检出未修改。证据 `voice483-format-overwrite-disassembly.txt`、`voice484-format-getter-disassembly.txt`。

484正式入口的手机扬声器留出回放仍未唤醒：目标音频SHA256 `ab3ce2c3d233a8e3f5fa342847f8d7f94cb2e7b5ef6f97a8b8faef93699419df` 与本地原件一致，播放器确认完整播放目标词及普通指令，但板端max_wake=0，无云请求。41项前处理输入哈希、窗口/步长/量化/张量合同一致；这不验证实际声学输入或真人泛化。证据 `voice484-physical-wake-command-r1/`。正式唤醒、流畅播报、语音看图和本版本真实App OTA仍未验收，不将诊断入口当产品完成。

476历史回合：ASR/Agent均HTTP200，TTS94,376B/PCM69,120B，仍接收超时82,392ms、consume867ms、stopped=0/done=0/parser=0；解码器22:13:03.65完成，DAC22:13:37.85才configure/start ret0，多次欠载，错误后恢复Trigger/MIC，无断言。三帧参数没有取得起播改善。证据 `voice476-diag-ordinary-r1/uart/serial.raw`。

476对应交付输入为 `voice476-diag-team-sources.tar.gz`（643项，SHA256 `b2b6da6e899a48cc2c7e653b85e2017c491c08a072c787ecdf3a1982c73b967e`）、`voice476-diag-build-manifest.json`、`voice476-diag-elfs/` 与 `voice476-diag-full/`。源码树 `d20073238315598bb2f3a1460c32b62c2daf903110e743c5e00aa20056d2ece3`；8MiB镜像SHA256 `6d20d651150de1c4780cf8fa44dab189863b68fea62eb9fc26c135fa1b5addb2`。包/信任/manifest/HIL预检及全量写入通过，复用同设备有效base，保护尾区一致。此为诊断版本，不是项目完成。

475完成的会话架构切片已保留：AIDK AP 已显式关闭 `BK7258_VOICE_LEGACY_GATEWAY`：旧 WSS/Gateway/session 源文件、运行时对象、接收线程和回退调用退出产品构建；ELF 相关符号从62个降为0，`g_runtime` 从8496B降为2064B。公共验证TLS接口移出WSS header，cloud HTTP、认领、直接控制与OTA继续使用各自原入口；不创建第二个MIC/DAC或请求所有者。旧三模块仅由历史配置开关保留。现有WSS主机回归和TLS并发9项通过，没有新增测试代码。

475 启动后配置自动恢复，`CLOUD ready=1 busy=0`，本地Trigger监听运行。Mi10原APK打开后已连接；设置页音量53%启用，真实语音错误后仍已连接、单独提示对话未完成。未修改音量、认领或信任数据。证据 `voice475-diag-status/serial.raw`、`voice475-phone-{initial,settings,after-voice,after-voice-settings}-ui.xml`；不替代465已有的20轮切页回归，也不宣称本轮完成真实OTA。

475 的普通指令诊断为手机合法合成音频回放→实板MIC→ASR/官方Agent均HTTP200；TTS收到115,238B、解码84,480B PCM，DAC configure/start均返回0，但流接收81,869ms后仍超时，stopped=0/done=0/parser=0。IOB为36/36空闲，消费累计931ms；多次播放欠载，未完成整段播报。错误后播放器关闭并恢复MIC监听，无断言。证据 `voice475-diag-ordinary-r1/uart/serial.raw`。对应 `voice475-diag-full/`、`voice475-diag-team-sources.tar.gz`、`voice475-diag-build-manifest.json`、`voice475-diag-elfs/`；源码树 `bd99c06365c4ebb0988514654e0f14d286925098e3d653cfa2a4a7d41dfe9944`，643项。该诊断入口不等于正式唤醒验收。

474仅收到7,680B PCM而未启动DAC的现象现已由实际Media源码解释：s16le 16k单声道每帧约2048B，默认六帧队列需约12KiB后起播；EOF也能启动未满队列。产品将仅在官方Media播放prepare参数中使用已有`datqmax=3`（约192ms预缓冲），不改录音参数、不新增播放器。476已构建、签名并全量部署；起播延迟未改善，正在重新核对实际解码帧、队列和STARTING转换，先前按原始PCM包大小推导的门槛不能作为充分解释。这只能改善首段起播等待，不能解释或宣称修复TTS网络停收。完整SSE终止后的HTTP关闭路径仍未取得实板终止事件证据；下一步继续定位接收停顿，并完成正式唤醒及语音看图。

473 的手机回归使用已保存认领凭据正常认证，TTS 错误后仍显示“已连接傻妞”，单独提示对话未完成；切到设置页回读音量53%，入口启用。“收音或播报时暂不可调整”是启用时的固定说明，不是当前 busy。未改变音量、认领或信任数据；证据 `voice473-phone-auth-ui.xml`、`voice473-phone-settings-ui.xml`。期望音量默认50与官方Media离散档位回读53需区分，不能把两者差异视作配置丢失。

472 留出目标音频的第二次物理回放已补齐播放器证明：UTC21:02:47开始、21:02:50完成，AudioFlinger与MUSIC均指向手机扬声器。同期 MIC 帧均幅最大216、max_wake=0，无队列溢出；未触发唤醒。当前采集为16kHz PCM16单声道MIC1，数字0dB、模拟码0、未启用前处理；未依据弱输入盲调增益或阈值。实际到达MIC的声压/摆位尚不能自动确认，正式唤醒和真人泛化仍未验证。证据 `voice472-physical-wake-playback-proof/`。

摄像头云端联调已获用户对当前画面及指定服务的明确授权；此前执行环境自动审批的拒绝不再是当前阻塞。

唤醒模型仍为 v38。新增幅度训练候选 v42、v43 均未替换产品模型：v42 正常幅度验证流新增 2 次误触发；v43 在三组独立合成目标流中为 16/24、7 次误触发，劣于 v38 的 23/24、1 次。原 v38 在 0.25 倍幅度验证流为 10/10、0 次误触发，0.1 倍为 9/10、1 次误触发。报告与确切训练源码在 `kws-tts-train/candidate-v42-level/`、`candidate-v43-level-preserved/` 与 `validation-level-{025,010}/`；这些均是主机合成语音连续触发评估，不是实板或真人泛化证据。

465 让 OTA prepare/status 共享“已确认且版本和安全计数均严格高于旧事务两端”的判定；状态查询只读返回 IDLE，不清旧记录、不回放也不宣称旧OTA成功。可信保留记录为 source435/target436，当前板为465，不能将436当作实板基线。既有 `run-voice-ota-flow` 通过，流程源码未改。

当前 APK 为 `shaniu-companion-0.5.4-ota-status.apk`（SHA256 `d4b91aa1052325b0c375cdb903c4a7e6ec9380caf8684dea6049518a11dd270a`）。升级页经原单请求调度轮询 OTA_STATUS、INFO 后按代次重读，OTA新鲜度与普通 snapshot 分离；真机读465/counter465，显示“当前没有进行中的升级任务”与音量53%启用。系统蓝牙 off/on 后未点连接即恢复并重读当前OTA状态（动作UTC18:12:35）。文件选择器第一次Back仅返回上级目录，检查期间超过30秒后台宽限；取消后UTC18:16:01自动重连，重读465与当前OTA空闲状态。此连接持续超过6分钟后完成20轮切页（60次点击，UTC18:22:08–18:22:16），窗口内GATT connect和扫描日志均为0，前后音量53%启用；另一次文件选择器2.04秒取消返回也无新增连接/扫描日志，未选择文件。证据为工作区输出中的 `ota465-phone-v054-picker-recovery.log`、`ota465-phone-v054-tabs-*`、`ota465-phone-v054-picker-short-*`；不将首次Back导航误判为连接故障，也不把超过宽限后的恢复写成长时间后台保活。

`ota465-confirmed-camera/serial.raw` 记录当次 BKVISION SNAPSHOT 640×480 JPEG 26,607B、SOI/EOI完整、V4L2无错误；这不是图片理解或完整语音看图链。正式实板唤醒、完整播报与语音看图仍未验收；下文 465 及更早记录均为对应历史版本证据。

`trigger442-build-v17.log` 记录相同模型的增量构建通过：AP 原始镜像 1,471,512 字节、SHA256 `57fb4e3f16b80c66c7f180087dace42553cd7f2eae544bb1224e9a097e9f90cf`，模型在镜像中恰好出现一次，配置固定哈希匹配；使用锁定 GCC 的 STL、Ruy instrumentation 和官方 Media graph/policy/Trigger。对应 `trigger442-full/release.json` 已用原受信 BL1/MCUboot 身份签发 `18.6.355+442 / floor442`，package、trust 与 HIL preflight 通过，8 MiB operator SHA256 `cebb990f7464600ed0e4dc79b27010af99ae291d1cd9eb0cf391b7b7ecc05bab`。同设备基底和唯一数据尾区哈希匹配，未新增备份或设备写入；软件复位仍无响应，烧录与实板加载/arena/延迟/声学联调等待 K1 物理复位。

新增音频恢复修正已进入 `18.6.355+443 / floor443` 候选：当前 NuttX 本地套接字的 FIFO 读写不使用 `SO_RCVTIMEO/SO_SNDTIMEO`，产品改用非阻塞官方 Media API 与单次传输 1000 ms 的 `poll` 期限；短写在同一期限内续传，取消/EOF 回到原会话清理。团队 `0002-stream-io-lifetime.patch` 修正官方客户端准备失败遗漏监听 FD、短传输误判和关闭后 FD 状态，未修改官方依赖仓。`media443-build-v20.log` 构建通过，AP 1,471,880 字节、SHA256 `60ae240c8013b3339f22b627b0f54284c56e52ebff78ddea052e00b1b9e7485a`；原 turn/audio 与音量主机回归通过，它们不覆盖真实 Media/FIFO/MIC/DAC。`media443-full/release.json` 的包/信任/HIL 预检通过，8 MiB operator SHA256 `fd1526936cc31b28995fea22fae9745de40541d167c29c59be46cc862bebd67c`，尚未烧录。`media443-team-sources.tar.gz` 保留 637 个构建输入，来源树 `69e21d23…5a13`，归档 SHA256 `673e030d8a70c35670f7379dd53d61271e06e85fd24380ba47d33a9937cfc34f`；实际 AP 中 v30 模型仅一份。442 保留为前一候选，不覆盖其签名产物。 最新 `media443-target-readiness/serial.raw` 仍为 0 字节，短 `bkota status` 没有回包；尚未取得 K1 复位后的启动证据，不能执行软件复位接管。实板仍仅能引用此前最后确认的 440，443 的模型 arena 实占、推理时延、录放/摄像头和 Android 跨端回归均未核验。

上述证据位于工作区 `out/shaniu-p0-20260913/`；`trigger442-v30-build-manifest.json` 与 `trigger442-team-sources.tar.gz` 保留该候选的实际源码状态（636 个构建范围文件，来源树 `10019e9b…a816`），不依赖后续训练工具改动。441 OTA 包和旧事务/串口故障日志继续保留；它们不表示 442 已运行。本轮没有新增测试代码、夹具、探针、验收脚本，也未暂存、提交或推送。本次原生 Codex 会话的公开消息和工具调用经格式映射后复用比赛 collector 导出，保留原时间戳/内容并由原工具脱敏；未导出内部推理或系统指令。`logs/lijian/2026-09-13/codex__01a098d3-72cb-7a91-89fc-038599cc8cb1.jsonl` 当时导出 1,385 个事件（历史快照），原生快照哈希及映射说明在 `contest-log-export/export-provenance.json`，对应单会话官方格式校验通过。

v30 的原训练配方及构建输入仅封存在 442 归档，不能调用当前训练入口或换入 v31 环境 manifest 后仍称同一模型；canonical v38 按其候选目录中的元数据与当前训练代码复现。443 及更早构建命令、镜像和部署叙述均为历史证据；当时待执行的是 K1 复位后的 445 部署、官方 Media 路由格式协商、v38 实板模型加载与真实声学联调，不能提前写为通过。

当前开发身份：官方仓库 `open-vela/contest2026_135_yongwangzhiqian`，`Embracecactus` 为 fork；本地 `openvela` / `fork` remote 分别指向两者，分支 `feat/shaniu-feature-acceptance`、HEAD `1acfefe024c80339c54eebaf1a5c0b5b0eb6597e`。保留进入本轮时的 KWS/监听/测试/计划改动和未跟踪日志；未暂存、提交、推送。当前 manifest HEAD `fe2feda23ddf3fd9671036e5712a93b5775fe3e4`；实际 Agent `41723c61…`、Media `fb7db0e9…`、TFLM `94f7cee1…`、SDK `cb080de1…`。活动 manifest 和团队新版 linkfile 有差异，未覆盖；现有隔离构建树提供所需 overlay。

本 goal 启动时的历史基线（后续版本与现状以上方为准）：当时 COM8 读取得到 `18.6.355+436 / counter=436 / confirmed`，云服务 ready=1、busy=0；此处 436 仅来自当时串口记录。手机 Mi10/59d707dc 原 APK 0.5.0-a1（SHA256 `b426e016…`）；新版 0.5.1-control 已 `install -r` 成功，保留数据。冻结的 P0 437 仍是原 voice-service 配置；随后 provider 候选已经链接官方 Agent 请求库，但未启动完整 Agent loop、官方 Media 服务或有效 KWS 模型，不能计作核心架构或真实唤醒已完成。

本批证据统一位于工作区 `out/shaniu-p0-20260913/`，主机和实板证据分别归类。旧发布、旧身份、其他参赛仓库均不证明本批功能完成。

## 2026-09-13 当前两个 P0 与迁移合同

| 现有职责 | 官方实现或扩展接口 | 保留产品差异 | 被替代实现 | 验收点 |
| --- | --- | --- | --- | --- |
| 页面直接管理控制连接与快照 | 原 GATT/TLS + 单一 DeviceControlSession 调度 | 主动断开、前台策略、设备回读确认 | 页面连接字段、空快照当断线、日常绝对 120 秒期限 | 同连接切页 20 轮；前台超过 120 秒；返回 Activity；真实音量回读 |
| 自有对话请求/消息/工具 | 官方 Agent request/provider 与后续会话扩展 | 认领、隐私、取消与产品会话策略 | 自有请求序列化/解析和重复工具循环 | 目标配置编译链接并实际调用，一次请求仅一个重试所有者 |
| App 同名 Media ABI 桥与音频路径 | 官方 Media graph/policy、FFmpeg NuttX 音频接口 | 半双工交互与取消策略 | 桥接伪实现及重复通用音频机制 | 收音→ASR→对话→TTS→真实 drain；MIC/DAC 单一仲裁 |
| 本地唤醒监听 | 官方 Media Trigger、TFLM 和前处理 | 唤醒阈值、前滚与隐私窗口 | 重复 MIC 生命周期及可替代前处理 | 有效模型真实唤醒；合成 fixture 不作为产品证据 |
| 语音看图及硬件边界 | Agent 多模态/工具、V4L2；chip 机制、board 实例 | 当轮取图和用户交互 | 重复多模态封包及越层实例控制 | 真实语音→当次摄像头→理解→播报 |

- CP 实测 16 MiB 容量以同 generation 的 boot record 交给 AP。
- SDK 的低 8 MiB 地址保持不变，高区 4 MiB 由 NuttX 唯一 `systemheap` 使用。
- board 选择 `MM_REGIONS=3`；容量不匹配拒绝启动，上游既有 64 MiB MPU aperture 无需修改。

- 已复现原 APK 认证后音量仍灰色；无切页持续连接在 `120062ms` 记录 `session_timeout`，约 140 秒界面断连。现有 Android 与板端均有绝对期限，整改为配网有界事务、认证控制按有效活动续期，保留握手/ATT 写/命令超时。
- 新控制 owner 串行调度读写并共用全部标签；连接、快照新鲜度、读写进度及操作失败分离。背景 STATUS 不锁全部控件；写入必须随后 STATUS 回读才确认，旧代次回调和掉线前操作不重放。离开前台只保留 30 秒 grace；主动断开不自动重连。
- 音量 STATUS 不再因 SD 偏好读取失败而一同丢失，读写接入现有唯一播放音量回调及持久存储；读取失败、未知、处理中、设备忙分别提示。当前播放/收音期间调音仍不支持，界面明确提示结束后调整，未只删 App busy 条件。
- 已安装版本在真实 Mi10 完成三个底部标签往返 20 轮（60 次点击），该窗口内无新增 GATT connect；系统文件选择器打开并取消返回耗时 8.56 秒，未新增 GATT connect，版本仍可读取。旧 436 在 121.38 秒主动断开后，新 App 自动重连认证并恢复状态；这不是 437 长连接通过。证据 `tabs20-*`、`picker-short-*`、`file-picker-gatt.log`。
- APK `shaniu-companion-0.5.1-control-final.apk` SHA256 `92afe9d6bccc870688e8051395e340f111e98cdcc02a137a5e99854d6e3e820b`；Android 155 项中 153 通过、2 项按原互操作条件跳过。控制 owner 假时钟覆盖 >120 秒、配网期限保留、20 轮页面观察订阅、串行写回读、错误不掉线、断线不重放、主动断开和有界后台。
- 既有 host `test-control-pair`、`run-voice-turn-audio` 与 `test_provision_tls.py` 通过；包含真实 mbedTLS 分片/认证期限及音量量化读回。实板音量两个值和扬声器效果、437 超过 120 秒及完整 OTA/取消回归尚未验证。
- 最终 APK install-r 与相同实机流程复验通过：20 轮标签往返、系统文件选择器 8.21 秒取消返回，全程单个 GATT connect、0 个 transport_close，真实回读仍为 436。扫描日志没有可用 startScan 标记，不能用零条日志独立证明扫描调用次数；页面动作未调用扫描入口。证据 `final-phone-validation.json` 与 `final-phone-*`。
- 官方 Agent provider 已经通过 build-tree 补丁机制编译/链接到 AIDK AP；`bkcloud_chat` 与 `bkcloud_understand_jpeg` 经统一适配实际 ARM call 到 `llm_chat_tools_request`，旧 chat/image 的重复请求封包和文本解析退出两条主调用路径。已用当前官方源码与真实 OpenVela webclient/mbedTLS loopback验证原 model、两 dialect、ASR/TTS/JPEG wire 合同；产品仍持有唯一会话与 TLS 取消/期限，Agent request API 不自重试。此批未上板，官方完整 loop、工具执行及 Media 仍待迁移，不能标记架构完成。官方 Media 仍须替换 App 同名 ABI 桥。兼容真实唤醒模型/授权真实语料缺失，已有两份合成候选不满足正式验收。

此前源码分支发布记录（不作为本 goal 新功能验收）：复用下述OTA flow/store/cancel、NFC UART、Android及436实板证据；HIL现有15项单测另行执行通过，日志`feature-acceptance436-publication-hil-tests.log`。差异格式和新增行秘密模式检查通过。该次是源码分支交付，不新增固件构建、签发、刷机或完整产品验收；远端分支SHA及与官方基线的比较结果以发布后核验记录为准，PR由用户创建。

## 2026-09-13 开发期停用Mi Pay，核对真实唤醒入口

- Mi10/59d707dc执行`pm disable-user --user 0 com.miui.tsmclient`，disabled列表回读确认。仅停用应用，未删除卡片或数据；恢复命令`pm default-state --user 0 com.miui.tsmclient`保留，开发期不自动执行。证据`feature-acceptance436-mipay-development-disabled.json`。
- 436封存build-manifest与现有AP配置/ELF哈希逐一相等：配置`33204b87…`、ELF`fb64ddff…`。KWS、WAKE_RUNTIME、模型路径及哈希均未启用；VISION_SERVICE=y且实际ELF有`bk7258_vision_capture_jpeg`。因此缺口是有效模型及对应固件接入，不能把当前436当作可直接喊醒的固件。证据`feature-acceptance436-wake-entry-readiness.json`；未为核查重新构建或操作串口。
- 限定引用路径内未发现可复用的有效模型/真人语料；既有合成fixture仍只证明转换管线。现有入口`voice kws audit/train`与16kHz、单声道PCM16、两秒窗、INT8 `[1,99,40,1]`三分类模型保留，不另建训练或唤醒框架。真实语料需包含经同意的官方词、普通语音/近音反例、背景声，并按说话人分离训练与评测。当前没有采集真人录音或启动训练。只读输入核对指定gpt-5.6-terra，实际模型路由未核实。
- 定向查到的其他参赛实现采用云ASR文本匹配openvela（[公开补丁](https://raw.githubusercontent.com/open-vela/contest2026_130_xingshuangrenran/dev-ai-contest-2026/docs/%E8%AF%81%E6%8D%AE/%E4%BB%A3%E7%A0%81%E6%94%B9%E5%8A%A8/packages_ai_agent.patch)），并未提供可直接接入本地TFLM的唤醒模型；未将其替换成常驻上传方案。当前真实唤醒和语音看图均未验收，下一输入为有效模型，或用于现有训练入口的已授权真实语料。本轮未编译、签发、刷写、提交或推送。

## 2026-09-13 436新身份配置与重启恢复实板通过

- 同一Mi10/59d707dc、APK b426e0…、固件18.6.355+436，源码为`cae8d99d3022d1f02ca949596296a400a3c82a79`加现有未提交差异。复用前述真实NFC locator、认证BLE扫描及激活导入结果，通过真实控件填写已授权配置并点击一次“保存并连接”。22.1秒页面显示“设置已保存／设备已确认保存连接设置”，终态日志为`tls_established=true protocol_state=COMMITTED failure_stage=none failure_code=none`。`category=OTHER`发生在COMMITTED后的关闭，不能标成失败。证据：`feature-acceptance436-config-retry-v2.{json,raw}`、`feature-acceptance436-config-retry-v2-final.xml`、`feature-acceptance436-config-retry-v2-phone.log`。
- 串口实际DHCP成功，`configured=1`、`CLOUD ready=1 busy=0`；当前`bkcloud_runtime_ready`要求配置端点的真实chat预检成功，该结果不是只保存表单，也不是ASR/TTS验收。`connected=0/gateway_ready=0`属于冻结的Gateway路径，不代表直连云失败。存在性检查确认活动`/data/shaniu/config.bin`和`identity/config.bin`，以及保留的`config-before-nfc436.bin`、`identity-before-nfc436/config.bin`均存在；仅以独立标记行判定，不以命令回显判定。证据：`feature-acceptance436-new-config-persisted.raw`、`feature-acceptance436-final-new-config-status.raw`。
- 使用现有`deploy --reboot-only --expected-version 18.6.355+436 --expected-counter 436`正常重启，CP/AP配对confirmed、AP/CPU2/RPTUN正常，Wi-Fi重新取得地址。App force-stop后正常启动、保留数据，通过实际设备选择重新认证，首页“已连接傻妞／设备已连接Wi-Fi”。证据：`feature-acceptance436-new-config-reboot.log`、`feature-acceptance436-reboot-reauthenticated.{xml,raw}`。用户随后人工复位，正常控制台再次确认`configured=1/CLOUD ready=1/last_error=0`，证据`feature-acceptance436-user-reset-status.raw`；没有据此重刷或重新供应身份。
- 已将测试临时停用的`com.miui.tsmclient`恢复原default状态，将默认钱包恢复“小米钱包（小米公交、Mi Pay、门卡模拟）”；没有操作银行卡、门卡、支付或钱包修复。证据：`feature-acceptance436-mipay-restoration.json`、`feature-acceptance436-wallet-restored-check.xml`。复位后尝试经正常App入口进入NFC页，前台重新成为`com.miui.tsmclient/.ui.quick.DoubleClickActivity`；原钱包条件下未完成NFC按钮/射频观察，不能写成射频失败或共存通过。该轮未提交配置，原设置保持恢复；证据`feature-acceptance436-nfc-original-wallet.json`。前序焦点导航未找到“设备配置”、尝试启动未导出Activity被系统拒绝，均为自动化导航失败，不是板端验收失败；未放宽Activity导出权限。
- 当前App导入反馈修复已真机通过，终态日志已捕获本次COMMITTED；沿用对应APK的10项ProvisionClaimProtocolTest及增量构建通过结果，没有为文档再跑测试。历史6秒TLS status19断开的精确原因仍未定位；本轮成功不证明已消除间歇断开或长期稳定。保留先前失败证据与输入法自动化问题的区别。
- 下一项限制：有效官方唤醒模型仍缺失，当前有效配置未启用VOICE_KWS/VOICE_WAKE_RUNTIME；既有合成pipeline样本不作为真实唤醒模型，不以PTT、注入或固定图片替代语音/看图验收。原钱包共存问题另列，不回退已通过的新身份配置。当前没有提交或推送，本节证据均位于`out/shaniu-product-acceptance-20260910/`；可安装App为`feature-acceptance436-app-claim-stage.apk`。完整产品仍为IN_PROGRESS。

## 2026-09-12 App OTA实机拒绝定位与最小修复

- 用户明确允许点名的激活文件导入Mi10后，原审批阻塞解除；同一激活SHA `a57dc957929d8349af8062193a2d0b2665a3951f8d7631ad4de43a34c0691496`经系统文件选择器成功导入b426e0…，手机临时副本随后删除并核对不存在。当前APK自动滚到“资料已就绪”，导入反馈修复真机通过；证据 `feature-acceptance436-second-activation-import.json`和`feature-acceptance436-second-activation-imported.xml`。身份未再次生成，固件签名身份未变。
- 手机自动化根因之一已实证：KEYCODE_ESCAPE未收起该手机输入法，mInputShown/mIsInputViewShown仍true，后续滑动触及键盘把SAF查询由nfc改成nfc......。现按实际输入法状态发送BACK并核对已隐藏，正确查询找到同一文件；不是SAF权限或App解析失败。先前一次配置的输入准确性未在所有后续手势后复核，不把其TLS断开直接归因于固件；本轮后续用真实控件焦点定位、填写后复核，未新增产品协议或自动化框架。

- 首次配置失败历史（已由上方同身份重试通过结果更新当前状态）：已从板端扫描结果选择TP-LINK_8937，按授权填写网络/云字段，输入长度在手机内核对，值未写入证据。通过真实“保存并连接”提交一次；最终失败为 `disconnected stage=tls elapsed_ms=6077 gatt_status=19`，发现服务/MTU70/订阅均成功。手机端未获COMMITTED，串口configured0、活动配置文件不存在，故未重复提交待定事务。证据 `feature-acceptance436-nfc-configuration.{json,raw}`、`feature-acceptance436-config-gatt-tags.log`、`feature-acceptance436-config-provision-tags.log`、`feature-acceptance436-config-failure-status.raw`。6秒断开尚未定位根因，不据此换身份或改协议/超时。
- 既有ProvisioningConnection终止日志只含CONNECTION_CLOSED，无法区分TLS是否建立及AUTH/APPLY/VERIFY。本轮只增加tls_established/protocol_state/failure_stage/failure_code，均为布尔/枚举/错误码，无敏感负载。既有ProvisionClaimProtocolTest 10/10通过，assembleDebug增量通过；候选b426e0…（含前述导入反馈修复）已install-r到同一Mi10，未清App数据。原458e…未安装并被此候选取代。可安装文件 `feature-acceptance436-app-claim-stage.apk`，安装证据 `feature-acceptance436-claim-stage-install.json`。新增日志尚未捕获下一次实际失败，不宣称已修复status19。
- 为核对新认领从存储加载，使用既有deploy正常重启一次，436 confirmed、AP/CPU2/RPTUN正常，supervisor faults0/recoveries0；未刷写/生成密钥/格式化，旧原件不动。证据 `feature-acceptance436-new-identity-load-reboot.log`及`feature-acceptance436-after-identity-reload-status.raw`。新APK真实“碰一碰”再次显示已碰到设备并只返回SHANIU7F:81，已选择；见 `feature-acceptance436-second-nfc-devices.xml`。本轮部分安全页面screencap返回空文件，不将这些PNG列为有效截图；页面行为依据真实UI hierarchy、GATT及UART记录，未关闭FLAG_SECURE。
- 当前唯一认领执行阻塞：重新安装App后，需要把同一受控 `claim-20260913-nfc436/owner-activation.json`（含possession_secret，公开证书c65daa11…）重新导入同一Mi10/59d707dc。自动审批拒绝该ADB临时文件传输；核对同一文件SHA/此前成功导入/当前手机后提交一次复核仍被拒，理由是未认可现有用户消息对确切payload和目的手机的授权。两次命令均未执行，未换工具绕过、未导出私钥或创建另一个身份。最小恢复动作仅为用户明确点名这份激活资料和Mi10的导入授权；之后复用同一材料，导入后删除手机临时副本，从第二次认证配置继续。当前无commit/push。

- 436新身份激活资料已通过真实Mi10系统文件选择器导入；初次状态采集停在页面中部，随后滚到顶部实际确认“资料已就绪”，不是认证失败。证据 `feature-acceptance436-nfc-activation-visible-status.{xml,png}`、`feature-acceptance436-nfc-activation-import-final.json`；手机临时激活文件已移除，受控原件保留。
- 436真实NFC发现后继续同一认证BLE入口，板端Wi-Fi扫描返回21个热点，App实际显示并选中TP-LINK_8937。证据 `feature-acceptance436-auth-wifi{.raw,-result.xml,-status.xml}`、`feature-acceptance436-wifi-selected.xml`。两次自动化定位未找到按钮是滚动方向/步长问题，未触发认证，不计为App故障；尚未完成本轮配置最终提交。
- 针对导入成功/错误提示在视口外的实际问题，ProvisionActivity三个导入反馈改用既有reportStatus；候选APK `458e7474626da0379086d00c4163d6aa29f4f481240ff753880c984b516769d0`定向单测和增量构建通过，暂未安装以保留当前进行中的激活事务。现用手机APK仍为057acd…；常规实现由既有子代理完成，实际模型路由未独立核实。签名身份、固件和协议未改。

- 用户明确补充授权原认领与配置保留改名后继续新认领：两个原件已保存在`/data/shaniu/identity-before-nfc436/config.bin`和`/data/shaniu/config-before-nfc436.bin`，当前活动身份/配置路径已移空，未格式化/删除。初次自动化前置检查的一条长命令超过NSH行长引发语法错误；实际mv无报错，但不采用脚本返回码证明安全，随后五个短行独立路径检查确认原件存在、活动路径为空且没有目录嵌套，未重复移动。证据 `feature-acceptance436-claim-preserve{,-check}.raw`。既有deploy正常重启436 confirmed、supervisor faults0/recoveries0通过（feature-acceptance436-new-claim-reboot.log）。
- 新认领首候选缺少当前identity_load强制要求的clientAuth EKU，供应commit返回-10240，未安装；这是本轮生成材料错误，未改固件绕过校验。保留该未安装候选，复用同一新私钥和proof，仅将设备证书用途补为serverAuth/clientAuth；新公开证书SHA256 `c65daa11ae2a3d34755a50e73fcd2f399c6ced9bf8bb74dd7c7e75658b9f888a`。既有voice pairing --resume供应628字节返回identity-supplied，证据 `feature-acceptance436-claim-supply{,-v2}.log`和claim-public.json。当前新认领活动，尚未完成本轮新配置提交；旧配置原件保留。
- 真实Mi10/App057acd…在436的新认领身份下点击“碰一碰查找”，5.8秒由真实RF locator只找到匹配SHANIU7F:81，未注入或发bknfc hce。证据 `feature-acceptance436-nfc-new-identity-discovery.{json,raw,xml,png}`。继续通过真实系统文件选择器导入对应新激活资料；首轮自动化误把系统AutoCompleteTextView当成EditText，未导入且临时副本已删除，修正定位重试，不计作App认证故障。

- 用户随后明确“这种就重新生成”，旧激活原件路径请求取消，不再要求用户寻找旧文件。已按授权生成新认领身份与四字段激活原件，引用`aidk-ai-toy-001/claim-20260913-nfc436`，公开证书SHA256 `346152b2dbad62cd52a94249ea6ff9ab3436cb2d781e7402aa7d8110071534a1`；受控目录0700、私钥/激活资料0600，固件签名身份未变化。激活原件使用宿主受控`shaniu-identities/<引用>/owner-activation.json`，后续通过既有voice pairing --resume使用，不再生成第二份身份或扩搜旧钥。证据 `feature-acceptance436-claim-public.json`。
- 历史阻塞（已由本轮明确授权解除，执行结果见本节前项）：当时仅新认领材料生成通过，尚未供应板端或导入手机。自动审批拒绝了将现有`/data/shaniu/identity`及`/data/shaniu/config.bin`原位保留改名的具体操作，理由是“重新生成身份”未明确覆盖当前持久化配置迁移；命令未启动，原claim-20260912及配置仍在，未换工具绕过。已一次请求明确的保留改名/暂时未配置/重新认领授权；获准后保留旧原件，正常重启进入新认领，供应新材料并通过真实NFC→同一BLE认证→配网提交验证。没有格式化、全量刷机、读回秘密、提交或推送。
- 436 OTA后正常bkvoice status确认ready1/configured1/cloud ready1/busy0，证据 `feature-acceptance436-config-status.raw`；这仅证明当前配置加载状态，未发起新的云端语音或图片请求。COM8和手机由根代理独占，现已释放串口。

- 2026-09-13继续验收：435经现有deploy正常重启后，真实Mi10 App发起436；全程未调用bkwifi status或手动同步网络。约472秒串口确认A436、manager0/error0、supervisor faults0/recoveries0；手机重新认证后实际回读436/100%/已确认完成/无错误。原435负载未重编，436包SHA256 `e0198c100af0254f26199c61ead6e79c29c3e39c1164293843cc0697b9fca834`，APK为`3f2cd2028ffc34cf043bc42c493a9f4d94789eb6aa44e6821cfb0a8e495616bb`。证据 `feature-acceptance436-source435-reboot-windows.log`、`feature-acceptance436-{release,trust}.log`、`feature-acceptance436-phone-ota-v1.*`、`feature-acceptance436-version-confirmation.*`。这证明本次重启直达OTA可用，不证明历史-101根因已解决；未新增网络API或修改SDK/NuttX。
- confirmed436后卡片仍保留“已结束，等待版本核对”标题的实际问题已修复：MainActivity复用原OtaUpdatePolicy.confirmed完整设备/目标版本/计数/状态判定，真正确认才显示“已确认完成升级”，未放宽错误/回滚条件。APK SHA256 `057acd22231f129d69052de674597204e373f4c10c8fea5ee36d75dca351f5fa`已install-r；真实Mi10重连436后正确标题、100%、无错误及目标版本提示通过，没有再做OTA。证据 `feature-acceptance436-app-confirmed-title.*`、`feature-acceptance436-app-title-validation.json`、`feature-acceptance436-app-ota-confirmation-install.json`；可安装APK `feature-acceptance436-app-ota-confirmation.apk`。143项现有单测中141通过、2按原条件跳过；DeviceUiAcceptance既有场景只补状态一致性断言，assembleDebugAndroidTest增量编译通过（43任务/4执行/39未变化，日志feature-acceptance436-app-title-test-build.log），未重新跑无关fixture；实机由根代理操作。官方有效唤醒模型仍未提供，日常唤醒与语音触发图片理解不计作通过。

- 2026-09-13继续时修正-101判断：当前源码Wi-Fi worker已经250ms定时同步CP租约，实际435 AP ELF也有超时分支调用，编译输入确认来自当前团队源码；`feature-acceptance435-ota-native-worker.asm`保留反汇编。旧“-101说明未自动同步”的结论证据不足：既有HTTP等待还会把poll错误归并为-101，失败时未采集AP接口状态，bkwifi status之后成功仅为相关性。不新增重复同步API，不宣称已找到根因。随后正常重启后真实App OTA436且全程不执行bkwifi status通过；复用435原负载，仅按现行计数规则签发436，不重编启动链/镜像。

- 本检查点结束状态：COM8确认B435 confirmed、manager idle/error0、supervisor faults0/recoveries0、configured1/cloud ready1/busy0（`feature-acceptance435-final-status.raw`）。只是配置加载状态，未发新的云请求或声称官方语音/图片理解通过。COM8已释放；App数据、板端身份及网络/云配置保持，MiPay按测试要求继续关闭。后续限制：官方有效唤醒模型仍缺，NFC后新配置事务需App载入原设备激活资料；早先首次OTA出现source-open -101；本次已核实存在自动租约同步，436在正常重启后没有手动同步即升级成功，-101历史单次原因仍未确认，不能写成网络驱动修复。未提交/推送。

- 最新App仅追加nextPage两处本地拒绝使用既有reportStatus/revealStatus，不改变激活/身份校验。6项ProvisionSettingsTest及增量构建通过，APK SHA256 `3f2cd2028ffc34cf043bc42c493a9f4d94789eb6aa44e6821cfb0a8e495616bb`已install-r。第二次真实NFC由435自动交接，6.7秒只找到匹配目标；点击下一步后“请先导入随设备提供的激活资料，再继续设置网络”完整滚动可见，未开始新认领/配置事务。证据 `feature-acceptance435-nfc-repeat-feedback*`、`feature-acceptance435-app-nfc-feedback-{build.log,install.json}`，可安装APK `feature-acceptance435-app-nfc-feedback.apk`。实现复用原T2代理，运行时模型路由未核实；根代理独占手机/串口验证。

- uiMode专项补充：MIUI dumpsys显示mNightModeLocked=true，通用cmd被忽略；随后通过系统正常“显示与亮度”界面切到深色，实际mCurUiMode=0x21，返回MainActivity仍在OTA页且无对应destroy/create；恢复浅色后为0x11。证据 `feature-acceptance435-uimode-{dark-state.log,dark-return.xml,light-restored.log,settings-lifecycle.log}`。此项为真实系统配置变化的页面保留验证，未在活跃OTA中再次强行切换；完整OTA成功证据仍为v3，不能把那次未生效命令写成在线切换通过。MiPay维持用户要求的disabled/HCE钱包，未更改卡片。

- 435 NFC真实射频交接已通过：Mi10保持MiPay停用/HCE钱包且天线物理贴近，真实App点击碰一碰后3.7秒显示“已碰到设备，正在查找对应的蓝牙连接”，只列出匹配locator的SHANIU · 7F:81。没有发送bknfc hce/注入locator；同次raw确认435/manager0/faults0。证据 `feature-acceptance435-nfc-stage.{raw,json}`及-0.xml/png；UART事件等待修复通过本观察点。点击匹配设备后仍使用原ProvisionActivity激活/认证流程，未重新供应身份或提交新配置；发现交接不等于新认领事务已完成。

- 435第三次App更新完成：修复后的APK SHA256 `3f6edc294ea97d78ecff0c9032226b342a3761666ee88c046e4dbd3b2f491a09`，增量构建及17项现有OTA单元测试通过，install-r保留数据。真实App发起435，越过此前720896字节中断点，写后校验、自动重启及B435 confirmed通过；App重新认证回读435/100%/已确认完成/无错误。证据 `feature-acceptance435-phone-ota-v3.*`、`feature-acceptance435-version-confirmation.*`、`feature-acceptance435-app-uimode-{build.log,install.json}`。注意本次cmd uimode night yes返回仍为no，不能把发送命令当作已真实切换；日夜配置切换专项仍待验证。原434失败日志已确证系统0x200配置重建关闭来源，本次实际OTA成功不独自证明日夜切换验收。接回435 NFC验证，未重签包、未改身份/配置。

- 435第二次实际App更新已进入AP镜像写入，在720896/1183744处manager7/phase3/error=-129，仍运行A434。此返回点位于下一段TLS建连，不是包签名失败证据；手机07:00:00.067记录Config changes=200（uiMode），紧接MainActivity stop/destroy/create，已接受的HTTPS来源因现有页面清理被关闭。证据 `feature-acceptance435-phone-ota-v2.*`、`feature-acceptance435-ota-config-change.log`、`feature-acceptance435-ota-lifecycle-events.log`。最小App修复仅声明固定浅色MainActivity自己处理uiMode，不豁免其他页面生命周期；增量构建和真实切换复测进行中，复用原435包，不重签/清配置。

- 435 App首轮已接受来源，但source-open返回-101，manager7/phase0/progress0/0，434仍confirmed，没有新镜像写入。证据 `feature-acceptance435-phone-ota-v1.*`，手机只有listener无TCP accept。调用既有bkwifi status后link3/ip192.168.0.102/router192.168.0.1，见 `feature-acceptance435-source-route-sync.raw`；该接口具有AP native租约同步副作用，不能称仅查询或产品自动恢复。正以改变后的网络状态重试原435包，未重签/清数据；自动同步缺口做定向定位以免反复依赖控制台。

- 435候选已完成：只改AIDK MFRC522 UART RX等待为NuttX file_poll+sem事件等待，每字节固定200ms总上限，9600及协议不变；poll在sem离栈前注销。复用现有test_mfrc522_deadlines.py验证即时数据、注册竞态、唤醒、10ms tick下虚假唤醒/EINTR不续期、超时/错误/注册失败清理通过，原RF deadline用例保留。增量只重编AIDK NFC对象，现有身份签署435并验签通过，包SHA256 `d43efcc72f8369bb8fb88462bcc69a31d21c6ccf41c65ade9a4a2b2939252681`。证据 `feature-acceptance435-nfc-rx-{host,build}.log`、`feature-acceptance435-{release,trust}.log`及ready.json。该条为构建检查点；后续部署及NFC实测结果见顶部435记录。434另以bkvoice status确认configured1/cloud ready1/busy0，见feature-acceptance434-config-status.raw，未发起新的云请求。

- 434真实NFC阶段证据：Mi10前台HCE、MiPay关闭、HCE钱包下，显式bknfc hce返回 `terminal stage=select-aid ret=-110`，证明本次UID选择及ISO-DEP激活已过，SELECT交换未过。证据 `feature-acceptance434-nfc-stage.{raw,json}`。手机自己的AID/服务路由已注册，HostEmulationManager记录绑定ShaniuHceService、Service bound、Sending data后约135ms失活（`feature-acceptance434-nfc-phone-routing.json`、`feature-acceptance434-nfc-host-events.log`）。当前唯一候选是UART每字节EAGAIN后usleep(1000)在10ms tick下拖慢ISO-DEP响应；实际UART9600，未改变波特率/时序校验。准备使用现有串口事件等待替换固定sleep，仍需下一版实板证实，不宣称已修复NFC。

- 434最终App确认通过：升级后真实Mi10重新认证，OTA_STATUS显示“已结束，等待版本核对 / 阶段：已确认完成 / 进度100% / 结果无错误”，并依据版本回读显示“设备已确认完成升级：18.6.355+434”；串口同次pair confirmed434。证据 `feature-acceptance434-version-confirmation.{raw,json,png}`。这验证了同AP generation=1的target判定修复，不扩大为长期重连、断电回滚或整个产品验收通过。下一步真实NFC阶段定位，MiPay保持关闭。

- 434实板部署通过：Mi10真实App432→434，传输期间未重认证，HTTPS服务跨BLE会话到期继续，约437秒捕获目标confirmed；新鲜串口active=A、18.6.355+434/counter434、manager0/error0、supervisor faults0/recoveries0，B432保留。证据 `feature-acceptance434-phone-ota-v3.{raw,json,png}`及server.log。此处仅确认写入/校验/启动，App最终OTA_STATUS及NFC阶段测试继续执行。

- 434真实App升级已开始：前两段自动化分别遇横屏按钮不可见及BLE会话到期（按钮禁用），未发起OTA，不算固件拒绝；按真实页面修正滚动后重新认证一次，App接受434并显示下载1%，COM8 manager state2/phase2/image1/progress131072/1183744/error0。证据 `feature-acceptance434-phone-ota-v3.{raw,json}`（运行中）；传输期间不重连、不更换包，最终结果另记。

- 2026-09-13用户物理复位后，新鲜COM8再次确认B432 confirmed/manager idle，单次目录查询确认confirmed-432.bin存在且config.bin已不存在；上次改名已成功，不重复移动。证据 `feature-acceptance434-after-reset.{raw,json}`。继续真实App434。只读定位表明mv不使用getopt，有限ls参数不能正常产生1681次未知选项错误；候选是NSH argv/getopt指针完整性，尚无写坏位置证据，不把重置optind当根因修复，不扩大本轮改动。

- 2026-09-13用户已授权后续同类保留记录与受信固件验证操作，不再为相同范围重复请求。434部署准备中，先确认432 pair confirmed/manager0，首次无选项ls成功；mv将config.bin保留改名命令返回提示符，第二次同样无选项ls却循环1681条argument invalid，未能独立确认目标文件。证据 `feature-acceptance434-intent-preserved.{raw,json}`。因此旧“只避开ls -l”的假设不足；未重复改名。现有deploy正常重启未取得健康响应，见 `feature-acceptance434-pre-ota-reboot.log`，434尚未安装。控制台问题仅作为本次NFC部署阻塞做限定定位，恢复后核对改名结果并返回App434。MiPay继续关闭、HCE钱包保留；未清身份或配置。

- 2026-09-13 NFC继续：按用户“测试过程关闭Mi Pay”要求，Mi10已再次临时设为MiPay disabled-user、使用HCE钱包，测试期间保持此状态；真实页面确认傻妞前台，NFC on、screen ON_UNLOCKED，未改卡片或认领数据。证据 `feature-acceptance434-phone-test-mode.json`、`feature-acceptance434-phone-current.xml`、`feature-acceptance434-phone-nfc-flags.json`。仅在已有显式bknfc hce入口增加终止阶段/errno，背景worker不打印，不导出UID/locator/APDU；RPC19例及现有RF释放1例通过。增量构建仅重编bk7258_nfc_service.c，434使用既定身份验签通过，包SHA256 `3ed5059a0550ac8e24603fa962a48f02638a33293e633f8225a0dae13c5e6e1e`，见 `feature-acceptance434-{nfc-stage-build,release,trust}.log`、`feature-acceptance434-ready.json`。433原包保持不变；434含此前OTA目标状态修复和本次NFC阶段日志，尚未安装，NFC根因及修复仍未实板确认。板端已完成432事务记录的保留改名尚未授权/执行；未清数据、未刷机、未提交推送。下一步获该限定设备操作授权后安装434，按终止阶段选择唯一最小协议修复。

- NFC最新对照（用户授权先关闭Mi Pay）：force-stop后约3秒被系统重新拉起；Android拒绝单组件状态更改（没有生效），随后按系统支持的整包接口临时停用Mi Pay，傻妞前台稳定。原默认小米钱包下，真实HCE窗口+现有单次bknfc hce返回rpc0/operation=-93；代码对应SAK未满足ISO-DEP激活要求，未导出UID。随后记录原设置并临时选择系统“使用HCE钱包”，同一已贴近天线、真实前台App条件返回rpc0/operation=-110，仍未观察到locator交接；不将错误码变化当作RATS或APDU某一阶段通过，也不宣称硬件损坏。证据 `feature-acceptance432-nfc-mipay-disabled.*`、`feature-acceptance432-nfc-hce-wallet.*`。原钱包选择和Mi Pay默认启用状态已恢复，见 `feature-acceptance432-wallet-restore.json`、`feature-acceptance432-wallet-restored.xml`、`feature-acceptance432-mipay-app-state.json`。已排除前台切换作为剩余失败的唯一原因；下一定位需区分UID选择、RATS/ATS、SELECT AID、locator APDU各阶段，现有单错误码无法唯一判断。不清除手机卡片/身份/配置，433固件部署授权仍独立等待，不把关闭Mi Pay授权扩展到OTA事务处置。

- 本轮设备暂停边界：433包已复制到真实Mi10的 `/sdcard/Download/shaniu-18.6.355-433-ota.bkpack` 并核对同一SHA256（`feature-acceptance433-phone-input.json`）；`make test-voice-ota-cancel run-voice-ota-store`通过（`feature-acceptance433-ota-adjacent-host-v2.log`）。为继续真实App433，已明确请求仅将已confirmed432的OTA事务记录改名保留为confirmed-432.bin并正常重启；未执行该新记录处置，未安装433。现有取消保护不允许直接清除已提交intent，不绕过；没有新增整片读回/格式化/换钥/提交/推送。
- NFC本轮实测：432上真实Mi10开启前台HCE60秒，未见locator交接，按超时回到可重试状态，证据 `feature-acceptance432-real-nfc-window.*`。随后用户确认天线已贴手机背面，前台实际是Mi Pay；一次明确启动MainActivity后约3秒又变为Mi Pay DoubleClickActivity，证据 `feature-acceptance432-nfc-foreground-check.log`。两次后续导航因前台不是预期App而停止，均未发送bknfc hce，不将脚本入口失败算RF失败。已请求先暂时移开天线，以便先在傻妞App开启HCE再贴回；未修改默认支付/卡片设置，真实RF交接与同一BLE认证仍待验收。

- 433修复已准备：仅产品 `bk7258_voice_ota_flow.c` 改变重启后target判定，保留source重试/回退边界与版本、安全计数、有效状态校验。现有测试显式source/current generation都为1，HEAD旧flow断言失败、补丁PASS（`feature-acceptance433-flow-host.log`）；原用例保留，根代理修正了初次新增用例仍为1/7而未覆盖缺陷的问题。既有构建入口增量通过（`feature-acceptance433-flow-build-v2.log`），只AP负载变化，CP、BL1、BL2、布局、受信公钥与floor426均未变化。沿用现有受控应用签名身份生成433 OTA包，验签通过，SHA256 `72a1a98aa5b3eba75df7780a83c4948ecb64f4cfef5c5986ccb95a5e11d7e081`，见 `feature-acceptance433-ota/release.json`、`feature-acceptance433-trust.log`；尚未安装。当前432的已提交intent被原有取消保护保留，新包不能直接越过旧flow的ESTALE；不改记录伪造结果，不新增全量刷机。后续仅需明确处理已确认432事务或限定部署修复的操作授权，既有429专属授权不自动复用到432记录。

- 2026-09-13实质检查点：432第二轮从真实Mi10 App发起，CP/AP源校验、写入、写后校验、自动重启完成；COM8确认B432 confirmed、manager idle/error0、supervisor faults0/recoveries0，A431保留。传输期间未BLE重认证，手机HTTPS在120s控制会话结束后持续服务；越过此前AP393216及1114112字节的-110位置，本轮没有-110。证据 `feature-acceptance432-phone-ota-v2.{raw,json,png}`及server.log。重启后App真实认证成功、INFO回读432，但OTA_STATUS=-116，因此App最终确认仍未通过，见 `feature-acceptance432-version-confirmation.*`。根因已由源码与新旧启动日志定位：AP boot generation两次均为1，产品flow把其相等错误当作没有整机重启，从而拒绝精确匹配目标432的confirmed pair。最小修复仅在产品flow依据不同于source的精确target version+counter及可信pair状态确认，不改变启动/签名/计数机制；补充现有同代数回归后增量构建。新版本配置加载configured1/cloud ready1，证据 `feature-acceptance432-config-status.raw`；未据此宣称执行过新云请求。

- 432真实App第一轮（431上）已认证、OTA_START接受，但source-open阶段返回-101，phase0/progress0，未写镜像，431保持confirmed；手机来源只有listener、没有TCP accept。证据 `feature-acceptance432-phone-ota-v1.*`。随后bkwifi status成功回读 `.102/24`，手机 `.101/24`；代码确认该命令同时同步AP native lease，不能称纯只读或倒推先前路由已就绪。真实App重连显示设备Wi-Fi已连接，证据 `feature-acceptance431-wifi-before-retry.raw`、`feature-acceptance431-phone-reconnected-status.{xml,png}`。利用这一已改变网络状态，重试同一432，未清事务/重签；不把-101归因于原-110，也不提前宣称30s调整有效。

- 2026-09-13最新：物理复位后431限定A槽下载完成，实际擦写仅CP `0x11000+0x154000`、AP `0x165000+0x121000`，未写B、启动链或数据。新鲜COM8确认 `active=A`、`pair=confirmed version=18.6.355+431 counter=431`、supervisor faults0/recoveries0，cloud configured/ready均1；不将其等同于真实云请求通过。证据 `feature-acceptance431-after-reset-download/result.json`（Writing Flash OK）、`feature-acceptance431-boot.{raw,json}`。失败429记录已保留为failed-429.bin；431的30s读等待首次在当前板运行，下一步使用已准备且哈希不变的432执行真实Mi10 App OTA，不重编/重签/重复下载431。

- 2026-09-13物理复位后纠正：新鲜COM8确认B428健康，OTA目录仅failed-429.bin，没有活动config.bin。复核上一轮原始日志可见mv已执行，故此前“事务未改动”的说明错误：异常发生在改名之后的第二次ls -l复查。保留原始失败日志，未重复改名；`feature-acceptance431-after-reset-transaction.{raw,json}`已记录实际状态。上次HIL仍确为握手阶段失败/未擦写；此次在物理复位后的正常B428上，按同一授权和同一431字节启动A槽限定下载，结果另记。
- 2026-09-13独立准备完成：复用431构建产物和当前签名身份生成后续App验收432，未重编启动链/CP/AP；包SHA256 `837b3858e24968877bfe4a8c749f8e1abaf6063e9796e228f51f8c37f9ad5986`，公钥指纹与目标一致，签名校验通过，Mi10 `/sdcard/Download/shaniu-18.6.355-432-ota.bkpack`回读哈希通过，未发起升级。证据`feature-acceptance432-ota/release.json`、`feature-acceptance432-trust.log`、`feature-acceptance432-phone-input.json`。getopt只读核查确认实际实现来自NuttX libc，正常有限argv不应形成数百次错误；不把强制重置optind当作已证实修复，不改官方checkout。T2委派沿用gpt-5.6-terra请求（运行时路由未核实）；设备由根代理独占，当前已释放COM8。剩余部署等待已经请求的一次物理复位，原授权有效；恢复后先用无选项目录查询确认，仅将失败429记录改名保留，再使用已验签431和新证据目录执行同一A槽下载，随后真实App432验收。
- 2026-09-13授权执行检查点：用户已明确授权仅处理失败429事务并写非活动A的431。新鲜COM8确认仍B428、manager FAILED/-110、faults0/recoveries0；Mi10在线，App已退后台释放控制/HTTPS。事务目录仅132字节config.bin，独立网络/云config.bin仍1269字节；431当次package/trust及A槽分段哈希/范围检查通过。随后重复ls -l查询出现连续argument invalid；后续原始日志复核确认mv已经执行，错误发生在改名后的复查，旧事务已改名保留。Ctrl-C/echo无回显，一次HIL软件复位接管在GetBus阶段失败，没有擦写、当时431未安装。已请求一次完全断电再上电，授权保持有效；不重试失去响应的串口或新增备份。证据`feature-acceptance431-authorized-prewrite.raw`、`feature-acceptance431-transaction-retired.raw`（含改名执行及随后查询失败）、`feature-acceptance431-console-query-recovery.raw`、`feature-acceptance431-bounded-download/result.json`。待物理复位后继续同一事务处理与同一431产物，不重编/重签。
- 最新检查点：不并发BLE重认证的同429对照轮仍在AP写入393216/1183744处返回-110，B428 confirmed、supervisor faults/recoveries均0；排除“必须由重认证触发”的解释，未证明唯一超时来源。证据`feature-acceptance429-no-reauth-v5.{raw,json,png}`及server.log；手机后续重新认证取得未完成提示，见`feature-acceptance429-failed-reconnect.*`。最终本轮APK SHA256 `a2e6d424be207927d8fac2e9af99a6440b8c92f89582a07cde89aacdd950873c`，清除新接受事务的旧otaStatus，真实页面已显示当前下载状态；对应JVM用例/assembleDebug通过，证据`feature-acceptance430-app-fresh-status.log`。
- 431候选仅增加AIDK CP `CONFIG_BK7258_OTA_RPMSG_READ_TIMEOUT_MS=30000`，保留其他板默认、HTTP/TLS/取消保护及原rollback_floor426；同时包含先前owner窗口修复。目标构建通过，实际CP ELF的等待参数为3000 ticks（10ms/tick），证据`feature-acceptance431-source-budget-build.log`、`feature-acceptance431-cp-read-disassembly.txt`。复用当前签名身份，包/公钥验签通过；包SHA256 `611d71e2bb7608413c049d9ce90326fddb85a6b6523cd81cea214ce57d7f09fb`，证据`feature-acceptance431-{package,trust}-verify.log`、`feature-acceptance431-ota/release.json`。未部署，不能据此认定-110修复。
- 设备写入停止点：429仍有未完成OTA intent，当前source428/target429。既有flow明确拒绝不匹配的431；不能删除认证校验、伪装429版本或直接装431后宣称事务成功。下一最小操作需单独确认“仅处理失败429的voice-ota事务记录，并限定写当前非活动A的CP/AP”，保留B428、启动链、认领身份、网络/云配置、SD和校准。拟写范围`0x11000..0x286000`；已校验具体431分段，见`feature-acceptance431-ota/prepared-bounded-inputs.json`，明确标为待授权/未部署。此前授权的B428写入已完成，不把其范围自动扩展到此操作。语音/图片真实入口仍缺有效官方模型，NFC仍待已请求的物理贴近；本轮不提交或推送。
- App来源生命周期修复已实机取得阶段证据：APK `8d898e91ca8936af7288b28e49e7c80653b7b755310412d745116255ed17af42`保留认领数据安装Mi10，补齐OTA准备阶段与directPoll的竞争；429恢复被接纳，BLE到期断开后HTTPS继续，写入越过旧262144字节中断点，认证重连后App显示实时下载进度、无错误。证据`feature-acceptance429-retained-source-v4.*`、`feature-acceptance429-retained-observation.*`及`feature-acceptance429-app-poll-race.log`。此轮随后在AP镜像1114112/1183744字节处返回-110；手机来源无TLS/request异常记录，B428保持confirmed。已确认CP每次跨核READ仅等5000ms，而AP缓存未命中的HTTP连接/IO各允许15000ms，存在时限不匹配，但尚不能唯一归因本次超时；下一轮仅去掉传输中的BLE重新认证作为对照，不清除事务。尚未完成重启/版本确认。430首次请求因设备保留未完成429事务返回-16，按既有保护恢复同一429，不清除intent；430包和owner窗口补丁待429完成后部署。来源修复前APK与430拒绝证据保留于`feature-acceptance430-app-source-lifetime.log`、`feature-acceptance430-app-ota.*`。
- 同包定向复测取得更深实板证据：APK `7f49bf751c0f6d50018ef3c1abef3b5cf05f35830aaa2b076e139fa4fa31fbca`仅补固定来源阶段日志。实际手机192.168.0.101监听38681，首次TCP/TLS均通过，manager先完成CP/AP源读取，再进入phase3、image1并写到262144/1183744；BLE在约120秒关闭，MainActivity.closeDirect同步关闭HTTPS源，后续读取返回-111。证据`feature-acceptance429-source-stages.{raw,json,xml,png}`及`feature-acceptance429-source-stages-server.log`。B428仍confirmed；A槽已有本次OTA写入，不能再声称A426仍是完整恢复基底。上一轮-110根因尚未定，但本轮已证实网络/TLS可达及“BLE关闭误杀Wi-Fi传输”的独立阻断。下一修改仅保留已接受、前台、仍在有效期内的来源服务，并通过原BLE认证重连回读；不放宽TLS/验签。
- control窗口补丁已完成目标增量构建，只编译provision_owner.c对应AP对象，rollback_floor仍426；尚未部署。证据`feature-acceptance430-control-window-build.log`。
- 428手机实测推进：同一Mi10选429（SHA256 `92dbcd1eba19e1499c856b89e80472015d6c1661a4d767b5e2559680a4379ebd`），原OTA_START=-16已消失，来源被接受，App显示下载1%；随后manager在source-open阶段返回-110，phase0/progress0，未进入镜像写入，428仍confirmed。证据`feature-acceptance429-fresh-connection-ota.{raw,json,xml,png}`。这证明OTA接纳修复有效，不代表HTTP传输/升级成功；当前仅定位该超时。
- 同次连接仍提前断开：手机日志21:31:07和21:33:12的status19间隔约125秒，与control广播120秒+重试5秒吻合；owner按广告起点终止已连接TLS是明确代码缺陷。最小补丁仅把接受首个control generation后的窗口起点改为TLS实际开始，保留120秒会话、30秒握手和认证限时；既有provision_owner/TLS定向测试通过，尚未构建/安装。首次两轮只回读/连接，没有发出OTA请求，不能重复计数升级。证据`feature-acceptance429-control-logcat.log`、`feature-acceptance429-phone-ota.*`、`feature-acceptance429-app-start.*`。
- 新授权检查点：已补齐现有HIL的AIDK signed-segments支持，要求显式4KiB对齐物理授权范围及每段SHA256，保留软件reset/禁止RTS策略；既有15项主机测试通过。技能目录实际链接仓库`tools/bk7258-hil-download/`，五个文件改动保留本地，未提交。原件和候选保存在`feature-acceptance428-hil-original/`、`feature-acceptance428-hil-candidate/`。
- 428实际限定下载通过：仅B槽`0x286000..0x4fb000`，签名/末端哈希通过，39秒，Loader五个成功标记齐全；A426、启动链、内部/data、SD与校准未纳入写入范围。证据`feature-acceptance428-bounded-{trust,preflight}.json`及`feature-acceptance428-bounded-download/result.json`（trust内容为工具文本）。新鲜COM8确认B428、trial confirmed、AP/CPU2/RPTUN健康、faults0/recoveries0、manager idle、cloud ready1、config.bin1269字节；证据`feature-acceptance428-bounded-boot.raw`。配置元数据及联网恢复通过，不声称对/data做过逐字节比较。此下载用于安装修复，不能替代App OTA验收；下一步使用同负载/同身份429包由真实手机发起升级。
- 官方合并基线cae8d99d，仍运行426；真实Mi10选择同签名身份427包并点击“从手机开始升级”。首轮旧提示仅报中断；第二轮增加分阶段提示后明确收到OTA_START=-16，来源记录已传完，manager仍IDLE且未写入。证据`feature-acceptance427-progress-retest.{json,xml,png,raw}`，原始证据统一在`out/shaniu-product-acceptance-20260910/`。本次APK `5f1b2f35…`，不把GUI采集的短暂空XML认作App崩溃。
- 已确认软件错误：云端ready会初始化空闲PTT会话并置capture_ready，OTA把该标记误当活动采集。新增`bkvoice_ptt_quiescent()`检查线程、turn和MIC/DAC/采集源/发送残留，仅替换direct OTA_START的该项判断；其余身份、网络、任务忙及验签门槛保留。`run-voice-ptt`、`run-voice-capture`、`run-cloud-runtime`通过；目标增量构建通过，AP原始镜像变为`5c4778ae…`，CP/BL1/BL2摘要未变，rollback_floor仍426。尚未用新固件证明实板OTA修复。
- App补齐来源传输阶段和字节进度；终止拒绝时关闭本地HTTPS源并断开该控制会话，避免下一次BEGIN撞上板端保留的旧来源记录。错误保留具体阶段/errno并提示重新连接，未绕过认证/VERIFY。定向OtaControlUpload JVM用例和assembleDebug通过；最新APK `788a0515…`的实机结果见本节后续记录，不用较早APK证据代替。
- 428候选修复包已复用既定受信签名身份；使用现有SD文件OTA作为修复部署的准备路径，目录`/ota/shaniu428-20260912`，不格式化、不清/data、不更换启动链。文件OTA部署不算手机OTA验收；部署成功后回到真实App升级、重连、版本及配置回读。最终状态以后续记录为准。
- 部署实际结果：428 SD成员文件fsync及回读SHA通过（包SHA256 `a4c73a5b16c311b43e11726405544b2295dfbe6df857a800b6774e601c51fecf`，记录`feature-acceptance428-sd-file-copy.json`）。Windows图形弹出未生效，后续标准锁卷/卸载均成功；卸载后的额外FlushFileBuffers返回设备未就绪，不计为文件写失败。恢复CDC后file OTA收到请求，但manager state7/error=-20，phase0、progress0/0，未进入升级槽写入；正常软件重启确认426后，同一请求仍返回-20，停止重试。证据`feature-acceptance428-file-ota-stage.raw`、`feature-acceptance426-storage-restart.log`、`feature-acceptance428-file-ota-after-restart.raw`。目录/挂载原因尚未定位，不能归咎签名或硬件。
- 备用非活动槽部署仅完成候选范围核对：CP B `0x286000+0x154000`、AP B `0x3da000+0x121000`，来自当次运行槽信息及相同布局；已提取428签名镜像。现有HIL AIDK profile硬限制single/8MiB，`--allow-profile-override`不能放行multi，preflight退出2（未打开COM/擦写），故没有执行该下载或改动工具规则；已一次性请求补齐AIDK受限分段能力及本次B槽写入授权，等待回复。当前真实设备仍426，428不算实板通过；后续须先解决受限部署能力或file OTA目录错误，不能用全量旧基底覆盖当前/data。
- 最新App APK `788a0515870c24c23dcb0d1f3416a7cb6a713274c6e3d002aa0416ec9447c12d`已保留数据安装到Mi10。真实重复操作明确看到“设备拒绝升级开始请求（-16）…已断开设备连接，请重新连接后重试”，重连按钮enabled；证据`feature-acceptance427-rejection-recovery.{json,xml,png,raw}`。这是设备真实拒绝后的手机恢复验收，不是模拟响应，也不是OTA成功。 实际点击重连后已重新认证并回读426；首次INFO截图中开始按钮在视口外，不将采集器false字段解释为禁用。后续滚动截图已显示“连接设备后读取/连接设备读取版本”，说明控制连接随后再次关闭；按钮visible=True、enabled=False符合断连限制，不能据此声称持续连接或重试发起升级通过。未再次开始升级。证据`feature-acceptance427-after-rejection-reconnect.*`、`feature-acceptance427-after-rejection-retry-button.*`。
- 摄像头独立实测：426运行现有`bkvision snapshot`，GC2145真实640×480 JPEG，17439字节，SOI/EOI正常、v4l2_error=no，屏幕thinking→happy→neutral；`privacy=metadata-only storage=disabled`，未保存或上传图像。证据`feature-acceptance426-camera-component.raw`。该命令未经过SD存储，不能证明SD挂载正常，也不能替代官方唤醒后的图片理解闭环。
- 委派指定gpt-5.6-terra完成局部实现和既有定向测试，实际模型路由未独立核实；根代理负责资源语义、构建、签名及独占实板/手机验收。未提交或推送。

## 2026-09-12 语音、NFC、图片与App OTA验收启动

- 新鲜COM8确认426 confirmed、AP/CPU2/RPTUN健康、faults0/recoveries0，配置与cloud ready=1仍有效；Mi10在线、Android13/API33、已安装APK SHA256 `4913387a927a940b315f71deb67e03af49f93e26412338a940f345a9207f38a9`，NFC开启且解锁。证据`feature-acceptance426-board-start.raw`、`feature-acceptance426-phone-start.json`，统一位于`out/shaniu-product-acceptance-20260910/`。
- 语音日常入口未就绪：当前有效AP配置未启用VOICE_KWS/VOICE_WAKE_RUNTIME，既有9条合成管线fixture不是有效官方唤醒模型。本轮不以PTT或注入替代“你好，openvela”验收，不自行启动训练。图片理解的明确语音匹配→当次JPEG→云请求→TTS源码和VISION配置存在，但真实语音入口仍依赖此模型缺口，尚未验收。
- NFC历史U19的“无ISO-DEP/APDU路径”已过时：现有源与配置具备MFRC522 UART1、CL_ISODEP、Android前台HCE、临时locator交接及同一BLE认证入口；自动RF轮询由AP NFC worker在未连接的授权广告窗口内执行，无需NSH注入。真实App已进入“碰一碰查找”，前台HCE成功启用（`feature-acceptance426-nfc-armed.xml/.png`）；尚未见实际RF交接，需要手机背部NFC区与AIDK天线物理贴近，已一次请求该操作。软件入口就绪不等于NFC射频通过。
- App OTA准备：现有426构建清单重哈希通过，直接复用已验证CP/AP负载，不重编/重签BL1/BL2。通过既有release ota入口及受控应用身份签发427（保持现有generation到安全计数映射，编译下限426）。`feature-acceptance427-ota/release.json`绑定18.6.355+427、AIDK、公钥指纹4979ece7…；包2581352字节，SHA256 `c66bd6885647c92f8744a93d1a553737e85f7e0a611877bd7a6ee91cc4cd9d5c`，只含manifest/catalog/签名及CP/AP。已有签名/包校验通过；复制手机独立文件并复核哈希通过（`feature-acceptance427-phone-package.json`），手机和板端同一Wi-Fi网段。实际App选包/发起结果见上方OTA拒绝定位检查点，未完成升级。

## 2026-09-12 配置恢复修复提交检查点

- 本批包括App本地校验、失败提示/输入保留/生命周期恢复及现有模拟器用例，云请求工作区按需分配，固定阶段错误记录和控制关闭原因。保留426真实配置、正常重启恢复证据，以及约89秒GATT断开未解决的限制；不作为全产品验收完成。
- 复用本轮已执行的Android单元/模拟器验收、cloud-http/cloud-request、426增量构建及包校验、真实手机/实板记录。提交前检查精确差异、敏感内容及diff格式；没有为提交重刷、重签、clean或重跑无变化验收。
- 目标为个人仓`fork/feat/shaniu-config-recovery`，直接基于最新官方基线，保持Rebase and merge所需线性历史。原始日志、三份未跟踪SDIO探针、固件/设备资料、密钥和凭据保留本地，不纳入Git；远端提交SHA以推送后核对结果为准。

## 2026-09-12 426内存修复，真实手机配置及重启恢复通过

- 实际源码为`d3350c8f6fff9ecd081fa5290d121b523a5c8268`加当前未提交差异，目标AIDK/aidk_ai_toy；保留原Android修改及无关本地成果。`bk7258_cloud_client.h/.c`移除每个client固定65536字节request数组，使用有界临时JSON缓冲：从256字节扩展到原65536字节上限，在同步HTTP消费后及错误路径清零整个已分配容量并释放。保留response32768、认证、TLS、协议及资源所有权；未改SDK/NuttX或增加堆配置。425同次实板日志已确定具体失败分配点，426同条件真实提交成功证明本次配置路径的修复有效，不代表所有音频/并发负载已经验收。
- 定向验证：现有`make -C tests/host/bk7258 run-cloud-http run-cloud-request`通过；HTTP测试仅移除已不存在的固定request成员断言，保留原HTTP、响应及凭据清理验证，没有新建测试框架。AIDK增量构建426通过，原签名身份签发；package/trust及HIL预检通过。构建/校验证据为`phone-joint-client426-build.log`、`phone-joint-client426-release.log`、`phone-joint-client426-check.log`；上述host命令结果保留于本轮执行记录。实现委派指定`gpt-5.6-terra`，实际模型路由未独立核实，硬件由根代理独占操作。
- 全量文件为`phone-joint-client426-full/flash/shaniu-bk7258-aidk_ai_toy-app__openvela_ap-v18.6.355+426-bcloud-request-full-20260912-01-full.bin`，8388608字节，SHA256 `2c159a6808ce2ba9298a9e133b3316d3a21b881f74269e5854641cd8b9b1156b`。COM8/2Mbps既有HIL入口约75秒完成，五项成功标记齐全，`phone-joint-client426-hil-run/result.json`为passed；新鲜启动确认426、CP/AP/CPU2正常。全量后按本次恢复授权初始化内部/data并`voice pairing --resume`恢复同一认领资料，再正常重启；未格式化SD NAND，未声称全量前/data原样保留。证据`phone-joint-client426-boot.raw`、`phone-joint-client426-data-init.raw`、`phone-joint-client426-identity-resume.log`、`phone-joint-client426-identity-reboot.log`。
- 真实手机Mi10，App `com.shaniu.companion` 0.5.0-a1/code5，APK SHA256 `347a0fd76897f9d4a9851f396736109a3b9a42ffcbf32c082710121809de23ed`。经实际页面扫描、认证、选Wi-Fi及提交已授权配置，页面显示“设置已保存／设备已确认保存连接设置。”，同步串口无client_alloc失败。`phone-joint-client426-config-phone.json`最初观察器漏匹配真实成功文字而记timeout，已追加原始结果更正，未删原记录；此timeout不是App或设备超时。证据`phone-joint-client426-config-uart.raw`及`phone-joint-client426-after-config.raw`显示configured=1、CLOUD ready=1、实际DHCP地址与1269字节0600配置文件。CLOUD ready由实际云预检成功设置；Gateway/WSS connected=0不等同直连云失败。
- 通过现有`deploy --reboot-only`正常重启426，`phone-joint-client426-config-reboot.log`确认启动；`phone-joint-client426-reboot-status.raw`确认配置1269字节仍在、configured=1/CLOUD ready=1和Wi-Fi地址恢复。App force-stop后正常启动、保留数据，首页“已保存认领结果”；实际扫描选中Shaniu设备后，10:45:34Z状态为“已连接傻妞／设备已连接Wi-Fi”。证据`phone-joint-client426-app-restart-home.xml`、`phone-joint-client426-control-home.json/.xml`。
- 后续截图`phone-joint-client426-connected-home.png`虽然文件名含connected，实际内容已经是连接断开，不能作为持续在线证据。首轮只读回连8.6秒连接、87.22秒观察到断开（`phone-joint-client426-session-window.json`）；不能仅因TLS有120秒上限就归因于它，同步UART筛选无关闭原因，手机处于Awake。进一步检查确认固件`bk7258_provision_owner.c`的控制WINDOW_MS还从广告窗口打开时起算120秒，早于TLS会话创建，关闭GATT使用0x13；这与后述主动断开一致，但未取得精确owner关闭原因，尚不能把这一候选计时原因当作已修复。尚未完成有效官方唤醒模型、真实自动语音/AEC打断、NFC、看图播报及App OTA；此前425文件OTA读包-2与本次配置修复分开保留。没有提交或推送。

- App最小改动：`AndroidProvisionGatt.kt`保留固定关闭原因、阶段、单调经过时间与GATT状态，每次关闭至多一条非取消错误日志，无载荷、设备ID、秘密或异常原文；`DeviceControlProtocol.kt`用类型保留控制应答超时；`MainActivity.kt`按已知原因显示可重试提示，不再一律要求靠近设备。原会话/握手/写入/控制超时和认证均未放宽。现有DeviceControlProtocolTest及增量assembleDebug通过；首次候选仅安装未测试，完整原因版本APK SHA256 `4913387a927a940b315f71deb67e03af49f93e26412338a940f345a9207f38a9`已用install -r安装，保留原数据，APK路径`android/shaniu-companion/app/build/outputs/apk/debug/app-debug.apk`。
- 更新App后的真实认证回连8.57秒成功、正常回读设备Wi-Fi状态（`phone-joint-client426-closecode-connected.png/.xml`），随后日志`transport_close reason=disconnected stage=tls elapsed_ms=89037 gatt_status=19`，页面93.61秒观察到断开并恢复“连接我的傻妞”按钮。证据`phone-joint-client426-closecode-phone.json`、`phone-joint-client426-closecode-android.log`、`phone-joint-client426-closecode-closed.png/.xml`；UART筛选仍无关闭原因。这证明本轮是GATT断开回调而非App的session_timeout/control_timeout分支，未证明精确板端关闭条件。配置提交证据属于前述APK347a0fd7，更新APK仅完成保留认领资料、真实回连、状态回读与准确失败提示验收，不能混写为重做了全部配网。下一限定问题是控制广告窗口与已连接会话的生命周期及到期恢复，复用现有owner/GATT路径定位，尚未改变固件426。

本检查点原始证据统一位于工作区`out/shaniu-product-acceptance-20260910/`；上述425记录保留为故障定位历史，不能代替426验收。

## 2026-09-12 425全量安装，实板定位VERIFY内存分配失败

- 用户再次明确要求直接全量下载、不新增备份。复用原持续签名身份及424同设备封存基底，425整包package/trust和HIL预检通过；operator SHA256 `2703dc86404e119d9e619f7032cc95fa2b4e0e80994d627b8e7d550a24eae120`，8MiB，尾64KiB与424一致。COM8按既有成功记录使用2Mbps，仅覆盖波特率，保留禁止RTS/DTR及loader原子软件复位。`phone-joint-verify425-hil-run/result.json`记录75秒、五项成功标记齐全。
- 新鲜串口确认18.6.355+425 confirmed，CP/AP/CPU2正常，supervisor faults0/recoveries0。全量后的内部/data普通挂载失败14，按本次整包恢复授权初始化内部1MiB并使用`voice pairing --resume`恢复同一claim-20260912身份627字节；正常软件重启再次confirmed。未格式化SD NAND、未生成/轮换身份，不将此描述为原样保留全量前/data。证据`phone-joint-verify425-boot.raw`、`phone-joint-verify425-data-init.raw`、`phone-joint-verify425-identity-resume.log`、`phone-joint-verify425-identity-reboot.log`。
- 同一Mi10/APK347a0fd7…重新认证扫描成功22条。首轮UI自动化错误地仅匹配显示文字而不是Wi-Fi按钮内容描述，未提交；修正定位后使用现有真实结果提交一次配置，全程同步COM8。Wi-Fi DHCP成功，新增日志明确`BKVOICE CLOUD probe stage=client_alloc ret=-12`，随后`PROVISION stage=service_ready ret=-12`，App同次失败。证据`phone-joint-verify425-config-uart.raw`、`phone-joint-verify425-config-phone.json`。这是具体失败分配点的实板证据；尚不能宣称修复通过。
- `bkcloud_client_s`固定request65536+response32768，使小型配置预检也申请约96KiB连续对象。下一最小修复将request改为按实际JSON大小临时分配，保留既有上限、协议、清理和认证；实现委派原执行代理，根代理负责集成及同条件实板验证。没有扩大到418恢复、Dolphin或新的OTA框架。未提交/推送。

## 2026-09-12 新认领身份实机通过，配置VERIFY -12复现

- 用户物理复位后，只读目录确认上次改名已生效：`identity-before-claim-20260912/config.bin`仍为691字节，新`identity/`为空；424正常。没有重复改名、格式化或刷机。证据 `phone-joint-after-physical-reset.raw`。
- 使用已生成且已授权的 `aidk-ai-toy-001/claim-20260912`，既有 `voice pairing --direct-cloud` 供应627字节成功；随后既有 `deploy --reboot-only --expected-version 18.6.355+424 --expected-counter 424` 正常重启并confirmed。证据 `phone-joint-new-identity-supply.log`、`phone-joint-new-identity-persist-reboot.log`。设备认领身份与固件签名身份独立，本轮未新建/轮换固件签名身份。
- 同一Mi10/App通过实际系统文件选择器导入对应四字段激活资料，显示“资料已就绪”；临时手机JSON已在finally删除并核实不存在，主机受控原件保留。搜索保留旧查询、异步结果及底部按钮边界导致的自动化定位失败已修正操作，不计作App产品故障或认证失败。最终证据 `phone-joint-activation-import-success.json`。
- 新身份经正常重启后，真实BLE认证Wi-Fi扫描返回可选网络，选中用户已授权网络。证据 `phone-joint-new-identity-wifi-scan.json`及脱敏XML。此结果证明新身份重启后可被真实手机认证，不代表配网已提交。
- 用真实App输入已授权Wi-Fi和既定MiMo Token Plan配置并点击保存；页面先“正在验证语音服务连接”→“认领进行中”，最终明确“设备验证配置失败（-12）”。未获得提交成功回执。失败后`bkvoice configured=0 connected=0`、`bkwifi status=0`且IP为0，新identity/config.bin为691字节，不能把查询status0当作联网成功。证据 `phone-joint-new-identity-configure-final.json`、`phone-joint-configure-result-ui.json`、`phone-joint-new-identity-after-config-failure.raw`；凭据不写入这些证据。
- 用户指出初轮没有同步串口采集属实；补做同次424真实App提交的连续COM8采集后，已证实Wi-Fi四次握手及DHCP ACK成功，约0.3秒后开始回收，App仍返回VERIFY -12。不能据此断言具体分配点或云服务根因。证据 `phone-joint-verify424-synchronized-uart.raw`、`phone-joint-verify424-synchronized-phone.json`；已脱敏，未记录秘密。
- 最小观察修改仅在`bk7258_provision_network.c`和`bk7258_cloud_runtime.c`添加固定stage/errno失败日志，保留原控制流。现有provision/cloud-runtime及实际network.c定向编译运行通过；增量构建只编译2个AP对象，CP/BL1/BL2原始摘要未变。证据 `phone-joint-verify-stage-host.log`、`phone-joint-verify-stage-build-summary.json`及同名前缀构建日志。实现指定`gpt-5.6-terra`，实际模型路由未独立核实。
- 已从实际424签发记录找回并核实持续签名引用；原先请求用户再次提供引用不再需要。MCUboot公钥指纹4979ece7…、BL1指纹58e384ae…与封存424匹配（`phone-joint-signing-reference-confirmed.json`）。复用应用身份生成425 OTA，不重新签发启动组件或生成身份。`phone-joint-verify425-ota/release.json`绑定18.6.355+425，包SHA256 e00e83a65f18e0c0e4a8cf498172a4c1bdc484c41c499c583c58e86f24f7e7b9；现有verify package和verify trust通过。
- 425尚未安装，424仍为实际运行版本。当前424 confirmed/manager idle健康核对后，经MSC将四个已验签文件独占写入新的`OTA425`目录、逐个读回哈希通过（`phone-joint-verify425-msc-copy.json`），Windows非强制卸载返回0并切回CDC。仅执行一次当前425 `bkota apply-file /mnt/sdnand/OTA425`，manager state7/phase0/progress0/0/error=-2（`phone-joint-verify425-file-apply.raw`），没有新槽安装/启动证据。当前包结构、FAT/LFN配置已核对，读包失败的具体位置仍未知；停止重复。未清空当前/data，也未回退新认领身份或旧身份留存目录。现有全量路径依赖历史基底，不能证明保留当前1MiB persistent_data；受控保存当前分区或授权原样恢复该区域属于后续数据保护待决项，不能冒充旧私钥缺失。观察包安装后返回VERIFY阶段定位；未提交/推送。

## 2026-09-12 真机联调及新认领身份检查点（供应前历史）

- 源码仍为 `d3350c8f6fff9ecd081fa5290d121b523a5c8268` + 上述本地Android修改；没有固件改动、重编、签发、刷机、提交或推送。Mi10 `59d707dc` 保留数据安装App `0.5.0-a1` / code5，APK SHA256 `347a0fd76897f9d4a9851f396736109a3b9a42ffcbf32c082710121809de23ed`；真实首页和扫描发现 `SHANIU · 7F:81`，未完成新身份导入或配置提交。
- COM8正常查询先确认424健康，`bkvoice` 为ready1/configured0/connected0；`/data/shaniu` 仅列identity及voice-ota目录，原identity/config.bin为691字节0600。证据仍在 `out/shaniu-product-acceptance-20260910/`：`phone-joint-board-status.log`、`phone-joint-before-identity.raw`、`phone-joint-main.xml`、`phone-joint-scan.xml`。
- 用户明确授权“直接新身份”。已在受控持久位置生成设备认领身份引用 `aidk-ai-toy-001/claim-20260912`（ECDSA P-256），证书SHA256 `63adae26bf768e10576563a78fa007f66d03252ffd92714e58b004d55d6e64cf`，目录0700/私钥0600；公钥匹配检查通过。只记录公开身份，未生成新的固件签名身份，未输出私钥。公开证据 `phone-joint-claim-public.json`；尚未生成/导入App激活资料或供应板端。
- 为保留旧身份，尝试将 `/data/shaniu/identity` 改名为 `/data/shaniu/identity-before-claim-20260912`，但串口只返回命令回显，没有成功/失败或后续目录结果。随后正常软件复位也没有取得健康版本标记。不能确认改名结果，也不能据此认定整板死机；已经停止身份供应及后续写入。证据 `phone-joint-identity-preserve.raw`、`phone-joint-new-identity-reboot.log`。
- 当前最小外部动作：AIDK物理复位或重新上电。恢复后先核对版本和上述两个目录的元数据，再决定是否使用已生成身份继续既有 `voice pairing`；不盲目重复改名、不重新生成身份、不格式化、不重刷。旧激活资料查找已随新身份授权收口。真实认证→配网→云配置→重启恢复仍未通过；已有VERIFY -12待该链路恢复后定位。

## 2026-09-12 Android模拟器专项验收（本阶段完成，实机待验）

- 范围仅 Android 模拟器，源码基线 `d3350c8f6fff9ecd081fa5290d121b523a5c8268` + 本地 Android 修改；未提交/推送。用户已报告手机重新接入，本轮没有操作手机、板卡、生产身份或云服务。
- 现有 `Medium_Phone_API_36.0` / `emulator-5554`，Android 16 / API 36；App `com.shaniu.companion`、`0.5.0-a1` / code 5。保留数据安装的 APK SHA256 `347a0fd76897f9d4a9851f396736109a3b9a42ffcbf32c082710121809de23ed` 与安装路径内的 APK 摘要一致。可安装文件：`android/shaniu-companion/app/build/outputs/apk/debug/app-debug.apk`。
- 以下证据均在工作区 `out/shaniu-product-acceptance-20260910/`，身份见 `emu-goal-final-identity.json`。

| 路径 | 实际结果与边界 | 证据 |
|---|---|---|
| A 启动/导航 | 真实冷启动、再次启动、首页/认领/心情/设置/OTA及返回；权限拒绝可重试，蓝牙关闭有提示，30秒无设备扫描会结束；OTA无设备不允许开始，系统选文件可取消。最终APK冷启动490ms，无空白页或阻断。 | `emu-goal-final-launch.log`；`emu-goal-final-home/ota/discovery.png`及XML；`emu-goal-permission-denied.xml`、`emu-goal-bluetooth-off.xml`、`emu-goal-no-device.xml`、`emu-goal-ota-cancel-return.xml` |
| B 表单 | 实际Save验证UTF-8 SSID/非hex 64字节密码、云必填及开放Wi-Fi空密码；缺项在DNS之前拒绝、输入保留、修改后同页重试；实际IME显示及键入、滚动后Save可点，错误完整可见。设备选择/激活前提使用合成fixture。 | `emu-goal-acceptance.log`；`emulator-flow-error.png`；原失败及修复见 `emu-goal-baseline-fixture.log`、`emu-goal-local-fix-fixture.log` |
| C 失败/取消/重试 | 受控DNS失败无认领会话；真实协议解码认证失败、VERIFY -12、超时；ACK仅处理中，失败返回后重新填写重试，最终type6才显示保存成功并提交隔离绑定；取消后晚回执不覆盖页面。 | `emu-goal-acceptance.log`；`emulator-flow-verify-failure.png`、`emulator-flow-committed.png` |
| D 生命周期/pending | 真实moveTaskToBack/onStop及同Activity恢复后可修改重试，晚解析结果不会覆盖；未提交密码/Key经实际recreate后旧/新实例均为空；APPLY后pending经recreate及独立进程重启保留，重新进入核对页面。仅测试命名空间prefs/Keystore，未清真实App数据。 | `emu-goal-acceptance.log`；`emu-goal-process-create-final.log`、`emu-goal-process-resume-final.log`；停止后pid为空的执行证据已记录 |

- 修复：`ProvisionSettings.kt`、`CloudSettings.kt`公开纯本地校验并由编码器复用；`ProvisionActivity.kt`在DNS前使用相同约束、本地失败保留输入、显示状态时收起IME并滚到提示、后台终止解析时清除处理中提示；`ProvisionClaimProtocol.kt`仅保留已验证错误帧的非敏感阶段/状态码，`ProvisioningConnection.kt`区分认证/配置验证/超时且不打印原始传输原因。未放宽认证/VERIFY/未知提交状态约束。
- 验证：`./gradlew :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest` 增量通过；143项单元用例中141执行通过、2项固件控制互操作按原条件跳过（不算本轮已验）。10项模拟器场景通过，本次crash buffer为空，验收窗口内App ANR事件为0。构建日志 `emu-goal-visible-recovery-build.log`、`emu-goal-acceptance-test-build.log`；结果 `emu-goal-acceptance.log`、`emu-goal-acceptance-crash.log`、`emu-goal-final-health.log`。最终绘制帧截图单项复查见 `emu-goal-capture-visible.log`（测试APK `1dc976e6…`），不重复其余未变场景。
- 复现入口：现有 `ControlKeyInstrumentation` 的 `-e emulator_flow_probe 1`；可选 `-e emulator_flow_case <精确既有场景名>` 只复测受影响场景，未知名称失败；跨进程入口 `-e emulator_process_fixture create` → `am force-stop com.shaniu.companion` → `-e emulator_process_fixture resume`。进程恢复证据使用测试APK `45257157…`；最终扩展用例使用 `34d435dd…`，同一产品APK、过程函数未变，完整摘要在身份记录。实际IME与页面生命周期属于系统行为；DNS/TLS端点与协议回执为注入响应；恢复页会尝试连接虚拟测试地址，不证明BLE射频或设备确认。安全页面截图由测试对合成字段的View.draw采集，FLAG_SECURE未关闭，不包含系统IME像素。
- 测试仅扩展现有 `DeviceUiAcceptance.kt`、`ControlKeyInstrumentation.kt`及3个相关单元测试文件；没有新增测试框架。定向实现委派指定 `gpt-5.6-terra`，实际模型路由未独立核实；根代理协调模拟器、集成与验收。
- 待后续实机：真实BLE/NFC认证认领、板端联网/云验证、真实VERIFY -12根因及OTA升级/重启保持；均未宣称通过。本轮不自动进入这些工作。早期测试APK与App不匹配、任务重排权限和截图时序故障属于测试工具问题，已分别修正，不计作产品端故障。

## 2026-09-12 开发分支提交检查点

- 本次发布为功能开发检查点，范围包括认证通道内有界Wi-Fi扫描、CCC/ATT兼容补丁、App回调与本地输入保留、失败cloud回收，以及文件OTA源的只读挂载/占用生命周期。保留真实配置VERIFY -12和产品验收未闭环状态，不作为最终固件发布。
- 提交前定向回归：media-volume、cloud-runtime、provision GATT、patched GATT/CCC/MTU、provision TLS及verify layers通过；证据out/shaniu-product-acceptance-20260910/publication-*.log。diff检查通过。复用424构建/启动与相关功能证据，没有为推送重刷或全量clean。
- 清理后Android testDebugUnitTest及assembleDebug通过，APK SHA256 6b28f4a643bca503211860d6a65b462124cd2f4943eb8b9391bfc8c42fcfb248；该包尚未安装实板配套手机。
- Android发布源码移除本轮临时GATT首写计数、token/长度及认领收发采集日志，保留实际回调、安全校验与错误原因。此前手机/模拟器日志对应其注明APK，不能冒充清理后新APK的实机验收。
- 历史logs/bk7258-shaniu-a1-com8及三个SDIO探针文件保留本地，不暂存；不发布云Key、激活资料、私钥、原始录音、原始串口日志或固件备份。官方dev-ai-contest-2026核对为a4d542cf，当前分支线性基于此提交，目标为fork/feat/shaniu-first-use-p0。

## 2026-09-12 明确端点授权后真实云接口测试通过

- 用户明确授权现有mimo凭据发往既定Token Plan chat/completions端点；第一次执行被审批拒绝未发送，具体端点授权后原请求获准。Key只在内存用于请求，不打印、不保存到日志或APK，不操作板端配置。
- 真实mimo-v2.5对话HTTP200、正文非空、finish_reason=stop；首轮max_tokens64没有正文，调整为1024后通过，不能将首轮原因直接认定为固件问题。mimo-v2.5-tts内置音色流式PCM145920字节、SSE DONE；按历史24kHz假设转为16kHz WAV后，mimo-v2.5-asr HTTP200、正文非空且匹配非敏感测试短句。音频源是本次TTS，不是麦克风；不证明真实唤醒/扬声器/采样率听感。
- 证据out/shaniu-product-acceptance-20260910/cloud-live-chat.json、cloud-live-audio.json，仅状态、长度、哈希。一次性采集对末尾audio=null的处理修正后完成，不修改产品代码或恢复Gateway。
- 模拟器完整既有ui_probe通过：本地pending恢复、证书pin、密钥借用清理、TLS分片/错误pin拒绝及合成状态UI。证据emulator-full-ui.log。主机cloud-config、cloud-request通过；真实webclient本地夹具通过（cloud-audio-host-network.log），voice-turn-audio通过。cloud-playback需要已展开SpeexDSP源码，当前未定位输入，标未验证，不为此下载或扩大构建。
- 仍未完成：真实云Key通过App BLE交付、板端VERIFY -12排除、真实收音/播放、正常唤醒；模拟器没有对应真实射频路径。后续主线回到424配置验证-12的最小定位。未提交、推送、签名、刷机。

## 2026-09-12 转入模拟器：表单失败保留与重复保存通过

- 按用户最新要求暂停真机导入/配置，使用现有 Medium_Phone_API_36.0（emulator-5554）。真实手机已不在ADB列表；中断后的临时激活副本清理命令返回device not found，故最终清理状态未核实，手机恢复后只核查并清理该临时副本。此前授权导入命令已获准执行，原“审批阻止”不再是当前结论；未取得导入成功证据。
- 复用APK 724e3563647cf3add33556f4dde4735686c2333c05269bdd5f8d165c1ab238a8，现有provision_input_probe在模拟器通过。随后仅扩展已有断言为连续两次点击保存，每次都验证缺Key提示、SSID/密码保留、connection=null、cloudResolving=false、generation=0；无DNS/BLE/认领/私钥操作。
- 全部现有Android单元测试通过（执行代理报告），重复点击测试增量构建通过（43项任务，39项up-to-date），模拟器PASS，git diff --check通过。测试APK SHA256 3512d6d27f68a5a13be1d7ad3ef4e6f0e70646a2b99abbb7fd9145b8e20a134f。证据：out/shaniu-product-acceptance-20260910/emulator-input-retain.log、emulator-input-repeat.log、emulator-repeat-build.log。
- 常规逻辑检查指定gpt-5.6-terra执行，实际模型路由未独立核实；未发现需要新增生产修改的缺陷。没有固件重编、签名、下载、提交或推送。
- 此结果仅证明本地表单行为；424真实VERIFY -12、认证配置成功/重启恢复、NFC及真实OTA仍未通过。下一可执行项：App配置失败状态与重试交互；不将模拟器结果替代射频和云端实板验收。

## 2026-09-12 自动化表单测试超时，身份导入被执行审批阻止

- 用户明确授权自动化测试及身份导入/密钥传输/配置发送。既有ControlKeyInstrumentation新增provision_input_probe独立早退分支，仅以固定非敏感文本点击真实Activity保存按钮，检查缺Key提示、输入保留、未启动DNS/BLE；不执行默认Keystore/绑定测试。测试APK 8c9bc846d9750a357df3afa7760f9de82a9385bee0cd09fc97722a70907eb941构建及安装通过。
- Mi10 am instrument -w在45秒内未返回，不能标UI自动化通过。当前App进程存在但没有取得测试结果；停止该进程并普通启动MainActivity，保留应用数据，不扩大测试框架调查。普通UI返回首页可用。
- 原激活资料重新导入在执行前被自动审批拒绝；用户再次明确具体授权后，以同一命令finally清理临时副本的直接导入仍被拒绝。没有复制激活文件、没有输入/发送配置，不换工具绕过。审批理由为未认可载荷/目的地授权，不能归因App/实板。真实配置仍为先前-12失败，424回收证据保留。下一步需执行环境允许已授权导入，或用户手动在App导入/填写后再采集协议结果；不再要求泛化重复授权。

## 2026-09-12 App本地失败保留输入修复已安装

- ProvisionActivity在DNS前检查SSID、语音服务Key等必要输入；开放Wi-Fi允许空密码。本地缺项/DNS/编码/连接构造失败保留当前页面字段，实际交接连接及页面销毁继续清理；未新增Wi-Fi独立提交协议。
- 既有ProvisionSettingsTest、CloudSettingsTest、ProvisionTlsTest及assembleDebug通过，diff检查通过。wifi-input-retain-app.apk SHA256 724e3563647cf3add33556f4dde4735686c2333c05269bdd5f8d165c1ab238a8，以adb install -r安装真实Mi10成功，保留App数据；手机端保留输入交互尚待复测，没有代填秘密。
- 固件仍424；本次不重刷。-12待定位：cloud probe client有约96KiB连续分配，也有cJSON分配返回ENOMEM，仅凭状态不能判断具体来源。不宣称内存根因已解决。未提交/推送。

## 2026-09-12 用户配置失败后回收通过，App输入清理缺陷修复中

- 用户手动填写并提交，wifi-full424-user-configure.log：08:31认证/上传status0，VERIFY先返回type5 status0，再返回type7 status=-12；未提交成功。-12为内存不足类返回，具体分配/调用位置待定位，不认定云服务本身失败。
- 失败后bkvoice configured=0，bkwifi status=0；wifi-full424-after-user-failure.raw及wifi-full424-after-user-wifi.raw证明本次失败后的cloud配置和Wi-Fi占用已经回收，不再保持423的configured1/持续-16。不能用此替代完整配网和云连接成功。
- 用户报告只填Wi-Fi点保存后密码消失。确定ProvisionActivity.connect的finally无条件清空字段，本地encode/必填校验失败也触发；最小改动为本地校验失败保留内存输入、准确提示云配置缺项，真正交接连接及销毁继续清理，不持久化或打印秘密。App修复及定向验证进行中，不改固件或协议。

## 2026-09-12 424整包下载启动、原身份恢复和手机扫描通过

- 当前用户明确授权整包不备份。full要求版本与编译floor相同，故沿原构建树将floor对齐424后生成整包；首个floor423打包请求被正确拒绝，没有写入。复用批准签名身份，没有生成密钥或新增备份。
- wifi-full424/release.json绑定operator SHA256 efa399c99957021e94feb47b5b4e862eaa3c0555440c3ebbae5e28409359b0c7，8MiB，尾64KiB与423同板基底一致。package/trust/preflight通过，wifi-full424-hil-run/result.json五个成功标记齐全、无失败标记。
- wifi-full424-boot.raw：18.6.355+424 confirmed、manager idle、CPU2 online、supervisor faults0/recoveries0。按本次恢复授权初始化刷入的内部/data，voice pairing --resume恢复同一原身份627字节；正常软件重启完成，wifi-full424-identity-persist.raw记录config.bin权限/大小。SD NAND未格式化，不能把恢复原身份说成保留全部实时配置。
- wifi-full424-phone-scan.log：09-12 08:19:04真实Mi10收到status0 count19 truncated=false，UI选择用户网络。云清理-EALREADY修复的配置失败/重试闭环未验收。
- 自动审批拒绝ADB填写Wi-Fi密码；引用既有授权及用户再次明确授权后仍被拒绝，所有该输入命令均未执行。不更换工具绕过；最小解除动作是用户在现有App手动填写Wi-Fi密码/云服务Key并点击保存，随后继续采集类型/状态日志。不打印凭据，不新增框架，未提交/推送。

## 2026-09-12 云探测失败清理修复通过构建，424未安装

- bk7258_cloud_runtime.c只将无PTT worker的-EALREADY视为已关闭，继续释放cloud；worker仍在则-EAGAIN保留对象。既有test_bk7258_cloud_runtime.c覆盖失败probe→clear保留/释放→重新创建连接；make run-cloud-runtime及diff检查通过。不移除网络busy/QUARANTINE保护。
- 现有AIDK构建树增量构建通过（wifi-cloud-cleanup-build.log），保持rollback-floor423和既定公钥，不clean、不生成身份。424 OTA由现有批准应用身份签署，package/trust均通过；未重签BL1/BL2。release.json位于out/shaniu-product-acceptance-20260910/wifi-cloud-cleanup424-ota，包SHA256 f6798172e46387156a96066ea5b93fc13ca1da39130c8f5957f835277253b88a；应用公钥指纹与已安装423相同。
- 只读bkota status确认423 confirmed、manager idle、supervisor faults0/recoveries0；usbmode=cdc。COM16真实存在，但一次既有deploy HELLO超时，未进入镜像传输。有效AP .config明确CONFIG_BK7258_OTA_SOURCE_USB未启用，不能把CDC枚举当OTA接收器；停止重复握手。证据wifi-cloud-cleanup424-deploy.log、wifi-full423-before-cleanup-fix.raw、wifi-cloud-cleanup-usbmode.raw。
- 424尚未安装，不能宣称实板-16已修复。HIL AIDK现有单8MiB路径须另行落实当前/data保留/原身份恢复边界，不能拿423空白基底当当前配置。旧文件OTA源失败没有新的通过证据；不转为无关USB框架整改，不生成整片备份、不清配置。未提交/推送。

## 2026-09-12 COM8恢复，配置失败回收路径定位

- 用户重新插拔后COM8五秒纯接收正常结束；首次命令含残留字符被NSH拒绝，正常提示符下重发成功。bkvoice status：AP ready=1 configured=1 connected=0 cloud ready=0 busy=0 last_error=0；bkwifi status返回-16，其零地址是错误响应，不作为真实网络地址证据。手机59d707dc仍为真实Mi_10 device。
- 证据：out/shaniu-product-acceptance-20260910/wifi-full423-replug-ready.raw、wifi-full423-replug-voice-status-clean.raw、wifi-full423-replug-wifi-status.raw。未重启、未重新配置设备。
- 确定源码缺陷：bkcloud_runtime_clear在初次云探测未打开capture会话时，把bkvoice_ptt_session_close的-EALREADY作为致命清理失败；网络ABORTING随后不断重试，不能释放trial。最小修复限定已关闭语义，保留worker join和其它错误保护；实板是否命中此分支仍待候选验证。常规实现交既有执行代理，实际模型路由未独立核实。

## 2026-09-12 App回调修复通过，配置校验忙及COM8访问阻塞

- Events.tick改为必须实现，ProvisioningConnection提供显式空回调；既有ProvisionTlsTest 6项、ProvisionClaimProtocolTest 7项及assembleDebug通过。安装修复后不再出现该AbstractMethodError，手机流程进入真实认证与配置上传。新增既有客户端的类型/序号/状态日志，不记录负载或凭据。
- wifi-full423-claim-reason.log：23:57认证type1及上传type2/type3均status0；VERIFY type4 seq4在23:57:17返回type7 status=-16，随后FAILED。未发送提交，不能标为认领配置闭环成功。wifi-full423-claim-reason-stage.log记录后续扫描-16。
- 源码核对网络试连在poll失效或回退失败时保留QUARANTINE及资源占用；这是候选路径，尚无首次失败阶段/errno证据，不删除busy或隔离保护。后续需正常控制台取得当前状态，再选择最小修复。
- COM8仍枚举存在；本轮启动的serial_capture进程59824限定匹配后Stop-Process -Force，回读仍存在。一次5秒纯接收在Open阶段直接访问被拒绝，未发送板端命令、未取得新的串口运行证据。需要解除宿主COM8占用；不将手机拔插或串口枚举视为端口可用证明。
- 证据沿用out/shaniu-product-acceptance-20260910：wifi-full423-tick-fix-configure.log、wifi-full423-claim-reason.log。源码/主机/App实测分开记录；未改固件、未签发/刷机、未提交或推送。

## 2026-09-11 手机恢复，423扫描/重连通过，App保存回调崩溃定位

- 用户重新插拔手机后59d707dc恢复device；复用原激活资料，临时副本删除exit0。23:37:39首次真实扫描status0 count14 truncated=false；23:38:54独立重连扫描status0 count11 truncated=false。MTU70、20字节前三写回调均status0。通过UI列表选中既定家庭网络，不手填SSID。
- 用用户已有Wi-Fi密码与mimo配置填写并点击保存后退到桌面。23:40:35 AndroidRuntime明确AbstractMethodError: AndroidProvisionGatt$Events.tick()，来自worker第248行；此轮没有新GATT连接回调，不宣称已认领或已提交配置。
- 证据：out/shaniu-product-acceptance-20260910/wifi-full423-phone-scan.log、wifi-full423-phone-reconnect.log、wifi-full423-phone-configure.log、wifi-full423-app-crash.log（仅保留当前崩溃；未记录凭据）。下一步仅修复App回调实现并安装重试，不重编/重签固件、不清空/data。未提交或推送。

## 2026-09-11 扫描所有权与CCC重连修复通过主机构建，423下载启动通过

- 422/正常MTU70的67字节首写无回调；同固件20字节对照重连返回ATT14。正常软件重启后20字节前三写均成功，真实CP扫描完成13条结果，但App收到-116。证据wifi-full422-write20-phone.log、wifi-full422-write20-fresh-phone.log及同名fresh-console.raw；不能把扫描底层成功算作App流程通过。
- 已修复bk7258_provision_scan.c：owner每轮drain不再抢先消费正常pair结果，close标记retired、drain只回收retired任务。既有test_provision_tls.py验证正常结果所有权及关闭回收通过。
- 已修复bk7258_provision_gatt.c：锁内保留CCC值，connected建立物理连接后重新计算就绪；覆盖CCC先到、迟到连接和已关闭窗口。既有test_provision_gatt.py通过。没有放宽身份/窗口校验。
- App采用保守20字节写分片，保持MTU185协商用于接收；不是大包超时根因修复。6项ProvisionTlsTest及assembleDebug通过。当前手机安装正式兼容APK wifi-compatible20-app.apk，SHA256 4c77b3dbab295db7f4eb7e484199ee2125969130321622b4a9b8e40b22f2d6c1。
- 增量构建及423全量包校验通过，沿用已安装两层公钥身份，无新私钥、无整片备份。wifi-full423/release.json绑定18.6.355+423，operator SHA256 ed78b895ff6295f657455e8a6a9d5c83a10cdd38b7cc2b6b62e23d0706253bda，8MiB、尾64KiB与422同板基底相同。HIL五项成功标记齐全；423启动confirmed、CPU2 online，supervisor faults=0/recoveries=0。功能回归仍未通过。
- 正式APK安装后准备重新导入激活资料，首次UI读取即为空，未复制激活文件；随后ADB get-state返回device 59d707dc not found。手机真实验收转为外部阻塞，不把此前422手机结果替代423。板端下载/恢复继续。
- 423更新后/data初始化成功，既有voice pairing --resume恢复原身份（exit0）；正常重启后config.bin仍691字节0600。证据wifi-full423-data-init.raw、wifi-full423-identity-resume.log、wifi-full423-identity-reboot.raw。不生成或轮换身份，不把已格式化内部/data说成保留原实时数据。
- 用户确认手机物理连接仍在。Windows设备枚举：小米10历史VID_18D1/PID_4EE9/59D707DC实例与当前VID_0000/PID_0003异常实例具有相同父Hub和Port_#0001.Hub_#0008；usbipd未显示手机被转交。仅该异常实例pnputil restart被Windows拒绝访问（不能以退出码0判通过），日志phone-usb-instance-restart.log。没有重启Hub或改动其它设备；手机临时激活文件在本次失败前未复制。该环境问题阻塞423真实App回归，不归因于BLE固件。
- 更新前wifi-before423-status.raw确认422健康，/data/shaniu下只有identity、voice-ota目录，身份config.bin691字节；本轮未提交Wi-Fi/云配置。沿用已授权全量更新后原身份恢复；SD NAND不动。后续先确认下载/启动、resume原身份，再手机扫描及断线重连。未提交或推送。

## 2026-09-11 422身份恢复、手机常亮通过，首笔写入仍超时

- /data实际已挂载：获授权的forceformat命令返回mount failed:20，随后mount列出/data littlefs；没有重复强制清空。既有voice pairing --resume返回identity-supplied；正常重启后422 confirmed、supervisor faults=0/recoveries=0，身份config.bin为691字节0600。
- 小米10 59d707dc已恢复ADB授权，保持现有App和已导入激活资料。stay_on_while_plugged_in从2设为7，screen_off_timeout从600000设为2147483647，回读mStayOn=true、Awake、powered；未修改锁屏凭据。
- 真实UI扫描并选择SHANIU 7F:81，进入Wi-Fi页后请求扫描。22:58:16 GATT连接status0、MTU回调status0/70、首笔67字节accepted；22:58:21 write_timeout，无对应写回调。MTU补丁已实际运行，但不能据此宣称首笔写入问题解决；下一步限于接收/应答路径。
- 证据均在out/shaniu-product-acceptance-20260910：phone-keep-awake.json、wifi-full422-data-init-resume.raw、wifi-full422-identity-resume.log、wifi-full422-identity-reboot.raw、wifi-full422-phone-scan.log。未重编、重签或重刷，未提交/推送；真实认领配置和Wi-Fi扫描仍未验收。

## 2026-09-11 内部初始化已明确授权，串口及手机USB授权阻塞

- 用户明确“授权”对应内部/dev/mtdblock0（1MiB /data）初始化后恢复原身份，SD NAND不动；不再重复询问该权限。串口工具尚在Open(COM8)阶段即失败“函数不正确”，初始化没有执行。
- Windows可枚举COM8/COM3/COM4/COM16，COM8 CH340 PnP状态OK但打开失败。按原串口恢复授权，只对COM8对应CH340实例执行pnputil /restart-device，Windows返回拒绝访问；不以退出码0误判恢复成功，不重启其它串口或USB控制器。
- 用户要求手机停止自动锁屏。尝试先读取原设置即遇ADB offline，尚未写设置；仅重连59d707dc后状态变为unauthorized，需要手机确认该电脑USB调试授权。未更改锁屏凭据、未重置ADB密钥。
- 常亮方案仍为充电连接期间stayon、延长screen_off_timeout，恢复ADB授权后保存原值并实施/回读。当前最小动作：手机勾选始终允许并允许USB调试；COM8需要重新插拔连接或在有管理员权限的Windows中恢复该CH340驱动。原内部初始化授权继续有效，恢复后从该步执行，无需重编/重签/重刷422。
- 证据：wifi-full422-console-recheck（工具错误）、phone-keep-awake-block.json；保持422下载/启动通过，身份恢复/新BLE功能未验收。

## 2026-09-11 422下载与启动通过，内部配置初始化被审批阻止

- wifi-full422-hil-run/result.json五项loader成功标记齐全、无失败标记；loader退出码1仅为其既有附加状态，工具结果passed。新启动wifi-full422-boot.raw确认18.6.355+422、counter422、pair confirmed、CPU2 ready/online、manager idle，supervisor faults=0 recoveries=0。
- 全量更新后/data未挂载，ls /data/shaniu/identity返回ENOENT。原身份仍保留在此前受控主机位置，本轮未重新生成/轮换。
- 尝试按先前恢复流程执行mount -t littlefs -o forceformat /dev/mtdblock0 /data，被自动审批在执行前拒绝；理由为内部1MiB配置/身份分区清除需要具体授权，不能仅沿用泛化格式化许可。命令未执行；不通过改命令、工具或默认格式化选项绕过。
- 当前唯一恢复前置：明确批准初始化内部/dev/mtdblock0（1MiB /data）并复用原认领身份恢复，SD NAND不动。获准后执行该初始化、既有voice pairing --resume、正常重启保留检查，再用当前手机App验收422 CCC/MTU与Wi-Fi扫描。不能用下载/启动PASS替代这些未完成项。
- 记录wifi-full422-restore-block.json。422源码/构建/签名/下载/启动已完成；身份恢复和BLE功能未验收。未提交或推送。

## 2026-09-11 连接恢复，422文件OTA受阻，授权整包下载中

- 实际ADB 59d707dc、COM8和SDNAND D:（FAT，126611456字节）恢复。422四文件与封存包逐字节一致，复制新目录shaniu-ble422后再次逐文件哈希通过。Shell弹出请求未卸载；改用Windows Dismount(Force=false,Permanent=false)返回0，再切CDC。
- 421 confirmed、manager idle核对后请求apply-file；首次与正常reboot对照均manager error=-2 progress=0/0。未绕过校验、未降低计数，停止重复请求。源读取阶段的具体失败位置仍未确认；不能把本次当成OTA验收通过。
- 只读列举/data/shaniu仅691字节identity/config.bin和空voice-ota目录；bkvoice configured=0。沿本次已有全量更新及恢复原身份授权，使用封存421同设备整包作基底；422 full构建/签名/package/trust/HIL预检通过，无新密钥或整片读回，校准尾64KiB与基底一致。它仍需恢复原认领身份，不宣称保留实时/data。
- 全量文件：wifi-full422/release.json，operator SHA256 d1b6f97b9866083bedae135cfa544daedfac7d2d048e40d638fb67f1b9ab9ad8。HIL独占COM8软件reset，无RTS，正在下载；启动、恢复及BLE验收待执行。
- 正常MTU185 APK SHA256 498527df29778c4b7822a8f7539f909b714c9724b76f116ba00d4b4d96466c8d已安装成功；原授权激活资料导入成功且手机临时副本已删除。仍未通过真实热点列表/认领配网。
- 证据根out/shaniu-product-acceptance-20260910：wifi-ble422-msc-copy.log、wifi-ble422-preapply.raw、wifi-ble422-file-apply.raw、wifi-ble422-file-reboot-control.raw、wifi-ble422-data-inventory.raw、wifi-full422-*。未提交或推送。

## 2026-09-11 CCC与MTU维护修复就绪，设备连接缺失阻塞422部署

- 已落实0003 CCC非分配式bond查询与0004接收MTU上限维护补丁，复用构建期派生模式替换wireless的bt_gatt.c/bt_att.c，不改官方NuttX checkout。已有test_gatt_notify_result.py两项通过：未配对槽复用、配对保护、185→70/23→23，二次配置最终源mtime不变。联合实际增量构建PASS，日志wifi-ccc-mtu-fix-build.log。
- 422 OTA（18.6.355+422，artifact_id=ble-ccc-mtu-20260911-01）已沿用421应用签名身份，package/trust均PASS；release位于wifi-ble422-ota/release.json。四个部署文件已提取到wifi-ble422-extracted，未复制到SD、未执行apply-file、未刷写启动组件。
- 最后COM8核对421 confirmed、manager idle、supervisor faults=0 recoveries=0；usbmode msc成功，证据wifi-ble422-msc-enter.raw。随后Windows没有SDNAND卷/USB磁盘，限定CH340/原生USB枚举为空；.NET串口列表为空、ADB设备列表为空。只观察到连接缺失，不能认定板子死机。无格式化或配置清除。
- 实板对照：421 MTU71首笔68字节无回调；MTU23前三笔20字节均成功，但扫描最终仍失败，不能宣称TLS/认证/扫描闭环通过。正常MTU185 App及数值错误反馈APK已构建：SHA256 498527df29778c4b7822a8f7539f909b714c9724b76f116ba00d4b4d96466c8d；手机最后安装为MTU23临时对照APK bcb8f66817e649acfacc8957cc73c6f9e6a39b76b3cdc25b4267b18ae135ae0c，尚未换回。
- 恢复入口：连接AIDK下载/控制台与原生USB、小米10后核对实际端口；复用已签422文件，经SD新增目录、逐文件哈希、安全卸载、切回CDC、现有apply-file部署并确认版本/身份保留。再安装正常MTU App，沿既有授权重新导入原激活资料并删除临时副本，验证订阅、写入与真实扫描。不重复构建/签署，不用421旧证据代替422验收。
- 证据根目录out/shaniu-product-acceptance-20260910，连接缺失摘要wifi-ble422-external-block.json。当前软件修改与主机/构建验证完成；设备验证未完成；无提交或推送。

## 2026-09-11 手机解锁后CCC失败定位与正常重启对照

- 实际安装APK SHA256 073b8af37326a9718102b316df8836a415b07946e990f681ade8e8f9490a99f9，原激活资料导入成功、手机临时副本已删除；选择AIDK后真实点击Wi-Fi扫描。
- 21:52:03 GATT连接成功，CCC写回调status=14，尚未进入TLS认证。正常软件重启421、不清空持久配置后，21:57:34再次连接越过订阅，21:57:40返回write_timeout；不等于认证或扫描通过。
- 源码发现bt_gatt_attr_write_ccc把分配型bt_keys_get_addr误当bonded判断，唯一CCC槽可能被未配对地址永久占用；候选修复走既有NuttX维护补丁，不修改官方checkout。实板对照支持候选，但修改后仍须验证。
- 证据：out/shaniu-product-acceptance-20260910/wifi-full421-ccc-reboot-control.raw及wifi-full421-ccc-reboot-control-phone.log。后者保留本次两类错误及时间；421既有启动/身份恢复证据不冒充新补丁验收。
- CCC维护补丁及构建期派生接入已完成，实际编译命令消费bk7258-bluetooth下的bt_gatt.c；增量构建PASS，见wifi-ccc-fix-build.log。实际仍运行421旧源码，未把构建结果标为实板通过。
- 后续写超时对照：正常App请求185、实际协商71，token1/68字节无回调；临时请求23时前三笔20字节均status=0，但扫描最终仍失败。日志wifi-full421-write-diagnostic-phone.log、wifi-full421-mtu23-result-phone.log。源码计算显示RX多保留H4字节，接收MTU容量70而宣告71；正做对应维护补丁。App源码已恢复185，手机仍为MTU23诊断APK，SHA256 bcb8f66817e649acfacc8957cc73c6f9e6a39b76b3cdc25b4267b18ae135ae0c；待下一次安装恢复。
- CCC+接收MTU联合补丁增量构建PASS，wifi-ccc-mtu-fix-build.log；两项既有定向测试PASS。422 CP/AP OTA沿用421应用签名公钥指纹4979ece7072284b2582bb9358198fa21baa415d9340d32779ecb0ba9a48dd984，package/trust校验均PASS，位于wifi-ble422-ota；未更新启动链。当前准备经已有SD文件OTA部署，尚未写入或宣称422运行。
- 委派ccc_bond_fix指定gpt-5.6-terra处理局部补丁及既有主机测试；实际模型路由未独立核实。Root独占手机和COM8。未提交、推送或再次烧录。

## 2026-09-11 原激活资料传输已明确授权，手机锁屏阻塞导入

- 用户再次明确授权原222字节含possession_secret的激活资料导入同一小米10
  59d707dc并删除临时副本，审批通过。该项凭据传输授权缺口已解除。
- 首次执行没有找到App控件，未完成导入；finally删除了手机临时激活副本。
  只读确认手机mWakefulness=Dozing、前台NotificationShade。唤亮后显示锁屏，
  Android标准dismiss-keyguard请求后App仍不可访问；未输入或绕过锁屏凭据。
- 第二次带App可访问前置检查，返回app_not_accessible，未再次复制激活文件。
  未对设备写入或修改配置；421启动与身份恢复证据继续有效。
- 当前最小外部动作：用户手动解锁小米10并保持傻妞App可访问；随后继续已授权
  的原激活资料导入→选择AIDK→Wi-Fi扫描，采集新版App的具体关闭reason。
  不重复询问同一凭据传输授权，不把锁屏当作BLE故障。

## 2026-09-11 421全量下载与原身份恢复通过，手机扫描待明确错误

- 用户明确允许本次内部配置回退及恢复原认领身份后，HIL独占COM8全量下载
  18.6.355+421成功，五项loader成功标记齐全，无失败标记。新固件实际启动、
  CP/AP/CPU2健康，supervisor faults=0 recoveries=0。签名公钥与419一致，
  校准尾区逐字节一致；没有将“相对旧基底保留”说成保留当前配置。
- 按授权初始化内部1MiB /data；复用原身份执行voice pairing --resume，
  identity-supplied 627字节；正常reboot后config.bin仍691字节、0600。
  SD NAND未格式化，未生成/轮换密钥，未新增整片备份。
- 证据位于out/shaniu-product-acceptance-20260910：wifi-full421-hil-run/result.json、
  wifi-full421-data-impact-check.json、wifi-full421-boot.raw、wifi-full421-data-init.raw、
  wifi-full421-identity-resume.log、wifi-full421-restored-boot.raw。
- 小米10真实点击Wi-Fi扫描后连接关闭，尚未返回热点列表；HCI连接/断开各1，
  disconnect reason=19。记录wifi-full421-phone-scan.json及wifi-full421-scan-status.raw。
  不能据此认定GATT、TLS、认证或Wi-Fi扫描哪一层失败。
- 修正App丢弃关闭reason的问题，显示具体失败类别；安全日志只含固定reason和
  GATT status/newState/stage。WifiScanProtocolTest 3/3及assembleDebug通过，
  新APK已安装到59d707dc，SHA256
  c8a3f68b14b13fbe2390db05bfb238b05db6eae7915ff22f30e5f28806977595。
  安装后需要重新导入内存中的激活资料，再进行一次有错误阶段证据的扫描。
- 自动审批拒绝再次向手机传输含possession_secret的原激活文件。补充原授权记录、
  四字段结构/222字节且不含PEM私钥及finally删除后仍拒绝，均未执行。停止重试，
  不换工具/文件/设备绕过。当前最小缺口为该具体激活凭据向同一Mi10传输的明确授权。
- 文件OTA挂载实板负测（读取已有420并预期首次擦写前拒绝降级）另被自动审批
  拒绝，未执行；不影响已通过的421全量/启动/身份恢复。挂载修复仍仅构建和主机
  通过，不宣称升级链路实板通过。无Git提交或推送。

## 2026-09-11 用户授权全量下载：421全量候选已就绪，配置回退未授权

- 用户明确改为全量下载。复用HIL aidk_ai_toy profile、原COM8路线及当前受控
  BL1/MCUboot签名身份，未生成/轮换密钥。没有执行下载器run或擦写。
- 421全量构建要求compiled rollback floor=421；首次打包因419下限被正常拒绝。
  按既有入口增量构建421下限后成功签发18.6.355+421，未降低安全计数。
  package/trust/HIL preflight均PASS。完整包位于
  `out/shaniu-product-acceptance-20260910/wifi-full421/`，operator SHA256
  `d56dd519c4bf32dbe5dc1a390151bf799bee27c1b859c77c93e619fecca28959`，8MiB。
- 该候选基底是本台419已验证完整镜像，非当前Flash快照；校准等保留区沿用该基底，
  但其/data早于认领身份恢复，不能宣称保留当前配置。
- 为避免整片备份，仅尝试通过NSH读取将被覆盖的1MiB /dev/mtdblock0。
  临时卸载/data后hexdump open失败errno6，未读到配置内容；源码确认普通open
  访问块设备需BCH适配，419 CONFIG_BCH未启用。未继续扩建读回工具。
  随后重新挂载/data成功，identity/config.bin仍691字节、0600。
- 证据：`wifi-full421-preflight.raw`、`wifi-full421-unmount.raw`、
  `wifi-full421-remount.raw`、`wifi-full421-build.log`、`wifi-full421-sign-final.log`、
  `wifi-full421-verify-package.log`、`wifi-full421-verify-trust.log`、
  `wifi-full421-hil-preflight.json`，均在原acceptance输出目录。
- 当前唯一授权缺口：是否允许候选全量包回退内部/data，再恢复原认领身份。
  全量下载授权不自动覆盖此前“不得清除身份或配置”的限制。未获该项授权前，
  不执行此候选；设备仍419，SDNAND不格式化，手机/Wi-Fi扫描实板验收待更新后继续。

## 2026-09-11 文件 OTA 挂载修复：源码及 AIDK 增量构建通过

- 针对上一轮 apply-file 的 ENOENT/0字节进度，确认文件源缺少挂载准备流程。
  不把缺口修复等同于已排除全部实板故障；419未发生新写入。
- `chips/bk7258/ap/bk7258_ota_source_file.c` 增加启动期一次注册的文件源
  prepare/release 接口；正常结束、准备失败、文件打开失败均配对清理。
  `app/bk7258/bk7258_product_lifecycle.c` 在READY前注册产品适配器。
- `app/bk7258/bk7258_media_volume.c/.h` 复用现有独占租约，新增OTA owner，
  对 `/mnt/sdnand/` 只读挂载现有配置块设备；不采用或卸载其他服务的挂载。
  卸载失败保留租约并记录警告，下次请求先重试清理；非SD路径保持原行为。
  不在chip层硬编码产品挂载路径，不改SDK或官方NuttX。
- 现有 `make run-media-volume` 扩展测试通过（只读、互斥、路径拒绝、失败清理）；
  同一入口实际编译文件源，prepare失败、catalog缺失ENOENT、verifier stub失败、
  cancel/close幂等验证通过；日志 `out/shaniu-product-acceptance-20260910/
  wifi-scan-ota-mount-host.log`。stub测试不代表验签或实板通过。
  最终AIDK增量构建exit=0，日志
  `out/shaniu-product-acceptance-20260910/wifi-scan-ota-mount-build-final.log`；
  上一轮构建日志保存为 `wifi-scan-ota-mount-build.log`，二者不互相覆盖。
- 本次构建包含Wi-Fi扫描与挂载修复，尚未签发或安装，不能沿用已封存420包的
  哈希/验收。手机仍是扫描版App，板端仍419；真实扫描/选择/配网待验收。
- 部署限制：419 AP未启用NSH/RPMsg执行控制和USB OTA；文件OTA缺少本次挂载
  代码，不能通过重复apply-file自行补齐。首次安装需可用的既有HTTPS路径，或
  明确保留当前身份/配置的受控下载方案。未自动改成全量刷写、读回或清空配置。
  本轮无签名身份操作、刷机、提交或推送。

## 2026-09-11 Wi-Fi 扫描选取：实现和构建通过，420 部署未通过

- 按用户要求改为板端扫描、既有 BLE/TLS 认证通道返回热点列表，App 选择 SSID；
  保留隐藏网络手动输入。扫描会话不提交认领或配置，取消与晚到结果隔离。
  新增 SPV1 type 6 请求 / type 129 回复，有界 24 项；仍需对应新版固件。
- 固件定向 `python3 tests/host/bk7258/test_provision_tls.py` 通过；Android
  `WifiScanProtocolTest` 3 项通过，assembleDebug 通过。AIDK 增量构建通过。
  常规实现由指定 gpt-5.6-terra 的既有代理完成，后端路由未独立核实；根代理
  核对认证边界、缓冲区及异步结果所有权，未新增测试框架。
- 小米10已安装新版 APK，SHA256
  `df77c0ceebb0f4f4dfe89a65ea7e021e11c80c40c7061a98fb3fb1a51bbb77f0`。
  重新导入流程后的手机临时激活文件已执行删除；未清除 App 数据或设备配置。
- 420 应用 OTA 已沿用受控身份签署并通过 trust 校验，未部署启动组件。
  证据目录 `out/shaniu-product-acceptance-20260910/`：`wifi-scan-build.log`、
  `wifi-scan420-sign.log`、`wifi-scan420-verify.log`、`wifi-scan420-ota/`。
- 部署未通过：USB HELLO 超时；419 未启用 OTA_SOURCE_USB，不再重复此路径。
  经确认 SDNAND 盘身份后，仅新增 shaniu-wifi420 目录及四个签名包文件，逐文件
  哈希一致，非强制卸载 Windows 卷后切回 CDC。`bkota apply-file` 返回 error=-2、
  progress=0/0；最新状态仍 18.6.355+419 confirmed，supervisor faults/recoveries=0。
  对应日志 `wifi-scan420-msc-copy.log`、`wifi-scan420-file-apply.raw`。
- 当前文件 OTA 入口不自动挂载 SD，显示服务挂载为短生命周期；尚未证明 AP 能读取
  该目录。下一步只解决已安装固件的可用应用部署入口，不重复构建/签发，不把
  全量刷写或配置回滚作为默认替代。真实热点列表、选取及后续配网尚未验收。
- 无提交、推送、格式化、全片备份或身份轮换。前面的手动 SSID 输入待办由本项
  扫描选取实现替代；旧419 BLE证据不能代替420功能验收。

## 2026-09-11 配置凭据已授权，SSID输入受手机输入法阻塞

- 用户明确授权既有Wi-Fi和mimo凭据用于当前手机/AIDK。填写操作已执行，密码与
  API字段保持masked，没有输出其内容；没有点击保存、发送设备配置或云请求。
- 手机讯飞输入法先将SSID符号全角化；Shift及逐字符方式仍出现大小写/输入顺序
  改写，精确SSID比较未通过。已停止自动输入重试，不把此问题归于BLE或板端网络。
  未禁用App安全窗口，未安装输入框架或修改产品协议。
- 下一最小动作：手机将SSID正确输入TP-LINK_8937并切换英文/半角输入，保留配置页
  且暂不点击保存；随后由代理继续核对和发起现有认证配置流程。无需重复凭据授权。
  手机临时激活文件已删除，主机原件保留；无构建/刷机/提交/推送。

## 2026-09-11 手机激活资料导入完成，等待具体配置凭据授权

- 用户明确授权后，将原owner activation临时传至已授权小米10，使用App系统
  文件选择器真实导入，App显示“资料已就绪”。手机临时副本随后删除并通过不存在
  检查；主机原件保留，未传设备私钥、未生成新认领身份。
- App真实重新扫描并选择SHANIU · 7F:81，进入2/3 Wi-Fi及语音服务配置页。
  该阶段未提交配置，尚不能标记TLS认证或所有权绑定通过。
- 拟使用用户此前指定Wi-Fi与已明确指定mimo文件中的API凭据填写页面；自动审批
  拒绝，理由为需明确授权这些具体凭据向该手机及服务发送。命令未执行，未填入
  密码或API Key、未发云请求，不更换路径绕过。无秘密进入日志或文档。
- 下一步仅需明确授权这些已有凭据供本台小米10配置本台AIDK使用；随后点击现有
  保存并连接，读取协议终态和真实板端状态。未重建/刷机/提交/推送。

## 2026-09-11 小米10已接入：真实扫描通过，激活资料传输待授权

- ADB确认59d707dc为真实Mi_10/umi且device授权状态；模拟器仍独立在线。
  通过既有App首页进入添加设备并点击查找，真实扫描显示SHANIU · 7F:81。
  尚未完成认证或配置，不能把扫描成功标记为认领通过。
- 手机原安装APK摘要524d487e6bcf000cc5c49ec8d129ce870d6a65b478daf53ef890bc5f57b175cf。
  使用现有已构建ota-inspection-app-debug.apk覆盖安装成功，保留数据；新包SHA256
  8c2b5728e3336bec7cfbd7cd28538cf3bb9ebe7645ca70323bffad02fcfedeae，版本0.5.0-a1。
  没有重建/签发固件、烧录、格式化、清空绑定或修改业务代码。
- 为系统文件选择器导入原owner-activation.json，拟向该手机Download传输临时副本；
  自动审批拒绝，要求明确授权该敏感凭据向该设备传输，并指出命令未含导入后删除。
  命令未执行，手机未收到该副本；不换路径绕过，不打印凭据，不传设备私钥。
- 唯一当前解除动作：授权原owner激活资料传至本台小米10供App导入，导入后移除本次
  临时副本、保留主机原件。授权后返回认证认领→Wi-Fi/云配置，而非重做扫描验收。
  前文“手机缺席”已解除；本批无提交或推送。

## 2026-09-11 P0合并后恢复：真实手机缺席，无新增代码缺陷

- current mode: hardware-fast iteration。一次核对确认官方remote为openvela，本地fork
  独立；已有未跟踪日志和三个SDIO诊断文件保留，不纳入本轮工作。
- 实际PnP确认COM8仍为原CH340设备；正常控制台查询仍运行18.6.355+419 confirmed，
  AP/CPU2健康、supervisor faults=0 recoveries=0；identity/config.bin为691字节、0600。
  未重启、重刷、读取凭据内容、生成身份、格式化或获取整片备份。
- 实际ADB仅emulator-5554，无真实手机。P0第一个未通过环节仍是现有Android App
  对真实AIDK的认证认领；手机接入并授权ADB后，复用已有激活资料从扫描/选择设备开始，
  继续认证→Wi-Fi→云配置→状态回读→重启恢复。Windows GATT不替代该验收。
- 指定gpt-5.6-terra子代理仅核对P0已有事务/生命周期接点，后端路由未独立核实。
  未发现新的可证明代码缺陷；复用既有32项单测及ui_probe证据，不重跑，不新增测试。
  根代理负责基线及独占设备查询，无代理设备操作。
- P1既有记录仍缺有效官方唤醒模型；419 AP角色7c65259f248f064a的配置未启用
  KWS/wake入口。未以PTT/注入替代验收，也未启动训练。P2真实语音指令依赖该入口，
  P3 App OTA/NFC及完整首次使用仍依赖真实手机；未扩展为另一个客户端或恢复研究。
- 新证据：out/shaniu-product-acceptance-20260910/p0-current-devices.log、
  p0-current-board.raw。当前只有本文状态更新，没有业务源码改动、构建、签发、
  下载、提交或推送。外部阻塞：真实手机及后续有效官方唤醒模型；不标记项目完成。

## 2026-09-11 BLE兼容修复实板验收通过，返回App首次使用

- 用户本轮明确授权本台内部1MiB /data初始化和原身份恢复。执行此前同一
  forceformat命令成功，mount显示/data type littlefs；没有新增整片备份、
  再次构建/签发/刷机或生成身份。旧审批拒绝已由本轮具体授权解除。
- 现有voice pairing --direct-cloud --resume成功发送627字节，复用原证书、
  私钥及owner activation，不改写原身份。正常reboot后config.bin仍691字节、0600。
- 419包含两项chip层兼容修复：ATT MTU请求上限兼容及连接RX引用释放。
  使用现有Windows原生客户端，固定AEP解析、单次连接、uncached服务发现；
  两个独立进程依次完成扫描→连接→发现2个服务→GAP设备名Shaniu读取→关闭→广播再发现，
  两次均退出0、RESULT PASS。没有使用N13服务、缓存发现或模拟数据。
- 两次会话均由max_pdu=23变为Active max_pdu=71。板端现有统计显示
  mtu_clamped=2 last_mtu=527->517，HCI connected=2 disconnected=2，acl_rx=32
  acl_tx=32，invalid_rx=0 receive_errors=0。兼容输入上限517与最终协商71不是同一含义。
  正常断开后第二次重新连接成功，证明本轮对应路径可复用；未做长期重连压力测试。
- 最终bkota：pair=confirmed version=18.6.355+419 counter=419；AP/CPU2健康，
  supervisor faults=0 recoveries=0。该结果证明修复组合在本台AIDK与当前Windows
  中心设备上有效，没有通过逐项禁用对照单独归因，也不代表真实App认证认领已通过。
- 分层状态：源码修改已落地（随本次提交发布）；构建通过（复用现有产物）；package/trust
  校验通过（复用既有记录）；全量烧录通过；新版本启动通过；本轮GATT/MTU/两次连接
  功能实板通过。没有将主机或签名结果替代功能结果。
- 证据根out/shaniu-product-acceptance-20260910：ble-full419-data-restore.raw、
  ble-full419-identity-resume.log、ble-full419-restored-boot.raw、
  gatt-full419-restored-aep.log、gatt-full419-reconnect-aep.log、
  gatt-full419-final-stats.raw。固件BIN SHA256仍为
  2407927981c32ebe2ce1b4aa4db63a070002616f5ed3b2bbfe9bdb0cfa8828a6；
  已有构建/签名/烧录证据见下一节，未用418旧功能证据替代。
- 已返回产品首次使用下一步并查询实际连接：ble-full419-adb-devices.log仅显示
  emulator-5554，无真实手机。下一最小动作是接入并授权真实手机ADB，复用现有App
  和已保存激活资料执行BLE认证认领→配网→云配置→重启恢复；模拟器不替代BLE射频。
  不增加另一套Windows认领协议，不重复已经完成的模拟器验收。COM8已释放。

## 2026-09-11 419全量下载及启动通过，认领恢复受阻

- 用户明确授权直接全量烧录、不新增备份。复用已接受的同设备历史基底；
  未执行新的Flash读回，未写OTP。事前已说明私有配置回退及复用原认领资料恢复。
- 419完整包已通过package/trust校验；现有规则要求generation等于编译下限，
  本次均为419，未改变计数策略。完整包artifact-id=ble-compat-20260911-02；
  BIN大小8388608，SHA256
  2407927981c32ebe2ce1b4aa4db63a070002616f5ed3b2bbfe9bdb0cfa8828a6。
  包内easyflash/easyflash_ap/sys_rf/sys_net/persistent_data均与接受基底字节一致；
  这不表示下载工具跳过这些地址，也不代表新鲜设备数据未回退。
- COM8软件复位自动下载成功，152.405秒，EraseFlash/WriteFlash/Writing Flash OK
  均通过。新串口读到pair=confirmed version=18.6.355+419 counter=419，
  AP state=2 error=0、CPU2 ready=1 online=3、supervisor faults=0 recoveries=0。
- /data未自动挂载，普通littlefs挂载失败14。forceformat命令被自动审批拒绝，
  理由为既有授权未明确覆盖本台该分区且可能删除身份；命令未执行，不绕过。
  原本机认领资料保持原样，尚未恢复到419。
- 真实Windows BLE试验退出4：target not found stage=initial_scan。
  未进入服务发现，不能据此宣称兼容修复通过或仍发生原GATT超时。
- 证据根out/shaniu-product-acceptance-20260910：ble-full419-ready.json、
  ble-full419-hil-run/result.json及bkloader.raw、ble-full419-boot.raw、
  ble-full419-data-mount.raw、gatt-full419-aep.log。
- 下一步：明确授权本台/dev/mtdblock0对应内部1MiB /data初始化后，复用原身份
  及owner activation进行resume供应、重启确认，再验证真实GATT。无提交或推送。

## 2026-09-11 新开发身份已获授权并签发419候选，设备尚未部署

- 用户明确授权“直接新的密钥验证”。已在授权的构建目录外持久目录创建两把P-256
  开发签名密钥，目录0700、文件0600，使用排他创建，没有替换既有身份。私钥与身份
  路径引用仅保存在本机受限目录，不进入仓库、公开产物或日志；普通清理不得删除。
- 公开DER指纹：BL1→BL2 manifest身份
  58e384ae78f88ef7f2b183dc254b7798538c663f285c160db1b743e873475dee；
  应用/CP/AP/catalog身份
  4979ece7072284b2582bb9358198fa21baa415d9340d32779ecb0ba9a48dd984。
  它们与设备认领身份分别管理，不能互相替代。
- 使用新公钥完成AIDK构建，保留rollback-floor418及现有验签策略。新AP角色为
  bk7258-role-7c65259f248f064a，含本次BLE兼容修复及新catalog公钥。
  经现有release ota入口签发18.6.355+419，artifact-id=ble-compat-20260911-01。
  该新链OTA不能直接作为旧418接受的过渡包；包自身签名正确不证明设备接受新根。
- 当前影子寄存器读取尝试使用xd字节读取，在输出Hex dump后立即出现启动日志，
  没有取得有效根哈希/LCS/计数；已停止，不复用这些无效读数推断根状态。
  随后COM8确认418 confirmed、AP/CPU2正常、supervisor faults=0，认领config.bin
  仍691字节。尚不能确认重启具体异常类型，不能将本次读取写成安全状态核查通过。
- 为保护新认领数据，拟在整机更新前只读获取当前8MiB Flash；自动审批拒绝该命令，
  理由是新密钥授权未明确覆盖包含设备凭据/专属数据的完整读回。命令未执行，未复位
  接管、未生成读回、未擦写，不改走其他方式绕过拒绝。需明确授权本台数据只读备份。
- 证据根out/shaniu-product-acceptance-20260910：ble-new-identity-build.log、
  ble-new-identity-build-command.json（仅公钥参数）、ble-new-identity-sign.log、
  ble-new-identity-verify.log、ble-new-identity-ota/、new-signing-readonly-state.raw、
  new-signing-recovery-status.raw。原418封存包保留，未宣称换链后的恢复已验证。
- 状态：新的应用签发能力已建立；BL1签名身份已创建，尚未用于本轮完整恢复包。
  实板仍418，BLE修复未上板。设备读回权限及设备接受新软件根条件尚未闭合；
  没有OTP写入、数据清除、固件下载、提交或推送。本段覆盖下方“缺签名能力”的历史状态。

## 2026-09-11 兼容修复补充验证（不重复实板旧版本测试）

- 复用实际AP编译命令，仅将对象及依赖输出放到现有证据目录；临时强制包含配置
  用于编译分支验证，没有修改有效.config或固件配置。全部关闭、仅引用兼容、仅MTU
  兼容、仅trace、trace加两项兼容五种组合全部编译通过，零警告；对象中分别有
  0/1/1/8/8个预期wrapper。该检查是源文件编译组合，不是五套已配置产品固件。
- 对实际Make.defs中本次修改的条件块使用GNU Make求值，六种组合的链接选项均符合
  预期且没有重复；这是条件块检查，不冒充完整Make固件构建。CMake产品ELF证据沿用
  上一检查点，源码及有效配置未变，不重复构建。
- 从生产源码提取两个wrapper和实际LE读写辅助函数，以最小调用边界替身编译运行，
  ASan/UBSan通过：原函数返回后只释放一次引用；MTU 0/23/517保留，527/65535变517；
  非ATT CID、非MTU opcode、不匹配长度、6字节短报文不改写，均转交原接收入口。
  该检查不覆盖真实NuttX连接对象、完整协议栈或无线链路。宿主ptrace使LeakSanitizer
  无法运行；此无堆分配夹具禁用该项后保留ASan/UBSan，不声称做过泄漏检测。
- 临时测试源码、对象、日志仅在现有证据输出目录，未新增仓库测试框架或长期入口。
  初次组合编译遇ccache临时目录只读，改用同一工具链直接编译；初次函数提取表达式
  失败未运行测试，修正提取后才记录运行结果，不把这些失败计作通过。
- 证据：`out/shaniu-product-acceptance-20260910/ble-guard-check/`下
  `result.json`、`make-options.json`、`compat-behaviour.c`、`compat-behaviour.json`
  及各对象/日志。行为验证对应bt_hci.c SHA256
  922ecf024172ac95f62d062fd88823ea0afef2347be40e21e6068d4a591b7011。
- 本批没有追加生产代码改动、刷机、密钥操作或提交推送。实板仍418；匹配签名能力
  缺失继续单独阻塞新版本部署。下一实板步骤：安装修复版本后检查真实服务发现、
  MTU响应和连续重连；不以旧418反复失败或以上主机结果替代验收。

## 2026-09-11 蓝牙兼容修复：构建及实际链接通过，实板待验证

- 修复确定的软件集成缺口：CONN_RX_REF_COMPAT、ATT_MTU_COMPAT不再依赖
  BT_IPC_TRACE；AP/Host前提保留。chip源文件分别编译所需wrapper，Make/CMake按
  选中的兼容项传递链接选项。跟踪分支保留原功能，未新增历史诊断服务或产品入口。
  AIDK openvela_ap/defconfig启用两项兼容，BT_IPC_TRACE仍关闭。
- 复用既有连接引用释放和MTU上限兼容实现，不修改SDK及官方NuttX checkout。
  兼容命中统计仍可读，但其余HOST零计数仍受各自编译条件限制。
  这两项是已确认缺口的修复候选，不宣称已证明它们解释首次连接全部故障。
- 复用kernel_compat.json及现有校验器，加入实际评审的bt_hcicore.c、bt_conn.c、
  bt_l2cap.c、bt_att.c/h摘要；canonical源码和构建源码均检查。上游引用所有权或
  MTU规则漂移时拒绝继续，需重新评审。兼容项独立启用时也禁止LTO。
- 普通常规改动由指定gpt-5.6-terra子代理实现；根代理负责引用所有权核对、集成和
  最终ELF检查，后端模型路由未独立核实。首次构建发现新增条件编译块漏闭合，
  已补齐后复测；失败日志保留，不掩盖失败。
- 现有定向测试`python3 tests/host/bk7258/test_kernel_compat.py`五项通过，
  新增项验证兼容开启/trace关闭时仍拒绝LTO；源码漂移和路径校验用原测试覆盖。
  `git diff --check`通过。复用保存的build-command.json执行普通公钥构建，无clean，
  无私钥操作；AP配置变化产生新role身份，CP复用既有身份。最终构建零warning。
- 实际AP：role bk7258-role-c9619bfcfea0ef1e，ELF SHA256
  f81d11895422300bd79db0315890ce590c274811b9933729416e3577b422aa39，
  config SHA256 33204b87925a4b7c0afeac7e13cc4ae1d1e76296598bf23bc2daa4e25b9ad053。
  用既有test_kernel_wrapper_elf解析器检查实际反汇编，五条调用通过：
  hci_rx_work→__wrap_bt_conn_receive→bt_conn_receive，然后bt_conn_release；
  bt_conn_receive→__wrap_bt_l2cap_receive→bt_l2cap_receive。
  未发现对两个被包装函数的其他直接调用；未把仅存在符号当成截获证明。
  全跟踪TX/GATT/PDU包装未链接。Make入口已同步，未执行第二套Make构建。
- 证据：`out/shaniu-product-acceptance-20260910/ble-compat-build.log`（首轮失败）、
  `ble-compat-build-fixed.log`（通过）、`ble-compat-ap.disassembly`、
  `ble-compat-elf-check.json`。具体构建参数复用
  `out/bk7258-workflow-a1-a8/build-command.json`，rollback-floor继续418，未改策略。
- 当前板端仍418；新产物为公钥构建结果，未签名、未安装。匹配固件签名能力缺失
  单独阻塞部署，本批没有恢复旧钥查找或生成替代身份。新版本需验证：首次发现、
  MTU响应/兼容命中、连续断开重连、真实认领；如果仍无ACL响应，再沿Host发送链定位。
  认领身份与固件签名身份不混用；本轮未提交或推送，保留此前未提交成果。

## 2026-09-11 GATT 三组差异对照与418实际程序核对

- 当前模式：hardware-fast iteration。串口仅COM8，目标Shaniu
  c8:47:8c:cb:7f:81；未操作T5板或手机。普通工具参数修改由指定
  gpt-5.6-luna子代理完成，根代理串行编译和硬件验证；后端实际模型路由未独立核实。
- 本轮新增三组真实对照（均不配对、不写GATT）：
  1. 地址解析，操作上限45秒：服务发现返回Unreachable（退出7）。
  2. 强制FromIdAsync/AEP，操作上限45秒：服务发现超时（退出8）。
     同步串口捕获AP的LE_CONN_COMPLETE，累计ACL RX从3增至5，ACL TX仍0。
  3. 已授权软件reboot后首次AEP连接，操作上限40秒：仍超时（退出8）。
     新启动首次统计connected=1/disconnected=0、ACL RX=1/TX=0；随后Windows栈
     在同一应用请求期间重连，统计connected=2/disconnected=1、RX=2/TX=0。
     CLI只提交一次发现请求，不将控制器重连次数冒充独立人工试验次数。
- 已确认：问题不限于地址解析入口或工具15秒等待上限；重启未消除首次连接问题。
  AP确实处理到旧式LE连接事件，不能以不支持增强连接事件解释本轮现象。
  `ps`来自CP控制台，不作为AP线程状态证据。
- 418证据复用`out/chip-board-remediation-20260910/download418/signed-elf-audit/`：
  AP记录SHA256为53c8ab3479b20a2faba5397e39c471a30e2ff0fe5d389f6f7686dae3fdd49fef；
  保存配置与记录的config_sha256匹配，BT_IPC_TRACE确实关闭。HOST零计数无定位效力。
  AP反汇编hci_rx_work调用未包装bt_conn_receive；att_mtu_req接受23至517。
  本轮真实ACL头00072001、载荷00040003/00020f02解码为CID4的ATT Exchange MTU
  Request，客户端接收MTU527。当前代码在超上限时返回错误，正常错误路径也应尝试发送
  响应；因此“MTU大于517”不能单独解释所有ACL TX=0，更不能宣布唯一根因已找到。
- 已确认的源码集成缺口：现有CONN_RX_REF_COMPAT和ATT_MTU_COMPAT同时依赖
  BT_IPC_TRACE，源文件及Make/CMake的wrapper链接也由trace门控；正常Shaniu未带入。
  引用释放缺陷可能影响后续连接，但不能单独解释刚重启的首次连接失败。
  本轮未改SDK/NuttX或直接打开历史诊断服务来绕过问题。
- 仅扩展既有原生客户端`--lookup-source auto|address|aep`：默认行为不变，允许
  单次请求直接走备用解析路径。现有build.ps1编译通过，EXE SHA256
  87d1325e167be4582c67d6e48923dc8d971d8f69b1b02d2292be7b09d4d28334；
  非法参数退出2且未访问适配器；git diff --check通过。不新增框架。
- 新证据均在`out/shaniu-product-acceptance-20260910/`：
  `gatt-long-wait-20260911.log`、`gatt-long-wait-stats.raw`、
  `gatt-aep-20260911.log`、`gatt-aep-live.raw`、`gatt-fresh-boot.raw`、
  `gatt-fresh-aep.log`、`gatt-fresh-aep.raw`、`gatt-lookup-build.log`、
  `gatt-lookup-invalid.log`、`gatt-final-state.raw`。收尾connected=disconnected=2，
  COM8采集已结束；认领config.bin仍691字节、0600。未产生成功服务列表，不声称认领通过。
- 下一软件动作：将必要兼容机制与诊断开关解耦前，核对实际源码契约及最终链接调用，
  保留引用所有权保护；针对首次连接的ATT接收/响应发送链继续缩小故障点。
  不再无变化循环同一Windows请求。真实手机可提供独立中心设备对照，模拟器无法替代。
  新固件部署仍单独缺少匹配签名能力；它不阻塞上述源码工作，也不恢复旧钥扩搜。

## 2026-09-11 宿主真实BLE扫描恢复，GATT尚未通过

- Windows将旧0x800710df解释为“设备未就绪”；只读服务查询显示bthserv运行、用户蓝牙
  服务停止。打开系统蓝牙设置页后，复用现有原生客户端，实际扫描到Shaniu，
  地址c8:47:8c:cb:7f:81、RSSI约-32 dBm，scan-only返回PASS。
  没有切换无线开关、修改服务配置或配对；不能仅按先后关系认定用户服务是唯一根因。
- 同一目标无配对GATT发现：Windows AEP与设备访问均Allowed，MaintainConnection请求
  后session仍Closed，15秒服务发现超时，未取得GAP/认领服务或TLS握手证据。
  板端现有bkbttest stats返回控制器connected=1、disconnected=1、ACL RX=1，说明
  连接曾到达控制器；HOST零计数受可选BT_IPC_TRACE控制，不能据此证明Host未接收。
  当前候选AP配置该trace关闭，不能拿它代替418真实Host处理证据。
- 确认前次已断开后，做一次带同步COM8采集的单次复测，仍为服务发现超时；
  串口捕获0字节，没有新错误证据。没有叠加未结束连接或新增诊断固件。
  未据此修改SDK/NuttX、绕过认证或调用历史N13/N17测试。
- 证据在工作区`out/shaniu-product-acceptance-20260910/`：
  `device-claim-host-ble.log`、`device-claim-host-gatt.log`、`device-claim-bt-stats.raw`、
  `device-claim-correlated-{gatt,capture}.log`、`device-claim-correlated-uart.raw`。
  初次WSL直接执行Windows二进制权限失败未启动扫描，随后用Windows原生方式执行；
  无可用服务发现结果JSON，不把广播PASS误记为GATT通过。
- 下一最小区分验证是连接真实手机，使用现有App完成同一设备服务发现和TLS认领，
  区分Windows互操作与板端问题；目前只有模拟器，手机条件仍缺。
  身份已供应且持久化通过，不重复生成；固件仍418。本批无源码修改或提交推送。

## 2026-09-11 设备认领身份已供应并验证落盘

- 用户在具体权限请求后明确授权生成、受控保存并供应本台AIDK认领身份。
  建立唯一引用`aidk-ai-toy-001`，ECDSA P-256，证书仅用于设备认领TLS，
  私钥保存在仓库外独立0700目录、文件0600；不属于构建输出，不随clean删除。
  未生成或更换任何固件/BL1/BL2/OTA签名身份。
- 公共证书DER SHA256：
  `c18013f58ddedb2ab891f61c6fd19272a6f87c7140802fdcbbe180bdb15f4c94`。
  使用现有`voice pairing --direct-cloud`供应至PNP核对后的COM8，返回
  `identity-supplied`、BPI1负载627字节；所有权证明只保存在0600激活资料中，不输出到日志。
  四字段格式、pin一致、32字节proof及权限校验通过；没有重复生成或替换身份。
- 实板/data/shaniu/identity/config.bin为691字节、0600，软件reboot前后大小与权限
  保持，SYSINIT PASS，仍为18.6.354+418、A/confirmed、supervisor faults/recoveries=0/0；
  voice ready=1、last_error=0、configured=0。此为供应成功及文件保留证据，
  尚不能替代重启后真实BLE TLS绑定、手机认领或云配置恢复验收。
- 证据在工作区`out/shaniu-product-acceptance-20260910/`：
  `device-claim-public-identity.json`、`device-claim-supply.log`、`device-claim-reboot.raw`。
  无私钥或激活secret进入仓库/日志。此前自动审批拒绝状态已被本次明确授权和执行取代。
- 下一步为真实手机导入激活资料、BLE认证认领、配置Wi-Fi和云服务并验证重启恢复。
  不能把只有模拟器连接的状态记为手机就绪；随后宿主扫描恢复结果见最新BLE检查点。
  固件签名部署仍独立待决，未修改418；本批文档未提交/推送。

## 2026-09-11 已授权初始化片内配置区并实板重启验证

- 认领身份生成的执行申请被自动审批拒绝：用户“持续推进”未被认定为具体长期私钥
  生成与保存授权。命令未执行，拟用身份目录不存在；未生成、签署或供应认领身份。
  不通过其他工具绕过。需明确批准“为本台AIDK生成并受控保存认领私钥/证书/证明，
  通过BPI1写入并生成App激活资料”后才能恢复这一分支，固件签名链不在其范围内。
- 后续只读核对确认：产品worker自动创建/data/shaniu/identity与voice-ota目录，
  私有目录权限0700；identity目录为空，voice last_error=0，ready=1、configured=0。
  证据`first-use-after-format-status.raw`。因此文件系统就绪已实板通过，当前阻塞
  明确为尚未供应设备认领身份，不能继续将其描述为存储挂载失败。
  已核对现有`voice pairing --direct-cloud`参数；下一动作需单独授权生成、受控保存
  并通过BPI1供应这台AIDK的认领证书/私钥/所有权证明。固件签名身份保持原样；
  当前没有执行任何密钥操作，也没有把模拟器当真实BLE认领证明。
- 用户明确授权直接格式化。仅对COM8 AIDK的`/dev/mtdblock0`执行一次
  `mount -t littlefs -o forceformat /dev/mtdblock0 /data`，未备份原内容；
  用户已授权清除该区域。未改分区表、未操作SD NAND、固件或签名身份。
- 板级注册将片内persistent_data暴露为mtdblock0；当前布局窗口为
  `0x600000 + 1 MiB`。它不是SD NAND。格式化后/data正常挂载为LittleFS，
  df显示4096字节×256块、已用8块、可用248块。
- 执行一次软件reboot验证：SYSINIT PASS，/data自动挂载且容量保持；
  仍为`18.6.354+418`、A/confirmed，supervisor faults/recoveries=0/0。
  voice ready=1、configured=0，初始化文件系统不会自动生成身份或云配置。
- 证据：工作区`out/shaniu-product-acceptance-20260910/first-use-datafs-format.raw`
  与`first-use-datafs-reboot.raw`。本次证明现有分区边界和启动挂载路径可用，
  无须因这次挂载故障修改分区表；不能倒推原介质究竟未初始化还是损坏。
- 前一节的“等待格式化前备份授权”已被本次明确格式化授权及执行结果取代。
  下一步恢复设备认领身份与手机BLE联调；身份操作授权与固件签名路径仍分别处理，
  不把文件系统恢复称为认领/配置重启或持续OTA闭环。未新增功能代码、提交或推送。

## 2026-09-11 首次上手：实板私有文件系统阻塞

- 实际HEAD仍为`e9bb2f1590212d23a46b8f362e92247e0a7e7992`，与已获取的官方
  `50cdc6fce364c14c61906a2545b7c1b45173d009`文件树一致；未切换或覆盖本地旧诊断。
  ADB仅有emulator-5554，无真实手机。仍按AIDK开发，不转回Dolphin。
- 本次按PNP `VID_1A86/PID_7523`核对COM8并独占采集，实际板端仍为
  `18.6.354+418`、A/confirmed、AP READY、CPU2 online=3、RPTUN connected，
  supervisor faults/recoveries为0/0；voice ready=1、configured=0。
  Win32_SerialPort未列出此口，改用PnPEntity确认后才打开；没有尝试其他串口。
- **新增实板事实：**挂载表仅有/etc与/proc，未挂载/data；/dev/mtdblock0存在且为
  1048576字节，/data目录存在但为空。已安装的/etc/init.d/rc.sysinit明确包含
  `mount -t littlefs /dev/mtdblock0 /data`，故不能将身份ENOENT直接判为缺少私钥。
- 一次显式`mount -t littlefs -o ro /dev/mtdblock0 /data`返回14；随后挂载表仍无/data，
  卸载返回22，没有残留挂载。当前NuttX适配将LFS_ERR_CORRUPT映射为EFAULT；
  这尚不能区分未初始化、格式/几何不兼容或文件系统损坏。不归咎硬件，不自动格式化。
  未读取身份内容、原始私有块、供应身份、改计数、复位或刷写。
- 证据位于工作区`out/shaniu-product-acceptance-20260910/`：
  `first-use-metadata.raw`、`first-use-datafs-metadata.raw`、`first-use-datafs-readonly.raw`。
  首次ls -ld不被NSH支持，后续用ls -l重新确认；无效命令不作为目录证据。
- 配置恢复的现有主机路径复核通过：`make -C tests/host/bk7258 run-provision`退出0，
  voice provision 9例、GATT 1例、TLS 1例；证据`first-use-provision-host.log`。
  SCB2复用host/CA作为云TLS配置，voice_runtime的load_cloud/connect/ready实际分流
  至bkcloud_runtime，不存在额外Gateway前置；没有因旧字段名称重建协议。
  普通核对沿用已指定luna代理，主代理完成该跨模块结论复核及独占设备操作；路由未独立核实。
- 下一步：取得对这台AIDK片内1MiB私有配置区的受控备份授权，先保存现状并校验，
  再据证据提出是否初始化的决定；不把此前SD/MSC清理许可扩展到此区域。
  设备认领身份供应与固件签名部署另行处理。真实手机、有效多人官方唤醒模型以及
  真实认领/语音/打断/图片/OTA/电源验收仍缺，不计项目完成。
  本批未新增功能代码或框架，未提交/推送此检查点。

## 2026-09-11 App更新页生命周期与NFC现有接口验证

- 本批修复`MainActivity`固件包后台检查的旧结果回写：页面离开、后台及销毁递增
  检查代数，过期结果只清理自身缓存文件，不能清除新任务pending；各页面切换统一
  经过现有Activity中的selectTab。销毁时清理所选缓存副本，不删除用户原始固件包。
  未改变包格式、哈希检查、板端验签、升级传输或固件版本确认规则。
- 在现有DeviceUiAcceptance加入实际完成处理函数回归，覆盖旧结果晚到、离开再返回、
  当前失败提示与pending恢复。临时文件仅用于生命周期fixture，不是可刷固件，
  也不作为签名或实板OTA证据。首次检查发现Result反射ABI不符，改为显式Pair/Boolean
  参数后完成构建及模拟器执行；没有降低验收条件或新增框架。
- 实际Gradle单测/两个APK构建通过：26套135项，133通过、2跳过、0失败；
  最终65任务12执行、53up-to-date。emulator-5554安装及ui_probe返回PASS。
  App SHA256 `8c2b5728e3336bec7cfbd7cd28538cf3bb9ebe7645ca70323bffad02fcfedeae`。
  对应`out/shaniu-product-acceptance-20260910/ota-inspection-{build-final,emulator}.log`、
  `ota-inspection-artifacts.json`及同目录保留的`ota-inspection-app-debug.apk`。
- NFC已有功能未重写。候选mcuboot/AP角色`3c9f0bc5aa792c33`的有效.config确认
  NFC_SERVICE、CL_ISODEP、CL_MFRC522_FRAME、PROVISION_GATT均启用。
  直接运行现有`test_isodep.py`、`test_isodep_apdu.py`通过；
  `pytest test_nfc_rf_lifecycle.py`通过；`make run-nfc-core run-nfc-rpc`通过，RPC19例。
  覆盖激活、APDU序列/分帧/重传、WTX期限及失败释放。日志同目录
  `nfc-isodep-{activation,apdu}.log`、`nfc-handover-host.log`、`nfc-service-host.log`。
  这是脚本化RF传输及主机执行，不是手机贴近实测，也不证明418已安装相同代码。
- 普通实现沿用已指定luna子代理（实际后端路由未独立核实），主代理负责生命周期
  集成与工具执行。未访问板子、生成密钥、供应身份、刷写、提交或推送。
  真实BLE/NFC、有效官方唤醒模型及语音/图片/OTA实板闭环仍未验收；
  下一真实认领步骤需要手机或可用宿主BLE与受控设备身份，不能靠取消认证绕过。

## 2026-09-11 直连云端激活入口修复

- 已退出旧目标，当前未创建新goal；继续AIDK傻妞，Dolphin成果保持原样。
  以下是最新软件检查点；未安装的新源码不能使用418旧证据代替验收。
- 实际COM8只读查询`/data/shaniu/identity/config.bin`及`/data/shaniu/config.bin`
  均返回ENOENT；`bkvoice status`仍为ready=1、configured=0、connected=0。
  未读取身份内容、写配置或复位。仅凭ENOENT不能证明父目录和挂载完全正常，
  也不将其归因于网络；证据`resume-20260911-provision-metadata.log`。
- 确认BPI1板端身份记录不包含Gateway路由，旧供应工具和App却要求Gateway地址与CA。
  复用现有四字段`provision-bootstrap-v1`，不新增板端协议：App正常入口接受
  device_id、certificate_sha256、possession_secret及protocol；仍严格拒绝混合、重复、
  未知或非法字段。旧八字段激活资料继续兼容，开发者Gateway模式仍要求其完整路由。
  正常云配置的CA继续由CloudEndpoint校验，不拿设备证书pin充当云端CA。
- `voice pairing --direct-cloud`明确选择无Gateway模式；沿用原BPI1负载和材料转换，
  在读取材料前拒绝非法COM或混合Gateway参数。默认旧模式保持8765端口及原路由要求。
  resume保持原proof及文件字节，跨格式/身份不匹配拒绝，保留防覆盖及文件权限检查。
  此命令会供应设备身份，本批仅用mock验证，**未实际执行身份供应**。
- 常规实现使用已有指定gpt-5.6-luna子代理，实际后端身份未独立核实；主代理审查
  身份边界及执行集成。未生成或使用设备/固件签名私钥，未访问旧418密钥或刷写。
- Android实际执行`:app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest
  --offline`：26套135项，133通过、2跳过、0失败。新增界面fixture语法错误已修正，
  最终构建65任务中6执行、59up-to-date；初次失败日志保留。
  App SHA256 `b29c816660b8fccb855f7db37db1225bc6eb9d658e7512216cd05595acd0cdc6`。
  指定emulator-5554安装App及测试APK，现有`ControlKeyInstrumentation -e ui_probe 1`
  返回PASS，覆盖无Gateway CA进入Wi-Fi页及此前取消回退。此为fixture界面验收，
  不代表真实BLE认领。现有`test_bk7258_voice_provision.py`9项通过，含原身份重试用例。
- 证据均在工作区`out/shaniu-product-acceptance-20260910/`：
  `direct-bootstrap-app-build-final.log`、`direct-bootstrap-app-artifacts.json`、
  `direct-bootstrap-emulator.log`、`direct-bootstrap-tool-tests.log`。
- 图片理解已有代码保留：明确语音触发词、真实取帧接口、取消/deadline隔离及JPEG清理；
  现有`make -C tests/host/bk7258 run-cloud-request`与`run-cloud-vision-runtime`通过。
  这些主机结果不能替代真实语音取帧、上传及播报。
- 本批未提交/推送。下一实板闭环仍需确认可用设备身份及受控供应、真实BLE连接；
  宿主扫描启动失败且手机缺席是当前射频验收限制，不能由模拟器替代。
  有效官方唤醒模型、真实图片理解与App OTA仍未验收。418签名部署独立待决，
  不作为后续App/源码工作的前置，不恢复旧密钥扩搜。

## 2026-09-11 从 Dolphin 返回傻妞的恢复点

- Dolphin 已独立提交并推送至 `fork/feat/bk7258-dolphin`，提交
  `e3fb5672b1ac63570ff9873d0f26d34b69876832`。本节之后的傻妞修改不属于该提交。
- 最近已有 AIDK 安装证据是 **418**，以
  [流程审查报告的418签名核查及计数证据](chip-board-wrapper-review-20260910.md)
  为准；下方411启动、417候选和当时等待备份的内容属于历史检查点，不能继续作为
  最新版本或日常开发前置。本次未访问AIDK串口，未核实当前在线设备/手机版本。
- 当前没有可用的匹配签名能力，限定查找已收口。418封存包保持原样；本批不查OTP、
  不搜索旧私钥、不生成身份或刷写。签名部署单独待决，源码、App和主机验证继续。
- 产品方向保持：K2电源、K1/K3音量；官方唤醒词触发自动收音；BLE/NFC认证认领、
  BLE配网及云配置；App首页固件更新。Gateway冻结，PTT仅内部调试用途。
- 本批从 App 的认领、配网提交与状态恢复开始核对实际缺口。常规实现复用已指定
  `gpt-5.6-luna` 子代理（指定配置沿用，子代理无法独立回报实际后端模型，路由未独立核实）；工具执行定向验证；主代理负责身份/设备边界和集成。
  不将模拟器或主机结果记为真实BLE/NFC、语音或OTA验收。

### 本批 App 恢复验收

- 用户说明手机未连接，改用现有 `emulator-5554`。Windows只读查询确认Qualcomm
  FastConnect 7800 Wi-Fi为Up、蓝牙为OK；未据此声称Android模拟器可访问宿主BLE。
- 定向代码核对未发现认领pending回执、提交清理、重启首页状态的新增缺陷，保留现有实现。
  `:app:testDebugUnitTest --offline`定向选取ProvisionClaimProtocolTest、
  ProvisionBindingStoreTest、CloudSettingsTest、DeviceStatusTextTest，共32项通过。
  `:app:assembleDebug :app:assembleDebugAndroidTest --offline`通过，60项任务均up-to-date。
- 指定模拟器安装两个APK后，运行现有ControlKeyInstrumentation `-e ui_probe 1`返回PASS：
  Android Keystore、加密pending恢复、pin拒绝、TLS分片及合成绑定UI通过；未启用cloud_probe。
  测试采用独立fixture偏好，不清空用户绑定，不对板子或长期签名身份进行操作。
- App SHA256 `524d487e6bcf000cc5c49ec8d129ce870d6a65b478daf53ef890bc5f57b175cf`；
  测试APK `0ecaa382700c6b3aea5f83ab6f53f0c07ec3659c70e3829e28cafc0d867989b7`。
  日志位于工作区 `out/shaniu-product-acceptance-20260910/resume-20260911-{config-tests,app-build,emulator}.log`。
- 本批没有新增业务源码或测试。真实BLE/NFC认领、板端配置重启恢复、有效唤醒模型、
  自动语音/图片理解及App OTA仍未完成；下一步先核对AIDK实际在线状态与现有公开接口，
  再选择无需信任迁移的真实配置联调；手机缺席时继续模拟器与宿主无线可执行部分。

### AIDK实时核对与恢复缺陷（2026-09-11）

- 用户要求退出旧Dolphin goal并继续AIDK傻妞。旧目标系统状态为blocked，当前工具无取消接口；
  不将其改成虚假的complete，不创建新goal。本批只推进傻妞。
- COM8 PNP为CH340 `VID_1A86/PID_7523`，与T5 COM3/COM4的CH342分离。
  一次只读`bkota status`确认`18.6.354+418`、A/confirmed、AP READY、CPU2 online=3、
  RPTUN connected、manager idle、faults/recoveries=0/0；无复位或刷写。
  `bkvoice status`返回ready=1、configured=0、connected=0、tls_available=1；
  不能从legacy状态字段推断完整认领身份是否存在，也不能将未配置判为云端网络故障。
  日志在工作区 `out/shaniu-product-acceptance-20260910/resume-20260911-com8-{status,voice}.log`。
- 复用已有Windows BLE客户端，定向扫描Shaniu时在watcher启动处失败，HRESULT
  `0x800710df`；蓝牙Radio=On、bthserv运行、适配器声明支持LE Central。
  未建立ATT连接，不能据此判定板端没有广播；未配对、未改无线开关或设备配置。
  证据`resume-20260911-ble-scan.log`，无成功result JSON。
- 软件核对发现身份加载/持久化成功后的绑定若暂时EBUSY，非空record会使后续恢复永久跳过。
  当前切片修复这一个恢复状态问题，保持未落盘身份不能启用、认证及OTA/关机排他门。
  已完成实现与下述主机/构建验证；尚不声称这是实板configured=0的根因。
- 官方唤醒KWS/自动VAD接线已有；AIDK有效配置仍未启用KWS和wake runtime，缺有效模型。
  不把SHA256校验称为独立签名，不用演示模型或主机fixture代替多人泛化/实板验收。

### 身份恢复修复完成检查点

- `bk7258_voice_runtime.c`新增identity_bind_pending，仅在身份读取/安装成功后绑定暂忙时进入；
  按既有1秒间隔重试，CONFIG_COMMIT保持EAGAIN，成功或永久错误结束等待，关闭清理状态。
  不重复读写身份，不对持久化失败的身份调用bind，保留OTA/关机排他检查。
- 在既有`test_bk7258_voice_provision.py`增加执行真实progress函数的回归，覆盖恢复及安装后
  暂忙、deadline前不重试、成功后不重复读写、落盘失败不绑定、永久错误停止；8项通过。
  `resume-20260911-identity-host.log`为实际日志。
- 同次AIDK构建暴露AP Wi-Fi的`dns_add_nameserver`隐式声明：条件包含发生在config.h前，
  因而被预处理跳过。仅调整`chips/bk7258/ap/bk7258_wifi.c`头文件顺序，复用NuttX正式声明。
- 复用`out/bk7258-workflow-a1-a8/build-command.json`进行public-only增量构建，最终通过，
  本次日志无warning/error；未clean、未用私钥、未改计数策略或封存包。
  `resume-20260911-identity-final-build.log`和`resume-20260911-identity-final-artifacts.json`
  记录最终构建及CP/AP ELF、清单摘要；先前identity-build/artifacts仅是头文件修正前的检查点。
- 新源码尚未提交/推送，未安装；实板仍是已确认418。真实恢复、BLE认领、语音及OTA
  不计通过。签名部署能力与宿主BLE扫描异常分别保留待决，不进行旧密钥扩搜或强制刷写。

### 配网恢复与取消后续切片

- 再次确认goal为空，后续为普通AIDK开发。保留此前身份恢复/DNS改动，不重复构建未变固件。
- Windows无遗留BLE客户端/广播进程；bthserv运行，BluetoothUserService_e808b停止。
  对该服务的Start-Service请求被Windows拒绝，未改启动类型、无线开关或配对；仅能确认
  宿主扫描通路仍未恢复，不能把服务停止当作已证实的唯一根因。不循环重试或复位AIDK。
- 配网生命周期沿用已有实现：失败/取消进入ABORTING，finish完成后释放；提交结果未知
  保留QUARANTINE。`python3 -m pytest -q tests/host/bk7258/test_provision_tls.py`通过，
  实际编译执行network等既有harness。日志`resume-20260911-network-lifecycle.log`；没有
  `run-provision-network` Make目标，不声称执行过该目标，也未新增测试框架。
- App恢复连接的取消入口存在缺口：resultButton关闭连接却停在连接中，goBack忽略
  recoveryControl。当前改动限定为取消/返回、回调隔离及保留pending回执，已完成下述模拟器验收。

### App恢复连接取消修复与模拟器验收

- `ProvisionActivity`将恢复连接的取消按钮、返回键和切后台统一到取消路径：先递增epoch，
  脱离连接引用再close，回到发现页并显示“已取消本次连接，认领结果仍需核对”。不删除
  pending回执、不清除bootstrap、不把取消解释为提交失败或成功；普通认领路径保留。
- `recoveryControl`仅消费close能力，类型收窄为标准AutoCloseable，实际仍为
  DeviceControlConnection；没有新增协议或mock产品入口。
- 复用`DeviceUiAcceptance`启动真实ProvisionActivity，仅替换连接对象与独立测试回执，
  驱动实际按钮和返回键，验证页面、文案、epoch/引用清理顺序、关闭一次及回执保留。
  这是模拟器UI/生命周期验收，不代表无线实际断连、真实认领或实板配置恢复。
- 当前模拟器原先已退出，确认无进程后启动已有Medium_Phone_API_36.0 AVD，未wipe数据。
  在emulator-5554安装新App/测试APK，ControlKeyInstrumentation `-e ui_probe 1`返回PASS。
- `:app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest --offline`通过：
  26 suites/133 tests，131通过、2跳过、0失败；65构建任务中12执行、53up-to-date。
  日志 `resume-20260911-cancel-app-build.log`、`resume-20260911-cancel-emulator.log`，
  产物清单 `resume-20260911-cancel-app-artifacts.json`，均位于工作区既有证据目录。
  App SHA256 `307375ce3dac14e33bb06e1a8c8050b26f3a65b9f355c06e66dbcda1c205f2d3`；
  测试APK `aadeb454a14ee85ecdb49f206752b3e896aae6f6ee3ebc021affe10c99403265`。
- 本批由已有Luna配置的子代理实施、主代理审查及独占模拟器验证；实际后端模型未独立核实。
  改动尚未提交/推送；此前固件修复仍未刷写。宿主BLE启动异常和实板签名部署单列待决。

## 2026-09-10 每板应用入口收束

正常AIDK CP入口由 `configs/openvela_cp` 迁为 `configs/app`，配置内容保持
逐字节不变；配套AP、分区与发布策略不变，`--board aidk_ai_toy` 自动选择新路径。
这只是配置组织整改，不代表新实板验收。历史417与其他产物路径保留作为原版证据。
小海豚只在T5-Board的app中启用；没有将傻妞产品替换为小海豚。

## 2026-09-10 发布当前进度并转入小海豚新固件

用户要求保存并推送当前项目进度，随后开启独立“小海豚”固件开发。此检查点不是傻妞完整验收通过：实板最新已核验411，417候选未安装；真实认领/唤醒/图片理解/App OTA及K2电流与唤醒仍待实物验收。Gateway保留已冻结代码，后续主线不恢复其部署依赖。

- 发布前重复执行产品键、认领、cloud/vision/memory、OTA事务、wake listener/owner/session、USB存储租约和reset主机回归，均通过；日志 `out/shaniu-product-acceptance-20260910/publication-host-tests.log`。
- Android `testDebugUnitTest`、`assembleDebug` 通过：26 suites、133 tests、0 failures/errors、2 skipped（跳过项不计通过）；日志 `out/shaniu-product-acceptance-20260910/publication-android.log`。
- 417双核构建及签名包证据沿用上节，未修改二进制或刷写设备。代码发布状态由实际Git提交/远端SHA确认，不把提交等同实板通过。
- 发布排除：735份原始诊断日志、两份drivercheck候选配置、3份引用已撤回容量/busy探针的失效测试。它们留在本地，不混入产品提交。凭据与私有语料不纳入。
- 新固件的需求与进度独立维护；复用已验证的BK7258板级/驱动与OpenVela生态，不把傻妞未验收功能自动标成新产品已支持。


## 当前交付状态（2026-09-10，覆盖下方历史检查点）

证据目录：工作区 `out/shaniu-product-acceptance-20260910/`。本节按当前版本判断；下方流水记录仅保留定位过程，不作为当前结论。

用户最新设备状态：已再次人工复位，COM8只读确认411/confirmed、manager idle、faults/recoveries=0/0（`board-user-reset-status.log`）。小米10已由用户拿走；ADB当前只有 `emulator-5554`。后续App开发使用模拟器，不再操作手机；物理BLE/NFC与手机OTA继续单列待验收。现有复位证明正常启动，不等于Loader读取窗口已捕获。

模拟器当前APK `524d487e6bcf000cc5c49ec8d129ce870d6a65b478daf53ef890bc5f57b175cf` 已安装；Keystore不可导出密钥、加密待定事务恢复、认证证书pin、TLS分片/错误pin拒绝、合成设备状态UI通过，见 `emulator-ui-security-result.json`。合成状态不构成真实认领证明。

模拟器后续验收：新增instrumentation从实际首页进入“固件更新”，未连接/旧能力快照禁用升级；使用唯一测试偏好调用真实 `confirmExpectedOta()`，INFO版本/计数不匹配拒绝、匹配才成功、终态-5显示失败均通过。记录 `emulator-ota-ui-result.json`，测试APK SHA256 `0ecaa382700c6b3aea5f83ab6f53f0c07ec3659c70e3829e28cafc0d867989b7`。未创建连接/上传或触碰真实绑定。416包的模拟器HTTPS来源尝试在 `open_wifi` 被拒绝：当前仅eth0/10.0.2.15，没有Wi-Fi IPv4；`emulator-ota-source-416-result.json` 为失败记录，不冒充手机来源或实板OTA通过。

| 需求 | 当前状态 | 证据与下一动作 |
|---|---|---|
| 实板与恢复路径 | 实板通过（仅411启动健康）；候选安装外部阻塞 | 软件复位后仍为 `18.6.347+411`、A/confirmed、faults/recoveries=0/0，见 `board-after-readback-probe.log`。Loader read 不支持 swrst；软件复位后立即 read 仍未取得总线，未读出文件、未写 Flash。等实际 RESET/CEN 配合完成两份一致的8MiB恢复备份，不能把进程退出0当读取成功。 |
| 候选固件 | 自动化通过；未安装 | `18.6.353+417` 双核增量构建、包校验及签名验证通过，见 `candidate417.json`；包 SHA256 `7c9485e5005f6d46cc2d956c3215d54d92833d598fe2352e7589b9fbbf04c0cd`。413–416候选与证据保留，均未安装；默认KWS、wake runtime、AEC关闭，不能据此称唤醒可用。 |
| K1/K3音量、K2电源 | 已实现未实板验证 | 417新增K2长按3秒松开→业务排空/存储同步→CP整芯片reset-to-super-deep→P12模拟GPIO唤醒；K1/K3音量保持。尚未安装，未测休眠电流/唤醒；不代表物理切断VBAT。 |
| BLE认领、配网、配置恢复 | 已实现未验证 | 自动受控发现与认证/提交恢复已实现并有主机测试；新固件上的真实认领与重启恢复未验收。 |
| NFC入口 | 已实现未验证 | 真机小米钱包模式 operation=-93，临时HCE钱包模式=-110，均未收到选择事件；已恢复原钱包模式并读回，见 `nfc-route-experiment.json`。不能算射频成功。 |
| 唤醒、自动语音及打断恢复 | 未实现（组件自动化通过） | 可信模型session与runtime接线、listener/owner生命周期和真实cloud异步取消排空均已实现；根代理回归与启用分支ARM对象编译通过，见 `wake-session-integration-host.log`、`wake-session-integration-arm.log`。仍缺有效模型、采播并发与真实唤醒/语音验收。 |
| 语音拍照理解 | 已实现未验证 | 真实取帧上传路径有源码及主机检查；真实说话触发、上传、理解与扬声器未验收。 |
| App固件更新 | 手机来源组件自动化通过；整链已实现未验证 | 小米10当前APK SHA256 `524d487e6bcf000cc5c49ec8d129ce870d6a65b478daf53ef890bc5f57b175cf`，文案修复安装及回读哈希通过（`phone-product-copy-result.json`）。HTTPS源验收对应此前APK `ce40687a1dcef6403ef2770436e9e959417047c557cfa56834e6e7d8c37cd46b`：159次请求、2576998字节及包哈希、错误信任/主机名拒绝、清理均通过，见 `phone-https-source-result.txt`。未执行App→实板升级/重连版本确认/回滚验收。 |

415新增安全边界：Flash写入前，AP owner必须先持久化已验证的目标身份，worker收到批准才能读镜像；取消与批准竞争、暂存提交状态不明均保留恢复记录。主机并发/持久化/恢复测试及完整构建通过；真实断电、丢失完成通知与回滚试验未执行。

唤醒打断的独立缺口：当前 `bk7258_voice_turn_audio.c` 的 MIC/DAC acquire 双向互斥，`wake_owner` 在 cloud busy 时停止监听。因此单轮自动收音接线不能证明播放中声学打断；不得仅删 busy 判断或另建上下文绕过仲裁。芯片 `bk7258_media_root.c` 同样只有单一 ADC/DAC owner，`chips/bk7258/Kconfig` 明确共用 AUD clock root；AIDK MIC2 为扬声器参考，产品 defconfig 尚关闭待AP-SMP验收的预处理。最小下一步是复合会话的时钟/PM、两路DMA和停止顺序验证，再复用 MIC2/AEC 完成近端唤醒与远端自触发对照；此项仍属必交验收，未降为可选。实板恢复与当前候选安装前不直接移除芯片互斥。

App产品指引再次校正：删除首页、交互页、设备状态的4条“按住说话键”残留，现有状态文案测试与APK构建通过；实际安装小米10并回读APK哈希一致，当前App可见且无此提示。未把“网络已连接”改写为“唤醒已可用”。

OTA source主机安全回归新增并由根代理复核通过：真实签名与预期摘要绑定、签名篡改拒绝；真实MbedTLS回环HTTPS正确CA/hostname成功、错误CA/hostname拒绝。记录 `ota-source-auth-host.json`、`ota-source-tls-host.json`，脚本 `tests/host/bk7258/test_bk7258_ota_source_http.py`。同步POSIX回环桩不覆盖NuttX/RPMsg，也不证明此前实板-EIO根因已解决。

最新接线修复：`bkcloud_runtime_cancel()`只发布取消标志，关闭路径必须驱动既有cloud step排空。新增最小 `cancel_drain()` 并以真实cloud runtime验证延迟worker释放，避免OTA/配置/关闭早退造成永不空闲；失败时保留对象，外部非wake会话不误取消。固定路径模型有界读取、SHA校验、FIFO非阻塞拒绝、缺模型不占MIC/不刷屏/不阻断内部调试入口均有主机证据。416包含默认配置下的集成，启用KWS分支仍只有主机与ARM对象证据。

### 2026-09-10 K2低功耗关机实施检查点

- 根代理负责芯片/板级/安全和集成；复用已指定Terra的按键代理与Sol的AP代理，未新增代理。后端实际模型路由未验证。手机/App物理联调仍按用户指示暂停。
- 板级传入P12/低有效唤醒映射；按键长按3秒只锁存，全释放才提交。断链/组合键/时钟回退不会误提交。`run-product-keys`通过，日志 `/tmp/shaniu-product-417-keys-test.log`。
- AP持有SD块设备使用租约，MSC导出拒绝关机；OTA未恢复、持久intent/作业、认领/配置事务未闭合时拒绝。排空wake/cloud/legacy音频后sync，再向CP请求。接受或通信结果不确定时保持关闭业务，只有CP只读status明确空闲才能恢复，不能按超时猜测。
- CP使用官方 `RESET_SOURCE_FORCE_DEEPSLEEP=0x19`，只接受confirmed固件的请求。LPWORK同步文件系统并取得Flash事务guard后整芯片重启，避免单独停止AP留下CP Wi-Fi持有的旧指针。早启动消费意图，在AP/无线/SD启动前复用SDK `pm_hardware_init`、模拟GPIO唤醒和super-deep入口；进入失败则普通重启，不循环睡眠。正常NuttX PM_SLEEP仍保持既有保护。
- SDK回调会处理GPIO16=SD_D0，因此睡眠仅在冷启动阶段进入。SDK/NuttX源码未改动。主机reset测试验证功能开/关两种reset reason白名单；不作为物理睡眠证据。
- 417双核增量构建、包校验、MCUboot CP/AP签名验证通过；证据 `out/shaniu-product-acceptance-20260910/candidate417.json` 及同目录 `candidate417-*.log`。当前未安装，板端仅保留411既有证据；没有本轮真实K2操作、关机电流或唤醒证据。
- 下一动作：417候选包已校验，按既有恢复保护完成安装与长按松开/再次按K2/启动版本/电流验证。整片下载仍缺本机完整备份，手机OTA物理联调仍暂停，不以绕过这些条件换取“通过”。

### K2/P12 电源能力核对（源码结论，未实板执行）

用户补充引脚图后重新核对：KEY2=P12。SDK `cp/include/driver/gpio.h:493` 明确模拟GPIO唤醒接口专用于super deep sleep；`cp/middleware/driver/gpio/gpio_driver_base.c:1227` 接受GPIO0～15及高/低电平，`cp/middleware/soc/bk7258/hal/sys_pm_hal.c:1133` 将0～15选择值写入模拟唤醒寄存器。因此P12具有超深睡按键唤醒的源码依据，不能因缺少VBAT锁存就判定必须先改硬件才能实现产品开关机。

现有适配缺口：实际链接的 `v3.1.1.9/cp-aidk/config/sdkconfig.h` 已开启 `CONFIG_PM_SUPER_DEEP_SLEEP` 和 `CONFIG_GPIO_ANA_WAKEUP_SUPPORT`，未开启 `CONFIG_DEEPSLEEP_USING_WDT_PROTECT`；此前仅看通用默认Kconfig的结论已纠正。NuttX `chips/bk7258/cp/bk7258_pm_policy.c:60` 主动拒绝PM_SLEEP，产品K2路径目前返回ENOTSUP。应先复用SDK机制完成CP拥有的低功耗关机/唤醒接线，安全停止AP/外设、保存数据，释放K2后再使能低电平唤醒，核对SDK回调对GPIO16的处理，并实测电流与再次启动。未改SDK/NuttX代码，也未向实板发送睡眠/关机命令。

此结论支持低功耗软关机候选，不证明VBAT被物理切断或整板达到指定功耗；真正断开电池供电仍是独立电路问题。纠正此前“必须新增锁存才能做K2开关机”的过度结论，保留真实电源行为验收标准。手机相关联调继续暂停。

### 恢复入口与剩余外部条件

- **恢复备份/新版本实板验收阻塞**：两种自动read路径均未读出数据，且当前恢复基线不能代替本机新备份。等待一次真实RESET/CEN配合，在读工具进入Getting Bus后操作；不是中间K2。先取得两份8MiB且哈希一致的私有备份并核对设备，再按板规则构造恢复/安装路径；不提前启动无限等待或重复写入。用户随后报告已复位，已确认411正常启动；本次没有与运行中的Loader读取同步，完整备份仍未取得。
- **官方唤醒与声学打断未完成**：需要有效“你好，openvela”模型及真实多人/噪声输入；当前9条合成管线fixture不可发布。还需恢复后核验芯片ADC/DAC复合owner、时钟/DMA/AEC实测，之后才能开放播放中声学打断。不得将半双工单轮测试算通过。
- **K2电源行为未完成**：P12超深睡唤醒已有SDK源码依据，先完成低功耗开关机适配及电流/再次启动验证；物理切断VBAT另需电路，不能将两者混同，也不预先断言产品开关机必须改板。
- **完整流程仍待实物输入**：新版本BLE认领/配网重启恢复、NFC选卡与保护、真实说话拍照理解、App OTA/失败/回滚均未验收。恢复后先认领与配置，随后语音/照片，再App更新及安全回归；不使用旧411证据覆盖新版本。

目前没有提交、推送或发布。交付状态为未完成；已保留候选固件、实际安装App与证据。物理准备到位后从本节继续，不再重跑已通过且未变化的主机基线。

## 2026-09-10 先前执行检查点（历史，实测与源码分开）

- 当前只读实测：COM8 返回 `18.6.347+411`、A/confirmed、counter=411，supervisor faults/recoveries=0/0；
  小米10（59d707dc）和模拟器均在线。未安装412，也未重做其验收。记录在工作区 `out/shaniu-product-acceptance-20260910/baseline.json`。
- 原理图核对：KEY1=P13、KEY2=P12、KEY3=P8，均按下接地；另一个 RESET 器件的位号 K1 接 CEN，不能和功能键KEY1混用。
  原理图第2/3页显示主芯片 VBAT 直供、无 K2 整机电源锁存；外设 LDO33_EN 不切断主芯片供电。
  真断电能力尚不满足；最小硬件方案为可按键启动、GPIO保持/释放的电源锁存或负载开关，并需实测关机电流/再次启动。
  不把未测深睡或仅关闭外设标为真断电，当前不执行可能丢失软件恢复入口的电源实验。
- 首次自动认领 owner 代码与限定主机测试通过：仅存储明确 `-ENOENT` 时开放，有界窗口/退避；身份认证后授权，
  不再依赖 PTT 链路/长短按。App 首次认领提示已同步，相关单测与 APK 构建通过（`/tmp/shaniu-auto-claim-android.log`）。尚未部署或手机认领。
- 三键标准 `/dev/buttons` 绑定已写，GPIO层用既有芯片接口；轮询模式不提供中断通知。产品键事件/音量处理正在接入。
- 提交结果恢复仍在改造：优先使用与待定事务绑定的密封控制密钥、固定证书 TLS 认证证明已提交；明确未认领设备仅允许认证后的只读回执查询。
  失败不得清空待定事务，不以删除认证或8秒PTT流程替代恢复。
- 三键产品配置已同时接入 CP/AP，发布端不再启用诊断 PTT；主机键策略通过。413 clean 目标构建失败于 CP GPIO 输入接口仅有 AP 实现，正在补芯片 CP 适配；未签名、未安装。
- 提交恢复 App 单测与 assembleDebug 已再次通过；已提交事务以对应控制密钥认证确认，本机提交失败保留未确认，退出/取消使旧回调失效。移除8秒指引。仍未实机认领。
- OTA 实际缺口复核：当前 App 升级页仍读取 Gateway 发布列表，不能算直连升级已实现。后续复用设备 HTTPS OTA source/manager，BLE 仅传认证控制，Wi-Fi 拉取大包。
- 413 最终 clean 构建、包/签名校验通过（包 SHA256 `6df73838c8348fd9e8ce8a2a16144ab69211e59f61aceb90376d65a595e8969a`），CP/AP 产品键实际配置均启用。
  两次 HTTPS 部署失败：首次 CP 暂存 progress=131072 返回 -EIO；降低服务器发送突发后第二次 AP progress=262144 返回 -EIO。均未发送重启，411继续运行。停止HTTP写入重试，下一步隔离下载源与Flash暂存；临时HTTPS已关闭并移除私钥。
  日志 `/tmp/shaniu-413-ota-live.log`、`/tmp/shaniu-413-ota-r2.log`，候选与App摘要 `out/shaniu-product-acceptance-20260910/candidate413.json`。
- 小米10实际安装新版App并进入添加页，BLE查找/NFC碰一碰入口可见且无长按PTT提示（`phone-discovery-entry.json`），仅UI入口通过，不算认领/NFC射频成功。
- 后续源码新增显式自动语音begin/end接口，复用capture仲裁，预填失败保留借用直到取消join，不上传失败回合；cloud/runtime、vision、memory主机测试通过。这些后续源码不在413中。
- 部署隔离补充：本地HTTP对照被411协议策略拒绝（-EPROTONOSUPPORT，phase=0），未进入写入。SDNAND经MSC新增 `/ota413` 公开签名文件并主机校验后安全弹出，USB已恢复CDC；411缺少直接挂载该AP目录的控制入口，尚未执行file OTA。两次HTTPS的-EIO根因仍未确认，不继续盲目写入重试。状态证据 `deployment413.json`。
- 官方唤醒资产复核：仅找到9条合成样本、1 epoch的既有管线测试模型，不能视为可交付唤醒模型。官方推理运行时可复用；有效模型及真实多人/噪声验证仍缺。
- 本轮工具模型已指定 Luna low 与 Terra medium；实际运行模型未独立可核验。goal 工具仍绑定旧 blocked 目标且拒绝新建，
  未虚报旧目标完成，工程工作继续以本计划恢复。新增源码未提交/推送。

- 自动语音基础补充：连续MIC listener生命周期主机测试通过、listener/HTTP source及最新INFO runtime已用实际AP ARM编译命令编译通过（仅对象编译，不是完整固件或实板证明）。有效唤醒模型与owner接线仍缺。
- INFO=9受认证版本查询及旧固件能力位兼容单测、APK构建通过（`/tmp/shaniu-info-android.log`）；手机已安装此APK。真实首页检查发现更新入口仍只在设置页，已补首页入口但该补丁尚未重装。
- App本地bkpack检查/选择入口正在验证：读取实际catalog与镜像哈希，不将完整性冒充签名信任；第一次单测编译失败已修，第二次发现Android JSON桩依赖，正改用已有Gson。未开始App OTA，未修改实板411。

- 手机本地固件检查已实际通过：首页下方“固件更新”→系统选择器→真实413包，显示 `18.6.349+413`、aidk_ai_toy、镜像2576384字节；不把完整性当签名。证据 `out/shaniu-product-acceptance-20260910/phone-package-inspection.json`，包含安装APK哈希。尚无手机发起升级或板端版本切换。
- SOU1请求解析与SDC1 OTA 10–14分片/认证/错误保留/清理测试通过；C端TLS控制、云语音/图片/记忆回归通过（`/tmp/shaniu-ota-control-host.log`）。Android OTA协议限定单测通过；尚未接实际升级。
- HTTPS源新增独立服务器认证入口，原双向TLS入口仍强制客户端证书；主机公开头98项和AP ARM对象编译通过，TLS连接及签名/摘要不匹配运行测试尚未执行。正接既有OTA manager/store，未改SDK/NuttX。
- 云服务未创建时的STATUS可继续提供本机能力/网络快照，空云会话turn明确未知，不伪报idle；相关云运行期回归通过。所有这些后续源码均不在实板411或签名413包中。

- 手机 HTTPS 来源实测已开始，输入包通过 ADB 文件传输后 SHA256 与 413 发布包一致。首轮 stdin 复制损坏已隔离；正常 TLS 连接仍在 catalog 握手阶段失败，尚未通过手机 HTTPS 验收。记录 `out/shaniu-product-acceptance-20260910/phone-https-source-result.txt` 与 `phone-https-source-apks.json`；未触发板端升级。
- App 已接直接 OTA 分片上传、独立状态、期望版本/设备绑定持久化、防重复启动及前后台传输中断提示；成功判定必须实际 INFO 与 CONFIRMED 阶段一致。限定单测与 APK 构建通过，手机尚未安装这批 UI 后续修改，不能当作 App OTA 验收。
- 设备直接 OTA 已复用既有 manager/store，移除该路径对 Gateway 配置的依赖；审查修复已暂存/等待重启时的取消行为，无法撤回返回 -EALREADY 并保留记录，不伪报取消。取消策略主机测试与实际 AP 对象编译通过，完整新固件构建/安装和物理验收仍缺。

- 414 候选 `18.6.350+414` 完整构建、包校验和 MCUboot CP/AP 签名验证通过（SHA256 `78d6b28afa9665b44df14c48ef8884126990365f51a522d7e404d8e44a0af522`）。随后审查补上重启恢复的 durable staged intent 取消保护，414因此不安装；新改动已通过取消主机测试和AP对象编译，需下一候选。记录 `candidate414.json`。
- 手机TLS第二轮仍在正常catalog握手失败，异常为 CertificateException 且证书仅CN；正在补SAN证书并同条件复测，不取消TrustManager或主机名检查。COM8再次只读确认411/A/confirmed/faults0/recoveries0，日志 `board-after-phone-https.log`。

- 小米10 HTTPS 来源实测通过：159请求/2576998字节，真实413包 catalog/AP/CP SHA256全部一致；错误主机名与无受信证书拒绝，阻塞客户端关闭及唯一临时密钥/解包目录清理通过。SAN证书修复保持系统TrustManager/hostname校验，App实际安装哈希 `ce40687a1dcef6403ef2770436e9e959417047c557cfa56834e6e7d8c37cd46b`。证据 `phone-https-source-result.txt`、`phone-https-source-apks.json`。这是手机自动化通过，不是实板OTA；实板仍411。
- 自动唤醒会话owner已新增并有主机生命周期测试，正在补终态锁存/停止等待期间追加帧与目标ARM适配。有效模型、runtime接线、真实唤醒及语音图片验收仍缺；未使用模拟输入冒充物理证据。

- 自动唤醒owner已通过真实window端点的主机回归：END后追加帧与auto_end等待、空唤醒后重监听、MIC初始化失败清理及1秒退避恢复。已加入KWS条件构建并用实际AP编译命令编译通过；有效模型和runtime接线仍缺，不能声称唤醒可用。日志 `wake-owner-host.log`。
- OTA恢复审查确认CP可启动提交早于AP STAGED记录的断电窗口；正在将已验证目标身份提前到Flash写入前由AP owner持久化，worker等待明确批准，任何不确定提交结果保留intent。此修复未完成前不安装候选，也不以返回取消代替回滚证明。

- 当前手机App已真实进入NFC_ARMED，411执行一次HCE返回 `rpc=0 operation=-93`，手机未收到selection；与旧超时区分记录，尚不认定射频或认领成功。日志 `nfc-real-once-board.log`/`nfc-real-once-phone.txt`。正核查选择结果到ISO-DEP激活的SAK处理，未去除协议或身份校验。

## 产品要求校正与当前交付门（2026-09-09，权威）

以下要求覆盖本文更早的 PTT 优先、有限 PTT gate、Gateway 依赖和“可选/后续”措辞；旧文字与证据保留为历史记录，不能作为当前产品范围或完成声明。

| 产品要求 | 当前状态 | 当前证据/缺口 |
|---|---|---|
| 中间 K2 为电源键（实测区分真正断电与待机）；左 K1 音量减、右 K3 音量加 | 未实现 | 需要按键映射、真实断电/待机读回及实板观察；不能沿用旧 PTT 映射 |
| 官方唤醒词“你好，openvela”→自动录音/端点→MiMo→TTS；重唤醒、打断、离线恢复 | 未实现 | KWS/端点/会话接线及实板连续、打断、断网恢复均缺；主机测试不构成产品验收 |
| PTT 仅内部调试入口 | 已实现未验证 | 仅可作诊断/回归辅助，不能作为产品能力、验收前置或发布 gate |
| 未认领设备自动受控发现；BLE/NFC 共享认证认领与安全；BLE 配网及 Wi-Fi/云配置持久化 | 已实现未验证 | 现有 BLE/NFC/持久化各有源码或主机证据，认证联动、重启恢复和实板认领仍缺 |
| NFC 必须提供物理证明；“拍照看看/看看眼前”→真实 JPEG→云端→TTS | 已实现未验证 | 412 主机/签名候选证据通过，未安装；411 无实板图片理解或物理 NFC 证明 |
| App 首页下方固件更新入口；签名/兼容 OTA 进度、错误、重连、版本、回滚 | 已实现未验证 | 底层 OTA 与 UI/主机检查存在，受信源、跨重启状态及实板升级/回滚证据缺 |
| Wi-Fi 承担固件等大包传输，BLE 承担发现、配网和必要控制；不要求 Gateway | 已实现未验证 | App 发起升级、断网恢复尚未闭环；不新增批量设备控制范围 |

状态含义：`未实现`=尚无可交付实现；`已实现未验证`=源码/主机或候选包已有证据但无规定实板/物理验收；`自动化通过`=目标自动化检查通过；`实板通过`=指定版本和步骤的物理证据；`外部阻塞`=需用户、设备、服务或环境输入。当前版本与证据以本文顶部“当前交付状态”为准；主机镜像/运行测试通过不改变实板验收状态。

历史基线日期：2026-09-02；本轮执行顺序更新：2026-09-07（第 17.5–17.6 节，复用边界、功能完善计划与实施结果）。

目标产品（2026-09-09 修订）：运行于 AIDK AI Toy 的虚构 AI 伴侣“傻妞”，板端直接
调用可配置的云端模型（首个适配为 MiMo），Android 负责认领、配网、凭据配置及设备控制。日常 MiMo 对话不依赖
额外 Gateway、电脑或手机常驻。本节新决策优先于下文历史 Gateway 实施记录。

## 当前检查点（2026-09-09）

- **已部署且健康：**签名 HTTPS OTA `18.6.346+410` 与 `18.6.347+411` 均已安装并自动确认；当前为
  `18.6.347+411`，CP/AP/CPU2、RPMsg 与 supervisor 均健康，`faults=0`、`recoveries=0`。410/411 的精简证据在工作区
  `/home/lijian/project/open-vela/out/shaniu-acceptance-20260909/`（411 为 `ota411.log`）；原始 411 串口记录为
  `/tmp/shaniu-411-ota-live.log`。这只确认该 OTA 及启动健康，不替代产品功能验收。
- **本轮完成：**411 修复了 NFC service 对旧 `CL_MFRC522` 的依赖，改为匹配板载
  `CL_MFRC522_FRAME`，并补齐 CMake/Make 的公开头路径及延迟初始化重试。实板启动出现
  `BKNFC SERVICE READY`（0.47 s）；初始 idle 为 pending `-2`，注册后 4.15 s 报告 field released，证明服务
  已启动并执行关 RF。手机 NFC 保持开启，间隔 20 秒两次确认 App 保持前台；随后通过真实 UI 一次进入 `NFC_ARMED`。但 `bknfc hce` 超时 `-110`、随后
  `scan present=no`，未获得选卡/APDU 或认领成功；不归因硬件。NFC 仅辅助定位/认领联动，不作为认证。
- **已恢复基线：**经授权清空 SD 后，FAT 卷标为 `SDNAND`；项目默认显示包已恢复并完成 SHA-256 核验，双屏
  `READY` 且 `last_error=0`。左右视觉映射仍未验证；卷标变更本身没有证明或修复此前 `ENOTDIR` 根因。
- **411 只读功能验收：**复用现有七项 AIDK HIL 合约，AP 健康、Wi-Fi 原生路由、语音状态、双屏状态、电池/温度读取、加速度采样、NFC 在场查询共 11 步全部通过。机器结果为上述工作区证据目录的 `aidk411-safe.json`，串口为 `aidk411-safe.log`；不替代声音、屏幕映射、传感器标定或已知卡片的物理观察。HIL 成功条件已按源码枚举由错误的 `link=2` 改为严格 `link=3`，继续拒绝连接中与未连接状态，5 项主机合约测试通过。
- **仍阻塞产品闭环：**板端 PTT 服务为 `link=1`，但尚无云配置或 Cloud 状态。下一实板步骤是既有 BLE
  认领→云配置提交→一次真实 PTT→MIC→MiMo→DAC 闭环；随后各执行一次取消与断网释放。Windows 原生
  `192.168.1.2:18406` 已完成 32-byte TCP 回显和 HTTPS OTA，WSL 服务路径失败，不能据此归因全部网络问题。
- **范围与发布：**比赛版保持 MiMo 为主；IndexTTS 私有模型是后续路线，不新增声音切换入口。新增改动尚未提交或
  推送。模型路由分别指定 Terra（盘点/进度）与 Luna（局部 HIL 合约修复），实际运行模型未独立验证。

有限验收标准是：同一已确认镜像完成一次非零 MIC、MiMo 回复、可听 DAC 播放和播放后短期上下文提交，并在取消与
断网各一次确认 MIC/DAC/worker 有界释放；BLE 认领须另行验证提交和重启恢复。未通过的 KWS、长期记忆、NFC APDU、
视频和 AEC/并发能力继续关闭或不声明。

## 当前续作：等待实体认领期间的独立任务

- 411 最新只读串口仍为 PTT link=1、pressed=0、presses=0；未观测到本次启动后的按键输入。
  已请求真实长按 3 秒后松开，不通过软件注入替代物理认领验收。
- 拍照理解先补通用 Chat Completions JPEG 请求/客户端，复用现有 HTTPS、截止时间、取消和回复解析；
  图片上限 512 KiB，借用不可变 JPEG，按块 Base64 编码，不保留完整编码副本。
  请求/客户端已实现，独立 JSON/Base64 分块测试和真实 OpenVela webclient 的本地 TLS 测试通过；
  AIDK CP/AP 目标构建通过（`/tmp/shaniu-image-client-build.log`）。尚无实板图片理解证据。
- AP 本地 JPEG 复制接口已实现：复用同一 V4L2 capture helper，互斥 trylock 使 snapshot/record 不并发占用相机。
  容量不足拒绝，退出时释放 V4L2；关闭失败则擦除已复制数据。`run-vision-core` 主机测试通过，尚未单独实板验收。
- 语音入口限定为用户明确说“拍照看看”或“看看眼前”，仅忽略外围空白/结尾标点；否定句和普通谈话不触发相机。
  已接入现有云 worker 的 ASR 后阶段，复用同一 TTS、取消、播放完成和历史提交；不由模型回复触发拍照。
  `run-cloud-runtime` 与 `run-cloud-vision-runtime` 两种宏配置均通过，覆盖否定句、采集失败、采集后取消、播放后提交。
  取帧前后均检查取消/截止；本地 capture 复用已有屏幕拍照反馈，`run-vision-feedback` 通过。
- 新增功能已完成独立 counter=412 的 AIDK 构建与签名 OTA 发布校验，版本 `18.6.348+412`；
  候选包为工作区 `out/shaniu-vision-412/package/firmware-aidk_ai_toy-v18.6.348+412-ota.bkpack`，尚未安装。
  最终 AP ELF 保留 capture/image/client 三个调用符号；证据位于工作区 `out/shaniu-acceptance-20260909/vision412-*`。
  板端仍为原已签名 411，最后检查 PTT presses=0、pressed=0；无云配置，真实认领/语音/图片上传未验收。
- 现有 `bkvision-v2` snapshot 只返回元数据，在关闭 V4L2 前未向云端转交像素。
  产品接入须在 AP 内由现有相机 owner 交付有界 JPEG，再释放相机并由云 worker 上传；
  不通过 RPMsg 传递地址，不新增并行相机 owner，不延长 V4L2 缓冲持有到网络完成。
- 接口依据：<https://mimo.mi.com/docs/en-US/quick-start/usage-guide/multimodal-understanding/image-understanding>，
  本日官方文档列出的图片理解模型为 `mimo-v2.5`；通用配置继续由使用者选择具备图片能力的模型。
  只有请求测试通过不能宣称拍照理解或当前 PTT 主线已完成。

## 历史证据（截至 411 前）

下列按版本叙述保留为源码、主机测试和当时实板过程的历史证据；与上述当前检查点冲突时，以上述当前检查点及其日期化
证据为准，不得把历史构建或旧版本状态表述为新的实板验收。

复位后实板核对（2026-09-09）：COM8 在 115200 下返回 NSH，安装版本为
18.6.336+400，A 槽 confirmed、counter=400；AP supervisor HEALTHY，
faults/recoveries=0/0，CPU2 ready=1。`/data/shaniu` 仅有 identity 目录，
其中 config.bin 为 719 bytes；未读取凭据正文。当前没有 `BKVOICE CLOUD`
状态行。源码中 configured/connected 属于旧 session，不能单凭这两个零值
判断直连网络失败；产品配置根为 AP `/cpdata/shaniu`，映射到 CP `/data/shaniu`。
下一实板步骤应恢复认领与云端配置路径，再验证真实语音，不重复排查已恢复的串口。
本次只读检查未复现挂起，也未证明此前无响应的根因。

首次认领按键启动缺口（2026-09-09）：实板 `ps` 无 CP 按键采样线程，
`bkvoice status` 的 PTT link=0。源码仅在 connect 命令启动采样，连接失败和
disconnect 又会停止它，rcS 未启动，因此未配置云端时无法通过长按发起认领。
新增产品版 PTT_AUTOSTART 和幂等 buttons 入口，由 AIDK openvela_cp rcS
启动真实 GPIO 采样，连接失败/断开不再停止产品按键服务；drivercheck 行为保留。
AIDK CP/AP 构建通过，已检查生成 rcS 含启动命令；尚未安装此修复，不能标记
实板按键恢复。小米 10 已重新连接 ADB，新版 App 更新安装成功，应用数据保留。

402 安装尝试：18.6.338+402 签名 OTA 发布校验通过，但 COM16 握手超时，
板端 manager 保持 idle、progress=0/0，未开始写入。随后软件 reset reboot
成功返回 NSH；启动日志明确 USBCDC consumer=/dev/ttyGS0，当前构建配置
未启用 CONFIG_BK7258_OTA_SOURCE_USB。普通 CDC 枚举不代表 OTA 服务存在，
不能据此重复 USB OTA 尝试或归因硬件。402 仍未安装，应使用已支持的更新路径。

403 实板更新：新独立 BL1/MCUboot 密钥构建并签名的 18.6.339+403 全量包，
通过 COM8 BK Loader 软件复位接管、擦除、写入成功。启动查询确认 pair=confirmed、
counter=403、AP healthy 且 faults/recoveries=0/0；无需云配置即出现 bkvoice-button
线程及 PTT link=1、pressed=0，修复前为线程缺失、link=0。这证明开机按键租约
恢复，不替代实际长按认领验收。原身份 resume 事务被 ready 阶段以 -EBUSY
拒绝，未重复供应，继续核查身份/配网 owner 状态。

404 实板更新：发现 CP provision 在按键线程运行时无条件拒绝，因此产品常驻
模式跳过该旧检查，仍由 AP CONFIG_BEGIN 的真实事务忙状态守护；诊断模式保留。
新独立签名全量 18.6.340+404 下载、启动 confirmed，AP healthy、PTT link=1。
身份供应已通过 ready/传输，commit 返回 -ENODEV。实板 mount 仅列出 /etc、
/proc，/dev/mtdblock0 存在而 /data 未挂载；手动执行启动脚本同一普通挂载
返回 errno=14 (EFAULT)，未格式化。当前下一阻塞点是片内 LittleFS 挂载，
不是 SD NAND 容量/一致性，也不能将身份事务记为完成。

404 私有存储恢复：NuttX 将 LFS_ERR_CORRUPT 映射为 EFAULT；检查已下载镜像
0x600000 起 1 MiB persistent_data 全为 0xff，原因为采用空白基底后未初始化，
并非已证明的 SDK 读错误。仅对该已知空白片内分区执行一次 LittleFS 初始化，
未格式化 SD NAND、未修改 SDK/NuttX。原身份供应随后成功（655 bytes）；
再次软件重启后 /data 自动挂载为 littlefs，identity/config.bin 仍为 719 bytes。
凭据正文未回读。身份持久化已通过本次重启验证；App 认领及云端配置仍需继续。

小米 10 认领界面：实机进入首次添加页面并成功启动 BLE 扫描，无权限拒绝；
未开启实体认领窗口，不能据此判定设备发现或认领成功。补齐首次添加的
“空闲时长按说话键 3 秒后松开”和连接确认的“先松开再短按”提示，恢复流程
仍保留 8 秒说明，不用首次添加时长替代恢复操作。APK 构建通过。

NFC 辅助认领开始实现：现有 MFRC522 为读卡器，手机采用 Android HCE 卡模拟，
不是让手机直接读取板子为标签。当前 NuttX MFRC522 接口只有选卡/UID 和 MIFARE
操作，尚缺面向 HCE 的 ISO-DEP/APDU 交换。Android 新增非支付 HCE 服务，
只在认领页面前台且设置 preferred service 成功时响应；离开页面立即关闭应答，
要求手机解锁。当前 SELECT AID 只返回协议版本，不发送凭据、不授予认领权限。
命令截断、变异、额外数据、非前台拒绝测试及 APK 构建通过；已安装小米 10，
系统 NFC=on，dumpsys 确认 ShaniuHceService/AID 注册。尚无射频 APDU 验收，
也未接入设备定位/现有 BLE 认领事务，不可称“碰一碰添加”完成。
下一步补板端标准 ISO-DEP 交换及其超时/撤场处理，再接已有 BLE 身份校验。
参考：Android https://developer.android.com/develop/connectivity/nfc/hce
与 NXP https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf 。

NFC 后续进展：小米 10 dumpsys 确认认领页将 ShaniuHceService 设为 preferred
foreground service；同轮板端 bknfc scan present=no，当前天线贴合未确认，
不据此判断射频硬件故障。新增维护补丁
nuttx/patches/contactless/0002-mfrc522-crca-frame-exchange.patch：选卡后有界
CRC_A 帧交换，发送最多 62 字节，由驱动追加/检查 CRC，错误清空接收结果。
调用方仍须串行独占选卡状态，沿用当前硬件计时器；未实现 ISO-DEP 会话、WTX
或 APDU。test_mfrc522_exchange.py 在固定 NuttX 基线组合两个补丁，编译实际
新增函数并验证所有字节长度、错误传播、残留清理与异常回复，通过 UBSan。
补丁尚未接入目标构建/烧录，因此不得声称板端已有 HCE 交换能力。

帧交换构建接入：新增规范目录下完整 MFRC522 覆盖源码和公开扩展头，复用已有
vendor/beken/nuttx 目录映射及 CMake/Make 接口。AIDK 选择 CL_MFRC522_FRAME，
关闭 CL_MFRC522，构建禁止重复实现；保留独立板级 UART 适配。测试核对覆盖
源码与固定基线加两份维护补丁一致。AIDK CP/AP 构建通过，最终配置和 Ninja
确认新实现确实参与编译。尚未烧录，ISO-DEP/WTX/APDU 仍未实现，不是射频验收。

1. 手机配置 HTTPS 服务地址、协议类型、ASR／对话／TTS 模型及凭据；MiMo 作为首个适配。
   板端在私有片内 LittleFS 中持久保存凭据，不提供明文回读。
2. 复用音频和 TLS 基础，完成板端 ASR → MiMo 对话 → TTS → 扬声器，并验收
   电脑 Gateway 关闭、App 退出后的独立 PTT 与多轮对话。
3. 人物配置和可选长期记忆使用 128MB SD NAND；近期上下文在 RAM 有界保存。
   SD 数据设计须处理 USB MSC 导出边界，不能将目录隐藏等同于访问保护。
4. 比赛版以 MiMo 声音为主，不做声音切换入口。IndexTTS 2.5 私有声音模型作为
   后续独立路线保留，推理运行位置另行确定。

长期记忆基础（2026-09-09，尚未启用）：私有 LittleFS 策略默认关闭，绑定当前
SCB3 owner；SD 快照只保存 AES-GCM 密文，支持密钥轮换使旧密文失效。
新增有界快照保存/恢复及现有 preferences owner 下的串行存储回调，避免另建
一套 mount/MSC 所有权。缺失、篡改、截断和读取失败不会返回残留明文；重命名后
同步失败返回 EINPROGRESS，不宣称旧快照仍有效。主机策略/快照故障注入、
存储占用与清理测试、AIDK CP/AP 构建通过。已接入已提交 SCB3 owner 和运行时：
首次真实对话在工作线程中读取策略并恢复，连接探测不读取；默认关闭时不访问 SD。
播放确认后由独立任务保存最近三轮上下文，保存期间保持 busy，控制线程不执行
文件 I/O；取消/配置清理必须等待任务退出。恢复数据在卸载/释放成功后才进入 RAM，
保存发布后的清理失败映射为 EINPROGRESS。SMH1 显式编码轮数、人物和文本长度，
不把含 padding/size_t 的 C 结构体直接写盘；人物不符、截断和多余字节均拒绝。
主机验证含最大长度、所有截断边界、恢复内容进入下一次请求、关闭时零 SD 调用、
播放失败不保存、保存阻塞时拒绝提前释放，以及相同 owner 刷新与更换 owner 的忙状态。
App 已接入跨重启记忆开关、删除确认和 SDC1 异步回执：新命令受理只显示处理中，
必须在后续状态中 pending 消失、无失败且 enabled 符合目标后才显示成功。断连清除
本次操作归因，重连只展示当前状态，不自动重放。私有策略发布不确定时，进程级
锁定后续修改/读回确认，更换 owner 不解除该状态；重启后再读持久结果。
默认仍关闭。开启不会用旧磁盘快照覆盖已有 RAM 对话；关闭保留密文，删除轮换密钥、
关闭保存并在策略确认后清空 RAM。当前只实现最近三轮上下文的跨启动保留，不等于
完整的长期事实记忆。C 运行时/协议测试、Android↔C 新命令互操作及 104 项 Android
测试通过（无跳过），目标构建成功。新版 APK 已安装模拟器，未绑定页面布局已核对；
已绑定控件及 BLE 实板读写尚未验收。未烧录，FAT 断电恢复未验证，不能称长期记忆
功能闭环。同一密钥下旧密文回放不可检测，后续恢复策略需保留此边界。

真实云服务验证（2026-09-09）：使用用户授权的 MiMo Token Plan 专属地址，
由当前 C 客户端 + OpenVela webclient + mbedTLS 完成固定合成输入联测。
对话成功 1342 ms；TTS 成功 1191 ms，24kHz PCM16 单声道 92160 bytes、
非零峰值 19246；主机转换为 16kHz 后 ASR 成功 688 ms，输入 61440 bytes，
预期测试词匹配。只记录汇总，临时凭据配置和合成音频随测试目录自动清理。
该结果验证主机运行的真实协议路径；主机 ffmpeg 转换不替代板端 Speex/DAC 验收。
发现并修正 App 默认普通接口与 Token Plan 凭据不匹配的问题：新增明确的接入方式
选择，默认 Token Plan，可选择标准接口或自定义服务，不从 Key 静默推断/改写地址。

设备状态反馈（2026-09-09）：ready 只代表语音会话已初始化，原按键链路 link
不是 Wi-Fi 状态。SDC1 已通过现有芯片本地连接快照 + AP native lease 匹配报告
wifi-known/wifi-ready；不发起网络探测，不回传 SSID、凭据或 IP。App 区分未知、
未就绪和 Wi-Fi 已连接，旧固件没有字段时保持待确认，不把 Wi-Fi 连接当成云服务
可达证据；记忆任务单独显示，上次对话错误提供重试提示。Android/C 字段互操作、
界面状态映射测试和 AIDK 构建通过；尚无实板断网/重连验收。

手机云服务预检（2026-09-09）：发现 CloudEndpoint 默认协商版本可能与板端
仅 TLS 1.2 不一致。已将手机握手显式限定 TLS 1.2，保留系统 CA、HTTPS 主机名
验证及连接超时。APK 构建并安装模拟器；显式 cloud_probe=1 instrumentation
通过真实 MiMo Token Plan 端点握手和系统信任根提取，不发送 API Key 或模型请求，
同时原有 Android Keystore、证书绑定、TLS 分块及错误 pin 拒绝测试通过。
该证据仅覆盖手机预检，未替代板端使用已供应 CA 联网和物理播放验收。

已连接界面验收（2026-09-09）：真实 MainActivity 注入测试内存快照，验证已关闭、
记忆处理中、结果未知及 Wi-Fi 未就绪状态，按钮启用条件符合预期。发现并修复
记忆任务 busy 时误显示“停止这次对话”的问题；存储事务仍不可被对话取消按钮
伪装成已取消。ui_probe=1 模拟器测试通过，已核对合成截图中三个记忆入口可见。
测试不写入真实绑定资料、不打开 BLE、不操作开发板；这不是三端物理验收。

Gateway 已归档为 `archive/shaniu-gateway-20260909`，仅作历史参考与测试工具。
CCF1 通用手机/板端配置已接入 SCB2 认领事务和云端请求，代码与主机验证通过，
实板直连仍未验收。官方唤醒词优先“你好，openvela”，“小冰”后置。

当前实现：CCF1 按协议类型配置服务地址和三个模型，不将厂商名作为公共接口。
板端配置校验、Android 配置测试及 APK 构建通过；本轮 AIDK 的 CP/AP/BL1/BL2
目标构建通过，未烧录。Chat Completions 音频请求支持
WAV/Base64 按需编码，直接匹配 OpenVela webclient 的 body callback，不保留完整
Base64 副本；8 种 PCM 长度（含 30 秒上限）、4 种拉取分块大小通过独立 JSON、
Base64、WAV 解码核对。该音频格式不是 multipart transcription，也不意味着所有
声称兼容 OpenAI 的服务都支持；具体协议适配与能力验证仍需完成。

TLS 已增加显式 server-auth-only 模式用于云端 HTTPS；默认仍要求双向认证，
所有模式都校验服务器 CA、域名和可信时间。此前 6 项 TLS 互操作测试通过。
CCF1 已嵌入认证认领事务；直连服务探测成功后仍需完成本地确认和配置提交。
已新增 OpenVela webclient 到现有 TLS provider 的 HTTPS 适配，保留总期限、
取消、响应大小限制、凭据缓冲清零，并拒绝重定向。ASR client 已将 PCM 编码、
HTTP POST 和文本解析串联；真实 webclient 的部分收发、chunked、401、超大响应、
取消和超时测试通过，真实 mbedTLS 与本机 HTTPS 服务的组合测试也通过。测试
不依赖冻结 Gateway，未调用真实云模型。CP/AP/BL1/BL2 构建通过，未烧录。
本轮新增 Chat Completions 对话、最多三轮 RAM 上下文与 MiMo 流式 TTS adapter。
上下文由播放完成后的显式 commit 更新；请求失败或取消不写入，支持清空和淘汰
最旧一轮。MiMo 扩展参数只在 MiMo dialect 下发送。TTS 严格检查 SSE、Base64、
单 choice、stop 与 DONE 顺序，有界接收，取消/截断会失败。2026-09-09 已补
协议 1 的标准 `POST audio/speech`：配置的模型、input、固定音色 alloy 和
`response_format=pcm`，直接流式消费 24kHz PCM16-LE，再复用现有 16kHz 播放适配。
兼容范围仍要求 ASR 支持 Chat Completions input_audio，不能将任意文字或
multipart transcription 模型视作可用。原始音频要求 audio/pcm 或
application/octet-stream，JSON/其他音频格式、非 2xx、空流、奇数长度、截断和
超过上限均失败，部分播放仍由原 turn owner 中止。参考标准协议：
https://developers.openai.com/api/reference/resources/audio/subresources/speech/methods/create
及 https://developers.openai.com/api/docs/guides/text-to-speech 。
标准 TTS 主机故障测试、本机真实 mbedTLS HTTPS 双适配联测、AIDK 构建与 App 构建通过；尚未调用真实标准 TTS 服务，未烧录验收。
后续发现标准 PCM 路径的真实缺口：HTTP 可按奇数字节分块，但播放器此前逐块
要求偶数长度。已在既有播放适配中保留一个未完成采样字节，下一次回调补齐；
整流结束仍有半采样时中止并释放 DAC，90 秒长度上限包含暂存字节。
真实 Speex 验证覆盖 8 种长度 × 10 种分块，含逐字节及奇数分块，与完整输入
输出逐字节一致；尾部截断、长度边界、滤波尾部及取消/排空测试通过。
实时云测试的音量统计也改为跨分块补齐采样，本机 HTTPS 回归及 AIDK 构建通过。
同轮 COM8 115200 回车探测可打开但 3 秒返回 0 字节，无 NSH；已关闭串口，未复位、
未刷写，因此新增播放修复还没有实板证据。

参考官方说明，MiMo PCM16 是 24kHz mono：
https://mimo.mi.com/docs/usage-guide/speech-synthesis-v2.5 。现有板端播放器及 DAC
基线仍是 16kHz。本轮已复用 OpenVela SpeexDSP（fixed-point）完成 24→16kHz
重采样，`bkcloud_synthesize_turn` 在收到第一块音频时才向既有 turn arbiter 申请
播放资源；按原帧长输出、补齐滤波尾部与最后一帧，结束后仍走原 drain/result。
请求失败或取消会清理该轮；返回成功只表示已安排 drain，不等于扬声器播放完成。
8 种输入长度、5 种分块、滤波尾部、混叠抑制、DAC 取消和异步完成测试通过。
重新生成配置时修复了 VOICE_TLS 与 WEBCLIENT 对 NET_SOCKOPTS 的循环依赖；
干净目标构建实际启用了 SPEEXDSP/FIXED_POINT/WEBCLIENT，并编译了播放适配。

本机真实 HTTPS 联测已串联 ASR、两轮对话与流式 TTS，测试验证的是协议和音频
字节，不是云模型质量或实板出声。配置事务与运行时 PTT 已接线：录音停止并回收采集
线程后启动 ASR/对话/TTS 工作线程，播放排空成功才提交 RAM 上下文。取消和清理必须
等待工作线程退出；配置提交前禁止 PTT 抢用确认按键，播放超时不提交上下文。
主机模拟按键、超时及取消生命周期测试和 AIDK 目标构建通过，不能称为实板产品闭环。
直连状态查询已区分 cloud mode/ready/busy；工作线程持有音频回合时返回
`turn_known=0`，不并发读取回合或误报空闲。原有 `BK7258_VOICE_HIL_TEST` 自动采集
入口已接入云语音路径，仍须显式发起、限时 200..5000 ms、标记 `physical=0`；
不新增录音实现，也不算物理按键验收。正式配置保持关闭；正式目标构建及开启 HIL
分支的目标编译器语法检查通过，尚未烧录验证自动采集。

Android 普通配网页已支持云端地址、模型和凭据，现使用 SCB3 一同提交独立控制密钥；旧 Gateway 地址仅在
开发者模式要求。APK 构建及配置编码测试通过，尚未完成手机/实板事务验收。
当前连接探测只验证对话服务，不能证明 ASR/TTS 模型均可用。云任务已复用 OpenVela
`getaddrinfo(AF_INET)` 解析服务地址，遵循系统 DNS 缓存；不继续依赖手机保存的旧地址，
域名证书校验保持不变。解析在云工作线程执行，AIDK AP 配置将 DNS 单次收发等待设为
2 秒、尝试次数设为 2；这不是整个解析过程的硬期限，也不代表能立即中断 DNS。
取消期间仍持有任务资源，解析返回后检查取消状态，禁止继续发云请求。
主机模拟解析失败、恢复、地址变化与取消清理通过；真实板端 DNS 尚未验收。
App 普通入口已切换到直连配网路线，不自动连接旧 Gateway、不要求导入控制令牌；
旧服务联调须在调试版显式进入。本机认领结果与在线状态分开展示，尚未接通的
人物控制、手机升级等明确不可用。模拟器已验证首页、心情、设置、隐私、升级提示和
添加设备页面，89 项 Android 主机测试通过；真实认领/在线状态及板端控制仍待验收。
直连人物已复用 SD NAND 上的 KVDB 配置；正式 AP 开启已有偏好模块。每轮请求读取
人物风格，介质忙碌/读取失败时保留上次值，首次使用默认为 gentle。主机测试验证了
quiet/playful 切换及读取失败回退；偏好存储回归通过。补齐 UnQLite、任务局部存储和
文件锁依赖后，正式干净构建通过，并核对最终配置确实启用 KVDB_UNQLITE 和
BK7258_PREFERENCES，尚未实板验收。
空闲直连会话允许偏好命令，录音/云任务/播放/配网期间仍禁止变更。
人物发生实际切换时清空 RAM 短期上下文；读取失败保留原人物和上下文，相关主机测试通过。
开启 VOICE_VOLUME_PERSISTENCE 时，偏好命令和播放已共用 LittleFS 音量存储，不再
读写旧 KVDB 音量；每次播放读取持久化值，旧内存 override 不再覆盖后来的设置。
未开启该选项的配置保留 KVDB 行为。主机测试覆盖外部变更、SD 占用、保存错误及
已保存但立即应用失败后的下一轮恢复。实板音量、App 人物/音量控制尚未验收/接通。
2026-09-09 本轮 COM8 查询可成功打开端口，但 8 秒内未收到 `bkota status` 响应；
串口已释放，未复位或烧录。PnP 正常枚举不等同于固件控制台可响应，当前安装版本未确认。
随后 COM16 可打开，但使用既有 OTA 协议发送 HELLO 后 5 秒内无 ACK，已关闭端口；
未发送 START 或写入 Flash，自动升级尚未执行。
再以 Ctrl-C/换行退出可能占用控制台的前台命令后查询，8 秒内仍为 0 字符，已释放 COM8；
该尝试未恢复控制台，也没有据此定位到硬件或固件根因。

失联操作回溯（2026-09-09）：07:41 左右仍可查询 `/data/ota-ca.pem`，
随后串口 `echo` 重定向上传公共 CA，先超过 CP NSH 80 字符限制产生截断错误，
缩短命令后等待提示符超时，之后被动采集为 0 字节。该时间窗口指向 CP
LittleFS/片内 Flash 写路径，尚无故障栈证明具体阻塞点；不能归因于已验收的
SD NAND 一致性路径。暂停此串口重定向上传方式。历史 g400 未启用 USB OTA
接收，故 COM16 无 HELLO ACK 本身不能证明整板死机。
当前源码检查未发现 MTD 返回分支漏解锁；当前构建 ELF 确认 SDK Flash 调用
调度锁和跨核 START/END 通知，但尚未证明与失联的因果关系，未据此改 SDK。

真实云接口验证（2026-09-09）：使用目标同源 C client、OpenVela webclient 和 mbedTLS，
通过 Linux 主机套接字直连已配置的 MiMo 服务；对话返回 0、耗时 1822 ms，TTS 返回 0、
耗时 1419 ms，解出 92160 字节 24 kHz 单声道 PCM，峰值 20218。未经过 Gateway。
仅记录状态、长度、耗时和音频统计，未记录密钥、对话正文或音频；临时配置已删除。
追加固定合成语音回环：TTS 解出 107520 字节 24 kHz PCM，主机 FFmpeg 转换为
71680 字节 16 kHz PCM，再由同源 C ASR 请求识别；返回 0、耗时 816 ms，结果包含
预期测试词。该轮对话 1527 ms、TTS 1775 ms，均返回 0。转换器为主机工具，不构成
板端 Speex、DNS/网络、MIC/DAC 或物理验收。临时测试音频与凭据随测试目录清理。

直连设备控制契约：复用 BLE/TLS 传输，目标命令为状态、取消、音量、人物设置；
AP 保持配置与音频的唯一可变状态所有者，App 只展示设备回执，不乐观宣称成功。
工厂 possession secret 只用于初始认领，日常控制使用手机随机生成的独立 32 字节密钥。
SCB3 在 SCB2 尾部追加该密钥，沿用现有私有配置事务，确保网络/云配置与控制身份
一同提交；密钥不得出现在状态或恢复响应中。旧 SCB1/2 仍可解码，但不具备新控制身份。
已完成 C/Android 编解码及非零/长度/尾部检查，相关主机测试和 APK 构建通过。
手机端已实现 Keystore AES-GCM 密钥封装，AAD 绑定设备及事务；待确认密文与回执
一同保存，确认后与有效绑定原子切换。16 项绑定测试通过，包括失败重试、重启恢复、
密文替换/丢失拒绝、跨设备拒绝和明文回调清零；APK 构建通过。测试使用 JVM AES 密钥，
2026-09-09 已在 Android 模拟器运行独立 instrumentation，实际 AndroidKeyStore
密钥不可导出、待确认密文经重新创建存储对象恢复、确认晋升和回调明文清零均通过。
测试使用随机别名与独立 SharedPreferences，结束清理；未验证进程被杀/掉电、
物理手机硬件保护或实板认领事务。
BLE 连接对象已接入协议 APPLY 前的控制密钥保存：从本次已上传候选提取，先加密
保存再允许 APPLY；保存异常禁止 APPLY，配置/事务/密钥临时副本清零。认领包测试
及 APK 构建通过，覆盖多分片候选、失败顺序、畸形长度和 SCB2 无密钥兼容。
普通 App 已切换 SCB3，每次新认领生成独立 32 字节 SecureRandom 密钥，编码后清零；
恢复仅查询原事务，不重发配置或生成新控制身份。设备证书指纹取自激活资料，与
设备 ID/事务一起纳入 AES-GCM AAD，待确认到有效绑定同时保存；指纹替换或删除
均拒绝解密，不退回无 pin 的日常连接。96 项 Android 测试及 APK 构建通过，模拟器
实际 Keystore 验证了指纹绑定、对象重建后的恢复和临时密钥/pin 清零。
尚待 BLE 日常认证和实际控制命令，以及进程重启/实板认领恢复；不能将配置/存储
测试通过算作认证控制完成。
旧 Gateway 控制仅保留在已冻结的历史调试入口，不新增 Gateway 依赖或复用其 bearer token。
日常控制 SDC1 会话核心已实现：仅接收 TLS 内完整帧，独立密钥认证、严格递增序号，
支持状态/取消/音量/人物的 AP 执行回调。认证失败、重复序号、非法参数均终止会话；
操作失败返回错误及未知字段，不伪造设置成功。主机测试及 ASan/UBSan 通过
（当前环境不支持 LeakSanitizer，单独关闭泄漏检查），AIDK 目标构建通过。
新增 control pair 已复用现有 TLS/GATT API 串联该核心，保留跨步响应，发送背压
期间不继续读取或重复执行。1..48 字节分片、连续 AUTH/STATUS、队列背压、认证
10 秒期限、TLS generation 失效和启动失败清零测试通过，AIDK 构建通过；测试
使用 TLS 接口模拟，不等于真实 TLS 联测。AP execute 回调已实现，复用云状态、
取消及既有偏好存储；录音/云任务/播放忙碌时拒绝设置，不在取消路径读取文件。
取消请求保留 cloud 会话，等待云工作线程和采集退出再释放缓冲，按键保持按下时
不重触发。主机模拟采集取消不上传、云工作线程取消后保留会话/不提交上下文通过，
AIDK 构建通过。产品 owner 现已从已提交 SCB3 快照获取独立控制密钥，打开日常
BLE 窗口并调用 control pair/AP execute；认领与控制共用一个 GATT owner。
日常连接及其断线清理不占用 PTT，身份替换仍受 busy 保护；八秒物理长按可转入
回执恢复，并等待音频取消完成。owner 测试覆盖自动窗口、普通按键、断线清理、
密钥复制和恢复转换，AIDK 构建及最终 AP 符号确认通过。Android 已实现 SDC1
对端及基于已绑定密钥/pin 的 DeviceControlConnection，复用 AndroidProvisionGatt
工作线程串行收发和超时，认证前不执行控制命令，超时不重放；设备忙碌和未知
字段按回执解析，不将取消回执等同播放排空。99 项 Android 单元测试及 APK
构建通过。App 页面已接入附近设备扫描、已绑定身份认证连接、状态轮询、取消、
音量和人物设置；后台退出关闭手机连接，界面以设备回执显示结果。
主机跨端 TLS 已联测；Android 设备提供者/实板尚未联测，未烧录，不能称为日常控制已实板可用。

Android 视觉改版已接入生成的陶瓷机器人插画、奶油白/深绿配色和图标导航。
首页与未认领心情页已在模拟器检查；未认领时提供添加设备入口。
1080×2400 / 560dpi（约 309dp 宽）检查发现插画将主按钮挤出首屏，已改为
按屏高缩放插画，复测主按钮完整可见；测试后已恢复模拟器默认 420dpi。
配网页默认 MiMo，将地址、协议、模型收进自定义服务区域，保留通用配置能力；
返回设备选择时清除输入凭据并使未完成的服务查询回调失效。
APK 构建与 99 项既有单元测试通过；不能据此认定实板配网、设置回执或全部
屏幕尺寸已经验收。

认领持久化失败保护：依据本机 Android 35 `SharedPreferencesImpl.commit()` 的
先更新内存、再确认写盘顺序，修复写盘失败后其他页面误读内存绑定的风险。
共享同一 preferences 的 backend 在失败/异常后停止读写，直到新进程从磁盘
重新加载；不尝试用可能同样失败的补偿写入覆盖待核对结果。新增两项回归覆盖
内存先变更再返回失败、异常、现有/新建页面和独立磁盘重载模型，共 101 项
单元测试通过。真实 Android Keystore/SharedPreferences 的正常保存恢复仍需
与故障注入结果区分，模拟写盘故障不等于手机断电测试。

SDC1 跨语言联测：Android `DeviceControlProtocol` 通过进程管道直连固件
`bk7258_control_session.c`，使用主机合成 AP 状态，验证认证、状态、忙碌拒绝、
取消、音量和人物设置，以及逐字节响应分片。包含该联测的 Android 102 项测试
通过，未跳过。复现命令在 Android README；C 可执行文件已列入 Gradle 测试输入，
避免未启用原生对端的跳过结果被复用。该证据不包含真实 TLS/GATT 或实板 AP。

日常控制真实加密通道验证：在既有 `test_provision_tls.py` 中接入产品
control pair/session，复用实际 workspace mbedTLS 与 20 字节模拟 GATT。
20 轮临时证书场景全部通过，新增 AUTH 按单字节 TLS record 分片、输出拥塞
不重复执行音量、重复序号拒绝、错误 owner key 拒绝、认证超时全对象清零。
这是主机 mbedTLS 客户端/服务端证据，尚非 Android TLS 对端或实板 BLE 验收。

App/固件加密互通：现有 host runner 可用 `SHANIU_ANDROID_INTEROP=1` 同时
构建两个原生对端并运行全部 Android 测试。App 的 `ProvisionTls`、
`ProvisionGattSession`、`DeviceControlProtocol` 经 20 字节密文管道对接产品
mbedTLS/control pair，通过指纹匹配握手、owner 认证、音量回执及错误指纹拒绝。
完整 Android 103 项测试通过、零跳过，原有 20 轮 mbedTLS 场景也通过。
该 TLS 客户端使用主机 JVM 提供者，仍未覆盖 Android 设备 TLS 提供者、真实
BluetoothGatt 回调或实板动作；临时证书/测试私钥退出后删除，未改动板端信任。

Android 原生 TLS 提供者验证：模拟器 instrumentation 使用临时 AndroidKeyStore
P-256 签名密钥、Conscrypt TLS 1.2 与产品 `ProvisionTlsChannel`，经内存 20 字节
密文分片完成双向明文核对，并拒绝错误证书指纹。与加密回执恢复/借用清零测试
一并返回 PASS。测试密钥按 Conscrypt 预哈希签名要求允许 NONEwithECDSA，并
允许 SHA-256 签署临时证书；该设置仅存在于 androidTest，未修改产品密钥策略。
这补足了 Android 模拟器提供者的基础证据，仍未证明手机 Conscrypt 与板端
mbedTLS 的真实 BLE 连接、物理按键、扬声器及存储写入。

设置/心情 UI：设置页已采用带说明的设备、声音、隐私和管理选项行，未认领
时突出添加入口；人物列表仅对已确认 persona 显示选中标记。debug 历史服务
入口缩为页脚，release 不显示。未认领设置页已在模拟器截图核对，APK 构建通过。
移除本机连接资料现在先关闭日常控制会话、停止轮询再删除持久化绑定，避免
旧认证连接继续存活；实板解绑后的断线效果仍需验证。

近期对话清除：SDC1 新增 `CLEAR_HISTORY=6`（空载荷），App 隐私页经确认后
发送，只有设备成功回执才提示完成。AP owner 在空闲状态清除 RAM history 和
文本缓冲，录音/云任务/播放/取消清理中拒绝；不修改 SD、凭据或人物设置，
不宣称删除云服务记录。主机覆盖已有上下文清除、重复调用和忙碌拒绝，C/Android
互通及全部 103 项 Android 测试通过，AIDK CP/AP 构建通过，未烧录。
长期记忆仍未实现；已确认现有 preferences/media_volume 租约可复用来约束
SD 与 MSC 互斥，后续仍需加密存储、开关/删除及导出边界验收。
本次 COM8 仅换行探测：端口可打开，3 秒收到 0 字符，已关闭释放，未复位或下载。

## 1. 文档职责与唯一入口

本文是傻妞项目的总入口，统一管理：

- 产品范围、非目标和成功判据；
- AIDK 固件、Gateway/AI、Android、模型/音色/UI 资产六条工作流；
- 跨工作流依赖、里程碑、验收门和发布物；
- 当前状态、风险、决策和下一执行队列。

专项细节由以下文档承接：

- [BKVoice 产品架构与适配计划](bkvoice-authorized-voice-app.md)：板端 App、Gateway AI、
  授权音色、视觉、UI 和 OpenVela 组件；
- [下一阶段单轮接话纵切计划](shaniu-next-stage-single-turn-plan.md)：PTT、出站 WSS、Gateway、
  下行播放、无串口验收和故障门；
- [原生 Android companion 计划](shaniu-android-companion-plan.md)：BLE 配网、控制台、视频、
  权限及后续微信小程序；
- [BK7258 构建、发布与硬件证据 SOP](nuttx-port/bk7258-build-flash-debug-sop.md)：构建、签名、
  打包、部署和实板证据；
- [AIDK 当前板测交接](aidk-ai-toy-board-test-handoff.md)：日期化硬件基线和明确缺口。

冲突时使用以下优先级：当前源码/配置与日期化证据 > Master Plan 的状态和优先级 > 专项设计 >
历史研究文档。Master Plan 不覆盖真实配置或实板证据。

## 2. 产品定义

傻妞是一台可独立启动、能听、能说、能看、能通过双圆屏表达状态的随身 AI 伴侣：

- AIDK 是身体和唯一运行终端；
- 用户 Gateway 是 ASR、VLM、LLM、TTS、唱歌和长期记忆的可替换算力端；
- Android 是可选的认领、配网、设置、视觉和隐私控制台；
- 手机不在线时，AIDK 仍能显示状态、执行本地按键、播报签名离线资产并明确降级；
- 人物 machine ID 固定为 `shaniu`，显示名为“傻妞”；
- `fictional-ex-girlfriend` 只表示虚构互动风格，真实身份始终披露为 AI companion 和合成声音。

产品体验不是“让大模型任意控制所有外设”，而是把板上外设封装为有限、可撤销、可审计的
语义能力，由确定性状态机和权限策略决定是否执行。

## 3. 已冻结决策

| ID | 决策 | 状态 |
|---|---|---|
| D-001 | 产品名“傻妞”，稳定 ID `shaniu` | `ACCEPTED` |
| D-002 | AIDK 是主产品；手机和 Gateway 都不能替代板端隐私/安全状态机 | `ACCEPTED` |
| D-003 | 首版原生 Android；微信小程序后置为 Gateway 轻客户端 | `ACCEPTED` |
| D-004 | BLE 只做认领、配网、恢复和小型控制；PCM/JPEG/视频走 Wi-Fi | `ACCEPTED` |
| D-005 | 日常手机控制经 Gateway；板端不开放公网入站服务 | `ACCEPTED` |
| D-006 | 首版半双工；MIC 完全释放后才允许 DAC，视频与语音并发另立门 | `ACCEPTED` |
| D-007 | Camera 首版显式单帧；低帧率预览在单帧稳定后加入 | `ACCEPTED` |
| D-008 | 双屏首版是一对眼睛；程序化 UI 通过后才批量生成最终美术资产 | `ACCEPTED` |
| D-009 | 当前实板基线由 standalone BKVoice 独占 session/audio；优先验证官方 AI Agent 的替代 profile，通过接话、清理及资源预算后再切换。每个运行 profile 始终只有一个 session/audio owner | `ACCEPTED` |
| D-010 | LLM 只调用能力白名单，不接触 GPIO、寄存器、Shell、裸 Flash 或密钥 | `ACCEPTED` |
| D-011 | 产品提供 PTT 和本地唤醒词两种入口，共用 AP 会话 owner。按用户提供的赛事规则第 4 条，比赛固件只允许“你好，openvela”或“Hello，openvela”；先实现中文官方词的多人通用 INT8 DS-CNN。“小冰”保留为后续非比赛版本扩展，不进入首期适配或比赛可选词。KWS 模型与参数由 App 管理、签名并支持撤销，未验收前保留 PTT | `ACCEPTED` |
| D-012 | 不实现产品 UART/串口通信功能；只保留传输介质无关的 provider seam。既有启动、恢复、平台 console 或外设 UART 不扩展为产品能力，也不作为接话退出门 | `ACCEPTED` |
| D-013 | 优先复用 OpenVela 的公共采播 API、AI Agent 对话和语音通道；补齐供应商协议适配，不复制整套框架。原生 Agent 与 Python Gateway 的运行边界按第 17.5 节验证，不把 C 源码视为 Python 的现成模块 | `ACCEPTED` |
| D-014 | 首条真实问答以赛事提供的 MiMo 资源为候选，先对话、再 ASR/TTS 适配；IndexTTS 保留为 Gateway 后续音色方案，不阻塞首轮问答。赛事资源的适用范围与实际接口能力分别核验 | `ACCEPTED` |

## 4. 产品范围

### 4.1 Contest MVP 必须完成（被 2026-09-09 产品门覆盖）

1. Android 通过近场认领完成 Wi-Fi 和 Gateway 配置；
2. 经官方“你好，openvela”唤醒并自动端点录音，直连 MiMo ASR/对话/TTS 的半双工对话；PTT 仅内部调试；
3. 两块 GC9D01 显示校准正确的监听、思考、说话、离线、Camera 和错误状态；
4. 用户明确触发一次 GC2145 单帧视觉问答，并获得语音与双屏反馈；
5. Android 查看在线、电量、版本、权限，修改音量和有限 `persona_mode`；
6. 断网、Gateway 重启、取消和错误输入均能释放 MIC/DAC/Camera 并进入明确降级；
7. 固件、模型/音色和 UI 资产不含私人源数据，交付物具有版本、hash 和验证记录。

### 4.2 Beta 目标

- 本地唤醒词和 1 秒 pre-roll；
- 2–5 fps、单 viewer、用户持续确认的 MJPEG 预览；
- 拿起/摇一摇/翻转等姿态事件和 NFC 白名单场景；
- Android 侧 OTA、资产更新、诊断摘要、解绑和隐私数据删除；
- 用户主机侧可查看、导出和删除的长期记忆。

### 4.3 非 MVP 与明确不承诺

- 30 fps 手机直播、全双工、边播边唤醒；
- iOS、首版微信小程序、公共云强依赖或板端公网端口；
- 舵机、行走等运动控制；CN10 马达振子作为有界提示外设纳入本轮板级验证；
- 在 BK7258 上训练/运行扩散生图、声码器或大型 VLM/LLM；
- 未经硬件证明的高 8 MiB PSRAM、复杂 UIKit、DMA2D/RGB scanout；
- 未授权真人身份、声音、肖像或聊天内容的复制和训练；
- 把软件密钥存储表述为 hardware-backed/TEE。

## 5. 系统架构与控制边界

```text
                         User-owned Gateway
                  ASR | VLM | LLM | TTS | Memory
                    ^                         ^
                    | companion-v1 TLS/WSS    | console-v1 HTTPS/WSS
                    |                         |
+-------------------+------------------+      +--------------------+
| AIDK AI Toy / BK7258                 |                           |
|                                      |                    Android App
| CP: Wi-Fi/BT controller, OTA, health |                           |
| AP: session, audio, UI, vision       |<-- provision-v1 BLE ------+
|                                      |
| capability + permission + arbiter    |
|   |      |       |       |           |
| audio  LCDs   camera  sensors/NFC    |
+--------------------------------------+

后续微信小程序 ---------------- console-v1 ----------------> Gateway
```

执行路径固定为：

```text
user/LLM request
  -> App capability allowlist
  -> permission + expiry + generation check
  -> product state/resource arbitration
  -> public NuttX/OpenVela ABI
  -> Chip lower-half
  -> Board physical binding
```

Board 只描述器件、总线、引脚、电平、设备节点和板级资源关系；Chip 只拥有 SoC/SDK controller、
DMA/cache/PM 和 lower-half；人物、模型、UI、Gateway 和会话策略只属于 App/Gateway。

## 6. 状态定义

Master Plan 只使用以下状态：

| 状态 | 含义 |
|---|---|
| `ACCEPTED` | 需求或设计已经明确确认，不代表实现完成 |
| `IMPLEMENTED` | 源码和相应无硬件测试完成，尚不能称实板通过 |
| `BUILT` | 目标配置构建和 manifest 校验通过 |
| `BOARD_VERIFIED` | 指定物理板、构建身份和用例得到机器可读证据 |
| `EVALUATED` | 模型/资产完成约定客观指标和人工评测 |
| `SELECTED` | 经评测、授权和签发，可进入产品 |
| `IN_PROGRESS` | 有工作产物，但退出条件未满足 |
| `NOT_STARTED` | 尚无实现或可复验证据 |
| `BLOCKED` | 已记录外部阻塞，无法继续取得有效进展 |

构建、设备注册、READY 日志、mock、emulator 和历史其他板证据都不能自动升级为
`BOARD_VERIFIED`。

## 7. 2026-09-02 基线快照

| 子系统 | 当前状态 | 已有证据 | 关键缺口 |
|---|---|---|---|
| BK7258/AIDK 平台 | `IN_PROGRESS` | CP/AP、外设、网络、OTA 和构建交付基础已合并 | 当前产品组合的全外设、PM 和长稳回归 |
| 傻妞身份/离线语音 | `IMPLEMENTED` | persona、voice-pack parser/player、host tests | 实板资产播放、产品 UI 与最终签名资产 |
| Voice session | `IMPLEMENTED` | `companion-v1`、turn/media/capture/PTT owner、cancel/timeout/overflow host tests；aggregate-only `capture-test` 已构建 | 公共按键、真实 sink、TLS/WSS 和 `capture-test` 实机非零 MIC 数据 |
| Gateway AI | `IN_PROGRESS` | 授权音色评测和协议方向已建立 | ASR/LLM/VLM 统一服务、流式接口、模型 profile 加载/撤销 |
| 授权音色模型 | `EVALUATED` | IndexTTS-2.5 zero-shot BASE 在同门槛盲听中胜出；4 个候选内容门均通过 | 授权/披露复核、签名、撤销和 Gateway product manifest；当前 LoRA 不作默认 |
| 双屏产品 UI | `NOT_STARTED` | `/dev/fb0`、`/dev/fb1` 板级路径与产品设计 | 实板左右映射、renderer、动画、30 分钟长稳 |
| Camera 产品能力 | `IN_PROGRESS` | `/dev/video0`、VGA/30 MJPEG 源码和构建 | 历史 `-ENOMEM` 定位、稳定出帧、单帧 owner、上传和回收 |
| Android | `ACCEPTED` | 完整专项计划 | 测试手机基线、工程、fake Gateway、BLE/Gateway 实现 |
| Product BLE provisioning | `IN_PROGRESS` | [安全决策及输入格式](shaniu-provision-security.md)；Android 认领入口、权限/扫描、TLS/ATT、SPV1 事务及提交前回执持久化；板端 TLS/认领/整包文件提交主机联测、SCB1→现有语音配置校验；CP 私有 LittleFS/AP RPMsgFS 配置及 AIDK CP/AP 构建 | bootstrap 供应、广播/按键窗口 worker、Wi-Fi/Gateway 试连与文件提交的运行期接线、Android↔mbedTLS 互通及 20 次实板闭环 |
| KWS | `IN_PROGRESS` | 官方中文词训练审计/INT8 导出入口、上游 microfrontend、TFLM runner；32 KiB/1 秒 pre-roll 和免提端点状态机已有宿主验证 | 真实多人语料/有效模型、AP MIC owner 与 Gateway pre-roll 接线、能量门限标定、目标完整构建、签名、FAR/FRR 和实板验收 |
| Low-fps video | `NOT_STARTED` | Camera MJPEG 与 Android 方案 | 单帧门、Gateway 转发、背压、隐私和并发验证 |
| Product release | `NOT_STARTED` | 配对 OTA/恢复/交付工具基础 | 三端版本矩阵、签名资产、APK、端到端证据包 |

这张表是日期化快照；任何状态改变必须附源码/配置、测试或实板证据，不靠口头更新。

## 8. 六条工作流

### W1：平台与板端基础

责任域：Chip、Board、CP/AP、启动、网络、音视频 lower-half、PSRAM、PM、存储、OTA。

主要交付：

- AIDK 产品 profile 的 resolved config、CP/AP ELF 和 build manifest；
- MIC/DAC、双屏、Camera、SC7A20H、MFRC522、电池、按键、LED、SD NAND、P9 马达振子的设备证据；
- Wi-Fi/BLE 并存、低压休眠恢复、资源峰值和长稳证据；
- 可恢复 direct 诊断包及正式签名 release/OTA 包。

### W2：傻妞设备 App

责任域：persona、状态机、PTT、session、资源仲裁、UI、视觉、权限、离线降级。

主要交付：

- `shaniu` 产品身份和合成披露；
- 半双工 voice turn、cancel、重连和离线 fallback；
- 双屏 eye renderer、情绪映射和隐私/错误覆盖；
- `vision.snapshot`、motion/NFC/battery 语义能力；
- 不包含 Board 私有头、引脚、SDK 私有调用或服务器凭据。

### W3：User Gateway 与 AI

责任域：设备会话、ASR、VLM、LLM、TTS、记忆、Android API、认证、背压和可观测性。

主要交付：

- 单一设备 `companion-v1` TLS/WSS owner；
- ASR → LLM/tool policy → TTS 的首 chunk 流式路径；
- JPEG/VLM 与有限 emotion/tool 结果；
- Android `console-v1` HTTPS/WSS；
- 可验证的 `gateway-model-profile-v1`：固定 backend/model lock、prompt profile、运行时、
  16 kHz/mono/S16 delivery tuple、评测状态、签名和撤销状态；
- 本地可查看/导出/删除的记忆和无正文 telemetry。

### W4：原生 Android

责任域：BLE 认领/配网、Gateway 控制台、状态、心情、视觉、隐私、更新和解绑。

主要交付：

- Kotlin/Compose App、fake BLE/Gateway 和自动测试；
- `provision-v1`、`console-v1` 客户端；
- snapshot 与受控 MJPEG viewer；
- Android Keystore token、权限说明、签名 APK 和 hash。

### W5：模型、音色和 UI 资产

责任域：KWS、ASR/VLM/LLM/TTS 选型、授权音色、离线语音、眼睛素材和签发。

主要交付：

- 数据授权、去重、隔离、评测和撤销记录；
- 冷/热首包、RTF、显存、正确率/相似度和盲测；
- `EVALUATED` 后才能比较，`SELECTED` 后才能签入产品 manifest；
- KWS 资产必须绑定 model hash、词表、frontend/输入 tuple、arena 上限、阈值、FAR/FRR
  噪声集、签名与撤销；产品必选交付是 App-owned KWS inference runner，先用选定模型和 TFLM
  在独立 `kws-app` profile 验证；Media Trigger 只是可选生命周期/事件 adapter，只能在独立
  `media-standard` profile 验证，不能被写成另一种推理 runtime 或第二个产品 owner；
- 程序化眼睛先行，最终 PNG 经人工选择后转为有界 RGB565 sprite/调色板/RLE。

当前音色结论冻结为：IndexTTS-2.5 zero-shot BASE 是 `EVALUATED` 的主候选，私有 prompt
profile 固定 `emotion_alpha=0.25`、`duration_factor=0.94`；三组实验 LoRA checkpoint 均未在
盲听中胜过 BASE，不得因机器相似度或“已训练”而替换默认候选。该结论只完成模型评测门，
没有自动完成授权、披露、签名、撤销或 Gateway runtime 选择门。

### W6：安全、质量与交付证据

责任域：权限、凭据、证书、签名、隐私、测试矩阵、版本和发布物。

主要交付：

- `provision-v1` 安全 ADR、设备认领、撤销和重放保护；
- firmware/model/voice/UI/APK 独立版本、签名、hash 和回滚；
- host/build/Gateway/Android/实板/故障注入/长稳的分层证据；
- 不含私人正文、原始语音、图片、权重、密钥和识别性路径的发布包。

## 9. 依赖关系与并行边界

```text
W1 audio baseline ---------> W2 wake/capture ------> W3 direct MiMo voice
          |                         |                         |
          +-------------------------+-------------------------+--> M2 voice gate

W1 BLE substrate ----------> provision-v1 ADR -----> W4 Android pairing --> M3

W1 camera -----------------> W2 vision.snapshot ---> W3 VLM ---> W4 image UI --> M4

W1 dual LCD ---------------> W2 renderer ----------> W5 selected assets ------> M4

M2 + M3 + M4 -------------> W6 security/long-run/release ---------------------> M8
```

可并行：Android A0 fake Gateway、Gateway mock、UI 资产 brief、host protocol tests。

M7-KWS、M7-Memory 和 M7-Assets 是三个独立 gate：互不阻塞，也不要求全部完成后才能进入 M8。
KWS 或长期记忆未通过时，Contest MVP 分别保持 PTT 或不声明长期记忆；只有实际进入发布包和产品
说明的资产必须先通过对应 `SELECTED`、签名和撤销门。

不可并行共享：同一物理 AIDK、串口/下载工具、同一 build output、同一 GPU 训练/推理实例和同一
Android 测试机。Camera、双屏、音频和 PM 的组合验收必须由一个总控按固定镜像串行执行。

## 10. 里程碑

### M0：计划、边界和可复现基线

状态：`IN_PROGRESS`

交付：Master Plan、专项计划、三协议职责、代码路径边界、当前改动独立交接、AIDK direct build
和 host/layer/manifest 基线。

退出门：所有现有改动归属清楚；没有把用户文件混入提交；当前能力状态与证据一致；下一刀只
有一个 runtime owner。

### M1：离线身份、音频与双眼基础

状态：`IN_PROGRESS`

交付：傻妞身份、合成披露、签名 voice pack、程序化双眼、明确的离线/错误/隐私状态。

当前诊断纵切新增固定 `bkvoice tone-test`：AP 通过公共 `media_player` 播放 1 kHz、500 ms、
低幅度 S16LE 非语音 PCM，复用既有 DAC/PA owner，并对 partial write、断链及清理错误失败关闭。
这只形成可构建和可主机测试的实板入口；听感、PA 时序与重复资源回收仍属于硬件门。

退出门：合法/非法 voice pack 门禁；100 次播放无资源泄漏；两屏物理左右确认；30 分钟低刷新
不影响 supervisor 和音频 deadline。

### M2：PTT 端到端语音

状态：`IN_PROGRESS`

交付：按键事件、公共采播链路、真实网络 provider、Gateway ASR/LLM/TTS、流式下行和 cancel。
当前执行顺序见[第 17.5 节](#openvela-mimo-execution)：先复用和适配 MiMo，固定回复保留为
传输回归夹具；不再以扩建固定回复服务或选定最终 IndexTTS 音色作为真实问答的前置条件。

退出门：50 次连续 turn；MIC 有非零样本/能量；MIC→DAC 顺序正确；断网、Gateway 重启、超时、
取消和旧 generation 均回收；记录首包/P50/P95、heap、stack 和 deadline。

### M3：Android 认领与控制台

状态：`NOT_STARTED`

交付：Android A0/A1/A2、产品 GATT、Wi-Fi/Gateway 配置、状态/权限/音量/persona mode。

退出门：20 次认领/错误密码/重连/解绑；设备本地确认；无明文凭据；Android 不在线时设备仍可
运行；服务端和设备端同时执行权限检查。

### M4：单帧视觉与双屏反应

状态：`IN_PROGRESS`

交付：Camera owner、JPEG chunk、Gateway VLM、语音回答、emotion tag 和 Android snapshot。

单帧纵切：`bkvision-v1` 已实现 CP 命令/AP Camera owner，通过 `/dev/video0` 的 V4L2
MMAP 路径捕获一帧并只返回宽高、fourcc、有效字节数、sequence 和 JPEG SOI/EOI 完整性。
帧内容不经 RPMsg、不记录、不落盘，捕获带有界超时并在所有已进入的阶段逆序回收。该实现
已在 `18.6.230+290` 通过同次启动三次及正常重启后一次有效 JPEG 拍照和关闭，见
[GC2145 频率投票修复记录](aidk-gc2145-jpeg-frequency-fix.md)；100 次场景与泄漏验证仍待完成。
JPEG chunk、Gateway VLM、语音/双屏反馈及 Android snapshot 尚未接入，不能把这一诊断纵切
标记为 M4 完成。

后续连续录像适配升级为 `bkvision-v2`，新增 `bkvision record <1..60秒>`，由 AP 以三个
MMAP 缓冲持续采集并保存无声 MJPEG AVI。CP/AP 干净构建、协议/文件/存储互斥主机测试
及合成视频独立解码通过；v232 已完成实板 1/10/60 秒录像及随后拍照，10/60 秒录像
平均交付约 6.7 fps，文件导出解码和长期资源释放尚待验收。它不改变 snapshot
不落盘的行为，也不代表 Gateway 实时视频已接通，详见[连续录像适配](aidk-continuous-recording.md)。

退出门：100 次成功/取消/超时；有效 JPEG；无 fd/heap/Camera owner 泄漏；真实 Camera 指示覆盖
占用期；未知情绪回退 `neutral`。

### M5：姿态、NFC、电池与旅行场景

状态：`IN_PROGRESS`

交付：拿起/摇一摇/翻转事件、NFC 白名单场景、电量降级、手机可选位置授权和离线待同步。

当前纵切：只读 `bkhealth-v1` 已实现电池状态、毫伏电压和 raw-first 片上温度查询；无电芯
放电曲线时不输出百分比，无单芯片 25 摄氏度参考值时不输出摄氏温度。该纵切仍需实板读取
证据。`bknfc-v1` 已实现只返回 `present=yes/no` 的 NFC 在场查询，协议不包含 UID、卡号或
卡片内容，且不把在场结果当作身份认证；仍需实板完成无卡/有卡重复扫描。姿态事件、NFC
白名单策略和低电动作策略尚未完成。

退出门：传感器只发布语义事件；NFC 不作为唯一强认证；低电禁止高耗能动作；位置撤销立即生效；
任何传感器故障不阻塞语音、OTA 和 supervisor。

### M6：Android 低帧率视频

状态：`NOT_STARTED`

交付：640×480 MJPEG 2–5 fps、单 viewer、Gateway relay、Android viewer、背压和自动停止。

退出门：30 分钟无无界队列或 heap 增长；锁屏、后台、松开、断线、低电和超时均停止；隐私指示
同步；通过并发门之前与 voice turn 互斥。

### M7：三个独立 Beta gate

状态：`IN_PROGRESS`（资产评测和 KWS 基础实现已有进展；长期记忆仍为 `NOT_STARTED`）

以下历史 gate 描述已被 2026-09-09 产品要求覆盖；官方唤醒、离线恢复、认领安全、视觉和 OTA
均是当前交付范围，不能再标为可选或后续。失败时必须保留明确未完成状态。

#### M7-KWS：本地唤醒

状态：`IN_PROGRESS`；实现入口和输入契约见[语音应用说明](bkvoice-authorized-voice-app.md#111-本地唤醒)。

交付：App-owned KWS inference runner、所选唤醒词模型/词图、1 秒 pre-roll、签名和撤销 manifest。
比赛固件仅接受官方“你好，openvela”或“Hello，openvela”；先实现前者，英语版本另行训练验收。
产品名仍为“傻妞”。App 只选择符合赛事规则且已安装、已验收的唤醒词资产。
“小冰”保留在后续非比赛版本的扩展队列，复用同一前端、runner 和触发接口，但单独训练、
签发并验收模型；不能仅改显示字符串。比赛 profile 的词表白名单同时约束 App 选择和资产
安装，不允许通过设置或下载模型启用“小冰”。官方词是产品入口，PTT 仅内部调试。
唤醒后进入同一 AP 语音会话，由 VAD 静音判定或最大收音时长结束上行；
PTT 由松键结束。空唤醒、取消与超时都必须释放资源。半双工播放期间暂停 KWS，
播放完成后恢复监听；边播边唤醒仍需另行通过 AEC 和并发验收。

先过 config/build 门：目标产品 profile 明确启用选定 TFLM runtime 和 App runner，保存对应
resolved config；实际输入固定并验证为 16 kHz/mono/S16，记录 MIC/AEC physical path、模型/
词表/frontend/阈值/arena provenance。Media Trigger 只有生命周期 API；只有选择独立
`media-standard` profile 时才给同一 runner 增加 adapter，不能把“树里存在”写成已有 BK7258
模型 plugin。vendor Wanson 的“嗨阿米诺/拜拜阿米诺”静态词图也不能替代所选唤醒词模型。

实板退出门：在安静、音乐、车内、户外风噪、远场、不同人声和扬声器回放集上记录 FAR/FRR、
P50/P95、CPU、arena/heap 和 1 小时连续运行；触发后 1 秒 pre-roll 连续，PM 恢复后仍满足门槛。
任一门未过时产品标记官方唤醒未完成，不得用 PTT 作为产品替代；不能用 vendor 串口出现一次
`recognized` 代替验收。

#### M7-Memory：长期记忆

状态：`NOT_STARTED`

交付：Gateway 侧可查看、导出、删除和撤销的长期记忆，以及设备/Android 的有限控制入口。

退出门：来源、用途、保留期和同步状态可追踪；删除与撤销后服务端、cache 和设备待同步副本均
不可继续使用。未通过时只关闭长期记忆能力，不阻塞 KWS、资产选择或 Contest MVP/M8。

#### M7-Assets：最终模型、音色和 UI 资产

状态：`IN_PROGRESS`

交付：最终 TTS/音色和 UI 资产 `SELECTED` 清单及签名、版本、有效期和撤销 manifest。

退出门：只有通过统一评测和人工选择的资产才能进入发布包；当前 IndexTTS-2.5 zero-shot BASE
仍只是 `EVALUATED`。未完成的可选候选从发布能力矩阵移除，Contest MVP 继续使用已验收的签名
voice pack 和程序化双眼，不能用“已有 checkpoint/图片”代替 `SELECTED`。

### M8：安全、长稳与比赛交付

状态：`NOT_STARTED`

交付：正式固件 release/OTA、Gateway 部署包、签名 APK、签名模型/音色/UI manifest、演示脚本和
证据索引。

M7-KWS 或 M7-Memory 未完成不阻塞 Contest MVP/M8：发布版保持 PTT、关闭未验收的长期记忆，并在
能力矩阵中明确标记未包含。M7-Assets 只约束实际随发布包交付或在产品说明中声明的资产。

退出门：24 小时目标组合长稳；掉电/弱网/错误证书/重放/升级失败注入；可回滚；不破坏 `sys_rf`；
发布包不含私人数据；最终说明只列 `BOARD_VERIFIED` 与 `SELECTED` 能力。

### M9：可选扩展

状态：`NOT_STARTED`

微信小程序、唱歌、全双工、Opus、高 8 MiB PSRAM、复杂 UIKit、局域网直连和未来电机均在 M8
之后单独立项，不得反向阻塞 Contest MVP。

## 11. 三个核心用户旅程

### 11.1 首次开机

```text
设备进入未认领态
-> Android 发现
-> 板端按键/双屏确认
-> 安全传递 Wi-Fi/Gateway 配置
-> 关联/DHCP/Gateway 注册
-> Android 与双屏同时显示成功
-> 手机离线后设备仍进入 IDLE/OFFLINE
```

### 11.2 日常语音

```text
本地 KWS 检出官方唤醒词“你好，openvela”（PTT 仅内部调试）
-> 同一个 AP 会话 owner 接受触发
-> 双眼监听 + MIC 指示
-> 语音入口 VAD 检出静音/达到收音上限
-> interrupt/join/release
-> 上行结束
-> Gateway ASR/LLM/TTS
-> 首 PCM chunk 下行
-> 双眼说话 + DAC
-> 完整 release 回到 IDLE，按配置恢复 KWS 监听
```

### 11.3 “傻妞，帮我看一下”

```text
明确请求
-> 双屏 Camera 确认
-> reserve + 单帧 JPEG
-> 立即 stream-off/release
-> Gateway VLM + LLM
-> answer + bounded emotion
-> 双屏反应 + TTS
-> 原图默认删除
```

## 12. 协议与数据所有权

| 契约 | 两端 | 数据 | 唯一 owner |
|---|---|---|---|
| `companion-v1` | AIDK ↔ Gateway | PCM、JPEG、turn、窗口、取消、错误 | AIDK App + Gateway session |
| `provision-v1` | Android ↔ AIDK BLE | 认领、Wi-Fi/Gateway 配置、结果 | AIDK provisioning App |
| `console-v1` | Android/小程序 ↔ Gateway | 状态、设置、权限、视觉和管理 | Gateway |
| `bkvoice-pack-v1` | 签名资产 ↔ AIDK | 离线 PCM 和授权/披露 manifest | AIDK App |
| `gateway-model-profile-v1` | Release pipeline ↔ Gateway | backend/model lock/profile/runtime/delivery/评测/签名/撤销 | Gateway release pipeline |
| product asset manifest | Gateway/Android/AIDK | 模型、音色、UI 的版本/hash/撤销 | Release pipeline |

私人原始聊天、原始语音、图片、embedding、训练 checkpoint、模型权重和密钥只保留在获授权的
用户主机或密钥系统中，不进入 Git、固件、APK、默认 telemetry 或公开证据。

## 13. 测试与证据矩阵

| 层级 | 证明什么 | 不能证明什么 |
|---|---|---|
| Host unit/fake | parser、状态机、协议、超时、取消、错误回滚 | 真实驱动、时序、RF、音质和画面 |
| Layer/config gate | Chip/Board/App 依赖与配置闭包 | 运行时资源和硬件功能 |
| Target build/manifest | 源码可链接、角色和交付输入一致 | 板上可用、数据有效和长期稳定 |
| Gateway/Android mock | API、UI、重连和错误模型 | BLE/Wi-Fi/Camera 实际链路 |
| 实板单功能 | 指定镜像下单外设功能 | 组合并发、PM 和长稳 |
| 实板组合/故障注入 | 产品状态机、资源、隐私、恢复 | 量产一致性和长期现场数据 |
| Release verification | 版本、hash、签名、布局和回滚输入 | 未执行的硬件用例 |

每条验收记录至少包含：物理板、profile、build identity、固件/资产版本、操作步骤、机器可读
结果、失败边界和证据路径。不同板、旧镜像或历史 profile 的 PASS 不得替代当前 AIDK 产品证据。

## 14. 关键指标

### 可靠性

- Voice：50 连续 turn；离线 voice pack 100 次播放；
- Camera：100 次单帧成功/取消/超时；
- BLE provisioning：20 次认领/重连/解绑；
- UI/video：双屏 30 分钟、视频 30 分钟；
- Release candidate：24 小时目标组合运行。

### 性能

- 记录 voice ASR/TTS 冷/热首包和端到端 P50/P95；
- 记录 20 ms 音频 deadline、MIC→DAC 切换、Camera 首帧和 UI 刷新耗时；
- KWS 单独记录 FAR/FRR、触发 P50/P95、CPU、tensor arena/heap 峰值、1 秒 pre-roll 和
  PM 恢复后首个有效 deadline；
- 记录 internal SRAM、PSRAM、heap min-free、线程 stack high-water、fd/mqueue 和网络窗口；
- 指标在首次硬件基线后冻结阈值，不能在没有数据时伪造数字目标。

### 隐私与安全

- MIC/Camera 指示覆盖真实 reserve 到 release；
- 凭据、token、图片和私密正文不进入日志；
- L2/L3 请求有权限、过期、generation、重放和撤销检查；
- 固件、模型、音色、UI 和 APK 版本可独立识别，必要时独立回滚或撤销。

## 15. 发布物

Contest release 至少包含：

1. 经过验证的 CP/AP 配对固件、build/release manifest、hash 和烧录/恢复说明；
2. Gateway 可复现部署定义、依赖锁定、API schema 和不含私密权重的模型 manifest；
3. 签名 Android APK、版本、hash、权限用途和兼容设备清单；
4. 签名 voice/KWS/UI 资产 manifest，不包含训练源数据；
5. Host/build/Gateway/Android/实板/长稳的证据索引；
6. 一条从首次认领到语音、视觉、离线、恢复的演示脚本；
7. 已知限制、未通过能力和回滚步骤。

## 16. 风险登记

| 风险 | 当前判断 | 应对与退出证据 |
|---|---|---|
| MIC/PTT 产品闭环未通过 | 高影响，v328 已有按键与网络接线，但 Gateway 连接超时，缺少完整 turn 证据 | 复用现有接线，验证真实模型回复和 MIC/DAC 换向，再取得 50-turn 证据 |
| 摄像头与新增外设组合回归 | v321 单项基线已保留，后续共享资源改动可能引入回归 | 复用既有帧率、冷断电与读回基线；只在相关时钟、DMA、电源或并发改变时补对应回归 |
| BLE 配网泄露凭据 | 高影响，产品协议未设计 | 独立安全 ADR、本地 possession、会话保护、无明文日志/抓包证据 |
| 双屏刷新挤占音频 | 中高影响 | 程序化低刷新、脏矩形、30 分钟 UI+audio deadline |
| Wi-Fi/Gateway 弱网 | 高概率 | 有界窗口、取消、重连、离线 fallback、P50/P95 和故障注入 |
| 多 owner 状态冲突 | 高影响 | standalone BKVoice 与 AI Agent 二选一，三协议唯一 owner |
| 私人数据进入 Git/发布包 | 不可接受 | 路径隔离、source audit、release 内容扫描和人工复核 |
| 功能过多拖垮 MVP | 高概率 | M9 可选项不阻塞 M0–M8；每阶段只有明确退出门 |
| 构建被误当实板完成 | 已发生过的认知风险 | 强制状态词与日期化 BOARD_VERIFIED 证据 |
| 缺少已验收的官方唤醒词模型 | 现有 Wanson 固定词图不能满足赛事词要求，TFLM runner 通过不等于模型有效 | 按 M7-KWS 训练、签发和验证官方词的多人通用模型；“小冰”留待非比赛扩展，不等待供应商定制词图 |
| Media Trigger 被误当成现成 KWS | 高影响，当前只有 API/生命周期，未见 BK7258 模型 plugin | 先证明 App-owned TFLM runner；Media Trigger adapter 只在独立 `media-standard` profile 验证，不引入第二 owner；保存 resolved config 与实板 FAR/FRR |
| KWS defconfig/output 漂移 | 高影响，历史 resolved config 关闭 Media/Trigger/CMSIS-NN，不能代表未来产品 profile | 以指定 build identity 保存 resolved config、模型/runner manifest 和板测证据 |

## 17. 立即执行队列

当前优先级以第 17.5 节为准：复用 OpenVela，完成 MiMo 真实 PTT 问答并进入 Android 联调。
现有改动继续按外设和工具拆分验证、提交；马达只属于实际连接它的 AIDK。
第 17.1–17.4 节保留此前执行和验收记录，不把旧日志或源码检查升级为新的实板通过。

### 17.1 当前源码与验收基线

- 2026-09-07 已重新获取远端主线 `openvela/dev-ai-contest-2026`，当前为 `3e694fb`；
  主工作区已对齐该提交并进入 `fix/bk7258-aidk-board-dependencies`。旧 `782e019` 分支
  保留，未合入的语音/外设变动保留为工作区差异；摄像头、SDIO 使用已合并版本。
- 摄像头/V4L2、SDIO 写入修复已经合入。保留 v321（`18.6.321+381`）恢复包，以及
  `out/bkvision-perf-v321/` 的冷断电、读回与解码证据，作为后续组合回归的参照。
  后续只有相关时钟、DMA、共享电源、文件系统或并发发生变化，才补对应回归。
- 用户本轮确认 CN10 接有马达振子，并提供 P9 驱动电路。当前
  `bk7258_board_config.h` 已定义 `BK7258_BOARD_PIN_MOTOR=9`，`HAS_MOTOR` 和
  `CN10_MOTOR_CONNECTED` 已改为 1；Kconfig、HIL README 已同步，驱动实板验收尚未完成。
  现有 AIDK 用户 LED 绑定 P40、按键绑定 P8；源码检查未发现该绑定直接用 P9 测 LED。
- 正常产品 HIL 已移除依赖旧诊断日志的 `dmesg` 容量用例，保留 6 项真实串口检查。
  容量单独通过 v323 AP 块设备与 Windows MSC 核对：126877696 bytes / 247808 个
  512-byte sector。该几何结果不能替代整盘读写或冷断电保持验证。
- 依赖来源已重新确认：`nuttx/openamp/open-amp` 和 `libmetal` 是官方 manifest 中的
  独立 Git 项目，当前均无源码修改，并非私改缓存。通用 NuttX fallback 的 upstream zip
  与当前 OpenVela RPMsg API 不匹配。已撤回两头文件候选补丁，采用
  `nuttx/dependencies.lock.json` 固定官方提交并用 `git archive` 重建。当前没有证据要求
  修改 OpenAMP/libmetal 源码。AIDK、T5-Board、T5AI-Core 的 CP/AP 冷构建已通过；
  AIDK v322/v323 已确认启动，AP、CPU2、RPMsg 正常。其他两板仅有构建证据。
- `gateway/shaniu` 当前是 loopback TLS/WSS 的确定性音调回复服务，尚无 ASR/LLM/TTS。
  板端 `bkvoice status` 仍标记 PTT event、sink、transport 未安装；已有逻辑测试不等于
  物理按键到真实 Gateway 的接线完成。Android 已有 HTTPS/WSS 客户端工程，实机联调另验。

### 17.2 马达振子的板级约束

用户提供的电路为：P9 经 R51（1 kΩ）驱动 Q2（MMBT3904）基极，R53（10 kΩ）下拉；
Q2 集电极连接 CN10 低端，马达高端经 R46（15 Ω）接 LDO_3V3，D8 为续流二极管。
由该电路可知 P9 高有效、低电平停止；这不构成转速、电流或温升已经验证的证据。

- P9 归属马达，禁止纳入通用 LED、GPIO 扫描或回环测试；接入状态与驱动验收状态分别记录。
- 先完成上电、初始化失败、取消、超时、休眠及复位后的停止路径，再进行短脉冲验证。
  测试与产品请求均需有界时长；脉宽、间隔和最大连续工作时间由首次实测冻结。
- 复用 NuttX GPIO/定时器能力，审查现有 lower-half 是否足够。AP 作为马达动作和计时的
  唯一 owner；Board 描述引脚、电平和供电，Chip 负责 GPIO/PM 机制，产品层只提交有界动作。
- LDO_3V3 与 LCD、NFC、SD NAND 共用。停止马达只释放自己的动作与电源投票，不能直接
  拉低 P52 切断其他外设。实板验证短脉冲、重复启停、电压波动，以及存储/显示/NFC 并发。
- 首版采音期间暂停振动，避免机械噪声进入 MIC；若产品以后要求同时工作，新增串扰验收。
  马达动作属于明确选择的主动 HIL 用例，普通状态检查不能启动马达。

### 17.3 验证与提交批次

固件按下表顺序执行；工具、Gateway 和 Android 的主机工作可独立推进，实板始终只有一个
操作者。每行可以拆成多个小提交，共享 Kconfig/Make/CMake 文件按本批必要 hunk 暂存。

| 批次 | 范围与提交边界 | 提交前退出条件 |
|---|---|---|
| B0 主线与板级契约 | 保留真实差异并对齐 `3e694fb`；更新 CN10/P9 装配事实、马达测试排除规则、SD NAND 容量契约 | 已合入内容去重；板级事实、配置和 HIL 断言一致；不以启用能力标志代替驱动验收 |
| B1 可复现依赖 | 锁定官方 manifest 的 OpenAMP/libmetal Git 提交，从 `git archive` 重建；维护依赖锁和隔离构建入口 | 无工作目录拷贝、无旧 role 输出时生成 AIDK CP/AP；两端 API/资源表 ABI 与 RPMsg 建链实测通过；仅在确证官方实现缺口后维护源码补丁 |
| B2 看门狗与启动 | 必要的 CP WDT/启动顺序/恢复修复；不夹带临时 IRQ 与重启追踪 | 平台和复位主机用例通过；实板正常喂狗、受控超时、复位原因、CP/AP 恢复分别有证据 |
| B3a MIC 与 AEC | MIC lower-half、recorder 生命周期、MIC1/MIC2 参考路由；AEC 修复单独提交，采集探针留在测试配置 | 静音/近端说话/远端播放可区分；MIC1 为主麦、MIC2 为 AEC 参考；20 ms 帧截止期、启停回收通过。AEC 另有开关对照、参考延迟、削顶与残余回声测量，不能用非零样本代替效果验收 |
| B3b PTT 与语音会话 | 物理 P8 按键事件、有界控制队列、真实 WSS transport/sink、取消及资源释放 | 真实按下/松开、快速重按、超时、断网、重连、背压；连续 50 turn 和 30 分钟运行；MIC 释放后再启 DAC，失败后可再次发起。Gateway 可先用受控固定回复，真实问答另验 |
| B4 马达振子 | P9 绑定、单 owner、有界动作及停止路径；与显示/LED 提交分开 | 第 17.2 节的短脉冲、取消、复位、供电和并发验证完成；采音期间不误振动 |
| B5 显示 | 双屏资源、左右映射、显示协议/资产与 renderer | 两屏独立图样和左右映射人工确认；启动缺资产/坏资产可恢复；语音与显示 30 分钟并发；若影响共享电源或搬运路径，补摄像头/存储回归 |
| B6 电池与温度 | 电池、温度各自小提交；保留原始量、单位与校准状态 | 电压与仪表对照，充电/满电/断开状态真实切换；温度有参考测量。没有标定依据时不宣称温度精度或电量百分比准确 |
| B7 加速度计 | 标准 sensor 路径、板级轴向与产品 motion 服务 | 六面静置、重力幅值/符号、时间戳、实际运动、休眠恢复；WHO_AM_I 或注册成功不足以验收 |
| B8 NFC | UART 外设传输、读卡生命周期与 presence 服务 | 无卡/已知卡、移入移出、重复读取、超时与断电恢复；普通日志不输出 UID |
| T 工具 | HIL 下载工具和 debug skill 显式调用限制分别提交 | 主机契约测试；明确指定板型/设备身份/串口，错误目标被拒绝；debug skill 仅明确点名触发。真实下载复用某个固件批次的独占板窗口，避免重复刷机 |
| G Gateway | 保留固定回复协议回归，按第 17.5 节优先接入 MiMo 真实 ASR/LLM/TTS；鉴权和部署随接入验证 | `make -C gateway/shaniu test`；双向窗口、取消、断连、证书失败；设备接入须有明确部署/凭据策略，不能直接放开 loopback 限制。真实语音链另验首包、流式取消、故障降级 |
| A Android | 保留现有真实 HTTPS/WSS 客户端；工程、控制面、实机问题独立提交 | 单元测试和 `assembleDebug`；小米 10 安装、Keystore、真实 TLS/WSS、断线重连和设备状态回报。BLE 配网按专项协议门后续接入 |

B1 的共享依赖以及 Chip/common 改动，在提交前补齐 AIDK、T5-Board、T5AI-Core 对应 CP/AP
干净构建回归；各板的硬件状态分开记录。只从锁定官方 Git archive/已维护补丁重建，不把展开目录、
zip、缓存版本漂移或整个 SDK 打包进源码提交，也不因看到差异就预先修改 SDK/NuttX。

### 17.4 执行入口和交付证据

已运行以下主机入口并通过；这些结果不能代替实板及物理验收：

```sh
make -C tests/host/bk7258 run-platform run-reset-marker
make -C tests/host/bk7258 run-voice-capture run-voice-ptt run-voice-media-recorder \
  run-voice-turn run-voice-turn-audio run-voice-gateway run-voice-session run-voice-wss
make -C tests/host/bk7258 run-display-pack run-display-rpc
make -C tests/host/bk7258 run-health-core run-motion-core run-nfc-core
make -C gateway/shaniu test
```

Android 在独立临时源码副本执行
`./gradlew :app:testDebugUnitTest :app:assembleDebug --offline --no-daemon` 已通过：
42 tests、0 failures/errors/skipped；APK 已生成，尚未安装到测试手机。
Gateway 12 项协议/TLS 测试通过；HIL 下载工具 14 项主机契约通过。板测继续使用当前维护的构建/下载入口和
`tests/pytest/test_bk7258/`，B0 的过时容量契约已修正。COM8 已通过 Windows PnP 确认为 CH340 UART0，
存储验证使用 `18.6.323+383`，当时由 HIL 自动刷入并确认；`apctl health` 为 HEALTHY，
faults/recoveries/consecutive=0/0/0。v323 修复 AIDK 两端漏选 USBMODE_RPMSG 导致命令未编入的问题。
新录像 10 秒记录 298 帧（29.8 fps），三次 AP 读回及 MSC 副本的 FNV 同为 `645f35a5`，
但主机仅解码 297 帧并报 MJPEG overread，尚未通过录像完整性验收。8.2/9.2 MB 文件的
Windows 复制分别耗时约 156/174 秒，读性能异常不能归因为 RPC 回复丢失。

v323 验证时读取的原 v321 录像 FNV 为 `37aa5528`，与保留原件 `f081c022` 不同。旧文件内两个
128 KiB 区间逐字节等于新录像的两段；文件簇范围无重叠，原始卷读取也确认相同内容，且
整机软件复位后保持。只读 FAT 检查另发现多份旧测试文件的交叉链接和长度错误，未运行
修复或格式化。没有 v323 写入前的整文件/原始扇区快照，尚不能判定异常发生于哪次操作，
也不能将它归因于新增代码、MSC、SDIO 或硬件。已提交的 SDIO、MMCSD 和录像源码在
`3e694fb` 到 `25da215` 之间没有后续差异；这不等于两个集成固件的全部输入一致。

一份旧测试录像 `video-2801343a-00000003.avi` 因文件损坏未能备份，已按此前清理授权删除；
没有删除模型。原 v321 基线、当前两份主机录像、四段原始扇区及软件复位后的副本均保留在
`out/shaniu-plan-20260907/` 和 `out/bkvision-perf-v321/`，不进入源码提交。

每批按“审查真实差异 → 剔除临时诊断 → 主机检查 → 构建 → 对应板测 → 小范围暂存并提交”
推进。提交说明必须给出问题、行为变化、准确测试结果、构建身份和剩余物理验收限制；
验收证据留精简摘要、必要日志与 hash。沿用既有 fork/中文 PR 交付边界，由用户创建远端 PR。
临时探针、IRQ/重启追踪、原始日志、私人语料、模型、密钥和构建输出不进入提交。

B0/B1、T、Gateway、Android 和 B2 启动/整机复位，以及 B4 的 FF 维护补丁、
通用驱动、板级绑定、应用命令已形成独立提交。发布分支为
`fork/fix/bk7258-aidk-board-dependencies`；14 个提交已通过 PR #101 合入上游，
合并后基线为 `25da215`。上游 rebase 后的提交 ID 改变，提交树与原 `9fa922b` 完全一致。
Gateway mTLS 也已合入，上游当前基线为 `9ea6de9`。后续在原工作区的
`fix/bk7258-physical-ptt` 分支推进，未提交改动完整保留。
B2 的 `reset reboot` 实测恢复 AP/CPU2/RPMsg 健康；AP-only restart 被 Wi-Fi
生命周期保护以 `-EBUSY` 拒绝，不能记为 AP 单独重启通过。当前 v326 已完成标准
wdog 受控停喂 -> rtc_watchdog(0) -> AP/CPU2/RPMsg 恢复；配置及共享检查已提交。

B4 的 v324（18.6.324+384）已通过自动下载、启动、能力查询、停止、超长请求拒绝、
P9 激活/自动到期读回和冷却期拒绝。P9 脉冲期间读到 `0x00000003`，板端延时后、
显式 stop 前读到 `0x00030001`（输出锁存位为低）。这不是示波器脉宽测量。
FF 驱动 15 场景、AP 服务 10 场景、FF upper-half ASAN 测试通过；
AIDK、T5-Board、T5AI-Core CP/AP 干净构建通过。马达仅适用于已连接 CN10/P9
的 AIDK；T5-Board 和 T5AI-Core 未连接马达，其 CP/AP 最终配置未启用马达或
bkhaptic，ELF 中也无对应驱动和命令。两者在本批仅做共享代码编译回归。
v324 来自集成工作树，
包含尚未提交的其他外设候选，不能作为纯发布 HEAD 的全功能整包验收。
本次临时签名私钥已删除，只保留公开签名产物。

B4 尚缺人工振感、供电波动、采音串扰及并发验收，故第 17.3 节整行仍未完成。
MIC raw PCM 5 秒标准采集和 watchdog 实板复位恢复已通过 v326 验证；
54c605d、6cf1d89、9fa922b 分别提交音频生命周期、任务上下文喂狗和 RPMsg drivercheck。
这三笔已随 PR #101 合入上游。AEC/PTT 和其余外设未满足各自退出条件，继续留在工作区。
PTT 的 P8 `/dev/gpio1` 归 CP 所有。v328 已接入独立按键 RPMsg 通道、AP 会话 owner
和 RX 队列，并在实板完成 mTLS/端点/可信时间的 RAM 装载及连接超时后的清除。
当前 Gateway 连接仍超时，尚无物理按键到 MIC/Gateway/DAC 的通过证据；手机安装和
人工听感验收也未取得。不能以主机模拟或凭据装载成功冒充端到端验收。
存储修复已有通过结论：v321 的 10 秒/301 帧录像，经重复读取、软件复位和一次完全断电，
FNV 始终为 `f081c022`，主机副本逐字节一致且 301 帧全部严格解码。这个验收不撤销。
单独保留 v323 的后续内容变化/解码异常待复现项，不将其写成“此前存储从未完成”，也不
将它当成已证实的代码回归。长时、多文件及组合压力尚不在 v321 的通过范围内。

后续 v327（18.6.327+387）已自动下载并确认 AP/CPU2/RPMsg 健康。motion/NFC 客户端
与服务端补齐发送拥塞重试、连接代次和 namespace unbind 清理；真实源码 host 各 19 场景
通过 UBSan。MFRC522 官方 read 忽略选卡错误的问题以 contactless/0001 维护补丁修复，
只在隔离 NuttX 应用；无卡和选卡失败故障注入通过，官方 checkout 未改。
v327 连续三次加速度计采样时间戳递增，单一静置姿态重力模长约 9.87–9.90 m/s²；
两次 NFC 查询均报告 present=no。电池查询为 charging、4059/4060 mV；温度 raw=541/542，
未标定故不报告摄氏度。这些是查询路径证据，六面/动态、已知卡移入移出和仪表对照仍未完成。
两块 framebuffer 的官方绘制和新固件麦克风回归亦通过，最终 AP 零故障；临时签名私钥已删除。
详见 [v327 外围 RPC 验证](../../verification/bk7258/2026-09-07-aidk-peripheral-rpc.md)。
新外围服务改动继续留在工作区，按对应退出门再提交。

此前的固定回复接线阶段保留为验证基线；下一执行顺序由第 17.5 节更新为 OpenVela 复用、
MiMo 真实问答和 Android console-v1 联调。其他外设和 AEC 的独立验收继续保留，
不作为 Android/Gateway 接口联调的前置条件。首版保持半双工。
BLE 承担 App 认领和配网；PCM/JPEG/视频走 Wi-Fi。IndexTTS 位于 Gateway 算力端，
AP 负责流式音频和设备执行回报。v328 的隐藏串口装载用于当前实板联调，
不代表 App 配网或持久设备注册已经实现。

KWS、长期记忆、最终音色资产、BLE 配网和手机预览继续遵循各自既有里程碑，
未通过的能力保持关闭，不扩大本轮提交范围。

2026-09-07 的组件复用接入将 KWS 自写前端替换为现有 TFLM microfrontend，
同源批处理/流式特征、合成数据 INT8 导出和真实 TFLM host 推理已通过。
新增 KVDB 非秘密偏好适配和 `bkvoice prefs`，复用现有 CP→AP endpoint；
API、CLI host 测试及现有目标参数下的对象编译通过，尚未开启板级后端。
`desired_volume/persona` 仅是设置值，尚不改变音频增益或 Gateway 人物状态。

复用审计发现两个必要前置：KVDB FILE 的短写/EINTR/零写处理有缺陷，
UnQLite 使用 `OMIT_JOURNALING` 且缺失项错误码尚未归一，需选定后端后修复并验证持久化；
当前 NuttX SMP 不支持所需的认证配对，AI Agent 配网示例又依赖另一套 Bluetooth
Framework API，不能直接代替 `provision-v1` 的认领、物理确认和凭据保护流程。
完整 Media 的 stop/join/release 和资源预算也需独立验证，本轮维持现有 AP owner。
这些源码进展不构成 BLE 配网、掉电持久化或正式唤醒的实板验收。

<a id="openvela-mimo-execution"></a>

### 17.5 OpenVela/MiMo 复用与当前执行顺序

2026-09-07 决策更新：先完成“按键说话 → 识别 → 对话 → 播放回答 → 回到空闲”的真实链路。
以下是待执行计划；现有源码支持、主机测试通过和实板通过分别记证据，不互相替代。

**复用边界和唯一 owner**

- 官方 `packages/ai_agent` 已有 voice channel、公共 recorder/player 接入、message bus、
  LLM/tool loop 和回复播报。优先在 AP 的最小替代 profile 验证，复用现有
  `BK7258_APP_AGENT` 生命周期绑定和公共采播 bridge；不新写一套录音器或播放器。
- 产品基线仍为 `BKVoice → companion-v1 → gateway/shaniu`。替代 profile 中关闭 BKVoice
  session/audio owner，再启用 Agent；完成资源、取消、离线和故障验证后才能迁移产品。
  若 Agent 的资源或生命周期尚不满足实板要求，继续当前 BKVoice 路线完成首条问答。
- 官方 Agent 是 C/NuttX 应用，当前 Gateway 是 Python 服务。Gateway 接入 MiMo 需要自身的
  provider adapter；不能把官方 C 模块直接称为已复用的 Gateway 后端。Agent 路线通过
  Gateway 的模型代理接入云端；现有 BKVoice 路线由 Gateway 编排 ASR/LLM/TTS。
  同一会话只选择一条路径，不能同时在 AP 和 Gateway 运行两套对话编排。
- 两条路径均保持 AP 掌握物理 PTT、MIC/DAC 仲裁、设备权限和 UI；Gateway 掌握供应商密钥、
  模型 profile、长期记忆和云端调用；Android 负责认领、配网和设置。MiMo 密钥不进入 App、
  固件、配置示例或日志。Gateway 内部 provider 接口不改变 Android `console-v1`。
- 替换前列出入口、线程/缓冲上限和删除集合：仅移除被新 owner 完整接管且已无消费者的
  session/voice provider；仍用于控制、Camera、OTA 或已选基线的协议不能随之删除。
  不保留两份可变会话状态，也不为复用强制启用完整 Media、MQTT 或 Bluetooth Framework。
  公共包确需补能力时，以团队仓可复现补丁/overlay 维护，保留上游来源，不直接改官方 checkout。

**供应商适配范围**

| 部分 | 首选接入与已有复用点 | 必须补齐和验证 |
|---|---|---|
| 对话 | MiMo OpenAI-compatible `/v1/chat/completions`；优先验证 `mimo-v2.5`，`mimo-v2.5-pro` 作为质量/延迟对照。官方 `cmd_llm.c`、`llm_proxy.c` 已有 MiMo preset、自定义 URL 和通用 chat/tool 处理 | 显式配置当前 endpoint/model，不沿用旧 `mimo-v2-flash` 默认；验证鉴权、响应、上下文、工具白名单和取消。其他厂商 preset 只表示代码入口，不能据此宣称实测支持；当前 Claude preset 不等于原生 Anthropic 协议适配完成 |
| ASR | `mimo-v2.5-asr` 的 chat 请求中上传一个 `input_audio`，先使用 WAV | 首轮在松开 PTT 后提交有界完整录音；BKVoice 路线由 Gateway 将上行 PCM 封装为 WAV。固定时长/字节上限、超时和空录音行为；`stream:true` 是识别结果的 SSE 输出，不等于边录边上传 |
| TTS | 先验证 `mimo-v2.5-tts` 内置音色；待播文本按接口放入 assistant message，流式 PCM 从 SSE 的 `choices[].delta.audio.data` 解码 | 处理 SSE 任意分片、Base64、结束和取消，核实实际采样率/声道/格式；Gateway 转成板端的 16 kHz/mono/S16LE，再按 20 ms 分帧。当前协议固定 640 字节，只给 TTS 尾帧补静音，不裁尾音、不填充输入录音 |
| Agent 语音 provider | 复用 `voice_channel.c`、`audio_capture.c`、`audio_playback.c`；现有 `voice_asr.c`/`voice_tts.c` 的流式入口直接委托火山实现 | 增加可替换的 ASR/TTS provider 能力与生命周期接口，区分整段 ASR 和实时上行；保留 start/finish/abort 及 TTS cancel，不以“改 URL”替代协议适配 |
| 后续音色 | 保留 Gateway IndexTTS 与 MiMo voiceclone/voicedesign 的候选位置 | 先完成内置音色问答，再比较授权、音质、首包和成本；既有 IndexTTS `EVALUATED` 结论保留，未通过资产签发与实板验收前不升级为 `SELECTED` |

协议依据：[MiMo ASR](https://mimo.mi.com/docs/en-US/api/audio/Speech-Recognition)、
[MiMo TTS](https://mimo.mi.com/docs/en-US/api/audio/tts)及
[TTS 使用说明](https://mimo.mi.com/docs/usage-guide/speech-synthesis-v2.5)。
采样率、输入上限、实际可用模型和赛事 endpoint 的音频能力仍需接入验证。

用户说明资源由小米赛事赠送；候选 Base URL 为 `https://token-plan-cn.xiaomimimo.com/v1`。
[赛事说明](https://openvela.csdn.net/6a6180bf662f9a54cb936a05.html)确认有 Token Plan 资源，
但未证明定制后端使用的豁免；[普通订阅规则](https://mimo.mi.com/docs/zh-CN/tokenplan/Token%20Plan/subscription)
有此用途限制。因此按赛事发放条款核对实际使用范围，不直接断言赠送资源禁止接入，
也不把另购普通 API 作为代码适配前置。当前计划不包含凭据值或已获豁免的声明。

**执行和验收**

| 顺序 | 工作与产出 | 退出条件 |
|---|---|---|
| R0 复用与 provider 契约 | 固定官方源码 identity；列出 Agent 最小 profile 的 channel、入口、唯一 owner、代理路由、缓冲/线程预算和替换集合；以已知非私人文本验证 MiMo 对话请求格式 | 得到可构建的互斥 profile、可重放的脱敏协议夹具和真实接口验证记录；构建/接口各自标状态。选定 R1/R2 的单一路线；硬件预算未过不切换当前 BKVoice 产品基线 |
| R1 真实语音 provider | 按 R0 选定路线补 MiMo 整段 ASR、对话和流式 TTS：BKVoice 路线在 Gateway 编排，Agent 路线复用官方对话层并补语音 provider seam。只实现所选路径，固定回复用于故障回归 | 分别验证实际语音识别、可变文本回答、可解码 PCM；拒绝超长/损坏音频，断流和取消有界；记录供应商 PCM tuple、首包和重采样结果 |
| R2 AIDK 物理 PTT | 修复已有 Gateway 连通性，给选定唯一 owner 接入 PTT 和眼睛状态；先 10 轮冒烟，再完成 M2 的 50 轮及 30 分钟运行 | 按下收音、松开释放 MIC、收到回答后播放、完成回到空闲；断网/服务重启/超时/取消/旧 generation 不泄漏资源，记录端到端 P50/P95、堆栈和 deadline。固定音调不能代替真实问答证据 |
| R3 Android 首次联调 | 基于已工作的真实链路接 `console-v1` 状态、音量/persona 生效；修复并启用合适的 KVDB 后端，再完成安全认领与 BLE 配网 | 手机实际设置能在板端/会话生效，断电读回与失败回滚通过；凭据配置和设备注册闭环，不把隐藏串口装载当成 App 配网 |
| R4 独立扩展（可并行） | 官方词多人 KWS；随后非比赛版本“小冰”；IndexTTS 音色、OTA/资产、视觉传输按既有里程碑推进，不等待 R3 全部完成 | 各自通过模型、资源、权限与实板门才开启。“小冰”、最终音色、全双工 AEC 不阻塞首条 PTT 问答或比赛交付 |

R0 的接口/预算确认完成后即进入 R1，不为列举更多框架继续延长调研。对话协议验证与
Gateway 连通性排查可以并行；物理端到端验收需要两者都通过。已有摄像头约 30 fps、v321
存储一致性证据继续复用；只有变更触及共享时钟、DMA、电源或并发时才补对应回归。

2026-09-07 R0/R1 执行更新：核对官方 Agent 提交
`41723c61725c4e845bfee724f3ad2fafc416b6e1` 后，首条问答选定现有 BKVoice→Gateway 路线。
`voice_channel.c` 的流式语音仍直接注册火山，`audio_capture_close()` 丢弃公共 recorder
stop/close 结果且返回 void，语音通道还打印转写正文；这些先于资源预算构成替代缺口。
因此本轮不启用 Agent profile，不声称完成其构建或实板门；保留官方公共采播 API 与现有 bridge。
该选择没有新增 AP/CP owner、公共命令或固件配置，删除集合为空；只替换 Gateway 所选回复来源。

`gateway/shaniu` 已增加显式 MiMo provider：有界 PCM/WAV ASR、关闭 thinking 的短文本对话、
SSE TTS、FFmpeg 流式重采样和固定帧下行。供应商凭据仅从 Gateway 的 0600 文件读取。
现有固定回复仍为默认夹具；取消、超时、窗口和错误关闭共用同一连接 owner。
真实 MiMo 请求和本机 WSS 问答已通过，WAV 输出实测 24 kHz/mono/S16；
完整实板 PTT、原始 PCM 的板端速度/听感及 50 轮门仍待运行验证。
`make -C gateway/shaniu test` 的 31 项测试通过，包含本地 HTTPS ASR/chat/TTS 到真实 WSS、
鉴权/重定向/截断错误、取消/超时/下一轮、尾帧、首包早于 EOF、FFmpeg 满管道退出及重采样。
验证期间发现并修正了 FFmpeg 输入探测延迟和取消后等待读取管道的问题。
本地模拟测试不能替代云端或实板证据。没有新固件构建、刷机或 SDK/NuttX 修改。

用户提供凭据后，本机客户端经生产 Gateway 调用真实 MiMo，完成 1 轮问答、352 帧下行，
首音 2830 ms、协议错误 0。首条技术短句识别未完全匹配，第二条普通短句编辑距离为 0；
尚无整体识别率或 P50/P95 结论。实测 gzip 响应解压问题已修复，31 项回归通过。
COM8 可打开，但未取得 `bkvoice status` 响应，暂未装载设备配置或开启物理录音。
细节见 [MiMo/Gateway 真实验证](../../verification/bk7258/2026-09-07-mimo-gateway-live.md)。

<a id="feature-completion-execution"></a>

### 17.6 功能完善计划与实施批次

2026-09-07 用户授权：按以下计划持续实施。沿用现有 BKVoice/AP 唯一音频会话 owner、

本节中的旧 PTT 长按 3 秒、PTT 作为首条语音 gate，以及“唤醒未过则继续 PTT”等表述均为
历史执行记录，已被 2026-09-09 产品要求标记为 superseded；保留日期和证据，不得据此判断当前产品完成。
Gateway 云端服务和原生 Android；首条主线不等待框架迁移或最终音色训练。
本节细化 R2–R4 的执行批次，里程碑及验收状态仍统一维护在本文。

| 批次 | 实现与复用范围 | 完成条件与依赖 |
|---|---|---|
| F1 板端语音闭环（首要） | 恢复 COM8 启动/状态响应，核对当前镜像；接通 Wi-Fi 与受控 mTLS；复用现有 PTT、公共 recorder/player、双眼状态和 MiMo provider | 先完成 10 次真实按键问答，再做 50 轮与 30 分钟运行；测首音 P50/P95、MIC/DAC 回收、取消/断网/重连。当前阻塞是无串口响应；不得以主机 WSS 通过替代 |
| F2 连续对话（可立即实施） | Gateway 增加每个连接独立的有界短期上下文；按既有五种 persona 设计设置入口；复用 MiMo 请求、流式 TTS 和连接取消逻辑 | 首先完成多轮上下文：正常轮进入历史，取消/失败不进入；会话间隔离、断连清空、超限裁剪通过。persona 的 App 写入与生效在 F3 完成；不把短期上下文称为长期记忆 |
| F3 Android 控制台与心情 | 对接已有 Kotlin `console-v1` 客户端；Gateway 实现设备绑定、鉴权、状态快照/事件、音量/persona/取消操作；设备回报实际状态 | 首次联调先做在线/监听/思考/说话状态与取消，再做真实音量、五种 persona。重复请求幂等、旧 generation 拒绝、断线补状态；App 显示实际生效结果，不能只改 desired 值。实体设备反馈依赖 F1 |
| F4 认领、配网与持久配置 | 复用适合当前 host 的 BLE GATT、Wi-Fi 与 KVDB；先闭合后端短写/提交语义，再接 Android 认领与配网；设备凭据由认领流程供应 | 20 次发现/配网/错误密码/取消/重连/解绑；配置提交失败可恢复、掉电后可读；凭据保护和本地确认通过。F3 的受控联调无需等待 BLE 全部完成，交付前需移除对隐藏串口配网的依赖 |
| F5 官方唤醒（独立推进） | 复用 TFLM microfrontend/INT8 DS-CNN runner，准备多人语料、训练“你好，openvela”；补连续监听、pre-roll、VAD 和与 PTT 共用的会话入口 | 目标构建、独立说话人测试、连续负例 FAR/FRR、远场/噪声/扬声器回放、1 小时资源和 PM 恢复通过。首版播放时停唤醒；英语官方词另验。“小冰”保留到后续非比赛版本 |
| F6 视觉与屏幕 | 复用已验收摄像头/V4L2、SDIO 和双 framebuffer；先显式拍照→Gateway VLM→语音/眼睛反馈→App 图片，再做受控 MJPEG 预览 | 单帧 100 次成功/取消/超时回收；预览先 2–5 fps、单 viewer，后台/锁屏/断线停止；30 分钟运行后再提高传输帧率。录像约 30 fps 不等于手机传输已支持 30 fps |
| F7 OTA、音色与长期记忆 | 固件 OTA 复用 CP/AP 配对升级；补 App 更新状态、版本服务、签名资产下载/回滚；Gateway 后续接 IndexTTS 和可关闭、可删除的长期记忆 | 升级成功/失败/断电回滚且设备唯一数据保留；音色经授权、比较、签发和实板播放后才启用；长期记忆须独立授权、按设备/用户隔离、导出/删除验证。三项分别交付，不互相阻塞 |
| F8 组合体验与发布 | 实测 AEC、双屏刷新与音频、传感器事件、电池/温度、NFC；整合 App 首次配网→对话→视觉→更新→恢复演示 | AEC 做同条件开/关对照；全双工仅在其声学和并发门通过后开启。T5-Board/T5AI-Core 不测马达；AIDK 按实际接线验收。正式交付前再跑受影响板型构建与完整回归 |

依赖主线：F1 → F3 的实板联调 → F4 的首次使用闭环；F2 可在等待板端时先完成。
F5 的语料/模型、F6 的 Gateway/App 协议工作可独立进行，实板和 GPU 同时只有一个操作者。
F7 按更新、音色、记忆分别推进；先保持默认 MiMo 音色和半双工可用，再做体验优化。
不在缺少板端、手机和训练数据时承诺固定完工日期；每批通过即更新实际证据和下一缺口。

**本轮立即实施 F2 的短期上下文。** 可变历史由单个 Gateway 连接持有，限制最近 4 个完整
问答及总计 8192 字符；ASR/LLM/TTS 生成结果先待提交，只有 Gateway 成功发出本轮
`TTS_END` 后才进入历史。这表示网关已发送完成，不表示板端已播完；设备播放错误需丢弃
最近一轮上下文。取消、超时或错误丢弃待提交轮；连接关闭清空历史，不写文件、不跨连接恢复。
模型只得到固定 system policy 加成对 user/assistant 历史，不把转写提升为 system 指令。

F2 保留唯一服务命令 `python -m shaniu_gateway`，把共享无状态回复回调改为每连接创建的
会话对象，由现有连接 owner 调用完成/取消/关闭；移除旧回调注入入口及对应调用，避免两种
历史 owner 并存。公开 wire 协议、板端 owner、供应商密钥来源不变。
跨设备持久会话、自动情绪推断和 App persona 已生效不属于本轮完成声明。

每批提交按现有授权范围整理最小改动集，先区分已合并代码和工作区候选；临时探针、密钥、
模型私有素材、原始音频及日志不提交。更新主计划及日期化验收记录，不创建第二份任务清单。

**2026-09-07 实施结果：** F2 短期上下文已实现，Gateway 37 项测试通过；真实 MiMo 经
本机 WSS 连续两轮完成，第二轮携带第一轮上下文并正确引用测试信息，协议错误为 0。
首音分别 2470/1937 ms，仅为两个样本。详见
[多轮对话验证](../../verification/bk7258/2026-09-07-mimo-multiturn.md)。
F1 仍需恢复并核实 COM8 状态，最近一次读取无响应，本轮未重测串口；F3–F8 尚未按本节
完成验收，不能把主机测试解释为实板或 Android 全链路完成。

**F4 前置进展（2026-09-07）：** KVDB FILE I/O 缺陷已整理成团队维护补丁，
12 个故障/正常子场景及既有偏好 API/CLI 测试通过；上游未打补丁时其中 9 个场景失败。
详见 [KVDB I/O 验证](../../verification/bk7258/2026-09-07-kvdb-file-io.md)。
补丁尚未接入板级构建，持久化保持关闭。掉电恢复是下一独立实现门：当前 FILE 使用
O_TRUNC、commit 无同步；NuttX VFS 覆盖 rename 先删目标，不能以临时文件替换直接宣称
原子提交。本项不重开已通过的 SDIO 存储一致性基线。

**F4 恢复实现进展（2026-09-07）：** 继续复用 UnQLite 原生事务日志作为持久化候选，
已补日志开启、显式提交/失败回滚、errno 归一和 DIRECT 错误反馈；真实故障注入又定位到
引擎忽略同步错误，已另存最小引擎补丁。主机真实数据库的同步失败与同步点中断恢复通过，
见 [UnQLite 恢复验证](../../verification/bk7258/2026-09-07-kvdb-unqlite-recovery.md)。
目标文件锁、目录同步/flush、资源预算和补丁构建集成仍待闭合，板级持久化继续关闭。
持续执行已获授权，以上缺口不要求用户逐项再次发出“继续”。

**F2/F3 人物风格实施边界：** 保持现有五个 persona wire 名称；Gateway 的单连接会话
持有当前风格，固定 system policy 加白名单风格文本。既有服务命令增加启动默认风格选项，
会话仅在无进行中/待提交回复时切换，下轮生效且保留有界历史；拒绝任意 system prompt。
不新增另一套对话入口，不改变 emotion，不把默认配置当成 App 已获权的控制请求。
Android 的鉴权设置入口尚未连接前，只声明服务配置/会话能力，不声明手机设置已生效。

**人物风格实现结果（2026-09-07）：** 五种风格已加入 MiMo 请求，服务默认风格参数与
连接内切换能力完成；Gateway 40 项测试通过。验证下轮生效、保留历史、会话隔离、
进行中/待提交拒绝切换和固定 policy 保留。见
[人物风格验证](../../verification/bk7258/2026-09-07-gateway-persona.md)。
Android 鉴权控制端点尚未接通，人物情绪/音色质量没有据此宣称通过。
音量源码核实已有 `media_policy_set_stream_volume` 桥接；后续直接接该公共入口，
不重新实现 DAC 增益路径。

**人物风格真实接口进展：** 五种配置各用同一非私人输入调用生产 MiMo provider，
5/5 请求成功且文本可解析，单次 796–3799 ms；只有单样本，不代表风格质量或延迟分位数。
见 [人物风格真实接口验证](../../verification/bk7258/2026-09-07-gateway-persona-live.md)。

**F4 提交状态反馈边界：** 新增目录同步/日志删除故障注入已通过，修复引擎的错误吞掉。
日志删除后的同步失败会隔离句柄，重开后可能是新值，设置反馈必须读回协调，不能保证
所有提交错误都恢复旧值。见
[目录提交验证](../../verification/bk7258/2026-09-07-kvdb-directory-commit.md)。
已完成的 128MB 容量目标按用户确认收尾，当前主计划已登记为持续执行目标；COM8 最新
只读探测仍为 0 字节，未复位或刷机，实板闭环等待控制台恢复。

**音量生效实现进展：** 已把偏好音量接到下一轮 PTT 回复的公共 media policy；范围查询、
设置和读回通过后才播放，失败保留清理路径。轻量 player bridge 补齐范围/档位查询，
不新增 DAC 控制路径。开/关偏好主机测试、8 个故障/正常场景、6 个量化边界以及两个
AIDK AP 对象编译通过，见
[偏好音量接线验证](../../verification/bk7258/2026-09-07-voice-preference-volume.md)。
板级偏好仍默认关闭；需要后端启用、完整镜像和实板播放后才能声明音量验收通过。

**F4 目标端预检进展：** 事务引擎、KVDB 后端及 DIRECT 接口已在临时配置下用 AIDK AP
工具链编译通过。现有任务 TLS 和文件锁桶均为 0，正式启用需要解析新的配置并完整链接。
NuttX 目录 fsync 当前为空成功，FAT 删除自身刷新缓存，掉电语义仍需实板验证；不能将
Linux 恢复测试视为目标端保证。见
[KVDB 目标端预检](../../verification/bk7258/2026-09-07-kvdb-target-preflight.md)。
本预检阶段未改板级配置或刷机。

**F4 构建消费进展：** CMake 已接入团队补丁，按偏好开关在输出目录替换 KVDB/UnQLite
编译源，官方 checkout 保持不变；重复配置及真实引擎恢复测试通过。见
[构建消费验证](../../verification/bk7258/2026-09-07-kvdb-build-integration.md)。
板级配置仍关闭；下一步闭合持久路径、挂载与 SD NAND/MSC 介质所有权，然后完成目标
配置解析、完整链接和实板读回。此前临时对象编译不替代这些步骤。

**F4 介质接线进展：** 偏好读写已接共享 media volume，占用期间短时挂载，关闭数据库、
卸载后才释放；失败保留状态并在下次重试清理。生命周期和排他测试、三个 AIDK AP
对象编译通过，见
[偏好介质接线验证](../../verification/bk7258/2026-09-07-preferences-storage-lease.md)。
下一步是实际 Kconfig 配置与完整链接、实板读回；播放前读取遇录像/MSC 占用仍会失败，
并发体验与偏好缓存策略尚未闭合。板级偏好仍未启用。

**F4 完整链接进展：** `drivercheck_ap` 诊断配置已启用偏好后端，完整 MCUboot 链接验证
通过；最终配置、三个补丁副本编译源及 AP ELF 的 KVDB/TLS/文件锁符号均已核对。
AP 裸二进制 856424 字节，见
[完整链接验证](../../verification/bk7258/2026-09-07-preferences-full-link.md)。
生产 `openvela_ap` 仍未启用偏好。COM8 本轮状态查询仍为 0 字节，无刷机或实板读回；
软件继续完善播放期间已加载偏好的使用与失效策略，实板闭环等待控制台恢复。

**F3/F4 播放音量进展：** 已确认音量进入偏好层缓存，播放不再每轮读盘；成功设置更新，
不确定结果失效，显式偏好查询刷新。主机缓存/播放生命周期测试及完整 MCUboot 构建通过，
见 [播放音量缓存验证](../../verification/bk7258/2026-09-07-playback-volume-cache.md)。
首次加载与失效重读仍依赖介质；实板并发和 PTT 闭环未通过，Android 鉴权控制接线仍待完成。

**F3 设备寻址进展：** Gateway 已支持操作员证书登记，以验证过的 mTLS 叶证书绑定设备 ID，
HELLO 后上线、重复连接拒绝、断开/超时释放。46 项 Gateway 测试通过，见
[设备绑定验证](../../verification/bk7258/2026-09-07-gateway-device-bindings.md)。
这是控制寻址基础；Android bearer 授权、console-v1 端点及板端状态上报仍未接通，
不能将预登记等同于 BLE/App 认领。

**F3 Android 状态表达进展：** 首次连接尚未上报的音量/充电状态改为显式未知，协议、
状态存储和界面已贯通；43 项 Android 单元测试及调试 APK 构建通过，见
[Android 未知状态验证](../../verification/bk7258/2026-09-07-android-unknown-state.md)。
尚未安装手机；该阶段的 HTTPS 端点待办已由下述批次接续，不以固定默认值替代板端上报。

**F3 HTTPS 控制接线进展：** Gateway 已实现独立 console-v1 HTTPS/WSS 入口、设备范围
bearer 授权、快照/事件和真实 MiMo 人物设置，包含持久 generation、revision 检查及请求去重。
Python 与 Android 共用协议向量；51 项 Gateway 测试、44 项 Android 测试及 APK 构建通过，见
[HTTPS 控制验证](../../verification/bk7258/2026-09-07-console-https.md)。
未知遥测保持未知；操作员授权不等同 BLE 认领。远程取消、音量和 OTA 尚未接线，人物
设置尚未持久化；下一步推进取消与板端控制，实板 PTT 和手机端点部署验收仍未完成。

**F3 远程取消接线进展：** 板端 companion-v1 已修复同轮迟到 CANCEL 在 IDLE 被拒绝的问题，
允许正常结束或本地取消后同一非零轮次的幂等取消，仍拒绝旧轮、重放和未就绪会话。
新增主机测试先复现失败，修改后 `run-voice-companion`、`run-voice-session` 和
`run-voice-gateway` 通过；会话测试确认完成后的迟到取消不重复释放 DAC，并可进入下一轮。
通过唯一 CLI 在既有隔离诊断输出中完成 MCUboot 增量构建，未刷机。
当前 TTS_END 同步排空播放；此修复不证明能抢占排空等待。后续需接通带轮次校验的
Android/Gateway 取消请求及实际停止确认，console 取消入口尚未开放。

**F3 Gateway 取消发送进展：** `GatewayConnection.cancel_turn(turn_id)` 已能按精确轮次
停止 provider、清空 PCM 并发送实际 companion-v1 CANCEL；与上行状态处理串行，旧轮调用
不影响新轮。取消交叉到达的上行 PCM 保留序号、身份、窗口和时长上限检查后丢弃，不进入
模型；下一轮恢复正常处理。53 项 Gateway 测试通过，包含本地 mTLS 真连接上的取消帧、
provider 清理与下一轮继续，以及协议重放/旧轮拒绝。此处仅证明发送，不是板端停止 ACK；
Android 请求轮次关联及 HTTPS 取消入口仍待接通，未新增实板验收。

**F3 HTTPS 取消入口进展：** 已接 Android 现有 `turn.cancel` 空参数请求；console revision
随新轮次推进，旧快照取消被拒绝，即便新轮开始后尚未轮询快照也不会误取消。
每设备写请求串行化，并发同 ID 重试共享回执、只发一次 CANCEL。本地 HTTPS/mTLS
集成覆盖实际取消帧、并发重试和旧快照保护，54 项 Gateway 测试通过。Android 受理提示
明确为“取消已发送；尚未确认板端停止播放”。物理停止 ACK 与排空抢占仍待实现验收。

**F3 板端取消回执进展：** 串行会话清理成功、线程退出且回合空闲后发送同轮 ACK，
完成后的迟到取消也可确认；清理失败无成功回执。ACK 在 IDLE 保留轮次，发送前验证会话
及轮次。协议/Gateway/会话主机测试通过，包含停止失败注入；AIDK 诊断 MCUboot 增量构建
通过。Gateway 在收到同轮 ACK 前显示 cancelling，普通 ACK 不确认，54 项 Gateway
测试通过。Android 已接此等待状态；物理音频停止效果和同步排空抢占仍未验收。

**F3 立即停止接口进展：** 官方 `frameworks/multimedia/media/include/media_player.h`
明确规定 `media_player_close(handle, 0)` 立即停止，参数 1 等待完成。团队播放器此前忽略
该参数，现已按参数选择是否排空；准备失败清理也不排空，正常 `media_player_stop` 行为
保持。AIDK 诊断 MCUboot 增量构建通过，SDK/官方框架未改。取消仲裁仍需切换到立即关闭
路径，尚不能宣称播放抢占完成。本轮 COM8/115200 查询依旧 0 字节，未刷机。

**F3 取消立即关闭接线进展：** 回合仲裁仅在正常 TTS_END 清理时排空；取消、超时、
故障及恢复走停止/释放。音频适配的停止与释放复用 `media_player_close(handle, 0)`，
关闭成功立即清空句柄，失败保留重试所有权。回合、音频生命周期、会话与 PTT 主机测试
通过，新增关闭连续失败及恢复测试确认无排空调用，正常播放排空测试仍通过；AIDK
诊断 MCUboot 增量构建通过。未刷机或测量停止延迟。已进入正常 TTS_END 同步排空的
串行处理仍不能被随后排队的取消抢占，这个边界继续处理。

**F3 取消确认超时进展：** 5 秒未收到同轮 ACK 时，console/Android 转为
cancel_unconfirmed，明确停止结果未知；不清除待确认轮次，迟到有效 ACK 可恢复。
55 项 Gateway 测试及 Android 测试/APK 构建通过。源码核对确认 TTS_END 在 Gateway
锁内同步排空，RX 线程只排队；直接从 RX 线程关闭播放器会越过会话校验及清理所有权，
因此未采用该做法。同步排空抢占与实板停止延迟仍待闭合。

**F3 非阻塞播放完成改造边界（待实施）：** 已核对官方
`frameworks/multimedia/media/include/media_player.h` 和 `client/media_graph.c`：buffer 模式
通过 `media_player_close_socket()` 结束输入，并由已注册事件回调报告
`MEDIA_EVENT_COMPLETED`。团队播放器已有回调登记，但缺少 close_socket 结束输入实现；
语音适配目前未登记播放器完成回调。

- 对外仍使用现有 bkvoice/console 命令；播放 API 复用官方结束输入及完成事件，不新增
  SDK 私有取消接口。
- 播放适配拥有待播放缓冲、EOF 和底层完成事实；完成通知只发布结果，不从回调销毁
  播放器或修改 PTT。异步工作退出及回调静默必须先于句柄释放。
- 回合仲裁拥有等待播放完成状态和活动 token；runtime 串行处理完成、取消、超时，
  完成事件须校验会话/轮次。已经取消或换轮的迟到完成事件不得结束新轮或重复释放。
- 替换 TTS_END 中同步排空的调用链，正常尾帧仍须完整播放；取消调用立即关闭，并在
  清理成功后确认。原有显式同步 stop 的其他消费者不在本次删除范围。
- 验证需覆盖结束输入后主循环可处理取消、尾帧完成、关闭与回调交叉、清理失败重试、
  陈旧完成事件，以及最终实板播放/取消延迟。当前仅完成接口与所有权核查，未实现或
  验收该改造；不将既有主机取消测试当作此项通过证据。

**F3 播放器 EOF 后端进展：** 团队播放器已实现 `media_player_close_socket()`：后台
提交尾帧并等待完成，等待期间释放锁；立即关闭请求中止，清理/释放前 join，回调尚未
退出不得释放句柄，回调自关闭拒绝为 EDEADLK。EOF 后拒绝继续写入，重复结束输入不
重复建线程。同步 stop/close 对已有 EOF 工作先收尾，避免重复提交最终缓冲。
新增真实实现主机测试覆盖结束标记、完成前取消及回调与关闭交叉，纳入 host run；
播放器/录音适配测试及 AIDK 诊断 MCUboot 构建通过。语音回合尚未调用此异步 EOF
接口，下一步接入完成事件与串行回合处理；本轮没有实板验收。

**F3 TTS_END 异步接线进展：** 语音音频适配已登记完成事件，TTS_END 发出标准 EOF 后
进入 DRAINING 并返回；原同步排空清理分支已移除。runtime 每轮在接收队列处理后轮询
完成结果，后台只发布结果，停止/释放和状态切换仍由串行回合所有者执行；异步失败
清理后断开网络会话。无 TLS 配置由相同服务线程轮询。等待期间取消可立即关闭并 join
后台，关闭前回调静默及下一次 prepare 清除旧结果防止跨轮完成污染。
回合、音频、会话、PTT 与音量主机测试通过，包含等待期间取消、关闭失败恢复、正常
尾帧完成和迟到结果不结束下一轮；AIDK 诊断 MCUboot 构建通过。此为主机/构建证据，
尚未刷机测量真实取消延迟或完成整机验收。

**F1/F3 正常播放完成回执进展：** 会话轮询在异步播放结束并成功清理后发送同轮 ACK，
等待期间不发送，重复轮询不重复发送。Gateway 分开记录发送完成和 playback_confirmed，
取消/错误清除正常播放待确认记录，重复 ACK 不重复计数。会话主机测试、55 项 Gateway
测试及 AIDK 诊断 MCUboot 构建通过；TLS 夹具按临时证书 notBefore 做有限等待，未放宽
证书校验。Windows 枚举确认 COM8=CH340、COM16 存在，未发现 BK Loader 进程；已请求
开发板完全断电上电以恢复日志，本轮未烧录或取得新的实板功能证据。

**F2 上下文确认进展：** 已将 MiMo 历史提交移至对应正常播放 ACK；发送 TTS_END 仅
保留待确认对。下一轮开始而旧轮未确认时丢弃旧记录，重复 ACK 不重复提交，播放错误
仍可撤回最近一轮。新增本地真实协议/模拟 MiMo 集成验证未确认轮不入上下文、确认轮
进入下一次请求；连接隔离与播放错误回滚测试保持通过，共 56 项 Gateway 测试通过。
旧固件不发送完成 ACK 时仅保留独立轮次，不假定旧固件已播放；新固件实板连续对话
仍待验证。本轮没有外部模型调用、烧录、提交或推送。

**F3 App 播放状态进展：** console 在 TTS_END 后继续显示 speaking，正常播放 ACK 后
才回到 idle；5 秒未确认显示 playback_unconfirmed，Android 明确停止结果未知。
等待播放/取消确认时人物切换返回 409；已确认空闲时取消返回 409，保护已完成的上下文。
真实本地 HTTPS/模拟 MiMo 测试覆盖等待、超时、迟到 ACK、上下文提交及空闲取消保护；
57 项 Gateway 测试和 Android 测试/APK 构建通过，未实板验收。

**F3 音量协议进展：** C/Python 已支持可选 HELLO 能力位图及音量 GET/SET/REPORT，
包含百分比、结果及原请求序号校验；空 HELLO 的旧固件不开放音量控制。
新增协议边界与重放/重连测试通过，C companion/gateway/session 回归、Python 8 项协议
及 16 项 MiMo 测试通过，AIDK drivercheck CP/AP MCUboot 增量构建通过。
板端已接入可选音量回调，有回调才声明能力；音频后端启用 PREFERENCES 时复用媒体
策略设置与读回接口，按策略档位换算百分比，运行期设置跨语音轮次保留、不写偏好数据库。
回报绑定请求序号；驱动错误/非法读回标为未知，保留连接。读回是策略层结果，不是
独立 DAC 寄存器或声学测量。新增量化、设置失败、跨轮次保留及分发测试通过。
Gateway 已接入请求匹配、忙拒绝、5 秒超时和断线清理；迟到/无关回报不改变状态。
console 快照查询音量，volume.set 等待板端确认并缓存回执，成功快照显示量化读回值；
未知时 Android 滑条保持禁用。Android 增加音量错误码及运行期设置提示。
62 项 Gateway 测试通过，含本地 HTTPS→WSS 查询、设置、确认前等待、超时回执幂等
及再次查询恢复测试；Android 音量错误回执解析测试和 APK 构建通过，超时文案明确
生效结果未知。实板声音变化、重新连接与重启行为仍需验收。
本轮 COM8 115200 状态查询实际返回 0 字符，端口已释放，尚无新的实板启动证据。
本批无烧录、实板验收、提交或推送。

**F4 当前增量（2026-09-07）：** 已实现 TLS 内 SPV1 认证、板上确认门、递增序号和
分片整包上传；SCB1 包含 Wi-Fi/Gateway/CA，复用 BVC1 的 DER/密钥匹配校验。
新增纯校验接口不设置系统时间；正式加载保持原行为。文件提交保存完整配置、修订号
及事务 ID，覆盖短写、同步失败、提交未知和损坏文件拒绝；异步提交断线后保持未知，
不得中断存储后释放其上下文。私有存储 worker 已接产品启动，独立持有提交数据；
阻塞 fsync 时轮询/取消不会等待文件完成，结果不确定时禁止刷新或停止来清除状态。
网络试连回滚和按键/广播窗口仍未接入。
AP 异步 Wi-Fi API 复用已有控制 worker；CP 可在固定 SD NAND 主拓扑之外挂载
CSV 既有 persistent_data LittleFS，AP 通过 RPMsgFS 访问；未修改分区容量、格式化或
将私有目录导出 MSC。构建不能证明目前板上目录已经可用。

Android 总览已接“认领设备与 Wi-Fi 配网”：系统文件选择器导入认领凭据/CA，
运行时权限、30 秒定向服务扫描和设备选择后调用实际 GATT/TLS 连接。
界面不保存 Wi-Fi 密码；APPLY 前同步保存公开设备/事务 ID，保存失败禁止发送；
未确认结果跨 App 重启保留并阻止重复提交。已接只读 QUERY 和“核对上次提交结果”
入口：复用 TLS/秘密认证及本地确认；恢复窗口没有写入后端，拒绝 BEGIN/DATA/APPLY。
仅匹配已提交事务或明确空配置时清除 App 回执，其他结果保留未知。板端窗口与存储查询
回调尚待产品 owner 接线，不能视为实机恢复闭环。62 项 Android 单元测试/APK 构建、真实 mbedTLS
主机联测及 AIDK CP/AP 增量编译通过；网络步骤仍为测试回调，非实板配网。
当前 ADB 无设备，未取得新的手机/板端互通证据。本批未烧录、提交或推送。

### 17.7 当前未闭环总清单（2026-09-07 源码复核）

本节汇总 F1–F8，补齐此前逐批反馈遗漏的接线和验收项。第 7 节及早期 M2/M4 等
日期化记录属于历史快照，不能用其中的旧帧率、Android 状态或摄像头诊断故障替代当前
状态。已合并的 128MB SD NAND 一致性及摄像头帧率基线继续复用，不重开 1GB 支线。
下表“待实现/接线”表示当前产品路径未闭合；“待实测”不表示测试已经失败。

| 编号 / 主线 | 尚需完成的具体内容 | 当前证据及闭合条件 |
|---|---|---|
| U01 / F1、F3 手机入口 | 小米 10 的 ADB 授权、APK 安装启动、权限拒绝/重试、旋转/后台/锁屏/重连；真实手机 SSLEngine 与 AIDK mbedTLS 互通 | 已实际识别 Mi 10 / Android 13，ADB 授权通过；APK 0.5.0-a1 安装成功，主 Activity 冷启动 Status: ok、633 ms，UI 树确认主界面和配网按钮。MIUI 模拟输入权限在用户设置/重启后复测 KEYCODE_WAKEUP 成功；已解锁并进入配网页面，SCAN/CONNECT 权限读回为 granted=true，扫描启动与 30 秒结束提示已实测；发现并修复超时后仍显示扫描中的文案。扫描中回到桌面停留 3 秒再返回已实测显示扫描暂停。权限拒绝/重试、事务取消、TLS 与三端功能仍待实测。手机已由用户取走，App 日常调试改用现有 API 36 模拟器。修复并实测系统栏安全边距、隐私/更新返回设置、设置返回首页、配网返回首页，以及无设备时的添加入口；65 项 JVM 测试通过。模拟器用于界面和客户端流程，不替代真实 BLE/NFC、板端音频和三端验收 |
| U02 / F1 实板语音 | 核对当前板端镜像与启动；PTT→真实 MIC PCM→MiMo→扬声器；检查采样率、播放速度/听感、释放资源 | 真实 MiMo 的本机客户端验证已存在；当前组合未完成 10 次按键问答、50 轮、30 分钟及首音 P50/P95。COM8 当前已恢复响应；18.6.329+389 已烧录并确认启动，CP/AP/CPU2 健康；Wi-Fi 联网及 BVC1 RAM 供应通过。临时放行后手机 TCP 通过，头部采集确认 NAT 来源为 192.168.1.7；板端 CONNECT 仍超时，同窗口未见其连接包，Gateway HELLO 未建立。临时防火墙规则已删除。后续 AP proc 读取尝试后 COM8 暂无回应，软件 reset reboot 未取得启动回读，用户报告重上电后仍未取得 NSH；COM8 CH340 与原生 COM16 PnP 状态正常，控制台恢复待处理。源码核对发现语音使用普通 socket，而 AP Wi-Fi/HTTPS OTA 已使用 psock 避免共享描述符表争用；语音 psock 候选已完成正式 CP/AP 构建、5 项 TLS 与 9 项并发主机测试，并生成同已安装信任根的 391 OTA，尚未安装或证明根因 |
| U03 / F2、F3 对话及控制 | 多轮上下文、播放 ACK、取消/迟到回执、断网补状态；人物模式与音量实际生效 | 源码和主机测试已有，仍需手机→Gateway→板端验收；不能把 App desired 状态或 Gateway 发完音频等同板端播完 |
| U04 / F4 身份供应 | 每设备证书/私钥、认领 secret 和所有者 bootstrap 的生成、安装与读取；Gateway 设备注册及 App 所有者凭据绑定 | 已新增 BPI1 证书/私钥/secret 供应、原子身份安装及启动读取；`voice pairing` 生成 mode-0600 激活资料，App 一次导入设备 pin、证明和 Gateway 路由。身份经持久化后才 bind 配对 owner。主机测试及诊断/正式配置构建通过；真实 BPI1 串口上传完成但提交返回 EXDEV：产品代码误将 RPMsgFS 类型要求为 LittleFS，修复已构建待安装。`voice pairing --resume` 校验并重用原激活资料，主机测试确认上传字节一致且不覆盖原文件；私有持久化、Gateway 注册与 App 所有者凭据绑定仍待闭合，MiMo 密钥只留 Gateway |
| U05 / F4 窗口和按键 | 产品 worker 调用 pair start/step/confirm/close；本地开窗、二次确认、超时、停止广播、断开连接；与 PTT 按键归属互斥 | AP 20 ms 语音任务接入配对 owner，持久化身份供应/启动加载后实际调用 bind；3 秒松开首次认领、8 秒松开只读查询，认证后需新按键确认，120 秒窗口及断线清理。主机状态机/TLS 与 CP/AP 构建通过；实板窗口、按键与手机互通尚未验收 |
| U06 / F4 网络事务 | SCB1→BVC1；异步 Wi-Fi 试连→受认证 Gateway HELLO；成功再调用文件提交；失败/取消恢复旧网络及语音配置 | 产品 network owner 已串接 SCB1→BVC1、AP Wi-Fi 独占试连租约、真实语音 Gateway HELLO 和异步文件提交；取消/确定失败执行网络及语音回滚，不确定落盘保持隔离。实际控制辅助函数及产品回调主机测试覆盖错误密码、Gateway 拒绝、取消和提交时断线；实板网络事务仍待验收 |
| U07 / F4 私有持久化 | 首次私有目录初始化、CP 挂载/AP RPMsgFS 就绪与重试、启动加载配置、损坏处理；可信时间恢复/刷新 | 文件任务仅在已挂载片内 LittleFS 上创建私有目录，不自动格式化；身份与配置分开保存，runtime 重试挂载就绪、加载身份并恢复已提交连接。复用官方 NTP 单次客户端与 DHCP DNS 供给，在重启 Gateway TLS 前获取新时间；保留证书日期校验。NTP 为普通网络校时，不能称为认证时间。持久化/恢复/校时主机测试及正式配置构建通过；实板只对已核验全 FF 的片内私有区显式初始化 LittleFS。AP 的 RPMsgFS 类型兼容修复待安装；真实介质掉电和坏配置仍待验收 |
| U08 / F4 回执恢复 | 将只读恢复窗口接实际 storage receipt；已认领设备只开放本地确认后的查询；提交时断线/掉电/手机进程退出后核对 | 8 秒本地手势连接真实 storage receipt；App 导入身份后发现未决事务会进入核对路径，不再次索取 Wi-Fi 密码。不同事务及存储错误保留未知。身份供应调用已接通；提交时断线、掉电及手机进程退出的实板组合仍待验证 |
| U09 / F4 解绑与重配 | 本地授权解绑、旧所有者/令牌撤销、旧 bootstrap 失效或轮换、换 Wi-Fi/Gateway 的配置更新及回滚 | 当前首次认领默认拒绝 already_claimed；只读查询不是重配或解绑。20 次认领/错误密码/重连/解绑总验收尚未完成 |
| U10 / F3、F8 状态与权限 | 电量、充电、固件版本、情绪和权限来源；手机授权→Gateway→设备能力执行与撤销 | console 仍返回未知电量/版本/情绪、未授予权限；`claimed=True` 来自受控注册连接，不能当 BLE 所有权证据。后端实际 mutation 仅取消、音量、人物模式，其他操作返回 unsupported_operation |
| U11 / F5 官方唤醒 | 多人通用“你好，openvela”有效模型、语料/独立说话人评测、MIC owner、pre-roll/VAD、播放时暂停/恢复 | 前端/runner/训练入口已有；50 帧 pre-roll、时间戳断层、连续语音/静音、空唤醒及最大时长已有纯 App 宿主验证。未完成正式模型、MIC/session 接线、门限标定、目标资源、FAR/FRR、远场/噪声、1 小时和 PM 恢复验收。英语官方词另验；“小冰”仅后续非比赛扩展 |
| U12 / F6 拍照理解/屏幕 | 显式触发→单帧上传→Gateway VLM→语音/双眼→App 图片；统一 Camera 占用与隐私指示；屏幕映射/状态/动画 | 复用已合并 V4L2/显示基础；尚需产品链路和 100 次成功/取消/超时、双屏 30 分钟并发及资源回收证明 |
| U13 / F6 视频预览 | Gateway relay、Android viewer、2–5 fps 单 viewer、背压和前后台自动停止 | 不把本地约 30 fps 录像称为手机视频传输；30 分钟运行、锁屏/断线/低电停止和与语音互斥门未闭合 |
| U14 / F7 OTA | 发布目录/版本与板型匹配、App→Gateway→板端升级、进度/重启确认、失败和断电回滚、唯一数据保留 | 底层配对 OTA 可复用；App 已按来源版本和空闲状态开放本地确认入口，Gateway 只向具备能力的绑定设备发送 manifest SHA-256，板端已有严格序号/摘要/阶段回报协议。三端 host 测试覆盖拒绝、超时、迟到回报和终态冻结；尚未接设备受信 HTTP 源、跨重启事务恢复及实板回滚，因此不能称为端到端升级完成 |
| U15 / F7 IndexTTS | Gateway provider 与默认 MiMo 切换、流式/取消/资源限制、授权音色选择、签名和撤销 | 历史离线评测不等于产品接入或实板播放验收。先保持默认 MiMo 声音，不阻塞基本问答 |
| U16 / F7 长期记忆 | 独立授权和可关闭开关、按所有者/设备隔离、存取策略、查看/导出/删除与撤销 | Gateway 已实现默认关闭、按设备隔离的私有 SQLite 记忆，仅在板端播放 ACK 后提交；撤销/删除清空，失败关闭写入。Android 只提供关闭和清除，不提供远程开启。81 项 Gateway 测试及 Android 构建已通过；所有者本地开启、查看/导出和三端实机验收仍未闭合 |
| U17 / F8 外设及体验 | AEC 同条件开关对照、音频/双屏/相机/存储并发、低电策略、姿态事件、NFC 场景、故障隔离和长稳 | 外设驱动或只读查询通过不代表产品策略通过；T5-Board/T5AI-Core 无马达，AIDK 按实际接线验收；首版继续半双工 |
| U19 / F4、F8 NFC 辅助连接 | 手机 HCE↔板端 ISO-DEP/APDU、前台触碰发现与 BLE 配对衔接；后续触碰交互 | 小米 10 实查支持 NFC/HCE；曾为 BLE/UI 联调临时关闭 NFC，当前已恢复开启，USB 常亮按授权保留；现有 MFRC522 驱动尚无 ISO-DEP/APDU 产品链路。按基础 PTT、App 控制及配网认领联调之后推进；不把随机 UID 当身份，不因贴近跳过证书、认领证明或本地确认。新增入口尚未实现，原有 NFC 外设基线不变 |
| U18 / F8 产品交付 | 将诊断配置已验证能力整合进正式配置；清理临时诊断后精确提交；APK/固件/Gateway 版本、签名资产、恢复及完整演示 | PTT、VOICE_TLS、PROVISION_GATT、CP 私有 LittleFS、双核 RPMsgFS 与单次校时已纳入正式 openvela_cp/openvela_ap，隔离构建通过并核对最终配置。已生成独立临时签名信任链并完成 389 全量烧录及启动核验；390 是沿用已安装 MCUboot 信任根的待装签名 OTA。实板验证、其余产品能力整合、精确提交及三端发布仍未完成 |

复核入口：`bk7258_product_lifecycle.c`、`bk7258_provision_pair.c`、
`bk7258_provision_storage.c`、`bk7258_voice_runtime.c`、`bk7258_voice_config.c`、
`gateway/shaniu/shaniu_gateway/console.py` 与 `server.py`、Android `MainActivity.kt`
及 `provision/`、AIDK 两组 AP defconfig。CodeGraph 发现后以当前源码调用点确认；
本次没有重新跑宽范围测试或把既有测试重复记为新验收。

用户已指出现有界面过于原始且暴露联调参数。首页第一版已在小米 10 安装启动并检查：
单一“添加傻妞”主要操作、陪伴/心情/设置三入口；地址/令牌/诊断信息移入仅调试包可见
的开发者联调页。配网页面已拆为找到设备→连接 Wi-Fi→确认结果三页，普通入口不挂载开发者参数视图；
仅开发者联调入口且调试包允许导入凭据/CA、配置服务地址。小米 10 已验证第一页
无技术字段/密码框、无提前下一步按钮、启动扫描、手动停止后恢复查找及返回首页。
未选择设备不能推进；身份/服务资料未就绪时停在第一步，不收集 Wi-Fi 密码，不伪造
添加成功。第二/三步已构建，但真实身份供应、选中设备后的配网/结果页仍未实机走通。
最终身份/证书/服务配置由供应与认领流程承担；人物素材、心情/相册及完整引导验收仍待完成。

**下一步执行顺序：**

1. U01 小米 10 ADB 可用后安装现有 APK，先验启动、权限和控制台；不等待 BLE 全功能。
2. U02/U03 接实板 PTT 和三端控制，复用已有 MiMo、音频及摄像头/存储基线。
3. U04–U09 接完整首次使用流程：身份→本地窗口→网络试连→私有提交→重启/回执恢复→解绑重配。
4. U10/U12 补真实遥测、权限、拍照理解和双屏反馈；U11 的语料/模型工作按资源独立推进。
5. U13–U17 按既定 Beta 边界分别验收，再做 U18 产品整合和发布；不因某个 Beta 项未过阻塞基础语音交付。

## 18. 尚待输入，不虚构日历

依赖结构和退出门已经确定，但日历排期还需要以下真实输入：

- 可持续使用的 AIDK 数量，以及刷机、恢复和独占实板时间窗口；
- 历史已指定小米 10；当前系统版本、连接可用性与调试 APK 安装条件待现场核对；
- Gateway 部署在同局域网主机、移动电脑还是远程主机；
- MiMo 赛事资源的实际调用配置与适用范围；后续 IndexTTS/KWS 的 GPU 可用时段；
- 实际参与开发、测试和美术的人数与每周可用时间；
- Contest 截止日期、演示环境和网络限制。

这些输入确认后，才能把 M0–M8 转换成周计划、责任人和日期。缺少输入时只使用依赖顺序，
不把估算日期写成承诺。

## 19. 执行与交付判据

用户已授权按计划开始实施，本文进入 `IN_PROGRESS`。第 18 节尚待输入仅约束相关部署、
日历和物理验收，不阻塞已授权且不依赖这些输入的实现。

每批完成后更新实现状态、实际证据和剩余风险；主机、构建、刷机、实板与用户体验验收分别
记录。比赛交付必须覆盖 Contest MVP、协议与设备身份边界、M0–M8 对应退出门；M7 的三个
独立 gate 和 M9 扩展保持各自边界。提交前核对当前远端基线与精确改动集，不整体纳入脏树。
执行过程中不静默扩张范围，也不把计划获批视为功能验收通过。


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
