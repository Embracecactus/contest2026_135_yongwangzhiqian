# BK7258 N13 BLE GAP/GATT source verification

> 日期：2026-08-03
> 状态：**`source-verified` + `build-verified` + `board-verified`**；四类negative、
> 20轮uncached重连、BLE 100帧与RPMsg/RPMsgFS主动并发、最终系统健康及连接引用归零均已闭环
> 基线：contest `d4661fd`，NuttX `e02f581e235fc7b527d57ff62b668ce625d139ab`，
> apps `e81a73794786189f15e6c9fe9931ffddd561fd73`
> 边界：官方 NuttX 与 Beken SDK 只读；永久实现只能进入 contest board/app/tool overlay
> 证据索引：[N13 evidence index](n13-evidence-index.md)

## 1. 结论先行

N13 继续沿用 N12 已验证的双核职责，不引入第二套 Bluetooth Host：

```text
Windows Central / GATT client
        ↕ BLE air link
CP: official Beken BLE Controller-only archive
        ↕ official MB_CHNL_BT_CMD pointer IPC
AP: board HCI lower-half → stock NuttX BLE Host/GAP/GATT
        ↕
board-owned N13 GATT service + bounded validation control plane
```

冻结结论：

1. **不需要更新或重编 SDK 静态库。**N13 使用的 GAP/GATT/ATT 均在 AP 的 stock NuttX
   Host 中；CP 继续使用 N12 的 v3.1.1.9 controller-only BLE、PHY 和 IPC archives。
2. N12 wrapper 已在源码和链接层覆盖 HCI command/event 与双向 ACL；N13 实板已完成
   stock GAP Device Name read、custom service read/write-with-response、四类错误写入拒绝、
   100-frame notify、20轮uncached重连及主动RPMsg/RPMsgFS共存，证明连接后的
   ACL/L2CAP/ATT双向数据面和重复生命周期。N13批准范围现为`board-verified`。
3. 当前 NuttX 在 `bt_netdev_register()` 内自动注册 stock GAP attribute table，并立即启动
   connectable `ADV_IND`。该零代码 N13-A 探针已由仓内无 GUI WinRT CLI 完成，地址
   `c8:47:8c:47:47:48`、Device Name `NuttX` 均匹配。
4. 当前 `bt_gatt_register()` 不是增量 API：它只替换一个全局 database 指针和 count，
   没有 unregister、append 或锁。自定义服务必须注册一张**包含 GAP + N13 service 的完整、
   静态、排序且 handle 唯一的表**，并且只允许在启动期、尚无连接时执行一次。
5. 首版只批准：connectable advertising、单连接、read、write with response、CCC notify。
   indication、write without response、long/prepare write、pairing/bonding/security、并行多连接
   和 Bluetooth profile 的 AP-only warm restart均不在 N13 首版范围。
6. 默认 ATT MTU 是 23；首版 wire frame 固定 20 bytes，正好落在默认 ATT value payload
   上限内，不把 MTU exchange 作为前置条件。

实现、历史诊断与最终证据（按时间记录；末尾闭环状态覆盖较早的pending描述）：

- board overlay 已新增 combined GAP+N13 静态表、CPU0 worker、被动 HCI lifecycle observer、
  MTU 527→517 兼容、HCI/ATT 诊断、独立 CP/AP profile、静态 verifier 和 Windows WinRT CLI；
- official NuttX、apps 与 v3.1.1.9 SDK 源码/静态库均未修改；
- board-owned SDK wrapper原先把`bk_delay_us()`实现为空函数，而official v3.1.1.9
  Bluetooth/PHY archive依赖真实微秒busy-wait；现已映射为NuttX `up_udelay(us)`。CP N13
  profile同时选择official FreeRTOS等价的1 ms tick，均未修改official SDK/NuttX；
- 根因定位到 BK7258 Controller 对 opcode `0x0c35` 返回非标准 Command Complete
  (`status=0x07, ncmd=0`)，并发现 stock auto-enable 即使返回 success 也可能未恢复 RF；
- 源码复核进一步确认：Controller 接受 legacy connection 时已自动停止 RF advertising，
  但当前 NuttX 保留 `g_btdev.adv_enable=1`；旧实现因此在 disconnect 形成 stock auto-enable
  → board disable → board enable 的三命令竞态。新 wrapper 在 connect worker 中调用一次
  `bt_stop_advertising()` 只同步 Host flag，disconnect worker随后按官方 sample只执行一次
  full start；verifier已固定“stop只在connect路径、start只在disconnect路径”的契约；
- 20 ms notification间隔在100-frame压力下复现ATT发送资源耗尽/静默丢帧。当前NuttX
  `bt_gatt_notify()`返回`void`，`bt_att_create_pdu()`失败时callback只返回，调用者无法得到错误；
  N13 AP profile现固定50 ms间隔，使100-frame burst约5秒，Windows接收结果是唯一完成真值；
- 50 ms镜像已完整构建，RPTUN与BLE verifier均PASS；稀疏烧录三段均`WriteFlash ->pass`。
  独立RTS物理冷复位在
  `$WORKSPACE/logs/bk7258-n13/n13-pacing50-rts/20260803-031501/`取得
  `PASS_NSH`/`cold_path=yes`；
- 该镜像CP/AP CRC SHA-256分别为
  `d48c76558387e2eb21e7a69ada316010c72bcad2ff40bfca6374bd2c180a0d38` /
  `4578db59cde073f2a3b62e30502da5f73488aa504c8e3a6a95c70875a92dcc39`；对应padded flash
  SHA-256为`b89477f065c7def3b102630fc267d1dd6361536dd35fda5a05503331e275c920` /
  `798c46fa35189334aaff5114d3c4547aa88e7e521eb08960ec50a30b91b994fb`；
