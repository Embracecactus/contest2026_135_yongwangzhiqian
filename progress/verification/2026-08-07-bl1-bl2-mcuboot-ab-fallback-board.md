# BK7258 自有 BL1/BL2 MCUboot CP/AP A/B 回退板测

日期：2026-08-07

## 结论

已在 T5-AI 实板验证一条**自有且可恢复**的软件启动链：启用自有 BL1
Manifest 后，BL1 验证并载入 SRAM BL2；该 BL2 使用 pinned NuttX MCUboot
bootutil 验证 CP/AP 成对镜像。当主槽 A 的 CP ECDSA 签名被故意破坏时，BL2
拒绝 A、打开 B 槽 Flash remap，B 槽 CP 随后释放 AP。AP 与 AP SMP secondary
均报告 READY。恢复 A 后，remap 关闭，CP/AP/CPU2 均重新从 A 正常启动。

这不是 Beken 官方 Secure Boot 或官方 Manifest ABI 的逆向结论；它也没有写入
OTP/eFuse。它是 ADR-016 定义的、项目自行拥有的可恢复软件信任链。

## 本次固定的启动组成

| 部件 | 位置或配置 | 本次状态 |
| --- | --- | --- |
| 自有 BL1 Manifest | boot 逻辑末尾 `0x0200ff00`，256 字节 | 已启用，绑定当前 BL2 的 P-256 签名与 SHA-256 |
| BL2 | Flash 原始地址 `0x51d000`；BL1 复制窗口 `0x3000` | bare-metal NuttX MCUboot，`MCUBOOT_IMAGE_NUMBER=2` |
| 主槽 A | CP `0x011000`，AP `0x165000` | EC256 签名的 v18.1.1 成对镜像；最终为活动槽 |
| 备用槽 B | CP `0x286000`，AP `0x3da000` | 同一 v18.1.1 成对镜像；用于本次真实回退 |

所有可执行 Flash 输入均按 BK7258 的 32 字节数据加 2 字节 CRC 物理编码写入。
签名封装使用仓库固定的 NuttX `imgtool.py`；私钥只存在于受限临时位置，未进入
仓库、固件、日志或本记录。

## 证据步骤

1. 当前 CP/AP v18.1.1 镜像在主机侧用 NuttX `imgtool verify` 分别通过。BL2 源码配置为
   两个 MCUboot image：image 0 对应 CP A/B，image 1 对应 AP A/B；选中 CP 前还会
   检查同一槽 AP 的向量表。
2. MCUboot 镜像头是 `0x200` 字节，AP 的真实 Cortex-M 向量表因此在
   `0x02150200`。CP 的 CPU1 release 寄存器也必须使用该向量地址除以 256，即
   `0x00021502`，不能使用含 MCUboot 头的 `0x00021500`。该板级修复不修改
   SDK、NuttX 或 apps 源码。
3. 将 A 槽 CP/AP 以及 B 槽 CP/AP 写入实板。正常 A 启动时，COM11 `apctl status`
   报告 AP `READY`、`VTOR(init/run)=02150200/28050800`、有效 AP heap test，及
   CPU2 `SECONDARY_READY`。这证明完整运行顺序已到达
   BL1 → BL2 → CP → CPU1/AP → CPU2。
4. 仅修改 A 槽 CP 的已签名负载一个字节，再重新做 32+2 CRC 编码。主机侧
   `imgtool verify` 因签名不匹配失败，证明该输入确为无效签名镜像。
5. 只覆盖 A 槽 CP 的该无效物理范围并复位。约十余秒后，J-Link 读取 remap 寄存器
   `0x44030058` 得到：

   ```text
   0x02010000  0x02260000  0x02250000  0x00000001
   ```

   最后一项 bit 0 为 1，说明 B 槽偏移映射已启用。此时 COM11 `apctl status` 仍
   报告 AP `READY` 和 CPU2 `SECONDARY_READY`，构成“无效 A 被拒绝、实际从 B
   启动并完整释放 AP”的实机证据。
6. 立即写回有效 A 槽 CP。再次启动后，寄存器为：

   ```text
   0x00010000  0x00100000  0x00000000  0x00000000
   ```

   remap enable 已清零，COM11 `apctl status` 再次报告 AP `READY` 和 CPU2
   `SECONDARY_READY`。最终板子保持 A 为默认活动槽，B 保留有效 v18.1.1 成对镜像。

## 边界与限制

- 软件 P-256 验证会使冷启动在串口输出前持续十余秒；这不是早期串口采样为空的失败证据。
- 本次证明 CP/AP 镜像作为一个 MCUboot 认证选择单元、无效 A 到 B 的选择、CP 对 AP
  的运行时释放，以及 AP SMP secondary bootstrap。它不单独证明 `apctl` 的 stop/restart
  生命周期、RPMsg 服务或 OTA 元数据状态机。
- N15 的 trial/confirmed 元数据和 N17 的 Manifest、format-3 journal、counter floor
  均未在本次过程中写入或启用；因此不能据此宣称 N17 已 armed，也不能据此宣称完整
  OTA 生命周期已经接通。
- 未证明、也不猜测 Beken 未公开的 BootROM/官方 BL1 Manifest/OTP 授权格式。

## 后续主线

以该已验证的可恢复 BL1/BL2/MCUboot 基座回到 N17：先定义 MCUboot 的成对槽选择与
N15/N17 生命周期状态之间的唯一权威，防止两个独立选择器产生分裂状态；任何元数据或
不可逆硬件信任根写入仍需单独授权。
