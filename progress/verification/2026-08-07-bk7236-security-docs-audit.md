# BK7236 v2.0.1 System Security 文档审计

日期：2026-08-07
状态：只读资料审计；未修改 SDK/NuttX；未写 OTP/eFuse；未启用 Secure Boot。

## 1. 审计范围与来源

用户提供的官方入口：

<https://docs.bekencorp.com/arminodoc/bk_idk/bk7236/en/v2.0.1/security/index.html>

由于网页抓取不稳定，本次使用本机对应的完整文档树交叉阅读：

```text
/home/lijian/project/TuyaOpen/bk_idk
branch: release/v2.0.1
commit: 650e754e12fe1e43c37ce2316a973668b033fd48
docs:   docs/bk7236/en/security/
```

该目录的 `index.rst` 列出 14 个子页，均已阅读。页面引用的图也逐个核对：10 个
SVG（读取图中文字、颜色/连线语义）和 3 个 PNG（逐图查看）。本记录只总结含义，
不复制 SDK 文件到当前工程。

## 2. 结论先行

这套文档提供了目前最完整的 Beken/Armino 安全启动“职责语义”参考：

```text
芯片固化 BL1 / BootROM
        │  读取并验证 Manifest、BL2 哈希、版本下限
        ▼
BL2 / MCUboot
        │  验证后续签名镜像、TLV 公钥/哈希、安全计数器
        ▼
TFM-S + CPU0 APP
```

Beken 支持人员说明 BK7258 与 BK7236 属于同一安全架构，但 BK7236 是单核。因此：

- 可以把 BL1→Manifest→BL2、OTP 根密钥绑定、SHA-256、版本下限、MCUboot TLV、
  TrustEngine、PPC/MPC/TrustZone 等作为**同架构语义参考**；
- 不能把 BK7236 的地址、寄存器、BootROM 字节、单核 `TFM-S/CPU0 APP` 镜像关系、
  Manifest 二进制布局直接当成 BK7258 事实；
- 文档没有公开 BK7258 v3.1.1.9 的 Secure-Boot BL1/Manifest/BL2 可启动样本，
  也没有解决 BK7258 双核 CP/AP 的官方镜像配对方式；
- 因此当前项目的 CP/AP 双镜像 MCUboot 和开发期 BL1 Manifest 仍是
  **board-owned、可恢复适配**，不能标为“官方 BK7258 Secure Boot 已完整逆向”。

## 3. 每个子页面的审计结果

