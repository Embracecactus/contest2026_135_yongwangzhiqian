# N12 — Beken Bluetooth IPC 的 NuttX HCI Wrapper

> 状态：N12 批准的冷启动范围已完整 `board-verified`。Controller/Host 握手、HCI info、
> MAC 持久化、UART 生命周期恢复、RPTUN/RPMsg/RPMsgFS 共存和真实 BLE advertising
> report 均已实板闭环。
>
> 适用 SDK：`v3.1.1.9`，来源为官方 `bk_avdk_smp-release-v3.1.1.9`。
>
> 约束：不修改 NuttX 源码，不修改 Beken SDK 源码；实现只位于 BK7258 board overlay。

## 1. 架构决策

N12 使用官方 SDK 已有的 CP/AP Bluetooth mailbox IPC，并在 AP 侧实现 NuttX
`bt_driver_s` lower-half：

```text
AP NuttX Bluetooth Host
        │ bt_driver_s: command / ACL
        ▼
board/bk7258/chip/ap/bk7258_bt_hci.c
        │ bt_ipc_hci_send_cmd() / bt_ipc_hci_send_acl_data()
        ▼
official MB_CHNL_BT_CMD mailbox IPC
        │ pointer ownership + source-core free acknowledgement
        ▼
CP Beken BLE Controller
```

反向数据流为：

```text
CP controller HCI event / ACL
  → official CP bt_ipc
  → MB_CHNL_BT_CMD
  → AP mailbox ISR
  → official SDK queue
  → bt_ipc_thd
  → board callback
  → bt_netdev_receive()
  → stock NuttX Bluetooth Host HP/LP workers
```

该方案与官方 SDK 的 CPU 分工一致：CP 保留射频和 BLE Controller，AP 运行 Host。
board wrapper 只转换 HCI framing 和 NuttX driver contract，不重新实现 mailbox
协议、控制器或 Host。

## 2. 与 RPTUN、RPMsgHCI 和 SMP 的关系

N12 不替换既有 AMP/RPTUN，也不改变 AP SMP：

- AP 仍是双 logical CPU 的 NuttX SMP image。
- CP 与 AP 的通用服务继续走 RPTUN/RPMsg，包括 syslog、测试 endpoint 和 RPMsgFS。
- Bluetooth HCI 使用 SDK 的 `MB_CHNL_BT_CMD`，其 logical-channel index 为 3。
- RPTUN notify 使用 board 已保留的 `MB_CHNL_LOG`，其 logical-channel index 为 14。
- 两者共用 SDK physical mailbox dispatcher，但 channel、callback 和协议状态互相独立。

stock RPMsgHCI 在本架构中不是数据面的必需组件。前期 RPMsgHCI 工作仍有价值：它验证了
NuttX Host/Controller 分核边界、RPTUN/RPMsg transport 和 `bt_driver_s` 接入方向；N12
最终选择官方 Bluetooth IPC，是因为官方 Controller archive 已经实现并依赖其专有的
指针所有权与源核释放协议。将其再封装进 RPMsgHCI 会形成没有收益的双重 transport。

## 3. 已核实的官方 SDK 行为

核实来源：

- AP：`ap/components/bk_bluetooth/ipc/src/bt_ipc_core.c`
- CP：`cp/components/bk_bluetooth/ipc/src/bt_ipc_core.c`
- ABI：两侧 `components/bk_bluetooth/ipc/include/bt_ipc_core.h`
- channel：两侧 `include/driver/mailbox_channel.h`

关键事实如下：

1. AP 的 `bt_ipc_init()` 返回 `void`；CP 版本返回 `0`（新建）、`1`（已初始化）或负值。
2. mailbox ISR 只把指针放入长度为 64 的 SDK queue；HCI callback 在 `bt_ipc_thd`
   线程中执行，不在 ISR 中执行。
3. 发送函数先通过 `os_malloc()` 分配本核 buffer 并复制调用方数据，因此 NuttX 的发送
   buffer 在 SDK 调用返回后可以释放。
4. 接收 callback 返回后 SDK 会释放临时 H4 buffer；`bt_netdev_receive()` 在返回前同步
   复制完整 HCI packet，之后才交给 NuttX HP/LP worker。