- earlier `pnputil /restart-device`所要求的Windows系统重启已经完成，不再是pending prerequisite；
- 同一镜像的一次完整无GUI WinRT gate读到`BK7258 N13`与固定service/handles，echo匹配，
  notification requested/received=`100/100`，sequence=`318771200..318771299`，CRC/lost/
  duplicate全0，unsubscribe quiet、link usable及post-close rediscovery全部通过。JSON为
  `$WORKSPACE/logs/bk7258-n13/n13-pacing50-cli/result-100.json`，SHA-256
  `5896a0611f6cd6da131095b82e2e29925a8199390f41218841b5eb9799d47ed5`；
- 后续Windows进程有时形成Controller连接但未发送ACL/ATT，30秒discovery timeout后板端以
  reason `0x13`正常断开并重广播。第一份板端统计为
  `connected/disconnected/readvertised=4/4/4`、state=ADVERTISING、last_error=0、queue_full=0，
  且无HCI/Host error。证据
  `$WORKSPACE/logs/bk7258-n13/n13-pacing50-cli/stats-final-lifecycle.bin`，SHA-256
  `efc62c1349a6c3f74623d7f27a6f66b35ed500fab1ce5f279fc7a6e121974717`；
- 无GUI CLI仅对明确`Unreachable`做fresh-object有限重试；uncached service discovery才算
  RF/ATT proof。discovery timeout不重试，因为Windows连接过程不能真正取消，避免堆叠请求；
- CLI已新增默认关闭的`--n13-negative`：依次注入bad length/magic/version/CRC，四次均必须为
  WinRT `ProtocolError`或exact Windows SDK ATT HRESULT，随后合法echo必须继续通过。Windows
  `/W4` build与adapter probe已PASS，
  被阻断的AEP-first候选EXE SHA-256为
  `aa9d52212830d13759c50a27ec1d9ede437f843fc0918cecd4271ff3379f28a8`；
- 新工具的一次单attempt真机运行在uncached service discovery阶段timeout/exit8，尚未发送任何
  negative write且未生成result JSON。J-Link前后读取`g_stats`证明bad length/magic/version/CRC
  均保持0，accepted/CCC/notify也不变；板端则正常从`4/4/4`推进为`5/5/5`并保持
  ADVERTISING/error0/queue0。UART证据
  `$WORKSPACE/logs/bk7258-n13/n13-negative-cli/stats-after-host-timeout.bin`，SHA-256
  `94db90f0209095870eb5ec3a70609a01ab7ca6e53ac1e94f5951297b3bac7247`；
- 完整100-frame PASS来自address-first EXE
  `700d933f2acfc0daa3e06cff872b3ce5606c1f3c0437c320e27a341ef7e7a8ae`；因此最终工具恢复
  address-first、AEP ID作为第二候选。candidate session在discovery前也由RAII管理，timeout时明确清
  `MaintainConnection`并`Close()`。该候选重新`/W4` build/probe PASS，当时EXE SHA-256为
  `b9f0ead1c4f512f30a203881e1a2f47d547da25a69872f40355d9c72b0f8cd4c`；
- 最终address-first候选随后只执行一次真机请求。板端Controller连接/断开并重新广播，但Windows
  仍在uncached service discovery 30秒超时，未产生新ATT trace、未发送坏帧、exit8且没有结果
  JSON；没有叠加重试。pre/post UART显示lifecycle由`5/5/5`推进为`6/6/6`，ACL RX只增加1，
  state/error/queue=`2/0/0`。证据位于
  `$WORKSPACE/logs/bk7258-n13/n13-negative-final/`，pre/post raw SHA-256分别为
  `60e3c67ba18df2846a8b922bd0fdd2a42484503dd5ec4df05612023115c76edc`和
  `ce459c2379822e4cb0bcaea710e3bcb2e38e66db34e26ca3e11111ccc125ddd0`；
- 为绕过Windows卡在uncached enumeration的问题，CLI新增严格限制在`--n13-negative`下的
  `--n13-cached-discovery`。它只缓存已冻结service/characteristic/CCC索引；uncached GAP/status
  read、真实ATT拒绝、合法echo/notify、退订和重发现仍是成功前置，且JSON明确记录cache mode。
  第一次single attempt完成uncached Device Name和status read后，19-byte write被实板以ATT
  `0x0d`拒绝；ATT trace与J-Link共同证明仅`writes_bad_length`从`0→1`，其他bad counter和
  accepted均为0。WinRT把拒绝投影成HRESULT `0x8065000d`而非status，旧classifier因此fail-closed
  停止、未生成JSON。工具现严格接受status或Windows SDK exact HRESULT：length=`0x8065000d`，
  当前NuttX magic/version/CRC=`0x8065000e`。修正版`/W4` build/probe PASS，EXE SHA-256为
  `ceafb0457983de8760c0410148ad1ff18e3753e77d44b86f38fd21ef465a70a3`。
- 修正版随后只做一次single attempt，但Windows这次在cached service enumeration前超时，未发
  ATT且无JSON；未重试。latest cold后的board lifecycle由`1/1/1`健康推进到`2/2/2`，state/error/
  queue=`2/0/0`，ATT trace与bad counters不变。完整证据与raw hashes位于
  `$WORKSPACE/logs/bk7258-n13/n13-negative-cached/summary.md`。因此invalid length已
  board-observed；magic/version/CRC与post-reject valid echo仍待clean host session。
