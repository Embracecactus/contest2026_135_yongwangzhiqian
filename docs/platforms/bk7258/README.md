# BK7258 平台集成（T5AI Core / T5 Board / AIDK AI Toy）

[English index](README_EN.md) | 简体中文

本目录是三块 BK7258 物理板共用的平台集成入口，覆盖 CP/AP 配对构建、交付符合性、
调试方法和历史阶段记录。纯 SoC 契约位于 [BK7258 chip 文档](../../chips/bk7258/README.md)，
单板引脚和 profile 以 `boards/bk7258/` 为准，动态验收状态以
`boards/bk7258/CONFIGS.md` 和对应 verification 记录为准。

评审时应先阅读 [官方符合性复核（中文）](official-compliance-review.md) /
[English](official-compliance-review.en.md)，再按需查阅[移植报告](porting-report.md)和
[文档适配矩阵](openvela-document-adaptation-matrix.md)。下方长篇 N1–N17 叙述是工程阶段
索引，保留用于追溯，不代表所有历史 profile 都仍是当前产品配置。

> **2026-08-10 当前状态勘误：**N15/N17 自定义 OTA selector、writer、journal、
> validation profile 和脚本已经退役并从现役源码删除。下文 N15 内容只保留
> 历史实板证据。当前实现是完整的 board-owned BL1 → pinned NuttX MCUboot
> BL2 → signed same-slot CP/AP 启动链；没有 field OTA writer/confirm/rollback
> 服务。动态状态以 `boards/bk7258/CONFIGS.md` 为准。

板级原理图和验证边界见 [hardware/README.md](hardware/README.md)。SoC 共用的 OPP、
SDK 索引、chip 清理评审和 J-Link/SWD 方法已归档到
[BK7258 chip 层文档](../../chips/bk7258/README.md)；文档分层规则见
[docs 导航](../../README.md)。

openvela 官网 dev-ai-contest-2026 中文目录的逐项覆盖、剩余适配项、优先级和
统一验收规则见
[openvela-document-adaptation-matrix.md](openvela-document-adaptation-matrix.md)。

## T5-Board ILI9488 驱动边界

官方 NuttX 已提供 `include/nuttx/lcd/ili9488.h`
命令定义，但现有 `sam_ili9488.c` 是 SAMV71 SMC/DMA 板级实现，且其 SPI 路径明确
尚未支持，不能直接用于 T5-Board 的 RGB 扫描输出加独立三线控制总线。本项目因此
复用官方命令头，并在团队 `nuttx/` overlay 中只补充 transport-independent 的
ILI9488 RGB 初始化器；BK7258 chip 层实现通用 GPIO 9-bit 三线传输，T5-Board 板层
拥有 GPIO49/48/50 控制线、GPIO53 RESET、GPIO9 背光、RGB 引脚和时序。通用面板层
不包含 BK7258 GPIO、板型或 SDK 私有面板对象。

## AIDK AI Toy 外设总线与驱动边界

AIDK AI Toy 上的 MFRC522 **物理连接是 UART，不是 SPI**：UART1 使用 P0/TX、
P1/RX，配置为 9600 baud、8N1；P53 是 `NFC_IRQ`，P54 是 `NFC_MX`，P55 是
低有效的 `_NFC_DTRQ`；器件通过 P52 `LDO33_EN` 上电，高电平有效。
同一条 `LDO_3V3` 还经 R45 给板载 1 Gbit SD NAND 的 `NAND_VDD` 供电。
板上两块 GC9D01 的 LEDA/VDD 也连接这条 `LDO_3V3`。P52 属于 SDK 跨核电源
管理域，板级初始化必须使用
`bk_pm_module_vote_ctrl_external_ldo(GPIO_CTRL_LDO_MODULE_NFC, P52, HIGH)`
以及对应的 `GPIO_CTRL_LDO_MODULE_SDIO`、`GPIO_CTRL_LDO_MODULE_LCD` 分别投票，
不得由 AP 直接解除 GPIO 映射后抢占 P52。SDIO 和 LCD 投票必须分别发生在首次
MMC/SD 探测和首条面板命令之前；NFC 后续加入自己的投票。SDK 会保持共享电源，
直到所有使用者都释放投票。

板上两块 LCD 共用一条背光：P25 `LCD_BL_PWM` 经 R61 驱动 NPN 管 Q3，Q3 控制公共
`LCD_BL`。PWM 高电平使 Q3 导通，因此两屏只能一起启停/调光，不能独立控制。
CN5 的可选单屏模块未接，但这不影响板上直接安装的两块 GC9D01；维护配置会在
启动阶段为这两块屏占用并驱动 P25。

双屏接线和设备号如下：

- LCD1 `/dev/fb0`：QSPI1，P2 `CLK`、P3 `CS`、P4 `D0/SDA`、P5 `D/C`、
  P45 `RESET`；
- LCD2 `/dev/fb1`：QSPI0，P22 `CLK`、P23 `CS`、P24 `D0/SDA`、P7 `D/C`、
  P6 `RESET`。

