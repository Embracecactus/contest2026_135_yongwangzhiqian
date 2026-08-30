# BK7258 v3.1.1.9 Secure Boot 链证据

日期：2026-08-07  
状态：SDK 静态证据完成；未启用 Secure Boot，未写 OTP/eFuse，未烧录

## 1. 结论先行

v3.1.1.9 同时包含两条不同的启动路径：

```text
公开 A/B OTA bootloader
  └─ RBL + FNV/hash + appa/s_app remap + trial/confirm
     （官方 ab_bootloader.bin；没有 MCUboot Manifest）

Secure Boot 生成路径（SDK 中的通用脚本/配置残留，非 BK7258 可用适配）
  └─ BL1 Manifest（ECDSA-256）
       └─ BL2 MCUboot image（imgtool 签名）
            └─ primary_all/secondary_all 应用镜像（BL2 验证）
```

因此此前“官方 A/B binary 没有 Manifest”的结论仍成立。SDK 中的脚本和配置只能
证明 Beken 工具链保留了一个通用 Secure Boot 生成接口，不能证明 BK7258 已完成
Secure Boot 适配。这个结论与技术支持“BK7258 硬件支持，但 SDK 尚未适配”的回复
一致。

## 2. BL1 Manifest 生成证据

来源：

```text
tools/env_tools/beken_utils/scripts/bl1_sign.py
tools/env_tools/beken_utils/scripts/genbl1.py
```

`Genbl1` 生成的 `primary_manifest.json` 固定包含：

```json
{
  "key_desc_cfg": {
    "fmt_ver": "0x00010001",
    "mnft_sig_cfg": {
      "pubkey_hash_sch": "SHA256",
      "mnft_sig_sch": "ECDSA_256_SHA256",
      "mnft_pubkey": "bl1_ec256_pubkey.pem"
    },
    "img_dgst_cfg": {"img_hash_sch": "SHA256"}
  },
  "mnft_desc_cfg": {
    "fmt_ver": "0x00010001",
    "mnft_ver": "<security-counter>",
    "sec_boot": true
  },
  "imgs": [{
    "is_enc": false,
    "static_addr": "<BL2 static address>",
    "load_addr": "<BL2 load address>",
    "entry": "<BL2 entry>",
    "path": "bl2.bin"
  }]
}
```

`bl1_sign.py` 调用 SDK 内应存在但未随 v3.1.1.9 源码发布的：

```text
tools/sh_sec_tools/secure_boot_tool
```

动作包括 `manifest_digest`、`gen_manifest_with_signature` 和使用私钥直接生成
Manifest。JSON 只是输入描述，最终产物预期是二进制授权对象。但由于
`secure_boot_tool` 不在 v3.1.1.9 SDK 中，不能据此恢复 BK7258 实际二进制格式。

后续通过一个独立、仅用于取证的通用 IPSS 工具样本确认了 Armino 兼容记录的
**候选形状**（213 字节、`0xa1bc2fd8` magic、`0x00010001` layout、
`0x00030619` 算法标志、原始镜像长度、SHA-256、SEC1 公钥和 `r||s` 签名）。
该样本不是 v3.1.1.9 发布物，也没有 BK7258 BootROM 消费者证据；因此它更新了
“通用 IPSS 格式已能复现”的状态，但不改变“BK7258 官方 ABI 未证明”的结论。
板上验证详见 `verification/2026-08-07-ipss-manifest-format-recovery.md`。

## 3. BL2 MCUboot 生成证据

来源：`bl2_sign.py`、`pack.py`、`steps.py`。官方脚本把 BL2/应用作为 MCUboot
image 签名，参数为：

```text
--public-key-format full --max-align 8 --align 1
--version <version> --security-counter <counter>
--pad-header --header-size 0x1000 --slot-size <partition_size>
--pad --boot-record SPE --endian little --encrypt-keylen 128
```

BL2 的签名输入是 `primary_all_code.bin`，输出包括
`primary_all_code_signed.bin`、`ota_signed.bin` 和 hash 文件。`partition.py`
明确把 `primary_all`/`secondary_all` 作为由 BL2 验证的连续应用容器。

但是 v3.1.1.9 缺少：

```text
tools/env_tools/beken_utils/tools/mcuboot_tools/imgtool.py
```

