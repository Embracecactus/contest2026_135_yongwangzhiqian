# 2026-10-19 资源工作台接板检查

用户接入开发板及原绑定手机后，继续已评审的资源安装/限时试用计划。
本轮执行至真实授权前置条件：初始电脑 USB 链路正常，手机管理连接未建立；
后续自主操作复现 MTU 握手超时，并回读到板端 PRIMARY_TIMEOUT / RPTUN
FAULTED，随后 PC TLS 握手也失败。未能授予 resources/scenes，未进入资源写入。
软件准备与测试包见
[上一轮记录](2026-10-19-workbench-resource-continuation.md)。

## 首轮实测基线

- Windows 枚举 CH340 UART0 为 COM9，原生 USB CDC 为 COM16；两个 PnP
  设备状态均为 OK。使用现有 Windows CurrentUser DPAPI profile，经正式
  PC/TLS 客户端鉴权读取固件 `0.7.124+770`，security counter 770。
- PC `status`：ready=true、busy=false、runtime_error=0、volume=100。
  `camera-status`：admitted=true、valid=false、无待取帧。
- `catalog-status` 与 `trial-status` 均由设备拒绝，错误 `-13`。结合当前
  权限路由，这是 resources/scenes 操作未获授权；不能把 USB 鉴权成功
  当作全部能力均已授权。历史正常 profile 授权范围为 tasks/camera。
- UART 的 `bkdisplay status` 回读 service=ready、last_error=0、双屏、
  expression=neutral、pack=shaniu-cyan-v3、revision=3；`bkmotion status`
  返回实际周期采样。`bkhealth status` 回读充电状态。沿用历史诊断命令
  `bkap status` 在当前映像返回 command not found，不计为有效 AP 状态证据。
- 手机 ADB 为已授权 Mi 10；已安装 App versionCode 56、
  versionName `0.7.26-shaniu-ota-retry`。未安装 APK 或清除数据。

## 首轮手机连接结果与停止点

通过现有 App 扫描到一台候选设备，选中后由正常流程连接，沿用原认领资料。
没有扫码重新认领。一次手动连接意图触发 App 内置重试，未额外手动重复连接。

2026-10-19 21:00:02（手机本地时间）首次 BLE 连接建立，服务发现成功；
MTU 请求后于 21:00:17 返回 status=133，随后断开 status=19，传输关闭原因
为 `descriptor_write_start_failed`，阶段 subscribing、耗时 15706 ms。
内置重试又出现 discovering 阶段断开及 connecting 阶段约 30 秒后 status=255。
App 始终没有报告已验证管理连接，未到电脑授权确认/导出步骤。

按本阶段停止条件保留第一失败及后续内置重试证据，不展开 BLE/升级重连修复。
当时连接页没有可用的“断开手机连接”入口，使用正常 HOME 操作将 App 退至后台；
后台生命周期取消待执行重试。超过 35 秒后复核，前台为系统桌面，过滤日志
未出现新的 GATT connect；未 force-stop、删除本机配对记录或更改手机蓝牙开关。

首轮结束时再次通过原生 USB 鉴权读取 ready=true、busy=false、runtime_error=0；
UART 的显示与动作回读仍通过，显示包仍为 shaniu-cyan-v3/revision 3。
这些结果不构成长稳验收，也不证明手机连接已恢复。

## 已保留的状态与剩余工作

没有生成新配对请求、替换电脑授权、上传/删除资源、修改默认选择、烧录或 OTA。
独立测试包 SHA-256 仍为
`df0a7af8672a71c815b21c68a3bc03826efa9a32e485f8d326504cf2ce91a48b`。

当前阻塞包括手机管理连接及后续复现的板端主核心跳故障，随后才是
resources/scenes 授权。恢复并重新核对板端健康状态后，按正常 App
流程请求 resources/scenes/tasks/camera，核对摘要、持久回执与授权回读，导入到
新的受保护 profile 并验证 PC 鉴权，再从目录冲突检查继续。原 profile 保留。
资源安装/目录回读、限时试用及恢复、真实中断/取消、任务/相机组合窗口均未验收。

证据位于工作区 `out/shaniu-resources-hil-20261019/`：`preflight.json`、
`device-info.*`、`status.*`、`catalog-status.*`、`trial-status.*`、
`camera-status.*`、`readbacks.json`、`board-baseline/`、手机版本及 UI XML、
`phone-filtered-gatt.log`、`phone-connect-uart/`、`phone-background.json`、
`phone-final-filtered-gatt.log`、`phone-final-foreground.json`、`final-board/`、
`final-status.*`。原始串口字节均保留；初始无断言的采集只报告
completed_unverified，最终显示/动作正则核对的 session.json 报告 passed。

