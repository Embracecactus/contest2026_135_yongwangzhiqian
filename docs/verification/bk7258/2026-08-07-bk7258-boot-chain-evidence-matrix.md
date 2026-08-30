# BK7258 启动链证据矩阵：公开事实、模板与缺口

日期：2026-08-07
状态：静态证据基线；未启用 Secure Boot，未写 OTP/eFuse，未烧录

## 1. 为什么需要这张表

“芯片硬件支持 Secure Boot”不等于“当前 SDK 有可用的 Secure Boot 方案”，更不等于
“公开 A/B bootloader 就是 MCUboot”。为了避免把三类材料混在一起，本表把每一层按
证据强度分开：

```text
BootROM（芯片内部、未公开）
   │  公开 raw 头只能证明它接受某种启动封装
   ▼
公开 normal / A-B bootloader（二进制已拿到，可逆向）
   │  当前实证是 RBL/FNV/CRC 与 A/B remap
   ▼
应用 CP + AP 成对窗口

另一条尚未落地的通用模板路径（BK7236 同架构参考）：
OTP / BL1 Manifest → BL2 MCUboot → primary_all / secondary_all
```

右侧路径来自 v3.1.1.9 的脚本和 JSON 模板。它描述了工具链接口，**不是**已取得的
BK7258 可启动实现。

## 2. 逐层矩阵

| 层 | 已有的直接证据 | 可确认的事实 | 不能确认的事实 | 当前项目定位 |
|---|---|---|---|---|
| BootROM / BL1-0 | 两个公开 bootloader 的 raw 首部都是 `BK7236\x10\0`；向量表位于 raw image 起点 | BK7258 官方包使用该 raw 头，由 ROM 或更早阶段装入 XIP 启动窗口 | ROM 是否支持/何时启用签名、它如何读取 OTP、是否解析 Manifest | 不复刻 ROM；仅兼容公开 raw 启动约束 |
| normal bootloader | v3.1.1.9 `normal_bootloader/bootloader.bin`，52,352 B，SHA-256 `105161bb...813de6` | 默认 Reset 固定 handoff 到 CP；独立 FAL 路径使用 `bkbl` 0x44-byte diff-FOTA 头、RBL/CRC32/FNV 与 `FOTAL\0` 恢复记录；不是 MCUboot 签名验证样本 | FAL 外部触发、未命名 diff-header/journal 字段及完整升级协议 | 只作为非安全启动行为参考 |
| A/B bootloader | `ab_bootloader/bootloader.bin`，18,720 B，SHA-256 `3b27958e...164047`；Ghidra 反编译 | `appa` / `s_app` 成对窗口；RBL/FNV 校验；`ota_fina_executive` trial/confirm；Flash remap 后 VTOR/MSP 跳转 | MCUboot、EC256、TLV、Secure-Boot Manifest 行为；这些均未在该 binary 中发现 | N15 的成对 CP/AP 与单 remap 是可参考的；不能把它说成 BL2/MCUboot |
| v3.1.1.9 Secure-Boot 脚本 | `genbl1.py`、`bl1_sign.py`、`bl2_sign.py`、`partition.py`、`bl1_control.json` | 工具链预留了 Manifest JSON、ECDSA-256/SHA-256 名称、`primary_all`/`secondary_all` 和 `0x1000` image header 参数 | BK7258 最终 Manifest 二进制格式、BL1 验证代码、可运行签名包 | 仅作为接口线索，不能据它写“官方已适配” |
| BK7236 v2.0.1 官方安全文档 | `TuyaOpen/bk_idk/docs/bk7236/zh_CN/security/bk_security_boot.rst` 与分区/EFUSE 文档；官方支持说明为同架构、但 BK7236 单核 | 明确描述 BL1→Manifest/BL2 验签→BL2/MCUboot 验签的职责；给出 `bl1_control` 12 KiB、`manifest` 4 KiB、`bl2` 128 KiB 及 SHA-256、版本下限、OTP 根密钥语义 | BK7258 的 Manifest 字节格式、地址/寄存器、BootROM 行为、CP/AP 双镜像映射；不能把 BK7236 的单核样例当成 BK7258 实现 | 同架构语义证据；用于约束逆向和 board-owned 适配，不能填补 BK7258 缺失二进制 |
| 签名工具与样本 | v3.1.1.9 的引用路径存在 | 脚本原本期望 `secure_boot_tool` 和 Beken 扩展 imgtool | 这两个工具在 v3.1.1.9 源码/下载包中缺失；无 `primary_manifest.bin`、无 Secure-Boot BL1/BL2 样本 | 缺口；禁止用 v4/BK7259 替代填充 |
| OTP 字段 | 生成的 `otp1.csv` 有 BL1/BL2 key hash 与 counter 名称、偏移、长度 | 通用模板声明过这些字段 | BK7258 ROM/BL1 真正读取的位置与语义；熔丝烧写流程 | 只记录字段名；绝不读写 OTP/eFuse |
| board-owned BL1/BL2 | `bootloader/boot_main.c` 与 `bootloader/bl2/`，使用 pinned NuttX MCUboot bootutil | 这是项目自行构建的、可恢复开发期 BL1→SRAM BL2→MCUboot direct-XIP 原型 | 它与未发布的官方 Secure-Boot BL1/BL2 字节级或协议级等价 | 保留为实验实现；不得称为“官方完整逆向已完成” |