- 后续恢复会话先通过adapter probe和广播scan。一次device-wide cached请求仍在ATT前timeout；
  CLI因此增加两条边界清楚的host-only路径：cached模式先按`BluetoothDeviceId + UUID`查精确
  service instance，未配对设备没有PnP instance时只尝试legacy单UUID cache；另有独立
  `--n13-targeted-discovery`用`GetGattServicesForUuidAsync(..., Uncached)`分别查GAP/N13，
  不枚举全部service。当前Windows session中exact cache为0、legacy cache返回`0x80070490`，
  targeted GAP query仍在ATT前timeout。最终board lifecycle健康到`4/4/4`，state/error/queue=
  `2/0/0`，ATT trace保持sequence37；没有结果JSON。最终`/W4` EXE/probe PASS，SHA-256为
  `a7a977dc0ed3d4a81c386656c51b58cc0229aee5c2ab2fb2ce8aec86667af76b`，证据见
  `$WORKSPACE/logs/bk7258-n13/n13-negative-resume/summary.md`。该结果排除了“仅device-wide
  enumeration卡住”，但未增加任何negative-write证据。
- advertising-idle N13-E smoke已在同一镜像通过：RPMsg payload 1/64/464 × idle/load六场景
  均CPU0/CPU1 5/5、error0且AP heap不变；RPMsgFS payload 1/64/464/1024四档checksum全对，
  AP heap稳定、CP首次service固定增长224 bytes后稳定。pre/post AP READY、RPTUN CONNECTED、
  supervisor HEALTHY、CPU2 SCHEDULER_ONLINE且pending 0/0；最终BLE仍ADVERTISING/error0/
  queue0、lifecycle=`5/5/5`。核心证据SHA-256为
  `60d45aa04bc8d4651d33d939182054b5f43e7c4ead1cce6405cbfe89f9d0e1be`（RPMsg）、
  `03111055fa29af379800c59f8b9ee189b9e81a5de5ddc272546d5d0e3a4ad528`（RPMsgFS）、
  `b5265385e353b66b10789c9fe30e552a8848419a5648371f6aba4705b58c8fa3`（post health）与
  `5169492ae2f78f5fafff9b2687c148e81feb0a04c6320d04151bdf67532cec62`（post BLE stats）。
  该时点active BLE traffic并发和正式满载尚未验证；后续final gate已闭环，见本节末尾。
- N13-V build rollback已部分闭环：N12 `cp_nsh_btipc + ap_smp_btipc`在`v3.1.1.9`和`legacy`
  bundle下均完整构建，通过SDK manifest、RPTUN ELF layout和打包一致性；随后恢复N13
  `v3.1.1.9`构建，RPTUN/BLE verifier均PASS且AP CRC/padded hash精确复现。CP镜像包含NuttX
  自动生成的`e02f581e23 Aug  3 2026 04:02:57`构建字符串（raw offset `0x8f118`），因此其
  重建hash变化是预期行为。证据目录为
  `$WORKSPACE/logs/bk7258-n13/n13-v-build-regression/20260803/`；official NuttX/apps tracked
  diff仍为空。
- physical RTS cold startup现为3/3 `PASS_NSH`/`cold_path=yes`：原
  `$WORKSPACE/logs/bk7258-n13/n13-pacing50-rts/20260803-031501/`，以及新增
  `n13-v-cold-repeat/round2/20260803-040716`和`round3/20260803-040755`。第三轮后BLE以全新
  lifecycle `0/0/0`进入ADVERTISING，HCI error为0；独立`apctl status`确认AP READY、RPTUN
  CONNECTED、supervisor HEALTHY、CPU2 SCHEDULER_ONLINE与全部SMP gate PASS。首个串口会话中
  一次`apctl`命令注入异常由后续`help + apctl status`复跑排除。
- Windows系统重启后，最终negative会话以uncached discovery一次通过：bad length得到
  `0x8065000d`，bad magic/version/CRC各得到`0x8065000e`，四次均被拒绝；随后合法echo、
  `1/1` notify、unsubscribe和rediscovery全部PASS。JSON SHA-256为
  `65ca0da04f5ddf7220d0f5c2da0eb6800f9bb90e0e08a62df28fc62dad2592af`。
- 正式20轮重连全部一次PASS；每轮均为uncached service discovery、GAP read、echo、notify、
  unsubscribe、disconnect和RF rediscovery。20份JSON的sha256sum manifest digest为
  `d9e0cacbe90f5d47bbefc77d44a9b3b1c5f4a077c314318a45a4dd3e2d574728`；板端随后精确为
  Host/HCI/N13 `22/22/22`、error/queue=`0/0`。
- 主动并发正式通过：BLE `100/100`与RPMsg payload 1/64/464 × idle/load六场景各
  CPU0/CPU1 `100/100`同时PASS，RPMsg heap稳定；显式90秒deadline下BLE总会话45.41秒。
  BLE JSON与RPMsg serial SHA-256分别为`aa13ac79...ed49`和`4c342b02...499`。同样的BLE
  100帧与RPMsgFS payload 1/64/464/1024各20/20同时PASS，耗时14.18秒，证据分别为
  `1359067f...c89`和`19500381...aba`。
- 全部测试后AP READY、RPTUN CONNECTED/pending 0/0、supervisor HEALTHY、CPU2 online与
  SMP gates全PASS；Host/HCI/N13 lifecycle=`25/25/25`，PDU/HCI/Host/queue error全0。
  J-Link最终再次确认`g_conns + 0x48`引用为0、`+0x4c`状态为DISCONNECTED。
- 最终重建镜像的CP/AP CRC为`208f4d5d...bfd2b`/`4fd8a199...cc68`，padded segment为
  `5036bacd...5e7d`/`ffedd085...6be4`，factory image为`eddca830...a79e`；final sparse
  flash在`$WORKSPACE/logs/bk7258-auto-debug/20260803-073017/`全部写入验证并启动成功。
  official NuttX/apps tracked diff为空，SDK源码和静态库未修改或重编。

## 2. 已核实版本与官方资料

### 2.1 当前仓库