## 后续自主操作与故障采集

用户先反馈“找不到设备”，随后明确要求自主操作收集。本次通过现有 ADB 操作
手机正常 App 流程；每次点击依据新取的 UI 层级，未清除 App 数据或更换身份。
此前协作窗口执行过一次普通 NSH `reboot`，日志在 `cooperate/reboot/`；重启后
显示仍为原包，Host 广播启动返回成功。该返回本身不等于射频发现成功。
以下自动操作窗口没有再次重启或烧录，保留故障现场。

手机日志使用北京时间，UART 时间使用 UTC。下表均换算为北京时间，精确顺序
只在各自日志内成立；未独立测量两端时钟偏差。

| 时间 | 实际证据 |
| --- | --- |
| 21:34:34 起 | 扫描页发现一台 Shaniu 候选；点击候选触发一次连接意图，之后为 App 内置重试。 |
| 21:34:52 | 手机 GATT 连接及服务发现均返回 status=0，随后请求 MTU=185。板端记录 LE_CONN_COMPLETE status=0，Host `stop_end result=0`、host_adv=0、latched=0。 |
| 21:35:22 | 手机 `handshake_timeout stage=mtu elapsed_ms=30027`。没有建立已验证管理连接，也未进入电脑授权操作。 |
| 21:36:11–19 | UART `apctl status` 返回 PRIMARY_TIMEOUT 与 RPTUN FAULTED/error=110；紧接着的 `bkdisplay status` 在此次采集窗口内没有返回结果。 |
| 21:36:51 后 | 正常 HOME 操作将 App 放到后台；已发出的连接尝试于 21:36:59 结束。等待超过 35 秒后确认前台为系统桌面，过滤日志未出现新的 GATT connect。 |
| 21:40:51 起 | 使用原受保护 profile 执行一次 5 秒预算的 PC `workbench status`：COM16 打开成功，TLS 握手失败，exit=1，耗时 6.391 秒；客户端报告未重放命令。没有新的设备状态回读。 |

板端故障回读的关键值为：

- AP 引导状态仍为 READY/generation=1，但 supervisor 为 FAULTED，
  reason=PRIMARY_TIMEOUT，last_error=-110，faults/recoveries=1/0。
- RPTUN 为 FAULTED/error=110；supervisor primary/secondary age=2600/0 ms。
- CPU2 为 SCHEDULER_ONLINE/error=0、heartbeat=7230；CP 控制台仍能返回
  `apctl status`。这些证据不足以宣称整片死机。

源码核对边界：`bk7258_ap_supervisor.c` 的故障路径会将 RPTUN 标为故障；
同一 generation 的 FAULTED 状态保持锁存，后续不继续刷新上述 age。因此
2600 ms 是故障判定时的快照，不能当作查询当下的持续卡住时长。连续 UART
窗口结束于 21:35:08，之后才查询故障状态，尚不能给出主核心跳停止的精确时刻，
也不能证明 MTU 超时与主核故障之间的因果方向。

本次 Host 停广播返回成功，不能沿用此前“停广播命令超时”的原因解释本次故障。
历史 770 映像已出现过 PRIMARY_TIMEOUT 与后续 PC TLS 超时，见
[10 月 18 日记录](2026-10-18-stability-and-phone-hil.md)；本轮新增的是重新发现、
连接、成功停止广播、MTU 超时以及后续板端/PC 失败的实际证据链。
下一定位范围是 AP 主核心跳停滞与 ATT/MTU 处理的关系，仍未判定根因或验证修复。

本窗口没有修改产品源码、手机数据、绑定身份、授权、默认资源或固件。
资源安装、试用和组合验收继续保持未执行。原始证据新增于
`out/shaniu-resources-hil-20261019/automated/`：扫描及连接 UI XML、
`final-phone-gatt.log`、`connect-uart/`、`after-failure/`、后台操作与前台确认、
`final-pc-status.*`。两次 UART session 均为无目标断言的采集，
completed_unverified 只表示采集完成，不代表设备通过验收。`result.json`
记录观察结论、证据及本报告 SHA-256，`SHA256SUMS` 校验本窗口所有证据文件。
