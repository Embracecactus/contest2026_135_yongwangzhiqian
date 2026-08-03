# BK7258 Stage N13：BLE GAP/GATT Peripheral 端到端计划

> 日期：2026-08-03
> 状态：**COMPLETED / `board-verified`（2026-08-03）**
> 最新完整实板基线：N13 `cp_nsh_ble_gatt + ap_smp_ble_gatt`；四类negative、20轮重连及主动共存均闭环
> 源码复核：[N13 BLE GAP/GATT source verification](../n13-ble-gap-gatt-source-verification.md)
> 证据索引：[N13 evidence index](../n13-evidence-index.md)
> 硬边界：官方 NuttX 与 Beken SDK 源码只读；N13 只允许 board/app/tool wrapper、配置与文档改动

## 1. 已批准需求基线

### 1.1 问题与目标

N12 已证明 BK7258 的 official Controller IPC、NuttX Host初始化、稳定 BD_ADDR 和真实 BLE
scan report，但尚未证明本板作为 BLE Peripheral 被 Windows连接并完成 ACL/L2CAP/ATT/GATT
数据交换。

N13 的最小端到端目标是：Windows作为 Central/GATT client发现并连接 BK7258，读取 service，
以 write-with-response发送带 sequence/CRC 的固定 frame，BK7258通过 read和 notify返回可核对
结果；同时 AP SMP、RPTUN/RPMsg、RPMsgFS、supervisor与 CP NSH保持健康。

### 1.2 参与者与主场景

- BK7258 CP：official Beken BLE Controller、PHY/RF、Bluetooth mailbox IPC owner。
- BK7258 AP：stock NuttX BLE Host/GAP/GATT 与唯一 board-owned N13 service owner。
- Windows：Central/GATT client；只使用仓内无 GUI WinRT helper自动化。按用户要求禁止启动
  `C:\Users\lijian\Downloads\BLEDebug\BLEDebug.EXE`，因为它会导致 Windows明显卡顿。
- CP NSH：沿用 `bkbttest`，其 `stats` 输出包含 HCI/ATT/GATT lifecycle与flow-control计数，
  是机器可解析结果的权威 UART入口。

主场景：

1. cold boot 后 Windows发现 `BK7258-N13`；
2. connect并发现固定128-bit service/characteristics；
3. read status、write 20-byte valid frame、read back同一 sequence/value；
4. enable CCC，发送 bounded notify burst，Windows逐帧校验 sequence/CRC；
5. disconnect后设备自动恢复 advertising，可重复连接；
6. BLE traffic 前后运行既有 RPMsg/RPMsgFS/SMP健康回归。

### 1.3 成功标准

- stock probe先证明 Device Name discovery/read，首次闭环双向 ACL/ATT。
- custom advertising/name/service UUID从 Windows可见，地址等于 N12持久化 BD_ADDR。
- 20 次 connect → discover/read → disconnect 均成功，每次断开后重新可发现。
- valid write得到 ATT success；invalid length/magic/version/CRC被拒绝且连接保持可用。
- 单次 echo和固定100帧 notify测试 sequence连续、CRC全对、duplicate/lost=0。
- BLE测试期间所有等待都有 deadline，无 callback阻塞、无 heap无界增长。
- N13-E/V 的 RPMsg 6场景、RPMsgFS四档、AP READY/CPU2 online/supervisor健康回归通过。
- NuttX/SDK tree零永久修改；N12 profiles仍可独立构建回退。

以上次数是首版工程验收 gate，不是产品吞吐或可用性 SLA。

### 1.4 明确不做

- indication；
- write without response、long/prepare write、MTU>23 payload；
- 多 Central并发；
- pairing、bonding、MITM、encrypted/authenticated attribute；
- BLE Mesh、GATT client、Wi-Fi/BLE coexistence；
- Bluetooth profile的 AP-only warm restart；
- 修改或替换 stock NuttX Bluetooth Host；
- 链接 Beken BLE Host/GATT archive；
- QEMU 模拟 BLE radio。

### 1.5 Approval

- Status：Approved
- Approved by：用户
- Date：2026-08-02
- 解释：用户已确认以 N13 BLE GAP/GATT为下一 MAIN Stage，并批准先完成计划与源码验证。