5. 跨核 packet 的原始分配由源核释放。接收核通过 `HCI_FREE_PKT` 把地址返回给源核，
   不能由 board wrapper 提前释放或改写。
6. Controller init/deinit 通过 vendor opcode `0xfefe` 和 subopcode `0x0001/0x0002`
   完成。AP SDK 对成功响应调用其拼写固定的 ABI
   `bk_bluetooth_init_deinit_compelete()`。
7. 官方 HCI send API 返回 `void`。本地成功只表示请求已交给 SDK；端到端结果必须由
   HCI command completion 或上层 timeout 判定。

## 4. Wrapper contract

### 4.1 AP HCI lower-half

实现文件：`board/bk7258/chip/ap/bk7258_bt_hci.c`。

| NuttX 输入 | SDK 调用 | 校验 |
|---|---|---|
| `BT_CMD` | `bt_ipc_hci_send_cmd(opcode, payload, len)` | 3-byte HCI command header 和 parameter length 必须完全匹配 |
| `BT_ACL_OUT` | `bt_ipc_hci_send_acl_data(handle_flags, payload, len)` | 4-byte ACL header 和 data length 必须完全匹配 |
| `BT_ISO_OUT` | 不支持 | 返回 `-EOPNOTSUPP` |

SDK callback 的 H4 event (`0x04`) 转成 `BT_EVT`，H4 ACL (`0x02`) 转成
`BT_ACL_IN`。wrapper 会先验证 HCI header 中声明的长度与 callback 总长度完全一致，
再去掉 H4 type byte 投递给 NuttX。SCO 和未知 type 第一版直接丢弃。

`open()` 注册 callback、初始化 AP Bluetooth IPC、发送 Controller init，并在 5 秒
deadline 内等待官方成功事件。`close()` 先阻止新 HCI 数据进入 Host，再发 Controller
deinit，最后注销 callback。

### 4.2 CP bootstrap 和系统服务

实现文件：`board/bk7258/chip/cp/bk7258_bt_controller.c`。

CP 在释放 AP 前初始化官方 `bt_ipc` worker。Controller 本体不在 CP boot 时立即启动，
而是在收到 AP vendor-init 后由官方 IPC object 调用 `bk_bluetooth_init()`。

官方 Controller 需要 `bk_get_mac()`。不能直接链接 `libbk_system.a`，因为该 archive 同时
带入 FreeRTOS tick、printf、delay 和 system runtime，会与 NuttX board adapter 发生
多重定义。board 因此用 SDK 导出的 flash/TRNG leaf 复现 Controller 所需的官方
`CONFIG_NEW_MAC_POLICY + CONFIG_RANDOM_MAC_ADDR + CONFIG_BK_MAC_ADDR_CHECK` 编排：

- 先调用幂等的 chip `bk7258_flash_initialize()`（它封装官方 driver init、JEDEC
  校验和物理 I/O 串行），再读取 `sys_net` 分区 offset 0 的 6-byte base MAC；有效
  地址还必须匹配 Beken OUI `c8:47:8c`；
- 在 `sys_rf + 0xe00` 的最后 512 bytes 中读取 append-only 10-byte record：
  `{magic=0x4d41, data_crc, header_crc, mac[6]}`；CRC8 polynomial 为 `0x31`；
- 数字 partition ID 只在运行时确认其 start/size 与 v3.1.1.9 官方表完全一致后才使用，
  SDK bundle 缺失的私有 `partitions_gen.h` 不会复制进 board；
- `sys_net` 有效时以它为准，并在备份缺失/不一致时追加记录；只有备份有效时用备份恢复
  `sys_net`；两者都为空时初始化官方 TRNG，用 Beken OUI + 三个随机字节生成 base MAC，
  再同步写入两处分区；
- `sys_net` 通过 chip `bk7258_flash_partition_update()` 封装官方整-sector
  erase/rewrite，备份通过生成分区 wrapper 进入同一个 chip raw-Flash service；board
  不直接调用物理 Flash SDK，也不自行实现寄存器或擦写算法；
- 拒绝全零、全 `0xff`、multicast 和非 Beken OUI 地址；flash/TRNG 失败时才使用 SDK
  默认地址 `c8:47:8c:00:00:18`，持久化不完整会明确输出 warning；
