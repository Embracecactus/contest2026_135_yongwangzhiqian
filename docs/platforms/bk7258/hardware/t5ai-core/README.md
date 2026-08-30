# T5AI-Core V1.0.1 硬件连接与验证边界

本目录只适用于 T5AI-Core V1.0.1。板级验证记录见
[validation-2026-08-09.md](validation-2026-08-09.md)，最小裸机启动/UART 探针见
[probe/README.md](probe/README.md)。

## 1. 证据来源

- 板卡：T5AI-Core
- 硬件版本：V1.0.1
- 原理图：[`T5AI-Core_V101-SCH-a69f7b5a91b4bf21a39bdb7c17812373.pdf`](schematics/T5AI-Core_V101-SCH-a69f7b5a91b4bf21a39bdb7c17812373.pdf)
- 页数：5
- 文件大小：1,044,350 bytes
- SHA256：`091b7a9302ec51cbb71f12b339cddbf34cf5d5a28b529f64613e498759eef086`
- 审阅方式：逐页文字提取与原理图渲染图交叉核对

本文件记录的是板级事实，不用于推导 BK7258 芯片内部寄存器或安全 ABI。芯片实现仍以官方 BK7258 SDK v3.1.1.9 为依据。

项目维护者已按实物确认：该 Core 板不提供可用的 BK7258 原生 USB 接口，板载音频验证范围为单 MIC 输入。以下验证边界以实物确认为准，不把芯片或模组的信号能力等同于 Core 板接口。

## 2. 关键连接总览

```text
USB Type-C
  ├─ VBUS ───────────────> 电源/充电电路
  └─ D+/D- ──────────────> CH342F 双串口芯片
                              ├─ 通道 0 <-> BK7258 UART0_TX/RX
                              └─ 通道 1 <-> BK7258 P0/P1

P9  ─> 板载 LED（高电平点亮）
P29 <─ 用户按键（按下接地，低有效）
P28 <─ 电池电压 BAT/4
P38 <─ CHG_DET_N
P20/P21 <-> SWCLK/SWDIO，并同时引出到 J2
```

## 3. 分页审阅结论

| 页码 | 电路 | 与软件验证相关的结论 |
| --- | --- | --- |
| 1 | Type-C、电池和 ETA6003 充电管理 | Type-C 数据线连接 CH342F；`ADC_BAT` 由 3 MΩ/1 MΩ 分压得到，约为电池电压的四分之一；`CHG_DET_N` 为低有效命名。 |
| 2 | 5 V/3.3 V 电源、CH342F、自动复位 | CH342F 提供两路串口。自动复位由 DTR0、RTS0 和 Q2 共同构成，不能把任一控制线当作独立的简单复位信号。 |
| 3 | T5-E1 模组和主信号映射 | P28/P29/P38 分别用于电池采样、用户按键和充电检测；Core 板不提供可用的 BK7258 原生 USB 接口。 |
| 4 | 单 MIC 音频输入 | 板载麦克风接 MICP1/MICN1；本板型只按这一组单 MIC 输入验证。 |
| 5 | 按键、LED 和排针 | P9 LED 高有效；P29 按键低有效；复位键低有效；SWD 和普通扩展 GPIO 通过排针引出。 |

## 4. 板级信号表