| 对象 | 冻结值 |
|---|---|
| contest baseline | `d4661fd5106ea8c95f4cb73405bb7b953e5d129e` |
| NuttX | `e02f581e235fc7b527d57ff62b668ce625d139ab` |
| apps | `e81a73794786189f15e6c9fe9931ffddd561fd73` |
| N12 profiles | `cp_nsh_btipc + ap_smp_btipc` |
| AP default cpuset | `CONFIG_SMP_DEFAULT_CPUSET=0x1` |
| Controller-reported ACL | MTU/buffers `70/20`，来自 N12 实板 info gate |

### 2.2 官方 v3.1.1.9 archives

FAE 提供的原始压缩包保持未解压、只读使用：

| archive | SHA-256 |
|---|---|
| `sample_project-release-v3.1.1.9.tar.gz` | `a6c8402d03366dad438ff1c8e45c393871851cb45fd9216da7b8af51248e37b3` |
| `bk_avdk_smp-release-v3.1.1.9.tar.gz` | `39ae282d6d20f77734b7eed3ceb1c679427180d697b12ab3f61fcc39959efcbd` |

参考文件位于 archive 内：

- `bluetooth/single_mode_ble/gatt_server/README.md`
- `.../ap/ap_main.c`
- `.../ap/gatt_ble/gatts.c`
- `.../ap/gatt_ble/fa00/fa00_server.c`
- `ap|cp/components/bk_bluetooth/ipc/src/bt_ipc_core.c`

官方 GATT server 示例同样把 Host/GATT server 放在 AP、Controller 放在 CP；它采用
connectable/scannable advertising、连接/断开/write callback 只投递 queue、worker 处理
业务、断开后重新启动 advertising、CCC enable 后才 notify。N13 复用的是这些**职责与线程
边界**，不是 Beken Host API 或示例源码。把 Beken Host archive 再链接到 AP 会与 stock
NuttX Host 同时拥有 HCI、connection 和 attribute database，明确禁止。

## 3. HCI/ACL 数据路径

### 3.1 AP → CP/Controller

`board/bk7258_t5ai/chip/ap/bk7258_bt_hci.c` 的 `bk7258_bt_send()` 已实现：

| NuttX type | wrapper validation | SDK call |
|---|---|---|
| `BT_CMD` | 3-byte header、parameter length 完全匹配 | `bt_ipc_hci_send_cmd()` |
| `BT_ACL_OUT` | 4-byte header、ACL data length 完全匹配 | `bt_ipc_hci_send_acl_data()` |
| `BT_ISO_OUT` | 不支持 | `-EOPNOTSUPP` |

官方 AP `bt_ipc_hci_send_acl_data()` 为 header+payload 分配 SDK heap、复制 caller data，
再通过 `HCI_ACL_DATA_PKT` 发送指针。因此 NuttX buffer 在 SDK call 返回后可以释放。

### 3.2 CP/Controller → AP

官方 CP/AP `bt_ipc_core.c` 都把 mailbox ISR 收到的 HCI pointer 放入长度固定的 SDK queue；
`bt_ipc_thd` 再构造 H4 event/ACL、调用注册 callback，callback 返回后释放临时 H4 copy，并以
`HCI_FREE_PKT` 把原始 allocation 交回源核释放。

AP wrapper 校验 H4 ACL/event 总长度，去掉 H4 type byte后调用 `bt_netdev_receive()`；该函数
在返回前同步复制 packet，然后把普通 event 与 ACL 投递到 NuttX `LPWORK`。因此 GATT/ATT
callback 不发生在 mailbox ISR，也不依赖 SDK 临时 buffer 的生命周期。

### 3.3 已有硬件证据与剩余 gate

N13 的 stock GAP probe 与完整 custom data-plane gate 已实板经过下列完整链路：

```text
Windows connect/service discovery/read/write
→ Controller ACL RX
→ official pointer IPC
→ AP wrapper BT_ACL_IN
→ NuttX L2CAP/ATT/GATT
→ NuttX BT_ACL_OUT
→ official pointer IPC
→ Controller air TX
```

早期3-frame smoke的板端统计为 ACL RX/TX `30/34`，`receive_errors=0`、`pdu_fail=0`。
最终50 ms镜像又由同一CLI进程完整通过GAP read、echo read/notify、100/100 burst、退订、
link-alive和断开后重发现；该镜像已有lifecycle累计`6/6/6`且error/queue_full为0的历史证据。
latest physical cold后的当前运行又健康到`4/4/4`。数据面与单次重新广播功能风险已经关闭；
剩余gate是20轮正式重复、三类negative write+valid echo、active共存与最终回归。

## 4. stock NuttX GAP/GATT 行为

### 4.1 自动 advertising 与默认服务

当前调用顺序为：

```text
bk7258_bt_hci_initialize()
→ bt_driver_register()
→ bt_netdev_register()
→ bt_driver_set() / bt_initialize()
→ bt_add_services()
   → bt_gatt_register(stock GAP table)
   → bt_start_advertising(BT_LE_ADV_IND, ...)
```

`bt_services.c` 的默认表只包含 GAP Device Name 与 Appearance；默认配置为：

- `CONFIG_DEVICE_NAME="Apache NuttX"`
- `CONFIG_DEVICE_LOCAL_NAME="NuttX"`
- public own address、legacy `ADV_IND`
- advertising min/max interval 均由当前 core 固定为 300 units

`bt_netdev_register()` 不传播 `bt_add_services()` 的返回值。因此 N13 profile 必须在 HCI
register 返回后显式停止 stock advertising、注册完整 N13 table、重新启动 custom
advertising，并检查每个可返回错误的步骤；必要 service 未 ready 时 AP 不发布 READY。

### 4.2 database 是全局 replace，不是多 service registry

当前 `bt_gatt_register()` 仅执行：

```text
g_db = attrs;
g_attr_count = count;
```

它返回 `void`，没有锁、duplicate-handle 检查、append、unregister 或 connection-state gate。
因此：