- STA 使用 base，AP 使用 SDK virtual-interface mask 规则，Bluetooth 为 base + 1，
  Ethernet 为 base + 3。

对当前 bundle 的 `libcom_phy.a` 反汇编确认：
`manual_cal_get_macaddr_from_flash()` 只是 `return 0` 的空桩，不会填写 buffer。因此 N12
不再把它当作持久化来源，也不能把“链接成功”误判为“读到了工厂 MAC”。实际编排逐项对照
官方 CP `components/bk_system/mac.c`、`net_param.c`、`trng_driver.c`、
`flash_partition.c` 和 `flash_driver_ext.c`，但这些 SDK 源文件均保持只读。

当前板初始 `sys_rf/sys_net` 均为 erased。首次启动生成 base MAC
`c8:47:8c:47:47:47`，Controller BD_ADDR 为 `c8:47:8c:47:47:48`。BKFIL 回读确认：

- `sys_rf + 0xe00`：`41 4d a5 74 c8 47 8c 47 47 47`；独立重算
  data/header CRC 分别为 `0xa5/0x74`；
- `sys_net + 0x0`：`c8 47 8c 47 47 47`；
- 8 KiB dump SHA-256：
  `f3dcd8b5cd700bb8900057fa775df29d41d29ded4a5209b735ac628a0e17013e`；
- loader reboot 后再次读取的 BD_ADDR 完全一致且 `fallback=0`。

在两轮额外 physical reset 后再次只读回读，目标 record 仍是同一 10 bytes，下一条
10-byte slot 保持全 `0xff`，`sys_net` 仍是同一 6-byte base MAC，证明多次启动不会重复
追加。第二份 8 KiB dump SHA-256 为
`941074635fd67701ebe4650d958246e666d2c9b31937a48a0de7e3e0b0e45bd2`；这里的幂等判据
是上述 MAC record/sys_net 字段，不要求 sys_rf 中其他 calibration bytes 的整文件哈希不变。

这些写入只发生在官方定义的数据分区，不改 SDK/NuttX 源码，也不改 boot/CP/AP image
分区。

### 4.3 SDK 生命周期后的 UART1 所有权

PHY/RF/calibration 和 Controller lifecycle 会在其公共调用返回前后重置共享 UART block，
但 SDK UART 软件状态仍可能报告“已初始化”。本板 console 固定为 UART1/GPIO0/1，因而在
board wrapper 中建立统一硬件不变量：

- global control `0x45830008 = 1`；
- UART config `0x45830010 = 0x371b`（26 MHz、460800 8N1）；
- FIFO RX threshold `0x45830014[15:8] = 1`。

Controller init/deinit wrapper 返回后会恢复硬件、RX callback 和 RX interrupt；polled
`arm_lowputc()` 与完整 NuttX serial TX 在最终使用点再次检查上述不变量。由于 UART block
没有 FIFO reset bit，恢复时还会在中断保持 masked 的条件下有界读取 RX data port，丢弃
复位前留下的不可信 partial byte。修复后，loader reboot 后未经任何预热的第一条
`bkbttest info` 已完整解析，原先可稳定复现的 `�bkbttest`/`�apctl` 前缀不再出现。

fallback 地址不是批量生产时的唯一地址保证。硬件验收必须打印或读取实际 BD_ADDR，
确认目标板 RF 数据分区包含有效 MAC；产品化阶段若存在多板并行测试，还需要把 fallback
升级为基于芯片唯一 ID 的稳定本地地址或完善 factory provisioning。

## 5. 板级诊断控制面

`CONFIG_BK7258_BT_IPC_TEST` 增加独立的 `bk7258-bt-test` RPMsg endpoint 和 CP NSH
命令 `bkbttest`。这条链路只用于有界验收，不承载 Bluetooth HCI 数据：

- RPMsg callback 只校验/copy generation-tagged request 并唤醒 semaphore；
- AP 的 `bk-bt-test` 永久线程固定在 logical CPU0，优先级低于 RPTUN RX 和 AP
  supervisor；
