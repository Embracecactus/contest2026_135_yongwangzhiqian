# BK7258 MCUboot / Secure Boot 资料索取单

日期：2026-08-06  
适用对象：Beken 技术支持 / SDK 维护团队

## 可直接发送的正文

您好，我们正在为 **BK7258 / T5-AI 双核板**移植 NuttX：CP 与 AP 是两份
独立固件，但必须作为一个版本对进行 A/B 原子切换。当前项目使用贵方
`release/v3.1.1.9` 的 BK7258 CP/AP 静态库，SDK 源码和静态库均保持只读，
仅在项目侧做 wrapper 适配。

我们希望采用贵方的官方启动模型：

```text
BootROM (BL1) -> BL2 (MCUboot) -> PRIMARY_ALL / SECONDARY_ALL
```

我们以公开 `bk_idk release/v2.0.1` 的 BK7236 secureboot 工程作为启动链
参考；公开 BK7258 `release/v3.1.1.9` 则保留 BK7258/BK7258_AP profile，
却不含对应的 TF-M、MCUboot BL2 或 secureboot 工程。因此烦请提供以下
**与 BK7258/BK7236 对应**的官方材料。

### P0：构建并验证 BL2 的必要材料

1. 可复现的 BK7258/BK7236 TF-M + MCUboot 源码包或官方 Git/repo manifest，
   包含确切 release、commit 和依赖版本。
2. BK7258/BK7236 的 BL2 platform target 完整目录，尤其是：
   - Flash map / Flash HAL / 34:32 CRC 地址换算；
   - Direct-XIP primary/secondary offset 配置；
   - startup、链接脚本、分区定义、时钟与看门狗初始化；
   - Crypto/OTP/eFuse/Root public key hash 接口；
   - BL2 到 CP/AP 的 handoff 代码。
3. 一个可直接构建的 BK7258 secureboot Direct-XIP A/B 工程，或与其等价的
   最小示例。请同时提供 `defconfig`、`security.csv`、`ota.csv`、
   `auto_partitions.csv`、pack 配置及构建命令。
4. 对应示例的产物和尺寸信息：`bl2.bin`、BL2 ELF/MAP、BootROM manifest、
   签名后的 `PRIMARY_ALL` / `SECONDARY_ALL`、以及生成它们的命令行。
5. Non-Secure 应用所需的 v3.1.1.9 静态库选择表和编译选项。我们已确认交付
   bundle 含 `libwifi_nspe.a`、`libcom_phy_nspe.a`、部分蓝牙 `_nspe` 库，
   但没有 `libdriver_nspe.a`、`libbk7258_nspe.a`、`libcm33_nspe.a` 的同名
   对。请确认这些核心库是否本身可由 Non-Secure CPU0 应用链接，以及官方
   `ARCH_TRUSTZONE_NONSECURE` / `-mcmse` / include-path 的完整组合。
   我们在**临时副本**上保持 `CONFIG_TZ=y`、仅改 `CONFIG_SPE=0` 后，已成功
   构建 BK7258 CP；生成配置为 `CONFIG_TZ=1`、`CONFIG_SPE=0`，且自动选择
   `libwifi_nspe.a`、`libbk_phy_nspe.a`、
   `libbluetooth_controller_controller_only_ble_nspe.a`。同样设置用于 AP 时，
   v3.1.1.9 在 `cli_dma.c` 与 `sys_pm_hal.c` 因
   `bk_dma_check_chn_status()` 缺少声明而以 `-Werror` 失败（实现位于
   `general_dma/dma_driver.c`）。请确认 AP 应保持 SPE，还是提供 BK7258 AP
   Non-Secure 的正式配置或修复；我们不会自行修改 SDK 源码来绕过该错误。

### P0：BK7258 双核镜像与 A/B 语义确认

请确认以下理解是否正确，并给出官方定义：

1. BL2 是否应将连续的 CP + AP span 作为**一个** MCUboot image
   (`PRIMARY_ALL` / `SECONDARY_ALL`) 签名、验证和 remap？
2. BK7258 的 Direct-XIP 双槽是否要求等长、连续；Flash controller remap
   的寄存器/API、调用时序和 cache/MPU 要求分别是什么？
3. CP 与 AP 的启动顺序、AP reset/release 时机，及 AP 固件在 BL2
   验证后的定位方式。
