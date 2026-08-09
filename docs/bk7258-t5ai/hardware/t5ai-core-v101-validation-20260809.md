# T5AI-Core V1.0.1 全面验证记录（2026-08-09）

## 1. 结论

T5AI-Core V1.0.1 已完成第一轮分层验证。启动安全链、CP/AP 双镜像、AP SMP、RPTUN/RPMsg、RPMsgFS、PSRAM、LittleFS、UART、J-Link SWD、Wi-Fi、Bluetooth HCI 和 BLE GATT 板内广播状态均获得实板证据。

当前不能宣布“整板全部通过”，原因有两类：

1. 单 MIC 音频、电池采样及排针外设需要额外接线、声学观察或外部器件，未把“驱动可编译”误记为“硬件已验证”；
2. Wi-Fi 功能闭环已经通过，但官方 AP archive 仍会输出一次 `wdrv_tx_msg: cmd confirm timeout`；命令实际完成、缓冲区已释放，暂作为稳定性观察项保留。

P9 LED 和 P29 USER 按键均已完成软件读写与维护者现场观察，Core 板最小人机接口验证闭环。

验证过程中没有修改 NuttX 或官方 SDK 源码，没有写入 OTP/eFuse，也没有启用不可逆 Secure Boot 生命周期。

## 2. 验证对象

- 板卡：T5AI-Core V1.0.1
- 原理图：[T5AI-Core V1.0.1](schematics/T5AI-Core_V101-SCH-a69f7b5a91b4bf21a39bdb7c17812373.pdf)
- 原理图 SHA256：`091b7a9302ec51cbb71f12b339cddbf34cf5d5a28b529f64613e498759eef086`
- 验证分支：`verify/bk7258-t5ai-core`
- 合并后基线：`4564394`
- 芯片静态库：官方 BK7258 SDK `v3.1.1.9`，CP/AP manifest 校验均通过
- 下载器：BK Loader `2.1.11.15`
- 串口：COM7 下载/复位，COM11 控制台，`460800 8N1`
- 调试器：J-Link V9，SWD `1 MHz`
- Flash：稀疏写入 BL1、主/备 BL2、主槽 CP/AP；保留 LittleFS、B 槽和校准尾区

最后烧录的回归验证镜像使用 MCUboot 版本 `18.1.10`、security counter `27`。关键烧录段 SHA256：

| 段 | SHA256 |
| --- | --- |
| BL1 `bl_crc.bin` | `2961286f973fb9bd1624baf23444fac2ad12033585d8f63fca6e87c0d72e1a25` |
| CP `app_crc_flash.bin` | `b979f3801b92d6bb288c6e09d4ece3aa69f62aa877a1f5976224e7180af236a9` |
| AP `app1_crc_flash.bin` | `ca7e5938795dbb997d8765e05f419852e1e22e48873772c49fd63adbac6f3e23` |
| BL2 主/备 | `535571b677f0ced7d2c8a49b2495fbc0b2778657dfab50cb732c56a106204f17` |

## 3. 验证矩阵