- 工作线程通过 stock NuttX `SIOCGBTINFO`、`SIOCGBTFEAT`、`SIOCGBTLEFEAT` 和
  `SIOCBTSCAN*` ioctl 访问 `bnep0`；
- response 固定为 128-byte ABI，包含 BD_ADDR、fallback 标志、BR/LE features、ACL
  参数、选中的 advertising report、原始结果索引和 N12-V payload 匹配标志；
- CP 单请求串行化，并在等待期间持续检查 endpoint 和 RPTUN generation，断链返回
  `-ENOTCONN`/`-ESTALE`，不会永久阻塞。

命令：

```text
bkbttest info [timeout_ms=10000]
bkbttest scan [duration_ms=3000] [timeout_ms=10000]
bkbttest all  [duration_ms=3000] [timeout_ms=10000]
bkbttest n12v [duration_ms=3000] [timeout_ms=10000]
```

固定 fallback、无效 BD_ADDR、非 CPU0 worker、零 scan result 或 ioctl/transport timeout
都会输出 `BBTT FAIL`。`all` 即使 info gate 失败仍继续 scan，便于一次日志同时区分
provisioning 问题和 RF/HCI 数据面问题。`n12v` 会遍历至多 8 条 report，要求 manufacturer
AD 精确包含 company ID `0xfffe` 和 payload `4e31325601020304`；这样即使 Windows 同一
适配器的系统广播先返回，也不会误选 `responses[0]`。

## 6. 链接库选择

AP 额外链接：

- `libbk_bluetooth.a`：官方 AP Bluetooth IPC object。

CP 额外链接：

- `libbluetooth_controller_controller_only_ble.a`：与官方 CP controller-only 配置一致；
- `libcom_phy.a`：完整 PHY、RF 和 calibration 实现；
- `libwifi.a`、`libbk_wifi.a`、`liblwip_intf_v2_1.a`：仅闭合该 SDK PHY archive 编译时
  固化的 Wi-Fi adapter 依赖；N12 不调用 Wi-Fi/LwIP 顶层初始化。

明确排除：

- generic `libbluetooth_controller.a`，避免错误 controller variant 和重复符号；
- `libbk_system.a`，避免 FreeRTOS runtime 污染 NuttX image；
- stock RPMsgHCI transport，避免在官方 Bluetooth IPC 外再叠一层 transport。

所有 SDK library 来自 board 内已校验 SHA-256 的
`bk_idk/armino_as_lib/versions/v3.1.1.9/{cp,ap}` bundle。SDK library 本身未被修改。

## 7. 启动顺序和失败语义

### 7.1 CP

1. 初始化 AP control 和 physical/logical mailbox 基础设施；
2. 初始化 CP official Bluetooth IPC worker；
3. 初始化 AP supervisor；
4. 释放 AP 并等待有界 READY。

如果 Bluetooth IPC 初始化失败，CP 不释放 AP，避免 AP Host 在没有 Controller transport
的情况下进入不可恢复等待。

### 7.2 AP

1. 完成 N8 SMP gates 和 AP health worker；
2. 初始化 RPTUN mailbox、RPTUN 和 RPMsgFS；
3. 注册 NuttX HCI wrapper，并完成 Controller init + NuttX HCI initialization；
4. 只有以上步骤全部成功后才发布 AP READY。

这样 AP READY 同时证明 SMP、通用 RPMsg service 和 Bluetooth HCI 基础握手都已成功。

### 7.3 Fail-closed

- malformed HCI packet 返回 `-EMSGSIZE`，不会进入 SDK；
- Host 未 open 时发送返回 `-ENETDOWN`；
- Controller init 超时会让 AP 发布 `BK7258_AP_ERROR_BLUETOOTH`，不会发布 READY；
- SDK send API 无法报告 allocation、semaphore 或 mailbox write 失败，NuttX 上层的 HCI
  command timeout 是最终失败判据。这是官方 binary ABI 的限制，不在 wrapper 中伪造
  “可靠发送成功”。

## 8. Warm restart 边界

当前 N12 只批准冷启动 gate，不批准 AP-only warm restart 下的 Bluetooth 连续性。

