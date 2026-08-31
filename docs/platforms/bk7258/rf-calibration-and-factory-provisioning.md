# BK7258 RF 校准与工厂烧录规范

Last reviewed: 2026-08-31

本文是 BK7258 产品级 RF 校准、设备唯一数据和工厂烧录契约，适用于
T5AI-Core、T5-Board 与 AIDK AI Toy。具体板卡的 RF 通路、天线、连接器、夹具和
线损仍由该物理板及其制造工位拥有，不能从另一块板复制。

## 产品结论

- 当前产品不自建 RF 校准工位，也不在普通开发板上尝试无仪器自校准。采购/生产环节
  必须交付已经逐台校准的板卡或模组及对应制造记录；本项目只负责识别、保留、回读
  和验收设备自己的 `sys_rf`。空白或 invalid 的设备不得作为成品放行。
- RF 校准数据是每台设备的制造数据，不是通用固件的一部分；MAC、晶振补偿、
  发射功率表和相关 Bluetooth/RF 状态不得跨设备复制。
- BK7258 启动时执行的 `calibration_main()` 包含运行时模拟/RF 调整和既有数据加载，
  不等于工厂量产校准，也不会把全 `0xff` 的 `sys_rf` 自动补成一套可交付校准数据。
- 普通构建、OTA 和设备绑定的有线恢复必须保留目标设备自己的 RF 数据。当前产品
  release policy 将 `sys_rf` 与 `sys_net` 声明为 `device-unique`。
- 通用工厂镜像只有在受审查的制造 provisioner 能为每台设备分配 MAC、执行 RF
  校准并回读验收后才成立。在此之前，产品发布保持 `requires-provisioning`。
- 当前 AIDK AI Toy 中恢复并验证的数据直接来自同一设备的 accepted-base 完整回读；
  现有证据不能追溯它最初由哪一台仪器、哪一个工位或哪一个工具版本生成。

## 事实与责任边界

| 事实或动作 | 唯一责任方 |
|---|---|
| Flash 容量、分区地址和大小 | 物理板 `openvela.conf` 选择的 partition CSV |
| 更新时 replace/preserve/device-unique 语义 | 所选 release-policy CSV |
| RF 算法、TLV 格式、量产命令语义 | 与 BK7258 及 RF 测试固件匹配的 Beken 工具链 |
| 天线/传导路径、屏蔽箱、夹具与线损 | 物理板制造测试设计 |
| 频偏、功率、EVM、灵敏度限值 | 产品法规区域、BOM、天线设计和受控量产配置 |
| 启动时读取并应用既有 RF 数据 | BK7258 chip/SDK 层 |
| 某台设备的 MAC、校准结果和工位记录 | 制造 provisioner/MES |
| 保留或恢复设备唯一分区 | `tools/bk7258/bk7258.py` 产品包流程 |

板层只能描述该板的 RF 接口、天线选择、控制 GPIO 和夹具约束；不能实现 SoC 校准
算法或保存一份“板级通用校准表”。应用层不能直接修改 `sys_rf`、伪造 valid 标志，
也不能把启动成功当作 RF 合格证据。

## Flash 数据契约

AIDK AI Toy 当前选择
`boards/bk7258/aidk_ai_toy/bk7258_ab_fixed_block_full_release.csv`，其中：

| 分区 | 当前范围 | partition policy | product release policy |
|---|---:|---|---|
| `sys_rf` | `0x7fe000`，4 KiB | `immutable` | `device-unique` |
| `sys_net` | `0x7ff000`，4 KiB | `immutable` | `device-unique` |

地址只说明当前所选布局。任何工具都必须从目标板所选 CSV 取得几何信息，禁止把上述
数值硬编码为所有 BK7258 产品的固定地址。

`sys_rf` 中已观察到厂商 TLV、Wi-Fi 发射功率/晶振数据和 MAC record；具体二进制格式
仍按厂商私有 ABI 作为整体处理。产品工具不得只复制其中一段、重排 TLV，或把一个
设备的尾区拼入另一个设备的完整镜像。

partition CSV 的 `immutable` 限制普通构建写入，release policy 的 `device-unique`
要求整机物料化从目标设备 accepted base 原样继承。两者是不同层面的约束，必须同时
满足。

## 启动行为的实板判定

2026-08-31 在 AIDK AI Toy 上对空白数据和同设备 accepted base 做了两次连续启动
观察：

| Flash 初始状态 | 两次启动的关键现象 | Flash trace | 判定 |
|---|---|---|---|
| `sys_rf` 目标区全 `0xff` | 每次均执行 `calibration_main()`；报告 RF flag invalid、polar table magic 错误并使用默认 `xtal_cali:58` | 只有 READ，没有 ERASE/WRITE | 启动不会生成或持久化完整工厂校准数据 |
| 恢复同设备 accepted-base RF 数据 | 两次读取相同 TLV/各表 hash；RF flag valid、`txpwr=0xf`、`xtal_cali:31`，Wi-Fi/BLE MAC 一致 | 只有 READ，没有 ERASE/WRITE | 工厂数据从 Flash 加载并跨重启保持 |

