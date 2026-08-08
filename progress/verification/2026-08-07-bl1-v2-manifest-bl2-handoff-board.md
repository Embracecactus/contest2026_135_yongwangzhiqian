# BK7258 自有 BL1 Manifest v2 与 BL2 交接板测

日期：2026-08-07

## 结论

board-owned BL1 Manifest 已从早期 v1 记录升级为 v2 记录，并在开发板上实测
通过：BL1 完成 Manifest 字段检查、BL2 SHA-256、Manifest 公钥哈希锚定和
P-256 签名验证，复制 BL2 到 `0x28020000`；BL2 的 pinned NuttX MCUboot
完成当前 CP/AP 成对镜像的验证和 CP 交接，现有 CP 镜像成功进入 NuttShell。

这次结果仍然不是 Beken 官方 BK7258 Secure Boot/Manifest ABI 的逆向结果。它是
项目自有、可整体恢复的软件链；没有改 SDK/NuttX/apps，也没有写 OTP/eFuse。

## 本次实现

Manifest 固定在 boot 逻辑尾部 `0x0200ff00..0x0200ffff`，256 字节、全小端：

| 偏移 | 内容 |
| --- | --- |
| `0x00` | `BKBL1M2\0` |
| `0x08..0x2f` | format、ECDSA-P256/SHA-256 算法号、key id、版本、BL2 XIP/长度/加载地址 |
| `0x30..0x4f` | `SHA-256(BL2 的 0x3000 字节逻辑镜像)` |
| `0x50..0x6f` | `SHA-256(public-key X||Y)`，同时与 BL1 编译时 root hash 比较 |
| `0x70..0xaf` | Manifest 中携带的 P-256 `X||Y` 公钥 |
| `0xb0..0xef` | 对 `0x00..0xaf` 的 ECDSA-P256 `r||s` 签名 |
| `0xf0..0xff` | 必须保持擦除值 `0xff` |

本次生成的固定字段为：BL2 XIP `0x024d0000`、逻辑长度 `0x3000`、SRAM
加载地址 `0x28020000`、image version `1`。Manifest SHA-256 为
`b3a6652d4e46fc126b30d25bdc427b961253d228cc39cc74a8996475bedf0d65`；BL2
逻辑 `bl2.bin` SHA-256 为
`99ee9e2df800dcd30448d2e527a58cbfef8bcac7cc52bd8cc3c95da6e75cd1a1`，其
`0x3000` 填充摘要写入 Manifest 的 `0x30` 字段。

独立主机检查重新计算了填充后的 BL2 摘要和公钥哈希，并用
`cryptography` 的 P-256 verifier 验证 `0x00..0xaf` 的签名；CRC 解码后，boot
逻辑尾部与该 Manifest 逐字节相同，结果均为 `PASS`。

## 交接问题与修复

第一轮 v2 boot 只写 boot 时，串口能重复看到 `BL2RAM`，说明 BL1 Manifest
已经通过；随后 BL2 交接 CP 后复位。J-Link 只读现场得到：

```text
CP vector MSP = 0x280146c0
SCB CFSR      = 0x00100000
SCB HFSR      = 0x40000000
SCB MMFAR     = 0x28020454
```

`CFSR` 的 `STKOF` 表示 ARMv8-M 栈下界违规。BL2 原先把公共官方直启路径的
`MSPLIM=0x2802f800` 带给了本项目 CP；但当前 MCUboot CP linker 的 RAM 起点是
`0x28010000`，初始 MSP `0x280146c0` 落在该下界之下，因此 CP 一使用栈便触发
HardFault/WDT 复位。这不是签名、摘要或 MCUboot `boot_go()` 失败。

修复为 BL2 最终跳转前设置 `MSPLIM=0x28010000`。BL1 和 BL2 自身运行时的
`0x2802f800`/`0x28020000` 保护边界没有改变；只有 board-owned CP handoff
采用 CP linker 的真实 RAM ABI。

## 板端证据

匹配的 boot 与 BL2 通过 COM7 下载：

```text
boot: 0x00000000..0x00010fff (0x11000 bytes)
bl2 : 0x0051d000..0x005202ff (0x3300 bytes)
```