## 3. 关键的反证

下列事实共同否定了“现有公开 A/B bootloader 就已经是 BK7258 MCUboot Secure Boot”的说法：

1. A/B binary 的实际分区名是 `appa`、`s_app`、`ota_fina_executive`，而非
   `bl1_control`、`primary_manifest`、`bl2`、`primary_all`。
2. 其反编译路径检验 `RBL\0`、FNV/hash 和 A/B 状态字节；没有发现 MCUboot image
   magic、MCUboot TLV、EC256/RSA 签名校验或 Manifest parser。
3. v3.1.1.9 BK7258 默认配置是 `secureboot_en=FALSE` 和
   `security_boot_ena=0`；模板中还出现 Model ID `7236`。
4. 关键签名工具、Manifest 样本、BK7258 Secure-Boot BL1/BL2 二进制均未随该版本发布。
5. BK7236 官方文档虽公开了同架构的 Manifest/BL1/MCUboot 语义，但它是单核
   BK7236 资料，不能变成 BK7258 的直接实现证据。
6. Beken 技术支持已明确：BK7258 硬件支持 Secure Boot，但当前 SDK 未适配。这是对
   SDK 模板最可靠的解释边界。

## 4. 对“完整逆向”目标的可执行定义

本主线不会因存在模板就宣告完成。只有取得下列每层的直接证据，才能声称已复现
对应层：

| 目标 | 完成所需的最小直接证据 |
|---|---|
| 公开 non-secure boot 链 | normal/A-B binary 的头、分区、校验、remap、最终 handoff 都有二进制或实板证据 |
| Secure BL1 行为 | 可逆向的 BK7258 secure BL1 binary，或厂家提供的同版本源码/规范与可启动样本 |
| Manifest 格式 | 一份已知输入、最终二进制 Manifest、签名结果和 BL1 接受/拒绝对照 |
| BL2 MCUboot ABI | 可逆向的 BK7258 BL2 binary，或厂家提供的同版本实现；含 flash map、CRC/XIP、multi-image 和 handoff |
| OTP/反回滚链 | 可恢复的仿真/只读证据先行；任何 OTP/eFuse 写入需要单独书面授权，且不属于当前阶段 |

在没有 Secure BL1/Manifest/BL2 直接材料前，可以继续做的只有：精确逆向公开
non-secure bootloader、让 board-owned 原型明确标注为原型、整理向厂家索取的最小
材料清单。不能通过猜测 Manifest 或烧写 OTP 来“补齐”证据。

## 5. 可复现来源

- [官方 A/B bootloader Ghidra 逆向](2026-08-07-ghidra-bk7258-ab-bootloader.md)
- [v3.1.1.9 通用 Secure-Boot 模板审计](2026-08-07-bk7258-v3119-secureboot-chain-evidence.md)
- [现有 board-owned BL1/BL2 ABI](2026-08-07-mcuboot-bl1-bl2-abi.md)
- [同族参考与边界](2026-08-07-beken-mcuboot-reference-flow.md)
- [BK7236 v2.0.1 安全文档逐页审计](2026-08-07-bk7236-security-docs-audit.md)

同族参考原始材料（只读，不属于 BK7258 运行时输入）：

- [Beken BK7236 安全启动文档](https://docs.bekencorp.com/arminodoc/bk_idk/bk7236/zh_CN/v2.0.1/security/index.html)
- 本地对应文件：`/home/lijian/project/TuyaOpen/bk_idk/docs/bk7236/zh_CN/security/bk_security_boot.rst`

所有 SDK 文件均来自只读目录
`/home/lijian/project/armino/bk_avdk_smp-release-v3.1.1.9`。没有使用
BK7259、v4.0.1 或修改 SDK/NuttX。
