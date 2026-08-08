# ADR-016: 自研且可恢复的 BL1 Manifest 边界

- Status: Accepted for the current BL1 -> BL2 -> MCUboot work
- Date: 2026-08-07

## Decision

由于 Beken 已确认 BK7258 SDK 尚未适配 Secure Boot、且不会提供缺失材料，公开
normal/A-B bootloader 的逆向与未发布 Secure-Boot 链必须分开处理。

在公开 raw 启动头、32+2 CRC 和 SRAM BL2 handoff 的可验证约束之上，项目采用一个
**自研**的最小 BL1 Manifest：

- 固定为 boot 逻辑分区末尾的 256 字节记录；
- 绑定固定 BL2 XIP 地址、逻辑长度、SRAM 加载地址和 SHA-256；
- 用编入 BL1 的 ECDSA-P256 公钥验证；
- BL1 成功验证后才复制 BL2 到 SRAM；
- BL2 继续使用 pinned NuttX MCUboot 验证 CP/AP 镜像。

当前编码版本为 format 2：`BKBL1M2\0`、ECDSA-P256/SHA-256 算法字段、BL2
摘要、公钥哈希、公钥 X||Y 和 `0xb0` 位置的 `r||s` 签名。它是项目自定义的
可恢复记录，不是从 BK7258 BootROM 逆出的字节格式。format 2 已通过板端
BL1→BL2→MCUboot→NuttShell 路径；证据见
[v2 handoff board record](../../progress/verification/2026-08-07-bl1-v2-manifest-bl2-handoff-board.md)。

该记录不读取或写入 N15/N17 生命周期数据，不改变 SDK、NuttX 或 apps，不操作
OTP/eFuse。私钥不得进入仓库、日志、固件或项目记忆。

## Consequence

这是一条可恢复的软件信任链：攻击者若能整体替换 Flash 上的 BL1，仍可替换公钥和
Manifest。它不能称为硬件 Secure Boot，也不能称为 Beken 官方 Manifest 的逆向结果。

NuttX `imgtool.py` 继续签 MCUboot CP/AP image；由于 BL1 Manifest 不是 MCUboot
image，板级 `make_bl1_manifest.py` 仅负责该固定记录的 OpenSSL ECDSA 编码和 DER 到
`r || s` 的转换。它是不可由 imgtool 替代的最小板级补充。

默认构建保持 `BL1_MANIFEST_ENFORCE=0`。只有提供外部签名的 256 字节记录并显式设为
`1` 才会强制验证，因此不会意外改变既有可恢复开发板的启动行为。

BL2 最终交接还必须按目标 CP image 的 linker RAM ABI 恢复 `MSPLIM`。官方公开
直启 bootloader 的 `0x2802f800` 是其应用布局的边界；本项目 MCUboot CP image
从 `0x28010000` 分配 RAM，因此最终跳转采用 `0x28010000`，避免初始 MSP
`0x280146c0` 触发 ARMv8-M `STKOF`。