| 功能 | BK7258 信号 | 电路属性 | 验证注意事项 |
| --- | --- | --- | --- |
| 板载 LED | P9 | GPIO 输出，高有效，串联 1 kΩ | 可直接验证，不需要外接器件。 |
| 用户按键 | P29 | 10 kΩ 上拉、按下接地、100 Ω 串联和 0.1 µF 去抖，低有效 | 可由用户按压完成物理验证。 |
| 复位按键 | T5_RST | 10 kΩ 上拉、按下接地、22 Ω 串联和 0.1 µF 滤波，低有效 | 可用于手动 warm reset；不是断电冷启动。 |
| 主调试串口 | UART0_TX/RX | 连接 CH342F 通道 0 | Windows 会枚举为两路 COM 中的一路，需用日志内容确认端口。 |
| 第二串口 | P0/P1 | 连接 CH342F 通道 1 | 与专用 UART0 引脚不同，不能交换配置。 |
| 自动复位 | DTR0/RTS0 -> Q2 -> T5_RST | 双控制线晶体管电路 | 工具必须按已验证的 DTR/RTS 组合动作，不能只切换一根线。 |
| 原生 USB | USB_P/USB_N | Core 板没有可用接口 | 不属于 T5AI-Core 的验证范围；不能借用 T5-Board 的结论。 |
| SWD | P20/P21 | SWCLK/SWDIO，同时引出到 J2 | J-Link 连接期间不得把 P20/P21 配置为普通 GPIO。 |
| 电池采样 | P28/ADC_BAT | BAT 经 3 MΩ/1 MΩ 分压和 100 Ω 输入 | 换算关系约为 `VBAT = VADC * 4`；实际精度仍需 ADC 校准和实测确认。 |
| 充电检测 | P38/CHG_DET_N | 低有效命名 | 需要 USB/电池状态组合才能完整验证。 |
| 板载麦克风 | MICP1/MICN1/MICBIAS | 单路模拟麦克风输入 | Core 板音频只按该单 MIC 采集验证。 |

## 5. 全面验证分层

### A. Core 板无需外接器件即可验证

- BL1 -> NuttX MCUboot BL2 -> CP/AP 镜像启动链；
- CP/AP、AP SMP、RPTUN/RPMsg；
- Flash MTD、LittleFS、PSRAM；
- RTC、Timer、watchdog、TRNG；
- P9 LED、P29 用户按键；
- Wi-Fi 和 BLE；
- 单路板载麦克风；
- BAT/USB 当前接线条件下的电池 ADC 和充电检测读值。

### B. 需要从排针连接外设后验证

- I2C、SPI、I2S、SDIO/MMC、QSPI、PWM 和普通 GPIO；
- CAN、Ethernet、DVP、LCD 等需要外部收发器、连接器或显示/摄像头的功能。

### C. Core 板自身不提供的连接器

- TF 卡座；
- RGB LCD 连接器；
- DVP 摄像头连接器。

这些控制器可以完成编译、链接和底层接口审查，但不能把“驱动已编译”表述成
“T5AI-Core 硬件已验证”。T5-Board 和 AIDK AI Toy 必须使用各自板型的配置与
日期化验收记录。

## 6. 安全边界

1. 不扫描或翻转全部 GPIO；先按原理图白名单选择引脚。
2. J-Link 工作时保留 P20/P21 的 SWD 功能。
3. Type-C 仅作为供电和 USB 转串口，不用于 BK7258 原生 USB 验证。
4. J-Link RESET、板载复位键和 CH342F 自动复位都属于 warm reset，不能替代完整断电冷启动。
5. 不进行 OTP/eFuse 写入，不启用不可逆 Secure Boot 生命周期。
6. 单 MIC 验证先使用正常环境声和低风险采集配置，不把未采样的音频能力标为通过。
7. 外设验证必须记录接线、电压域和所用排针，不将外部扩展板能力归属为 Core 板板载能力。

## 7. 与板型配置的核对

`boards/bk7258/t5ai_core/include/bk7258_board_config.h` 中的以下定义与 V1.0.1 原理图一致：

- P9：板载 LED，高有效；
- P29：用户按键，低有效；
- P28：电池 ADC；
- P38：充电检测；
- 单 MIC 输入和电池电路存在；
- 不存在 TF、RGB LCD 和 DVP 专用连接器。

实物确认了两个容易误判的事实：Core 板没有可用的 BK7258 原生 USB 接口；CH342F 的两路串口分别连接专用 UART0 和 P0/P1。