## 2. 架构驱动

| 排名 | 驱动 | 可验证场景 |
|---:|---|---|
| 1 | 不改官方源码、单一 Host owner | final diff不含 NuttX/SDK；AP ELF不链接 Beken Host |
| 2 | 先证明 N12 未覆盖的 ACL/ATT | 现有镜像完成 stock GAP read后才写 custom service |
| 3 | callback不阻塞且 SMP-safe | ATT callback只 validate/copy/post，worker固定 logical CPU0 |
| 4 | 可恢复的 Peripheral lifecycle | 20次断连后均自动重新 advertising |
| 5 | 端到端可判定，不以本地 send当成功 | Windows sequence/CRC/count与 CP counters一致 |
| 6 | 不破坏 N8..N12基线 | BLE前后运行固定 RPMsg/RPMsgFS/health回归 |
| 7 | 可回退、可复现 | 独立 config/Kconfig、Windows helper、UART机器可解析行 |

## 3. 冻结架构

### 3.1 owner 与数据路径

```text
Windows no-GUI WinRT GATT client
        ↕ air
Beken BLE Controller on CP
        ↕ official BT pointer IPC (HCI event + ACL)
board/bk7258_bt_hci.c on AP
        ↕ stock bt_driver_s
NuttX HCI → L2CAP → ATT → GATT
        ↕
static combined GAP+N13 attribute table
        ↕ validate/copy/post
CPU0-pinned bk-ble-gatt worker
```

CP 不运行 GATT server；RPMsg test endpoint只传控制/结果，不承载 HCI或attribute payload。

### 3.2 initialization

```text
N12 HCI/controller init
→ stock bt_netdev_register auto GAP + ADV_IND
→ N13 worker/init structures ready
→ stop stock advertising
→ register one static combined GAP + N13 table
→ start BK7258-N13 advertising
→ publish N13 READY
→ publish AP READY
```

`bt_gatt_register()` 返回 void，故 correctness由 source/static verifier、固定 table lifetime和
Windows discovery共同证明。任何可返回 error 的 stop/start/worker/init步骤失败都进入
`FAULTED`，不得只打印 warning后继续 READY。

### 3.3 callback 与 worker policy

- SDK HCI callback：只做现有校验/copy；在 `bt_netdev_receive()` 后被动记录 connection event。
- NuttX LPWORK中的 GATT read：从锁保护的20-byte snapshot同步复制 response。
- GATT write：只接受 offset0、len20、正确 magic/version/CRC；copy到固定 ring并 post。
- CCC callback：只 atomic更新 subscribed flag/counter。
- `bk7258-ble-gatt`：SCHED_FIFO、affinity `0x1`、priority 96、固定 queue depth；处理
  state transition、echo/status、notify burst、重新 advertising。
- callback/worker均不得 dynamic allocate per packet；queue full fail-closed，不覆盖旧 request。

固定执行优先级为 `HCI TX 120 > SDK bt_ipc_thd 98 > NuttX LPWORK 97 > N13 worker 96`。
这既保证 SDK-owned HCI pointer及时归还，也保证 stock connection/disconnect cleanup先于 board
状态同步与唯一一次 restart；
源码 static assert与构建 verifier共同检查该链。

### 3.4 service ABI

UUID、handle和20-byte frame以 source-verification文档为准。首版 opcodes：

| opcode | request | response |
|---:|---|---|
| `0x01 ECHO` | `sequence/value` | status characteristic返回相同 sequence/value，opcode置 response bit |
| `0x02 BURST` | `count=1..100`、sequence seed | 以固定安全间隔发 count帧 notify，每帧独立 sequence/CRC |
| `0x03 RESERVED` | 当前实现拒绝 | 不提供远端 counter reset，不改 HCI/RPTUN generation |

`RESET_COUNTERS` 仅保留了 ABI枚举值，当前 write validator不接受该 opcode；若以后启用，必须
单独评审未连接/pending-work条件。它不是 BLE Controller reset。
所有多字节字段使用 little-endian；CRC使用 CRC-32/ISO-HDLC，并只覆盖 wire bytes 0..15。