原因是官方 IPC 传递的是两核 heap pointer：AP reset 时可能仍有 AP-owned packet 等待 CP
消费或返回；CP Controller 和 CP `bt_ipc` worker 又可能保持旧 generation。直接重启 AP
会产生 stale pointer、重复 Controller init 或错误释放新 heap 地址的风险。官方 CP
vendor-init handler 还会忽略 `bk_bluetooth_init()` 的返回值并固定发送成功 status，不能
用该 status 证明重复初始化安全。

在把 Bluetooth profile 纳入 AP warm-restart 验收前，必须增加完整 quiesce gate：

1. 停止新 Host command/ACL；
2. 关闭 NuttX Bluetooth Host，发送并确认 Controller deinit；
3. 等待双方 pending packet 和 free acknowledgement 清空；
4. 注销 AP callback/channel ownership；
5. 之后才允许 CP reset/release AP；
6. 新 generation 重新初始化 IPC 和 Controller，并重新注册 Host。

任一步骤超时都应拒绝 warm restart，转为整芯片冷复位，不能带着不确定 pointer state
继续运行。

## 9. 构建与静态验收

构建命令：

```sh
CP_CONFIG_NAME=cp_nsh_btipc \
AP_CONFIG_NAME=ap_smp_btipc \
JOBS=8 \
./board/bk7258/scripts/build_dual_image.sh
```

2026-08-02 最终实板镜像的本机构建结果：

| 产物 | 大小 | 结果 |
|---|---:|---|
| CP `app.bin` | 666,180 bytes | SHA-256 `e5e5225d...0d9822a`；CP wrapper、BLE-only Controller、PHY closure、`bkbttest n12v` 已链接 |
| CP `app_crc.bin` | 707,846 bytes | SHA-256 `e3b6aa47...d6de57` |
| AP `app1.bin` | 165,500 bytes | SHA-256 `b8bd51a1...b22c8a`；HCI wrapper、BT test worker、NuttX Host、report filter 已链接 |
| AP `app1_crc.bin` | 175,848 bytes | SHA-256 `20ad02e9...11f4e` |
| RPTUN layout | carveout `0x7e80`，spare `0x4cc0` | `elf-verified PASS` |

额外静态检查：

- CP/AP ELF 均无 unresolved symbol；CP ELF 导出 `bkbttest_main()`、
  `bk_rand()`、`bk_trng_driver_init()`、chip raw-Flash service 和
  `bk7258_bt_test_run()`，AP ELF 导出 `btnet_ioctl()`、`bt_start_scanning()` 和
  `bk7258_bt_test_initialize()`；
- CP map 中没有 `libbk_system.a`；
- AP map 中没有 stock `rpmsghci`；
- 从 CP ELF 重新 `objcopy` 的 binary 与打包目录 `app.bin` 完全一致；
- 原有 `cp_nsh_rptun + ap_smp_rptun` dual-image profile 完整回归构建通过，layout
  同样为 `elf-verified PASS`；
- `git diff --check` 和 `bash -n build_dual_image.sh` 通过。

## 10. 硬件验收结果

最终精确源码重建镜像使用 `cp_nsh_btipc + ap_smp_btipc` sparse flash，LittleFS 保留；
确定性 N12-V 版本的自动调试日志为
`$WORKSPACE/logs/bk7258-auto-debug/20260802-184909/`。此前完整验证镜像的
最终 patch 日志为 `20260802-173018/`；此前完整验证镜像的
首次 sparse-flash 日志为 `20260802-170841/`；两轮独立 RTS physical reset 日志分别为
`20260802-171920/` 和 `20260802-172018/`。