下载器 `bk_loader 2.1.11.15` 两段均报告 `WriteFlash ->pass`、`Writing Flash OK`。
随后使用 COM7 RTS 复位并在 COM11/460800 捕获到：

```text
u_bootloader enter
BClk A5=8407A76C A9=787BC8A4
partition bl2 @ 0x024D0000
bl2 ram @ 0x28020000
BL2RAM
B2INIT
B2GO
B2GORET
B2GOOK
B2APOK
B2HANDOFF
[hal]
[gpio_log]:gpio:0 was busy: device num:0x21!
gpio: 0 is used.Please confirm unmap isn't impact is working module.!

NuttShell (NSH)
nsh>
```

`B2GORET/B2GOOK` 证明 MCUboot `boot_go()` 返回成功；`B2APOK` 证明选中槽的
AP 向量检查成功；`B2HANDOFF` 后进入 NSH，证明修正后的 MSPLIM 交接可运行。
`ipc_svr create_socket failed` 是当前应用服务层提示，不影响本次 BL1/BL2/NSH
链路结论。

捕获文件位于本机临时目录 `/tmp/bk7258-msplim-rts-20260807-145635/`，其中
`serial.raw` SHA-256 为
`77da42a1d0a59dbed0f43697095e78e343f83dc7db059c693bb136f0bf813f65`。

## v2 负向与恢复

为了验证不是“只要 CRC 正确就放行”，临时将 Manifest 签名 `0xb0` 的最低位
翻转，重新做完整 32+2 CRC，只刷 boot `0x0..0x10fff`。COM7 RTS 复位的
18 秒捕获只有：

```text
u_bootloader enter
BClk A5=8407A76C A9=787BC8A4
partition bl2 @ 0x024D0000
bl1 manifest rc 0x00000003
BAD
bl2 manifest
```

没有 `BL2RAM`、`B2INIT` 或 NuttShell，证明 v2 签名拒绝发生在 BL2 复制之前。
临时无效捕获 `serial.raw` SHA-256 为
`d2480e7ba4d3a09e0e3e41d28196920231083c2a60e4430578a92bed78e69829`。

随后立即恢复同一份有效 v2 boot，只写回 `0x0..0x10fff`；再次 RTS 复位得到
本记录上一节的 `B2GOOK/B2APOK/B2HANDOFF/NuttShell` 序列。恢复捕获与上一节
逐字节相同，证明负向测试可恢复且没有触碰 BL2、CP/AP 或数据分区。

## 范围和下一步

- 本次没有重刷 CP/AP，因此只证明匹配现有 A 槽镜像的 BL1→BL2→MCUboot→CP
  交接；完整 A/B 重新打包验证仍按单独记录执行。
- `B2*` 是临时、板级 bring-up 标记，后续稳定化时可保留为低成本诊断或移除，
  不能当作官方日志 ABI。
- 后续先把 v2 Manifest 与 CP handoff 的文档/构建入口统一，再回到 CP/AP
  成对镜像的独立签名下载验证；N17 metadata、anti-rollback、OTP/eFuse 仍关闭。

## 契约化重建结果

在上述板测之后，重新以统一参数 `BL2_LOGICAL_SIZE=0x3000` 构建了 BL2、Manifest
和 BL1：

```text
BL2 raw       : 9452 bytes
BL2 logical   : 0x3000 (12 KiB, FF padded before signing/CRC)
BL2 physical  : 0x3300 (32+2 CRC)
BL2 capacity  : 0x20000 logical / 0x22000 physical
BL1 physical  : 0x11000
```

当前重建产物 SHA-256：

```text
bl_crc.bin                              aa6379700a7e6c69eaa62d7625b0d267f0d57ff900bce466f1fac293d2bbe6e9
bl2.bin                                 4289886989ebcbe52e98d5812657f981925bea949c5b4e2903de5633c03d92f1
bl2_crc.bin                             5bf8d12cc2c0fc88e95ec58e87cdc62e41fb06086a0fada5d5ca7f673d4eaab2
BKBL1M2 Manifest                        4ea21c4a98a2f59b1a8b7d9f82eb51f8b0ca92908a39b764cbb5468506d61de1
```