- 表必须具有 static lifetime；
- attribute handles 必须由 team verifier 检查单调、唯一、非零；
- 注册后不再修改 table、UUID、characteristic 或 CCC object；
- 不允许运行期切换 stock/custom table；
- GAP attributes 必须复制为等价的 board-owned table entries，否则 custom registration 会让
  Device Name/Appearance 从 ATT database 消失。

这不是修改 NuttX 源码；实现仍只调用其公开 server API 和宏。

### 4.3 read/write callback contract

`struct bt_gatt_attr_s` 的 read/write callback 由 ATT receive path同步调用。read 必须在返回前
填好 response；write 返回值必须等于输入 length，否则 ATT 发送 error。N13 callback 只能：

1. 验证 handle/offset/length/magic/version/CRC；
2. 在极短的 SMP-safe critical section 内复制固定 20-byte frame；
3. 更新有界 counter、post semaphore；
4. 立即返回。

禁止在 callback 中做 UART、RPMsg、文件 I/O、sleep、advertising HCI command 或通知 burst。

当前 `att_write_req()` 会先消费 ATT write header，再把纯 value 交给 attribute callback；
该路径批准为首版 write-with-response。当前 `att_write_cmd()` 路径与它不同：源码未先消费
`bt_att_write_cmd_s` header 就把剩余 buffer 传给相同 write helper。该差异没有现成测试覆盖，
且项目禁止 patch NuttX，因此首版 characteristic 不声明
`BT_GATT_CHRC_WRITE_WITHOUT_RESP`。prepare/execute write 需要 `flush` callback，也不进入首版。

### 4.4 CCC、notify 与多连接限制

`BT_GATT_CCC` 必须提供固定-size `bt_gatt_ccc_cfg_s` array；`bt_gatt_notify()` 会从 value
handle 向后查找 CCC，然后向已连接 peer 发送 ATT notification。

当前实现限制：

- API 返回 `void`，不能把本地调用解释为 peer 已收到；
- 源码含 `TODO: Handle indications`，首版禁止 indication；
- notify gate 使用 CCC object 的 aggregate `ccc->value`，而发送 loop 没有再检查每个
  `cfg[i].value`。多连接下一个 peer subscribe 可能使其他 connected peer也进入发送路径。

因此 N13 profile 将 `CONFIG_BLUETOOTH_MAX_CONN=1`，端到端成功只以 Windows 收到且通过
sequence/CRC 校验为准。多连接是未来独立 gate，不能通过扩大 cfg array 宣称支持。

### 4.5 MTU 与 frame size

NuttX ATT context连接时初始化为 MTU 23。ATT write/notify 的 value payload 上限为 20 bytes。
Controller 报告的 ACL MTU 70 不等于 ATT MTU，不能据此发送 67-byte attribute value。

首版统一使用 20-byte little-endian frame：

| offset | field | size |
|---:|---|---:|
| 0 | magic `0x31474c42`（wire bytes `42 4c 47 31` / `BLG1`） | 4 |
| 4 | protocol version `1` | 1 |
| 5 | opcode | 1 |
| 6 | bounded count | 2 |
| 8 | sequence | 4 |
| 12 | value/seed/status | 4 |
| 16 | CRC32 over bytes 0..15 | 4 |

CRC 固定为 CRC-32/ISO-HDLC：reflected polynomial `0xedb88320`、init/xorout
`0xffffffff`，不把主机语言或 CPU native struct padding带入计算。

首版不需要 MTU exchange。以后若扩帧，必须先加入 MTU negotiation、peer-observed MTU、
fragmentation/backpressure 和新的 wire version。

## 5. 线程、CPU affinity 与生命周期

### 5.1 执行上下文

| 事件 | 当前上下文 | N13 policy |
|---|---|---|
| mailbox RX | SDK logical-channel ISR | SDK 自己只入 queue |
| NuttX HCI command TX | `CONFIG_BLUETOOTH_TXCMD_PRIORITY=120` | 最高的软件发送层级；命令串行化 |
| SDK HCI callback | AP `bt_ipc_thd`，runtime priority 98 | wrapper 校验/copy，不阻塞，返回后 SDK 才释放 pointer |
| ordinary HCI event / ACL / ATT callback | NuttX `LPWORK`，priority 97 | validate/copy/post only |
| N13 service logic | `bk7258-ble-gatt` SCHED_FIFO worker | affinity `0x1`，priority 96 |
| CP test command | NSH + generation-safe RPMsg endpoint | bounded wait，CP UART 为权威日志 |

最终固定优先级链为 `HCI TX 120 > bt_ipc_thd 98 > LPWORK 97 > N13 worker 96`。
它保证 SDK receive callback先归还跨核 pointer，stock Host再完成 ATT/disconnect cleanup，最后
由 board worker处理业务或重新广播。源码 `static_assert` 与构建 verifier共同固化这些不等式。

`bkrpmsgtest`是独立诊断负载，其CPU0/CPU1 worker priority为120，不属于Bluetooth服务链。
完整六场景×100并发时，它会显著推迟priority 96的N13节拍：一次30秒burst deadline超时，但
board无错误并恢复广播；显式90秒bounded复跑在45.41秒内同时完成BLE `100/100`与RPMsg
`6/6`。该值作为首版调度竞争基线记录，不通过降低既有RPMsg压测优先级伪造性能，也不把它提升
为产品SLA。

### 5.2 connection observer

当前 NuttX 的 `bt_conn_cb_register()` 只声明在 internal `bt_hcicore.h`，没有 unregister，
并且 `bt_initialize()` 会清空全局 callback list。为避免 board wrapper绑定 private source ABI，
N13 首版不直接调用该 internal API。

实现是在 AP HCI wrapper完成 `bt_netdev_receive()` 复制/投递后，被动解析标准 HCI
LE Connection Complete 与 Disconnection Complete，只更新 counter并唤醒 priority 96 worker。
observer 不消费、不改写 packet，也不替 stock Host决定 connection state。