所以脚本证明了通用接口和参数，却不能在当前 SDK 内独立生成最终 BL2 签名镜像，
更不能证明 BK7258 的官方 BL2 已经可用。不能将其他版本的 `imgtool.py` 混入
v3.1.1.9。

## 4. 分区和控制数据证据

`partition.py` 只有在 `secureboot_en=True` 时才允许以下保留分区：

```text
bl1_control
primary_manifest
secondary_manifest
bl2
primary_all
secondary_all
```

`bl1_control.json` 给出了三页控制结构（每页 4 KiB）：

```text
页 0（0x0000）：BL2 初始 MSP、PC
页 1（0x1000）：Boot control
页 2（0x2000）：OTP simulation / debug control
```

页 1 的字段顺序为：

| 偏移 | 字段 |
|---:|---|
| `0x1000` | magic = `63 54 72 4c`（JSON 值 `6354724C`） |
| `0x1004` | `boot_flag` |
| `0x1008` | `primary_manifest_addr` |
| `0x100c` | `recovery_manifest_addr` |
| `0x1010` | `pll_ena` |
| `0x1014` | `security_boot_supported` |
| `0x1018` | `security_boot_ena` |
| `0x101c` | `security_boot_print_dis` |
| `0x1020` | `jtag_dis` |
| `0x1024` | `sw_fih_delay_ena` |

默认 v3.1.1.9 BK7258 配置为：

```text
security_boot_supported = 1
security_boot_ena       = 0
secureboot_en           = FALSE
```

这与技术支持回复吻合：硬件能力标志存在，但 SDK 默认关闭且没有完整适配。另外，
`bl1_control.json` 中的 Model ID 为 `7236`，进一步说明它更像跨芯片通用模板/遗留
配置，不能作为 BK7258 已适配的证据。

## 5. OTP 字段模板（不是 BK7258 实际授权链证据）

SDK 生成的 `otp1.csv` 声明：

```text
OTP_BL1_BOOT_PUBLIC_KEY_HASH   offset 0x128, size 32
OTP_BL2_BOOT_PUBLIC_KEY_HASH   offset 0x148, size 32
OTP_BL1_SECURITY_COUNTER       offset 0x188, size 4
OTP_BL2_SECURITY_COUNTER       offset 0x200, size 64
```

这些名称说明通用工具链预留了如下**设计意图**：

```text
OTP BL1 key hash + BL1 counter
      ↓ 验证
BL1 Manifest / BL2
      ↓ BL2 MCUboot 验证
primary_all / secondary_all
```

但不能据此写成“BK7258 的 ROM/BL1 确实按这条链读取这些地址”。原因是：这些
字段来自未启用的通用模板，且同一模板的 Model ID 是 `7236`；v3.1.1.9 没有提供
可对照的 BK7258 Secure-Boot BL1、Manifest 样本或签名工具。技术支持已明确说明
BK7258 SDK 尚未适配 Secure Boot，这一边界优先于模板推断。

本项目当前不读取、不写入、不模拟 OTP/eFuse 烧毁流程；这些字段只作为“工具链曾
声明过的字段名和长度”，不是硬件行为的证明。

## 6. 当前逆向边界

已经确认：

1. 官方 A/B bootloader 是独立的 RBL OTA 路径；
2. SDK 通用工具链预留了 BL1 Manifest 和 BL2 MCUboot 的生成接口；
3. Manifest 的 JSON 描述字段、版本/计数器、ECDSA-256、SHA-256、BL2 地址和入口
   已从 v3.1.1.9 脚本获得；
4. BL2 MCUboot 的官方命令行参数已获得；
5. 真正的 `secure_boot_tool`、官方 v3.1.1.9 `imgtool.py`、BK7258 Secure Boot
   BL1/BL2 binary 和可启动签名样本均缺失；因此 BK7258 Secure Boot 仍属于
   “硬件支持、SDK 未适配”。

尚不能声称：

- 已恢复 Manifest 二进制的精确 TLV/字节布局；
- 已恢复 BK7258 BL1 对 Manifest 的逐字段验证代码；
- 已恢复 BK7258 BL2 的官方 MCUboot fork；
- 当前板子已经进入 Secure Boot 状态。

后续仍只做官方链路逆向和 board-owned 实现，不修改 SDK/NuttX，不使用 BK7259/v4
工具，不写 OTP/eFuse。N17 暂停，待本链路结论完成后再恢复。