主机重新验证了 Manifest 的公钥哈希、ECDSA 签名和 `0x3000` 填充后的 BL2 摘要，
并重新解码 `bl_crc.bin` 确认 Manifest 仍位于 boot 逻辑尾部。该次重建随后用于
板端回归，替代了前一节的旧构建捕获。

COM7 下载器报告 boot `0x11000` 和 BL2 `0x3300` 两段均 `WriteFlash -> pass`。
随后 COM7 RTS 复位、COM11/460800 捕获到：

```text
u_bootloader enter
BClk A5=8407A76C A9=787BC8A4
partition bl2 @ 0x024D0000
bl2 ram @ 0x28020000
BL2RAM
B2INIT
B2GORET
B2GOOK
B2APOK
B2HANDOFF
NuttShell (NSH)
```

新捕获目录为 `/tmp/bk7258-v2-contract-rts-20260807-1517/`，
`serial.raw` SHA-256 仍为
`77da42a1d0a59dbed0f43697095e78e343f83dc7db059c693bb136f0bf813f65`。
因此本次代码整理同时通过主机验证和板端回归。

## 与 BK7236 官方网页指导的差异

网页给出的是 BK7236 单核安全架构的职责和分区语义；它不是 BK7258 已公开的
二进制 ABI。当前实现按语义对齐，但以下差异必须保留在设计边界内：

| 事项 | BK7236 网页指导 | 当前 BK7258 项目 | 结论 |
| --- | --- | --- | --- |
| BL1 根 | 芯片固化 BL1/BootROM，使用 OTP 中的 ROTPK hash 和版本下限 | board-owned BL1，根公钥 hash 编译进软件，未写 OTP/eFuse | 可恢复开发链，不是硬件 Secure Boot |
| Manifest | 厂商定义的授权记录，真实字段/签名覆盖范围由 BootROM 解释 | `BKBL1M2`、256 字节、自定义字段和 ECDSA-P256/SHA-256 | 只复刻职责，不宣称复刻格式 |
| BL2 | MCUboot 1.9.0，位于独立 128KiB `bl2` 分区 | NuttX 仓库内 pinned MCUboot bootutil，裸机 BL2 放在 CP 侧 SRAM | 主体职责一致，载体和版本需独立验证 |
| BL2 容量 | 128KiB 逻辑容量（CRC/AES 后有物理扩展） | 同样预留 128KiB/136KiB；当前实际镜像只签名、复制 `0x3000/0x3300` | 容量对齐，活动长度不固定 |
| 后续镜像 | TF-M/SPE + CPU0 APP，单核映射 | CP/AP 成对 NuttX 镜像，AP 保持 SMP，当前 direct-XIP | 双核映射是 BK7258 专有适配 |
| 安全硬件 | TrustEngine、PPC/MPC/TrustZone、FIA、OTP 共同参与 | 当前只使用已验证的 32+2 CRC/XIP 和软件 TinyCrypt 验签 | 硬件 ABI 尚未证实，不能跨芯片套寄存器 |
| 防回滚 | OTP/NV security counter 与 BL2 `IMAGE_TLV_SEC_CNT` | 当前只保留软件版本字段；OTP/eFuse/N17 关闭 | 语义待接入，不能称量产防回滚 |
| Flash 安全 | 文档包含 AES、CRC、对齐、签名顺序 | 当前只接入实测 CRC/XIP；AES/key ladder 未开启 | 不能由 BK7236 文档推断 BK7258 开关状态 |
| 启动失败处理 | 固化安全错误和生命周期策略 | 软件 fail-closed、看门狗复位、可用 USB/J-Link 恢复 | 开发期等价目标，不是量产生命周期 |

因此当前没有把 7236 网页“照搬”到 7258：我们借用了它的职责检查表，已实测的
BK7258 规则才进入代码；未知的 Manifest 字节格式、TrustEngine/OTP/FIA 寄存器和
CP/AP 官方配对规则仍明确标为未证实。详细逐页审计见
[`2026-08-07-bk7236-security-docs-audit.md`](2026-08-07-bk7236-security-docs-audit.md)。