| 子页 | 文档实际内容 | 对 BK7258 逆向的用途 | 不可直接外推的部分 |
|---|---|---|---|
| `index.rst` | 列出 Glossary、认证、Overview、TrustEngine、Secure Boot、Secure Debug、LCS、UID、OTP、EFUSE、FLASH AES、FIA、Secure Program、漏洞披露 14 页 | 确认官方安全资料的边界和术语入口 | 目录本身不是实现证据 |
| `bk_security_glossary.rst` | 定义 S/NS、S/NS-Aware、P/NP、M33、TZ、PPC、MPC、BL1=BootROM、BL2=MCUboot、LCS | 统一 BL1/BL2 和 TrustZone 术语 | 没有 BK7258 地址或寄存器 |
| `bk_security_cert.rst` | 声称 BK7236 正在 PSA-L2/SESIP-L2 认证；列出 Security ID、生命周期、认证、隔离、安全启动、安全升级、防回滚、安全接口和安全服务 | 说明厂商安全目标包含防回滚和生命周期 | “正在认证”不是 BK7258 认证结论，也不是可启动样本 |
| `bk_security_overview.rst` | 介绍 CM33 TrustZone；M33 的 IDAU/SAU→MPU，SoC 侧 PPC→MPC→外设最终 S/NS 检查；描述 FLASH、DMA、AHB master；软件层分为不可升级 BL1/安全硬件、可升级 BL2/TFM、安全服务和 NSPE OS/应用；BL2 使用 MCUboot、SPE 使用 TF-M 1.6.0 | 给出硬件隔离和软件分层的整体模型 | BK7258 是双核，不能据此假定两个核共享同一 S/NS 组织或相同 MPC 表 |
| `bk_security_enginee.rst` | TrustEngine 同时有安全 host 和普通 host；提供 AES/SM4、SHA/SM3、RSA/ECC/SM2、密钥阶梯、LCS、TRNG、OTP；描述 SCA、ACA、HASH、OTP controller、TRNG 五大块和 OTP 锁 | 解释 BL1/BL2 为什么可能通过硬件加速验签/哈希，及 OTP 根密钥访问的硬件边界 | TrustEngine 的 BK7258 寄存器、命令 ABI、密钥阶梯配置未由该页证明 |
| `bk_security_boot.rst` | 明确两阶段安全启动：BL1 先验 BL2，BL2 再验其他镜像；给出 Manifest 语义、BL2 MCUboot 1.9.0、CRC/AES/对齐后再签名、密钥和分区配置步骤 | 当前最重要的同架构流程证据；支持 BL2 128 KiB 容量解释 | 它描述 BK7236 单核安全样例，不给 BK7258 Manifest 字节格式或双核映射 |
| `bk_security_debug.rst` | 量产默认关闭调试，故障分析时通过认证重新开启；由设备 Secure Debug Agent、主机 Secure Debug Tool、Secure Debug Server 三部分组成 | 说明 SWD/JTAG 安全开关与云端授权属于安全生命周期的一部分 | 没有 BK7258 调试授权协议；当前开发板不得按该页烧熔丝关闭 SWD |
| `bk_security_lcs.rst` | 页面正文为 `TBC` | 只能与 TrustEngine 页中 CM/DM/DD/DR 的说明相互参照 | 没有完整 LCS 转换表或实现细节 |
| `bk_security_uid.rst` | 页面正文为 `TBC` | 不能据该页设计 BK7258 UID 读取 | PUF/UID 的具体接口未公开 |
| `bk_security_otp.rst` | PUF 2 KiB、OTP 8 KiB、OTP2 24 KiB；给出 UID、熵池、AES key、BL1/BL2 ROTPK hash、BL2/app security counter 的物理偏移 | 是 OTP 地址与密钥/计数器字段的最清晰表格来源 | 表中是 BK7236 OTP map；不能读取或写入 BK7258 对应位置 |
| `bk_security_efuse.rst` | 32 个一次性 0→1 bit：安全启动、模式、调试、时钟、随机延时、直接跳转、FIA NMI、SPI→AHB、Flash AES、SPI 下载、SWD 等；给出错误码和部署顺序 | 明确“启用安全启动后不可回退”的风险；说明为什么当前只做可恢复验证 | 不能把 bit 编号/电气行为当 BK7258 结论，且当前项目禁止写 OTP/eFuse |
| `bk_flash_aes.rst` | 命令口负责 CRC 去除/AES 解密，写入反向处理；指令口用于取指、BL1/BL2 安全启动、OTA；物理地址公式 `physical=(virtual/32)*34` | 与当前 BK7258 32+2 CRC/XIP 逆向直接相关，解释逻辑/物理地址双视图 | AES 是否已在 BK7258 v3.1.1.9 开启仍需本机证据；不得因公式相同就宣称密钥/寄存器相同 |
| `bk_security_fih.rst` | BL1/BL2/TFM-S 用随机延时、复杂布尔值、随机 memcpy/memcmp；硬件用比较寄存器锁住公钥 hash 前 16 bit 的判断 | 提醒完整 Secure Boot 不只是 ECDSA 调用，还包括故障注入防护 | BK7258 比较寄存器位置和实现未公开 |
| `bk_security_programming.rst` | 正文为 `Coming soon...` | 不能从该页得到安全编码规范 | 无可复用实现 |
| `bk_security_bug_report.rst` | 指向 Beken 漏洞报告流程，声明 BK7236 尚无公开漏洞 | 仅作为厂商流程信息 | 不提供启动链技术证据 |

## 4. Secure Boot 页面给出的可核对语义

### 4.1 BL1 的职责

官方流程图 `bl1_verify_2.svg` 中标出了 `bl1_control`、`manifest`、`bl2`、
`public key`、`image info/algorithm info`、`image hash`、`image version`、
`manifest signature` 和 OTP 公钥 hash。正文将流程分为四步：

1. 从固定 Flash 位置读取 Manifest；读取其中的算法、版本、公钥等信息。
2. 计算 Manifest 中公钥的 hash，与 OTP 中的 BL1 ROTPK hash 比较；再用该公钥验证
   Manifest（签名不包含 Manifest signature 自身）。
3. 按 Manifest 指定算法（文档写明 SHA-256）计算 BL2 hash，与 Manifest 的 image hash
   比较。
4. 检查 image version 不低于 OTP 版本下限，成功后跳转 BL2。

这说明 Manifest 是厂家定义的**授权描述**，不是简单的 MCUboot image header；当前
项目的 256-byte board-owned Manifest 只能作为可恢复开发层，不能称为厂商格式复刻。

### 4.2 BL2 的职责

