# BK7258 自有 BL1 Manifest 拒绝与恢复板测

日期：2026-08-07

## 结论

自有、可恢复的软件信任链已经完成实板闭环：BL1 先验证 boot 逻辑尾部的
256 字节 Manifest，再把已授权的 BL2 复制到 SRAM。正确 Manifest 冷启动到
`BL2RAM` 与 NuttShell；只翻转签名中的一个 bit（但重新生成完整 32+2 CRC
物理镜像）时，BL1 明确拒绝并不启动 BL2；写回正确 boot 后，CP、AP 与 AP
SMP secondary 都恢复 READY。

这不是 Beken 官方 Secure Boot 或其未公开 Manifest ABI 的逆向结论，也没有写入
OTP/eFuse。唯一写入范围始终是 boot 物理分区 `0x000000..0x010fff`
（`0x11000` 字节）；BL2、CP、AP、A/B 槽、元数据和 LittleFS 都未写入。

## 固定事实

- BL1 Manifest 位于 boot 的逻辑尾部 `0x0200ff00`，格式为 256 字节：固定
  handoff 字段、BL2 SHA-256、以及 P-256 `r || s` 签名。
- J-Link 从目标 XIP `0x024d0000..0x024d2fff` 的只读导出 SHA-256 为
  `82721318218706a56d4d0c48437580ddc5a010fabb07078d6c8f0687e4ea981a`。
  它与当前项目 BL2 的 12 KiB `0xff` 填充逻辑镜像逐字节一致。早先离线
  解码文件的 `de54...` 摘要属于旧 BL2，不能用于当前 Manifest。
- BL1 使用 NuttX 固定 MCUboot 中的 TinyCrypt `uECC_verify()`；它不修改
  NuttX、apps 或 SDK 源码。该库要求 64 字节 `X || Y` 公钥，私钥始终在受限
  临时位置，未进入仓库、固件或本记录。
- P-256 运算期间，BL1 将 watchdog 临时扩展为 60 秒；调用返回后立即恢复普通
  boot 期限。无效签名仍 fail-closed 并进入短期复位循环，不会转入 BL2。

## 实测证据

1. 当前 BL2 的真实 XIP 摘要和该 Manifest 的 P-256 签名分别经只读导出、
   OpenSSL 与 TinyCrypt 主机验证。强制 Manifest boot 交叉编译通过，最终 ELF
   链接的是 `uECC_verify`，不含旧的 BL1 `ecdsa_verify` 路径。
2. 写入正确的 CRC 封装 boot 后，COM11 的 RTS 冷复位日志为：

   ```text
   u_bootloader enter
   BClk ...
   partition bl2 @ 0x024D0000
   bl2 ram @ 0x28020000
   BL2RAM
   NuttShell (NSH)
   nsh>
   ```

3. 只将 Manifest 签名第一个字节的最低 bit 翻转；前 64 字节授权内容、BL2
   摘要和其余填充不变，并对整个 boot 重新做 32+2 CRC 编码。冷复位输出：

   ```text
   partition bl2 @ 0x024D0000
   bl1 manifest rc 0x00000003
   BAD
   bl2 manifest
   ```

   `rc 3` 是 P-256 签名拒绝；该状态没有 `BL2RAM` 或 NSH，证明拒绝发生在
   BL2 SRAM 复制与交接之前。
4. 立即写回步骤 2 的正确强制 Manifest boot。再次 RTS 冷复位重获 `BL2RAM`
   与 NSH；随后 `apctl status` 报告 AP `READY(2)`、CPU2
   `SECONDARY_READY(7)`、AP 初始 vector `0x02150200`。

## 边界

- 这是项目自有的软件根：可整体替换 Flash 的攻击者仍可替换 BL1 与公钥；不能
  宣称硬件 Secure Boot 或硬件防回滚。
- BL1 只授权 BL2；BL2 继续使用 pinned NuttX MCUboot 验证 CP/AP 成对镜像。
- 本次不触及 N17 metadata、format-3 journal、策略扇区或 counter floor；它们
  仍不处于 armed 状态。