### 3.5 GAP ownership与scan互斥

N13 service active时不并发运行 N12 `bkbttest scan/n12v`。验证 N12 RF scan时：

```text
切回 cp_nsh_btipc + ap_smp_btipc
→ cold reset
→ bkbttest n12v ...
→ 验证结束后再烧回成对 N13 profile
```

当前没有单独的 `bkbletest adv`命令。`bkbttest info/stats` 可在未连接时顺序执行；首版不
宣称 Controller同时 scan+advertise 的能力。

## 4. 配置与文件边界

保留 N12 profile不变，新增 opt-in pair：

```text
board/bk7258_t5ai/configs/cp_nsh_ble_gatt/defconfig
board/bk7258_t5ai/configs/ap_smp_ble_gatt/defconfig
```

计划新增/修改的 team-owned文件：

```text
board/bk7258_t5ai/
├── chip/include/bk7258_ble_gatt.h
├── chip/ap/bk7258_ble_gatt.c
├── chip/ap/bk7258_bt_hci.c             # observer, flow compatibility, diagnostics
├── chip/common/bk7258_bt_test.c         # N13 stats transport extension
├── chip/Kconfig / Make.defs / CMakeLists.txt
└── configs/{cp_nsh_ble_gatt,ap_smp_ble_gatt}/defconfig

app/hello_app/
└── bk7258_bt_test_main.c               # existing CP NSH built-in: bkbttest

board/bk7258_t5ai/scripts/
└── verify_bk7258_ble_gatt.py           # config/ELF/table/owner gates

tools/windows-hardware-debug/
└── ble-gatt-client/                    # native Windows WinRT helper, WSL2 orchestrates
```

已生成配置的关键约束：

- `CONFIG_BK7258_BLE_GATT`
- `CONFIG_BK7258_BLE_GATT_PRIORITY=96`
- `CONFIG_BK7258_BLE_GATT_STACKSIZE=8192`
- `CONFIG_SCHED_LPWORKPRIORITY=97`
- `CONFIG_BLUETOOTH_TXCMD_PRIORITY=120`
- `CONFIG_BLUETOOTH_CNTRL_HOST_FLOW_DISABLE=y`
- N13 CP profile `CONFIG_USEC_PER_TICK=1000`（official SDK等价1 ms tick）
- `CONFIG_BK7258_BLE_GATT_NOTIFY_INTERVAL_MS=50`
- N13 AP profile `CONFIG_BLUETOOTH_MAX_CONN=1`
- `CONFIG_DEVICE_NAME="BK7258 N13"`
- `CONFIG_DEVICE_LOCAL_NAME="BK7258-N13"`

`build_dual_image.sh` 只增加严格成对 profile allowlist；SDK选择继续是 v3.1.1.9，legacy
profile不自动获得 N13 service。

## 5. 分阶段计划

| 子阶段 | 工作 | 退出条件 |
|---|---|---|
| **N13-R** | 当前 checkout、GATT API、callback context、global DB、CCC/notify、ACL wrapper、official sample与工具复核 | **完成：`source-verified`**；本文和 source-verification已更新 |
| **N13-A** | stock GAP probe；随后新增 N13 profiles、custom name/UUID advertising与静态 verifier | **完成：board-observed**；持久化地址、stock `NuttX` GAP read、custom `BK7258-N13`与service discovery已闭环 |
| **N13-B** | connection lifecycle observer、state machine、disconnect后重新 advertising、CP `bkbttest stats` | **完成**：20/20 uncached重连；结束后Host/HCI/N13=`22/22/22`、state/error/queue=`2/0/0`、连接引用为0 |
| **N13-C** | combined GAP+custom DB；20-byte control/status read与write-with-response；错误输入门禁 | **完成**：length/magic/version/CRC `4/4`真实ATT拒绝，post-reject合法echo/notify/rediscovery PASS |
| **N13-D** | CCC与status notify；single echo notify和bounded 100-frame burst；Windows client记录 | **完成**：50 ms pacing下多次100/100、CRC/lost/duplicate=0、退订quiet、link usable |
| **N13-E** | CPU0/CPU1与RPMsg/RPMsgFS并发回归、重复连接与资源基线 | **完成**：BLE 100帧分别与RPMsg六场景×100、RPMsgFS四档×20并发PASS；heap及post health稳定 |
| **N13-V** | cold-reset重复、focused review、latest/legacy/N12 build回退、SOP与证据归档 | **完成**：3/3 physical cold、构建回退、final rebuild/flash、verifier、官方树零diff、最终证据索引均PASS；N13升级为`board-verified` |

