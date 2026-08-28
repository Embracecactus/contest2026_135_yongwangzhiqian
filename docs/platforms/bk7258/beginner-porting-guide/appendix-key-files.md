> **事实截止日期**：2026-08-04
> **权威来源**：当前team仓库文件树、[上层README](../README.md)、[移植报告](../porting-report.md)、[memory index](../../../../memory/INDEX.md)
> **证据边界**：本附录是“从问题找到文件”的导航，不替代源码、ADR、verification或第11章动态状态。

# 附录：关键文件与阅读索引

## 1. 从目录看职责

| 路径 | 先在什么问题时打开 |
|---|---|
| `chips/bk7258/bootloader/` | BootROM handoff、vector/magic、cache/MPU/WDT、BL1/BL2 |
| `chips/bk7258/common/` | CP/AP共用UART、IRQ、heap、RPTUN、RPMsg、RPMsgFS、PSRAM |
| `chips/bk7258/cp/` | Flash/LittleFS、AP control/supervisor、Controller |
| `chips/bk7258/ap/` | AP startup/SMP/IPI、HCI Host lower-half、GATT |
| `boards/bk7258/<board>/configs/` | 某个CP/AP profile到底打开哪些Kconfig |
| `boards/bk7258/common/partitions/` | canonical CSV与生成布局 |
| `tools/bk7258/` | build、pack、import、verify、loader helper |
| `boards/bk7258/common/src/` | `board_app_initialize()`、procfs/LittleFS/AP/Bluetooth/PSRAM bring-up顺序 |
| `chips/bk7258/bk_idk/` | SDK role bundle入口和本地准备说明 |
| `app/hello_app/` | NSH builtin测试命令 |
| `docs/platforms/bk7258/` | 总报告、阶段worklog、SOP和本教程 |
| `memory/` | durable规则、架构与ADR |
| `progress/` | CURRENT、ROADMAP、milestone、verification |
| `logs/` | 原始构建/板测证据 |
| `tools/windows-hardware-debug/` | 通用Windows/WSL2 UART/J-Link capture工具 |

## 2. Boot与打包

| 文件 | 作用 |
|---|---|
| [start.S](../../../../chips/bk7258/bootloader/start.S) | vector、magic、reset与handoff汇编 |
| [boot_main.c](../../../../chips/bk7258/bootloader/boot_main.c) | 分区与CP app校验 |
| [boot_runtime.c](../../../../chips/bk7258/bootloader/boot_runtime.c) | reset/cache/MPU/core状态规范化 |
| [boot_wdt.h](../../../../chips/bk7258/bootloader/boot_wdt.h) | Boot watchdog init/feed contract |
| [bootloader.ld](../../../../chips/bk7258/bootloader/bootloader.ld) | Boot logical Flash/RAM布局 |
| [CRC 实现](../../../../tools/bk7258/_lib/image.py) | 32+2 physical image |
| [逆向综合](../bootloader-analysis/full-reverse-synthesis.md) | vendor binary与team契约边界 |

## 3. CP启动、外设与存储

| 文件 | 作用 |
|---|---|
| [bk7258_start.c](../../../../chips/bk7258/cp/bk7258_start.c) | CP `__start` |
| [bk7258_vectors.c](../../../../chips/bk7258/cp/bk7258_vectors.c) | CP vector/RAM-vector处理 |
| [bk7258_serial.c](../../../../chips/bk7258/common/bk7258_serial.c) | UART lower-half与RX ISR |
| [bk7258_sdk_irq.c](../../../../chips/bk7258/common/bk7258_sdk_irq.c) | SDK interrupt→NuttX IRQ bridge |
| [bk7258_flash_mtd.c](../../../../boards/bk7258/common/src/bk7258_flash_mtd.c) | board-owned partition/MTD composition |
| [bk7258_flash_mtd.c](../../../../boards/bk7258/common/src/bk7258_flash_mtd.c) | CP-only Flash serialization/permission policy |
| [bk7258_bringup.c](../../../../boards/bk7258/common/src/bk7258_bringup.c) | procfs、LittleFS与service bring-up |

## 4. AP与SMP

| 文件 | 作用 |
|---|---|
| [bk7258_ap_control.c](../../../../chips/bk7258/cp/bk7258_ap_control.c) | CP侧AP power/reset/restart |
| [bk7258_ap_start.c](../../../../chips/bk7258/ap/bk7258_ap_start.c) | AP primary startup |
| [bk7258_ap_smp.c](../../../../chips/bk7258/ap/bk7258_ap_smp.c) | secondary bootstrap与SMP基础 |
| [bk7258_ap_ipi.c](../../../../chips/bk7258/ap/bk7258_ap_ipi.c) | AP双向IPI |
| [bk7258_ap_smp_advanced.c](../../../../chips/bk7258/ap/bk7258_ap_smp_advanced.c) | affinity/wake/migration/timer/lifecycle gates |
| [bk7258_amp.h](../../../../chips/bk7258/include/bk7258_amp.h) | shared AP lifecycle/telemetry ABI |

## 5. RPTUN、监督与RPMsgFS