NuttX/OpenVela 当前没有 GC9D01 驱动，因此项目提供一份可上游化的通用 NuttX
驱动，真源位于 `nuttx/drivers/lcd/gc9d01.c`
和 `nuttx/include/nuttx/lcd/gc9d01.h`。
项目 manifest 把团队仓库的整个 `nuttx/` 目录映射为 `vendor/beken/nuttx`，构建从
该外部 overlay 读取；官方 `open-vela/nuttx` 检出目录不作修改。通用驱动拥有
GC9D01 命令、初始化序列、状态和标准 `lcd_dev_s` ABI；BK7258 chip 层只提供
SPI-over-QSPI/DMA 传输；AIDK 板层拥有两个物理实例、RESET/D-C、P25 背光和 P52
LCD 电源投票。初始化序列注明了 Apache-2.0 SDK 来源，但产品不链接 SDK 私有
`lcd_device_t` 面板对象。可分别使用 `fb /dev/fb0`、`fb /dev/fb1` 做帧缓冲测试。

该供电动作属于 AIDK 板层，不属于 BK7258 chip SDIO lower-half：P52、R45 和
`NAND_VDD` 是本板原理图事实，其他 BK7258 板可能没有外部 LDO，或使用完全不同的
控制引脚。通用 chip 驱动只负责 SDIO 控制器、命令、时钟和中断，并在访问控制器
前调用板级 `initialize()` 回调；AIDK 回调负责先投票上电、等待稳定，再配置
P14–P19。SDK 的默认 GPIO 表会预先把单线 SDIO 映射到 P2/P3/P4，而选择 map mode 1
不会自动解除旧映射；AIDK 板回调因此先释放 P2/P3/P4，再映射实际连线的
P14/P15/P16（四线模式再加入 P17–P19）。随后双屏初始化再由 LCD1 正式接管
P2–P4；QSPI 映射模式临时占用的 P6/P7、P25–P27 由相应板级设备按实际接线重新
配置。AIDK 维护配置使用仓库已有的
`ap-sdio4` SDK 变体并启用 D0–D3 四线传输；该变体只改变 SDK 数据路径的编译期
总线宽度，不新增板级脚本或复制 SDIO 驱动。当前 SD NAND、NFC 和 LCD 均以各自
模块身份共享 P52；P52 仍是 AIDK 板级事实，不能固化进通用 chip 驱动。

NuttX 上游 `drivers/contactless/mfrc522.c` 目前只公开接收
`struct spi_dev_s *` 的 `mfrc522_register()` 入口。为复用上游已经实现的
ISO14443-A 寻卡、防冲突、级联选择、CRC 和 `MFRC522IOC_*` ABI，板级文件
`boards/bk7258/aidk_ai_toy/src/bk7258_aidk_mfrc522.c`
提供一个仅存在于内存中的 `spi_dev_s` 适配器，把寄存器访问翻译成模块的 UART
协议：读操作发送 `0x80 | register` 后接收数据；写操作发送 `register`、接收回显，
再发送数据。`aidk_nfc_spi_*` 名称表示它实现了 NuttX 驱动所需的软件接口；代码
不会初始化 SPI 控制器、占用 SPI 引脚或产生 SPI 波形。用户接口仍为标准
`/dev/nfc0`。

这种边界避免复制一套 MFRC522 协议驱动，也不依赖 SDK 私有的 NFC 应用层接口；
UART1 和板级供电由 AIDK 板适配负责，卡协议与字符设备 ABI 继续由 NuttX 上游
驱动负责。AIDK AP 配置必须启用 `CONFIG_STANDARD_SERIAL`，否则 UART1 lower-half
不会注册 `/dev/ttyS1`，板级 UART 适配器会以 `-ENOENT` 失败。SDK 默认 GPIO 表把
P53–P55 复用为 LCD 输出；板级 NFC 初始化必须先解除该复用，将 P53 配成上拉输入，
将未使用的 P54/P55 握手线配成无上下拉输入，并通过 MFRC522 `TestPinEnReg`
关闭 MX/DTRQ 输出，避免两侧输出相互冲突。当前 NuttX 上游驱动轮询器件寄存器，
不使用 P53 中断。

P0 调试、xTS、压力测试和低噪声性能基线的 profile 边界、构建方法、板端命令与
证据格式见
[p0-diagnostics-performance.md](p0-diagnostics-performance.md)。
generation 143 诊断镜像、generation 145 历史性能镜像和当时 30 个独立 benchmark
session 见 [P0 实板验证记录](../../verification/bk7258/2026-08-27-bk7258-p0-diagnostics-performance.md)；
修正 SDK OPP 语义后的 generation 146 CP/CPU0 240 MHz 全量下载、回读、冷启动与
新一轮 30 个 session 见
[时钟适配实板记录](../../verification/bk7258/2026-08-27-bk7258-sdk-clock-240m-validation.md)。

## 当前维护入口

- 三板 CP/AP 配置与分区入口：`boards/bk7258/CONFIGS.md`；
- 通用 SoC 契约：[`docs/chips/bk7258/`](../../chips/bk7258/)；
- 构建、烧录与调试：[`nuttx-port/bk7258-build-flash-debug-sop.md`](nuttx-port/bk7258-build-flash-debug-sop.md)；
- 正式验收记录：[`docs/verification/bk7258/`](../../verification/bk7258/)；
- 初学者心智模型：[`docs/learning/bk7258/`](../../learning/bk7258/)；
- 许可证与派生来源：`SOURCE_PROVENANCE.md`。

旧 N1–N17 阶段资料仅在确有持续技术价值时保留于 `nuttx-port/` 和正式验收记录中；恢复提示词、动态待办和重复官方文档均不再作为产品文档维护。