正式关闭顺序仍为 `R → A → B → C → D → E → V`。C/D 的 bounded smoke用于诊断 ACL与
readvertise问题，不会越级把对应 gate标成完成；失败保留最小日志和 artifact hash。

### 5.1 当前实证、timing与 lifecycle 修复

- stock GAP probe：地址 `c8:47:8c:47:47:48`、advertised/device name `NuttX`；
- custom smoke：Device Name `BK7258 N13`、固定 N13 handles、20-byte echo、3/3 CRC notify、
  unsubscribe quiet与link usable均通过，旧镜像仅在post-close rediscovery失败；
- official SDK archive依赖真实`bk_delay_us()`，但旧board wrapper静默丢弃delay；最终wrapper在
  team-owned文件中调用NuttX `up_udelay()`。N13 CP profile固定official FreeRTOS等价的1 ms tick；
  不修改或重编official SDK/NuttX；
- 失败时板端仍有 ACL RX/TX `30/34`、receive/PDU error 0，证明故障不在GATT数据面；
- Controller为每个冗余 HCI `0x0c35`返回 `status=0x07,ncmd=0`。N13关闭Host flow control，
  board lower-half只过滤该 opcode；修复后 command complete保持健康且drop计数可观测；
- Controller在连接时自动停止legacy advertising，但NuttX保留陈旧`adv_enable=1`；旧路径由此
  形成stock auto-enable→board disable→board enable竞态。新路径在connect worker调用一次
  `bt_stop_advertising()`同步Host flag，disconnect时stock不再auto-enable，board worker按official
  SDK sample只执行一次full start。最终镜像已构建、静态验证、烧录并`PASS_NSH`，冷态scan PASS；
- 20 ms notification pacing在100-frame压力下出现静默缺帧；source verification确认当前
  NuttX `bt_gatt_notify()`返回void，ATT PDU分配失败时调用者得不到错误。AP profile现固定50 ms，
  端到端只认可Windows exact count/sequence/CRC；
- 最终镜像build时RPTUN/BLE verifier均PASS，稀疏烧录三段显式PASS；独立RTS物理冷复位在
  `$WORKSPACE/logs/bk7258-n13/n13-pacing50-rts/20260803-031501/`取得`PASS_NSH`；
- earlier `pnputil`要求的Windows系统重启已在本轮前完成；当前不是“等待Windows重启”状态；
- 完整无GUI CLI一次性通过GAP name/service、echo、100/100 notify、zero CRC/lost/duplicate、
  unsubscribe quiet、link usable与post-close rediscovery。结果JSON SHA-256为
  `5896a0611f6cd6da131095b82e2e29925a8199390f41218841b5eb9799d47ed5`；
- 后续Windows queued discovery有时只产生Controller连接而无ACL/ATT。板端仍正常以reason
  `0x13`断开并重新广播，最终`connected/disconnected/readvertised=6/6/6`、state=ADVERTISING、
  error/queue_full=0；stats证据SHA-256为
  `94db90f0209095870eb5ec3a70609a01ab7ca6e53ac1e94f5951297b3bac7247`；
- CLI以uncached service discovery作为RF/ATT proof，只对明确`Unreachable`做有限fresh-object
  retry；discovery timeout不叠加重试。该历史时点正式20轮尚需清洁Windows host状态；重启后的
  final 20/20已闭环，见本节末尾。
- CLI现有opt-in `--n13-negative`，逐个要求bad length/magic/version/CRC得到WinRT
  `ProtocolError`或exact Windows SDK ATT HRESULT，再以合法echo证明link usable；Windows
  `/W4` build/probe已PASS。一次single attempt真机运行在
  service discovery阶段timeout/exit8，未发送坏帧、未生成result；J-Link证明四类bad counter
  仍全0，当时N13-C实板gate仍被host阻断。