`bl2_verify.svg` 将 `tfm_s_crc_aes` 和 `cpu0_app_crc_aes` 放在各自分区，使用
`0xFF` padding 保证 Flash CRC 块和 Cortex-M vector 对齐，再由 MCUboot image header、
普通 TLV、Protected TLV 和 Trailer 组成签名对象。正文列出了 MCUboot 验签要点：

- `IMAGE_TLV_PUBKEY` 或 `IMAGE_TLV_KEYHASH` 与 OTP 公钥 hash 对比；
- `IMAGE_TLV_SHA256` 与合并 image hash 对比；
- 用公钥验证 `EXPECTED_SIG_TLV`；
- `IMAGE_TLV_SEC_CNT` 不低于 NV security counter。

文档明确 BK7236 BL2 为开源 MCUboot 1.9.0；这支持“BL2 使用 MCUboot 主体”的架构
判断，但不证明当前仓库的 NuttX pinned 版本必须等于 1.9.0。

### 4.3 安全分区和 128 KiB BL2

BK7236 安全启动最小分区表为：

| 分区 | 逻辑大小 | 文档职责 |
|---|---:|---|
| `bl1_control` | 12 KiB | BL1 控制、重启跳转、OTP 模拟、调试等 |
| `manifest` | 4 KiB | BL1 验签 BL2 的授权信息 |
| `bl2` | 128 KiB | MCUboot |

因此先前“完整 128 KiB BL2 可以跨 SRAM bank 复制并启动”的验证与同族安全分区容量
是相符的；它仍然不能把当前 BK7258 公共 normal bootloader 的 64 KiB 逻辑槽扩大。

### 4.4 CRC/AES 与签名顺序

文档给出的 BK7236 构建顺序是：

```text
原始 tfm_s/app
  → AES 加密
  → 32+2 CRC 编码
  → 分区/CRC/vector 对齐 padding
  → 合并 image
  → MCUboot 签名
```

这解释了为什么不能“先简单拼两个 BIN 再签名”。当前 BK7258 项目只把已实证的
32+2 CRC/XIP 规则接入 board-owned flash backend；AES 开关和 OTP key 未启用，不能
从 BK7236 文档推断为当前运行时事实。

## 5. TrustEngine、OTP、EFUSE 的关键关系

### 5.1 TrustEngine 的两种 host

TrustEngine 图 `security_te200.svg` 与架构图 `security_te200_architecture.png` 显示：

- APB_S 进入 APB Demux，连接 HASH/SCA/ACA 命令队列和状态；
- SCA、ACA、HASH 引擎分别连接管理器，ACA 旁边有 ACA SRAM，SCA 通过 Key Ladder；
- OTP interface 与 LCS Manager 连接 secure/non-secure OTP；
- AHB 侧有 DMA/Arbitration；HASH/SCA/ACA 可产生 Secure IRQ 或 Normal IRQ；
- TRNG 由内部熵源或外部熵源产生随机数，放入独立的随机数池；
- TrustEngine 作为 security host 支持 BL1/BL2/TFM-S，作为 normal host 支持普通应用。

### 5.2 两套 OTP 地址视图不能混用

`bk_security_enginee.rst` 给出 TrustEngine 逻辑布局：`model_id`、`model_key`、
`device_id`、`device_root_key`、`secure_boot_pk_hash`、`secure_debug_pk_hash`、
`LCS` 等从逻辑偏移 `0x0000` 开始。

`bk_security_otp.rst` 给出 OTP/PUF 物理布局：PUF/UID 从 `0x300`/`0x380`，硬件区从
`0x400`，Security Engine 区从 `0x500`；BL1 ROTPK hash 在 `0x528`，BL2 ROTPK hash
在 `0x548`，BL2 counter 在 `0x588`，application counter 在 `0x600`。

一个合理但尚未得到 BK7258 证据的解释是：TrustEngine 表是以安全引擎区起点为逻辑
基址，而 OTP 表是物理偏移（例如 `0x500 + 0x28 = 0x528`）。当前记录把它标为
**推断**，不会把它写进 BK7258 代码。

### 5.3 EFUSE 是不可逆部署开关

BK7236 文档描述的关键 bit 关系：

- bit 3 选择传统下载/安全启动模式；bit 0 在安全模式下真正启用 BL1 安全验证；
- bit 1/7 控制普通/严重错误日志；bit 2 控制 Deep Sleep 是否跳过完整安全启动；
- bit 4 选择 XTAL/PLL 安全启动时钟；bit 5 打开随机延时；bit 6 选择直接/间接跳转；
- bit 20 启用故障注入检测 NMI；bit 21 关闭 SPI→AHB；bit 29 启用 Flash AES；
- bit 30 关闭 SPI 下载；bit 31 关闭 SWD。