| 文件 | 作用 |
|---|---|
| [bk7258_rptun.c](../../../../chips/bk7258/common/bk7258_rptun.c) | RPTUN lower-half、resource table、generation |
| [bk7258_rptun_mbox.c](../../../../chips/bk7258/common/bk7258_rptun_mbox.c) | SDK logical mailbox、ISR→worker defer |
| [bk7258_rpmsg_test.c](../../../../chips/bk7258/common/bk7258_rpmsg_test.c) | 双producer测试与CPU0 gateway |
| [bk7258_ap_supervisor.c](../../../../chips/bk7258/cp/bk7258_ap_supervisor.c) | primary/secondary/RPMsg health |
| [bk7258_rpmsgfs.c](../../../../chips/bk7258/common/bk7258_rpmsgfs.c) | AP `/cpdata` mount wrapper |
| [bk7258_rpmsgfs_test.c](../../../../chips/bk7258/common/bk7258_rpmsgfs_test.c) | bounded控制面与文件gate |

## 6. Bluetooth

| 文件 | 作用 |
|---|---|
| [bk7258_bt_controller.c](../../../../chips/bk7258/cp/bk7258_bt_controller.c) | CP official Controller/bootstrap/MAC/UART恢复 |
| [bk7258_bt_hci.c](../../../../chips/bk7258/ap/bk7258_bt_hci.c) | AP NuttX `bt_driver_s` lower-half |
| [bk7258_ble_gatt.c](../../../../chips/bk7258/ap/bk7258_ble_gatt.c) | combined GAP+N13 service与worker |
| [bk7258_bt_ipc.h](../../../../chips/bk7258/include/bk7258_bt_ipc.h) | team Bluetooth IPC contract |
| [N13 evidence](../nuttx-port/n13-evidence-index.md) | RF/GATT/reconnect/coexistence原始证据索引 |

## 7. PSRAM与timer

| 文件 | 作用 |
|---|---|
| [bk7258_psram.c](../../../../chips/bk7258/common/bk7258_psram.c) | CP owner、capacity gate、role heaps、allocator wrapper |
| [bk7258_psram.h](../../../../chips/bk7258/include/bk7258_psram.h) | layout/API contract |
| [BK7258 build verifier](../../../../tools/bk7258/_lib/build.py) | source/layout/ELF ownership门禁 |
| [N14 evidence](../nuttx-port/n14-evidence-index.md) | build、cold、factory与回归索引 |

## 8. BL1、BL2 与 MCUboot

| 文件/目录 | 作用 |
|---|---|
| [canonical partition CSV](../../../../boards/bk7258/common/partitions/bk7258/bk7258_ab_onchip_persistent.csv) | raw布局唯一手工输入 |
| [partition layout module](../../../../tools/bk7258/_lib/layout.py) | 生成SDK/header/JSON/text |
| [BL1 main](../../../../chips/bk7258/bootloader/boot_main.c) | 固定Primary→Secondary验证、SRAM复制与BL2 handoff |
| [BL1 Manifest](../../../../chips/bk7258/bootloader/boot_bl1_manifest.c) | 生成board-owned Manifest及外部公钥源码 |
| `chips/bk7258/bootloader/bl2/` | pinned NuttX MCUboot bootutil与CP/AP成对slot适配 |
| [MCUboot/package implementation](../../../../tools/bk7258/_lib/package.py) | 使用NuttX `imgtool.py`签名CP/AP并生成32+2镜像 |
| [image implementation](../../../../tools/bk7258/_lib/image.py) | 按CSV组合BL1、双BL2及CP/AP两槽 |

旧N15/N17自研OTA的staging/trial/journal/publish/fault实现和配套验证脚本已退役并从活动源码删除；`progress/verification/`中的旧记录仅作为历史证据，不能当作当前可执行入口。

## 9. 读文档的优先级

遇到冲突时按这个顺序回溯：

1. 当前team-owned source、ELF/map、artifact与raw board log；
2. accepted ADR、verification/milestone；
3. stage worklog/source verification；
4. 本新手教程；
5. 旧prompt、聊天或legacy snapshot。

动态状态永远回到[第11章](11-current-status-and-next-steps.md)和[CURRENT](../../../../progress/CURRENT.md)。

## 10. 从现象跳到文件

| 现象 | 第一批文件/文档 |
|---|---|
| `BClk`后无Tier-1 | Boot magic/CRC packer/physical range |
| Tier-1 `BAD magic/reset/msp` | `boot_main.c`、partition CSV、CP ELF vector |
| `JMP`后死机 | `start.S`、`boot_runtime.c`、CP `bk7258_start.c` |
| NSH无输入 | `bk7258_serial.c`、IRQ 31三道门 |
| 约4295秒reset | `CONFIG_SYSTEM_TIME64`、N6 bug文档 |
| AP不READY | `bk7258_ap_control.c`、AP startup、telemetry |
| CPU2不online | AP SMP/IPI/CPACR/stack |
| RPMsg卡住 | mailbox pending、RX worker、generation、vring |
| RPMsgFS大payload失败 | exclusive-state placement、worker deadline |
| Bluetooth init后UART乱码 | CP Controller wrapper的UART restore |
| BLE第二次连接失败 | `bt_conn` reference与advertising flag |
| PSRAM heap init/realloc卡住 | control block位置、outer spinlock、PM vote |
| OTA reset loop | Boot WDT、pair validation、metadata/remap gate |