| Gate | 实板结果 |
|---|---|
| cold boot / NSH | `serial_bytes=393`、`verdict=PASS_NSH`，越过此前固定的 290-byte UART failure point |
| 冷启动第一条命令 | 未预热直接执行 `bkbttest info 10000`，完整解析并 `BBTT SUITE PASS` |
| 独立 physical reset 复验 | COM7 RTS reset 2/2：每轮 `cold_path=yes`、`serial_bytes=424`、`PASS_NSH`；每轮未经预热的首条 `bkbttest info` 均 PASS、无垃圾前缀 |
| BKFIL read/reboot 复验 | 第二次只读 partition dump 后，首条 `bkbttest info` 仍无乱码 PASS；MAC record 无重复追加 |
| AP/SMP/RPTUN | AP `READY`、generation 1、RPTUN `CONNECTED`、supervisor `HEALTHY`、CPU2 scheduler online，N8 gates 全 PASS |
| HCI info | `fallback=0`、ACL MTU/buffers=`70/20`，BR/LE feature ioctl 成功 |
| lifecycle 后 console | `help` 返回 926 bytes，NSH 继续可交互 |
| RPMsg 共存 | `bkrpmsgtest all 100 60000` 全 6 场景 PASS；CPU0/CPU1 各 100/100，AP heap 六轮完全稳定 |
| RPMsgFS 共存 | `bkrpmsgfstest all 2 30000` 四档 payload 全 PASS；CP 首次服务固定增长 224 bytes 后稳定 |
| 最终精确镜像回归 | 未预热首条 `bkbttest info 10000` PASS；`bkrpmsgtest all 20 30000` 6/6 PASS；`bkrpmsgfstest all 1 30000` 4/4 PASS |
| BLE scan / N12-V | `bkbttest n12v 10000 15000`：Windows legacy manufacturer-data advertiser 实射频验证，`results=2 selected=1 n12v_match=1`，address=`01:43:94:1f:ea:e4`、address type=`1`、RSSI=`-49`、adv type=`3`、完整 AD=`0b ff fe ff 4e 31 32 56 01 02 03 04`；info/scan/suite 全 PASS |
| N12-V 后 IPC 回归 | `bkrpmsgtest all 5 30000` 6/6 PASS，CPU0/CPU1 各场景均 5/5、AP heap 稳定；`bkrpmsgfstest all 1 30000` 4/4 PASS |

最终 RPMsg 与 RPMsgFS 串口 capture 分别为 `/tmp/n12-final-rpmsg-smoke.bin`
（7,900 bytes，SHA-256 `4bc5c1a3...1ec2b8`）和
`/tmp/n12-final-rpmsgfs-smoke.bin`（2,108 bytes，SHA-256
`6507534e...51443`）。RPMsgFS 的 AP heap 四轮完全稳定；CP 仅首轮创建服务时增加
224 bytes，后三轮保持稳定。

最初在没有 advertiser 的现场执行 scan 时有界返回 `-ENODATA`，该结果只证明了无超时，
不能证明 RF 接收。随后使用仓内
[Windows/WSL2 BLE test advertiser](../../../tools/windows-hardware-debug/ble-advertiser/README.md)
通过 Windows Qualcomm FastConnect 7800 发送 legacy manufacturer data：company ID
`0xfffe`，payload `4e31325601020304`。Windows publisher 状态完整经历
`Created → Waiting → Started → Stopped`；BK7258 在同一窗口运行：

核心命令序列：

```text
bkbttest n12v 10000 15000
```

得到 `BBTT SUITE PASS info=1 scan=1`，并实际接收 2 个 report。Windows 系统广播占据
index 0，板端 filter 正确选中 index 1 并给出 `n12v_match=1`。选中 report 的完整 AD
structure 为 `0b`（长度）、`ff`（manufacturer data）、`fe ff`（company ID，小端）和
唯一 payload `4e31325601020304`，与发送端逐字节一致，因此不是背景设备或合成 HCI
event。地址由 Windows 隐私机制随机化，不作为固定身份判据。N12-V 的真实
address/RSSI/payload gate 至此以确定性命令标为 `board-verified`。

原始证据保存在
`$WORKSPACE/logs/bk7258-n12v/20260802-deterministic-filter/`：

- `serial.raw`：881 bytes，SHA-256 `27808afa...4d96e`；
- `advertiser.log`：292 bytes，SHA-256 `4d52af8b...d9f0c`；
- `advertiser-ready.json`：123 bytes，SHA-256 `bccd20a5...0ee3bd9`；
- `post-rf-rpmsg.raw`：7,838 bytes，SHA-256 `4ad2ec03...2791`；
- `post-rf-rpmsgfs.raw`：2,108 bytes，SHA-256 `6507534e...51443`。

AP-only warm restart 必须等第 8 节的 quiesce/reconnect 设计落地后单独验收，不能把冷启动
PASS 外推为 warm-restart PASS。