- 完整100-frame PASS来自address-first对象；最终CLI因此以广播地址为首选、AEP ID为第二候选。
  candidate session在discovery前也由RAII管理，timeout会明确清`MaintainConnection`并close。
  该最终候选随后只执行一次address-first真机请求：Controller实际连接/断开各一次，但Windows仍在
  uncached service discovery 30秒超时，未发ATT、未生成JSON且未重试；pre/post UART表明
  lifecycle从`5/5/5`推进到`6/6/6`，last_error/queue_full仍为0。证据位于
  `$WORKSPACE/logs/bk7258-n13/n13-negative-final/`。
- CLI现新增默认关闭且只允许搭配negative gate的`--n13-cached-discovery`：缓存只用于已冻结
  service/characteristic/CCC索引，uncached GAP/status read、真实ATT拒绝、合法echo/notify、退订、
  重发现及JSON cache marker仍全部必需。第一次single attempt已完成live uncached read，并由ATT
  trace/J-Link证明19-byte write得到ATT `0x0d`、仅bad_length `0→1`；WinRT把它投影为HRESULT
  `0x8065000d`，旧classifier因此正确fail-closed但误判结果。现工具同时支持status projection和
  exact SDK HRESULT（其他当前NuttX语义拒绝为`0x8065000e`），修正版`/W4` EXE/probe PASS，
  SHA-256=`ceafb0457983de8760c0410148ad1ff18e3753e77d44b86f38fd21ef465a70a3`。
- 修正版只再执行一次，但Windows在cached enumeration前timeout，未发ATT/未生成JSON/未重试；
  latest cold后的board lifecycle仍健康推进到`2/2/2`且error/queue为0，bad counters不变。证据
  `$WORKSPACE/logs/bk7258-n13/n13-negative-cached/summary.md`。length负测已board-observed，
  其余三类与valid echo仍等待clean host；cached mode不作为20轮fresh discovery证据。
- 恢复会话中adapter probe和scan正常，但device-wide cached请求仍在ATT前timeout。host CLI进一步
  增加exact device+UUID cached instance/legacy single-UUID cache，以及独立的真正uncached
  `--n13-targeted-discovery`。当前Windows session分别得到instance count 0、`0x80070490`和
  GAP targeted query timeout；最终board lifecycle健康到`4/4/4`、state/error/queue=`2/0/0`，
  ATT trace仍为sequence37且无JSON。最终`/W4` EXE/probe SHA-256=
  `a7a977dc0ed3d4a81c386656c51b58cc0229aee5c2ab2fb2ce8aec86667af76b`；证据见
  `$WORKSPACE/logs/bk7258-n13/n13-negative-resume/summary.md`。targeted timeout证明当前阻塞不只
  是“枚举全部service”，但也不是firmware ATT拒绝或崩溃证据。
- 同一N13镜像的advertising-idle共存smoke已通过：pre/post `apctl status`均为AP READY、RPTUN
  CONNECTED、supervisor HEALTHY、CPU2 SCHEDULER_ONLINE、pending 0/0且SMP gates不退化；
  `bkrpmsgtest all 5 30000`六场景全部CPU0/CPU1 5/5、error0、AP heap逐轮完全一致；
  `bkrpmsgfstest all 1 30000`四档payload及checksum全PASS，AP heap稳定，CP只在首次service建立
  固定增长224 bytes后稳定；最终BLE仍ADVERTISING/error0/queue0、lifecycle=`5/5/5`。该smoke
  不替代active BLE traffic并发与正式count门禁。
- N13-V构建回退已部分完成：未改profile的N12分别用`v3.1.1.9`与`legacy`完整构建，两个SDK
  role manifest checksum、RPTUN ELF layout和打包一致性均PASS；随后恢复N13 `v3.1.1.9`构建，
  RPTUN/BLE verifier均PASS，AP CRC/padded hash精确复现。CP镜像包含NuttX自动生成的构建时间
  字符串，故重建hash会变化；证据保存在
  `$WORKSPACE/logs/bk7258-n13/n13-v-build-regression/20260803/`。official NuttX/apps仍无tracked diff。