4. 官方 OTA 的 trial / confirm / rollback 状态存放在哪里？请给出记录格式、
   擦写粒度、掉电恢复语义，以及应用确认接口。
5. MCUboot 的 image header、TLV、EC-P256 签名、security counter 的确切
   配置；BK7258 上使用 one image 还是 multi-image 的官方推荐方式。
6. 官方分区工具为 `primary_cpu0_app` 生成 TF-M 到 Non-Secure CPU0 的交接
   常量。对于一个连续签名的 CP + AP pair，请确认 AP 应作为同一
   `PRIMARY_ALL` 的附属镜像还是 MCUboot multi-image；并请提供 CP、AP 的
   `*_VIRTUAL_CODE_START`、Non-Secure XIP alias、SRAM alias 及 CPU1 boot
   address 的实际生成结果。

### P1：安全与开发板恢复边界

我们当前只需要可恢复的开发流程，**不请求烧写 OTP/eFuse、开启 secure boot、
写入 Flash AES key 或锁定 SWD/JTAG**。请提供文档说明：

1. BootROM 如何区分 normal boot 与 secure boot（BL1 control / manifest /
   magic / OTP 条件）。
2. 未 provision 的开发板能否构建、烧录并验证 BL2/Direct-XIP 流程；若可以，
   安全能力和生产模式相比缺失什么。
3. 正式 provisioning 所需 OTP/eFuse 字段、写入顺序、不可逆影响、read-back
   验证和官方恢复/返修流程。
4. Flash AES、34/32 CRC 和 MCUboot image 签名的先后顺序及地址计算规则。

### 若不便提供完整源码

请至少提供以下可审计替代物：

- 与 BK7258 固件版本精确匹配的 `bl2.bin`、ELF/MAP、头文件和分区生成结果；
- BL2 与 BootROM 的接口/镜像格式规范；
- 签名工具版本、命令和不含私钥的示例输入/输出；
- 官方 BK7258 secureboot/Direct-XIP A/B 应用说明。

我们可以提供当前 Flash 布局、启动日志、镜像头和最小复现工程；不会要求或
提交任何私钥、OTP 内容、设备唯一数据或生产凭据。谢谢。

## 已核实的项目背景（供支持人员定位）

- 目标为 BK7258；当前 raw bootloader 验证 `BK7236` 镜像 magic，并以
  34 数据字节 / 32 逻辑字节的 Flash CRC 映射工作。
- 当前已部署的 A/B 可执行窗口是 CP + AP 的连续 pair；现有 bootloader 位于
  Flash 开头、大小 68 KiB。这与安全链示例中 BL1 control、manifest、96 KiB
  BL2 的布局不同，迁移需单独设计，不能直接覆盖。
- 公开 BK7258 v3.1.1.9 库 manifest 声明其内部依赖过 MCUboot revision
  `556aaafee82ab85dd7d18aaf7dd8b5e0d2f5db38`、TF-M revision
  `fd8c85a882308171964cd14932d93262add744bd`。这些仅用于帮助技术支持定位，
  不表示项目已拥有或可重建这些内部组件。

## 期望回复格式

请按“资料名称 / 适用芯片与 SDK 版本 / 获取路径或附件 / 是否可用于开发板
非 provisioning 验证 / 注意事项”逐项回复。如某项不存在，也请明确推荐的
BK7258 官方替代路线。

## 已收到的官方边界回复

技术支持已确认：**BK7258 硬件支持 Secure Boot，但 BK7258 当前 SDK 尚未完成
Secure Boot 适配。**

这条回复确认的是芯片硬件能力，不等同于已经提供 BK7258 v3.1.1.9 的 BL1/BL2
工程、签名工具、分区模板或可烧录安全镜像。因此当前项目采取以下边界：

- 不烧写 OTP/eFuse，不打开不可逆 Secure Boot provisioning；
- 继续使用 NuttX MCUboot + 项目侧 BK7258 wrapper 完成可逆开发验证；
- v3.1.1.9 SDK 只提供已存在的 CRC/分区规则，不宣称提供完整 Secure Boot
  适配；
- 官方 Secure Boot 适配作为后续独立阶段，等待 Beken 提供精确 BL2/manifest/
  签名产物或接口规范后再接入。