实板推翻了“只依赖 stock auto-enable 即可”的源码假设。进一步逐行复核表明：legacy
Controller在接受连接时已经自动停止 advertising，但当前 NuttX没有同步清除
`g_btdev.adv_enable`；所以 `hci_disconn_complete()` 会基于陈旧 flag排队一次auto-enable。
旧board worker随后再disable/full-start，形成 enable→disable→enable竞态，即使三次completion
都success，RF也可能保持静默。

最终候选实现把职责拆开：priority 96 connect worker在stock LPWORK处理Connection Complete后
调用一次`bt_stop_advertising()`，同步Host flag为0；disconnect时stock只做ATT/connection cleanup，
不再auto-enable；同一低优先级worker最后按official SDK sample只执行一次完整
`bk7258_ble_gatt_start()`。connect-side stop固定`+1`、disconnect full start固定`+4`；总HCI
command数还包含Windows触发的其他连接管理命令，不能作为精确24门禁。静态verifier负责固定
stop/start调用归属，功能门禁由`connected==disconnected==readvertised`和断链后RF scan承担。

Controller已因建立legacy连接而自动停止advertising时，冗余disable返回非零HCI status；当前
NuttX把所有该类status统一映射为`-EIO`。board只在这个已证明的post-connection Host-flag同步点
接受`-EIO`，allocation、queue和timeout错误仍然fatal。最终`25/25/25`与每次RF rediscovery证明
该约束没有吞掉真正的生命周期错误。

### 5.3 inbound ACL connection reference compatibility

旧镜像完成一次完整GATT会话后，J-Link读到`struct bt_conn_s.ref=0x13`；同一时刻
`HOST conn_rx=19`，两者精确相等。逐行追踪当前官方NuttX源码得到明确ownership链：

1. `hci_acl()`调用`bt_conn_lookup_handle()`，返回值携带一个调用者引用；
2. `hci_acl()`随后调用`bt_conn_receive(conn, buf, flags)`；
3. 当前`hci_acl()`和`bt_conn_receive()`都没有释放该引用。

因此每个入站ACL包泄漏一次引用。虽然Controller可以再次建立物理连接，single-connection Host
slot仍被旧对象占用，后续Windows连接不会进入正常ATT/GATT路径。

最终实现不改NuttX：board link wrapper的`__wrap_bt_conn_receive()`先调用真实
`__real_bt_conn_receive()`，返回后执行一次`bt_conn_release(conn)`，只配对`hci_acl()`取得的
caller-owned引用。该兼容由`CONFIG_BK7258_BT_CONN_RX_REF_COMPAT`显式启用。构建verifier直接
检查同级official `bt_hcicore.c`和`bt_conn.c`；若未来upstream在任一处开始release，构建会
fail-closed，避免double release。

最终negative后的`2/2`、20轮后的`22/22`和全部主动并发后的`25/25`三个J-Link采样均显示
`ref=0`、state=DISCONNECTED。它们与Host/HCI/N13 lifecycle完全相等，是修复有效且无过度释放
的板端证据。

### 5.4 Controller flow-control compatibility

NuttX 在释放每个 `BT_ACL_IN` buffer时发送 HCI Host Number Of Completed Packets
(`0x0c35`)。BK7258 Controller即使已关闭 Controller-to-Host flow control，仍会为它返回非标准
Command Complete `status=0x07, ncmd=0`；足够多 ATT traffic后会耗尽/卡住 command credit。

N13 profile固定 `CONFIG_BLUETOOTH_CNTRL_HOST_FLOW_DISABLE=y`，board HCI lower-half只在该
配置下丢弃 `0x0c35`，并累加 `host_num_completed_dropped`。修复后的实板统计为
`command_tx=22`、`complete=22`、最后 completion `0x200a/status0/ncmd10`、drop `11`，且
connected/disconnected `2/2`、HCI/ACL error为0。该兼容仅位于 board wrapper，不改 NuttX或SDK。

### 5.5 SDK wrapper timing 与 notify backpressure

official v3.1.1.9 的Bluetooth/PHY实现把`bk_delay_us()`作为真实微秒busy-wait使用。原contest
wrapper把它实现为空函数，导致prebuilt archive运行在NuttX上时丢失时序语义；最终wrapper只在
team-owned文件中调用`up_udelay(us)`恢复等价语义。N13 CP profile还单独固定
`CONFIG_USEC_PER_TICK=1000`，匹配official FreeRTOS 1 ms tick。该配置已随50 ms镜像通过物理
RTS冷复位，不改变N12 profile，也不改official源码。

当前NuttX `bt_gatt_notify()`返回`void`。notify callback先调用`bt_att_create_pdu()`；ATT buffer
不可用时只打印warning并返回，调用者拿不到`-ENOMEM`或completion。因此：

- `notify_attempted`只统计本地API调用，不等于air-link delivery；
- 20 ms间隔在100-frame压力下会快于当前ATT/HCI buffer回收，实板出现静默缺帧；
- N13 AP profile固定`CONFIG_BK7258_BLE_GATT_NOTIFY_INTERVAL_MS=50`，100帧约5秒；
- 只有Windows端exact count、连续sequence和CRC全对才算notification gate通过。

### 5.6 lifecycle state

```text
DISABLED → INITIALIZING → ADVERTISING → CONNECTED → ADVERTISING
                    │           │            │
                    └───────────┴────────────┴→ FAULTED
```

- 只有 worker写 lifecycle state；callback 只投递 event。
- stock advertising stop、完整 table register、custom advertising start 都完成后才进入
  `ADVERTISING`。