| 项目 | 状态 | 实板证据或边界 |
| --- | --- | --- |
| SDK/分区/打包 | PASS | v3.1.1.9 CP/AP checksum、动态分区、A/B 布局和 factory layout 检查通过。 |
| BL1 -> MCUboot BL2 -> CP/AP | PASS | 串口依次出现 `B1PRIMARY`、`B2INIT`、`B2SELA`、`B2APOK`、`B2HANDOFF`，最终进入 NSH。 |
| 稀疏下载 | PASS | 五个受控段均由 BK Loader 报告 `WriteFlash ->pass`；LittleFS、B 槽和校准尾区未覆盖。 |
| UART/NSH | PASS | COM11 可持续收发命令，NSH 正常响应。 |
| J-Link SWD | PASS | `VTref=3.293V`，DPIDR `0x1BE12AEB`，识别 STAR r1p0；CPUID `0x631F1320`。 |
| AP 启动与 CPU2 SMP | PASS | AP `READY`、CPU2 `SCHEDULER_ONLINE`，online mask `0x3`；affinity、semaphore wake 和双向调度检查均为 `PASSED`。 |
| RPTUN/RPMsg | PASS | RPTUN `CONNECTED`；20 次、64-byte 双 CPU 测试均为 `20/20`、零错误。 |
| RPMsgFS | PASS | 两轮 64-byte 写读 checksum 一致；第二轮 CP/AP heap 完全稳定。 |
| PSRAM | PASS | ID `0x8d08`、配置 `0x8d1a`、容量 `16777216` bytes，启动自检 `BPSR BOOT PASS`。 |
| Flash MTD/LittleFS | PASS | `/data` 写入标记，RTS 复位后原值读回，再删除测试文件；真正断电 cold boot 后 `/data/probe.txt` 仍存在。 |
| P9 板载 LED | PASS | `/dev/gpio0` 从 0 写 1 并读回 1，维护者现场确认 LED 点亮；测试结束后重新写 0 并读回 0。 |
| P29 USER 按键 | PASS | 未按下时 `/dev/gpio1` 读 1；维护者按住按键后实测读 0，符合 10 kΩ 上拉、按下接地的低有效设计。 |
| Wi-Fi | PASS（稳定性观察） | 正确凭据连接、DHCP、原生 wlan0 网关 Ping 通过；错误凭据按预期超时，随后无需复位即可用正确凭据恢复并再次 Ping。错误尝试后 J-Link 确认三个 AP 命令缓冲区均恢复为 `0xF3EEF3EE`。最终 18.1.10 镜像联网后，RPMsg 六组合和 BLE 状态再次通过。曾有一次恢复连接瞬态超时，紧接着重试及第二轮完整矩阵均通过。凭据未写入本文或仓库。 |
| Bluetooth HCI | PASS | 真实 BDADDR、ACL MTU 70、buffer 20；3 秒扫描发现 7 个空口结果。未使用 BlueDebug。 |
| BLE GATT 板内状态 | PASS | `bkbttest stats` 返回 N13 `state=2`（ADVERTISING）、`last_error=0`、`worker_cpu=0`、`queue_full=0`。 |
| 运行态时钟诊断 | PASS | 官方 SDK 最终配置为 DPLL/8，即 60 MHz；`apctl` 报告 `AP clock=60000000`，SysTick reload 为 `0x927bf`（599999），与 10 ms tick 相符。J-Link 只读确认时钟寄存器 M1=`0x00100037`。 |
| BLE 外部中心设备 | INCONCLUSIVE | Windows 原生 BLE watcher 15 秒收到 0 个事件，连其他设备也未收到，故只能判定电脑侧扫描环境未闭环，不能据此判板端失败。 |
| watchdog | PARTIAL | `/dev/watchdog0` 注册且自动喂狗配置运行稳定；未故意停止喂狗触发复位。 |
| RTS/RESET warm reset | PASS | 多次自动复位均重新经过 BL1/BL2 并回到 NSH。该结果不等同于完整断电 cold boot。 |
| 真正断电 cold boot | PASS | 维护者同时断开 USB 与 J-Link 3.3V，等待后重新上电；COM7/COM11/COM12 重新枚举，AP、CPU2、SMP、RPTUN、BLE、RPMsg 和 LittleFS 均通过复查。 |
| RTC/Timer/PWM/ADC | NOT CLOSED | 当前轮次未形成独立的板端数值/波形证据。 |
| 单 MIC 音频输入 | NOT CLOSED | Core 板音频范围按单 MIC 采集验证，需要低幅度声源、采样数据和人工声学确认。 |
| 电池 ADC、充电检测 | NOT CLOSED | 需要明确电池/USB 供电组合及万用表参考值。 |
| 原生 USB Host/Device | NOT PRESENT | Core 板没有引出 BK7258 原生 USB，不属于该板验证范围；USB 留到完整 T5-Board 验证。 |
| I2C/SPI/I2S/SDIO/QSPI/CAN/Ethernet/DVP/LCD | FIXTURE REQUIRED | Core 板没有对应板载从设备、收发器或专用连接器，后续按排针和完整 T5-Board 分别验证。 |

## 4. 关键运行证据

### 4.1 双核与 SMP

```text
AP state=READY(2) error=0
RPTUN state=CONNECTED(4) error=0
AP supervisor state=HEALTHY(2)
CPU2 state=SCHEDULER_ONLINE(8)
AP SMP state=PASSED(4) online=00000003
AP affinity state=PASSED(4)
AP sem-wake state=PASSED(6)
AP sem-loop state=PASSED(7)
```

### 4.2 RPMsg 与 RPMsgFS

```text
BRPT CPU slot=0 sent=20 received=20 errors=0
BRPT CPU slot=1 sent=20 received=20 errors=0
BRPT PASS gen=1 run=1 count=20 payload=64

BRFS RESULT iterations=1/1 written=64 read=64 checksum=ba524945/ba524945
BRFS AP_HEAP before_used=102680 after_used=102680
BRFS CP_HEAP before_used=73340 after_used=73340
BRFS PASS gen=1 sequence=2
```

### 4.3 BLE

```text
BBTT RESULT operation=stats status=0 worker_cpu=0
BBTT HCI command_tx=19 event_rx=19 invalid_rx=0 receive_errors=0
BBTT N13 state=2 last_error=0 worker_cpu=0
BBTT PASS operation=stats
```

### 4.4 Wi-Fi 控制面修复与恢复矩阵

```text
DHCP_ACK received
BKWIFI RESULT operation=connect status=0 link=3 rssi=-36
ip=192.168.0.102 mask=255.255.255.0 router=192.168.0.1

BKWIFI RESULT operation=connect status=-110 link=2 rssi=0
ip=0.0.0.0 mask=0.0.0.0 router=0.0.0.0

BKWIFI RESULT operation=connect status=0 link=3 rssi=-40
ip=192.168.0.102 mask=255.255.255.0 router=192.168.0.1
BKWIFI RESULT operation=ping status=0 link=3 rssi=-40
```

