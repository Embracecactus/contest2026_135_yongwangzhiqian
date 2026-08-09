# 逆向尝试资产盘点（N17+，只读、可恢复）

Last updated: 2026-08-08
Owner: 逆向验证（CodeBuddy 只读核查，不改代码）

本文件是「继续逆向 BK7258 官方启动行为」尝试的 **P0 资产盘点**，与
`reverse-synthesis-N17.md` 配套。所有动作均为只读：读 bin、读 SDK 头、
读已有结论、读板上读出记录，不烧写、不改 nuttx/SDK/官方源码。

## 1. 可用官方 / 板上二进制资产

| 资产 | 路径 | 大小 | 性质 |
|---|---|---|---|
| 官方 BL1（normal bootloader） | `/mnt/c/Users/lijian/Downloads/bk7258_normal_bootloader.bin` | 43744 B | 官方出厂 BL1 镜像 |
| 官方 BL2（板上读出 2026-08-07） | `/mnt/c/Users/lijian/Downloads/bk7258-bl2-xip-read-20260807.bin` | 12288 B | J-Link 从板读出官方 BL2 XIP |
| 自有 BL1 构建 | `board/bk7258/bootloader/bl.bin` | 23444 B | 自有 BL1 构建产物 |
| 自有 BL2 构建 | `board/bk7258/bootloader/bl2/bl2.bin` | 10708 B | 自有 NuttX MCUboot BL2 |
| 自有 BL1 CRC 产物 | `board/bk7258/bootloader/bl_crc.bin` | 69632 B (0x11000) | CRC 块对齐打包产物 |
| 自有 BL2 CRC 产物 | `board/bk7258/bootloader/bl2/bl2_crc.bin` | 13056 B | CRC 块对齐打包产物 |
| 板上验证 BL2 | `logs/bk7258-auto-debug/20260808-101553/validated-bl2-crc.bin` | 16384 B | 独立 RTS 复位验证通过的 BL2 |

注：`/mnt/c/Users/lijian/Downloads/bk7258-bl2-xip.bin` 与
`bk7258-board-bl2-xip.bin` 为 **0 字节空文件**，不可用；有效官方 BL2 以
`bk7258-bl2-xip-read-20260807.bin`（板上读出）为准。`BK7258_SMP` 目录内无
`.bin`，不直接提供官方镜像。

## 2. 已有逆向结论文档（去重对齐）

- `docs/bk7258-t5ai/bootloader/bk-official-bootloader-reverse.md`（908 行）— 官方 BL 主逆向
- `docs/bk7258-t5ai/bootloader/full-reverse-synthesis.md`（172 行）— 综合合成
- `docs/bk7258-t5ai/bootloader/tuya-bootloader-reverse.md`（761 行）— 第三方对比
- `docs/bk7258-t5ai/bootloader/vendor-bootloader-comparison.md`（67 行）— vendor 对比
- `docs/bk7258-t5ai/nuttx-port/n17-signed-manifest-abi.md` — **已验证 BK7258 Manifest ABI**（security_counter @0x020、slot A @0x011000、slot B @0x286000）
- `docs/bk7258-t5ai/probe/README.md` — 探针验证（含官方 `"BK7236\0\0"` 魔数证据）

## 3. Ghidra 工程（可复用，不新建）

- `/tmp/ghidra-bk7258`、`/tmp/ghidra-bk7258-ab`、`/tmp/bk7258-ab-deep-ghidra`、
  `/tmp/bk7258-normal-deep-ghidra` — 既有 BK7258 官方 / AB 启动反汇编工程。

## 4. SDK 只读硬件证据源

`board/bk7258/bk_idk/armino_as_lib/versions/v3.1.1.9/cp/include/soc/bk7258/reg_base.h`
提供已验证 BK7258 地址：
- `SOC_AON_WDT_REG_BASE = 0x44000600`
- `SOC_OTP_REG_BASE = 0x4b100000`
- `SOC_OTP_AHB_BASE / APB_BASE = 0x4b010000 / 0x4b100000`
- `SOC_EFUSE_REG_BASE = 0x44880000`
- `SOC_MPC_OTP_REG_BASE = 0x41130000`

这些地址证明：启动链中引用的外设 / OTP 基址是 **BK7258 事实**，非 BK7236 遗留。

## 5. 红线约束（本次尝试全程遵守）

- 不修改 nuttx / SDK / 官方源码（仅临时调试探针，且历史已回退，当前工作区干净）。
- 不烧写 OTP / eFuse，不开启 secure boot，不做 lifecycle 切换 / debug lock。
- 仅产出结论文档，不改动任何构建产物或源码。