恢复场景还完成了完整分区的四点闭环：同设备 accepted-base、设备绑定的 8 MiB
诊断完整镜像、烧录后第一次启动和第二次启动的 `sys_rf` SHA-256 均为
`b6cb961f09a3ad8c3c5d749a27c63dbb010015f67cb06c77ea37f211c8cee781`。
不可变验收记录见
[AIDK AI Toy sys_rf preservation acceptance](../../verification/bk7258/2026-08-31-aidk-ai-toy-sys-rf-preservation.md)。

因此，日志中的“执行 calibration”不能解释为“首启量产校准并保存”。空白状态下的
启动过程只能完成当次运行所需的偏置、时序等调整；完整的频偏/功率表、身份记录和
valid 状态仍必须来自制造校准或同一设备的可信备份。

## 校准数据从哪里来

产品链路必须是：

```text
设备唯一身份
  + BK7258 匹配的 RF 测试固件
  + 受控 RF 夹具/综测仪/BAT 校准板
  + 经批准的目标值、限值和线损
  -> 实测频偏与发射功率
  -> 计算并写入该设备的校准参数
  -> EVM/功率/接收验证
  -> PASS 后写 valid
  -> 冷启动回读与 MES 记录
```

校准值来自该块 PCB 上实际晶振、射频前端、供电、布局和温度条件的测量结果，不能由
固件版本号推导，也不能由另一块“同型号板”的数据替代。通用固件只提供执行环境和
读取机制；制造工位才产生每台设备的数据。

现有 AIDK accepted base 只能证明这些字节属于已验收的目标设备。若原制造记录缺失，
不得反推并宣称它由某个具体工具生成；只能记录为“来源于同设备 verified readback，
原始工位 provenance 未建立”。

## Beken 工具的产品定位

### BEKEN_RF_CAL

审计样本为 `BEKEN_RF_CAL_V2.2.8.1_20240718`。它是两套工具中的量产校准主体：

- 随附《博通集成产测指南 v1.2》描述综测仪方案和 Beken Auto Test（BAT）方案，
  并在 Flash 保存能力中明确列出 BK7258；
- FLOW 配置包含 `BK_CAL_START`、多信道/速率功率校准、
  `BK_SAVE_CAL_DATA=1` 和 `BK_WRITE_FLASH=1`；
- PASS 路径最后使用 `txevm -e 4 1` 写有效标志。

这说明它具备“测量/拟合 -> 保存参数 -> 写 Flash -> PASS 后置 valid”的量产职责。
但投入生产前仍必须在界面中确认实际识别 BK7258，并确认 RF 测试固件、串口协议、
限值文件和硬件工位完全匹配。文档中出现 BK7258 不等于任意配置都可直接用于任意板卡。

### WiFi_Test_Tool

审计样本为 `Beken Wi-Fi Test Tool V1.7.9`。它主要是手动 RF 测试控制器：

- 控制 Wi-Fi/Bluetooth/BLE 的 Tx/Rx、信道、速率、功率 index、xtal index、
  `txevm` 和 `rxsens` 等测试；
- 适合研发调试、综测仪联调及校准后的 EVM/功率/灵敏度复核；
- 界面虽有 `Save Xtal C in Flash`，但这只是单项写入，不是完整的多点量产校准；
- 随附文档是《BK7236 Program Download & RF Test V1.3》，本地材料没有证明该
  V1.7.9 组合已完整支持 BK7258。

因此，WiFi_Test_Tool 不能代替 BEKEN_RF_CAL/正式 ATE provisioner。生产环境默认禁用
`Save Xtal C in Flash`，除非受控维修流程明确要求并能随后完成全项 RF 复验。

### 审计样本身份

这些文件只作为本次工具职责审计样本，不随仓库分发，也不自动成为批准的生产版本：

| 文件 | SHA-256 |
|---|---|
| `BEKEN_RF_CAL.../run.exe` | `85907deb58eea5bb500c379efe18924b0245626061514ba6455d7b8488c5a47e` |
| `博通集成产测指南v1.2.pdf` | `df350774d54614678305d7b619b61abb41aaa1c8b0055bc7518444ca527dc4c7` |
| `Wifi_Test_Tool_V1.7.9.ex` | `8c6f35da55457b0d4d813bbe1e40f79318f35068f7f6186ad1ff2d31a37425d4` |
| `BK7236 Program Download & RF Test V1.3.pdf` | `95ab73ef287ba94580f6e4eb03709c2d41f619c8ffff84b3cb1470138c3c6812` |

生产批准还需记录供应商来源、签名/发布渠道、工具许可证、BK7258 支持声明、配置文件
hash 和配套 RF 测试固件 hash；仅文件名或本表 hash 不能建立供应链信任。

## 量产工位流程

1. 为 DUT 分配稳定设备 ID，读取完整 Flash 并保存校准前备份；确认目标确为所选板型。
2. 固定 RF_CAL、限值配置、RF 测试固件、综测仪/BAT 固件和工位软件的版本与 hash。
3. 连接受控传导路径或已验证的辐射夹具，校准线缆/开关/衰减器线损；辐射方案还要
   固定屏蔽箱、天线姿态和距离。