原始失败由两个板级适配缺口叠加触发：AP 没有执行官方启动序列中的 `bk_event_init()`；更关键的是 RTOS wrapper 用 `nxsched_getpid()` 判断当前 SDK 线程。当前 NuttX 中该接口返回进程组 ID，而 SDK 保存的是 kthread 的线程 ID。WPA 线程因此误判自己为其他线程，在处理 `STA_STOP` 时向自己的队列发出同步请求并永久等待，继而阻塞 CIF worker，最终令 AP 的三个固定命令缓冲区全部保持 `0xCAFEBABE`。

板级 wrapper 改用 `nxsched_gettid()` 后，错误密码仍按业务语义返回 `-ETIMEDOUT`，但三个命令缓冲区都恢复为 `0xF3EEF3EE`，后续连接无需复位。mailbox 双侧计数在故障定位期间始终对齐，证明根因不是物理 mailbox 丢包，也不是 cache 可见性问题。

### 4.5 运行态时钟与 DVFS 边界

```text
AP clock=60000000 SysTick ctrl/load/current=00000007/000927bf/0005061b
AP state=READY(2) error=0
RPTUN state=CONNECTED(4) error=0
AP supervisor state=HEALTHY(2)
```

早期诊断把 SDK 的 DPLL/8 配置误落入“未知时钟”分支，并回退显示为 26 MHz；实际硬件寄存器 M1 一直为 `0x00100037`，对应官方 v3.1.1.9 的 60 MHz 档位。板级诊断现已补齐 DPLL 60/80 MHz 两种官方档位，AP SysTick 随之按真实频率配置。

曾临时验证“仅由 CP 投票到 120 MHz”的路径。该实验可稳定触发 AP IRQ 上下文中的 imprecise bus fault，并伴随 Bluetooth mailbox/semaphore 失败，说明 CP 与 AP 的 SDK PM 状态和切频时序必须成套协调。该单边投票代码已完全撤回，未进入最终实现；当前保留 SDK 启动后的 60 MHz 稳态。后续动态调频必须复刻 v3.1.1.9 的双核协调流程，不能再次写死频率或只修改一侧。

### 4.6 真正断电 cold boot

维护者同时断开 Core 板 USB 与 J-Link 3.3V 供电，等待后重新连接。Windows 重新枚举出 COM7、COM11、COM12；未再次烧录或触发软件复位。上电后的状态检查同时满足：

```text
AP state=READY
RPTUN state=CONNECTED
AP supervisor state=HEALTHY
CPU2 state=SCHEDULER_ONLINE
AP SMP state=PASSED
AP clock=60000000
BRPT SUITE PASS runs=6 count=3
BBTT N13 state=2 last_error=0
```

此外 `/data/probe.txt` 在 cold boot 后仍可见，证明本次全断电没有破坏 LittleFS 持久数据。

## 5. 本轮发现的问题

1. **Wi-Fi archive 残余诊断**：连接和停止期间仍可见一次 `wdrv_tx_msg: cmd confirm timeout`，但命令在 CP 已执行、mailbox 计数对齐、命令缓冲区最终释放。另有一次正确凭据恢复瞬态超时，紧接重试和第二轮完整矩阵均通过；后续耐久轮次继续统计，不把该日志直接静默掉。
2. **动态调频尚未闭环**：原先的 26 MHz 是诊断误判，真实稳态为官方 60 MHz。CP 单边切到 120 MHz 已证明不安全，后续需要实现 CP/AP 一致的投票、切频通知和 SysTick 更新时间；不要重新写死 320 MHz，也不要只修改单核。
3. **聚合 drivercheck 存在资源争用**：聚合配置会占用 P9/P29 及部分 SPI/DMA GPIO，不能用其结果否定单个 lower-half。GPIO 已用无争用专用配置复测通过软件读写。
4. **启动警告仍需归类**：BLE 配置启动时出现一次 `[ipc_svr] create_socket failed.`，但 AP READY、HCI 和 GATT 后续均通过。应确认它是重复服务注册还是可忽略的 SDK 诊断，不能直接静默删除。
5. **J-Link 型号匹配警告**：J-Link 以 `CORTEX-M33` 连接后实际识别为 STAR r1p0；老固件还提示 cache 调试支持有限。只读访问可靠，但后续断点/cache 调试要采用 STAR 对应配置并重新验证。

## 6. 下一步顺序

1. 参考 v3.1.1.9 设计 CP/AP 协调的动态调频适配，并在每个频点重跑 RPMsg、Bluetooth 和 Wi-Fi 回归；当前 60 MHz 稳态不再作为故障处理。
2. 在后续正常 Wi-Fi 使用中累计重连轮次，观察 archive 的确认超时日志和偶发恢复超时，不再为此提前增加 campaign 脚本。
3. 设计 Core 板单 MIC、电池 ADC 和充电检测的低风险人工测试步骤。
4. 切换完整 T5-Board，验证原生 USB 与其余板载总线外设。
