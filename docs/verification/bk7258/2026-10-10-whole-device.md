# 标准整机 713：输入累加、显示覆盖层与原生 CDC 分层验证

## 固定输入与边界

2026-10-10 核对官方目标 `e59ac2bb1c33ad5dd501515428278c97995fea57`；
PR126—132 均未合入。126→127→128、129→130→132 是两组依赖，131 独立。
本地集成分支 `integrate/shaniu-standard-20261010` 用于组合验证，不向官方提交全功能重放 PR。
官方 manifest/CI 来源解析保留，Agent 使用已发布的固定提交。

| 功能 | 生产来源及 Agent 依赖 | 712 的实际包含情况 | 标准 713 与证据 |
|---|---|---|---|
| 回答偏好、请求策略 | PR126；Agent `403a127f` 起 | 缺少相关生产调用与偏好入口 | Agent `b058771e` 包含；标准产品启动加载 mode 0/revision 16；真实生产持久化/请求构造主机回归 |
| 首段终答 | PR128；Agent `b058771e` | 缺少短 JSON 决策到 final SSE 的实现 | 已编入同一候选；受控本地流测试通过，真实云/声学仍待条件 |
| 本地专注 | 原单一计时器、PR127；Agent `5cdef055` 起 | 有计时器与 NFC 专注，缺识别后的快捷路由 | 同一计时器复用；生产识别路由开始/查询/暂停/继续/取消及拒绝回归通过 |
| 动作/触觉 | PR129，复用单一 motion、显示和电机服务 | 已包含 | motion/haptic/display 配置均启用；真实静置样本与收音保护观察见下文 |
| NFC、本地内容 | PR130，原 NFC/场景/播放器/卷所有者 | 已包含 | NFC 服务启用；映射权限、驻留去重、内容缺失/损坏、取消/关闭主机回归通过；实体卡/owner 正向仍待条件 |
| 电脑任务、资源 | PR131，原 PC 授权/任务/正式安装器 | 已包含设备服务及对应主机工具 | 原生 CDC 产品所有者保留；真实程序任务/资源回执与取消的生产集成测试通过；该板正向授权业务未通过 |
| 按需相机 | PR132，原 vision/V4L2/PC camera 服务 | 已包含，camera CONFIG=21 | vision 配置启用；CONFIG 控制改 23，帧仍 22，避免与回答偏好 21 冲突；真实单帧与释放、主机传输测试分层记录 |

712 是 `1242847c25b21eddd5d9849c4f27b6cea1b52d24` + Agent
`20890a97b9515cce3de34006ad7a9b1746a109ed`，不能由计数递增推断语音成果累加。
目标 Agent `b058771e25efa918819f3d25264b877eb971186e` 确实包含 20890a97 的媒体修复；
上述缺口来自代码/配置/依赖比较，不是只比较提交新旧。