4. 进入厂商 RF 测试模式，完成规定信道和速率的频偏、功率校准。禁止用正常产品
   NSH 启动日志冒充测试接口。
5. 使用独立测量结果验证发射功率、频偏、EVM 和接收灵敏度；限值必须对应销售区域、
   天线/BOM 和温度策略。
6. 仅在所有前置校准及验证 PASS 后保存完整参数到 Flash，并最后写 valid 标志。
7. 退出测试固件，至少进行两次冷启动；只读回查 valid、关键表 hash、xtal、功率表、
   Wi-Fi/BLE 身份及正常联网/扫描行为。
8. 将校准前后 `sys_rf` hash、设备 ID/MAC、工具与配置 hash、仪器序列号/校准有效期、
   线损、温度、测量结果和操作员/工位写入 MES 或不可变制造记录。

`txevm -e 4 1` 只负责设置有效标志，不能产生正确参数。禁止脱离完整 PASS 流程单独
执行；否则可能把空白、默认或失败数据伪装成“有效”。

## 构建、恢复和 OTA 策略

| 场景 | `sys_rf` 策略 | 产品要求 |
|---|---|---|
| 日常构建/普通启动 | 不生成、不写入 | SDK 只读取并应用；空白时应报告未 provisioned |
| apps-only OTA | 完全不包含 | 只更新已签名 CP/AP，不触碰设备唯一尾区 |
| 同设备有线恢复 | 从该设备 accepted base 原样继承 | 完整镜像绑定该设备，禁止复制给另一台 |
| 新板/更换 RF 相关器件后的维修 | 重新执行受控校准 | 旧数据不得直接复用；完成全项 RF 复验 |
| 通用工厂交付 | provisioner 在每台设备上生成 | 当前发布继续标记 `requires-provisioning` |
| 跨设备克隆 | 禁止 | 会造成 MAC 冲突、频偏/功率错误及法规风险 |

设备绑定恢复的完整流程仍以
[构建、发布与硬件证据 SOP](nuttx-port/bk7258-build-flash-debug-sop.md)为准。
完整 BIN 覆盖整个 Flash 地址空间并不表示其中的数据可以通用：materializer 必须把
目标设备 accepted base 中的 `device-unique` 字节原样带入同一设备的恢复镜像。

“不动 `sys_rf`”在不同交付形式中含义不同：

- 普通构建和 OTA：更新集合不包含 `sys_rf`，不会为它生成 payload；
- 8 MiB dense 完整镜像：文件必须覆盖该地址，因此只能从同设备 accepted-base
  逐字节继承；烧录器仍可能物理擦写这些地址，最终内容相同不代表没有发生编程动作；
- 不允许烧录器访问不可变尾区的恢复：必须使用在尾区之前结束的 bounded artifact，
  不能同时宣称它是覆盖全 Flash 的 8 MiB dense 镜像；
- 通用出厂镜像：不得嵌入任何一台样机的 `sys_rf`，必须由逐台 provisioner 生成。

所以，闭环后的产品动作是删除临时诊断代码并撤销固件对 `sys_rf` 的任何生成/写入
所有权，而不是从分区表删除 `sys_rf`，也不是在 dense 文件中用 `0xff` 代替它。

## 产品验收门禁

每台出厂设备至少满足：

- 设备 ID、Wi-Fi MAC 和 Bluetooth 地址符合分配策略，且批次内唯一；
- `sys_rf` 不是 erased 状态，厂商格式可读取，valid 成立；
- 校准写入后的完整分区 hash 已回读记录，两次冷启动保持一致；
- 晶振补偿和功率表来自本机测量，不是默认值或其他设备副本；
- 频偏、发射功率、EVM、接收灵敏度在批准限值内，并保留仪器原始结果；
- 正常产品启动只读取校准区，没有非预期 ERASE/WRITE；
- 设备绑定恢复前后，release policy 声明的 `device-unique` 范围逐字节相同；
- Wi-Fi 与 BLE 在产品固件下完成最小 RF 功能验收。

任一条件失败时，设备进入隔离/返修状态，不得仅写 valid、降低限值或复制 golden
board 数据后放行。

## 明确禁止

- 把某块开发板的 `sys_rf`、`sys_net` 或完整 accepted-base 打入通用发布包；
- 对 RF 数据区执行 chip erase、用全 `0xff` 覆盖后依赖“首次启动自动修复”；
- 单独点击 `Save Xtal C in Flash` 或执行 `txevm -e 4 1` 作为完整校准；
- 未备份当前设备数据就启动会写 Flash 的 RF_CAL 流程；
- 在没有屏蔽、衰减和法规控制的环境中持续发射测试；
- 用“Wi-Fi/BLE 能连接”替代功率、EVM、频偏和灵敏度验收；
- 根据另一型号或另一工具文档推断 BK7258 支持，而不核对实际芯片、测试固件和协议。