文档要求先烧录并验证稳定版本，再最后启用安全启动相关 bit；启用后不能再通过普通
BKFIL 下载 BL2。这个风险正是当前项目保留 J-Link/SWD、USB 串口和可恢复 board-owned
验证路径、禁止 OTP/eFuse 写入的原因。

## 6. 图片文字与流程图审计

| 图片 | 图中文字/结构 | 读图结论 |
|---|---|---|
| `bl1_overview.svg` | `BL1 (BootROM)`、`BL2 (MCUBOOT)`、`TFM_S`、`CPU0 APP`、三处 `Verify` | 两阶段链：BL1 验 BL2，BL2 再验 TFM-S/CPU0 APP |
| `bl1_verify_2.svg` | `bl1_control`、`manifest`、`bl2`、`public key`、`image info/algorithm info`、`manifest signature`、`image hash`、`image version`、`OTP`、步骤 1–4 | Manifest 是 BL1 的授权和描述输入；OTP 提供根公钥 hash/版本锚点 |
| `bl2_verify.svg` | `tfm_s_crc_aes`、`cpu0_app_crc_aes`、`CRC/CM33 Vector align padding`、`padding 0xFF (4K)`、`BL2 Header (4K)`、`BL2 TLV`、`BL2 Protected TLV`、`IMAGE_TLV_PUBKEY`、`IMAGE_TLV_SHA256`、`IMAGE_TLV_KEYHASH`、`EXPECTED_SIG_TLV`、`IMAGE_TLV_SEC_CNT`、`NV Security counter`、`BL2 Trailer` | 签名对象必须包含经过 CRC/AES/对齐处理的组合 image；TLV 和计数器由 BL2 检验 |
| `security_hw_arch.svg` | `CM33 With TrustZone`、`PPC`、`MPC`、`AHB Masters`、`AHB Bus Matrix`、`Security Peripherals`、`S/NS Aware Peripherals`、`Memory`、`block 1 S`、`block 2 S`、`block 3 NS` | 硬件安全属性由 CPU/总线、PPC、MPC 和外设多层共同决定 |
| `security_access.svg` | `AHB Masters`→`IDAU/SAU Check`、`MPU Check`、`PPC Check`、`MPC Check`、`Device Check (S/NS Aware Peripheral)`，沿途携带 `S/NS` | 一次访问会经过 CPU/总线到设备的串行安全检查 |
| `security_idau.svg` | 地址刻度 `0x00000000` 至 `0x70000000`，交替标注 `S`/`NS` 区块 | 图示 IDAU 地址属性分区；没有给出 BK7258 的对应地址表 |
| `security_flash.svg` | `CM33`、`FLASH Controller`、`SPI`、`FLASH`，路径标注 `code`/`data` | 代码取指走 Flash Controller/指令路径，数据可走数据路径；CRC/AES 由控制器处理 |
| `security_sw_arch.svg` | `OTP/EFUSE`、`TE200`、`CM33 TrustZone`、`BL1 (BootROM)`、`BL2 (MCUBOOT)`、`SPE (TF-M)`、可信服务、`Driver`、`OS`、`Applications`、`Immutable`、`Updatable`、`SPE`、`NSPE` | 把不可变安全根、可升级安全栈和非安全应用分层展示 |
| `security_otp_layout.svg` | `PUF`、`Random Bits (1024 Bits)`、`UID (1024 Bits)`、`OTP`；边界 `0x300`、`0x380`、`0x400`、`0x500`、`0x580`、`0x600`、`0x800`；分区 `Hardware (256 Bytes)`、`Security Engine (128 Bytes)`、`Software Non-Security (128 Bytes)`、`Software Security (512 Bytes)` | 图形化确认 OTP/PUF 逻辑分段和物理偏移表 |
| `security_te200.svg` | `CM33 With TrustZone` 的 `Secure World`/`Normal World` 通过 `AHB Bus Matrix` 访问 `TE200`；`OTP1`、`MPC`、`OTP2`、`AHB/APB`；标注 `Secure host`/`Normal host` | TrustEngine 同时服务安全世界和普通世界，但访问属性不同 |
| `security_te200_architecture.png` | `APB Demux`、HASH/SCA/ACA `CMD Queue`/`Status`、`HASH/SCA/ACA Management`、`Key Ladder`、`ACA Engine/ACA SRAM`、`OTP Interface`、`LCS Manager`、`Random Number Pool`、`TRNG`、`DMA`、`Secure OTP`、`Non-Secure OTP`、`Secure IRQ`/`Normal IRQ` | 展示 TrustEngine 内部硬件模块和安全/普通 OTP 分流 |
| `otp_region.png` | `System AHB Side`、`System APB Side`、`Crypto APB Side`；`OTP2`、`PIF`、`PTR`、`PTC`、`RNG`、`INT`、`PMK`、`PTM`、`PUF`、`OTP`；图上有 `CONFIDENTIAL` 水印 | 展示不同总线侧对 OTP/PUF/PIF 的可见区域；不能据图推导 BK7258 总线地址 |
| `security_debug.png` | `Secure Debug SaaS`经 HTTPS 连接`Secure Debug Tool`；`Communication Channel`经过 `Plug-in` 到设备 `Secure Debug Agent`/`Debug Enabling`；独立 `Debug Channel` 连接普通 `Debug Tool`；图例 `IPSS Release`/`Customer Implementation` | Secure Debug 是设备 agent、主机工具、服务器和客户插件组成的授权平面 |