- connect 后由 stock LPWORK先建立Host连接状态，board worker再stop以清除陈旧
  `adv_enable`；disconnect后stock只释放ATT/CCC，board worker执行唯一一次完整 advertising
  start，成功后递增`readvertised`。
- malformed write、queue full、CRC error 只拒绝该 request并记 counter，不破坏 link。
- HCI/GAP command timeout或database invariant failure进入 `FAULTED`，禁止伪报AP READY。
- 初始化是AP启动期的一次性契约：成功后的重复调用返回`-EALREADY`；首次初始化失败后
  AP main立即发布Bluetooth failure并park，不支持原地retry或Bluetooth AP-only restart。
- AP-only warm restart仍沿用 N12 禁令；N13 不扩大 pointer-quiesce lifecycle 权限。

### 5.7 N13-V focused review结论

对最终候选逐项复核了HCI receive顺序、ATT callback边界、event queue/锁、生命周期、错误映射、
构建隔离和Windows timeout清理，未发现需要改变已实板验证镜像的blocker：

- SDK H4 packet先完成类型和精确长度校验，`bt_netdev_receive()`成功同步复制后才调用
  lifecycle observer；observer只复制标准connect/disconnect token，不替换stock Host路径。
- read/write/CCC callback没有sleep、UART、RPMsg、文件I/O、dynamic allocation或HCI lifecycle
  command；固定frame与event ring由irq-safe spinlock保护，统计字段使用atomic访问，worker固定CPU0。
- NuttX `err_to_att()`只把`-EINVAL/-EFBIG`映射为specific offset/length错误，其他board语义拒绝
  映射为`BT_ATT_ERR_UNLIKELY`。Windows WinRT可能返回`ProtocolError` status，也可能直接抛出
  Windows SDK ATT HRESULT；工具同时接受前者并严格核对后者的`0x8065000d`（length）或
  `0x8065000e`（magic/version/CRC），不错误承诺后三者具有不同ATT error code。
- 单连接且不支持write-without-response；request、disconnect按HCI/LPWORK到达顺序进入有界ring，
  未处理disconnect前不会重新advertise。恶意write-command flood和AP-only Bluetooth warm restart
  均不在首版契约内；已验证数据/生命周期运行中`queue_full=0`。
- `Make.defs`和CMake只把GATT实现加入AP，CP ELF泄漏由verifier检查；N12/latest/legacy rollback
  构建均通过，official NuttX/apps tracked diff为空。
- Windows candidate session在service discovery前即由RAII持有；timeout会清
  `MaintainConnection`并close，且实板失败尝试未发ATT、未生成伪成功JSON。

审查同时修正了board Kconfig的GATT worker默认优先级`99→96`，使默认值与N13 defconfig、文档
及`120 > 98 > 97 > 96`编译期约束一致。N13 profile本来就显式取96，所以此修正文档/配置
一致性，不改变已经验证的固件行为。初始化失败后的原地retry仍明确不支持；若未来要支持，必须
设计worker停止、semaphore销毁与Bluetooth pointer quiesce，不能在N13首版中暗示可恢复。

## 6. GATT table 与 advertising ABI

首版 canonical UUID：

| object | UUID |
|---|---|
| service | `72580001-4e31-3347-4154-545f424c4500` |
| control read/write-with-response | `72580002-4e31-3347-4154-545f424c4500` |
| status read/notify | `72580003-4e31-3347-4154-545f424c4500` |

固定 handle：

```text
0x0001          GAP primary service
0x0003/0x0004   Device Name declaration/value
0x0005/0x0006   Appearance declaration/value
0x0010          N13 primary service
0x0011/0x0012   control characteristic declaration/value
0x0013/0x0014   status characteristic declaration/value
0x0015          status CCC
```

Advertising data放 flags + 128-bit service UUID；scan response放完整 local name
`BK7258-N13`。所有 EIR array 都以 `len=0` entry终止，并由 verifier检查 legacy 31-byte
上限。UUID wire byte order必须由 Windows discovery结果反向验证，不能只看 C initializer。

## 7. 方案比较与 ADR

| 方案 | 优点 | 主要问题 | 决策 |
|---|---|---|---|
| A. 只使用 stock GAP table | 零代码即可做首个连接探针 | 没有 custom write/notify，无法完成 N13目标 | 只用于 N13-A probe |
| B. board wrapper调用 stock NuttX GAP/GATT server API | 延续 N12 owner；不改 NuttX/SDK；可独立 Kconfig回退 | 需处理全局 table replace、callback context 和当前 API限制 | **采用** |
| C. AP 链接 Beken BLE Host/GATT archive | 官方 sample完整 | 与 NuttX Host双 owner，重复 HCI/connection/GATT state，破坏 N12架构 | 禁止 |

### ADR-N13-001：由 AP stock NuttX Host 唯一拥有 GAP/GATT

- 状态：Accepted（用户于 2026-08-02 确认 N13 方向与先规划后实现）
- 决策：CP 保持 Controller-only；AP 的 board wrapper使用 stock NuttX GATT server API，
  一张完整静态 table，单连接、20-byte write/read/notify。
- 正面结果：无新增 SDK library、无官方源码 patch、N12 transport边界不变、可通过独立
  Kconfig/profile回退。
- 代价：必须规避当前 global database、WWR、indication 和 multi-peer CCC限制；Windows
  receipt是 notification唯一端到端真值。
- 反转条件：NuttX checkout切换到具有稳定 dynamic service registry/public connection
  callback/notification completion 的新 API，或项目明确决定放弃 NuttX Host。

## 8. 不确定性账本