- physical RTS cold startup现累计3/3 `PASS_NSH`且`cold_path=yes`：原
  `n13-pacing50-rts/20260803-031501`加
  `n13-v-cold-repeat/round2/20260803-040716`与`round3/20260803-040755`。第三轮后的独立probe
  显示BLE全新Advertising/lifecycle `0/0/0`/HCI error0，AP READY、RPTUN CONNECTED、supervisor
  HEALTHY、CPU2 SCHEDULER_ONLINE且SMP gates全PASS。一次首命令`apctl`注入异常经随后
  `help + apctl status`复跑排除，不作为固件失败。
- N13-V focused review已逐项关闭HCI receive顺序、callback/queue/lock边界、single-connection
  lifecycle、ATT errno映射、AP-only链接隔离与Windows timeout cleanup；没有发现需要改变已验证
  镜像的blocker。审查修正了board Kconfig中worker默认优先级`99→96`，使其与显式N13 profile、
  文档和`120 > 98 > 97 > 96`源码约束一致；当前profile原本就是96，固件行为不变。首次初始化
  失败后的原地retry继续明确不支持，AP会fail-closed并park。
- 最终阻塞根因不是Windows缓存：stock `hci_acl()`每个入站ACL通过`bt_conn_lookup_handle()`
  增加引用，但当前`hci_acl()`/`bt_conn_receive()`均不释放。旧板一次完整会话后
  `ref=19 == HOST conn_rx=19`。board `__wrap_bt_conn_receive()`现只配对释放该引用；verifier直接
  检查official源码ownership变化并fail-closed，NuttX/SDK保持零改动。
- wrapper修复后的negative gate一次通过，length/magic/version/CRC全部被拒绝且合法echo仍可用；
  正式20轮uncached重连`20/20`，结束时Host/HCI/N13=`22/22/22`、引用0。
- active gate中BLE `100/100`分别与RPMsg六场景×100、RPMsgFS四档×20并发PASS。RPMsg满载使
  整个BLE会话延长到45.41秒，因此使用显式90秒deadline；功能、heap和最终健康均闭环。
  全部测试后lifecycle=`25/25/25`，AP READY、RPTUN CONNECTED、supervisor HEALTHY、CPU2 online、
  pending 0/0、所有SMP gate PASS，最终引用仍为0。

## 6. 验收矩阵

| Gate | Windows证据 | CP/AP证据 | 失败判据 |
|---|---|---|---|
| stock ACL probe | connect、GAP service、Device Name read | HCI/ACL RX/TX counters非零；系统健康 | 只能看到广播但 discovery/read timeout |
| custom advertising | name、address、service UUID | lifecycle=`ADVERTISING` | 地址变化、UUID endian错误、start timeout |
| lifecycle 20 | **20/20 PASS**：每轮uncached discover/disconnect后再次找到 | 结束后Host/HCI/N13=`22/22/22`、ref=0 | 任一轮不再广播或 counter不等 |
| echo | **PASS**：write success、read/notify同 sequence/value | accepted/replied +1 | 本地 accepted但 Windows无 matching response |
| negative write | **PASS**：四类`4/4`拒绝后合法echo/link仍在 | ATT length=`0x0d`，其余当前语义=`0x0e` | crash、disconnect或错误 frame被接受 |
| notify 100 | **PASS**：exactly 100、CRC全对、连续 sequence；主动并发重复通过 | queue/error=0；attempted不是delivery真值 | duplicate/lost/CRC error |
| unsubscribe | **PASS**：取消CCC后0 frame且link usable | subscribed=0 | 仍收到 notify |
| coexistence | **PASS**：BLE分别与RPMsg、RPMsgFS主动并发 | RPMsg 6×100、RPMsgFS 4×20、SMP/health/heap全PASS | bounded deadline超时、worker漂核、heap持续增长 |
| reset | **PASS 3/3**，每轮重新发现/连接 | cold_path、READY、CPU2 online | warm residue依赖或首次命令乱码 |

## 7. Windows/WSL2 验证策略