## 7. 对当前 BK7258 主线的具体影响

1. **128 KiB BL2 容量结论保持。** BK7236 官方安全分区也把 BL2 设为 128 KiB，
   与当前跨 64 KiB bank 的 BL2 复制验证方向一致；当前 BK7258 BL1 64 KiB 仍由公共
   normal bootloader 槽边界决定。
2. **Manifest 不能再被当作“随便定义的头”。** 语义上至少要覆盖算法、版本、公钥、
   BL2 hash、Manifest 自签名和根公钥绑定；但厂商格式仍未知，所以 board-owned
   Manifest 必须继续标注为自定义恢复实现。
3. **BL2 MCUboot 的职责划分正确。** 当前 NuttX MCUboot 负责 CP/AP image 验证和
   direct-XIP 选择；BL1 只做 board-owned BL2 授权，不把 BK7236 单核 TFM/CPU0 映射
   伪装成 BK7258 官方 CP/AP 映射。
4. **32+2 CRC 不是普通文件 CRC。** 文档明确它影响指令口物理地址和签名对象对齐；
   当前 flash backend 的 logical/XIP 视图与 CRC-expanded 下载视图必须继续分开。
5. **安全硬件暂不接入。** TrustEngine、OTP、EFUSE、PPC/MPC、FIA 比较寄存器是待
   BK7258 直接证据；当前不改 SDK/NuttX、不写 OTP/eFuse、不关闭 SWD/SPI。

## 8. 仍然缺失的 BK7258 直接证据

下面这些问题，BK7236 文档只能提供问题清单，不能提供答案：

- BK7258 Secure-Boot BL1/BootROM 是否读取同一 Manifest 字段和同一 OTP 视图；
- BK7258 Manifest 的实际二进制字段、大小、签名覆盖范围、编码端序和 CRC/AES 位置；
- BK7258 TrustEngine/ACA/HASH 命令寄存器、PPC/MPC/IDAU 映射和 FIA 比较寄存器；
- BK7258 双核 CP/AP 是两个 MCUboot image、一个组合 image，还是厂商另有配对层；
- BK7258 v3.1.1.9 的 secure `bl2_sign.py` 所引用的缺失工具和最终 package；
- BK7258 的 OTP/eFuse 位编号与官方安全生命周期。

因此当前可执行边界仍是：继续逆向公开 BK7258 non-secure bootloader，维护
board-owned 可恢复 BL1→BL2→MCUboot 原型，并把 BK7236 文档作为字段/职责检查表；
不能用跨芯片文档替代 BK7258 secure binary，也不能通过烧 OTP/eFuse 试错。

## 9. 可复现来源

- 官方入口：<https://docs.bekencorp.com/arminodoc/bk_idk/bk7236/en/v2.0.1/security/index.html>
- 本地文档树：`/home/lijian/project/TuyaOpen/bk_idk/docs/bk7236/en/security/`
- BK7236 分区语义：`/home/lijian/project/TuyaOpen/bk_idk/docs/bk7236/zh_CN/developer-guide/config_tools/bk_config_partitions.rst`
- BK7236 安全升级补充：`/home/lijian/project/TuyaOpen/bk_idk/docs/bk7236/zh_CN/developer-guide/bootloader_ota/bk_security_ota.rst`
- 当前项目官方启动链证据矩阵：`progress/verification/2026-08-07-bk7258-boot-chain-evidence-matrix.md`
- 当前项目同架构 MCUboot 参考：`progress/verification/2026-08-07-beken-mcuboot-reference-flow.md`
