# BK7258 T5 最终 App 回归 v8 —— 下载与运行验收

Date: 2026-08-17

## 结论

**FLASH_READBACK_PASS / SWD-RTT BOOT_PASS。** 基于当前未提交工作树（`feat/bk7258-app-config-decouple`）的 v8 签名固件已通过 COM3 五段稀疏下载、逐段回读（SHA-256 逐字节一致）、BL2 release 与 RTT NuttShell 启动验收。

## 交付物

- 构建根：`/tmp/bk7258-final-app-regression-v8`
- `firmware.bkpack` SHA-256：`e3f47d9593d3e700eb8821aadf6f8499c78e9758afab526aca1a0a972aed4457`
- 布局：`bk7258-v3119-ab-124ebfab37ca1fcd` / `124ebfab37ca1fcd9971c5aba7b9f214f0500df74cdc394c88ec602020732d8a`
- MCUboot：`18.7.1`，security counter `0x12060053`
- 可移植包目录：`/tmp/bk7258-t5board-final-v8-portable`
- 硬件日志：`/tmp/bk7258-final-hw-20260817/20260817-095950`

## 下载与回读

`bk7258_auto_debug.sh --flash --sparse-flash --no-console` 通过 J-Link 信任预检（BL1/BL2 指纹匹配）后，由 `bk_loader.exe` 经 COM3 写入五段：

| 段 | 物理范围 | 长度 | 回读 SHA-256（与候选一致） |
|---|---:|---:|---|
| BL1 | `[0x000000,0x011000)` | `0x11000` | `6e677b53ceb7b8be1cc3e294e0dc52ab5fd403a12f91ae5fa093c5fcde7c4345` |
| BL2 主 | `[0x51d000,0x521000)` | `0x4000` | `0dc104ac9ac8f25f3de3d0971e1a9f4867db23147c2056f2778363ee1dc1a490` |
| BL2 副 | `[0x53f000,0x543000)` | `0x4000` | `0dc104ac9ac8f25f3de3d0971e1a9f4867db23147c2056f2778363ee1dc1a490` |
| CP A | `[0x011000,0x04f000)` | `0x3e000` | `297a00bcde3a8930d92f5836f5285acbfbd6d0735bdb265edea39bec71a76cd3` |
| AP A | `[0x165000,0x192000)` | `0x2d000` | `5bd4b43ae895f211e96b29c7947b9333affd408dcbde3d39e8166fc141fc68cc` |

写后回读（`bk_loader.exe read`，460800）五段均与候选文件 SHA-256 完全一致；LittleFS/用户配置/校准尾保留区未写。

## 启动验收

回读后 CPU 处于 BL2 boot-hold（`VTOR=0x28020000`，hold magic=0）。写入已定义 release magic `0x4A4C4E4B` 至 `0x2809F7F0` 后：

- `VTOR` 切到 `0x28010800`（CP 应用 SRAM 向量表）；
- `0x2809F000` 区域出现 `ABPS`、`0x2809F180` 区域出现 `CPU2`、`0x28097000` 区域出现 `RPTR`、`0x2804F800` 区域出现 `SWDT` 运行态结构；
- `0x2809F1B4 = 0x5A`（CPU2/AP ready 标志）。

J-Link RTT Logger（`RTTAddress=0x28012b10`，Channel 0）捕获到：

```
NuttShell (NSH)
nsh>
```

## 本轮修复

- 隔离交付的 `pack_dual_image` 启动段成员名统一回既有 `bl_crc.bin` 契约（`bk7258_auto_debug.sh` 与 `verify_bk7258_factory_layout.py` 均按此名校验）；payload 内部仍保留 `bootloader_crc.bin`。
- 签名阶段隔离子进程的 `PYTHONPATH` 透传本机用户 site-packages，解决 `cbor2` 不可见导致的 MCUboot pair 签名失败（运行期显式传入，不写入仓库）。
- 最终回归树：157 passed / 1 skipped；`framework-check` PASS；`git diff --check` 干净。

## 边界

- 未提交/推送；私钥未进仓；未写 OTP/eFuse；LittleFS 与用户数据保持。