| ID | 类型 | 当前结论 | 关闭条件 |
|---|---|---|---|
| U13-01 | closed | stock `ADV_IND`、connect、GAP Device Name `NuttX` 已由无 GUI CLI实板完成 | `$WORKSPACE/logs/bk7258-n13/n13-a-stock-gap-pass.json` |
| U13-02 | closed | custom read/write/notify与双向 ACL/L2CAP/ATT已 air-link observed | 完整CLI echo + 100/100 burst + unsubscribe + rediscovery PASS |
| U13-03 | closed / repeat gate pending | 最终镜像board lifecycle累计`connected/disconnected/readvertised=6/6/6`、error 0 | 功能风险已关闭；N13-B仍需20次正式重复门禁 |
| U13-04 | closed/tool policy | 已实现无 GUI WinRT CLI；用户明确禁止运行 `BLEDebug.EXE`，后续验证不得启动它 | CLI保存 JSON/JSONL与明确 exit code |
| U13-05 | closed / API limitation retained | notify没有返回/complete API；50 ms pacing下100/100实板通过 | 继续以Windows逐帧sequence+CRC/count为完成真值；本地attempted不得替代 |
| U13-06 | deferred | AP-only warm restart缺少 Bluetooth pointer quiesce | 维持整芯片 cold reset验证，不纳入 N13 |
| U13-07 | host repeat blocker | device-wide与按UUID uncached discovery在当前Windows session都在ATT前timeout；板端可正常断开并重广播 | 清洁Windows host状态下完成negative与正式20轮，不在timeout后堆叠重试 |
| U13-08 | partial / host blocker | cached索引+uncached read已实板取得length ATT `0x0d`；WinRT HRESULT语义已适配；targeted模式已build/probe但未取得ATT | clean host下完成magic/version/CRC及valid echo；cached索引不得替代20轮uncached discovery |

## 9. N13-R checklist

- [x] 冻结 contest/NuttX/apps/SDK archive版本与 hash。
- [x] 追踪 AP/CP official HCI command/event/ACL pointer ownership。
- [x] 确认 SDK callback不是 ISR，NuttX receive在返回前复制。
- [x] 确认 stock Host自动注册 GAP并启动 connectable advertising。
- [x] 确认 GATT database是单一全局 replace、无 unregister/append/lock。
- [x] 确认 read/write callback同步发生在 ATT RX路径。
- [x] 确认默认 ATT MTU 23与首版20-byte上限。
- [x] 确认 notify依赖 CCC、返回 void、indication仍为 TODO。
- [x] 识别 multi-peer CCC aggregate风险并冻结 single connection。
- [x] 识别 write-command源码差异并排除 write without response。
- [x] 对照官方 v3.1.1.9 GATT server的 AP/CP owner、worker和重新广播模式。
- [x] 冻结不改 NuttX/SDK、不新增 Beken Host、不更新 SDK static libs。
- [x] N13-A 零代码实板 probe：Windows发现、连接、service discovery、GAP name read。
- [x] custom service gate：GAP name、handle discovery、20-byte echo、100/100 notify、unsubscribe、
      post-close rediscovery，Windows sequence/CRC/lost/duplicate全通过。
- [x] 定位并在 board wrapper隔离 BK7258 `0x0c35`非标准 completion。
- [x] 定位空`bk_delay_us()` wrapper并以`up_udelay()`恢复official archive所需微秒时序；
      N13 CP profile固定SDK等价1 ms tick。
- [x] 复核`bt_gatt_notify()` void/无buffer错误回传语义，以50 ms安全间隔完成100-frame gate。
- [x] connect-side Host flag sync + disconnect-side single restart镜像 build、static verify、flash并
      到达`PASS_NSH`；冷态scan PASS。
- [x] 最终板端`connected/disconnected/readvertised=6/6/6`、state/error=`2/0`，无HCI/Host
      error；同一CLI完整100-frame gate和post-close rediscovery exit0。
- [x] `--n13-negative`四类注入、status/exact-HRESULT判定、valid-echo link probe、cache marker和
      JSON fail-closed均已实现，Windows `/W4` build/probe PASS。
- [x] cached exact-instance/legacy single-UUID边界与独立targeted-uncached服务查询已实现，
      `/W4` build/probe和板端fail-closed行为PASS；当前host未产生ATT，因此不算功能gate PASS。
- [x] N13-C invalid length实板：uncached live read后ATT `0x0d`、bad_length `0→1`、link断开后
      正常重广播；host classifier问题已修正。
- [x] N13-E advertising-idle smoke：RPMsg六场景×5、RPMsgFS四档×1、pre/post health与BLE
      stats全PASS且heap稳定。
- [x] N13-V部分构建回退：N12 latest/legacy和恢复N13 latest完整构建、verifier、official tree
      zero tracked diff均PASS。
- [x] N13-V physical RTS cold startup 3/3 `PASS_NSH`/`cold_path=yes`，cold后BLE/AP/RPTUN/
      supervisor/CPU2状态复核健康。
- [x] N13-V focused review：HCI顺序、callback/queue/lock、lifecycle、ATT error mapping、
      wrapper/link隔离与host timeout cleanup无新增blocker；Kconfig默认优先级已对齐96。
- [x] N13-C四类negative：length/magic/version/CRC全部被真实ATT拒绝，post-reject合法echo、
      notify、unsubscribe及rediscovery PASS。
- [x] N13-B正式20轮：20/20 uncached connect/discover/read/echo/notify/disconnect/rediscovery，
      结束后Host/HCI/N13=`22/22/22`且连接引用为0。
- [x] N13-E active/full-load：BLE 100帧分别与RPMsg六场景×100、RPMsgFS四档×20并发PASS；
      post health、heap、SMP/supervisor及最终`25/25/25` lifecycle闭环。
- [x] N13-V最终证据索引、final image hash、静态verifier和official tree zero-diff复核完成。

N13 在批准的首版范围内已整体`board-verified`。主动RPMsg满载下BLE会话45.41秒是已记录的
性能基线；所有功能和资源gate仍在显式90秒deadline内完成。
`BLEDebug.EXE`不再是工具选项，也不得启动。