不得启动 `C:\Users\lijian\Downloads\BLEDebug\BLEDebug.EXE`。用户已确认该 GUI会造成电脑
明显卡顿；历史截图不作为后续 gate证据，也不需要删除。N13只运行仓内 native Windows WinRT
无 GUI CLI，WSL2负责启动并收集 JSON/JSONL。

自动工具运行在 Windows native WinRT，WSL2通过 `powershell.exe` 启动并收集 JSON/JSONL：

```text
scan → match address/name/service UUID
→ connect → discover
→ read/write-with-response
→ subscribe → collect N frames with deadline
→ validate sequence/CRC → unsubscribe/disconnect
```

WSL2不直接拥有 Windows Bluetooth radio。工具不得要求安装内核驱动，也不得把系统配对
缓存当作连接成功；每轮输出 adapter、device address、service/characteristic UUID、timestamp、
frame count、first/last sequence、CRC/lost/duplicate和exit code。

## 8. Fail-closed、回退与观测

- 所有 CP→AP control request带 RPTUN generation/sequence和总 deadline；断链返回
  `-ENOTCONN/-ESTALE/-ETIMEDOUT`。
- GATT request queue满返回 ATT error，不 drop-oldest、不阻塞 LPWORK。
- notify API无返回值；`attempted`只表示调用，Windows `received/validated`才是完成值。
- advertising start/stop失败进入可观测 FAULTED；不自动重置 Controller。
- N13 profile可通过切回 `cp_nsh_btipc + ap_smp_btipc`完全回退。
- AP-only warm restart仍 fail-closed；需要恢复时做整芯片 cold reset。
- 机器可解析 UART继续使用既有 `BBTT` 前缀；`bkbttest stats`输出 N13 HCI/ATT/GATT字段。

## 9. 对抗性评审

| 风险 | 分类 | 处置 |
|---|---|---|
| NuttX stock table与custom table都认为自己拥有 GATT DB | blocker | 只注册一张 combined table；禁止运行期切换 |
| Beken Host与NuttX Host双 owner | blocker | 不链接 Beken Host/GATT archives |
| GATT callback阻塞 LPWORK导致所有 ACL停顿 | blocker | callback只固定 copy/post；业务 worker隔离 |
| multi-peer CCC aggregate造成错发 | accepted limitation | N13 profile固定 max connection 1 |
| write-without-response current source差异 | accepted limitation | characteristic不声明该 property |
| notify本地调用无完成状态/ATT buffer耗尽 | single-run fixed / API limitation retained | 50 ms pacing下Windows 100/100通过；sequence/CRC/count仍是唯一端到端判据 |
| disconnect重新广播与stock Host陈旧flag竞态 | fixed / verified | connect worker同步flag，disconnect worker只single full start；20/20与最终`25/25/25`通过 |
| stock inbound ACL connection引用未释放 | fixed / verified | board receive link wrapper精确release；source verifier防double release；三次J-Link gate均ref=0 |
| BK7258为无须响应的`0x0c35`返回`status=0x07,ncmd=0` | implementation fixed | profile关闭Host flow control；board lower-half只过滤该opcode并记drop counter |
| scan与advertising并发争用 GAP owner | accepted limitation | 显式 stop adv → scan → start adv，首版不并发 |
| Windows缓存/queued GATT connection | historical host issue / closed | 系统重启后negative和20轮uncached gate全部通过；缓存路径不作为正式20轮证据，timeout仍不堆叠重试 |
| AP warm restart留下SDK heap pointer | blocker outside N13 | 继续禁止 AP-only Bluetooth restart |

## 10. 完成状态与后续边界

N13批准范围已全部完成并升级为`board-verified`。最终实现、镜像hash和实板证据以
[N13 evidence index](../n13-evidence-index.md)为准。主动RPMsg满载下BLE会话45.41秒是首版性能
基线；它在显式90秒deadline内完成，不宣称为产品吞吐/SLA。

后续若进入N14，应重新做需求与架构gate，不在N13中顺带加入indication、security、multi-Central、
Wi-Fi/BLE coexistence或AP-only Bluetooth warm restart。不得启动`BLEDebug.EXE`；官方NuttX与SDK
源码继续只读。