713 实际构建、烧板源码是 `80455f84417c557cc99710721ce3356cf64aa306`；
后续工具说明/错误分类提交 `ff82faa52f87ec077224d9941ae7b66a9bde69cc` 没有改变板端生产源码、配置或 Agent。
构建范围无未提交差异；范围外原有 `goal-objective.md` 保持原字节，SHA256
`9a0cef421e84804ae1936b047a27d49eeaabc768d7aa8aa93b2a149ccfdbe378`。
NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`、apps
`550cd3ba60a03f8ebf9ac7b72f6eed6aea3bedbe` 使用既有覆盖层；输入摘要分别为
`a71a6ba92af092490c60b8cfb18e86657874ea60fb8c4224da146deaa3a944a4`、
`2f631c1c4cbbd2acfae1978b238b21274e4409a8b5bd0a3ca49a802c122a10b9`，与 712 相同。
完整构建范围摘要 `122b0f3f208d5a4a7a78ca1879dd2226d9f3f7b01359d3498af732f6771802ee`。

## WAITING_ASSET 的第一处有效证据

先保存 712 状态和原始 UART，再部署增加只读观测的组合树。712 已出现真实
`RENDER PASS ... shaniu-cyan-v3 revision=3 screens=2 fallback=0`，底层 `last_error=0`。
713 快照进一步确认同一时刻 `state=2 overlay=1 error=0`，即认领二维码覆盖层。
`bk7258_display_onboarding()` 在设置 QR 时保留 WAITING_ASSET；不是资源不可读的充分证据。

正常资源路径实际完成卷租约/挂载、选择与包校验、解码、缓存及两个 framebuffer 提交。
重启后 uptime 12.333 s 和 141.144 s 再次记录生产 RENDER PASS，revision 3 不变；
中间是既有 120 s 认领窗口及重试。未改 READY、绕过 QR、伪造索引、重装包、格式化或清 owner。
当前待机资源可渲染；认领窗口内仍显示 QR 是实际权限场景限制，不是等待手机修复资源。
证据到 LCD 提交层，没有真人视觉确认。

## 原生 CDC：协议、背压与授权分别记录

一次核对 Windows 映射及占用：COM9 为 CH340 1a86:7523，COM16 为原生 CDC
1209:0001 MI00；后续只使用这次实际映射，不抢占其他工具。
历史 710 失败入口是 `deploy --package ... --ota-port COM16`，发送原始 OTA HELLO，
没有进入 TLS/PC 身份验证。标准 CP/AP 均未启用 `CONFIG_BK7258_OTA_SOURCE_USB`，
因此不能把该调用视为支持的标准产品 OTA 或据此断言 USB 驱动回归。
工具现在说明所需运行模式，并将写失败标出 phase、请求字节及已知/未知完成量；不重放、不增缓冲、不打印载荷。

713 的一次预认证实验仅发送 TLS ClientHello，无凭据、无 SDC1 业务命令：

| 观测点 | 真实结果 |
|---|---|
| 写前设备快照 | configured=1、opened=0、RX pending=1；RX/TX 字节与积压均 0 |
| 主机请求 | 一次 517 B 写，5019 ms 后 transport/client_hello 超时；没有成功返回的写字节，实际部分接收量由设备另证 |
| 写后设备快照 | RX=256 B、RX queued=256 B、RX pending=0、opened=0；TX=0，rx_error=0 |
| 清理/再连接 | 原句柄关闭；下一次打开并关闭耗时 7 ms，无发送、无隐式复位或取得 owner |
| 一次整机保持性重启后 | configured=1、RX pending=1、积压 0、健康故障/恢复计数 0 |

生产 `bkpc_usb_owner_step()` 在合法设备绑定与 PC grant 不成立时不打开串口读取；
256 B 下层队列被接满后按既有流控停止接收。现场观测与该准入路径一致，超时发生在
服务读取/TLS 之前。已证明部分 Bulk OUT 到达；没有证明 IN、TLS、认证后任务/资源/相机纵向成功。
合法 PC profile 未提供，正式定向检查未取得可用身份；保持 fail-closed，不扩大 diagnostics，
不复制手机 Keystore，不再搜索历史凭据。没有以 UART 代替原生 USB 业务。

## 触觉准入

40 ms 产品脉冲、100 ms 上限、冷却、合并、取消与停止失败保持均复用，不改电气参数。

| 真实场景 | 当前准入与反馈 |
|---|---|
| 本地 KWS 监听，哪怕语音回合 IDLE | 真正采集仍置 motor capture-quiet，电机拒绝；视觉按显示优先级准入 |
| 正在识别、播放 | 产品 voice-idle 门不满足，停止/抑制自主触觉与动作 |
| 内容/专注/电脑任务完成 | 先消费事件、尝试有限显示；仅健康电量、显示准入、真实收音释放和冷却都满足才可 40 ms 脉冲；常驻 KWS 通常抑制，过期不补播 |
| OTA、电源关键阶段、低电量、退出/故障 | 既有准入拒绝或停止，未知/失效电量不能授权脉冲 |

实板 713 KWS 正常采集时 `bkhaptic pulse 40` 返回 -EBUSY；stop accepted，active=0/fault=0。
这是受限证据，不是正向振动/体感通过。现有 Agent 显式振动工具可能在合法麦克风释放窗口工作，
但该路径本轮没有可验证的离线用户入口，不能据此称完成提示触觉已可用。
本轮不随意停麦漏唤醒，也不伪造 idle。若产品要让常驻 KWS 下的完成提示振动，需要另定监听暂停取舍并验证漏唤醒及恢复；当前视觉降级保留。

## 有限资源窗口与交叉回归

窗口为标准监听/静置、资源恢复、一次预认证 OUT、单帧采集、受保护触觉请求及一次重启。
复用堆/线程/栈统计，新增命令只在显式查询时运行，无新增常驻线程或计时器。

| 指标 | 本轮真实观测（B，除非另注） |
|---|---|
| AP NuttX 系统堆 | total 4769560；监听 used 1545288/free 3224272；窗口 peak 最大 1604992，按同一堆总量推得最小余量 3164568；largest-free 3157768–3213424 |
| 单帧后 | used 1548592；随后 1548704；这是包含短时反馈线程的快照，不称整个相机峰值或泄漏；重启后稳定 used 1545328 |
| AP 栈 | 35 个线程时分配 379928、已触达合计约 48888；单帧后短时 36 个/382936/最高观测 51196；最小单栈余量 288（PID 5） |
| 栈扫描时间 | 3188–12844 us；含抢占的观测耗时，不是全局锁硬上界；CP 未启用栈染色，不把分配栈尺寸当高水位 |
| AP 私有 SDK PSRAM 堆 | total 655360，free/minimum_free 均 125912；不含所有媒体 slab，不与系统堆简单相加 |
| CP 系统堆 | total 150392，used 93064，free 57328，maxused 100144；独立 SDK SRAM total 16384/free 15800；CP PSRAM total 131448/free 32744 |
| CDC 队列 | 真实观测 RX 最大 256；TX 0；仅前述未授权预认证窗口 |
| 采样/渲染 | 持续 KWS 约每 10 s 报告 500 输入帧；motion 真实 cache 时间有效；受 QR 抢占的资源提交 uptime 12.333/141.144 s，非动画帧率成绩 |
| SD 写入、细分驱动耗时 | 此配置没有覆盖全链路的累计写入/耗时计数，未伪报 0；资源路径只读，单帧明确 storage=disabled；额外扫描/写盘未用于观测 |

相机真实单帧 640×480 JPEG，13219 B，SOI/EOI 正常，无 V4L2 错误，不保存原图。
102400 B 仅 PC 保留帧的容量：真实启动还报告 SDK encode slab 的两块 102400 B 帧
（各有对齐开销）及 20480 B SRAM 行缓存；V4L2 MMAP 队列、PC 拷贝、TLS/分块及主机
JPEG/base64/解码显示缓存各自属于原有所有者。此次 console 单帧未经过授权 PC/TLS 链路，
上述未同时激活部分没有实板联合峰值，不能用 102400 B 宣称总内存上限。

合法本地内容、授权 USB 业务及正向触觉的资源窗口分别缺 owner/profile/真实收音释放条件；
功耗和真人体感需仪器或人在场。没有因此挂起已可测的堆/栈/队列。
一次自然 KWS 候选在网络未就绪时返回 -100，原始 Media 退出警告保留；随后生产 rearm=0、
持续采样，未观察到崩溃或 Supervisor 恢复。没有主动触发云请求或重开封存声学专项。

测试先固定反例：相机 kind 21 碰撞、健康 resources 请求旧实现 -EINVAL、OTA 写错误无法分层；
修改后原断言通过。production camera、health、display snapshot、USB RX/TX/lifecycle、
真实 Agent 本地专注/首段流、内容取消/关闭、TLS/权限/传输和产品退出等受影响回归通过。
组合树 80455f84 完整门禁 946 PASS，0 FAIL_ASSERTION/SETUP_ERROR/NOT_RUN，输入未变；
后续主机工具改变另外复测写错误与真实 workbench client。编译失败/环境锁文件失败记录为 setup，未当业务 Red。

## 部署、交付与剩余条件

713 采用同板既有信任签名，verify build-manifest/trust/package 通过。只写当时非活动 B 槽：
CP `0x00352000+0x00132000`，AP `0x00484000+0x0020f000`；Loader 全成功标记齐全。
烧板后及一次受控重启后均 confirmed `0.7.67+713`，Supervisor faults/recoveries=0/0。
CP/AP 工程自检、临时 factory diagnostics 均关闭；AP 只增加标准栈染色。未清 owner/SD/资源，
KWS 模型、前端、判据、“我在”和键位未改。认领覆盖层按原优先级保留。

- AP config SHA256：`f6055978c97664c3ba5429976618c90b8b0a8892e74729cd455398cf3ab7c5e4`。
- CP config SHA256：`dd503376e1b5436c9221274dc5a9ff86bf2e49660dfad4d306292e1113d82bed`。
- 713 bkpack SHA256：`e046dd2d1cccc9256747d021e4d3e36313292e94eb7ef6f68e6edea05182e748`，私密同板镜像不公开。
- 签名信任指纹 MCUboot `7163228f3221da6e81fcf5e9c6f13ae6c4afbd1e3b91b7669783a37d2f12fbc0`，BL1 `93709b494c3ba758ee144bd9ed5d12ca97726a13dc88e336d4c19e9329c44364`，没有更换信任根。
- 组合 Android debug APK SHA256：`ead027986d9a451191b59c8a0025c92c9dc97300bc1c8a1709324a057000c9ac`；assembleDebug/testDebugUnitTest 通过，未安装到真实手机。

相机编号修复独立候选 `260fb85a`，依赖 PR132；只读观测与 OTA 错误分类分支
`fix/whole-device-observation` 依赖 PR129，不依赖 PR128。两者的自动 CI 按各自准确候选记录；
本文件的 713 HIL 只归属组合树，不冒充独立 PR 候选已烧板。无自动合并。
剩余只有对应层的合法 owner/PC profile、手机流程、实体动作/卡片及真人视觉/体感、仪器和有效云条件；
USB Audio、标准 UVC/HID、家庭事件、更多小游戏保留后续路线，本轮未展开。

原始证据保留于本地 `out/shaniu-integration-20261010/`，不公开敏感日志：
`state-713/serial.raw` SHA256 `64440ec059662ab18b981d564f542983991a540f1ed1c00bb3ff5652de39fa1e`；
`usb-after-713/serial.raw` `d3f03301f47b174d4f13a710f1df0e4614149bf9b17de759e868af088c9052ca`；
`camera-haptic-713/serial.raw` `9fb73c5726f9abb8a0afb025e1379fe58522b094926cfee8cada3be24519e17f`；
完整门禁 `gate-standard/results.json` `d7120db8810b82ee54460e61ff62f4c0f7ed8e3c0454e4a3e7f094baf49199f9`。
